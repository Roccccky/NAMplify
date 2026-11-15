#pragma once
// CHORUS, FLANGER, PHASER, TREMOLO.

#include "Block.h"
#include "../dsp/DelayLine.h"
#include "../dsp/Lfo.h"
#include "../dsp/Biquad.h"
#include "../util/Smoothed.h"

namespace nmp
{

/// Chorus. Die Tiefe wird abhängig von der Rate begrenzt, damit die
/// Tonhöhenschwankung (2π · Rate · Tiefe) unter etwa 30 Cent bleibt. Sonst
/// leiert es bei hohen Raten.
class ChorusBlock : public Block
{
public:
    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setRate (float hz) { rateHz = clamp (hz, 0.1f, 5.0f); }
    void setDepth (float d) { depth  = clamp (d, 0.0f, 1.0f); }
    void setMix (float m)   { mix    = clamp (m, 0.0f, 1.0f); }

private:
    static constexpr int kVoices = 4;
    /// Höchste zugelassene Verstimmung als Verhältnis (≈ 31 Cent).
    static constexpr float kMaxDetune = 0.018f;

    double sampleRate = 48000.0;
    float  rateHz = 0.8f, depth = 0.5f, mix = 0.35f;

    DelayLine lineL, lineR;
    Lfo       lfo;
    Biquad    wetHpL, wetHpR;
    LinearSmoothed mixSm;
};

/// Flanger mit Through-Zero: der trockene Weg wird ebenfalls verzögert, der
/// nasse schwingt durch ihn hindurch. Die Kerben wandern so einmal je Halbwelle
/// aus dem Hörbereich und zurück.
class FlangerBlock : public Block
{
public:
    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setRate (float hz)     { rateHz = clamp (hz, 0.02f, 4.0f); }
    void setDepth (float d)     { depth  = clamp (d, 0.0f, 1.0f); }
    void setFeedback (float f)  { fdbk   = clamp (f, -0.9f, 0.9f); }
    void setManual (float m)    { manual = clamp (m, 0.0f, 1.0f); }
    void setMix (float m)       { mix    = clamp (m, 0.0f, 1.0f); }

private:
    double sampleRate = 48000.0;
    float  rateHz = 0.35f, depth = 0.70f, fdbk = 0.60f, manual = 0.30f, mix = 0.70f;

    DelayLine dryL, dryR, wetL, wetR;
    Lfo       lfo;
    Biquad    fbHpL, fbHpR, fbLpL, fbLpR;
    float     fbStateL = 0.0f, fbStateR = 0.0f;
    LinearSmoothed mixSm, fbSm;
};

/// Phaser, echt stereo: linker und rechter LFO laufen mit Phasenversatz.
class PhaserBlock : public Block
{
public:
    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setRate (float hz) { rateHz = clamp (hz, 0.05f, 2.0f); }
    void setDepth (float d) { depth  = clamp (d, 0.0f, 1.0f); }
    void setMix (float m)   { mix    = clamp (m, 0.0f, 1.0f); }

private:
    static constexpr int kStages = 6;

    struct Ap { float z = 0.0f; inline float process (float x, float a)
                { const float y = -a * x + z; z = flushDenormal (x + a * y); return y; } };

    double sampleRate = 48000.0;
    float  rateHz = 0.5f, depth = 0.7f, mix = 0.7f;

    Ap  apL[kStages], apR[kStages];
    float fbL = 0.0f, fbR = 0.0f;
    Lfo lfo;
    LinearSmoothed mixSm;
};

/// Tremolo. Bei kleiner Tiefe sinusförmig, bei großer zunehmend rechteckig.
class TremoloBlock : public Block
{
public:
    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setRate (float hz) { rateHz = clamp (hz, 1.0f, 12.0f); }
    void setDepth (float d) { depth  = clamp (d, 0.0f, 1.0f); }
    void setSync (bool on, NoteDiv d) { syncOn = on; div = d; }

private:
    double sampleRate = 48000.0;
    float  rateHz = 5.0f, depth = 0.6f;
    bool   syncOn = false;
    NoteDiv div = NoteDiv::quarter;

    Lfo lfo;
    LinearSmoothed depthSm;
};

} // namespace nmp
