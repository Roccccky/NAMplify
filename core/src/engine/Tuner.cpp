#include "namplify/engine/Tuner.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace nmp
{

const char* midiNoteName (int midiNote)
{
    static const char* const names[12] =
        { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int n = midiNote % 12;
    if (n < 0) n += 12;
    return names[n];
}

void Tuner::prepare (double sr)
{
    sampleRate = sr;

    // Das Fenster muss zwei Perioden des tiefsten Tons fassen. Bei 28 Hz und 48
    // kHz gut 3400 Samples, daher 4096, bei höheren Raten mehr.
    const int wanted = (int) std::ceil (sampleRate / kMinHz * 2.4);
    windowSize = 1;
    while (windowSize < wanted) windowSize <<= 1;
    windowSize = clamp (windowSize, 2048, 32768);

    maxLag = (int) std::ceil (sampleRate / kMinHz);
    minLag = std::max (2, (int) std::floor (sampleRate / kMaxHz));
    maxLag = std::min (maxLag, windowSize / 2 - 1);

    fftOrder = 1;
    while ((1 << fftOrder) < windowSize * 2) ++fftOrder;
    fft.prepare (fftOrder);

    const int fftSize = 1 << fftOrder;
    full.assign ((size_t) fftSize, 0.0f);
    part.assign ((size_t) fftSize, 0.0f);
    corr.assign ((size_t) fftSize, 0.0f);
    aRe .assign ((size_t) fft.numBins(), 0.0f);
    aIm .assign ((size_t) fft.numBins(), 0.0f);
    bRe .assign ((size_t) fft.numBins(), 0.0f);
    bIm .assign ((size_t) fft.numBins(), 0.0f);
    pRe .assign ((size_t) fft.numBins(), 0.0f);
    pIm .assign ((size_t) fft.numBins(), 0.0f);
    cumMean .assign ((size_t) maxLag + 1, 0.0);
    prefixSq.assign ((size_t) windowSize + 1, 0.0);

    ringSize = windowSize * 2;
    ring.assign ((size_t) ringSize, 0.0f);
    writePos.store (0);

    reset();
}

void Tuner::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writePos.store (0);
    heldFreq = 0.0f;
    heldNote = -1;
    stableCount = 0;
    silentCount = 0;
    outHas.store (false);
    outNote.store (-1);
    outFreq.store (0.0f);
    outCents.store (0.0f);
    outConf.store (0.0f);
}

void Tuner::push (const float* mono, int numFrames)
{
    if (ringSize <= 0) return;

    int w = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < numFrames; ++i)
    {
        ring[(size_t) w] = mono[i];
        if (++w >= ringSize) w = 0;
    }
    writePos.store (w, std::memory_order_release);
}

