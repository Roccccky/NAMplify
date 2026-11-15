#pragma once
// Stimmgerät. Bereich von der tiefen H-Saite eines Fünfsaiters bis in hohe
// Lagen.
//
// Der Audio-Thread schreibt nur in einen Ringpuffer, das kostet in jedem Block
// gleich wenig. Die Erkennung läuft in einem anderen Thread, den das Plugin
// bestimmt.

#include "../util/Types.h"
#include "../util/Fft.h"
#include <atomic>
#include <string>
#include <vector>

namespace nmp
{

struct TunerReading
{
    bool  hasPitch = false;
    float frequencyHz = 0.0f;
    float cents = 0.0f;        ///< −50 … +50 zum nächsten Halbton
    int   midiNote = 0;
    int   octave = 0;
    char  noteName[3] = { '-', 0, 0 };
    float confidence = 0.0f;
};

class Tuner
{
public:
    /// Tiefstes H eines Fünfsaiters (30,87 Hz) bis weit über das hohe E.
    static constexpr float kMinHz = 28.0f;
    static constexpr float kMaxHz = 1500.0f;

    void prepare (double sampleRate);
    void reset();

    /// Audio-Thread. Nur schreiben.
    void push (const float* mono, int numFrames);

    /// Nicht im Audio-Thread. Wertet den Ringpuffer aus, true wenn sich etwas
    /// geändert hat.
    bool analyse();

    /// Letztes Ergebnis, von jedem Thread lesbar.
    TunerReading reading() const;

private:
    void publish (const TunerReading&);

    double sampleRate = 48000.0;
    int    windowSize = 4096;
    int    maxLag = 0, minLag = 0;

    std::vector<float> ring;
    std::atomic<int>   writePos { 0 };
    int                ringSize = 0;

    RealFft fft;
    int fftOrder = 13;
    std::vector<float> full, part, corr;
    std::vector<float> aRe, aIm, bRe, bIm, pRe, pIm;
    std::vector<double> cumMean, prefixSq;

    // Glättung
    float  heldFreq = 0.0f;
    int    heldNote = -1;
    int    stableCount = 0;
    int    silentCount = 0;

    std::atomic<float> outFreq { 0.0f };
    std::atomic<float> outCents { 0.0f };
    std::atomic<int>   outNote { -1 };
    std::atomic<float> outConf { 0.0f };
    std::atomic<bool>  outHas { false };
};

/// Name eines Halbtons.
const char* midiNoteName (int midiNote);

} // namespace nmp
