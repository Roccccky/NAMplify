#pragma once
// DELAY mit vier Typen.
//
// TIME-Änderungen verschieben keinen Lesezeiger, das würde knacken und die
// Wiederholungen verstimmen. Stattdessen zwei Abgriffe mit fester Zeit und
// Überblendung (XfadeDelayTap).
//
// Im Feedback-Pfad sitzen Hoch- und Tiefpass, damit sich bei hohem Feedback
// weder Höhen noch Bass aufschaukeln.

#include "Block.h"
#include "../dsp/DelayLine.h"
#include "../dsp/Biquad.h"
#include "../dsp/Lfo.h"
#include "../dsp/Waveshaper.h"
#include "../util/Smoothed.h"

namespace nmp
{

enum class DelayType : int { tape = 0, analog, digital, ping, count };

const char* delayTypeName (DelayType t);

class DelayBlock : public Block
{
public:
    static constexpr float kMaxTimeMs = 2000.0f;

    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setType (DelayType t);
    void setTimeMs (float ms)  { timeMs = clamp (ms, 20.0f, kMaxTimeMs); }
    void setFeedback (float f) { fdbk   = clamp (f, 0.0f, 0.95f); }
    void setMix (float m)      { mix    = clamp (m, 0.0f, 1.0f); }
    void setSync (bool on, NoteDiv d) { syncOn = on; div = d; }

    DelayType type() const { return currentType; }
    float     effectiveTimeMs() const { return lastTimeMs; }

private:
    void updateFilters();

    double sampleRate = 48000.0;

    DelayType currentType = DelayType::tape;
    float timeMs = 375.0f, fdbk = 0.3f, mix = 0.25f;
    bool  syncOn = false;
    NoteDiv div = NoteDiv::quarter;
    float lastTimeMs = 375.0f;

    DelayLine     lineL, lineR;
    XfadeDelayTap tapL, tapR;

    Biquad fbLpL, fbLpR, fbHpL, fbHpR;
    Lfo    wow, flutter;

    LinearSmoothed mixSm, fbSm;
    bool  dirty = true;
    bool  pingSide = false;
};

} // namespace nmp
