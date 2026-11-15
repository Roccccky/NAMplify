#include "namplify/blocks/Cab.h"
#include <algorithm>
#include <memory>

namespace nmp
{

void CabBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;
    maxBlock   = info.maxBlockSize;

    mono   .assign ((size_t) maxBlock, 0.0f);
    monoOld.assign ((size_t) maxBlock, 0.0f);

    swapFade.reset (sampleRate, 0.030);
    swapFade.setCurrentAndTarget (1.0f);

    outGain.reset (sampleRate, 0.02);
    outGain.setCurrentAndTarget (dbToGain (outputDb));

    if (active) active->prepare (maxBlock);
    reset();
}

void CabBlock::reset()
{
    if (active)   active->reset();
    if (incoming) incoming->reset();
    swapping = false;
    swapFade.setCurrentAndTarget (1.0f);
}

void CabBlock::prepareIr (const float* samples, int numSamples)
{
    auto conv = std::make_unique<Convolver>();
    conv->prepare (maxBlock);
    conv->setImpulseResponse (samples, numSamples);

    // Die fertige Faltung wird nur übergeben und erst im Audio-Thread
    // übernommen, damit dort nichts alloziert wird.
    pendingOwner = std::move (conv);
    pending.store (pendingOwner.get(), std::memory_order_release);
}

void CabBlock::commitPendingIr()
{
    Convolver* p = pending.exchange (nullptr, std::memory_order_acquire);
    if (p == nullptr) return;

    // Die bisherige Faltung läuft weiter, bis die Blende fertig ist.
    if (incoming) { retired = std::move (incoming); }
    incoming = std::move (active);
    active   = std::move (pendingOwner);
    (void) p;

    swapping = incoming != nullptr;
    swapFade.setCurrentAndTarget (swapping ? 0.0f : 1.0f);
    swapFade.setTarget (1.0f);
}

void CabBlock::collectRetired()
{
    retired.reset();
}

void CabBlock::process (StereoBlock& block, const BlockContext&)
{
    commitPendingIr();

    if (! active || ! active->hasIr()) return;

    const int n = block.numFrames;
    if ((int) mono.size() < n) { mono.resize ((size_t) n); monoOld.resize ((size_t) n); }

    // Mono
    for (int i = 0; i < n; ++i)
        mono[(size_t) i] = 0.5f * (block.left[i] + block.right[i]);

    if (swapping)
        std::copy (mono.begin(), mono.begin() + n, monoOld.begin());

    active->process (mono.data(), n);

    if (swapping)
    {
        incoming->process (monoOld.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float t = swapFade.next();
            mono[(size_t) i] = mono[(size_t) i] * t + monoOld[(size_t) i] * (1.0f - t);
        }

        if (! swapFade.isSmoothing())
        {
            swapping = false;
            retired  = std::move (incoming);   // wird vom Message-Thread abgeholt
        }
    }

    outGain.setTarget (dbToGain (outputDb));

    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.next();
        const float v = mono[(size_t) i] * g;
        block.left[i]  = v;
        block.right[i] = v;
    }
}

} // namespace nmp
