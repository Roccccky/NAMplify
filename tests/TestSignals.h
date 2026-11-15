#pragma once
// Signale und Messungen, die mehrere Prüfungen brauchen.

#include "TestFramework.h"
#include "namplify/util/Fft.h"
#include "namplify/util/Random.h"
#include "namplify/dsp/Window.h"
#include "namplify/blocks/Block.h"
#include "namplify/engine/Rack.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace nmptest
{

/// Ein Stereopuffer, der seinen Speicher selbst hält.
struct Buf
{
    std::vector<float> l, r;

    explicit Buf (int n) : l ((size_t) n, 0.0f), r ((size_t) n, 0.0f) {}

    nmp::StereoBlock view (int from, int count)
    {
        return { l.data() + from, r.data() + from, count };
    }
    int size() const { return (int) l.size(); }

    void fillSine (double freq, double sr, float amp)
    {
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = (float) (amp * std::sin (nmp::kTwoPi * freq * (double) i / sr));
    }

    void fillNoise (float amp, unsigned seed = 42u)
    {
        nmp::Xorshift rng (seed);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = rng.nextBipolar() * amp;
    }

    void clear() { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); }
};

/// Einen Block in kleinen Stücken durchrechnen, wie ein Host es tut.
inline void runBlock (nmp::Block& b, Buf& buf, int chunk = 256,
                      const nmp::BlockContext& ctx = {})
{
    int pos = 0;
    while (pos < buf.size())
    {
        const int c = std::min (chunk, buf.size() - pos);
        auto v = buf.view (pos, c);
        b.process (v, ctx);
        pos += c;
    }
}

/// Die Slots in kleinen Stücken durchrechnen.
inline void runRack (nmp::Rack& r, Buf& buf, const nmp::RackMix& mix = {}, int chunk = 256,
                     const nmp::BlockContext& ctx = {})
{
    int pos = 0;
    while (pos < buf.size())
    {
        const int n = std::min (chunk, buf.size() - pos);
        auto v = buf.view (pos, n);
        r.process (v, ctx, mix);
        pos += n;
    }
}

inline double rms (const std::vector<float>& v, int from, int to)
{
    double e = 0.0;
    for (int i = from; i < to; ++i) e += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (e / std::max (1, to - from));
}

/// Betragsspektrum eines Abschnitts, Hann-gefenstert.
struct Spectrum
{
    std::vector<double> mag;   // Betrag je Fach
    double binHz = 1.0;

    double at (double hz) const
    {
        const int k = (int) std::lround (hz / binHz);
        if (k < 0 || k >= (int) mag.size()) return 0.0;
        // Etwas Schleppe zulassen: das Fenster verteilt einen Ton auf ein paar Fächer.
        double m = 0.0;
        for (int d = -2; d <= 2; ++d)
            if (k + d >= 0 && k + d < (int) mag.size())
                m = std::max (m, mag[(size_t) (k + d)]);
        return m;
    }

    /// Energie in einem Band, Fächer nahe der Ausschlussfrequenzen ausgenommen.
    double bandEnergy (double lo, double hi,
                       const std::vector<double>& exclude = {}, double excludeHz = 60.0) const
    {
        double e = 0.0;
        for (size_t k = 0; k < mag.size(); ++k)
        {
            const double f = (double) k * binHz;
            if (f < lo || f > hi) continue;
            bool skip = false;
            for (double x : exclude) if (std::abs (f - x) < excludeHz) { skip = true; break; }
            if (! skip) e += mag[k] * mag[k];
        }
        return e;
    }
};

inline Spectrum analyse (const std::vector<float>& sig, int from, int order, double sr)
{
    const int n = 1 << order;
    nmp::RealFft fft;
    fft.prepare (order);

    std::vector<float> w ((size_t) n);
    double winSum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double h = nmp::hann (i, n);
        w[(size_t) i] = (float) (sig[(size_t) (from + i)] * h);
        winSum += h;
    }

    std::vector<float> re ((size_t) fft.numBins()), im ((size_t) fft.numBins());
    fft.forward (w.data(), re.data(), im.data());

    Spectrum s;
    s.binHz = sr / n;
    s.mag.resize ((size_t) fft.numBins());
    for (int k = 0; k < fft.numBins(); ++k)
        s.mag[(size_t) k] = 2.0 * std::hypot (re[(size_t) k], im[(size_t) k]) / winSum;

    return s;
}

} // namespace nmptest
