// Prüft die Parameterliste gegen die interne Aufzählung.
//
// Die Kopie unten ist absichtlich von Hand geschrieben. Wer die Reihenfolge im
// Quelltext ändert, macht gespeicherte Presets und Automation kaputt und muss
// hier nachziehen.

#include "TestFramework.h"
#include "namplify/engine/Params.h"
#include "namplify/engine/Slots.h"
#include "namplify/engine/Engine.h"
#include <set>
#include <string>
#include <algorithm>
#include <cmath>

using namespace nmp;
using namespace nmptest;

namespace
{
    // Version 2 (freie Slots). Nichts ändern, nur hinten anfügen, hinter den
    // Slots.
    const char* const kFixedContract[] =
    {
        "in_gain", "out_gain", "mute", "tuner",
        "spread_ms",
        "drums_on", "drums_mode", "drums_pattern", "drums_click", "drums_bpm", "drums_volume",
        "looper_on", "recorder_on",
        "midi_channel", "midi_program_change", "midi_thru",
        "sync_drums", "sync_looper",
        "dual_on", "lane_b_source", "split_after", "merge_after",
        "lane_a_level", "lane_a_pan", "lane_a_solo", "lane_a_invert",
        "lane_b_level", "lane_b_pan", "lane_b_solo", "lane_b_invert",
    };
    constexpr int kFixedSize = (int) (sizeof (kFixedContract) / sizeof (kFixedContract[0]));

    /// Die Regler je Typ in ihrer Reihenfolge (Knob 1 … n im Host).
    struct TypeContract { SlotType type; std::vector<std::string> ids; };
    const TypeContract kTypeContract[] =
    {
        { SlotType::gate,    { "gate_threshold" } },
        { SlotType::comp,    { "comp_sustain", "comp_level" } },
        { SlotType::drive,   { "drive_model", "drive_gain", "drive_level", "drive_tone", "drive_body" } },
        { SlotType::nam,     { "nam_gain", "nam_level", "nam_mix", "amp_bass", "amp_mid", "amp_treble", "amp_norm" } },
        { SlotType::eq,      { "eq_mode", "eq3_bass", "eq3_mid", "eq3_treble",
                               "eq7_100", "eq7_200", "eq7_400", "eq7_800", "eq7_1600", "eq7_3200", "eq7_6400" } },
        { SlotType::ir,      { "cab_output" } },
        { SlotType::chorus,  { "chorus_rate", "chorus_depth", "chorus_mix" } },
        { SlotType::flanger, { "flanger_rate", "flanger_depth", "flanger_feedback", "flanger_manual", "flanger_mix" } },
        { SlotType::phaser,  { "phaser_rate", "phaser_depth", "phaser_mix" } },
        { SlotType::tremolo, { "tremolo_rate", "tremolo_depth", "sync_tremolo", "sync_tremolo_div" } },
        { SlotType::delay,   { "delay_type", "delay_time", "delay_feedback", "delay_mix", "sync_delay", "sync_delay_div" } },
        { SlotType::reverb,  { "reverb_type", "reverb_time", "reverb_size", "reverb_boing", "reverb_tone", "reverb_mix" } },
    };

    void checkDesc (const ParamDesc& d, const std::string& where)
    {
        CHECK_MSG (d.max > d.min, where + ": Bereich verkehrt herum");
        CHECK_MSG (d.def >= d.min && d.def <= d.max, where + ": Werkswert außerhalb des Bereichs");

        if (d.kind == ParamKind::choice)
        {
            CHECK_MSG (d.numChoices >= 2, where + ": Stufenschalter mit weniger als zwei Stellungen");
            CHECK_MSG (d.choices != nullptr, where + ": Stufenschalter ohne Begriffe");
            for (int c = 0; c < d.numChoices; ++c)
                CHECK_MSG (d.choices[c] != nullptr && d.choices[c][0] != '\0', where + ": leerer Begriff");
        }

        if (d.kind == ParamKind::logarithmic)
            CHECK_MSG (d.min > 0.0f, where + ": logarithmisch, aber der Bereich beginnt bei null");

        // Hin und zurück muss denselben Wert ergeben, sonst wandert ein Regler
        // bei jedem Speichern und Laden.
        for (float u : { 0.0f, 0.13f, 0.5f, 0.77f, 1.0f })
        {
            const float v = d.fromUnit (u);
            const float back = d.toUnit (v);
            const float again = d.fromUnit (back);
            CHECK_MSG (std::abs (again - v) <= std::max (1.0e-3f, std::abs (v) * 1.0e-3f),
                       where + ": " + fmt (u) + " → " + fmt (v) + " → " + fmt (back) + " → " + fmt (again));
        }
    }
}

