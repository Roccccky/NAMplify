#include <algorithm>
#include "namplify/engine/Slots.h"
#include <cstring>

namespace nmp
{

namespace
{
    using namespace desc;

    const char* const kDriveModels[] = { "SCREAM", "WARM", "CLEAN", "CRUSH" };
    const char* const kNamOutputModes[] = { "RAW", "CALIBRATED", "NORMALIZED" };
    const char* const kEqModes[]     = { "3 BAND", "7 BAND" };
    const char* const kDelayTypes[]  = { "TAPE", "ANALOG", "DIGITAL", "PING" };
    const char* const kReverbTypes[] = { "ROOM", "HALL", "PLATE", "SPRING" };

    // Regler je Typ. Die Reihenfolge entspricht den Aufzählungen in ctl und den
    // Knobs im Host: nur hinten anfügen. Die IDs stammen aus Version 1, wo es
    // den Regler schon gab.

    // GATE: am linken Anschlag ist der Block aus.
    constexpr ParamDesc kGate[] = {
        lin ("gate_threshold", "Threshold", -90.0f, 0.0f, -70.0f, "dB"),
    };

    constexpr ParamDesc kComp[] = {
        lin ("comp_sustain", "Sustain", 0.0f, 1.0f, 0.5f),
        lin ("comp_level",   "Level", -12.0f, 12.0f, 0.0f, "dB"),
    };

    constexpr ParamDesc kDrive[] = {
        choice ("drive_model", "Model", kDriveModels, 4, 0),
        lin ("drive_gain",  "Gain",  0.0f, 36.0f, 14.0f, "dB"),
        lin ("drive_level", "Level", -12.0f, 12.0f, 4.0f, "dB"),
        lin ("drive_tone",  "Tone",  0.0f, 1.0f, 0.55f),
        lin ("drive_body",  "Body",  0.0f, 1.0f, 0.50f),
    };

    // NAM: ein Satz für alle Rollen. AMP und FULL RIG zeigen die Klangregelung,
    // PEDAL und OUTBOARD den MIX.
    constexpr ParamDesc kNam[] = {
        lin ("nam_gain",  "Gain",  -24.0f, 24.0f, 0.0f, "dB"),
        lin ("nam_level", "Level", -12.0f, 12.0f, 0.0f, "dB"),
        lin ("nam_mix",   "Mix",   0.0f, 1.0f, 1.0f),
        lin ("amp_bass",   "Bass",   -12.0f, 12.0f, 0.0f, "dB"),
        lin ("amp_mid",    "Mid",    -12.0f, 12.0f, 0.0f, "dB"),
        lin ("amp_treble", "Treble", -12.0f, 12.0f, 0.0f, "dB"),
        // RAW, CALIBRATED, NORMALIZED wie im NAM-Plugin. Als Auswahl mit drei
        // Stellen bleiben alte Presets gültig: an (1,0) ist NORMALIZED, aus
        // (0,0) ist RAW.
        choice ("amp_norm", "Output", kNamOutputModes, 3, 2),
    };

    constexpr ParamDesc kEq[] = {
        choice ("eq_mode", "Mode", kEqModes, 2, 0),
        lin ("eq3_bass",   "Bass",   -12.0f, 12.0f, 0.0f, "dB"),
        lin ("eq3_mid",    "Mid",    -12.0f, 12.0f, 0.0f, "dB"),
        lin ("eq3_treble", "Treble", -12.0f, 12.0f, 0.0f, "dB"),
        lin ("eq7_100",  "100",  -12.0f, 12.0f, 0.0f, "dB"),
        lin ("eq7_200",  "200",  -12.0f, 12.0f, 0.0f, "dB"),
        lin ("eq7_400",  "400",  -12.0f, 12.0f, 0.0f, "dB"),
        lin ("eq7_800",  "800",  -12.0f, 12.0f, 0.0f, "dB"),
        lin ("eq7_1600", "1.6K", -12.0f, 12.0f, 0.0f, "dB"),
        lin ("eq7_3200", "3.2K", -12.0f, 12.0f, 0.0f, "dB"),
        lin ("eq7_6400", "6.4K", -12.0f, 12.0f, 0.0f, "dB"),
    };

    constexpr ParamDesc kIr[] = {
        lin ("cab_output", "Output", -12.0f, 12.0f, 0.0f, "dB"),
    };

    constexpr ParamDesc kChorus[] = {
        log ("chorus_rate",  "Rate", 0.1f, 5.0f, 0.8f, "Hz"),
        lin ("chorus_depth", "Depth", 0.0f, 1.0f, 0.5f),
        lin ("chorus_mix",   "Mix",   0.0f, 1.0f, 0.35f),
    };

