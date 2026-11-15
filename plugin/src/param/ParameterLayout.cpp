#include "ParameterLayout.h"
#include <cmath>
#include <memory>

namespace nmpl
{

namespace
{
    juce::NormalisableRange<float> rangeFor (const nmp::ParamDesc& d)
    {
        if (d.kind != nmp::ParamKind::logarithmic)
            return juce::NormalisableRange<float> (d.min, d.max);

        // Dieselbe Abbildung wie im Kern, damit Host und Plugin denselben Wert
        // zeigen.
        return juce::NormalisableRange<float> (
            d.min, d.max,
            [] (float lo, float hi, float u) { return nmp::mapToLog (u, lo, hi); },
            [] (float lo, float hi, float v) { return nmp::mapFromLog (v, lo, hi); },
            [] (float lo, float hi, float v) { return nmp::clamp (v, lo, hi); });
    }
}

namespace
{
    /// Ein Slot-Regler heißt im Host "Slot 3 Knob 2" und läuft von 0 bis 1. Der
    /// Anzeigetext hängt vom Typ ab (z. B. "420 ms" bei Delay, "6.0 dB" bei
    /// Drive). Namen laden Hosts nicht zuverlässig nach, Texte fragen sie bei
    /// jedem Zeichnen neu ab.
    std::string textFor (nmp::ParamId id, float v, const SlotTypeTable& slotTypes)
    {
        const int i = (int) id;
        const int first = (int) nmp::ParamId::firstSlotParam;
        if (i < first) return nmp::paramDesc (id).toText (v);

        const int slot = (i - first) / nmp::kParamsPerSlot;
        const int k    = (i - first) % nmp::kParamsPerSlot - 1;   // 0 = An/Aus
        const auto type = (nmp::SlotType) slotTypes[(size_t) slot].load (std::memory_order_relaxed);

        if (k < 0) return nmp::paramDesc (id).toText (v);
        if (k >= nmp::slotControlCount (type)) return "-";

        const auto& d = nmp::slotControl (type, k);
        return d.toText (d.fromUnit (v));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const SlotTypeTable& slotTypes)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < nmp::kNumParams; ++i)
    {
        const auto& d = nmp::paramDesc (i);
        const juce::ParameterID pid { juce::String (d.id), 1 };
        const juce::String name { juce::CharPointer_UTF8 (d.name) };

        switch (d.kind)
        {
            case nmp::ParamKind::boolean:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, name, d.def >= 0.5f));
                break;

            case nmp::ParamKind::choice:
            {
                juce::StringArray choices;
                for (int c = 0; c < d.numChoices; ++c)
                    choices.add (juce::String (juce::CharPointer_UTF8 (d.choices[c])));
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    pid, name, choices, (int) std::lround (d.def)));
                break;
            }

            case nmp::ParamKind::linear:
            case nmp::ParamKind::logarithmic:
            default:
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, name, rangeFor (d), d.def,
                    juce::AudioParameterFloatAttributes()
                        .withLabel (juce::String (d.unit))
                        .withStringFromValueFunction (
                            [id = (nmp::ParamId) i, &slotTypes] (float v, int)
                            { return juce::String (juce::CharPointer_UTF8 (
                                  textFor (id, v, slotTypes).c_str())); })));
                break;
        }
    }

    return layout;
}

ParamPointers collectParameters (juce::AudioProcessorValueTreeState& state)
{
    ParamPointers p {};
    for (int i = 0; i < nmp::kNumParams; ++i)
        p[(size_t) i] = state.getParameter (nmp::paramDesc (i).id);
    return p;
}

} // namespace nmpl
