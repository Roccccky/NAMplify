#pragma once
// EQ mit zwei Modi (3 Bänder, 7 Bänder). Beide Parametergruppen existieren
// immer, damit der Host eine feste Parameterliste hat. Hörbar ist jeweils eine
// Gruppe.

#include "Block.h"
#include "../dsp/Biquad.h"
#include "../util/Smoothed.h"
#include <vector>

namespace nmp
{

enum class EqMode : int { threeBand = 0, sevenBand, count };

/// Mittenfrequenzen des 7-Band-EQ (Oktavabstand).
inline constexpr float kEq7Freqs[7] = { 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f };

/// Bänder des 3-Band-EQ.
inline constexpr float kEq3BassHz   = 120.0f;
inline constexpr float kEq3MidHz    = 800.0f;
inline constexpr float kEq3TrebleHz = 3200.0f;

class EqBlock : public Block
{
public:
    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setMode (EqMode m);
    void setThreeBand (float bassDb, float midDb, float trebleDb);
    void setBand7 (int index, float db);

    EqMode mode() const { return currentMode; }

    /// Frequenzgang aus denselben Koeffizienten, mit denen gerechnet wird. Für
    /// Tests und die Anzeige.
    double magnitudeAt (double hz) const;

private:
    void updateThree();
    void updateSeven (int index);

    double sampleRate = 48000.0;

    EqMode currentMode = EqMode::threeBand;
    float  three[3] = { 0.0f, 0.0f, 0.0f };
    float  seven[7] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    StereoBiquad three3[3];
    StereoBiquad seven7[7];

    // Moduswechsel wird übergeblendet.
    LinearSmoothed modeFade;
    std::vector<float> scratchL, scratchR;
};

} // namespace nmp
