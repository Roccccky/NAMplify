#include "RigState.h"

namespace nmpl
{

nmp::NamRole roleForGear (const juce::String& gear)
{
    const auto g = gear.toLowerCase();
    if (g == "pedal")    return nmp::NamRole::pedal;
    if (g == "outboard") return nmp::NamRole::outboard;
    if (g == "space")    return nmp::NamRole::space;
    if (g == "amp-cab" || g == "full-rig" || g == "fullrig") return nmp::NamRole::fullrig;
    return nmp::NamRole::amp;
}

bool slotTakesFiles (nmp::SlotType t)
{
    return t == nmp::SlotType::nam || t == nmp::SlotType::ir;
}

juce::var RigState::toVar() const
{
    juce::DynamicObject::Ptr o (new juce::DynamicObject());
    o->setProperty ("version", 2);

    juce::Array<juce::var> slots;
    for (int lane = 0; lane < nmp::kNumLanes; ++lane)
        for (int pos = 0; pos < layout.length[(size_t) lane]; ++pos)
        {
            const int s = layout.lane[(size_t) lane][(size_t) pos];
            if (s < 0) continue;

            juce::DynamicObject::Ptr item (new juce::DynamicObject());
            item->setProperty ("slot", s);
            item->setProperty ("type", juce::String (nmp::slotTypeKey (layout.type[(size_t) s])));
            if (layout.type[(size_t) s] == nmp::SlotType::nam)
                item->setProperty ("role", juce::String (nmp::namRoleKey (layout.role[(size_t) s])));
            item->setProperty ("lane", lane);
            item->setProperty ("pos", pos);

            const auto& f = files[(size_t) s];
            if (! f.isEmpty())
            {
                juce::DynamicObject::Ptr file (new juce::DynamicObject());
                file->setProperty ("path", f.relativePath);
                file->setProperty ("bank", f.bankTitle);
                file->setProperty ("variant", f.variantName);
                file->setProperty ("gear", f.gear);
                file->setProperty ("image", f.imageFile);
                item->setProperty ("file", juce::var (file.get()));
            }
            slots.add (juce::var (item.get()));
        }
    o->setProperty ("slots", slots);

    juce::Array<juce::var> t;
    for (int i = 0; i < nmp::kNumTools; ++i)
        if (tools[(size_t) i]) t.add (juce::String (nmp::toolKey ((nmp::Tool) i)));
    o->setProperty ("tools", t);

    o->setProperty ("zoom", zoom);

    juce::Array<juce::var> vols;
    for (float v : looperVolumes) vols.add (v);
    o->setProperty ("looperVolumes", vols);

    return juce::var (o.get());
}

namespace
{
    SlotFile readFile (const juce::var& v)
    {
        SlotFile f;
        if (auto* item = v.getDynamicObject())
        {
            f.relativePath = item->getProperty ("path").toString();
            f.bankTitle    = item->getProperty ("bank").toString();
            f.variantName  = item->getProperty ("variant").toString();
            f.gear         = item->getProperty ("gear").toString();
            f.imageFile    = item->getProperty ("image").toString();
        }
        return f;
    }

    void readCommon (RigState& r, juce::DynamicObject& o)
    {
        if (o.hasProperty ("zoom")) r.zoom = juce::jlimit (0, 2, (int) o.getProperty ("zoom"));

        if (auto* arr = o.getProperty ("looperVolumes").getArray())
            for (int i = 0; i < juce::jmin (4, arr->size()); ++i)
                r.looperVolumes[(size_t) i] = juce::jlimit (0.0f, 1.0f, (float) (double) (*arr)[i]);
    }

    void readTools (RigState& r, const juce::var& list)
    {
        r.tools.fill (false);
        if (auto* arr = list.getArray())
            for (const auto& item : *arr)
                for (int i = 0; i < nmp::kNumTools; ++i)
                    if (item.toString().equalsIgnoreCase (nmp::toolKey ((nmp::Tool) i)))
                        r.tools[(size_t) i] = true;
    }

    // Format 1: vierzehn feste Karten in fester Reihenfolge. Jede Karte auf dem
    // Board wird ein Slot in Spur A, in derselben Reihenfolge. Die Regler
    // kommen über ihre ID mit (Slots.h verwendet die IDs aus Format 1). Nur die
    // drei Capture-Karten hatten eigene Namen (amp_vol, pedal_level …), die
    // stehen hier.

    struct LegacyCard
    {
        const char* name;          ///< Kurzname in Format 1
        nmp::SlotType type;
        nmp::NamRole role;
        const char* onId;          ///< An/Aus in Format 1, nullptr = immer an
        const char* fileKey;       ///< Schlüssel unter slots, nullptr = keine Datei
    };