NMP_TEST (Vertrag_ReihenfolgeUndAnzahlStimmen)
{
    CHECK_MSG ((int) ParamId::firstSlotParam == kFixedSize,
               "Vor den Plätzen stehen " + std::to_string ((int) ParamId::firstSlotParam)
               + " Parameter, der Vertrag nennt " + std::to_string (kFixedSize)
               + ". Neues gehört hinter die Plätze.");

    const int n = std::min ((int) ParamId::firstSlotParam, kFixedSize);
    for (int i = 0; i < n; ++i)
        CHECK_MSG (std::string (paramDesc (i).id) == kFixedContract[i],
                   "Platz " + std::to_string (i) + ": im Vertrag steht \""
                   + kFixedContract[i] + "\", im Quelltext \"" + paramDesc (i).id + "\"");

    // Die Slots: s1_on, s1_k1 … s16_k11, lückenlos.
    CHECK (kNumSlots == 16);
    CHECK (kSlotControls == 11);
    CHECK (kNumParams == kFixedSize + 16 * 12);
    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto on = "s" + std::to_string (s + 1) + "_on";
        CHECK_MSG (std::string (paramDesc (slotOnParam (s)).id) == on, "An/Aus von Platz " + std::to_string (s + 1));
        for (int k = 0; k < kSlotControls; ++k)
        {
            const auto id = "s" + std::to_string (s + 1) + "_k" + std::to_string (k + 1);
            CHECK_MSG (std::string (paramDesc (slotControlParam (s, k)).id) == id, "Regler " + id);

            int ss = -1, kk = -2;
            CHECK (slotParamInfo (slotControlParam (s, k), ss, kk) && ss == s && kk == k);
        }
    }
}

NMP_TEST (Vertrag_ReglerJeTypStehenFest)
{
    // Knob 2 eines Delays ist seine Zeit, in jedem Preset und jeder Automation.
    // Die Kennungen stammen aus Version 1, damit alte Presets ihre Werte
    // wiederfinden.
    for (const auto& t : kTypeContract)
    {
        CHECK_MSG (slotControlCount (t.type) == (int) t.ids.size(),
                   std::string (slotTypeKey (t.type)) + ": " + std::to_string (slotControlCount (t.type))
                   + " Regler, der Vertrag nennt " + std::to_string (t.ids.size()));
        for (int k = 0; k < (int) t.ids.size(); ++k)
            CHECK_MSG (t.ids[(size_t) k] == slotControl (t.type, k).id,
                       std::string (slotTypeKey (t.type)) + " Regler " + std::to_string (k + 1)
                       + ": Vertrag \"" + t.ids[(size_t) k] + "\", Quelltext \"" + slotControl (t.type, k).id + "\"");
        CHECK (slotControlCount (t.type) <= kSlotControls);
    }

    // Typen und Rollen über ihren Schlüssel hin und zurück.
    for (int i = 0; i < kNumSlotTypes; ++i)
        CHECK ((int) slotTypeFromKey (slotTypeKey ((SlotType) i)) == i);
    for (int i = 0; i < kNumNamRoles; ++i)
        CHECK ((int) namRoleFromKey (namRoleKey ((NamRole) i)) == i);
}

/// SPACE wird wie OUTBOARD beigemischt (MIX ja, Klangregelung nein) und steht
/// hinter den älteren Rollen.
NMP_TEST (Vertrag_SpaceMischtOhneKlangregelung)
{
    CHECK ((int) NamRole::space == 4);
    CHECK (std::string (namRoleKey (NamRole::space)) == "space");
    CHECK (namControlShown (NamRole::space, ctl::namMix));
    CHECK (namControlShown (NamRole::space, ctl::namNorm));
    CHECK (! namControlShown (NamRole::space, ctl::namBass));
    CHECK (! namControlShown (NamRole::space, ctl::namTreble));
}

