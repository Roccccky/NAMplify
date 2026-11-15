#pragma once
// REVERB mit vier Typen.
//
// - MIX regelt nur das nasse Signal, das trockene bleibt auf 1.
// - TIME ist die RT60 in Sekunden. Feedback je Leitung: g = 10^(−3·d/(T·fs)).
// - Der nasse Anteil wird mit √(1−g²) gewichtet, damit kurze und lange Zeiten
// gleich präsent sind.
// - Die Dämpfung sitzt im Feedback, jede Runde verliert also mehr Höhen.

#include "Block.h"
#include "../dsp/DelayLine.h"
#include "../dsp/Biquad.h"
#include "../dsp/Lfo.h"
#include "../util/Smoothed.h"
#include "../util/Random.h"
#include <algorithm>
#include <vector>

namespace nmp
{

enum class ReverbType : int { room = 0, hall, plate, spring, count };

const char* reverbTypeName (ReverbType t);

/// Werkseinstellungen je Typ.
struct ReverbDefaults
{
    float timeSec;   // 0,2 … 12
    float size;      // 0 … 1   (bei SPRING ohne Bedeutung)
    float boing;     // 0 … 1   (nur bei SPRING)
    float tone;      // 0 … 1
    float mix;       // 0 … 1
};

ReverbDefaults reverbDefaults (ReverbType t);

class ReverbBlock : public Block
{
public:
    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setType (ReverbType t);

    // Nur bei echter Änderung neu berechnen. Die Parameter kommen in jedem
    // Puffer, und ein Neuaufbau setzt Speicher und Modulationsphasen zurück.
    void setTimeSec (float s) { s = clamp (s, 0.2f, 12.0f); if (s != timeSec) { timeSec = s; dirty = true; } }
    void setSize (float v)    { v = clamp (v, 0.0f, 1.0f);  if (v != size)    { size    = v; dirty = true; } }
    void setBoing (float v)   { v = clamp (v, 0.0f, 1.0f);  if (v != boing)   { boing   = v; dirty = true; } }
    void setTone (float v)    { v = clamp (v, 0.0f, 1.0f);  if (v != tone)    { tone    = v; dirty = true; } }
    void setMix (float v)     { mix     = clamp (v, 0.0f, 1.0f); }

    ReverbType type() const { return currentType; }

private:
    static constexpr int kLines    = 8;
    static constexpr int kDiffuse  = 4;
    static constexpr int kSpringAp = 112;

    void rebuild();
    /// Setzt die Startphasen der Modulation einmal beim Vorbereiten. In
    /// rebuild() würden sie bei jeder Reglerbewegung zurückspringen und
    /// knacken.
    void seedModulation();
    void processFdn (StereoBlock&);
    void processSpring (StereoBlock&);

    double sampleRate = 48000.0;

    ReverbType currentType = ReverbType::plate;
    float timeSec = 1.8f, size = 0.45f, boing = 0.62f, tone = 0.72f, mix = 0.18f;
    bool  dirty = true;

    // Netz
    DelayLine lines[kLines];
    float     lineDelay[kLines] = {};
    float     lineGain[kLines]  = {};
    float     dampState[kLines] = {};
    float     dampCoeff[kLines] = {};   ///< eigener Wert je Leitung, siehe rebuild()
    float     springDampCoeff = 0.5f;
    float     modDepth[kLines]  = {};
    double    modPhase[kLines]  = {};
    double    modInc[kLines]    = {};

    // Diffusion
    AllpassDelay diffL[kDiffuse], diffR[kDiffuse];

    // Erstreflexionen
    DelayLine erLine;
    static constexpr int kErTaps = 8;
    float erTapL[kErTaps] = {}, erGainL[kErTaps] = {};
    float erTapR[kErTaps] = {}, erGainR[kErTaps] = {};
    float erMix = 0.0f;
    Biquad erLp, erLpR;

    // Federhall
    struct StretchedAp
    {
        std::vector<float> buf;
        int pos = 0, len = 1;
        float a = 0.6f;

        /// Legt nur neu an, wenn sich die Länge ändert. Sonst würde der
        /// Speicher gelöscht.
        void prepare (int m)
        {
            m = std::max (1, m);
            if (m == len && buf.size() == (size_t) m) return;
            len = m; buf.assign ((size_t) len, 0.0f); pos = 0;
        }
        void reset()         { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }

        inline float process (float x)
        {
            const float d = buf[(size_t) pos];
            const float v = x - a * d;
            buf[(size_t) pos] = flushDenormal (v);
            if (++pos >= len) pos = 0;
            return a * v + d;
        }
    };

    StretchedAp springL[kSpringAp], springR[kSpringAp];
    DelayLine   springDelayL, springDelayR;
    float       springFb = 0.6f, springDelaySamples = 1000.0f;
    float       springDampL = 0.0f, springDampR = 0.0f;
    Biquad      springBpL, springBpR;

    // gemeinsam
    float wetTrim = 1.0f;
    Biquad wetHpL, wetHpR;
    LinearSmoothed mixSm;
};

} // namespace nmp