    const LegacyCard kLegacy[] =
    {
        { "GATE",     nmp::SlotType::gate,    nmp::NamRole::amp,      nullptr,       nullptr },
        { "COMP",     nmp::SlotType::comp,    nmp::NamRole::amp,      "comp_on",     nullptr },
        { "DRIVE",    nmp::SlotType::drive,   nmp::NamRole::amp,      "drive_on",    nullptr },
        { "PEDAL",    nmp::SlotType::nam,     nmp::NamRole::pedal,    "pedal_on",    "pedal" },
        { "AMP",      nmp::SlotType::nam,     nmp::NamRole::amp,      "amp_on",      "amp" },
        { "EQ",       nmp::SlotType::eq,      nmp::NamRole::amp,      "eq_on",       nullptr },
        { "CAB",      nmp::SlotType::ir,      nmp::NamRole::amp,      "cab_on",      "cab" },
        { "OUTBOARD", nmp::SlotType::nam,     nmp::NamRole::outboard, "outboard_on", "outboard" },
        { "CHORUS",   nmp::SlotType::chorus,  nmp::NamRole::amp,      "chorus_on",   nullptr },
        { "FLANGER",  nmp::SlotType::flanger, nmp::NamRole::amp,      "flanger_on",  nullptr },
        { "PHASER",   nmp::SlotType::phaser,  nmp::NamRole::amp,      "phaser_on",   nullptr },
        { "TREMOLO",  nmp::SlotType::tremolo, nmp::NamRole::amp,      "tremolo_on",  nullptr },
        { "DELAY",    nmp::SlotType::delay,   nmp::NamRole::amp,      "delay_on",    nullptr },
        { "REVERB",   nmp::SlotType::reverb,  nmp::NamRole::amp,      "reverb_on",   nullptr },
    };

    /// Name eines NAM-Reglers in Format 1, je nach Karte.
    juce::String legacyNamControl (const juce::String& card, int k)
    {
        switch (k)
        {
            case nmp::ctl::namLevel:
                return card == "AMP" ? "amp_vol" : (card == "PEDAL" ? "pedal_level" : "outboard_level");
            case nmp::ctl::namMix:
                return card == "AMP" ? juce::String() : (card == "PEDAL" ? "pedal_mix" : "outboard_mix");
            case nmp::ctl::namBass: case nmp::ctl::namMid:
            case nmp::ctl::namTreble: case nmp::ctl::namNorm:
                return card == "AMP" ? juce::String (nmp::slotControl (nmp::SlotType::nam, k).id) : juce::String();
            default:
                return {};   // GAIN gab es pro Slot nicht
        }
    }

