#include "namplify/dsp/Biquad.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

const char* filterShapeName (FilterShape s)
{
    switch (s)
    {
        case FilterShape::bell:      return "BELL";
        case FilterShape::lowShelf:  return "LOW SHELF";
        case FilterShape::highShelf: return "HIGH SHELF";
        case FilterShape::lowCut:    return "LOW CUT";
        case FilterShape::highCut:   return "HIGH CUT";
        default:                     return "BELL";
    }
}

double BiquadCoeffs::magnitudeAt (double freqHz, double sampleRate) const
{
    const double w = kTwoPi * freqHz / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w);
    const std::complex<double> z2 = z1 * z1;

    const std::complex<double> num = b0 + b1 * z1 + b2 * z2;
    const std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;

    return std::abs (num / den);
}

namespace
{
    /// Frequenz unter Nyquist halten, sonst wird das Filter instabil.
    double clampFreq (double f, double sr)
    {
        return clamp (f, 10.0, sr * 0.49);
    }

    BiquadCoeffs normalise (double b0, double b1, double b2,
                            double a0, double a1, double a2)
    {
        BiquadCoeffs c;
        const double inv = 1.0 / a0;
        c.b0 = b0 * inv; c.b1 = b1 * inv; c.b2 = b2 * inv;
        c.a1 = a1 * inv; c.a2 = a2 * inv;
        return c;
    }
}

BiquadCoeffs designBell (double freq, double q, double gainDb, double sampleRate)
{
    const double w0    = kTwoPi * clampFreq (freq, sampleRate) / sampleRate;
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double alpha = std::sin (w0) / (2.0 * std::max (0.05, q));
    const double cosw  = std::cos (w0);

    return normalise (1.0 + alpha * A, -2.0 * cosw, 1.0 - alpha * A,
                      1.0 + alpha / A, -2.0 * cosw, 1.0 - alpha / A);
}

BiquadCoeffs designBellBandwidth (double freq, double bw, double gainDb, double sampleRate)
{
    const double w0 = kTwoPi * clampFreq (freq, sampleRate) / sampleRate;
    const double A  = std::pow (10.0, gainDb / 40.0);
    const double sinw = std::sin (w0);
    const double cosw = std::cos (w0);

    // alpha = sin(w0) · sinh( ln2/2 · BW · w0/sin(w0) )
    // Der Faktor w0/sin(w0) gleicht die Stauchung durch die bilineare Abbildung
    // aus.
    const double alpha = sinw * std::sinh (0.5 * std::log (2.0) * std::max (0.05, bw)
                                           * (sinw > 1.0e-9 ? w0 / sinw : 1.0));

    return normalise (1.0 + alpha * A, -2.0 * cosw, 1.0 - alpha * A,
                      1.0 + alpha / A, -2.0 * cosw, 1.0 - alpha / A);
}

BiquadCoeffs designLowShelf (double freq, double q, double gainDb, double sampleRate)
{
    const double w0   = kTwoPi * clampFreq (freq, sampleRate) / sampleRate;
    const double A    = std::pow (10.0, gainDb / 40.0);
    const double cosw = std::cos (w0);
    const double sinw = std::sin (w0);
    const double alpha= sinw / (2.0 * std::max (0.05, q));
    const double tsa  = 2.0 * std::sqrt (A) * alpha;

    return normalise (A * ((A + 1.0) - (A - 1.0) * cosw + tsa),
                      2.0 * A * ((A - 1.0) - (A + 1.0) * cosw),
                      A * ((A + 1.0) - (A - 1.0) * cosw - tsa),
                      (A + 1.0) + (A - 1.0) * cosw + tsa,
                      -2.0 * ((A - 1.0) + (A + 1.0) * cosw),
                      (A + 1.0) + (A - 1.0) * cosw - tsa);
}

BiquadCoeffs designHighShelf (double freq, double q, double gainDb, double sampleRate)
{
    const double w0   = kTwoPi * clampFreq (freq, sampleRate) / sampleRate;
    const double A    = std::pow (10.0, gainDb / 40.0);
    const double cosw = std::cos (w0);
    const double sinw = std::sin (w0);
    const double alpha= sinw / (2.0 * std::max (0.05, q));
    const double tsa  = 2.0 * std::sqrt (A) * alpha;

    return normalise (A * ((A + 1.0) + (A - 1.0) * cosw + tsa),
                      -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw),
                      A * ((A + 1.0) + (A - 1.0) * cosw - tsa),
                      (A + 1.0) - (A - 1.0) * cosw + tsa,
                      2.0 * ((A - 1.0) - (A + 1.0) * cosw),
                      (A + 1.0) - (A - 1.0) * cosw - tsa);
}

BiquadCoeffs designHighPass (double freq, double q, double sampleRate)
{
    const double w0    = kTwoPi * clampFreq (freq, sampleRate) / sampleRate;
    const double cosw  = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

    return normalise ((1.0 + cosw) * 0.5, -(1.0 + cosw), (1.0 + cosw) * 0.5,
                      1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

BiquadCoeffs designLowPass (double freq, double q, double sampleRate)
{
    const double w0    = kTwoPi * clampFreq (freq, sampleRate) / sampleRate;
    const double cosw  = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

    return normalise ((1.0 - cosw) * 0.5, 1.0 - cosw, (1.0 - cosw) * 0.5,
                      1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

BiquadCoeffs designAllPass (double freq, double q, double sampleRate)
{
    const double w0    = kTwoPi * clampFreq (freq, sampleRate) / sampleRate;
    const double cosw  = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

    return normalise (1.0 - alpha, -2.0 * cosw, 1.0 + alpha,
                      1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

BiquadCoeffs designNotch (double freq, double q, double sampleRate)
{
    const double w0    = kTwoPi * clampFreq (freq, sampleRate) / sampleRate;
    const double cosw  = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

    return normalise (1.0, -2.0 * cosw, 1.0,
                      1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

BiquadCoeffs designBandPassApprox (double freq, double q, double sampleRate)
{
    const double w0    = kTwoPi * clampFreq (freq, sampleRate) / sampleRate;
    const double cosw  = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

    return normalise (alpha, 0.0, -alpha,
                      1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

BiquadCoeffs designLowCut  (double freq, double q, double sampleRate) { return designHighPass (freq, q, sampleRate); }
BiquadCoeffs designHighCut (double freq, double q, double sampleRate) { return designLowPass  (freq, q, sampleRate); }

BiquadCoeffs designShape (FilterShape shape, double freq, double q, double gainDb, double sampleRate)
{
    switch (shape)
    {
        case FilterShape::bell:      return designBell      (freq, q, gainDb, sampleRate);
        case FilterShape::lowShelf:  return designLowShelf  (freq, q, gainDb, sampleRate);
        case FilterShape::highShelf: return designHighShelf (freq, q, gainDb, sampleRate);
        case FilterShape::lowCut:    return designLowCut    (freq, q, sampleRate);
        case FilterShape::highCut:   return designHighCut   (freq, q, sampleRate);
        default:                     return designBell      (freq, q, gainDb, sampleRate);
    }
}

} // namespace nmp
