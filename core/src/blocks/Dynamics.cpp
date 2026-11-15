#include "namplify/blocks/Dynamics.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

namespace
{
    inline float coeffFor (double sr, double ms)
    {
        return ms <= 0.0 ? 1.0f : (float) (1.0 - std::exp (-1.0 / (sr * ms * 0.001)));
    }
}

void GateBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;

    // Sidechain-Hochpass bei 55 Hz: lässt tief gestimmte Instrumente durch,
    // hält Brummen draußen.
    const auto hp = designHighPass (55.0, 0.707, sampleRate);
    detectHpL.setCoeffs (hp);
    detectHpR.setCoeffs (hp);

    envAtk = coeffFor (sampleRate, 0.5);
    envRel = coeffFor (sampleRate, 25.0);

    // Schnell öffnen, damit kein Anschlag verschluckt wird. Langsam schließen,
    // damit der Ausklang bleibt.
    atkCoeff = coeffFor (sampleRate, 1.0);
    relCoeff = coeffFor (sampleRate, 180.0);

    holdSamples = (int) std::round (sampleRate * 0.040);   // 40 ms

    reset();
}

void GateBlock::reset()
{
    detectHpL.reset();
    detectHpR.reset();
    env = 0.0f;
    gainDb = 0.0f;
    holdCounter = 0;
    lastReductionDb = 0.0f;
}

void GateBlock::process (StereoBlock& block, const BlockContext&)
{
    if (isOff()) { lastReductionDb = 0.0f; return; }

    // 1:4. Mehr wäre wieder ein Gate.
    constexpr float kRatio = 4.0f;
    constexpr float kKneeDb = 8.0f;
    constexpr float kMaxReductionDb = 48.0f;

    const float thr = thresholdDb;

    for (int i = 0; i < block.numFrames; ++i)
    {
        const float dl = detectHpL.process (block.left[i]);
        const float dr = detectHpR.process (block.right[i]);
        const float mag = std::max (std::abs (dl), std::abs (dr));

        const float c = mag > env ? envAtk : envRel;
        env += c * (mag - env);
        env = flushDenormal (env);

        const float lvlDb = gainToDb (env);

        // Zielabsenkung mit weichem Übergang um die Schwelle.
        float wantDb;
        const float over = lvlDb - thr;
        if (over >= kKneeDb * 0.5f)
            wantDb = 0.0f;
        else if (over <= -kKneeDb * 0.5f)
            wantDb = (over + kKneeDb * 0.5f) * (kRatio - 1.0f);
        else
        {
            // Quadratischer Übergang um die Schwelle, ohne Kante.
            const float t = (over - kKneeDb * 0.5f);
            wantDb = (kRatio - 1.0f) * t * t / (2.0f * kKneeDb);
        }
        wantDb = std::max (wantDb, -kMaxReductionDb);

        if (wantDb >= gainDb - 0.001f)
        {
            // Öffnen: sofort, die Haltezeit startet neu.
            gainDb += atkCoeff * (wantDb - gainDb);
            if (wantDb > -0.5f) holdCounter = holdSamples;
        }
        else if (holdCounter > 0)
        {
            --holdCounter;      // halten
        }
        else
        {
            gainDb += relCoeff * (wantDb - gainDb);
        }

        const float g = dbToGain (gainDb);
        block.left[i]  *= g;
        block.right[i] *= g;
    }

    lastReductionDb = -gainDb;
}

void CompBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;

    detCoeff     = coeffFor (sampleRate, 2.0);
    atkCoeff     = coeffFor (sampleRate, 6.0);
    relFastCoeff = coeffFor (sampleRate, 70.0);
    relSlowCoeff = coeffFor (sampleRate, 420.0);

    outGain.reset (sampleRate, 0.02);
    outGain.setCurrentAndTarget (1.0f);

    reset();
}

void CompBlock::reset()
{
    detEnv = 0.0f;
    grFast = grSlow = 0.0f;
    lastReductionDb = 0.0f;
    outGain.setCurrentAndTarget (dbToGain (levelDb));
}

void CompBlock::process (StereoBlock& block, const BlockContext&)
{
    // SUSTAIN regelt Schwelle und Ratio gemeinsam, wie der eine Knopf an einem
    // Pedalkompressor.
    const float threshDb = mapTo (sustain, -6.0f, -34.0f);
    const float ratio    = mapTo (sustain,  1.6f,  8.0f);
    constexpr float kneeDb = 6.0f;

    // Ein Teil der Absenkung wird automatisch ausgeglichen, damit der
    // Kompressor beim Aufdrehen von SUSTAIN nicht leiser wird.
    const float autoMakeupDb = -threshDb * (1.0f - 1.0f / ratio) * 0.6f;

    outGain.setTarget (dbToGain (levelDb + autoMakeupDb));

    for (int i = 0; i < block.numFrames; ++i)
    {
        const float mag = std::max (std::abs (block.left[i]), std::abs (block.right[i]));

        detEnv += detCoeff * (mag - detEnv);
        detEnv = flushDenormal (detEnv);

        const float lvlDb = gainToDb (detEnv);

        // Zielabsenkung, weicher Übergang
        float wantGr;
        const float over = lvlDb - threshDb;
        if (over <= -kneeDb * 0.5f)
            wantGr = 0.0f;
        else if (over >= kneeDb * 0.5f)
            wantGr = over * (1.0f - 1.0f / ratio);
        else
        {
            const float t = over + kneeDb * 0.5f;
            wantGr = (1.0f - 1.0f / ratio) * t * t / (2.0f * kneeDb);
        }

        // Zwei Release-Zeiten, es gilt die mit weniger Absenkung.
        if (wantGr > grFast) grFast += atkCoeff     * (wantGr - grFast);
        else                 grFast += relFastCoeff * (wantGr - grFast);

        if (wantGr > grSlow) grSlow += atkCoeff     * (wantGr - grSlow);
        else                 grSlow += relSlowCoeff * (wantGr - grSlow);

        grFast = flushDenormal (grFast);
        grSlow = flushDenormal (grSlow);

        const float gr = std::min (grFast, grSlow);
        const float g  = dbToGain (-gr) * outGain.next();

        block.left[i]  *= g;
        block.right[i] *= g;

        if (i == block.numFrames - 1) lastReductionDb = gr;
    }
}

} // namespace nmp
