#pragma once
// Rig-Presets. Ein Preset umfasst das ganze Board: Karten, Reihenfolge, alle
// Regler und die geladenen Dateien.
//
// Nicht im Preset (siehe Settings): Startverhalten, Einführung,
// Stimmgerät-Anzeige, globaler EQ, FULL/LITE, Referenzpegel, Zugangsdaten,
// MIDI-Zuordnungen.

#include <juce_core/juce_core.h>
#include "RigState.h"
#include <vector>

namespace nmpl
{

class Library;

struct PresetEntry
{
    juce::String name;
    juce::File   file;
};

class PresetManager
{
public:
    explicit PresetManager (Library&);

    void rescan();
    const std::vector<PresetEntry>& presets() const { return list; }

    juce::String currentName() const { return current; }
    void setCurrentName (const juce::String& n) { current = n; }

    /// −1, wenn nichts geladen ist.
    int currentIndex() const;

    bool save (const juce::String& name, const juce::var& params, const RigState&,
               juce::String& error);
    bool load (const juce::String& name, juce::var& params, RigState&, juce::String& error);
    bool remove (const juce::String& name, juce::String& error);

    /// Blättern, am Ende umlaufend.
    juce::String neighbour (int direction) const;
    /// Für Program Change über MIDI: Programm 1 lädt das erste Preset.
    juce::String byProgram (int program) const;

private:
    static juce::String sanitise (const juce::String&);

    Library& library;
    std::vector<PresetEntry> list;
    juce::String current;
};

} // namespace nmpl
