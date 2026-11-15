#pragma once
// Reelle FFT, Radix-2, ohne externe Bibliothek. Für die Cab-Faltung und
// Messungen in den Tests. Alloziert nicht im Audio-Thread.

#include "Types.h"
#include <complex>
#include <vector>

namespace nmp
{

/// Komplexe FFT über 2^order Punkte, an Ort und Stelle.
class Fft
{
public:
    void prepare (int order);

    int order() const { return fftOrder; }
    int size()  const { return fftSize; }

    /// Vorwärts ohne Skalierung, rückwärts mit 1/N.
    void forward (float* re, float* im) const;
    void inverse (float* re, float* im) const;

private:
    void run (float* re, float* im, bool inverseTransform) const;

    int fftOrder = 0, fftSize = 0;
    std::vector<int>   bitRev;
    std::vector<float> twiddleRe, twiddleIm;
};

/// Reelle FFT über N = 2^order Punkte. Ergebnis: N/2+1 komplexe Werte in
/// getrennten Feldern. Nutzt eine halb so große komplexe FFT.
class RealFft
{
public:
    void prepare (int order);

    int size()     const { return n; }
    int numBins()  const { return n / 2 + 1; }

    /// time[n] → re[n/2+1], im[n/2+1]
    void forward (const float* time, float* re, float* im) const;

    /// re/im[n/2+1] → time[n]   (mit 1/N)
    void inverse (const float* re, const float* im, float* time) const;

private:
    Fft half;
    int n = 0;
    std::vector<float> cosTab, sinTab;
    mutable std::vector<float> workRe, workIm;
};

} // namespace nmp
