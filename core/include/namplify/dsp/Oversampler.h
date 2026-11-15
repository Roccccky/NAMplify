#pragma once
// Oversampling für die Drives.
//
// Obertöne der Kennlinie dürfen nicht über Nyquist reichen, sonst falten sie
// zurück und erscheinen als unharmonisches Sirren.
//
// Aufbau: Halbband-FIR in Polyphasenform. Jeder zweite Tap ist null, gerechnet
// wird nur die Hälfte.

#include "../util/Types.h"
#include "Window.h"
#include <algorithm>
#include <vector>

namespace nmp
{

/// Eine Stufe: Faktor 2 hoch oder runter. Kernlänge L = 4m+1, damit die Mitte
/// auf einem geraden Index liegt und die Polyphasenzerlegung aufgeht.
class HalfbandStage
{
public:
    void prepare (int numTaps = 33);
    void reset();

    /// Latenz in Samples der niedrigeren Rate.
    int latencySlow() const { return centre / 2; }

    /// n Eingangswerte → 2n Ausgangswerte.
    void upsample (const float* in, int n, float* out);

    /// 2n Eingangswerte → n Ausgangswerte.
    void downsample (const float* in, int n, float* out);

private:
    std::vector<float> oddTaps;      // die Taps ungleich null
    std::vector<float> histUp;       // Verlauf für den Hochweg (langsame Rate)
    std::vector<float> histDownOdd;  // ungerade Werte der schnellen Rate
    std::vector<float> histDownEven; // gerade Werte der schnellen Rate
    int   centre = 16;               // Mittenindex des vollen Kerns
    int   numOdd = 16;
    float centreTap = 0.5f;
    int upPos = 0, dnOddPos = 0, dnEvenPos = 0;
};

/// Faktor 1, 2 oder 4.
class Oversampler
{
public:
    void prepare (int factor, int maxBlockSize);
    void reset();

    int factor() const { return oversampleFactor; }

    /// Latenz in Samples der Grundrate. Konstant, auch im Bypass, damit Ein-
    /// und Ausschalten nichts verschiebt.
    int latencySamples() const { return latency; }

    int maxOversampledFrames() const { return maxFrames * oversampleFactor; }

    /// Zeigt auf den internen Puffer mit n * factor Werten.
    float* upsample (const float* in, int n);

    /// Rechnet den internen Puffer zurück nach out (n Werte).
    void downsample (float* out, int n);

private:
    HalfbandStage s1, s2;
    std::vector<float> buf1, buf2;
    int oversampleFactor = 1, maxFrames = 0, latency = 0;
};

/// Feste kleine Verzögerung, damit ein Block im Bypass dieselbe Latenz hat wie
/// mit Oversampling.
class PureDelay
{
public:
    void prepare (int lengthSamples)
    {
        len = std::max (0, lengthSamples);
        buffer.assign ((size_t) std::max (1, len), 0.0f);
        pos = 0;
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }

    inline float process (float x)
    {
        if (len == 0) return x;
        const float y = buffer[(size_t) pos];
        buffer[(size_t) pos] = x;
        if (++pos >= len) pos = 0;
        return y;
    }

    void processBlock (float* data, int n)
    {
        if (len == 0) return;
        for (int i = 0; i < n; ++i) data[i] = process (data[i]);
    }

private:
    std::vector<float> buffer;
    int len = 0, pos = 0;
};

} // namespace nmp
