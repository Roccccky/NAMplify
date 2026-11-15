#include "namplify/engine/Params.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>

namespace nmp
{

const char* const kNoteDivNames[9] = { "1/1", "1/2", "1/4.", "1/4", "1/4T",
                                       "1/8.", "1/8", "1/8T", "1/16." };

namespace
{
    using namespace desc;

    const char* const kDrumModes[]   = { "BEAT", "CLICK" };
    const char* const kPatterns6[]   = { "1", "2", "3", "4", "5", "6" };
    const char* const kPatterns4[]   = { "1", "2", "3", "4" };
    const char* const kMidiChannels[]= { "OMNI", "1", "2", "3", "4", "5", "6", "7", "8",
                                         "9", "10", "11", "12", "13", "14", "15", "16" };
    const char* const kLaneSources[] = { "PARALLEL", "SPLIT", "STEREO" };
    const char* const kSplitPoints[] = { "IN", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" };
    // Hinter der zwölften Position ist das Ende der Spur, dort gibt es keinen
    // Tail.
    const char* const kMergePoints[] = { "IN", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "OUT" };

    // Feste Parameter. Reihenfolge exakt wie in ParamId bis firstSlotParam.
    constexpr ParamDesc kFixed[] =
    {
        lin ("in_gain",   "Input",   -24.0f, 24.0f,  0.0f, "dB"),
        lin ("out_gain",  "Output",  -24.0f, 24.0f,  0.0f, "dB"),
        boolean ("mute",  "Mute",  false),
        boolean ("tuner", "Tuner", false),

        lin ("spread_ms", "Spread", 1.0f, 25.0f, 1.0f, "ms"),

        boolean ("drums_on", "Drums", false),
        choice ("drums_mode",    "Drums Mode", kDrumModes, 2, 1),
        choice ("drums_pattern", "Drums Pattern", kPatterns6, 6, 0),
        choice ("drums_click",   "Click Pattern", kPatterns4, 4, 0),
        lin ("drums_bpm",    "Drums Tempo", 60.0f, 200.0f, 90.0f, "BPM"),
        lin ("drums_volume", "Drums Volume", 0.0f, 1.0f, 0.7f),

        boolean ("looper_on",   "Looper",   false),
        boolean ("recorder_on", "Recorder", false),

        choice ("midi_channel", "MIDI Channel", kMidiChannels, 17, 0),
        boolean ("midi_program_change", "MIDI Program Change", false),
        boolean ("midi_thru", "MIDI Thru", false),

        boolean ("sync_drums",  "Sync Drums",  false),
        boolean ("sync_looper", "Sync Looper", false),

        boolean ("dual_on", "Dual", false),
        choice ("lane_b_source", "Lane B Source", kLaneSources, 3, 0),
        choice ("split_after", "Split After", kSplitPoints, 13, 0),
        choice ("merge_after", "Merge After", kMergePoints, 13, 12),
        lin ("lane_a_level", "Lane A Level", -24.0f, 12.0f, 0.0f, "dB"),
        lin ("lane_a_pan",   "Lane A Pan",   -1.0f, 1.0f, 0.0f),
        boolean ("lane_a_solo",   "Lane A Solo", false),
        boolean ("lane_a_invert", "Lane A Phase", false),
        lin ("lane_b_level", "Lane B Level", -24.0f, 12.0f, 0.0f, "dB"),
        lin ("lane_b_pan",   "Lane B Pan",   -1.0f, 1.0f, 0.0f),
        boolean ("lane_b_solo",   "Lane B Solo", false),
        boolean ("lane_b_invert", "Lane B Phase", false),
    };

    static_assert (sizeof (kFixed) / sizeof (kFixed[0]) == (size_t) ParamId::firstSlotParam,
                   "kFixed und ParamId stimmen nicht überein, die Reihenfolge muss identisch sein.");

    // Slot-Parameter: IDs s3_on, s3_k1 …, einmal beim Start erzeugt. Bereich
    // 0…1, die Bedeutung hängt vom Typ des Slots ab.
    struct Table
    {
        std::array<ParamDesc, kNumParams> all {};
        std::array<std::string, kNumParams> ids, names;

        Table()
        {
            for (int i = 0; i < (int) ParamId::firstSlotParam; ++i) all[(size_t) i] = kFixed[i];

            for (int s = 0; s < kNumSlots; ++s)
            {
                const auto on = (size_t) slotOnParam (s);
                ids[on]   = "s" + std::to_string (s + 1) + "_on";
                names[on] = "Slot " + std::to_string (s + 1);
                all[on]   = boolean (ids[on].c_str(), names[on].c_str(), false);

                for (int k = 0; k < kSlotControls; ++k)
                {
                    const auto c = (size_t) slotControlParam (s, k);
                    ids[c]   = "s" + std::to_string (s + 1) + "_k" + std::to_string (k + 1);
                    names[c] = "Slot " + std::to_string (s + 1) + " Knob " + std::to_string (k + 1);
                    all[c]   = lin (ids[c].c_str(), names[c].c_str(), 0.0f, 1.0f, 0.0f);
                }
            }
        }
    };

    const Table& table()
    {
        static const Table t;
        return t;
    }
}

const ParamDesc& paramDesc (ParamId id)
{
    const int i = clamp ((int) id, 0, kNumParams - 1);
    return table().all[(size_t) i];
}

int paramIndexFromId (const char* id)
{
    if (id == nullptr) return -1;
    for (int i = 0; i < kNumParams; ++i)
        if (std::strcmp (table().all[(size_t) i].id, id) == 0) return i;
    return -1;
}

float ParamDesc::fromUnit (float u) const
{
    u = clamp (u, 0.0f, 1.0f);
    switch (kind)
    {
        case ParamKind::boolean: return u >= 0.5f ? 1.0f : 0.0f;
        case ParamKind::choice:  return (float) clamp ((int) std::lround (u * (float) (numChoices - 1)),
                                                       0, numChoices - 1);
        case ParamKind::logarithmic: return mapToLog (u, min, max);
        case ParamKind::linear:
        default: return mapTo (u, min, max);
    }
}

float ParamDesc::toUnit (float v) const
{
    switch (kind)
    {
        case ParamKind::boolean: return v >= 0.5f ? 1.0f : 0.0f;
        case ParamKind::choice:
            return numChoices > 1 ? clamp (v / (float) (numChoices - 1), 0.0f, 1.0f) : 0.0f;
        case ParamKind::logarithmic: return mapFromLog (v, min, max);
        case ParamKind::linear:
        default: return mapFrom (clamp (v, min, max), min, max);
    }
}

std::string ParamDesc::toText (float v) const
{
    char buf[64];

    switch (kind)
    {
        case ParamKind::boolean:
            return v >= 0.5f ? "ON" : "OFF";

        case ParamKind::choice:
        {
            const int i = clamp ((int) std::lround (v), 0, numChoices - 1);
            return choices[i];
        }

        case ParamKind::logarithmic:
        case ParamKind::linear:
        default:
        {
            const float span = max - min;
            const char* fmt = span >= 100.0f ? "%.0f" : (span >= 5.0f ? "%.1f" : "%.2f");
            std::snprintf (buf, sizeof buf, fmt, v);
            std::string s = buf;
            if (unit != nullptr && unit[0] != '\0') { s += " "; s += unit; }
            return s;
        }
    }
}

ParamSet::ParamSet() { resetToDefaults(); }

void ParamSet::resetToDefaults()
{
    for (int i = 0; i < kNumParams; ++i)
        values[i].store (paramDesc (i).def, std::memory_order_relaxed);
}

} // namespace nmp
