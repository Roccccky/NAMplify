#include "namplify/engine/GlobalEq.h"
#include <cmath>
#include <string>

namespace nmp
{

namespace
{
    constexpr EqBand bell (float f, float g, float q)
    { return { f, g, q, FilterShape::bell }; }

    constexpr EqBand loCut (float f, float q)
    { return { f, 0.0f, q, FilterShape::lowCut }; }

    constexpr EqBand hiCut (float f, float q)
    { return { f, 0.0f, q, FilterShape::highCut }; }

    constexpr EqBand loShelf (float f, float g, float q)
    { return { f, g, q, FilterShape::lowShelf }; }

    constexpr EqBand hiShelf (float f, float g, float q)
    { return { f, g, q, FilterShape::highShelf }; }

    const FactoryCurve kFactory[] =
    {
        { "FLAT",
          "All bands at 0 dB.",
          {{ bell (80.0f, 0.0f, 1.0f),   bell (200.0f, 0.0f, 1.0f),
             bell (500.0f, 0.0f, 1.0f),  bell (1200.0f, 0.0f, 1.0f),
             bell (3000.0f, 0.0f, 1.0f), bell (6000.0f, 0.0f, 1.0f),
             bell (10000.0f, 0.0f, 1.0f) }} },

        { "GUITAR CUT",
          "Rumble off the bottom, hiss off the top.",
          {{ loCut (85.0f, 0.72f),        bell (180.0f, -1.0f, 0.9f),
             bell (420.0f, 0.0f, 1.0f),   bell (1000.0f, 0.0f, 1.0f),
             bell (3400.0f, -2.0f, 1.1f), bell (6000.0f, 0.0f, 1.0f),
             hiCut (8500.0f, 0.72f) }} },

        { "SCOOP",
          "Mids scooped, for distorted rhythm tracks.",
          {{ loShelf (110.0f, 1.5f, 0.7f), bell (250.0f, 0.0f, 1.0f),
             bell (600.0f, -4.0f, 0.9f),   bell (1200.0f, -2.0f, 1.1f),
             bell (3000.0f, 0.0f, 1.0f),   bell (6000.0f, 0.0f, 1.0f),
             hiShelf (7000.0f, 1.5f, 0.7f) }} },

        { "LEAD",
          "Mids pushed forward, so a solo sits above the rest.",
          {{ bell (90.0f, 0.0f, 1.0f),    bell (300.0f, -1.5f, 0.9f),
             bell (700.0f, 0.0f, 1.0f),   bell (1100.0f, 3.5f, 0.8f),
             bell (2600.0f, 1.5f, 1.1f),  bell (6000.0f, 0.0f, 1.0f),
             hiCut (12000.0f, 0.7f) }} },

        { "CLEAN UP",
          "Just the edges trimmed, nothing else.",
          {{ loCut (45.0f, 0.7f),         bell (200.0f, 0.0f, 1.0f),
             bell (500.0f, 0.0f, 1.0f),   bell (1200.0f, 0.0f, 1.0f),
             bell (3000.0f, 0.0f, 1.0f),  bell (6000.0f, 0.0f, 1.0f),
             hiCut (15000.0f, 0.7f) }} },
    };
}

int numFactoryCurves() { return (int) (sizeof (kFactory) / sizeof (kFactory[0])); }

const FactoryCurve& factoryCurve (int index)
{
    return kFactory[clamp (index, 0, numFactoryCurves() - 1)];
}

const FactoryCurve* findFactoryCurve (const std::string& name)
{
    for (int i = 0; i < numFactoryCurves(); ++i)
        if (name == kFactory[i].name) return &kFactory[i];
    return nullptr;
}

void GlobalEq::prepare (double sr)
{
    sampleRate = sr;
    if (current.bands[0].freq <= 0.0f) current = factoryCurve (0).curve;
    dirty = true;
    rebuild();
    reset();
}

void GlobalEq::reset()
{
    for (auto& f : filters) f.reset();
}

void GlobalEq::setCurve (const EqCurve& c)
{
    current = c;
    dirty = true;
}

void GlobalEq::setBand (int index, const EqBand& b)
{
    if (index < 0 || index >= kBands) return;
    current.bands[index] = b;
    dirty = true;
}

void GlobalEq::rebuild()
{
    if (! dirty) return;
    dirty = false;

    for (int i = 0; i < kBands; ++i)
    {
        const auto& b = current.bands[i];
        filters[i].setCoeffs (designShape (b.shape, b.freq, b.q, b.gainDb, sampleRate));
    }
}

void GlobalEq::process (StereoBlock& block)
{
    rebuild();
    if (! enabled) return;

    for (int i = 0; i < kBands; ++i)
    {
        const auto& b = current.bands[i];
        // Bänder ohne Wirkung überspringen.
        if (b.shape == FilterShape::bell && std::abs (b.gainDb) < 0.01f) continue;
        if ((b.shape == FilterShape::lowShelf || b.shape == FilterShape::highShelf)
            && std::abs (b.gainDb) < 0.01f) continue;

        filters[i].processBlock (block);
    }
}

double GlobalEq::magnitudeDbAt (double hz) const
{
    // Produkt der Übertragungsfunktionen, wie tatsächlich gerechnet.
    double mag = 1.0;
    for (int i = 0; i < kBands; ++i)
    {
        const auto& b = current.bands[i];
        const auto co = designShape (b.shape, b.freq, b.q, b.gainDb, sampleRate);
        mag *= co.magnitudeAt (hz, sampleRate);
    }
    return gainToDb (mag);
}

} // namespace nmp
