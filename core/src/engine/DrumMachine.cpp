#include "namplify/engine/DrumMachine.h"
#include <cmath>

namespace nmp
{

namespace
{
    // Sechs Muster über 16 Sechzehntel. Bit 0 = Kick, 1 = Snare, 2 = HiHat, 3 =
    // offene HiHat.
    constexpr unsigned char K = 1, S = 2, H = 4, O = 8;

    const unsigned char kPatterns[DrumMachine::kBeatPatterns][DrumMachine::kSteps] =
    {
        // 1: gerader Achtel-Beat
        { K|H, 0, H, 0, S|H, 0, H, 0, K|H, 0, H, 0, S|H, 0, H, 0 },
        // 2: Achtel mit Auftakt
        { K|H, 0, H, 0, S|H, 0, H, K, K|H, 0, H, 0, S|H, 0, H|K, 0 },
        // 3: Sechzehntel-HiHat
        { K|H, H, H, H, S|H, H, H, H, K|H, H, H, H, S|H, H, H, H },
        // 4: halbes Tempo, offene HiHat auf der Vier
        { K|H, 0, 0, 0, S|H, 0, 0, 0, K|H, 0, 0, 0, S|O, 0, 0, 0 },
        // 5: Shuffle
        { K|H, 0, 0, H, S|H, 0, 0, H, K|H, 0, 0, H, S|H, 0, K, H },
        // 6: treibend, Kick auf jeder Viertel
        { K|H, 0, H, 0, K|S|H, 0, H, 0, K|H, 0, H, 0, K|S|H, 0, H, O },
    };

    /// Klick-Modus, vier Varianten:
    /// 0 Viertel, 1 Viertel mit betonter Eins, 2 Achtel, 3 Achtel mit betonter
    /// Eins.
    /// Rückgabe: 0 = nichts, 1 = Klick, 2 = betonter Klick.
    int clickAt (int variant, int step)
    {
        const bool onQuarter = (step % 4) == 0;
        const bool onEighth  = (step % 2) == 0;
        const bool onOne     = step == 0;

        switch (variant)
        {
            case 0: return onQuarter ? 1 : 0;
            case 1: return onOne ? 2 : (onQuarter ? 1 : 0);
            case 2: return onEighth ? 1 : 0;
            case 3: return onOne ? 2 : (onEighth ? 1 : 0);
            default: return 0;
        }
    }
}

void DrumMachine::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;
    volSm.reset (sampleRate, 0.02);
    volSm.setCurrentAndTarget (volume);
    // 5 ms Blende beim Schalten.
    enableFade.reset (sampleRate, 0.005);
    enableFade.setCurrentAndTarget (enabled ? 1.0f : 0.0f);
    reset();
}

void DrumMachine::reset()
{
    for (auto& v : voices) v.active = false;
    for (auto& f : flash) f = 0.0f;
    phase = 0.0;
    lastStep = -1;
    uiStep.store (-1);
}

float DrumMachine::stepFlash (int i) const
{
    return (i >= 0 && i < kSteps) ? flash[i] : 0.0f;
}

void DrumMachine::trigger (int step)
{
    auto claim = [&] () -> Voice*
    {
        for (auto& v : voices) if (! v.active) return &v;
        return &voices[0];   // die älteste Stimme wird ersetzt
    };

    auto startKick = [&] (float amp)
    {
        Voice* v = claim();
        *v = Voice{};
        v->active = true; v->kind = 0; v->amp = amp;
        v->phaseInc = 58.0 / sampleRate;
        v->env = 1.0f; v->envDecay = (float) std::exp (-1.0 / (sampleRate * 0.13));
        v->pitchEnv = 1.0f;
        v->pitchDecay = (float) std::exp (-1.0 / (sampleRate * 0.028));
        v->pitchAmount = 2.6f;    // von ~150 Hz herunter auf 58 Hz
    };

    auto startSnare = [&] (float amp)
    {
        Voice* v = claim();
        *v = Voice{};
        v->active = true; v->kind = 1; v->amp = amp;
        v->phaseInc = 186.0 / sampleRate;
        v->env = 1.0f; v->envDecay = (float) std::exp (-1.0 / (sampleRate * 0.085));
        v->filt.setCoeffs (designBandPassApprox (1800.0, 0.8, sampleRate));
    };

    auto startHat = [&] (float amp, bool open)
    {
        Voice* v = claim();
        *v = Voice{};
        v->active = true; v->kind = 2; v->amp = amp;
        v->env = 1.0f;
        v->envDecay = (float) std::exp (-1.0 / (sampleRate * (open ? 0.16 : 0.032)));
        v->filt.setCoeffs (designHighPass (7200.0, 0.707, sampleRate));
    };

    auto startClick = [&] (bool accent)
    {
        Voice* v = claim();
        *v = Voice{};
        v->active = true; v->kind = 3; v->amp = accent ? 1.0f : 0.62f;
        v->phaseInc = (accent ? 1600.0 : 1000.0) / sampleRate;
        v->env = 1.0f;
        v->envDecay = (float) std::exp (-1.0 / (sampleRate * 0.022));
    };

    if (mode == DrumMode::click)
    {
        const int c = clickAt (clickPattern, step);
        if (c > 0)
        {
            startClick (c == 2);
            flash[step] = c == 2 ? 1.0f : 0.7f;
        }
        else
        {
            flash[step] = 0.0f;
        }
        return;
    }

    const unsigned char bits = kPatterns[pattern][step];

    if (bits & K) startKick (1.0f);
    if (bits & S) startSnare (0.85f);
    if (bits & O) startHat (0.6f, true);
    else if (bits & H) startHat ((step % 4) == 0 ? 0.55f : 0.36f, false);

    // Anzeige: Eins am stärksten, Viertel kräftig, Sechzehntel dezent.
    flash[step] = step == 0 ? 1.0f : ((step % 4) == 0 ? 0.8f : (bits ? 0.42f : 0.0f));
}