    RigState fromLegacy (juce::DynamicObject& o, juce::var& params)
    {
        RigState r;
        r.layout = nmp::Layout{};
        readCommon (r, o);

        juce::StringArray board;
        if (auto* arr = o.getProperty ("board").getArray())
            for (const auto& item : *arr) board.add (item.toString().toUpperCase());
        else
            board.addArray ({ "AMP", "CAB" });

        juce::StringArray order;
        if (auto* arr = o.getProperty ("order").getArray())
            for (const auto& item : *arr) order.add (item.toString().toUpperCase());
        for (const auto& c : kLegacy)
            if (! order.contains (c.name)) order.add (c.name);

        r.tools[(size_t) nmp::Tool::looper]   = board.contains ("LOOPER");
        r.tools[(size_t) nmp::Tool::drums]    = board.contains ("DRUMS");
        r.tools[(size_t) nmp::Tool::recorder] = board.contains ("RECORDER");

        auto* oldSlots = o.getProperty ("slots").getDynamicObject();

        // Die alten Regler bleiben (die festen gehen über ihre ID durch), dazu
        // kommen die der Slots.
        juce::DynamicObject::Ptr out (new juce::DynamicObject());
        auto* in = params.getDynamicObject();
        if (in != nullptr)
            for (const auto& p : in->getProperties()) out->setProperty (p.name, p.value);

        auto oldUnit = [in] (const juce::String& id, float fallback)
        {
            if (in != nullptr && id.isNotEmpty() && in->hasProperty (juce::Identifier (id)))
                return (float) (double) in->getProperty (juce::Identifier (id));
            return fallback;
        };

        int slot = 0;
        for (const auto& name : order)
        {
            if (! board.contains (name)) continue;
            const LegacyCard* card = nullptr;
            for (const auto& c : kLegacy) if (name == c.name) card = &c;
            if (card == nullptr || slot >= nmp::kNumSlots || r.layout.length[0] >= nmp::kMaxLaneSlots) continue;

            r.layout.type[(size_t) slot] = card->type;
            r.layout.role[(size_t) slot] = card->role;
            r.layout.lane[0][(size_t) r.layout.length[0]++] = (int8_t) slot;

            if (card->fileKey != nullptr && oldSlots != nullptr)
            {
                r.files[(size_t) slot] = readFile (oldSlots->getProperty (card->fileKey));
                if (card->type == nmp::SlotType::nam && ! r.files[(size_t) slot].isEmpty()
                    && card->role == nmp::NamRole::amp)
                    r.layout.role[(size_t) slot] = roleForGear (r.files[(size_t) slot].gear);
            }

            const auto& onDesc = nmp::paramDesc (nmp::slotOnParam (slot));
            out->setProperty (onDesc.id, card->onId != nullptr ? oldUnit (card->onId, 0.0f) : 1.0f);

            for (int k = 0; k < nmp::kSlotControls; ++k)
            {
                const auto& d = nmp::slotControl (card->type, k);
                const float def = k < nmp::slotControlCount (card->type) ? d.toUnit (d.def) : 0.0f;
                const juce::String oldId = card->type == nmp::SlotType::nam
                                         ? legacyNamControl (name, k)
                                         : juce::String (k < nmp::slotControlCount (card->type) ? d.id : "");
                out->setProperty (nmp::paramDesc (nmp::slotControlParam (slot, k)).id, oldUnit (oldId, def));
            }
            ++slot;
        }

        // Freie Slots ausgeschaltet.
        for (int s = slot; s < nmp::kNumSlots; ++s)
            out->setProperty (nmp::paramDesc (nmp::slotOnParam (s)).id, 0.0f);

        r.layout.sanitise();
        params = juce::var (out.get());
        return r;
    }
}

RigState RigState::fromVar (const juce::var& v, juce::var& params)
{
    RigState r;
    auto* o = v.getDynamicObject();
    if (o == nullptr) return r;

    if ((int) o->getProperty ("version") < 2) return fromLegacy (*o, params);

    r.layout = nmp::Layout{};
    readCommon (r, *o);
    readTools (r, o->getProperty ("tools"));

    // Nach Spur und Position sortiert einsammeln, dann prüfen. Auch aus einer
    // beschädigten Liste muss ein gültiger Aufbau werden.
    struct Entry { int slot, lane, pos; };
    std::vector<Entry> entries;

    if (auto* arr = o->getProperty ("slots").getArray())
        for (const auto& item : *arr)
        {
            const int s = (int) item.getProperty ("slot", -1);
            if (s < 0 || s >= nmp::kNumSlots) continue;

            const auto type = nmp::slotTypeFromKey (item.getProperty ("type", {}).toString().toRawUTF8());
            if (type == nmp::SlotType::empty) continue;

            r.layout.type[(size_t) s] = type;
            r.layout.role[(size_t) s] = nmp::namRoleFromKey (item.getProperty ("role", "amp").toString().toRawUTF8());
            r.files[(size_t) s] = readFile (item.getProperty ("file", {}));
            entries.push_back ({ s, juce::jlimit (0, nmp::kNumLanes - 1, (int) item.getProperty ("lane", 0)),
                                 (int) item.getProperty ("pos", 0) });
        }

    std::stable_sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
                      { return a.lane != b.lane ? a.lane < b.lane : a.pos < b.pos; });

    // An die gespeicherte Position, Lücken eingeschlossen. Ältere Presets ohne
    // Lücken haben die Positionen 0, 1, 2 …
    for (const auto& e : entries)
    {
        const int at = juce::jlimit (0, nmp::kMaxLaneSlots - 1, e.pos);
        const int len = r.layout.length[(size_t) e.lane];
        auto& row = r.layout.lane[(size_t) e.lane];
        if (at >= len || row[(size_t) at] < 0)
        {
            for (int p = len; p < at; ++p) row[(size_t) p] = -1;
            row[(size_t) at] = (int8_t) e.slot;
            r.layout.length[(size_t) e.lane] = std::max (len, at + 1);
        }
        else
            r.layout.insertAt (e.lane, at, e.slot);
    }

    r.layout.sanitise();
    for (int s = 0; s < nmp::kNumSlots; ++s)
        if (r.layout.type[(size_t) s] == nmp::SlotType::empty || ! slotTakesFiles (r.layout.type[(size_t) s]))
            r.files[(size_t) s].clear();

    return r;
}

} // namespace nmpl