    constexpr ParamDesc kFlanger[] = {
        log ("flanger_rate",     "Rate", 0.02f, 4.0f, 0.35f, "Hz"),
        lin ("flanger_depth",    "Depth", 0.0f, 1.0f, 0.70f),
        lin ("flanger_feedback", "Feedback", -0.9f, 0.9f, 0.60f),
        lin ("flanger_manual",   "Manual", 0.0f, 1.0f, 0.30f),
        lin ("flanger_mix",      "Mix", 0.0f, 1.0f, 0.70f),
    };

    constexpr ParamDesc kPhaser[] = {
        log ("phaser_rate",  "Rate", 0.05f, 2.0f, 0.5f, "Hz"),
        lin ("phaser_depth", "Depth", 0.0f, 1.0f, 0.7f),
        lin ("phaser_mix",   "Mix",   0.0f, 1.0f, 0.7f),
    };

    constexpr ParamDesc kTremolo[] = {
        lin ("tremolo_rate",  "Rate", 1.0f, 12.0f, 5.0f, "Hz"),
        lin ("tremolo_depth", "Depth", 0.0f, 1.0f, 0.6f),
        boolean ("sync_tremolo", "Sync", false),
        choice ("sync_tremolo_div", "Division", kNoteDivNames, 9, (int) NoteDiv::eighth),
    };

    constexpr ParamDesc kDelay[] = {
        choice ("delay_type", "Type", kDelayTypes, 4, 0),
        log ("delay_time",     "Time", 20.0f, 2000.0f, 375.0f, "ms"),
        lin ("delay_feedback", "Feedback", 0.0f, 0.95f, 0.3f),
        lin ("delay_mix",      "Mix", 0.0f, 1.0f, 0.25f),
        boolean ("sync_delay", "Sync", false),
        choice ("sync_delay_div", "Division", kNoteDivNames, 9, (int) NoteDiv::quarter),
    };

    constexpr ParamDesc kReverb[] = {
        choice ("reverb_type", "Type", kReverbTypes, 4, 2),   // PLATE
        log ("reverb_time",  "Time", 0.2f, 12.0f, 1.8f, "s"),
        lin ("reverb_size",  "Size",  0.0f, 1.0f, 0.45f),
        lin ("reverb_boing", "Boing", 0.0f, 1.0f, 0.62f),
        lin ("reverb_tone",  "Tone",  0.0f, 1.0f, 0.72f),
        lin ("reverb_mix",   "Mix",   0.0f, 1.0f, 0.18f),
    };

    template <size_t N>
    constexpr int countOf (const ParamDesc (&)[N]) { return (int) N; }

    struct TypeInfo
    {
        const char* key;
        const char* name;
        const ParamDesc* controls;
        int numControls;
        int limit;
    };

    const TypeInfo kTypes[] =
    {
        { "EMPTY",   "EMPTY",   nullptr,  0,                   kNumSlots },
        { "GATE",    "GATE",    kGate,    countOf (kGate),     2 },
        { "COMP",    "COMP",    kComp,    countOf (kComp),     2 },
        { "DRIVE",   "DRIVE",   kDrive,   countOf (kDrive),    2 },
        { "NAM",     "NAM",     kNam,     countOf (kNam),      4 },
        { "EQ",      "EQ",      kEq,      countOf (kEq),       2 },
        { "IR",      "IR",      kIr,      countOf (kIr),       4 },
        { "CHORUS",  "CHORUS",  kChorus,  countOf (kChorus),   2 },
        { "FLANGER", "FLANGER", kFlanger, countOf (kFlanger),  2 },
        { "PHASER",  "PHASER",  kPhaser,  countOf (kPhaser),   2 },
        { "TREMOLO", "TREMOLO", kTremolo, countOf (kTremolo),  2 },
        { "DELAY",   "DELAY",   kDelay,   countOf (kDelay),    2 },
        { "REVERB",  "REVERB",  kReverb,  countOf (kReverb),   2 },
    };

    static_assert (sizeof (kTypes) / sizeof (kTypes[0]) == (size_t) kNumSlotTypes,
                   "Jeder Typ braucht seinen Eintrag.");
    static_assert (countOf (kEq) <= kSlotControls,
                   "Der Typ mit den meisten Reglern bestimmt, wie viele Regler der Host je Slot sieht.");

    const TypeInfo& info (SlotType t)
    {
        const int i = (int) t;
        return kTypes[i >= 0 && i < kNumSlotTypes ? i : 0];
    }

    const char* const kRoleKeys[]  = { "amp", "pedal", "outboard", "fullrig", "space" };
    const char* const kRoleNames[] = { "AMP", "PEDAL", "OUTBOARD", "AMP+CAB", "SPACE" };