void DrumMachine::renderVoices (StereoBlock& block, int from, int count)
{
    for (int i = from; i < from + count; ++i)
    {
        float mix = 0.0f;

        for (auto& v : voices)
        {
            if (! v.active) continue;

            float s = 0.0f;
            switch (v.kind)
            {
                case 0:   // Kick
                {
                    const double inc = v.phaseInc * (1.0 + v.pitchAmount * v.pitchEnv);
                    v.phase += inc;
                    if (v.phase >= 1.0) v.phase -= 1.0;
                    s = (float) std::sin (kTwoPi * v.phase);
                    // Etwas Sättigung für mehr Druck ohne mehr Pegel.
                    s = std::tanh (s * 1.6f) * 0.72f;
                    v.pitchEnv *= v.pitchDecay;
                    break;
                }

                case 1:   // Snare: Körper plus Teppich
                {
                    v.phase += v.phaseInc;
                    if (v.phase >= 1.0) v.phase -= 1.0;
                    const float body  = (float) std::sin (kTwoPi * v.phase) * 0.45f;
                    const float noise = v.filt.process (rng.nextBipolar()) * 0.85f;
                    s = body + noise;
                    break;
                }

                case 2:   // HiHat
                    s = v.filt.process (rng.nextBipolar()) * 0.8f;
                    break;

                case 3:   // Klick
                default:
                    v.phase += v.phaseInc;
                    if (v.phase >= 1.0) v.phase -= 1.0;
                    s = (float) std::sin (kTwoPi * v.phase);
                    break;
            }

            mix += s * v.env * v.amp;

            v.env *= v.envDecay;
            if (v.env < 1.0e-4f) v.active = false;
        }

        const float g = volSm.next() * enableFade.next() * 0.5f;
        block.left[i]  += mix * g;
        block.right[i] += mix * g;
    }
}

void DrumMachine::process (StereoBlock& block, const BlockContext& ctx)
{
    volSm.setTarget (volume);
    // Der Fußschalter blendet. Das Ziel wird hier gesetzt statt in setEnabled,
    // weil die Blende zum Audio-Thread gehört.
    enableFade.setTarget (enabled ? 1.0f : 0.0f);

    // Mit Sync gilt das externe Tempo, sonst der eigene Regler.
    lastBpm = (syncOn && ctx.clockRunning) ? ctx.bpm : (double) bpm;
    if (lastBpm < 20.0) lastBpm = 20.0;

    const double stepsPerSecond = lastBpm / 60.0 * 4.0;       // Sechzehntel
    const double phaseInc = stepsPerSecond / (double) kSteps / sampleRate;

    // Mit Sync folgt auch die Position im Takt dem Host.
    if (syncOn && ctx.clockRunning)
    {
        const double bars = ctx.ppqPosition / 4.0;
        phase = bars - std::floor (bars);
    }

    for (auto& f : flash) f *= 0.86f;

    int i = 0;
    while (i < block.numFrames)
    {
        const int step = clamp ((int) (phase * kSteps), 0, kSteps - 1);

        if (step != lastStep)
        {
            // Ausgeschaltet werden keine neuen Schläge ausgelöst, laufende
            // klingen aus.
            if (enabled) trigger (step);
            lastStep = step;
            uiStep.store (step, std::memory_order_relaxed);
        }

        // Bis zum nächsten Schritt am Stück rechnen.
        const double nextStepPhase = (double) (step + 1) / kSteps;
        const double togo = (nextStepPhase - phase) / phaseInc;
        const int count = clamp ((int) std::ceil (togo), 1, block.numFrames - i);

        renderVoices (block, i, count);

        phase += phaseInc * count;
        if (phase >= 1.0) { phase -= std::floor (phase); lastStep = -1; }

        i += count;
    }
}

} // namespace nmp
