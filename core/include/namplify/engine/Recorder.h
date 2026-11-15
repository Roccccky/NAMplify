#pragma once
// Recorder: nimmt den Ausgang auf, ohne Zyklus, bis man stoppt.
//
// Der Speicher wächst in Blöcken, die der Message-Thread anlegt. Der
// Audio-Thread alloziert nicht.

#include "../blocks/Block.h"
#include "../util/Smoothed.h"
#include <atomic>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

namespace nmp
{

enum class RecorderState : int { empty = 0, recording, playing, stopped };

class Recorder : public Block
{
public:
    static constexpr double kChunkSeconds = 20.0;
    static constexpr int    kMaxChunks    = 90;      ///< 30 Minuten
    static constexpr int    kPeakPoints   = 2048;

    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    /// Tippen: aufnehmen → stoppen → abspielen → stoppen.
    void tap();
    /// Lang drücken.
    void erase();

    void setEnabled (bool on);

    /// Message-Thread. Legt neue Blöcke an, solange Platz gebraucht wird.
    /// Während der Aufnahme regelmäßig aufrufen.
    void serviceAllocations();

    RecorderState state() const { return (RecorderState) stateAtomic.load (std::memory_order_relaxed); }
    double lengthSeconds() const;
    double positionUnit() const { return uiPos.load (std::memory_order_relaxed); }
    bool   hasTake() const { return recordedLength.load (std::memory_order_relaxed) > 0; }
    bool   isFull() const { return full.load (std::memory_order_relaxed); }

    void waveform (float* out, int numPoints) const;
    float levelLeft()  const { return meterL.load (std::memory_order_relaxed); }
    float levelRight() const { return meterR.load (std::memory_order_relaxed); }

    /// Als 24-Bit-Stereo speichern.
    bool save (const std::filesystem::path& path, std::string& error) const;

private:
    struct Chunk { std::vector<float> left, right; };

    inline float* chan (int chunkIndex, int c)
    {
        auto& ch = chunks[(size_t) chunkIndex];
        return c == 0 ? ch->left.data() : ch->right.data();
    }

    double sampleRate = 48000.0;
    int    chunkLength = 0;
    int    peakStride = 1;

    std::vector<std::unique_ptr<Chunk>> chunks;
    std::atomic<int>  readyChunks { 0 };
    std::atomic<int>  wantChunks { 0 };
    std::atomic<int>  recordedLength { 0 };
    std::atomic<int>  stateAtomic { (int) RecorderState::empty };
    std::atomic<bool> full { false };
    std::atomic<double> uiPos { 0.0 };
    std::atomic<float>  meterL { 0.0f }, meterR { 0.0f };

    int   pos = 0;
    bool  enabled = false;
    float peaks[kPeakPoints] = {};

    BallisticMeter ballL, ballR;
    LinearSmoothed fade;      ///< kurze Blende beim Starten und Stoppen
};

} // namespace nmp