bool Tuner::analyse()
{
    if (ringSize <= 0) return false;

    const int w = writePos.load (std::memory_order_acquire);

    // Das letzte volle Fenster aus dem Ringpuffer holen.
    for (int i = 0; i < windowSize; ++i)
    {
        int idx = w - windowSize + i;
        while (idx < 0) idx += ringSize;
        full[(size_t) i] = ring[(size_t) (idx % ringSize)];
    }

    // Unter dieser Schwelle wird kein Ton gemeldet.
    double energy = 0.0;
    float  peak = 0.0f;
    for (int i = 0; i < windowSize; ++i)
    {
        energy += (double) full[(size_t) i] * full[(size_t) i];
        peak = std::max (peak, std::abs (full[(size_t) i]));
    }
    const double rmsLevel = std::sqrt (energy / windowSize);

    if (rmsLevel < 0.0009 || peak < 0.004f)
    {
        if (++silentCount > 2)
        {
            TunerReading r;
            publish (r);
            heldNote = -1;
            heldFreq = 0.0f;
        }
        return true;
    }
    silentCount = 0;

    const int fftSize = 1 << fftOrder;
    const int W = windowSize - maxLag;      // Länge des Vergleichsfensters
    if (W < 64) return false;

    // YIN-Differenzfunktion über die FFT:
    // d(τ) = Σ_{j<W} (x[j] − x[j+τ])²
    //      = Σ_{j<W} x[j]² + Σ_{j<W} x[j+τ]² − 2·Σ_{j<W} x[j]·x[j+τ]
    // Der letzte Term läuft nur über die ersten W Werte. Er wird deshalb als
    // Kreuzkorrelation aus dem gekürzten und dem vollen Fenster berechnet,
    // nicht als Autokorrelation über |X|².
    std::fill (full.begin() + windowSize, full.end(), 0.0f);
    std::copy (full.begin(), full.begin() + W, part.begin());
    std::fill (part.begin() + W, part.end(), 0.0f);

    fft.forward (full.data(), aRe.data(), aIm.data());
    fft.forward (part.data(), bRe.data(), bIm.data());

    for (int k = 0; k < fft.numBins(); ++k)
    {
        // conj(B) · A
        pRe[(size_t) k] = bRe[(size_t) k] * aRe[(size_t) k] + bIm[(size_t) k] * aIm[(size_t) k];
        pIm[(size_t) k] = bRe[(size_t) k] * aIm[(size_t) k] - bIm[(size_t) k] * aRe[(size_t) k];
    }

    fft.inverse (pRe.data(), pIm.data(), corr.data());
    (void) fftSize;

    // Präfixsummen der Quadrate für die Energieterme.
    prefixSq[0] = 0.0;
    for (int i = 0; i < windowSize; ++i)
    {
        const double v = full[(size_t) i];
        prefixSq[(size_t) i + 1] = prefixSq[(size_t) i] + v * v;
    }

    double running = 0.0;
    double bestValue = 1.0e30;
    int    bestLag = -1;

    cumMean[0] = 1.0;

    for (int tau = 1; tau <= maxLag; ++tau)
    {
        const double p0  = prefixSq[(size_t) W];
        const double pT  = prefixSq[(size_t) (W + tau)] - prefixSq[(size_t) tau];
        const double x   = (double) corr[(size_t) tau];

        const double d = std::max (0.0, p0 + pT - 2.0 * x);

        running += d;
        cumMean[(size_t) tau] = running > 1.0e-12 ? d * tau / running : 1.0;
    }

    // Erster Wert unter der Schwelle, der auch ein lokales Minimum ist.
    constexpr double kThreshold = 0.14;
    for (int tau = minLag; tau <= maxLag - 1; ++tau)
    {
        if (cumMean[(size_t) tau] < kThreshold)
        {
            while (tau + 1 <= maxLag && cumMean[(size_t) (tau + 1)] < cumMean[(size_t) tau]) ++tau;
            bestLag = tau;
            bestValue = cumMean[(size_t) tau];
            break;
        }
    }

    if (bestLag < 0)
    {
        // Nichts unter der Schwelle: das kleinste Minimum nehmen und eine
        // niedrige Güte melden.
        for (int tau = minLag; tau <= maxLag; ++tau)
            if (cumMean[(size_t) tau] < bestValue) { bestValue = cumMean[(size_t) tau]; bestLag = tau; }
    }

    if (bestLag <= 0 || bestValue > 0.55)
    {
        TunerReading r;
        publish (r);
        return true;
    }

    // Parabolische Interpolation zwischen den Lags, sonst ist die Auflösung bei
    // hohen Tönen gröber als ein Cent.
    double lag = bestLag;
    if (bestLag > minLag && bestLag < maxLag)
    {
        const double a = cumMean[(size_t) (bestLag - 1)];
        const double b = cumMean[(size_t) bestLag];
        const double c = cumMean[(size_t) (bestLag + 1)];
        const double den = 2.0 * (2.0 * b - a - c);
        if (std::abs (den) > 1.0e-12) lag = bestLag + (c - a) / den;
    }

    const float freq = (float) (sampleRate / lag);
    if (freq < kMinHz || freq > kMaxHz)
    {
        TunerReading r;
        publish (r);
        return true;
    }

    // Glättung nur, solange der Halbton gleich bleibt. Bei einem Wechsel
    // springt die Anzeige sofort.
    const double midiExact = 69.0 + 12.0 * std::log2 ((double) freq / 440.0);
    const int    note = (int) std::lround (midiExact);

    if (note != heldNote)
    {
        heldNote = note;
        heldFreq = freq;
        stableCount = 0;
    }
    else
    {
        // Je länger derselbe Ton steht, desto ruhiger die Anzeige.
        const float a = stableCount < 3 ? 0.55f : 0.18f;
        heldFreq += a * (freq - heldFreq);
        if (stableCount < 100) ++stableCount;
    }

    TunerReading r;
    r.hasPitch    = true;
    r.frequencyHz = heldFreq;
    r.midiNote    = note;
    r.octave      = note / 12 - 1;
    r.cents       = (float) (100.0 * (69.0 + 12.0 * std::log2 ((double) heldFreq / 440.0) - note));
    r.confidence  = (float) clamp (1.0 - bestValue / 0.55, 0.0, 1.0);

    const char* n = midiNoteName (note);
    r.noteName[0] = n[0];
    r.noteName[1] = n[1];
    r.noteName[2] = 0;

    publish (r);
    return true;
}

void Tuner::publish (const TunerReading& r)
{
    outFreq .store (r.frequencyHz, std::memory_order_relaxed);
    outCents.store (r.cents,       std::memory_order_relaxed);
    outNote .store (r.hasPitch ? r.midiNote : -1, std::memory_order_relaxed);
    outConf .store (r.confidence,  std::memory_order_relaxed);
    outHas  .store (r.hasPitch,    std::memory_order_release);
}

TunerReading Tuner::reading() const
{
    TunerReading r;
    r.hasPitch = outHas.load (std::memory_order_acquire);
    if (! r.hasPitch) return r;

    r.frequencyHz = outFreq.load (std::memory_order_relaxed);
    r.cents       = outCents.load (std::memory_order_relaxed);
    r.midiNote    = outNote.load (std::memory_order_relaxed);
    r.confidence  = outConf.load (std::memory_order_relaxed);
    r.octave      = r.midiNote / 12 - 1;

    const char* n = midiNoteName (r.midiNote);
    r.noteName[0] = n[0];
    r.noteName[1] = n[1];
    r.noteName[2] = 0;
    return r;
}

} // namespace nmp