    const ParamDesc kNothing = lin ("", "", 0.0f, 1.0f, 0.0f);
}

int slotTypeLimit (SlotType t)    { return info (t).limit; }
int slotControlCount (SlotType t) { return info (t).numControls; }

const ParamDesc& slotControl (SlotType t, int k)
{
    const auto& i = info (t);
    return k >= 0 && k < i.numControls ? i.controls[k] : kNothing;
}

const char* slotTypeKey (SlotType t)  { return info (t).key; }
const char* slotTypeName (SlotType t) { return info (t).name; }

SlotType slotTypeFromKey (const char* key)
{
    if (key != nullptr)
        for (int i = 0; i < kNumSlotTypes; ++i)
            if (std::strcmp (kTypes[i].key, key) == 0) return (SlotType) i;
    return SlotType::empty;
}

const char* slotColour (SlotType t, NamRole r)
{
    // Farben angelehnt an typische Pedalfarben, etwas kräftiger für den dunklen
    // Hintergrund. Ein NAM-Slot hat die Farbe seiner Rolle.
    switch (t)
    {
        case SlotType::gate:    return "#8d99a8";   // kühles Grau
        case SlotType::comp:    return "#46b4e6";   // Hellblau
        case SlotType::drive:   return "#5fb03a";   // Grün
        case SlotType::nam:
            switch (r)
            {
                case NamRole::pedal:    return "#2fbf9c";   // Türkis
                case NamRole::outboard: return "#b25ad0";   // Violett
                case NamRole::fullrig:  return "#ea6b2f";   // Orange
                case NamRole::space:    return "#6f7fe0";   // Blau
                default:                return "#e04a3a";   // Rot
            }
        case SlotType::eq:      return "#c4ccd6";   // Silbergrau
        case SlotType::ir:      return "#e8c34a";   // Bernstein
        case SlotType::chorus:  return "#4a86e8";   // Blau
        case SlotType::flanger: return "#8a6ae8";   // Blauviolett
        case SlotType::phaser:  return "#f0912a";   // Orange
        case SlotType::tremolo: return "#e56aa8";   // Pink
        case SlotType::delay:   return "#4e9f72";   // gedecktes Grün
        case SlotType::reverb:  return "#3fc3e0";   // Cyan
        default:                return "#5a5f66";
    }
}

bool namControlShown (NamRole r, int k)
{
    const bool toneStack = r == NamRole::amp || r == NamRole::fullrig;
    switch (k)
    {
        case ctl::namGain: case ctl::namLevel:  return true;
        case ctl::namMix:                       return r == NamRole::pedal || r == NamRole::outboard
                                                    || r == NamRole::space;
        case ctl::namBass: case ctl::namMid:
        case ctl::namTreble:                    return toneStack;
        case ctl::namNorm:                      return r != NamRole::pedal;
        default:                                return false;
    }
}

const char* namRoleKey (NamRole r)
{
    const int i = (int) r;
    return kRoleKeys[i >= 0 && i < kNumNamRoles ? i : 0];
}

const char* namRoleName (NamRole r)
{
    const int i = (int) r;
    return kRoleNames[i >= 0 && i < kNumNamRoles ? i : 0];
}

NamRole namRoleFromKey (const char* key)
{
    if (key != nullptr)
        for (int i = 0; i < kNumNamRoles; ++i)
            if (std::strcmp (kRoleKeys[i], key) == 0) return (NamRole) i;
    return NamRole::amp;
}

bool sharesPlace (NamRole a, NamRole b)
{
    const auto isAmp = [] (NamRole r) { return r == NamRole::amp || r == NamRole::fullrig; };
    return a == b || (isAmp (a) && isAmp (b));
}

Layout::Layout()
{
    type.fill (SlotType::empty);
    role.fill (NamRole::amp);
    for (auto& l : lane) l.fill (-1);
    length.fill (0);
}

bool Layout::find (int slot, int& laneOut, int& posOut) const
{
    for (int l = 0; l < kNumLanes; ++l)
        for (int p = 0; p < length[(size_t) l]; ++p)
            if (lane[(size_t) l][(size_t) p] == slot) { laneOut = l; posOut = p; return true; }
    return false;
}

int Layout::count (SlotType t) const
{
    int n = 0;
    for (auto x : type) if (x == t) ++n;
    return n;
}

int Layout::firstFree() const
{
    for (int s = 0; s < kNumSlots; ++s)
        if (type[(size_t) s] == SlotType::empty) return s;
    return -1;
}

bool Layout::hasRoom (int l) const
{
    if (l < 0 || l >= kNumLanes) return false;
    for (int p = 0; p < kMaxLaneSlots; ++p)
        if (p >= length[(size_t) l] || lane[(size_t) l][(size_t) p] < 0) return true;
    return false;
}

bool Layout::insertAt (int l, int pos, int slot)
{
    if (l < 0 || l >= kNumLanes) return false;
    auto& row = lane[(size_t) l];
    auto& len = length[(size_t) l];
    pos = clamp (pos, 0, kMaxLaneSlots - 1);

    // Rechts von pos die erste Lücke (oder die Position hinter dem Ende). Bis
    // dorthin rückt alles eins weiter.
    int gap = pos;
    while (gap < kMaxLaneSlots && gap < len && row[(size_t) gap] >= 0) ++gap;
    if (gap >= kMaxLaneSlots) return false;

    for (int p = gap; p > pos; --p) row[(size_t) p] = row[(size_t) (p - 1)];
    row[(size_t) pos] = (int8_t) slot;
    // Hinter dem alten Ende liegen nur Lücken.
    for (int p = len; p < pos; ++p) row[(size_t) p] = -1;
    len = std::max (len, std::max (gap, pos) + 1);
    return true;
}

void Layout::removeAt (int l, int pos)
{
    if (l < 0 || l >= kNumLanes) return;
    auto& row = lane[(size_t) l];
    auto& len = length[(size_t) l];
    if (pos < 0 || pos >= len) return;
    row[(size_t) pos] = -1;
    while (len > 0 && row[(size_t) (len - 1)] < 0) --len;
}

void Layout::sanitise()
{
    // 1) Typ über seiner Grenze → leer. Die kleinere Nummer bleibt.
    int used[kNumSlotTypes] = {};
    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& t = type[(size_t) s];
        if ((int) t < 0 || (int) t >= kNumSlotTypes) t = SlotType::empty;
        if (t == SlotType::empty) continue;
        if (++used[(int) t] > slotTypeLimit (t)) t = SlotType::empty;
        if ((int) role[(size_t) s] < 0 || (int) role[(size_t) s] >= kNumNamRoles)
            role[(size_t) s] = NamRole::amp;
    }

