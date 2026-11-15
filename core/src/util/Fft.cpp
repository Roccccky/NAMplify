#include "namplify/util/Fft.h"
#include <cmath>

namespace nmp
{

void Fft::prepare (int orderIn)
{
    fftOrder = orderIn;
    fftSize  = 1 << orderIn;

    bitRev.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        int r = 0;
        for (int b = 0; b < fftOrder; ++b)
            if (i & (1 << b)) r |= 1 << (fftOrder - 1 - b);
        bitRev[(size_t) i] = r;
    }

    // Twiddle-Faktoren aller Stufen hintereinander.
    twiddleRe.resize ((size_t) fftSize / 2);
    twiddleIm.resize ((size_t) fftSize / 2);
    for (int i = 0; i < fftSize / 2; ++i)
    {
        const double a = -kTwoPi * (double) i / (double) fftSize;
        twiddleRe[(size_t) i] = (float) std::cos (a);
        twiddleIm[(size_t) i] = (float) std::sin (a);
    }
}

void Fft::run (float* re, float* im, bool inverseTransform) const
{
    // Bit-Reversal
    for (int i = 0; i < fftSize; ++i)
    {
        const int j = bitRev[(size_t) i];
        if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }

    for (int len = 2; len <= fftSize; len <<= 1)
    {
        const int half = len >> 1;
        const int step = fftSize / len;

        for (int i = 0; i < fftSize; i += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const int t  = k * step;
                const float wr = twiddleRe[(size_t) t];
                const float wi = inverseTransform ? -twiddleIm[(size_t) t] : twiddleIm[(size_t) t];

                const int a = i + k;
                const int b = a + half;

                const float xr = re[b] * wr - im[b] * wi;
                const float xi = re[b] * wi + im[b] * wr;

                re[b] = re[a] - xr;  im[b] = im[a] - xi;
                re[a] = re[a] + xr;  im[a] = im[a] + xi;
            }
        }
    }

    if (inverseTransform)
    {
        const float s = 1.0f / (float) fftSize;
        for (int i = 0; i < fftSize; ++i) { re[i] *= s; im[i] *= s; }
    }
}

void Fft::forward (float* re, float* im) const { run (re, im, false); }
void Fft::inverse (float* re, float* im) const { run (re, im, true);  }

void RealFft::prepare (int order)
{
    n = 1 << order;
    half.prepare (order - 1);

    const int h = n / 2;
    cosTab.resize ((size_t) h);
    sinTab.resize ((size_t) h);
    for (int i = 0; i < h; ++i)
    {
        const double a = -kPi * (double) i / (double) h;
        cosTab[(size_t) i] = (float) std::cos (a);
        sinTab[(size_t) i] = (float) std::sin (a);
    }

    workRe.assign ((size_t) h, 0.0f);
    workIm.assign ((size_t) h, 0.0f);
}

void RealFft::forward (const float* time, float* re, float* im) const
{
    const int h = n / 2;

    // Gerade Samples in den Realteil, ungerade in den Imaginärteil.
    for (int i = 0; i < h; ++i)
    {
        workRe[(size_t) i] = time[2 * i];
        workIm[(size_t) i] = time[2 * i + 1];
    }

    half.forward (workRe.data(), workIm.data());

    // Entflechten
    for (int k = 0; k < h; ++k)
    {
        const int kk = (h - k) % h;

        const float er =  0.5f * (workRe[(size_t) k] + workRe[(size_t) kk]);
        const float ei =  0.5f * (workIm[(size_t) k] - workIm[(size_t) kk]);
        const float or_ =  0.5f * (workIm[(size_t) k] + workIm[(size_t) kk]);
        const float oi = -0.5f * (workRe[(size_t) k] - workRe[(size_t) kk]);

        const float wr = cosTab[(size_t) k];
        const float wi = sinTab[(size_t) k];

        re[k] = er + (or_ * wr - oi * wi);
        im[k] = ei + (or_ * wi + oi * wr);
    }

    // Nyquist
    re[h] = workRe[0] - workIm[0];
    im[h] = 0.0f;
    im[0] = 0.0f;
}

void RealFft::inverse (const float* re, const float* im, float* time) const
{
    const int h = n / 2;

    for (int k = 0; k < h; ++k)
    {
        // Kein modulo h: für k = 0 wird X[h] gebraucht (Nyquist), nicht X[0].
        const int kk = h - k;

        const float xr  = re[k],  xi  = im[k];
        const float xkr = re[kk], xki = -im[kk];

        const float er = 0.5f * (xr + xkr);
        const float ei = 0.5f * (xi + xki);
        float       tr = 0.5f * (xr - xkr);
        float       ti = 0.5f * (xi - xki);

        // Rückwärts-Twiddle
        const float wr =  cosTab[(size_t) k];
        const float wi = -sinTab[(size_t) k];

        const float or_ = tr * wr - ti * wi;
        const float oi  = tr * wi + ti * wr;

        workRe[(size_t) k] = er - oi;
        workIm[(size_t) k] = ei + or_;
    }

    half.inverse (workRe.data(), workIm.data());

    for (int i = 0; i < h; ++i)
    {
        time[2 * i]     = workRe[(size_t) i];
        time[2 * i + 1] = workIm[(size_t) i];
    }
}

} // namespace nmp