NMP_TEST (Vertrag_KennungenSindEindeutigUndNichtLeer)
{
    std::set<std::string> ids, names;

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = paramDesc (i);

        CHECK_MSG (d.id != nullptr && d.id[0] != '\0', "Parameter " + std::to_string (i) + " hat keine Kennung");
        CHECK_MSG (d.name != nullptr && d.name[0] != '\0', "Parameter " + std::to_string (i) + " hat keinen Namen");
        CHECK_MSG (ids.insert (d.id).second, std::string ("Kennung \"") + d.id + "\" kommt zweimal vor");
        CHECK_MSG (names.insert (d.name).second, std::string ("Anzeigename \"") + d.name + "\" kommt zweimal vor");
        CHECK_MSG (paramIndexFromId (d.id) == i,
                   std::string ("Nachschlagen von \"") + d.id + "\" liefert den falschen Platz");
    }

    // Die Kennungen der Regler je Typ dürfen sich nicht doppeln, sonst fände
    // ein altes Preset zwei Regler für einen Wert.
    std::set<std::string> controls;
    for (int t = 1; t < kNumSlotTypes; ++t)
        for (int k = 0; k < slotControlCount ((SlotType) t); ++k)
            CHECK_MSG (controls.insert (slotControl ((SlotType) t, k).id).second,
                       std::string ("Regler \"") + slotControl ((SlotType) t, k).id + "\" gibt es zweimal");
}

NMP_TEST (Vertrag_BereicheUndWerkswerteSindStimmig)
{
    for (int i = 0; i < kNumParams; ++i)
        checkDesc (paramDesc (i), std::string ("\"") + paramDesc (i).id + "\"");

    for (int t = 1; t < kNumSlotTypes; ++t)
        for (int k = 0; k < slotControlCount ((SlotType) t); ++k)
            checkDesc (slotControl ((SlotType) t, k),
                       std::string (slotTypeKey ((SlotType) t)) + " \"" + slotControl ((SlotType) t, k).id + "\"");
}

NMP_TEST (Vertrag_WerkszustandIstEineLeereKette)
{
    // Ab Werk ist die Kette leer, wie bei einem Hardware-Modeller: kein Slot
    // belegt, keiner an, alles andere aus.
    const auto l = defaultLayout();
    CHECK (l.length[0] == 0 && l.length[1] == 0);
    for (int s = 0; s < kNumSlots; ++s) CHECK (l.type[(size_t) s] == SlotType::empty);

    Engine e;
    auto& p = e.params();
    for (int s = 0; s < kNumSlots; ++s) CHECK (! p.getBool (slotOnParam (s)));

    // Ein neuer NAM-Slot startet auf NORMALIZED (Stellung 2 von RAW,
    // CALIBRATED, NORMALIZED) und mit der Klangregelung auf null.
    CHECK_NEAR (slotControl (SlotType::nam, ctl::namNorm).def, 2.0f, 1.0e-6f);
    CHECK_NEAR (slotControl (SlotType::nam, ctl::namBass).def, 0.0f, 1.0e-4f);

    CHECK (! p.getBool (ParamId::drumsOn));
    CHECK (! p.getBool (ParamId::looperOn));
    CHECK (! p.getBool (ParamId::recorderOn));
    CHECK (! p.getBool (ParamId::dualOn));
    CHECK (! p.getBool (ParamId::mute));
    CHECK_NEAR (p.get (ParamId::inGain),  0.0f, 1.0e-6f);
    CHECK_NEAR (p.get (ParamId::outGain), 0.0f, 1.0e-6f);
    CHECK_NEAR (p.get (ParamId::spreadMs), 1.0f, 1.0e-6f);

    // Ein neuer Hall startet auf PLATE mit dessen Werkswerten.
    CHECK ((int) slotControl (SlotType::reverb, ctl::reverbType).def == 2);
    CHECK_NEAR (slotControl (SlotType::reverb, ctl::reverbTime).def, 1.80f, 1.0e-4f);
    CHECK_NEAR (slotControl (SlotType::reverb, ctl::reverbMix).def,  0.18f, 1.0e-4f);
    CHECK_NEAR (slotControl (SlotType::gate, ctl::gateThreshold).def, -70.0f, 1.0e-6f);
}
