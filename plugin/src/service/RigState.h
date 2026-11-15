#pragma once
// Zustand des Boards, der ins Preset gehört, aber kein Host-Parameter ist:
// welcher Slot welchen Typ hat, wo er steht und welche Datei darauf liegt. Die
// Regler stehen in der Parameterliste.

#include <juce_core/juce_core.h>
#include <namplify/engine/Engine.h>
#include <array>

namespace nmpl
{

struct SlotFile
{
    /// Pfad relativ zur Bibliothek, damit ein verschobener Dokumente-Ordner
    /// nichts kaputt macht.
    juce::String relativePath;
    juce::String bankTitle;      ///< obere Zeile im Display
    juce::String variantName;    ///< untere Zeile im Display
    juce::String gear;           ///< amp · amp-cab · cab · pedal · outboard
    juce::String imageFile;      ///< Dateiname des Bildes, leer wenn keins

    bool isEmpty() const { return relativePath.isEmpty(); }
    void clear() { *this = SlotFile{}; }
};

/// Rolle, die eine Datei nahelegt (gear aus TONE3000 oder der Bibliothek).
nmp::NamRole roleForGear (const juce::String& gear);

/// Nimmt ein Slot dieses Typs Dateien? NAM-Slots nehmen Captures, OUTBOARD auch
/// IRs, IR-Slots nur IRs.
bool slotTakesFiles (nmp::SlotType);

struct RigState
{
    nmp::Layout layout = nmp::defaultLayout();
    std::array<SlotFile, (size_t) nmp::kNumSlots> files;
    std::array<bool, (size_t) nmp::kNumTools> tools {};
    int   zoom = 2;                      ///< 0 = 65 %, 1 = 80 %, 2 = 100 %
    std::array<float, 4> looperVolumes { 0.8f, 0.8f, 0.8f, 0.8f };

    juce::var toVar() const;

    /// Liest Format 2 und Format 1 (feste Karten). Bei Format 1 werden auch die
    /// Regler übersetzt: params kommt im alten Format herein und geht im neuen
    /// heraus.
    static RigState fromVar (const juce::var& rig, juce::var& params);
};

} // namespace nmpl
