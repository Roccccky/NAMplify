#pragma once
// Tempo und Takt.
//
// Im Host kommt das Tempo vom Transport des Hosts (Hosts senden Plugins keine
// MIDI-Clock). Im Standalone ist die MIDI-Clock die einzige Quelle.
//
// Die MIDI-Clock zittert. Das Tempo wird deshalb geglättet, sonst würde z. B.
// ein tempo-synchrones Delay hörbar schwanken.

#include "../util/Types.h"
#include "../blocks/Block.h"
#include <atomic>

namespace nmp
{

class Transport
{
public:
    enum class Source { none = 0, host, midiClock };

    void prepare (double sampleRate);
    void reset();

    /// Infos vom Host, bei jedem Block.
    void setHostInfo (bool playing, double bpm, double ppqPosition);
    void clearHostInfo();

    /// Ein Impuls der MIDI-Clock, 24 pro Viertel.
    void midiClockPulse();
    void midiStart();
    void midiStop();
    void midiContinue();

    /// Nach jedem Block weiterzählen.
    void advance (int numFrames);

    /// Zustand für die Blöcke. fallbackBpm gilt ohne externes Tempo, dann
    /// nehmen die Effekte ihren eigenen Regler.
    BlockContext context (double fallbackBpm, int numFrames) const;

    Source source() const { return currentSource; }
    bool   isRunning() const { return running; }
    double bpm() const { return smoothedBpm; }

private:
    void pushInterval (double seconds);

    double sampleRate = 48000.0;
    double samplesSinceClock = 0.0;
    double totalSamples = 0.0;

    Source currentSource = Source::none;
    bool   running = false;

    double smoothedBpm = 120.0;
    double hostPpq = 0.0;
    double clockPpq = 0.0;

    static constexpr int kIntervalHistory = 24;   // eine Viertelnote
    double intervals[kIntervalHistory] = {};
    int    intervalCount = 0, intervalWrite = 0;
    int    pulseCount = 0;
    double lastPulseSamples = -1.0;
};

} // namespace nmp
