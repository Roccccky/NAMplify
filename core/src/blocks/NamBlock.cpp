#include "namplify/blocks/NamBlock.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace nmp
{

void NamBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;
    maxBlock   = info.maxBlockSize;

    mono   .assign ((size_t) maxBlock, 0.0f);
    monoOld.assign ((size_t) maxBlock, 0.0f);

    for (int f = 0; f < 2; ++f)
        for (auto& a : dryAlign[f]) a.prepare (f == 0 ? 2 : 4, maxBlock);
    dryFast.assign ((size_t) maxBlock * 4, 0.0f);

    swapFade.reset (sampleRate, 0.030);
    swapFade.setCurrentAndTarget (1.0f);

    preGain .reset (sampleRate, 0.03);
    postGain.reset (sampleRate, 0.03);
    volGain .reset (sampleRate, 0.03);
    mixSm   .reset (sampleRate, 0.02);

    preGain .setCurrentAndTarget (1.0f);
    postGain.setCurrentAndTarget (1.0f);
    volGain .setCurrentAndTarget (1.0f);
    mixSm   .setCurrentAndTarget (mix);

    if (active)   active  ->prepare (sampleRate, maxBlock);
    if (activeIr) activeIr->prepare (maxBlock);

    toneDirty = true;
    rebuildToneStack();
    reset();
}

void NamBlock::reset()
{
    if (active)   active->reset();
    if (outgoing) outgoing->reset();
    if (activeIr) activeIr->reset();
    toneBass.reset(); toneMid.reset(); toneTreble.reset();
    swapping = false;
    swapFade.setCurrentAndTarget (1.0f);
}

void NamBlock::onEnable()
{
    // Beim Einschalten nur den Zustand zurücksetzen, das Modell bleibt.
    if (active)   active->reset();
    if (activeIr) activeIr->reset();
    toneBass.reset(); toneMid.reset(); toneTreble.reset();
}

int NamBlock::latencySamples() const
{
    if (active) return active->latencySamples();
    return 0;
}

void NamBlock::setToneStack (float b, float m, float t)
{
    if (b == toneBassDb && m == toneMidDb && t == toneTrebleDb) return;
    toneBassDb = clamp (b, -12.0f, 12.0f);
    toneMidDb  = clamp (m, -12.0f, 12.0f);
    toneTrebleDb = clamp (t, -12.0f, 12.0f);
    toneDirty = true;
}

void NamBlock::rebuildToneStack()
{
    if (! toneDirty) return;
    toneDirty = false;

    // Klangregelung hinter dem Netz. Davor würde sie die Verzerrung verändern
    // statt den Klang.
    toneBass  .setCoeffs (designLowShelf  (120.0,  0.72, toneBassDb,   sampleRate));
    toneMid   .setCoeffs (designBell      (650.0,  0.80, toneMidDb,    sampleRate));
    toneTreble.setCoeffs (designHighShelf (3000.0, 0.70, toneTrebleDb, sampleRate));
}

void NamBlock::setLite (bool lite)
{
    liteWanted = lite;
    if (active) active->setLite (lite);
}

LoudnessMeasurement NamBlock::measurement() const
{
    return published ? publishedInfo.measurement : LoudnessMeasurement{};
}

void NamBlock::publishModel (std::unique_ptr<NamModel> m)
{
    if (! m) return;
    m->prepare (sampleRate, maxBlock);
    m->setLite (liteWanted);

    publishedInfo = m->info();
    published = true;

    pendingOwner = std::move (m);
    pendingModel.store (pendingOwner.get(), std::memory_order_release);
}

void NamBlock::publishIr (std::vector<float> samples)
{
    auto c = std::make_unique<Convolver>();
    c->prepare (maxBlock);
    c->setImpulseResponse (samples.data(), (int) samples.size());

    published = false;
    publishedInfo = NamModelInfo{};

    pendingIrOwner = std::move (c);
    pendingIr.store (pendingIrOwner.get(), std::memory_order_release);
}

void NamBlock::requestClear()
{
    published = false;
    publishedInfo = NamModelInfo{};
    clearRequested.store (true, std::memory_order_release);
}

void NamBlock::commitPending()
{
    if (clearRequested.exchange (false, std::memory_order_acquire))
    {
        if (active)   retired  = std::move (active);
        if (activeIr) retiredIr = std::move (activeIr);
        outgoing.reset();
        swapping = false;
        swapFade.setCurrentAndTarget (1.0f);
    }

    if (pendingModel.exchange (nullptr, std::memory_order_acquire) != nullptr)
    {
        if (outgoing) retired = std::move (outgoing);
        outgoing = std::move (active);
        active   = std::move (pendingOwner);
        if (activeIr) retiredIr = std::move (activeIr);

        swapping = outgoing != nullptr;
        swapFade.setCurrentAndTarget (swapping ? 0.0f : 1.0f);
        swapFade.setTarget (1.0f);
    }

    if (pendingIr.exchange (nullptr, std::memory_order_acquire) != nullptr)
    {
        if (activeIr) retiredIr = std::move (activeIr);
        activeIr = std::move (pendingIrOwner);
        if (active) { retired = std::move (active); }
        outgoing.reset();
        swapping = false;
        swapFade.setCurrentAndTarget (1.0f);
    }
}

