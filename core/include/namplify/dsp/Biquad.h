#pragma once
// Biquad-Filter und ihre Auslegung. Die UI zeichnet die Kurve des globalen EQ
// mit denselben Formeln (ui/biquad.js).

#include "../util/Types.h"
#include <complex>

namespace nmp
{

/// Bandbreite eines Bandes im 7-Band-EQ in Oktaven. Numerisch so bestimmt, dass
/// zwei Nachbarbänder eine glatte Kurve ergeben (siehe EqTest).
inline constexpr float kGraphicEqBandwidth = 1.34f;

/// Nur noch für Vergleiche, ausgelegt wird über die Bandbreite.
inline constexpr float kGraphicEqQ = 1.05f;

struct BiquadCoeffs
{
    // Normiert auf a0 = 1.
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /// Betrag der Übertragungsfunktion bei einer Frequenz, analytisch
    /// berechnet.
    double magnitudeAt (double freqHz, double sampleRate) const;
};

/// Filtertypen des globalen EQ. Die Reihenfolge wird gespeichert und von der UI
/// verwendet: neue Typen nur hinten anfügen.
enum class FilterShape : int
{
    bell = 0,
    lowShelf,
    highShelf,
    lowCut,
    highCut,
    count
};

const char* filterShapeName (FilterShape s);

// Auslegung nach dem RBJ-Cookbook, in double gerechnet.

BiquadCoeffs designBell      (double freq, double q, double gainDb, double sampleRate);
BiquadCoeffs designLowShelf  (double freq, double q, double gainDb, double sampleRate);
BiquadCoeffs designHighShelf (double freq, double q, double gainDb, double sampleRate);
BiquadCoeffs designLowCut    (double freq, double q,                double sampleRate);
BiquadCoeffs designHighCut   (double freq, double q,                double sampleRate);
BiquadCoeffs designLowPass   (double freq, double q,                double sampleRate);
BiquadCoeffs designHighPass  (double freq, double q,                double sampleRate);
BiquadCoeffs designAllPass   (double freq, double q,                double sampleRate);
BiquadCoeffs designNotch     (double freq, double q,                double sampleRate);
/// Bandpass mit Spitzenverstärkung 1, für die Klangerzeugung der Beat Machine.
BiquadCoeffs designBandPassApprox (double freq, double q,           double sampleRate);

/// Ein Band des globalen EQ nach seinem Typ auslegen.
BiquadCoeffs designShape (FilterShape shape, double freq, double q, double gainDb, double sampleRate);

/// Biquad in transponierter Direktform II.
class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) { co = c; }
    const BiquadCoeffs& coeffs() const     { return co; }

    void reset() { z1 = z2 = 0.0f; }

    inline float process (float x)
    {
        const float y = (float) (co.b0 * x + z1);
        z1 = (float) (co.b1 * x - co.a1 * y + z2);
        z2 = (float) (co.b2 * x - co.a2 * y);
        z1 = flushDenormal (z1);
        z2 = flushDenormal (z2);
        return y;
    }

    void processBlock (float* data, int n)
    {
        for (int i = 0; i < n; ++i) data[i] = process (data[i]);
    }

private:
    BiquadCoeffs co;
    float z1 = 0.0f, z2 = 0.0f;
};

/// Zwei Biquads mit denselben Koeffizienten für links und rechts.
class StereoBiquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) { l.setCoeffs (c); r.setCoeffs (c); }
    const BiquadCoeffs& coeffs() const     { return l.coeffs(); }
    void reset()                           { l.reset(); r.reset(); }

    void processBlock (StereoBlock& b)
    {
        l.processBlock (b.left,  b.numFrames);
        r.processBlock (b.right, b.numFrames);
    }

private:
    Biquad l, r;
};

} // namespace nmp

namespace nmp
{
/// Glocke, ausgelegt über die Bandbreite in Oktaven statt über die Güte. Über
/// die Güte (alpha = sin w0 / 2Q) werden Bänder nahe Nyquist durch die
/// bilineare Abbildung schmaler. Über die Bandbreite sind alle Bänder gleich
/// breit, was der 7-Band-EQ braucht.
BiquadCoeffs designBellBandwidth (double freq, double bandwidthOctaves,
                                  double gainDb, double sampleRate);
}
