#pragma once
// GATE und COMP.

#include "Block.h"
#include "../dsp/Biquad.h"
#include "../util/Smoothed.h"

namespace nmp
{

/// Gate als Expander: unterhalb der Schwelle wird das Signal leiser statt
/// abgeschnitten, Ausklänge bleiben erhalten. Gegen Flattern: weicher Übergang
/// um die Schwelle, Haltezeit nach einem Ton und ein Hochpass im Sidechain
/// gegen Brummen.
class GateBlock : public Block
{
public:
    static constexpr float kOffThresholdDb = -90.0f;  ///< linker Anschlag = aus

    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    /// −90 … 0 dB. Am linken Anschlag ist der Block aus.
    void setThresholdDb (float db) { thresholdDb = clamp (db, -90.0f, 0.0f); }

    bool  isOff() const { return thresholdDb <= kOffThresholdDb; }
    float currentReductionDb() const { return lastReductionDb; }

private:
    double sampleRate = 48000.0;

    float thresholdDb = -70.0f;
    float lastReductionDb = 0.0f;

    Biquad detectHpL, detectHpR;
    float  env = 0.0f;
    float  gainDb = 0.0f;
    float  atkCoeff = 1.0f, relCoeff = 1.0f, envAtk = 1.0f, envRel = 1.0f;
    int    holdSamples = 0, holdCounter = 0;
};

/// Kompressor mit zwei Release-Zeiten (schnell und langsam, es gilt die mit
/// weniger Gain-Reduction), ähnlich dem Auto-Release in Pedalkompressoren.
/// Verhindert Pumpen bei Signalen um die Schwelle.
class CompBlock : public Block
{
public:
    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setSustain (float s)  { sustain = clamp (s, 0.0f, 1.0f); }
    void setLevelDb (float db) { levelDb = clamp (db, -12.0f, 12.0f); }

    float currentReductionDb() const { return lastReductionDb; }

private:
    double sampleRate = 48000.0;

    float sustain = 0.5f, levelDb = 0.0f;
    float lastReductionDb = 0.0f;

    float detEnv = 0.0f;
    float grFast = 0.0f, grSlow = 0.0f;   // Regelung in dB, positiv = Absenkung
    float atkCoeff = 1.0f, relFastCoeff = 1.0f, relSlowCoeff = 1.0f, detCoeff = 1.0f;

    LinearSmoothed outGain;
};

} // namespace nmp
