#pragma once
// AudioProcessor: Parameter, Zustand, Audio, MIDI und die Regeln beim Laden und
// beim Hinzufügen und Entfernen von Karten.

#include <juce_audio_processors/juce_audio_processors.h>
#include <namplify/engine/Engine.h>

#include "param/ParameterLayout.h"
#include "service/Library.h"
#include "service/Settings.h"
#include "service/PresetManager.h"
#include "service/RigState.h"
#include "service/Loader.h"
#include "service/Tone3000.h"
#include "audio/MidiHandler.h"

#include <deque>
#include <mutex>
#include <atomic>
#include <memory>

namespace nmpl
{

/// Kurze Einblendung ohne Knopf, die von selbst verschwindet. Eine neue ersetzt
/// die alte.
struct Notice
{
    juce::String text;
    juce::String kind { "info" };   ///< info · warn · error
};

class NamplifyProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    NamplifyProcessor();
    ~NamplifyProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NAMplify"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    // Programme (Program Change). VST3 kennt keinen MIDI-Programmwechsel, der
    // Host schickt ihn über einen Programm-Parameter. Den legt JUCE nur bei
    // mehr als einem Programm an, daher eine feste Zahl unabhängig von der
    // Presetliste.
    static constexpr int kNumPrograms = 128;
    int getNumPrograms() override { return kNumPrograms; }
    int getCurrentProgram() override { return juce::jmax (0, presetManager.currentIndex()); }
    /// Kann vom Audio-Thread kommen, geladen wird im Timer.
    void setCurrentProgram (int index) override { pendingProgram.store (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Zugriff für die UI
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    nmp::Engine&    engine()   { return audioEngine; }
    Library&        library()  { return lib; }
    Settings&       settings() { return prefs; }
    PresetManager&  presets()  { return presetManager; }
    Loader&         loader()   { return modelLoader; }
    Tone3000&       tone3000() { return t3k; }
    MidiHandler&    midi()     { return midiHandler; }
    RigState&       rig()      { return rigState; }

    // Slots. Alle Änderungen am Aufbau laufen über diese Funktionen. Sie prüfen
    // die Grenzen je Typ, setzen die Regler eines neuen Slots über den Host auf
    // Werkswerte (damit Automation und Undo stimmen) und geben den Aufbau an
    // die Engine.

    /// Legt einen Slot an. Gibt die Slot-Nummer zurück, -1 wenn die Grenze des
    /// Typs erreicht oder die Spur voll ist (mit Hinweis).
    int  addSlot (nmp::SlotType, nmp::NamRole, int lane, int pos, bool announce = true);
    void removeSlot (int slot);
    void moveSlot (int slot, int lane, int pos);
    void setSlotType (int slot, nmp::SlotType);
    void setSlotRole (int slot, nmp::NamRole);
    void setToolShown (nmp::Tool, bool shown, bool announce = true);
    void setZoom (int);

    // Laden
    /// Lädt eine Datei in einen Slot und setzt danach die zugehörigen Regeln
    /// um.
    void loadIntoSlot (int slot, const juce::File&, const SlotFile& info);
    void clearSlot (int slot);

    /// Slot für eine Datei dieses Typs und dieser Rolle: der gewünschte, wenn
    /// er passt, sonst der erste passende ohne Datei, dann der erste passende
    /// überhaupt, sonst ein neuer. -1, wenn die Grenze erreicht ist.
    int slotForFile (nmp::SlotType, nmp::NamRole, int preferred);

    // Presets
    void loadPreset (const juce::String& name);
    void savePreset (const juce::String& name);
    void deletePreset (const juce::String& name);
    void stepPreset (int direction);
    void resetRig();

    // Infos
    void notify (const juce::String& text, const juce::String& kind = "info");
    bool takeNotice (Notice&);

    /// Wird erhöht, wenn die UI alles neu lesen muss (Bibliothek, Presets,
    /// Board).
    int  structureVersion() const { return structureRevision.load(); }
    void bumpStructure() { ++structureRevision; }

    /// Aktuelle Bedeutung eines Parameters: bei festen der Parameter selbst,
    /// bei Slot-Reglern der Regler des Typs im Slot.
    const nmp::ParamDesc& effectiveDesc (int paramIndex) const;

    void applyParamsFromVar (const juce::var&);
    juce::var paramsToVar() const;

    /// Mute mit gespeichertem Zustand. Ist das kleine Stimmgerät sichtbar,
    /// läuft die Tonerkennung auch bei geschlossenem großen.
    void setTunerWatched (bool w) { tunerWatched.store (w); }
    void setMuted (bool);
    bool isMuted() const;

    /// Einführung beendet oder abgebrochen.
    void markFirstRunDone();

    /// Erster Start?
    bool isFirstRun() const { return firstRun; }
    void clearFirstRun() { firstRun = false; }

    bool isStandalone() const
    { return wrapperType == wrapperType_Standalone; }

    /// Standalone: ist nur ein Eingangskanal aktiv, bleibt der zweite Bus-Kanal
    /// leer. NAM-Slots bilden Mono aus ½·(L+R), die Gitarre käme 6 dB zu leise
    /// am Netz an. Deshalb wird der eine Kanal auf beide Seiten gelegt. Im
    /// Plugin entscheidet der Host.
    void setStandaloneMonoInput (bool mono) { monoInput.store (mono, std::memory_order_relaxed); }
    bool standaloneMonoInput() const { return monoInput.load (std::memory_order_relaxed); }

private:
    void timerCallback() override;
    void syncParamsToEngine();
    void applyRigToEngine();
    void handleMidiEvents();
    void enforceBoardConsistency();
    void afterLoad (const Loader::Result&);
    void resetParam (nmp::ParamId, float value);
    void setParamUnit (nmp::ParamId, float unit);

    /// Setzt Regler k von slot auf einen Wert in der Einheit seines Typs.
    void setSlotValue (int slot, int k, float value);
    void resetSlotControls (int slot);
    void commitLayout();
    /// Nächste IR hinter slot in derselben Spur, -1 wenn keine.
    int  irAfter (int slot) const;
    /// Ein Amp braucht eine Box. Fehlt sie, wird eine angelegt, ohne Datei mit
    /// der ersten IR aus der Bibliothek.
    void ensureIrAfter (int slot, bool announce);
    void requestSlotFile (int slot);

    /// Muss vor apvts stehen, die Parameterliste hält eine Referenz darauf.
    SlotTypeTable slotTypes {};
    juce::AudioProcessorValueTreeState apvts;
    ParamPointers params {};

    nmp::Engine   audioEngine;
    Library       lib;
    Settings      prefs;
    PresetManager presetManager;
    Loader        modelLoader;
    Tone3000      t3k;
    MidiHandler   midiHandler;

    RigState rigState;

    std::mutex noticeLock;
    std::deque<Notice> notices;
    std::atomic<int> structureRevision { 0 };
    std::atomic<bool> monoInput { false };
    std::atomic<int>  pendingProgram { -1 };

    /// Mute-Zustand vor dem Stimmgerät.
    bool  muteBeforeTuner = false;
    bool  tunerWasOpen = false;
    bool  firstRun = false;

    class TunerWorker;
    std::unique_ptr<TunerWorker> tunerWorker;
    std::atomic<bool> tunerWatched { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamplifyProcessor)
};

} // namespace nmpl
