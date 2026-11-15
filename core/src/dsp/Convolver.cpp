#include "namplify/dsp/Convolver.h"
#include <algorithm>
#include <vector>

namespace nmp
{

void Convolver::prepare (int /*maxBlockSize*/)
{
    clear();
}

void Convolver::clear()
{
    numTaps  = 0;
    numParts = 0;
    directTaps.clear();
    filterRe.clear();
    filterIm.clear();
    reset();
}

void Convolver::reset()
{
    std::fill (directHist.begin(), directHist.end(), 0.0f);
    std::fill (fdlRe.begin(),      fdlRe.end(),      0.0f);
    std::fill (fdlIm.begin(),      fdlIm.end(),      0.0f);
    std::fill (blockIn.begin(),    blockIn.end(),    0.0f);
    std::fill (blockPrev.begin(),  blockPrev.end(),  0.0f);
    std::fill (fftOut.begin(),     fftOut.end(),     0.0f);
    directPos = 0;
    fdlHead   = 0;
    phase     = 0;
    primed    = false;
}

void Convolver::setImpulseResponse (const float* ir, int n)
{
    n = clamp (n, 0, kMaxIrSamples);

    if (n <= 0) { clear(); return; }

    // Blockgröße nach IR-Länge: der direkte Teil kostet P Operationen pro
    // Sample, der Frequenzteil wird mit größerem P billiger.
    partSize = n <= 1024 ? 64 : (n <= 8192 ? 128 : 256);
    fftSize  = partSize * 2;
    numBins  = fftSize / 2 + 1;

    int order = 0;
    while ((1 << order) < fftSize) ++order;
    fft.prepare (order);

    numTaps = n;

    // direkter Teil: die ersten partSize Taps
    directTaps.assign ((size_t) partSize, 0.0f);
    for (int i = 0; i < std::min (n, partSize); ++i)
        directTaps[(size_t) i] = ir[i];
    directHist.assign ((size_t) partSize, 0.0f);

    // Frequenzteil: alles ab Tap partSize
    const int rest = std::max (0, n - partSize);
    numParts = (rest + partSize - 1) / partSize;

    filterRe.assign ((size_t) std::max (1, numParts) * numBins, 0.0f);
    filterIm.assign ((size_t) std::max (1, numParts) * numBins, 0.0f);

    std::vector<float> tmp ((size_t) fftSize, 0.0f);
    for (int p = 0; p < numParts; ++p)
    {
        std::fill (tmp.begin(), tmp.end(), 0.0f);
        const int from = partSize + p * partSize;
        const int cnt  = std::min (partSize, n - from);
        for (int i = 0; i < cnt; ++i)
            tmp[(size_t) i] = ir[from + i];

        fft.forward (tmp.data(),
                     &filterRe[(size_t) p * numBins],
                     &filterIm[(size_t) p * numBins]);
    }

    fdlRe.assign ((size_t) std::max (1, numParts) * numBins, 0.0f);
    fdlIm.assign ((size_t) std::max (1, numParts) * numBins, 0.0f);

    blockIn.assign   ((size_t) partSize, 0.0f);
    blockPrev.assign ((size_t) partSize, 0.0f);
    scratchTime.assign ((size_t) fftSize, 0.0f);
    accRe.assign ((size_t) numBins, 0.0f);
    accIm.assign ((size_t) numBins, 0.0f);
    fftOut.assign ((size_t) partSize, 0.0f);

    reset();
}

void Convolver::accumulateSpectra()
{
    if (numParts <= 0)
    {
        std::fill (fftOut.begin(), fftOut.end(), 0.0f);
        return;
    }

    std::fill (accRe.begin(), accRe.end(), 0.0f);
    std::fill (accIm.begin(), accIm.end(), 0.0f);

    // Teil 1 trifft auf den zuletzt abgelegten Block, Teil 2 auf den davor …
    for (int p = 0; p < numParts; ++p)
    {
        int slot = fdlHead - p;
        while (slot < 0) slot += numParts;

        const float* xr = &fdlRe[(size_t) slot * numBins];
        const float* xi = &fdlIm[(size_t) slot * numBins];
        const float* hr = &filterRe[(size_t) p * numBins];
        const float* hi = &filterIm[(size_t) p * numBins];

        for (int k = 0; k < numBins; ++k)
        {
            accRe[(size_t) k] += xr[k] * hr[k] - xi[k] * hi[k];
            accIm[(size_t) k] += xr[k] * hi[k] + xi[k] * hr[k];
        }
    }

    fft.inverse (accRe.data(), accIm.data(), scratchTime.data());

    // Overlap-Save: gültig ist die zweite Hälfte.
    std::copy (scratchTime.begin() + partSize, scratchTime.end(), fftOut.begin());
}

void Convolver::transformCurrentBlock()
{
    if (numParts <= 0) { std::swap (blockPrev, blockIn); return; }

    std::copy (blockPrev.begin(), blockPrev.end(), scratchTime.begin());
    std::copy (blockIn.begin(),   blockIn.end(),   scratchTime.begin() + partSize);

    fdlHead = (fdlHead + 1) % numParts;

    fft.forward (scratchTime.data(),
                 &fdlRe[(size_t) fdlHead * numBins],
                 &fdlIm[(size_t) fdlHead * numBins]);

    std::swap (blockPrev, blockIn);
}

void Convolver::process (float* data, int numFrames)
{
    if (numTaps <= 0) return;

    const int P = partSize;
    int i = 0;

    while (i < numFrames)
    {
        if (phase == 0)
        {
            // Der Frequenzteil dieses Blocks hängt nur an schon fertigen
            // Blöcken und steht vorab fest.
            accumulateSpectra();
        }

        const int count = std::min (P - phase, numFrames - i);

        for (int k = 0; k < count; ++k)
        {
            const float x = data[i + k];

            directHist[(size_t) directPos] = x;

            float acc = 0.0f;
            int   idx = directPos;
            for (int t = 0; t < P; ++t)
            {
                acc += directTaps[(size_t) t] * directHist[(size_t) idx];
                if (--idx < 0) idx = P - 1;
            }

            if (++directPos >= P) directPos = 0;

            blockIn[(size_t) (phase + k)] = x;
            data[i + k] = acc + fftOut[(size_t) (phase + k)];
        }

        phase += count;
        i     += count;

        if (phase >= P)
        {
            transformCurrentBlock();
            phase = 0;
        }
    }
}

} // namespace nmp
