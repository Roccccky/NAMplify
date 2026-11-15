#pragma once
// DRIVE: vier Overdrive-Typen mit jeweils eigener Schaltung.

#include "Block.h"
#include "../dsp/Biquad.h"
#include "../dsp/Oversampler.h"
#include "../dsp/Waveshaper.h"
#include "../util/Smoothed.h"

namespace nmp
{

enum class DriveModel : int { scream = 0, warm, clean, crush, count };

const char* driveModelName (DriveModel m);

/// Werkseinstellungen eines Drive-Typs. Nur hier definiert, die UI bekommt sie
/// beim Verbinden vom Plugin.
struct DriveDefaults
{
    float gainDb;   // 0 … 36
    float levelDb;  // −12 … +12
    float tone;     // 0 … 1
    float body;     // 0 … 1
};

DriveDefaults driveDefaults (DriveModel m);

class DriveBlock : public Block
{
public:
    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;
    int  latencySamples() const override { return os.latencySamples(); }

    void setModel (DriveModel m);
    void setGainDb (float v)  { gainDb  = clamp (v, 0.0f, 36.0f);   dirty = true; }
    void setLevelDb (float v) { levelDb = clamp (v, -12.0f, 12.0f); dirty = true; }
    void setTone (float v)    { tone    = clamp (v, 0.0f, 1.0f);    dirty = true; }
    void setBody (float v)    { body    = clamp (v, 0.0f, 1.0f);    dirty = true; }

    DriveModel model() const { return currentModel; }

private:
    void updateCoefficients();

    struct Channel
    {
        Biquad preShelf, preHp, preLp, midPeak, toneLp, toneShelf;
        Biquad stage2Hp, stage3Hp, interLp1, interLp2;
        Biquad bodyPeak, sideHp, sideLp, tightHp;
        Biquad dcBlock;
        float  dcA = 0.0f, dcB = 0.0f;   // Gleichanteilsperre (ein Pol)

        void resetAll();
    };

    float processScream (Channel&, float x, float drive);
    float processWarm   (Channel&, float x, float drive);
    float processClean  (Channel&, float x, float drive);
    float processCrush  (Channel&, float x, float drive);

    double sampleRate = 48000.0, osRate = 192000.0;

    DriveModel currentModel = DriveModel::scream;
    float gainDb = 14.0f, levelDb = 4.0f, tone = 0.55f, body = 0.5f;
    bool  dirty = true;

    Oversampler os, osR;
    Channel chL, chR;

    LinearSmoothed driveGain, outLevel;
};

} // namespace nmp
