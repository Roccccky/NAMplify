#pragma once
// Geglättete Werte.
//
// - LinearSmoothed erreicht das Ziel nach fester Zeit exakt. Für Blenden
// (Ein/Aus, Umsortieren der Kette).
// - OnePoleSmoothed nähert sich exponentiell an und ist billiger. Für Regler.

#include "Types.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

class LinearSmoothed
{
public:
    void reset (double sampleRate, double rampSeconds)
    {
        stepsTotal = std::max (1, (int) std::round (sampleRate * rampSeconds));
        stepsLeft  = 0;
        step       = 0.0f;
    }

    void setCurrentAndTarget (float v) { current = target = v; stepsLeft = 0; step = 0.0f; }

    void setTarget (float v)
    {
        if (v == target) return;
        target = v;
        if (current == target) { stepsLeft = 0; step = 0.0f; return; }
        stepsLeft = stepsTotal;
        step      = (target - current) / (float) stepsTotal;
    }

    bool  isSmoothing() const { return stepsLeft > 0; }
    float getTarget()   const { return target; }
    float getCurrent()  const { return current; }

    float next()
    {
        if (stepsLeft <= 0) return current;
        current += step;
        if (--stepsLeft == 0) current = target;
        return current;
    }

    /// Für Blöcke, die den Wert nur einmal pro Puffer brauchen.
    float skip (int numFrames)
    {
        if (stepsLeft <= 0) return current;
        if (numFrames >= stepsLeft) { current = target; stepsLeft = 0; return current; }
        current   += step * (float) numFrames;
        stepsLeft -= numFrames;
        return current;
    }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int   stepsTotal = 1, stepsLeft = 0;
};

class OnePoleSmoothed
{
public:
    void reset (double sampleRate, double timeConstantSeconds)
    {
        coeff = timeConstantSeconds <= 0.0
              ? 1.0f
              : (float) (1.0 - std::exp (-1.0 / (sampleRate * timeConstantSeconds)));
    }

    void setCurrentAndTarget (float v) { current = target = v; }
    void setTarget (float v)           { target = v; }
    float getCurrent() const           { return current; }
    float getTarget()  const           { return target; }

    float next()
    {
        current += coeff * (target - current);
        return flushDenormal (current);
    }

    float skip (int numFrames)
    {
        // (1-c)^n in einem Schritt.
        const float a = std::pow (1.0f - coeff, (float) numFrames);
        current = target + (current - target) * a;
        current = flushDenormal (current);
        return current;
    }

private:
    float current = 0.0f, target = 0.0f, coeff = 1.0f;
};

/// Pegelanzeige: schnell hoch, langsam runter, damit kurze Spitzen sichtbar
/// bleiben.
class BallisticMeter
{
public:
    void reset (double sampleRate, double attackSeconds, double releaseSeconds)
    {
        atk = (float) (1.0 - std::exp (-1.0 / (sampleRate * std::max (1.0e-4, attackSeconds))));
        rel = (float) (1.0 - std::exp (-1.0 / (sampleRate * std::max (1.0e-4, releaseSeconds))));
        value = 0.0f;
    }

    float push (float v)
    {
        const float c = v > value ? atk : rel;
        value += c * (v - value);
        return flushDenormal (value);
    }

    /// Ein ganzer Puffer auf einmal, für Anzeigen ausreichend.
    float pushBlock (float peak, int numFrames)
    {
        const float c = peak > value ? atk : rel;
        const float a = std::pow (1.0f - c, (float) numFrames);
        value = peak + (value - peak) * a;
        return flushDenormal (value);
    }

    float get() const { return value; }
    void  set (float v) { value = v; }

private:
    float value = 0.0f, atk = 1.0f, rel = 1.0f;
};

} // namespace nmp
