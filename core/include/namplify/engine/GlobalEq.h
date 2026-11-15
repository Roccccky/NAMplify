#pragma once
// Globaler EQ am Ausgang, zur Anpassung an Kopfhörer oder Abhöre. Gehört nicht
// ins Preset und nicht in die Automation, die Werte speichert das Plugin.
//
// magnitudeDbAt liefert das Produkt der Übertragungsfunktionen aus denselben
// Koeffizienten, mit denen gerechnet wird. Die gezeichnete Kurve entspricht
// damit dem tatsächlichen Frequenzgang, auch wo sich Bänder überlappen.

#include "../util/Types.h"
#include "../dsp/Biquad.h"
#include <string>

namespace nmp
{

struct EqBand
{
    float       freq   = 1000.0f;
    float       gainDb = 0.0f;
    float       q      = 1.0f;
    FilterShape shape  = FilterShape::bell;
};

struct EqCurve
{
    static constexpr int kBands = 7;
    EqBand bands[kBands];
};

/// Mitgelieferte Kurven, bewusst dezent.
struct FactoryCurve { const char* name; const char* description; EqCurve curve; };

int                 numFactoryCurves();
const FactoryCurve& factoryCurve (int index);
const FactoryCurve* findFactoryCurve (const std::string& name);

class GlobalEq
{
public:
    static constexpr int kBands = EqCurve::kBands;

    void prepare (double sampleRate);
    void reset();

    void setEnabled (bool on) { enabled = on; }
    bool isEnabled() const    { return enabled; }

    void setCurve (const EqCurve&);
    void setBand (int index, const EqBand&);
    const EqCurve& curve() const { return current; }

    void process (StereoBlock&);

    /// Tatsächlicher Frequenzgang in dB, für Anzeige und Tests.
    double magnitudeDbAt (double hz) const;

private:
    void rebuild();

    double sampleRate = 48000.0;
    bool   enabled = false;
    bool   dirty = true;

    EqCurve current;
    StereoBiquad filters[kBands];
};

} // namespace nmp
