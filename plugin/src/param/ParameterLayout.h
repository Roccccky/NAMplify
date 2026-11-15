#pragma once
// Host-Parameter, abgeleitet aus core/include/namplify/engine/Params.h (ID,
// Name, Bereich, Werkswert, Abbildung).

#include <juce_audio_processors/juce_audio_processors.h>
#include <namplify/engine/Slots.h>
#include <array>
#include <atomic>

namespace nmpl
{

/// Typ je Slot, vom Processor gepflegt, damit der Host Slot-Regler in der
/// Einheit ihres Typs anzeigt. Atomar, weil Hosts die Texte aus beliebigen
/// Threads abfragen.
using SlotTypeTable = std::array<std::atomic<int>, (size_t) nmp::kNumSlots>;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const SlotTypeTable&);

/// Parameter-Zeiger in der Reihenfolge von ParamId.
using ParamPointers = std::array<juce::RangedAudioParameter*, (size_t) nmp::kNumParams>;

ParamPointers collectParameters (juce::AudioProcessorValueTreeState&);

/// Wert eines Parameters in seiner Einheit (nicht 0…1).
inline float readParam (const ParamPointers& p, nmp::ParamId id)
{
    const auto& d = nmp::paramDesc (id);
    auto* param = p[(size_t) id];
    return param != nullptr ? d.fromUnit (param->getValue()) : d.def;
}

} // namespace nmpl
