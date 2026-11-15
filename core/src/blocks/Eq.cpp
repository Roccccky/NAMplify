#include "namplify/blocks/Eq.h"
#include <algorithm>

namespace nmp
{

void EqBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;

    scratchL.assign ((size_t) info.maxBlockSize, 0.0f);
    scratchR.assign ((size_t) info.maxBlockSize, 0.0f);

    modeFade.reset (sampleRate, 0.020);
    modeFade.setCurrentAndTarget (currentMode == EqMode::threeBand ? 0.0f : 1.0f);

    updateThree();
    for (int i = 0; i < 7; ++i) updateSeven (i);

    reset();
}

void EqBlock::reset()
{
    for (auto& b : three3) b.reset();
    for (auto& b : seven7) b.reset();
}

void EqBlock::setMode (EqMode m)
{
    if (m == currentMode) return;
    currentMode = m;
    modeFade.setTarget (m == EqMode::threeBand ? 0.0f : 1.0f);
}

void EqBlock::setThreeBand (float bassDb, float midDb, float trebleDb)
{
    three[0] = clamp (bassDb,   -12.0f, 12.0f);
    three[1] = clamp (midDb,    -12.0f, 12.0f);
    three[2] = clamp (trebleDb, -12.0f, 12.0f);
    updateThree();
}

void EqBlock::setBand7 (int index, float db)
{
    if (index < 0 || index >= 7) return;
    seven[index] = clamp (db, -12.0f, 12.0f);
    updateSeven (index);
}

void EqBlock::updateThree()
{
    three3[0].setCoeffs (designLowShelf  (kEq3BassHz,   0.7, three[0], sampleRate));
    three3[1].setCoeffs (designBell      (kEq3MidHz,    0.7, three[1], sampleRate));
    three3[2].setCoeffs (designHighShelf (kEq3TrebleHz, 0.7, three[2], sampleRate));
}

void EqBlock::updateSeven (int i)
{
    // Mit kGraphicEqBandwidth ergeben zwei angehobene Nachbarbänder eine glatte
    // Kurve. Ausgelegt über die Bandbreite, damit oben und unten gleich breit
    // sind.
    seven7[i].setCoeffs (designBellBandwidth (kEq7Freqs[i], kGraphicEqBandwidth,
                                              seven[i], sampleRate));
}

double EqBlock::magnitudeAt (double hz) const
{
    double m = 1.0;
    if (currentMode == EqMode::threeBand)
        for (const auto& b : three3) m *= b.coeffs().magnitudeAt (hz, sampleRate);
    else
        for (const auto& b : seven7) m *= b.coeffs().magnitudeAt (hz, sampleRate);
    return m;
}

void EqBlock::process (StereoBlock& block, const BlockContext&)
{
    const int n = block.numFrames;

    if (! modeFade.isSmoothing())
    {
        if (currentMode == EqMode::threeBand)
            for (auto& b : three3) b.processBlock (block);
        else
            for (auto& b : seven7) b.processBlock (block);
        return;
    }

    // Während der Blende laufen beide Modi, damit keiner aus einem kalten
    // Zustand einsetzt.
    if ((int) scratchL.size() < n) { scratchL.resize ((size_t) n); scratchR.resize ((size_t) n); }

    std::copy (block.left,  block.left  + n, scratchL.begin());
    std::copy (block.right, block.right + n, scratchR.begin());

    StereoBlock a { block.left,        block.right,       n };
    StereoBlock b { scratchL.data(),   scratchR.data(),   n };

    for (auto& f : three3) f.processBlock (a);
    for (auto& f : seven7) f.processBlock (b);

    for (int i = 0; i < n; ++i)
    {
        const float t = modeFade.next();
        block.left[i]  = block.left[i]  * (1.0f - t) + scratchL[(size_t) i] * t;
        block.right[i] = block.right[i] * (1.0f - t) + scratchR[(size_t) i] * t;
    }
}

} // namespace nmp
