#pragma once
// MIDI.
//
// - CHANNEL gilt für Regler und Programmwechsel, Clock und Transport haben
// keinen Kanal.
// - THRU ist standardmäßig aus.
// - MIDI Learn passiert am Regler. Diese Klasse merkt sich, welcher Regler
// wartet, und meldet den empfangenen Befehl zurück.

#include <juce_audio_basics/juce_audio_basics.h>
#include <namplify/engine/Transport.h>
#include <namplify/util/SwitchPress.h>
#include <atomic>
#include <functional>
#include <vector>

namespace nmpl
{

class Settings;

class MidiHandler
{
public:
    explicit MidiHandler (Settings&);

    /// 0 = Omni, sonst 1 … 16.
    void setChannel (int c) { channel.store (juce::jlimit (0, 16, c)); }
    void setProgramChangeEnabled (bool v) { programChange.store (v); }
    void setThru (bool v) { thru.store (v); }

    /// Einen Regler auf den nächsten CC warten lassen.
    void armLearn (int paramIndex) { learnTarget.store (paramIndex); }
    void cancelLearn() { learnTarget.store (-1); }
    bool isLearning() const { return learnTarget.load() >= 0; }

    /// Audio-Thread.
    void processBlock (juce::MidiBuffer& incoming, juce::MidiBuffer& outgoing,
                       nmp::Transport&);

    /// Message-Thread. Holt ab, was der Audio-Thread empfangen hat.
    struct Event
    {
        /// toggle: Schalter wurde getreten, umschalten.
        enum class Type { parameter, toggle, programChange, learned } type = Type::parameter;
        int   paramIndex = -1;
        float unitValue = 0.0f;
        int   program = 0;
        int   cc = -1, ccChannel = 0;
    };

    std::vector<Event> takeEvents();

private:
    Settings& settings;

    std::atomic<int>  channel { 0 };
    std::atomic<bool> programChange { false };
    std::atomic<bool> thru { false };
    std::atomic<int>  learnTarget { -1 };

    juce::AbstractFifo fifo { 256 };
    Event events[256];
    void push (const Event&);

    /// Kopie der Zuordnungen für den Audio-Thread, wird beim Lernen und bei
    /// Änderungen in den Einstellungen erneuert. Bei An/Aus-Parametern zählt
    /// der Tritt, nicht der Wert (nmp::SwitchPress).
    struct Mapping { int cc, channel, param; bool isSwitch; nmp::SwitchPress press; };
    std::vector<Mapping> table;
    std::atomic<bool> tableDirty { true };

public:
    /// Nach jeder Änderung in den Einstellungen aufrufen.
    void refreshMappings() { tableDirty.store (true); }
    void rebuildIfNeeded();
};

} // namespace nmpl
