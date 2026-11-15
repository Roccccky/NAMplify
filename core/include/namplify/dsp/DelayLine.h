#pragma once
// Verzögerungsleitungen.
//
// XfadeDelayTap verschiebt beim Ändern der Zeit keinen Lesezeiger, das würde
// die Tonhöhe der Wiederholungen mitziehen. Stattdessen zwei Leseköpfe mit
// fester Zeit und eine Überblendung zwischen ihnen. Der alte Kopf behält seine
// Zeit, die alten Wiederholungen werden nur leiser.

#include "../util/Types.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace nmp
{

/// Ringpuffer mit fraktionalem Abgriff (Catmull-Rom, vier Stützstellen).
class DelayLine
{
public:
    void prepare (double sampleRate, double maxDelaySeconds)
    {
        capacity = std::max (8, (int) std::ceil (sampleRate * maxDelaySeconds) + 8);
        buffer.assign ((size_t) capacity, 0.0f);
        writePos = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    int maxDelaySamples() const { return capacity - 4; }

    inline void write (float x)
    {
        if (capacity <= 0) return;
        buffer[(size_t) writePos] = x;
        if (++writePos >= capacity) writePos = 0;
    }

    /// Liest delaySamples zurück. 1.0 ist der zuletzt geschriebene Wert.
    inline float readAt (float delaySamples) const
    {
        if (capacity <= 8) return 0.0f;   // nicht vorbereitet: Stille
        delaySamples = clamp (delaySamples, 1.0f, (float) (capacity - 3));

        const float pos  = (float) writePos - delaySamples;
        const int   ip   = (int) std::floor (pos);
        const float frac = pos - (float) ip;

        const float ym1 = at (ip - 1);
        const float y0  = at (ip);
        const float y1  = at (ip + 1);
        const float y2  = at (ip + 2);

        // Catmull-Rom
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    /// Ganzzahliger Abgriff, wenn kein Bruchteil gebraucht wird.
    inline float readAtInt (int delaySamples) const
    {
        if (capacity <= 8) return 0.0f;
        return at (writePos - clamp (delaySamples, 1, capacity - 1));
    }

private:
    inline float at (int i) const
    {
        i %= capacity;
        if (i < 0) i += capacity;
        return buffer[(size_t) i];
    }

    std::vector<float> buffer;
    int capacity = 0, writePos = 0;
};

/// Zwei Leseköpfe mit fester Zeit und einer Blende dazwischen.
class XfadeDelayTap
{
public:
    void prepare (double sampleRate, double crossfadeSeconds = 0.035)
    {
        fadeLength  = std::max (16, (int) std::round (sampleRate * crossfadeSeconds));
        fadeCounter = 0;
        activeDelay = oldDelay = 0.0f;
        started     = false;
    }

    void reset() { fadeCounter = 0; started = false; }

    /// Zielzeit setzen.
    ///
    /// Während eine Überblendung läuft, wird nur der neueste Wert gemerkt. Beim
    /// Ziehen am Regler kommt in jedem Puffer ein neuer Wert. Würde jeder davon
    /// den aktiven Kopf verschieben, ergäbe das eine Tonhöhenänderung. So wird
    /// daraus eine Folge von Überblendungen zwischen festen Zeiten.
    void setDelaySamples (float d)
    {
        if (! started) { activeDelay = oldDelay = pendingDelay = d; started = true; return; }

        pendingDelay = d;

        if (fadeCounter > 0) return;          // Überblendung läuft

        if (std::abs (pendingDelay - activeDelay) < 1.0f)
        {
            // Ein Bruchteil eines Samples lohnt keine Überblendung.
            activeDelay = pendingDelay;
            return;
        }

        oldDelay    = activeDelay;
        activeDelay = pendingDelay;
        fadeCounter = fadeLength;
    }

    float currentDelaySamples() const { return activeDelay; }
    bool  isCrossfading()       const { return fadeCounter > 0; }

    /// Ein Sample. extraMod verschiebt beide Köpfe gleich (für das
    /// Band-Flattern).
    inline float read (const DelayLine& line, float extraMod = 0.0f)
    {
        if (fadeCounter <= 0)
            return line.readAt (activeDelay + extraMod);

        // Gleiche Leistung, eine lineare Blende hätte in der Mitte einen
        // Einbruch.
        const float t = 1.0f - (float) fadeCounter / (float) fadeLength;
        const float gNew = std::sin (0.5f * (float) kPi * t);
        const float gOld = std::cos (0.5f * (float) kPi * t);

        const float a = line.readAt (activeDelay + extraMod);
        const float b = line.readAt (oldDelay    + extraMod);

        --fadeCounter;
        return gNew * a + gOld * b;
    }

private:
    float activeDelay = 0.0f, oldDelay = 0.0f, pendingDelay = 0.0f;
    int   fadeLength = 1, fadeCounter = 0;
    bool  started = false;
};

/// Allpass erster Ordnung mit ganzzahliger Verzögerung, für die Diffusion im
/// Hall.
class AllpassDelay
{
public:
    void prepare (int lengthSamples, float g)
    {
        len  = std::max (1, lengthSamples);
        gain = g;
        buffer.assign ((size_t) len, 0.0f);
        pos = 0;
    }

    /// Aktuelle Länge. prepare() löscht den Speicher, also nur bei geänderter
    /// Länge aufrufen.
    int length() const { return len; }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }
    void setGain (float g) { gain = g; }

    inline float process (float x)
    {
        const float d = buffer[(size_t) pos];
        const float v = x + gain * d;
        buffer[(size_t) pos] = flushDenormal (v);
        if (++pos >= len) pos = 0;
        return d - gain * v;
    }

private:
    std::vector<float> buffer;
    int   len = 1, pos = 0;
    float gain = 0.5f;
};

} // namespace nmp