void NamBlock::alignDry (StereoBlock& block, int factor)
{
    auto& pair = dryAlign[factor >= 4 ? 1 : 0];
    for (int c = 0; c < 2; ++c)
    {
        float* x = block.channel (c);
        for (int done = 0; done < block.numFrames;)
        {
            const int m = std::min (block.numFrames - done, maxBlock);
            pair[c].upsample (x + done, m, dryFast.data());
            pair[c].downsample (dryFast.data(), m, x + done);
            done += m;
        }
    }
}

void NamBlock::reconcileLite()
{
    if (active && active->isLite() != liteWanted) active->setLite (liteWanted);
}

void NamBlock::collectRetired()
{
    retired.reset();
    retiredIr.reset();
}

void NamBlock::process (StereoBlock& block, const BlockContext&)
{
    commitPending();
    rebuildToneStack();

    const int n = block.numFrames;
    if ((int) mono.size() < n) { mono.resize ((size_t) n); monoOld.resize ((size_t) n); }

    const bool haveModel = active != nullptr;
    const bool haveIr    = activeIr != nullptr && activeIr->hasIr();

    if (! haveModel && ! haveIr) return;

    // Eingangspegel. Ein Capture ist nicht linear, der Pegel ändert den Klang.
    // Ist der Eingangspegel in der Datei angegeben, wird die Differenz zum
    // Referenzpegel des Interfaces davor gesetzt, sonst bleibt es bei 1.
    float preDb = inputDb, postDb = 0.0f;

    if (haveModel)
    {
        const auto& info = active->info();
        if (info.hasInputLevel)
            preDb += interfaceDbu - (float) info.inputLevelDbu;

        // Ausgangspegel:
        // RAW         Ausgang des Netzes wie trainiert
        // CALIBRATED  Pegel des echten Geräts: Ausgangspegel der Datei gegen
        // den Referenzpegel des Interfaces, wie im NAM-Plugin. Ohne Angabe in
        // der Datei wie RAW.
        // NORMALIZED  auf gleiche Lautheit gemessen
        if (outputMode == Output::normalized && info.measurement.valid)
            postDb += (float) info.measurement.trimDb;
        else if (outputMode == Output::calibrated && info.hasOutputLevel)
            postDb += (float) info.outputLevelDbu - interfaceDbu;
    }

    preGain .setTarget (dbToGain (preDb));
    postGain.setTarget (dbToGain (postDb));
    volGain .setTarget (dbToGain (levelDb));
    mixSm   .setTarget (mix);

    // mono
    for (int i = 0; i < n; ++i)
        mono[(size_t) i] = 0.5f * (block.left[i] + block.right[i]);

    // Der trockene Weg bleibt stereo.
    const bool blending = mix < 0.999f;

    if (swapping) std::copy (mono.begin(), mono.begin() + n, monoOld.begin());

    for (int i = 0; i < n; ++i) mono[(size_t) i] *= preGain.next();

    if (haveModel) active->process (mono.data(), n);
    else           activeIr->process (mono.data(), n);

    if (swapping && outgoing)
    {
        for (int i = 0; i < n; ++i) monoOld[(size_t) i] *= preGain.getCurrent();
        outgoing->process (monoOld.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float t = swapFade.next();
            mono[(size_t) i] = mono[(size_t) i] * t + monoOld[(size_t) i] * (1.0f - t);
        }

        if (! swapFade.isSmoothing())
        {
            swapping = false;
            retired = std::move (outgoing);
        }
    }

    // Klangregelung hinter dem Netz (AMP und FULL RIG)
    if (role == Role::amp || role == Role::fullrig)
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] = toneTreble.process (toneMid.process (toneBass.process (mono[(size_t) i])));

    if (haveModel && active->info().oversampling > 1)
        alignDry (block, active->info().oversampling);

    for (int i = 0; i < n; ++i)
    {
        const float wet = mono[(size_t) i] * postGain.next();
        const float m   = blending ? mixSm.next() : 1.0f;
        const float vol = volGain.next();

        block.left[i]  = (block.left[i]  * (1.0f - m) + wet * m) * vol;
        block.right[i] = (block.right[i] * (1.0f - m) + wet * m) * vol;
    }
}

} // namespace nmp
