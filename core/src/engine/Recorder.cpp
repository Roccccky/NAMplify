#include "namplify/engine/Recorder.h"
#include "namplify/model/WavFile.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nmp
{

void Recorder::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;
    chunkLength = (int) (sampleRate * kChunkSeconds);
    peakStride  = std::max (1, chunkLength * kMaxChunks / kPeakPoints);

    chunks.clear();
    chunks.resize ((size_t) kMaxChunks);
    readyChunks.store (0);
    wantChunks.store (2);      // zwei Blöcke im Voraus

    ballL.reset (sampleRate, 0.004, 0.25);
    ballR.reset (sampleRate, 0.004, 0.25);

    fade.reset (sampleRate, 0.006);
    fade.setCurrentAndTarget (0.0f);

    serviceAllocations();
    reset();
}

void Recorder::reset()
{
    pos = 0;
    recordedLength.store (0);
    stateAtomic.store ((int) RecorderState::empty);
    full.store (false);
    uiPos.store (0.0);
    std::fill (peaks, peaks + kPeakPoints, 0.0f);
    fade.setCurrentAndTarget (0.0f);
}

void Recorder::serviceAllocations()
{
    const int want = clamp (wantChunks.load (std::memory_order_relaxed), 0, kMaxChunks);
    int ready = readyChunks.load (std::memory_order_relaxed);

    while (ready < want)
    {
        auto c = std::make_unique<Chunk>();
        c->left .assign ((size_t) chunkLength, 0.0f);
        c->right.assign ((size_t) chunkLength, 0.0f);
        chunks[(size_t) ready] = std::move (c);
        ++ready;
        readyChunks.store (ready, std::memory_order_release);
    }
}

void Recorder::setEnabled (bool on)
{
    if (on == enabled) return;
    enabled = on;

    // Wird die Karte entfernt, stoppt der Recorder.
    if (! enabled && state() != RecorderState::empty)
        stateAtomic.store ((int) RecorderState::stopped);
}

void Recorder::tap()
{
    switch (state())
    {
        case RecorderState::empty:
            pos = 0;
            recordedLength.store (0);
            full.store (false);
            std::fill (peaks, peaks + kPeakPoints, 0.0f);
            wantChunks.store (2);
            stateAtomic.store ((int) RecorderState::recording);
            fade.setCurrentAndTarget (0.0f);
            fade.setTarget (1.0f);
            break;

        case RecorderState::recording:
            stateAtomic.store ((int) RecorderState::stopped);
            fade.setTarget (0.0f);
            break;

        case RecorderState::stopped:
            pos = 0;
            stateAtomic.store ((int) RecorderState::playing);
            fade.setCurrentAndTarget (0.0f);
            fade.setTarget (1.0f);
            break;

        case RecorderState::playing:
        default:
            stateAtomic.store ((int) RecorderState::stopped);
            fade.setTarget (0.0f);
            break;
    }
}

void Recorder::erase()
{
    stateAtomic.store ((int) RecorderState::empty);
    pos = 0;
    recordedLength.store (0);
    full.store (false);
    std::fill (peaks, peaks + kPeakPoints, 0.0f);
    fade.setCurrentAndTarget (0.0f);
}

double Recorder::lengthSeconds() const
{
    return (double) recordedLength.load (std::memory_order_relaxed) / sampleRate;
}

void Recorder::process (StereoBlock& block, const BlockContext&)
{
    const auto st = state();

    // Die Pegelanzeige läuft immer und zeigt den Ausgang.
    {
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < block.numFrames; ++i)
        {
            pl = std::max (pl, std::abs (block.left[i]));
            pr = std::max (pr, std::abs (block.right[i]));
        }
        meterL.store (ballL.pushBlock (pl, block.numFrames), std::memory_order_relaxed);
        meterR.store (ballR.pushBlock (pr, block.numFrames), std::memory_order_relaxed);
    }

    if (st == RecorderState::empty || st == RecorderState::stopped) return;

    const int ready = readyChunks.load (std::memory_order_acquire);
    const int capacity = ready * chunkLength;

    if (st == RecorderState::recording)
    {
        for (int i = 0; i < block.numFrames; ++i)
        {
            if (pos >= capacity)
            {
                // Kein vorbereiteter Speicher mehr: anhalten statt im
                // Audio-Thread zu allozieren.
                if (ready >= kMaxChunks) { full.store (true); stateAtomic.store ((int) RecorderState::stopped); }
                break;
            }

            const int c = pos / chunkLength;
            const int o = pos % chunkLength;
            const float g = fade.next();

            chan (c, 0)[o] = block.left[i]  * g;
            chan (c, 1)[o] = block.right[i] * g;

            const int bucket = clamp (pos / peakStride, 0, kPeakPoints - 1);
            peaks[bucket] = std::max (peaks[bucket],
                                      std::max (std::abs (block.left[i]), std::abs (block.right[i])));
            ++pos;
        }

        recordedLength.store (pos, std::memory_order_relaxed);

        // Rechtzeitig Nachschub anfordern, der Message-Thread legt ihn an.
        const int used = pos / chunkLength;
        wantChunks.store (clamp (used + 2, 2, kMaxChunks), std::memory_order_relaxed);

        uiPos.store (capacity > 0 ? (double) pos / capacity : 0.0, std::memory_order_relaxed);
        return;
    }

    // Abspielen: die Aufnahme wird dazugemischt, man kann dazu weiterspielen.
    const int len = recordedLength.load (std::memory_order_relaxed);
    if (len <= 0) { stateAtomic.store ((int) RecorderState::stopped); return; }

    for (int i = 0; i < block.numFrames; ++i)
    {
        if (pos >= len)
        {
            stateAtomic.store ((int) RecorderState::stopped);
            fade.setCurrentAndTarget (0.0f);
            break;
        }

        const int c = pos / chunkLength;
        const int o = pos % chunkLength;
        if (c >= ready) break;

        const float g = fade.next();
        block.left[i]  += chan (c, 0)[o] * g;
        block.right[i] += chan (c, 1)[o] * g;
        ++pos;
    }

    uiPos.store (len > 0 ? (double) pos / len : 0.0, std::memory_order_relaxed);
}

void Recorder::waveform (float* out, int numPoints) const
{
    if (out == nullptr || numPoints <= 0) return;

    const int len = recordedLength.load (std::memory_order_relaxed);
    const int used = len > 0 ? clamp (len / peakStride + 1, 1, kPeakPoints) : 0;

    for (int k = 0; k < numPoints; ++k)
    {
        if (used <= 0) { out[k] = 0.0f; continue; }
        const int src = clamp (k * used / numPoints, 0, kPeakPoints - 1);
        out[k] = peaks[src];
    }
}

bool Recorder::save (const std::filesystem::path& path, std::string& error) const
{
    const int len = recordedLength.load (std::memory_order_relaxed);
    if (len <= 0) { error = "There is no take on the tape"; return false; }

    std::vector<float> l ((size_t) len), r ((size_t) len);
    const int ready = readyChunks.load (std::memory_order_acquire);

    for (int i = 0; i < len; ++i)
    {
        const int c = i / chunkLength;
        const int o = i % chunkLength;
        if (c >= ready || ! chunks[(size_t) c]) break;
        l[(size_t) i] = chunks[(size_t) c]->left[(size_t) o];
        r[(size_t) i] = chunks[(size_t) c]->right[(size_t) o];
    }

    return writeWav24 (path, l.data(), r.data(), len, sampleRate, error);
}

} // namespace nmp
