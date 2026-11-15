#include "namplify/dsp/Oversampler.h"
#include <algorithm>
#include <vector>

namespace nmp
{

void HalfbandStage::prepare (int numTaps)
{
    // L = 4m+1 erzwingen.
    if (((numTaps - 1) % 4) != 0) numTaps = ((numTaps - 1) / 4 + 1) * 4 + 1;
    const int L = numTaps;
    centre = (L - 1) / 2;         // gerade

    // Gefensterter Sinc bei fc = 0,25. Taps mit geradem Abstand zur Mitte
    // werden dabei null.
    std::vector<double> h ((size_t) L);
    double sum = 0.0;
    for (int i = 0; i < L; ++i)
    {
        const double x = (double) (i - centre);
        h[(size_t) i] = 0.5 * sincNorm (0.5 * x) * kaiser (i, L, 8.0);
        sum += h[(size_t) i];
    }
    // Auf Gleichanteil 1 normieren.
    for (auto& v : h) v /= sum;

    oddTaps.clear();
    for (int i = 1; i < L; i += 2)
        oddTaps.push_back ((float) h[(size_t) i]);

    numOdd    = (int) oddTaps.size();
    centreTap = (float) h[(size_t) centre];   // nach dem Fenstern gemessen

    reset();
}

void HalfbandStage::reset()
{
    histUp.assign ((size_t) numOdd, 0.0f);
    histDownOdd.assign ((size_t) numOdd + 2, 0.0f);
    histDownEven.assign ((size_t) (centre / 2 + 2), 0.0f);
    upPos = dnOddPos = dnEvenPos = 0;
}

void HalfbandStage::upsample (const float* in, int n, float* out)
{
    // Gerade Ausgangswerte: nur der Mitten-Tap trägt bei, also der Eingangswert
    // um centre/2 verzögert. Das ist dieselbe Verzögerung wie im ungeraden
    // Zweig.
    const int d = centre / 2;

    for (int i = 0; i < n; ++i)
    {
        // Verlauf schreiben (Ringpuffer der Länge numOdd)
        histUp[(size_t) upPos] = in[i];

        // gerader Ausgangswert
        int idx = upPos - d;
        if (idx < 0) idx += numOdd;
        out[2 * i] = histUp[(size_t) idx];

        // ungerader Ausgangswert: 2 · Σ h[ungerade] · x[…]
        float acc = 0.0f;
        for (int t = 0; t < numOdd; ++t)
        {
            int j = upPos - t;
            if (j < 0) j += numOdd;
            acc += oddTaps[(size_t) t] * histUp[(size_t) j];
        }
        out[2 * i + 1] = 2.0f * acc;

        if (++upPos >= numOdd) upPos = 0;
    }
}

void HalfbandStage::downsample (const float* in, int n, float* out)
{
    // y[p] = h[centre]·u[2p-centre] + Σ_{k ungerade} h[k]·u[2p-k]
    // Der gerade Anteil greift auf die geraden Werte der hohen Rate zu, der
    // ungerade auf die ungeraden, dort einen Schritt früher: u[2p-2t-1] ist der
    // ungerade Wert von Schritt p-t-1.
    const int d       = centre / 2;
    const int evenLen = (int) histDownEven.size();
    const int oddLen  = (int) histDownOdd.size();

    for (int i = 0; i < n; ++i)
    {
        histDownEven[(size_t) dnEvenPos] = in[2 * i];
        histDownOdd [(size_t) dnOddPos ] = in[2 * i + 1];

        int ei = dnEvenPos - d;
        if (ei < 0) ei += evenLen;
        float acc = centreTap * histDownEven[(size_t) ei];

        for (int t = 0; t < numOdd; ++t)
        {
            int j = dnOddPos - t - 1;
            while (j < 0) j += oddLen;
            acc += oddTaps[(size_t) t] * histDownOdd[(size_t) j];
        }

        out[i] = acc;

        if (++dnEvenPos >= evenLen) dnEvenPos = 0;
        if (++dnOddPos  >= oddLen)  dnOddPos  = 0;
    }
}

void Oversampler::prepare (int factor, int maxBlockSize)
{
    oversampleFactor = (factor >= 4) ? 4 : (factor >= 2 ? 2 : 1);
    maxFrames = std::max (1, maxBlockSize);

    if (oversampleFactor == 1)
    {
        latency = 0;
        buf1.assign ((size_t) maxFrames, 0.0f);
        return;
    }

    s1.prepare (33);
    buf1.assign ((size_t) maxFrames * 2 + 8, 0.0f);

    if (oversampleFactor == 4)
    {
        s2.prepare (33);
        buf2.assign ((size_t) maxFrames * 4 + 8, 0.0f);
        // Stufe 1 in der Grundrate, Stufe 2 in der doppelten.
        latency = 2 * s1.latencySlow() + s2.latencySlow();
    }
    else
    {
        latency = 2 * s1.latencySlow();
    }

    reset();
}

void Oversampler::reset()
{
    s1.reset();
    s2.reset();
    std::fill (buf1.begin(), buf1.end(), 0.0f);
    std::fill (buf2.begin(), buf2.end(), 0.0f);
}

float* Oversampler::upsample (const float* in, int n)
{
    if (oversampleFactor == 1)
    {
        std::copy (in, in + n, buf1.begin());
        return buf1.data();
    }

    s1.upsample (in, n, buf1.data());
    if (oversampleFactor == 2) return buf1.data();

    s2.upsample (buf1.data(), n * 2, buf2.data());
    return buf2.data();
}

void Oversampler::downsample (float* out, int n)
{
    if (oversampleFactor == 1)
    {
        std::copy (buf1.begin(), buf1.begin() + n, out);
        return;
    }

    if (oversampleFactor == 4)
        s2.downsample (buf2.data(), n * 2, buf1.data());

    s1.downsample (buf1.data(), n, out);
}

} // namespace nmp
