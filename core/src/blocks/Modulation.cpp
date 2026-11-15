#include "namplify/blocks/Modulation.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

void ChorusBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;

    lineL.prepare (sampleRate, 0.060);
    lineR.prepare (sampleRate, 0.060);

    lfo.prepare (sampleRate);
    lfo.setShape (Lfo::Shape::sine);

    // Hochpass im nassen Weg, schwankende Bässe klingen verstimmt.
    const auto hp = designHighPass (180.0, 0.707, sampleRate);
    wetHpL.setCoeffs (hp);
    wetHpR.setCoeffs (hp);

    mixSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTarget (mix);

    reset();
}

void ChorusBlock::reset()
{
    lineL.reset(); lineR.reset();
    wetHpL.reset(); wetHpR.reset();
    lfo.reset();
}

void ChorusBlock::process (StereoBlock& block, const BlockContext&)
{
    lfo.setRate (rateHz);
    mixSm.setTarget (mix);

    // Tiefe in ms, aber begrenzt auf kMaxDetune.
    const float wanted   = depth * 6.0f;                                   // ms
    const float allowed  = kMaxDetune / (float) (kTwoPi * std::max (0.05f, rateHz)) * 1000.0f;
    const float depthMs  = std::min (wanted, allowed);
    const float depthSmp = depthMs * 0.001f * (float) sampleRate;

    // Vier Stimmen, Phasen gleichmäßig verteilt, links und rechts um eine
    // Vierteldrehung versetzt.
    const double phases[kVoices] = { 0.0, 0.25, 0.5, 0.75 };
    const float  baseMs[kVoices] = { 11.0f, 15.0f, 19.0f, 23.0f };

    for (int i = 0; i < block.numFrames; ++i)
    {
        const float dryL = block.left[i];
        const float dryR = block.right[i];

        lineL.write (dryL);
        lineR.write (dryR);

        float wetL = 0.0f, wetR = 0.0f;

        for (int v = 0; v < kVoices; ++v)
        {
            const float base = baseMs[v] * 0.001f * (float) sampleRate;
            const float mL = lfo.peekOffset (phases[v]);
            const float mR = lfo.peekOffset (phases[v] + 0.25);

            wetL += lineL.readAt (base + depthSmp * mL);
            wetR += lineR.readAt (base + depthSmp * mR);
        }

        wetL = wetHpL.process (wetL * (1.0f / kVoices));
        wetR = wetHpR.process (wetR * (1.0f / kVoices));

        lfo.next();

        const float m = mixSm.next();
        block.left[i]  = dryL + wetL * m;
        block.right[i] = dryR + wetR * m;
    }
}

void FlangerBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;

    dryL.prepare (sampleRate, 0.040);
    dryR.prepare (sampleRate, 0.040);
    wetL.prepare (sampleRate, 0.040);
    wetR.prepare (sampleRate, 0.040);

    lfo.prepare (sampleRate);
    lfo.setShape (Lfo::Shape::sine);

    // Hochpass im Feedback, damit die Kerben nicht in den Bass reichen.
    const auto hp = designHighPass (140.0, 0.707, sampleRate);
    fbHpL.setCoeffs (hp);
    fbHpR.setCoeffs (hp);

    // Etwas Tiefpass im Feedback gegen Pfeifen bei ±0,9.
    const auto lp = designLowPass (9000.0, 0.707, sampleRate);
    fbLpL.setCoeffs (lp);
    fbLpR.setCoeffs (lp);

    mixSm.reset (sampleRate, 0.02);
    fbSm .reset (sampleRate, 0.02);
    mixSm.setCurrentAndTarget (mix);
    fbSm .setCurrentAndTarget (fdbk);

    reset();
}

void FlangerBlock::reset()
{
    dryL.reset(); dryR.reset(); wetL.reset(); wetR.reset();
    fbHpL.reset(); fbHpR.reset(); fbLpL.reset(); fbLpR.reset();
    fbStateL = fbStateR = 0.0f;
    lfo.reset();
}