    // 2) Jeder belegte Slot genau einmal in genau einer Spur, an seiner
    // Position. Lücken bleiben, nur am Ende fallen sie weg.
    bool placed[kNumSlots] = {};
    for (int l = 0; l < kNumLanes; ++l)
    {
        std::array<int8_t, kMaxLaneSlots> out;
        out.fill (-1);
        int w = 0;
        const int len = clamp (length[(size_t) l], 0, kMaxLaneSlots);
        for (int p = 0; p < len; ++p)
        {
            const int s = lane[(size_t) l][(size_t) p];
            if (s < 0 || s >= kNumSlots || placed[s]) continue;
            if (type[(size_t) s] == SlotType::empty) continue;
            placed[s] = true;
            out[(size_t) p] = (int8_t) s;
            w = p + 1;
        }
        lane[(size_t) l] = out;
        length[(size_t) l] = w;
    }

    // 3) Belegte Slots ohne Position hinten an eine Spur mit freiem Platz.
    for (int s = 0; s < kNumSlots; ++s)
    {
        if (placed[s] || type[(size_t) s] == SlotType::empty) continue;
        bool put = false;
        for (int l = 0; l < kNumLanes && ! put; ++l)
            if (length[(size_t) l] < kMaxLaneSlots)
                put = insertAt (l, length[(size_t) l], s);
        if (! put) type[(size_t) s] = SlotType::empty;
    }
}

Layout defaultLayout()
{
    // Werkszustand: leere Kette.
    return Layout {};
}

int placeForFile (const Layout& l, const std::array<bool, kNumSlots>& filled,
                  SlotType type, NamRole role, int preferred)
{
    const auto samePlace = [&] (int s)
    {
        return type != SlotType::nam || sharesPlace (l.role[(size_t) s], role);
    };

    // Der gewählte Slot: gleicher Typ und bei NAM leer oder derselbe Platz. Ein
    // Pedal überschreibt so keinen Amp, ein Full Rig aber den Amp und
    // umgekehrt.
    if (preferred >= 0 && preferred < kNumSlots && l.type[(size_t) preferred] == type
        && (type != SlotType::nam || ! filled[(size_t) preferred] || samePlace (preferred)))
        return preferred;

    // Sonst der erste passende Slot der Kette, ein leerer zuerst.
    for (int pass = 0; pass < 2; ++pass)
        for (int lane = 0; lane < kNumLanes; ++lane)
            for (int p = 0; p < l.length[(size_t) lane]; ++p)
            {
                const int s = l.lane[(size_t) lane][(size_t) p];
                if (s < 0 || l.type[(size_t) s] != type || ! samePlace (s)) continue;
                if (pass == 0 && filled[(size_t) s]) continue;
                return s;
            }

    return -1;
}

} // namespace nmp
