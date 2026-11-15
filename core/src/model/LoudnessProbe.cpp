#include "namplify/model/LoudnessProbe.h"
#include "namplify/util/Random.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace nmp
{

std::vector<float> makeLoudnessProbe (double sampleRate)
{
    const double seconds = 2.4;
    const int n = (int) std::round (sampleRate * seconds);

    std::vector<float> out ((size_t) n, 0.0f);
    Xorshift rng (0x5EEDu);   // fester Seed

    // Leersaiten einer Gitarre und zwei höhere Lagen.
    const double notes[8] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 587.33 };

    auto pluck = [&] (double f0, double startSec, double lengthSec, double amp)
    {
        const int from = (int) (startSec * sampleRate);
        const int len  = (int) (lengthSec * sampleRate);

        // Gezupfter Ton: viele Obertöne, die oberen klingen schneller aus.
        for (int h = 1; h <= 14; ++h)
        {
            const double f = f0 * h;
            if (f > sampleRate * 0.45) break;

            const double hAmp   = amp / (double) (h * h) * (1.0 + 0.4 * ((h % 2) ? 1.0 : -0.3));
            const double hDecay = 3.0 + 0.55 * h;
            const double phase  = rng.nextUnit() * kTwoPi;

            for (int i = 0; i < len && from + i < n; ++i)
            {
                const double t = (double) i / sampleRate;
                out[(size_t) (from + i)] += (float) (hAmp * std::exp (-hDecay * t)
                                                     * std::sin (kTwoPi * f * t + phase));
            }
        }

        // Anschlagsgeräusch
        const int noiseLen = (int) (0.012 * sampleRate);
        float lp = 0.0f;
        for (int i = 0; i < noiseLen && from + i < n; ++i)
        {
            lp += 0.35f * (rng.nextBipolar() - lp);
            const double env = std::exp (-90.0 * (double) i / sampleRate);
            out[(size_t) (from + i)] += (float) (lp * amp * 0.9 * env);
        }
    };

    // Acht Einzeltöne
    for (int i = 0; i < 8; ++i)
        pluck (notes[i], 0.02 + i * 0.20, 0.55, 0.9);

    // und am Ende ein Akkord, leicht versetzt angeschlagen.
    const double chord[4] = { 82.41, 123.47, 164.81, 246.94 };
    for (int i = 0; i < 4; ++i)
        pluck (chord[i], 1.70 + i * 0.012, 0.70, 0.65);

    // Auf −12 dBFS Spitze normieren.
    float peak = 0.0f;
    for (float v : out) peak = std::max (peak, std::abs (v));
    if (peak > 1.0e-9f)
    {
        const float g = dbToGain (-12.0f) / peak;
        for (auto& v : out) v *= g;
    }

    return out;
}

void KWeighting::prepare (double sampleRate)
{
    // Die beiden Stufen aus ITU-R BS.1770. Bei 48 kHz entsprechen sie den
    // Koeffizienten aus der Empfehlung, für andere Raten werden sie neu
    // ausgelegt.
    shelf.setCoeffs (designHighShelf (1681.97, 0.7071, 3.999, sampleRate));
    hp   .setCoeffs (designHighPass  (  38.13, 0.5003,        sampleRate));
    reset();
}

void KWeighting::reset()
{
    shelf.reset();
    hp.reset();
}

double measureKWeightedDb (const float* data, int numFrames, double sampleRate)
{
    KWeighting k;
    k.prepare (sampleRate);

    // Die ersten 100 ms auslassen (Einschwingen von Filter und Modell).
    const int skip = std::min (numFrames / 4, (int) (sampleRate * 0.1));

    double energy = 0.0;
    int counted = 0;

    for (int i = 0; i < numFrames; ++i)
    {
        const float y = k.process (data[i]);
        if (i >= skip) { energy += (double) y * y; ++counted; }
    }

    if (counted <= 0) return -120.0;
    return 20.0 * std::log10 (std::max (1.0e-12, std::sqrt (energy / counted)));
}

LoudnessMeasurement measureModelLoudness (double sampleRate,
                                          const std::function<void (float*, int)>& runner)
{
    LoudnessMeasurement m;

    auto probe = makeLoudnessProbe (sampleRate);
    if (probe.empty() || ! runner) return m;

    // In kleinen Blöcken wie im Betrieb.
    const int chunk = 512;
    int pos = 0;
    while (pos < (int) probe.size())
    {
        const int c = std::min (chunk, (int) probe.size() - pos);
        runner (probe.data() + pos, c);
        pos += c;
    }

    // Plausibilität: ein Modell, das unendliche Werte oder nichts liefert,
    // bekommt keinen Ausgleich.
    double peak = 0.0;
    for (float v : probe)
    {
        if (! std::isfinite (v)) return m;
        peak = std::max (peak, (double) std::abs (v));
    }
    if (peak < 1.0e-7) return m;

    m.measuredDb = measureKWeightedDb (probe.data(), (int) probe.size(), sampleRate);
    m.trimDb     = kNormalizationTargetDb - m.measuredDb;

    // Mehr als ±36 dB Ausgleich bedeutet eine fehlgeschlagene Messung.
    if (! std::isfinite (m.trimDb) || std::abs (m.trimDb) > 36.0) return m;

    m.valid = true;
    return m;
}

} // namespace nmp
