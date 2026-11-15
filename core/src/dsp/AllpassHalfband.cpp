#include "namplify/dsp/AllpassHalfband.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace nmp
{

namespace
{
    constexpr double kPiD = 3.14159265358979323846;

    double ipow (double x, int n)
    {
        double r = 1.0;
        for (int i = 0; i < n; ++i) r *= x;
        return r;
    }

    double accNum (double q, int order, int c)
    {
        double sum = 0.0, term = 0.0;
        int i = 0, sign = 1;
        do
        {
            term = ipow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * kPiD / order) * sign;
            sum += term;
            sign = -sign;
            ++i;
        }
        while (std::abs (term) > 1.0e-100 && i < 64);
        return sum;
    }

    double accDen (double q, int order, int c)
    {
        double sum = 0.0, term = 0.0;
        int i = 1, sign = -1;
        do
        {
            term = ipow (q, i * i) * std::cos (i * 2 * c * kPiD / order) * sign;
            sum += term;
            sign = -sign;
            ++i;
        }
        while (std::abs (term) > 1.0e-100 && i < 64);
        return sum;
    }
}

std::vector<double> designAllpassHalfband (int numCoefs, double transition)
{
    // Übergang → Selektivität k und Nome q des elliptischen Entwurfs.
    double k = std::tan ((1.0 - transition * 2.0) * kPiD / 4.0);
    k *= k;
    const double kk = std::pow (1.0 - k * k, 0.25);
    const double e  = 0.5 * (1.0 - kk) / (1.0 + kk);
    const double e4 = e * e * e * e;
    const double q  = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

    const int order = numCoefs * 2 + 1;
    std::vector<double> coefs ((size_t) numCoefs);

    for (int index = 0; index < numCoefs; ++index)
    {
        const int c = index + 1;
        const double num  = accNum (q, order, c) * std::pow (q, 0.25);
        const double den  = accDen (q, order, c) + 0.5;
        const double ww   = num / den;
        const double wwsq = ww * ww;
        const double x    = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coefs[(size_t) index] = (1.0 - x) / (1.0 + x);
    }

    return coefs;
}

double AllpassHalfband::Chain::process (double in)
{
    // Allpass erster Ordnung pro Glied, in der Rate des Zweigs: y[n] = c·(x[n]
    // − y[n−1]) + x[n−1]
    double v = in;
    for (size_t i = 0; i < coef.size(); ++i)
    {
        const double out = coef[i] * (v - y[i]) + x[i];
        x[i] = v;
        y[i] = out;
        v = out;
    }
    return v;
}

void AllpassHalfband::Chain::clear()
{
    std::fill (x.begin(), x.end(), 0.0);
    std::fill (y.begin(), y.end(), 0.0);
}

void AllpassHalfband::prepare (int numCoefs, double transition)
{
    const auto coefs = designAllpassHalfband (numCoefs, transition);

    std::vector<double> even, odd;
    for (size_t i = 0; i < coefs.size(); ++i)
        (i % 2 == 0 ? even : odd).push_back (coefs[i]);

    for (auto* c : { &upA, &downA }) c->coef = even;
    for (auto* c : { &upB, &downB }) c->coef = odd;

    for (auto* c : { &upA, &upB, &downA, &downB })
    {
        c->x.assign (c->coef.size(), 0.0);
        c->y.assign (c->coef.size(), 0.0);
    }
}

void AllpassHalfband::reset()
{
    for (auto* c : { &upA, &upB, &downA, &downB }) c->clear();
}

void AllpassHalfband::upsample (const float* in, int n, float* out)
{
    for (int i = 0; i < n; ++i)
    {
        const double x = in[i];
        out[2 * i]     = (float) upA.process (x);
        out[2 * i + 1] = (float) upB.process (x);
    }
}

void AllpassHalfband::downsample (const float* in, int n, float* out)
{
    // Ungerader Wert in den ersten Zweig, gerader in den zweiten (wie
    // Downsampler2x in HIIR).
    for (int i = 0; i < n; ++i)
        out[i] = (float) (0.5 * (downA.process (in[2 * i + 1]) + downB.process (in[2 * i])));
}

void CaptureOversampler::prepare (int factor, int maxFrames)
{
    oversampleFactor = factor >= 4 ? 4 : (factor >= 2 ? 2 : 1);
    scratch.assign ((size_t) std::max (1, maxFrames * 2), 0.0f);

    // Erste Stufe 48 → 96 kHz: Durchlass bis 21 kHz, Sperre ab 27 kHz, mit acht
    // Allpässen über 110 dB. Jedes weitere Paar bringt etwa 20 dB mehr
    // Sperrdämpfung und 0,4 Samples mehr Laufzeit.
    first.prepare (8, 6.0 / 96.0);
    // Zweite Stufe 96 → 192 kHz: der Bereich 20 bis 76 kHz ist schon gefiltert,
    // die Flanke darf breit sein.
    second.prepare (4, 0.2);
    reset();
}

void CaptureOversampler::reset()
{
    first.reset();
    second.reset();
}

void CaptureOversampler::upsample (const float* in, int n, float* out)
{
    if (oversampleFactor == 1) { std::copy (in, in + n, out); return; }
    if (oversampleFactor == 2) { first.upsample (in, n, out); return; }

    if ((int) scratch.size() < n * 2) scratch.resize ((size_t) (n * 2));
    first.upsample (in, n, scratch.data());
    second.upsample (scratch.data(), n * 2, out);
}

void CaptureOversampler::downsample (const float* in, int n, float* out)
{
    if (oversampleFactor == 1) { std::copy (in, in + n, out); return; }
    if (oversampleFactor == 2) { first.downsample (in, n, out); return; }

    if ((int) scratch.size() < n * 2) scratch.resize ((size_t) (n * 2));
    second.downsample (in, n * 2, scratch.data());
    first.downsample (scratch.data(), n, out);
}

} // namespace nmp
