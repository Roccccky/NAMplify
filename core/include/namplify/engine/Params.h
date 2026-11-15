#pragma once
// Host-Parameter.
//
// Die Reihenfolge nicht ändern: Presets und Automation hängen an den Nummern.
// Neue Parameter nur hinten anfügen, hinter den Slots.
//
// Version 2 hat die Liste einmal neu aufgebaut (freie Slots statt fester
// Karten). Alte Presets finden ihre Werte über die Regler-IDs wieder (Slots.h,
// slotControl), alte Automation nicht.
//
// ParamContractTest vergleicht die Liste mit einer festen Kopie.

#include "../util/Types.h"
#include <atomic>
#include <cmath>
#include <string>

namespace nmp
{

/// Slots insgesamt, über beide Spuren. Jeder hat eine feste Nummer für den Host
/// (Slot 1 … Slot 16), siehe Slots.h.
inline constexpr int kNumSlots = 16;

/// Regler pro Slot, die der Host sieht. So viele, wie der umfangreichste Typ
/// hat (EQ: Modus, 3 Bänder, 7 Bänder).
inline constexpr int kSlotControls = 11;

/// Parameter pro Slot: An/Aus und die Regler.
inline constexpr int kParamsPerSlot = 1 + kSlotControls;

enum class ParamId : int
{
    // Pegel und Mute
    inGain = 0,
    outGain,
    mute,
    tuner,

    // SPREAD (nur mit einer Spur)
    spreadMs,

    // DRUMS
    drumsOn, drumsMode, drumsPattern, drumsClickPattern, drumsBpm, drumsVolume,

    // LOOPER / RECORDER
    looperOn,
    recorderOn,

    // MIDI
    midiChannel, midiProgramChange, midiThru,

    // Tempo-Sync
    syncDrums,
    syncLooper,

    // zwei Spuren (DUAL)
    dualOn,
    laneBSource,     ///< PARALLEL · SPLIT · STEREO
    splitAfter,      ///< SPLIT: nach welcher Stelle von A abgezweigt wird (0 = ganz vorn)
    mergeAfter,      ///< SPLIT: nach welcher Stelle von A die Spuren wieder zusammenkommen, danach folgt der Tail
    laneALevel, laneAPan, laneASolo, laneAInvert,
    laneBLevel, laneBPan, laneBSolo, laneBInvert,

    // Slots: je Slot An/Aus und kSlotControls Regler
    firstSlotParam,

    count = firstSlotParam + kNumSlots * kParamsPerSlot
};

inline constexpr int kNumParams = (int) ParamId::count;

/// Signalquelle für Spur B.
enum class LaneSource : int
{
    input = 0,   ///< parallel: derselbe Eingang wie A, Zusammenführung am Ende
    split,       ///< nach einer Stelle von A abgezweigt und später wieder zusammengeführt
    stereo,      ///< A bekommt links, B bekommt rechts
    count
};

inline constexpr ParamId slotOnParam (int slot)
{
    return (ParamId) ((int) ParamId::firstSlotParam + slot * kParamsPerSlot);
}

inline constexpr ParamId slotControlParam (int slot, int k)
{
    return (ParamId) ((int) ParamId::firstSlotParam + slot * kParamsPerSlot + 1 + k);
}

/// Gehört der Parameter zu einem Slot? control ist −1 für An/Aus.
inline bool slotParamInfo (ParamId id, int& slot, int& control)
{
    const int i = (int) id - (int) ParamId::firstSlotParam;
    if (i < 0 || (int) id >= kNumParams) return false;
    slot    = i / kParamsPerSlot;
    control = i % kParamsPerSlot - 1;
    return true;
}

enum class ParamKind { linear, logarithmic, boolean, choice };

struct ParamDesc
{
    const char*  id;          ///< feste ID, wird so gespeichert
    const char*  name;        ///< Name im Host
    ParamKind    kind;
    float        min, max, def;
    const char*  unit;
    const char* const* choices;
    int          numChoices;

    /// 0…1 → Wert
    float fromUnit (float u) const;
    /// Wert → 0…1
    float toUnit (float v) const;
    /// Anzeigetext
    std::string  toText (float v) const;
};

namespace desc
{
    constexpr ParamDesc lin (const char* id, const char* name, float lo, float hi, float def,
                             const char* unit = "")
    { return { id, name, ParamKind::linear, lo, hi, def, unit, nullptr, 0 }; }

    constexpr ParamDesc log (const char* id, const char* name, float lo, float hi, float def,
                             const char* unit = "")
    { return { id, name, ParamKind::logarithmic, lo, hi, def, unit, nullptr, 0 }; }

    constexpr ParamDesc boolean (const char* id, const char* name, bool def)
    { return { id, name, ParamKind::boolean, 0.0f, 1.0f, def ? 1.0f : 0.0f, "", nullptr, 0 }; }

    constexpr ParamDesc choice (const char* id, const char* name,
                                const char* const* c, int n, int def)
    { return { id, name, ParamKind::choice, 0.0f, (float) (n - 1), (float) def, "", c, n }; }
}

/// Notenwerte für tempo-synchrone Parameter.
extern const char* const kNoteDivNames[9];

const ParamDesc& paramDesc (ParamId id);
inline const ParamDesc& paramDesc (int i) { return paramDesc ((ParamId) i); }

/// Parameterindex zu einer ID, −1 wenn unbekannt.
int paramIndexFromId (const char* id);

/// Parameterwerte für den Audio-Thread. Der Host schreibt, der Audio-Thread
/// liest, daher atomar.
///
/// Slot-Regler stehen hier als 0…1. Die Bedeutung hängt vom Typ des Slots ab
/// und wird erst in Engine umgerechnet.
class ParamSet
{
public:
    ParamSet();

    void set (ParamId p, float v) { values[(int) p].store (v, std::memory_order_relaxed); }
    float get (ParamId p) const   { return values[(int) p].load (std::memory_order_relaxed); }

    bool  getBool (ParamId p) const { return get (p) >= 0.5f; }
    int   getInt (ParamId p) const  { return (int) std::lround (get (p)); }

    void resetToDefaults();

private:
    std::atomic<float> values[kNumParams];
};

} // namespace nmp
