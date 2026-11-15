#pragma once
// Fensterfunktionen für Resampler, Faltung und Tests.

#include "../util/Types.h"
#include <cmath>

namespace nmp
{

/// Modifizierte Besselfunktion nullter Ordnung, Reihenentwicklung.
inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double halfX = x * 0.5;
    for (int k = 1; k < 64; ++k)
    {
        term *= (halfX / k) * (halfX / k);
        sum  += term;
        if (term < sum * 1.0e-16) break;
    }
    return sum;
}

/// Kaiser-Fenster über n Punkte. beta 8 ≈ −80 dB Nebenkeulen.
inline double kaiser (int i, int n, double beta)
{
    if (n <= 1) return 1.0;
    const double r = 2.0 * (double) i / (double) (n - 1) - 1.0;
    const double a = 1.0 - r * r;
    return besselI0 (beta * std::sqrt (a < 0.0 ? 0.0 : a)) / besselI0 (beta);
}

inline double sincNorm (double x)
{
    if (std::abs (x) < 1.0e-9) return 1.0;
    const double px = kPi * x;
    return std::sin (px) / px;
}

/// Hann, für Wellenformanzeigen und Blenden im Looper.
inline double hann (int i, int n)
{
    if (n <= 1) return 1.0;
    return 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) (n - 1));
}

} // namespace nmp
