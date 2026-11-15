#pragma once
// Beat Machine zum Mitspielen. Kurze synthetische Klänge, die den Takt halten.
//
// Dazu ein Klick-Modus als Metronom. Ein Schalter wechselt zwischen Beat und
// Klick, im Klick-Modus gilt ein eigener Muster-Regler mit vier Stellungen.

#include "../blocks/Block.h"
#include "../util/Random.h"
#include "../dsp/Biquad.h"
#include "../util/Smoothed.h"
#include <atomic>

namespace nmp
{

enum class DrumMode : int { beat = 0, click, count };

class DrumMachine : public Block
{
public:
    static constexpr int kSteps = 16;         ///< 16 Sechzehntel = ein 4/4-Takt
    static constexpr int kBeatPatterns  = 6;
    static constexpr int kClickPatterns = 4;

    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setMode (DrumMode m)      { mode = m; }
    void setPattern (int p)        { pattern = clamp (p, 0, kBeatPatterns - 1); }
    void setClickPattern (int p)   { clickPattern = clamp (p, 0, kClickPatterns - 1); }
    void setBpm (float b)          { bpm = clamp (b, 60.0f, 200.0f); }
    void setVolume (float v)       { volume = clamp (v, 0.0f, 1.0f); }
    void setSync (bool on)         { syncOn = on; }

    /// Fußschalter der Karte. Der Block blendet beim Ausschalten aus, statt
    /// einfach übersprungen zu werden, sonst würde ein laufender Schlag
    /// abgeschnitten.
    void setEnabled (bool on) { enabled = on; }

    /// Solange etwas ausklingt, muss process() weiter aufgerufen werden.
    bool tailActive() const { return enableFade.getCurrent() > 0.0f || enableFade.isSmoothing(); }

    /// Für die 16 Schrittanzeigen: step ist der aktuelle Schritt, accent seine
    /// Stärke.
    int   currentStep() const  { return uiStep.load (std::memory_order_relaxed); }
    float stepFlash (int i) const;

    /// Aktuell gültiges Tempo, auch bei externer Clock.
    double effectiveBpm() const { return lastBpm; }
    /// Taktlänge in Samples, der Looper richtet sich danach.
    double barSamples() const { return lastBpm > 0.0 ? sampleRate * 4.0 * 60.0 / lastBpm : 0.0; }
    /// Position im Takt, 0 … 1.
    double barPhase() const { return phase; }

private:
    void trigger (int step);
    void renderVoices (StereoBlock&, int from, int count);

    struct Voice
    {
        bool  active = false;
        float amp = 0.0f;
        double phase = 0.0, phaseInc = 0.0;
        float env = 0.0f, envDecay = 0.0f;
        float pitchEnv = 0.0f, pitchDecay = 0.0f, pitchAmount = 0.0f;
        int   kind = 0;      // 0 Kick, 1 Snare, 2 HiHat, 3 Klick
        Biquad filt;
        float  noiseState = 0.0f;
    };

    static constexpr int kMaxVoices = 8;
    Voice voices[kMaxVoices];
    Xorshift rng { 0xD00Du };

    double sampleRate = 48000.0;
    DrumMode mode = DrumMode::click;   // Werkseinstellung
    int   pattern = 0, clickPattern = 0;
    float bpm = 90.0f, volume = 0.7f;
    bool  syncOn = false;

    double phase = 0.0;         // 0 … 1 über einen Takt
    int    lastStep = -1;
    double lastBpm = 90.0;

    LinearSmoothed volSm;
    /// Blende des Fußschalters, unabhängig von der Lautstärke.
    LinearSmoothed enableFade;
    bool enabled = false;

    std::atomic<int> uiStep { -1 };
    float flash[kSteps] = {};
};

} // namespace nmp
