#pragma once
// Stereobreite.
//
// Gerechnet wird in Mitte/Seite. Die Mitte bleibt unverändert, damit kippt das
// Bild nicht und die Mono-Summe bleibt gleich. Zur Seite kommt ein Signal aus
// der Mitte dazu, das durch eine Allpass-Kette gestreut wird. Eine einfache
// Verzögerung würde einen Kammfilter erzeugen.

#include "../util/Types.h"
#include "../dsp/DelayLine.h"
#include "../util/Smoothed.h"

namespace nmp
{

class Spread
{
public:
    static constexpr float kOffMs = 1.0f;   ///< linker Anschlag = aus

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setMs (float ms) { targetMs = clamp (ms, 1.0f, 25.0f); }
    bool isOff() const { return targetMs <= kOffMs; }

    void process (StereoBlock&);

private:
    void rebuild (float ms);

    static constexpr int kStages = 4;
    AllpassDelay ap[kStages];
    DelayLine    line;

    double sampleRate = 48000.0;
    float  targetMs = 1.0f, builtMs = -1.0f;
    LinearSmoothed amount;
};

} // namespace nmp
