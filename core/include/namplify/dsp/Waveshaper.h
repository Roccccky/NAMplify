#pragma once
// Kennlinien.
//
// Punktsymmetrische Kennlinien (f(-x) = -f(x)) erzeugen nur ungerade Obertöne
// und klingen hart. Gerade Obertöne entstehen erst, wenn beide Halbwellen
// unterschiedlich behandelt werden. Deshalb sind die meisten Kennlinien hier
// asymmetrisch.

#include "../util/Types.h"
#include <cmath>

namespace nmp
{
namespace shape
{

/// Weiche, symmetrische Begrenzung. Billiger als tanh und praktisch gleich.
inline float softOdd (float x)
{
    const float ax = std::abs (x);
    return x * (27.0f + ax * ax) / (27.0f + 9.0f * ax * ax + 1.0f);
}

/// Schnelle tanh-Näherung, Fehler unter 1e-4 für |x| < 4.
inline float fastTanh (float x)
{
    if (x < -3.5f) return -1.0f;
    if (x >  3.5f) return  1.0f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)))
             / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}

/// Asymmetrisch weich: die positive Halbwelle begrenzt früher als die negative.
/// asym von 0 (symmetrisch) bis etwa 0,5. Der entstehende Gleichanteil wird
/// danach gefiltert.
inline float softAsym (float x, float asym)
{
    const float gp = 1.0f + asym;
    const float gn = 1.0f - asym * 0.6f;
    return x >= 0.0f ? fastTanh (gp * x) / gp
                     : fastTanh (gn * x) / gn;
}

/// Triodenähnlich: leise fast linear, laut zunehmend gekrümmt, die untere
/// Halbwelle begrenzt früher.
inline float triode (float x, float bias)
{
    const float b = x + bias;
    float y;
    if (b > 0.0f)
        y = b / (1.0f + 0.35f * b * b);           // langsam weich
    else
        y = fastTanh (1.6f * b) * 0.625f;         // schneller in die Begrenzung
    return y - (bias / (1.0f + 0.35f * bias * bias));
}

/// Begrenzung mit scharfem Knie ohne Ecke: x / (1 + |x|^n)^(1/n). Eine echte
/// Ecke erzeugt unendlich viele Obertöne, die auch mit 4-fachem Oversampling
/// zurückfalten.
inline float softClipN (float x, float n)
{
    const float ax = std::abs (x);
    if (ax < 1.0e-7f) return x;
    return x / std::pow (1.0f + std::pow (ax, n), 1.0f / n);
}

/// Harte Kante, asymmetrisch. Für CLEAN.
inline float hardAsym (float x)
{
    const float g = x >= 0.0f ? 1.0f : 1.28f;
    return softClipN (x * g, 6.0f) / g;
}

/// Gibt nur den Anteil über der Schwelle aus, wie eine Diode unterhalb der
/// Durchlassspannung. Dadurch kann die Kennlinie in einem Parallelweg stehen,
/// ohne bei kleinem Pegel einen Kammfilter mit dem Hauptweg zu bilden.
///
/// Unterschiedliche thrPos/thrNeg ergeben die Asymmetrie für gerade Obertöne.
inline float excess (float x, float thrPos, float thrNeg, float knee = 0.02f)
{
    const float t = x >= 0.0f ? thrPos : thrNeg;
    const float a = std::abs (x);
    const float d = a - t;

    float y;
    if (d <= -knee)      y = 0.0f;
    else if (d >= knee)  y = d;
    else                 y = (d + knee) * (d + knee) / (4.0f * knee);   // verrundetes Knie

    return x >= 0.0f ? y : -y;
}

/// Diodenpaar, asymmetrisch bestückt (eine Diode auf der einen Seite, zwei auf
/// der anderen). tanh sättigt von selbst, ein zusätzliches Clipping würde
/// wieder eine Ecke erzeugen.
inline float diodePair (float x)
{
    return x >= 0.0f ? fastTanh (x)
                     : fastTanh (x * 0.72f) / 0.72f;
}

} // namespace shape
} // namespace nmp