void FlangerBlock::process (StereoBlock& block, const BlockContext&)
{
    lfo.setRate (rateHz);
    mixSm.setTarget (mix);
    fbSm .setTarget (fdbk);

    // MANUAL verschiebt die Mitte der Modulation: 1 … 9 ms.
    const float centreMs = mapTo (manual, 1.0f, 9.0f);
    const float centre   = centreMs * 0.001f * (float) sampleRate;

    // Die Modulation geht durch die Mitte hindurch, bis auf einen kleinen Rest
    // für gültige Lesepositionen.
    const float swing = depth * (centre - 1.5f);

    for (int i = 0; i < block.numFrames; ++i)
    {
        const float inL = block.left[i];
        const float inR = block.right[i];

        const float g = fbSm.next();

        dryL.write (inL);
        dryR.write (inR);
        wetL.write (inL + fbStateL * g);
        wetR.write (inR + fbStateR * g);

        const float mL = lfo.peekOffset (0.0);
        const float mR = lfo.peekOffset (0.08);   // leichter Versatz für Breite
        lfo.next();

        const float dL = dryL.readAt (centre);
        const float dR = dryR.readAt (centre);
        const float wL = wetL.readAt (centre + swing * mL);
        const float wR = wetR.readAt (centre + swing * mR);

        fbStateL = fbLpL.process (fbHpL.process (wL));
        fbStateR = fbLpR.process (fbHpR.process (wR));

        const float m = mixSm.next();

        // Nur der nasse Weg ist gefiltert (fbHp/fbLp). Sonst verdoppelt sich
        // beim Durchgang durch die Mitte auch der Bass.
        block.left[i]  = dL + m * fbStateL;
        block.right[i] = dR + m * fbStateR;
    }
}

void PhaserBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;
    lfo.prepare (sampleRate);
    lfo.setShape (Lfo::Shape::triangle);

    mixSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTarget (mix);

    reset();
}

void PhaserBlock::reset()
{
    for (auto& a : apL) a.z = 0.0f;
    for (auto& a : apR) a.z = 0.0f;
    fbL = fbR = 0.0f;
    lfo.reset();
}

void PhaserBlock::process (StereoBlock& block, const BlockContext&)
{
    lfo.setRate (rateHz);
    mixSm.setTarget (mix);

    constexpr float kFeedback = 0.55f;

    for (int i = 0; i < block.numFrames; ++i)
    {
        // Links und rechts um eine Vierteldrehung versetzt.
        const float sL = lfo.peekOffset (0.0);
        const float sR = lfo.peekOffset (0.25);
        lfo.next();

        auto coeffFor = [&] (float s)
        {
            const float unit = 0.5f * (s + 1.0f);
            const float lo = 260.0f;
            const float hi = mapTo (depth, 900.0f, 4200.0f);
            const float f  = mapToLog (unit, lo, hi);
            // Allpass erster Ordnung: a = (tan(πf/fs) − 1) / (tan(πf/fs) + 1)
            const float t = std::tan ((float) kPi * f / (float) sampleRate);
            return (t - 1.0f) / (t + 1.0f);
        };

        const float aL = coeffFor (sL);
        const float aR = coeffFor (sR);

        float vL = block.left[i]  + fbL * kFeedback * depth;
        float vR = block.right[i] + fbR * kFeedback * depth;

        for (auto& a : apL) vL = a.process (vL, aL);
        for (auto& a : apR) vR = a.process (vR, aR);

        fbL = flushDenormal (vL);
        fbR = flushDenormal (vR);

        const float m = mixSm.next();
        block.left[i]  = block.left[i]  + vL * m;
        block.right[i] = block.right[i] + vR * m;
    }
}

void TremoloBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;
    lfo.prepare (sampleRate);

    depthSm.reset (sampleRate, 0.02);
    depthSm.setCurrentAndTarget (depth);

    reset();
}

void TremoloBlock::reset()
{
    lfo.reset();
}

void TremoloBlock::process (StereoBlock& block, const BlockContext& ctx)
{
    // Mit Tempo-Sync eine volle Schwingung pro Notenwert, sonst gilt der eigene
    // Regler.
    float hz = rateHz;
    if (syncOn && ctx.clockRunning)
        hz = (float) (1.0 / std::max (1.0e-3, noteDivSeconds (div, ctx.bpm)));

    lfo.setRate (hz);
    depthSm.setTarget (depth);

    for (int i = 0; i < block.numFrames; ++i)
    {
        const float d = depthSm.next();

        // Kurvenform hängt von der Tiefe ab: unten weich, oben eckig.
        const float hard = clamp ((d - 0.5f) * 2.0f, 0.0f, 1.0f);
        const float s    = lfo.peekOffset (0.0);
        const float sq   = std::tanh (3.0f * s);
        const float wave = s * (1.0f - hard) + sq * hard;
        lfo.next();

        // 1 → 1−d … 1 (nie über eins)
        const float g = 1.0f - d * 0.5f * (1.0f - wave);

        block.left[i]  *= g;
        block.right[i] *= g;
    }
}

} // namespace nmp
