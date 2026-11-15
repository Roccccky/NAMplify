#pragma once
// LFO und Hüllkurvenfolger für die Modulationseffekte.

#include "../util/Types.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

class Lfo
{
public:
    enum class Shape { sine, triangle, squareish };

    void prepare (double sr) { sampleRate = sr; }

    void setRate (float hz)   { rate  = std::max (0.0f, hz); }
    void setShape (Shape s)   { shape = s; }
    void setPhase (double p)  { phase = p - std::floor (p); }
    double getPhase() const   { return phase; }
    void reset()              { phase = 0.0; }

    /// Ein Schritt weiter, Ergebnis −1 … +1.
    inline float next()
    {
        const float v = valueAt (phase);
        phase += (double) rate / sampleRate;
        if (phase >= 1.0) phase -= std::floor (phase);
        return v;
    }

    /// Wert mit Phasenversatz, ohne weiterzulaufen. Für die zweite Seite eines
    /// Stereo-Effekts.
    inline float peekOffset (double offset) const
    {
        double p = phase + offset;
        p -= std::floor (p);
        return valueAt (p);
    }

    inline void advance (int n) { phase += (double) rate * n / sampleRate; phase -= std::floor (phase); }

private:
    inline float valueAt (double p) const
    {
        switch (shape)
        {
            case Shape::sine:
                return (float) std::sin (kTwoPi * p);

            case Shape::triangle:
                return (float) (p < 0.5 ? (4.0 * p - 1.0) : (3.0 - 4.0 * p));

            case Shape::squareish:
            default:
            {
                // Abgerundetes Rechteck: bei kleinem Wert fast Sinus, bei
                // großem fast Rechteck. Ein hartes Rechteck würde knacken.
                const double s = std::sin (kTwoPi * p);
                return (float) std::tanh (3.0 * s);
            }
        }
    }

    double sampleRate = 48000.0, phase = 0.0;
    float  rate = 1.0f;
    Shape  shape = Shape::sine;
};

/// Hüllkurvenfolger mit getrennten Attack- und Release-Zeiten, Peak oder RMS.
class EnvelopeFollower
{
public:
    void prepare (double sr) { sampleRate = sr; setTimes (attackMs, releaseMs); env = 0.0f; }

    void setTimes (float attMs, float relMs)
    {
        attackMs = attMs; releaseMs = relMs;
        atk = coeffFor (attMs);
        rel = coeffFor (relMs);
    }

    void reset() { env = 0.0f; }

    inline float process (float x)
    {
        const float a = std::abs (x);
        const float c = a > env ? atk : rel;
        env += c * (a - env);
        return flushDenormal (env);
    }

    float get() const { return env; }

private:
    float coeffFor (float ms) const
    {
        if (ms <= 0.0f) return 1.0f;
        return (float) (1.0 - std::exp (-1.0 / (sampleRate * (double) ms * 0.001)));
    }

    double sampleRate = 48000.0;
    float  env = 0.0f, atk = 1.0f, rel = 1.0f;
    float  attackMs = 5.0f, releaseMs = 50.0f;
};

} // namespace nmp
