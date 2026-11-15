#pragma once
// Brücke zwischen UI und Plugin. Ein Aufruf mit Befehl und Antwort, dazu Events
// in die andere Richtung. Der Zustand liegt im Plugin, die Seite holt beim
// Sichtbarwerden ein vollständiges Abbild.

#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"
#include "InputTap.h"
#include <functional>
#include <atomic>
#include <memory>

namespace nmpl
{

class WebBridge : private juce::Timer
{
public:
    using Emit = std::function<void (const juce::Identifier&, const juce::var&)>;

    WebBridge (NamplifyProcessor&, Emit);
    ~WebBridge() override;

    using Completion = std::function<void (juce::var)>;

    /// Befehl aus der Seite, läuft im Message-Thread. Die Antwort kommt über
    /// done, bei Netzwerk-Befehlen erst später.
    void handle (const juce::var& request, Completion done);

    void setVisible (bool);

    /// Fenster auf eine der Stufen setzen. Das macht der Editor.
    void setSizeRequest (std::function<void (int)> fn) { sizeRequest = std::move (fn); }

private:
    void timerCallback() override;

    // Abbilder
    juce::var buildSync();
    juce::var buildParams() const;
    juce::var buildParamDefs() const;
    juce::var buildSlots() const;
    juce::var buildLanes() const;
    juce::var buildTypes() const;
    juce::var buildRoles() const;
    juce::var buildTools() const;
    juce::var buildPresets() const;
    juce::var buildSettings() const;
    juce::var buildEngineInfo() const;
    juce::var buildLibrary (nmp::SlotType, nmp::NamRole) const;
    juce::var buildAudioDevices() const;
    juce::var buildMeters();

    // Befehle
    juce::var cmdParamSet (const juce::var&);
    juce::var cmdSlot (const juce::var&, const juce::String& cmd);
    juce::var cmdLibrary (const juce::var&, const juce::String& cmd);
    juce::var cmdPreset (const juce::var&, const juce::String& cmd);
    void      cmdT3k (const juce::var&, const juce::String& cmd, Completion);
    juce::var cmdSettings (const juce::var&, const juce::String& cmd);
    juce::var cmdEq (const juce::var&, const juce::String& cmd);
    juce::var cmdLooper (const juce::var&, const juce::String& cmd);
    juce::var cmdRecorder (const juce::var&, const juce::String& cmd);
    juce::var cmdAudio (const juce::var&, const juce::String& cmd);

    /// Pegel der Eingangskanäle, nur solange die Kanalwahl offen ist.
    void setInputMetering (bool on);
    InputTap inputTap;
    bool     inputMetering = false;

    /// Slot-Nummer aus einer Anfrage, -1 wenn keine gültige enthalten ist.
    static int slotFrom (const juce::var& request);

    /// URL, unter der die Seite ein Bild aus der Bibliothek bekommt.
    juce::String libImageUrl (const juce::File& image) const;

    NamplifyProcessor& proc;
    Emit emit;
    std::function<void (int)> sizeRequest;

    bool visible = true;
    bool miniTuner = false;   ///< kleines Stimmgerät an (tuner.watch)
    int  lastStructure = -1;

    /// Es lädt immer nur eine Zeile gleichzeitig.
    std::atomic<bool> t3kBusy { false };
    int  busyToneId = 0, busyModelId = 0;

    juce::var pendingSearchToken;
    int searchGeneration = 0;

    std::unique_ptr<juce::FileChooser> chooser;

    /// Ein angespieltes Capture in die Bibliothek übernehmen, ohne es neu zu
    /// laden. Gibt { slot, tone, model } zurück, leer bei Fehler.
    juce::var keepPreview (int slot, juce::String& error);
    bool isPreview (int slot) const;
    void storeBankImage (const juce::File& target, int modelId, const juce::String& imageUrl);

    void t3kFinishDownload (int toneId, const juce::String& title, const juce::String& gear,
                            const juce::String& author, const juce::String& page,
                            const juce::String& imageUrl,
                            const T3kModel& model, const juce::MemoryBlock& data,
                            bool alsoLoad, bool storeInLibrary, int preferredSlot,
                            Completion done);
};

} // namespace nmpl
