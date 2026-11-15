#pragma once
// Faltung mit einer Impulsantwort (Cab).
//
// Gleichmäßige Last bei kleiner Latenz:
// - Die ersten P Taps werden direkt im Zeitbereich gerechnet.
// - Der Rest wird in gleich große Teile zerlegt und im Frequenzbereich aus
// bereits fertigen Blöcken summiert.
//
// Ergebnis: keine Latenz und pro P Samples immer derselbe Aufwand. Eine
// ungleich partitionierte Faltung wäre billiger, hätte aber Lastspitzen.

#include "../util/Types.h"
#include "../util/Fft.h"
#include <vector>

namespace nmp
{

class Convolver
{
public:
    /// Maximale IR-Länge: eine halbe Sekunde bei 192 kHz.
    static constexpr int kMaxIrSamples = 96000;

    void prepare (int maxBlockSize);

    /// Neue Impulsantwort setzen. Alloziert, nicht aus dem Audio-Thread
    /// aufrufen.
    void setImpulseResponse (const float* ir, int numSamples);

    void clear();
    void reset();

    bool  hasIr() const { return numTaps > 0; }
    int   irLength() const { return numTaps; }
    int   latencySamples() const { return 0; }
    int   partitionSize() const { return partSize; }

    void process (float* data, int numFrames);

private:
    void transformCurrentBlock();
    void accumulateSpectra();

    // direkter Teil
    std::vector<float> directTaps;    // die ersten partSize Taps
    std::vector<float> directHist;    // Ringspeicher, Länge partSize
    int directPos = 0;

    // Frequenzteil
    RealFft fft;
    int partSize = 64, fftSize = 128, numBins = 65, numParts = 0;

    std::vector<float> filterRe, filterIm;   // numParts × numBins (ab Stück 1)
    std::vector<float> fdlRe, fdlIm;         // numParts × numBins
    int fdlHead = 0;

    std::vector<float> blockIn, blockPrev;   // je partSize
    std::vector<float> scratchTime;
    std::vector<float> accRe, accIm;
    std::vector<float> fftOut;               // partSize Werte für den laufenden Block

    int  numTaps = 0;
    int  phase   = 0;
    bool primed  = false;
};

} // namespace nmp
