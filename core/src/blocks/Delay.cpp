#include "namplify/blocks/Delay.h"

namespace nmp
{

const char* delayTypeName (DelayType t)
{
    switch (t)
    {
        case DelayType::tape:    return "TAPE";
        case DelayType::analog:  return "ANALOG";
        case DelayType::digital: return "DIGITAL";
        case DelayType::ping:    return "PING";
        default:                 return "TAPE";
    }
}

void DelayBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;

    const double maxSec = kMaxTimeMs * 0.001 + 0.05;
    lineL.prepare (sampleRate, maxSec);
    lineR.prepare (sampleRate, maxSec);

    tapL.prepare (sampleRate, 0.035);
    tapR.prepare (sampleRate, 0.035);

    wow    .prepare (sampleRate);
    flutter.prepare (sampleRate);
    wow    .setRate (0.55f);
    flutter.setRate (6.3f);
    wow    .setShape (Lfo::Shape::sine);
    flutter.setShape (Lfo::Shape::triangle);

    mixSm.reset (sampleRate, 0.02);
    fbSm .reset (sampleRate, 0.05);
    mixSm.setCurrentAndTarget (mix);
    fbSm .setCurrentAndTarget (fdbk);

    dirty = true;
    updateFilters();
    reset();
}

void DelayBlock::reset()
{
    lineL.reset(); lineR.reset();
    tapL.reset();  tapR.reset();
    fbLpL.reset(); fbLpR.reset(); fbHpL.reset(); fbHpR.reset();
    wow.reset();   flutter.reset();
    pingSide = false;
}

void DelayBlock::setType (DelayType t)
{
    if (t == currentType) return;
    currentType = t;
    dirty = true;
}

void DelayBlock::updateFilters()
{
    if (! dirty) return;
    dirty = false;

    double lpHz = 12000.0, hpHz = 90.0;

    switch (currentType)
    {
        case DelayType::tape:
            // Jede Wiederholung wird dunkler.
            lpHz = 3800.0; hpHz = 130.0;
            break;
        case DelayType::analog:
            // Dunkler als TAPE, die Wiederholungen werden weich und dumpf.
            lpHz = 2400.0; hpHz = 110.0;
            break;
        case DelayType::digital:
            // Linear. Der Hochpass bleibt trotzdem, sonst wächst bei 0,95
            // Feedback der Bass an.
            lpHz = 15000.0; hpHz = 45.0;
            break;
        case DelayType::ping:
        default:
            lpHz = 11000.0; hpHz = 60.0;
            break;
    }

    const auto lp = designLowPass  (lpHz, 0.707, sampleRate);
    const auto hp = designHighPass (hpHz, 0.707, sampleRate);

    fbLpL.setCoeffs (lp); fbLpR.setCoeffs (lp);
    fbHpL.setCoeffs (hp); fbHpR.setCoeffs (hp);
}

void DelayBlock::process (StereoBlock& block, const BlockContext& ctx)
{
    updateFilters();

    // Mit Tempo-Sync folgt die Zeit dem Notenwert. Der Regler behält seinen
    // Wert und gilt wieder, sobald kein Tempo mehr kommt.
    float ms = timeMs;
    if (syncOn && ctx.clockRunning)
        ms = (float) clamp (noteDivSeconds (div, ctx.bpm) * 1000.0, 20.0, (double) kMaxTimeMs);
    lastTimeMs = ms;

    const float samples = ms * 0.001f * (float) sampleRate;

    // PING läuft mit halber Zeit pro Seite, die Wiederholungen wechseln
    // zwischen links und rechts.
    const bool  isPing = currentType == DelayType::ping;
    const float dL = isPing ? samples * 0.5f : samples;
    const float dR = samples;

    tapL.setDelaySamples (dL);
    tapR.setDelaySamples (dR);

    mixSm.setTarget (mix);
    fbSm .setTarget (fdbk);

    const bool  isTape  = currentType == DelayType::tape;
    const bool  saturates = isTape || currentType == DelayType::analog;
    const float wowDepth = isTape ? 0.0016f * (float) sampleRate : 0.0f;   // ±1,6 ms

    for (int i = 0; i < block.numFrames; ++i)
    {
        const float inL = block.left[i];
        const float inR = block.right[i];

        // Wow/Flutter verschiebt beide Köpfe gemeinsam.
        float mod = 0.0f;
        if (isTape)
        {
            const float w = wow.next() * 0.75f + flutter.next() * 0.25f;
            mod = w * wowDepth;
        }

        const float outL = tapL.read (lineL, mod);
        const float outR = tapR.read (lineR, mod * 0.83f);

        float fL = fbHpL.process (fbLpL.process (outL));
        float fR = fbHpR.process (fbLpR.process (outR));

        if (saturates)
        {
            // Weiche Sättigung im Feedback begrenzt das Aufschaukeln und rundet
            // späte Wiederholungen ab.
            fL = shape::softOdd (fL * 1.2f) * 0.83f;
            fR = shape::softOdd (fR * 1.2f) * 0.83f;
        }

        const float g = fbSm.next();

        if (isPing)
        {
            // Über Kreuz: links raus, rechts wieder rein.
            lineL.write (inL + fR * g);
            lineR.write (inR + fL * g);
        }
        else
        {
            lineL.write (inL + fL * g);
            lineR.write (inR + fR * g);
        }

        const float m = mixSm.next();
        block.left[i]  = inL + outL * m;
        block.right[i] = inR + outR * m;
    }
}

} // namespace nmp
