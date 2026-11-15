#include "namplify/dsp/Resampler.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

namespace
{
    /// Taps links der Mitte. Der Kern reicht von base - kLeft bis base - kLeft
    /// + kTaps - 1.
    constexpr int kLeft = SincResampler::kTaps / 2 - 1;

    /// Kaiser-Fenster als stetige Funktion des Abstands zur Mitte, weil sich
    /// die Tap-Lage mit der Phase verschiebt.
    double kaiserAt (double x, double halfWidth, double beta)
    {
        const double r = x / halfWidth;
        const double a = 1.0 - r * r;
        if (a <= 0.0) return 0.0;
        return besselI0 (beta * std::sqrt (a)) / besselI0 (beta);
    }
}

void SincResampler::buildTable (double cutoffRatio)
{
    table.assign ((size_t) (kPhases + 1) * kTaps, 0.0f);

    constexpr double halfWidth = kTaps * 0.5;
    constexpr double beta      = 8.0;   // ≈ −80 dB Nebenkeulen

    for (int p = 0; p <= kPhases; ++p)
    {
        const double frac = (double) p / (double) kPhases;
        double sum = 0.0;

        for (int t = 0; t < kTaps; ++t)
        {
            const double x = (double) (t - kLeft) - frac;
            const double v = sincNorm (cutoffRatio * x) * kaiserAt (x, halfWidth, beta);
            table[(size_t) p * kTaps + (size_t) t] = (float) v;
            sum += v;
        }

        // Auf Gleichanteil 1 normieren, sonst schwankt der Pegel mit der Phase.
        if (sum > 1.0e-9)
        {
            const float inv = (float) (1.0 / sum);
            for (int t = 0; t < kTaps; ++t)
                table[(size_t) p * kTaps + (size_t) t] *= inv;
        }
    }
}

void SincResampler::prepare (double srIn, double srOut, int maxInputFrames)
{
    passthrough = std::abs (srIn - srOut) < 1.0e-6;
    step = srIn / srOut;

    if (! passthrough)
    {
        // Grenzfrequenz nach der kleineren Rate, sonst Aliasing beim
        // Herunterrechnen.
        const double cutoff = std::min (1.0, srOut / srIn) * 0.92;
        buildTable (cutoff);
    }

    history.assign ((size_t) (std::max (16, maxInputFrames) + kTaps * 4 + 8), 0.0f);
    reset();
}

void SincResampler::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    historyFill = kTaps;   // mit Stille vorbelegt
    pos         = (double) kLeft;
}

int SincResampler::maxOutputFor (int n) const
{
    if (passthrough) return n;
    return (int) std::ceil ((double) n / step) + 2;
}

int SincResampler::process (const float* in, int n, float* out)
{
    if (passthrough)
    {
        std::copy (in, in + n, out);
        return n;
    }

    if ((size_t) (historyFill + n) > history.size())
        history.resize ((size_t) (historyFill + n) * 2, 0.0f);

    std::copy (in, in + n, history.begin() + historyFill);
    historyFill += n;

    // Der letzte Tap liegt bei base - kLeft + kTaps - 1 = base + kTaps/2.
    const int lastBase = historyFill - kTaps / 2 - 1;

    int produced = 0;
    while ((int) std::floor (pos) <= lastBase)
    {
        const int    base = (int) std::floor (pos);
        const double frac = pos - (double) base;
        const int    ph   = (int) (frac * kPhases);
        const float  phFr = (float) (frac * kPhases - (double) ph);

        const float* t0 = &table[(size_t) ph * kTaps];
        const float* t1 = &table[(size_t) (ph + 1) * kTaps];
        const float* h  = &history[(size_t) (base - kLeft)];

        float acc = 0.0f;
        for (int t = 0; t < kTaps; ++t)
            acc += h[t] * (t0[t] + (t1[t] - t0[t]) * phFr);

        out[produced++] = acc;
        pos += step;
    }

    // Verlauf nach vorn schieben, ohne dass der linke Rand des Kerns unter null
    // fällt.
    const int keepFrom = (int) std::floor (pos) - kLeft;
    if (keepFrom > 0)
    {
        const int keep = historyFill - keepFrom;
        std::copy (history.begin() + keepFrom, history.begin() + historyFill, history.begin());
        historyFill = keep;
        pos -= (double) keepFrom;
    }

    return produced;
}

void RoundTripResampler::prepare (double outerRate, double innerRate, int maxOuterBlock)
{
    outer = outerRate;
    inner = innerRate;
    passthrough = std::abs (outer - inner) < 1.0e-6;

    up.prepare (outer, inner, maxOuterBlock);
    maxInner = up.maxOutputFor (maxOuterBlock);
    down.prepare (inner, outer, maxInner);

    innerBuf.assign ((size_t) std::max (16, maxInner + 16), 0.0f);

    if (passthrough)
    {
        reportedLatency = 0;
        outFifo.assign ((size_t) std::max (16, maxOuterBlock * 2), 0.0f);
        fifoFill = 0;
        return;
    }

    // Die Warteschlange startet mit ein paar Samples Stille, damit schon der
    // erste Puffer voll bedient wird.
    constexpr int prime = 4;

    reportedLatency = (int) std::lround ((double) up.latencyOut() * outer / inner)
                    + down.latencyOut() + prime;

    outFifo.assign ((size_t) (maxOuterBlock * 4 + down.maxOutputFor (maxInner) + 32), 0.0f);
    fifoFill = prime;
}

void RoundTripResampler::reset()
{
    up.reset();
    down.reset();
    std::fill (innerBuf.begin(), innerBuf.end(), 0.0f);
    std::fill (outFifo.begin(), outFifo.end(), 0.0f);
    fifoFill = passthrough ? 0 : 4;
}

void RoundTripResampler::process (float* buffer, int numFrames, Worker worker, void* ctx)
{
    if (passthrough)
    {
        worker (ctx, buffer, numFrames);
        return;
    }

    if ((size_t) up.maxOutputFor (numFrames) > innerBuf.size())
        innerBuf.assign ((size_t) up.maxOutputFor (numFrames) * 2, 0.0f);

    const int nInner = up.process (buffer, numFrames, innerBuf.data());
    if (nInner > 0)
        worker (ctx, innerBuf.data(), nInner);

    const size_t needed = (size_t) fifoFill + (size_t) down.maxOutputFor (nInner) + (size_t) numFrames;
    if (needed > outFifo.size())
        outFifo.resize (needed * 2, 0.0f);

    fifoFill += down.process (innerBuf.data(), nInner, outFifo.data() + fifoFill);

    if (fifoFill >= numFrames)
    {
        std::copy (outFifo.begin(), outFifo.begin() + numFrames, buffer);
        std::copy (outFifo.begin() + numFrames, outFifo.begin() + fifoFill, outFifo.begin());
        fifoFill -= numFrames;
    }
    else
    {
        // Nur im allerersten Puffer möglich. Der Rest bleibt still.
        std::copy (outFifo.begin(), outFifo.begin() + fifoFill, buffer);
        std::fill (buffer + fifoFill, buffer + numFrames, 0.0f);
        fifoFill = 0;
    }
}

} // namespace nmp
