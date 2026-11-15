// Die Demo-UI bekommt ihre Regler aus dem Kern.
//
// ui/mock.js ahmt das Programm nach, damit sich die UI ohne Build ansehen
// lässt. Regler, Bereiche, Werkswerte, Typen, Grenzen und Farben stehen in
// ui/mock-defs.js. Die Datei wird hier erzeugt, und der Test schlägt an, sobald
// sie nicht mehr zum Kern passt. Neu schreiben:
//
//   NAMPLIFY_WRITE_MOCK=1 build/tests/namplify_tests MockDefs

#include "TestFramework.h"
#include "namplify/engine/Params.h"
#include "namplify/engine/Slots.h"
#include "namplify/engine/Engine.h"
#include "namplify/blocks/Drive.h"
#include "namplify/blocks/Reverb.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace nmp;

namespace
{
    std::string str (const char* s)
    {
        std::string out = "\"";
        for (const char* p = s != nullptr ? s : ""; *p != '\0'; ++p)
        {
            if (*p == '"' || *p == '\\') out += '\\';
            out += *p;
        }
        return out + "\"";
    }

    std::string num (float v) { return nmptest::fmt (v); }

    const char* kindName (ParamKind k)
    {
        switch (k)
        {
            case ParamKind::boolean:     return "bool";
            case ParamKind::choice:      return "choice";
            case ParamKind::logarithmic: return "log";
            default:                     return "linear";
        }
    }

    /// Dieselbe Form wie describe() in plugin/src/ui/WebBridge.cpp.
    std::string describe (const ParamDesc& d, const std::string& extra = {})
    {
        std::string s = "{ id: " + str (d.id) + ", name: " + str (d.name)
                      + ", kind: " + str (kindName (d.kind))
                      + ", min: " + num (d.min) + ", max: " + num (d.max)
                      + ", def: " + num (d.def) + ", unit: " + str (d.unit);
        if (d.kind == ParamKind::choice)
        {
            s += ", choices: [";
            for (int c = 0; c < d.numChoices; ++c) s += (c ? ", " : "") + str (d.choices[c]);
            s += "]";
        }
        return s + extra + " }";
    }

    std::string generate()
    {
        std::ostringstream o;
        o << "// Erzeugt von tests/MockDefsTest.cpp aus dem Kern: nicht von Hand ändern.\n"
             "// Neu schreiben: NAMPLIFY_WRITE_MOCK=1 build/tests/namplify_tests MockDefs\n\n";

        o << "export const LIMITS = { slots: " << kNumSlots << ", laneSlots: " << kMaxLaneSlots
          << ", lanes: " << kNumLanes << ", slotControls: " << kSlotControls << " };\n\n";

        o << "/// Die festen Parameter, in der Reihenfolge der Parameterliste.\n";
        o << "export const FIXED = [\n";
        for (int i = 0; i < (int) ParamId::firstSlotParam; ++i)
            o << "  " << describe (paramDesc (i)) << ",\n";
        o << "];\n\n";

        o << "export const TYPES = [\n";
        for (int t = 1; t < kNumSlotTypes; ++t)
        {
            const auto type = (SlotType) t;
            o << "  { key: " << str (slotTypeKey (type)) << ", name: " << str (slotTypeName (type))
              << ", colour: " << str (slotColour (type, NamRole::amp))
              << ", limit: " << slotTypeLimit (type)
              << ", takesFiles: " << (type == SlotType::nam || type == SlotType::ir ? "true" : "false")
              << ", controls: [\n";
            for (int k = 0; k < slotControlCount (type); ++k)
            {
                std::string extra;
                if (type == SlotType::nam)
                {
                    extra = ", roles: [";
                    bool first = true;
                    for (int r = 0; r < kNumNamRoles; ++r)
                        if (namControlShown ((NamRole) r, k))
                        { extra += (first ? "" : ", ") + str (namRoleKey ((NamRole) r)); first = false; }
                    extra += "]";
                }
                o << "      " << describe (slotControl (type, k), extra) << ",\n";
            }
            o << "  ] },\n";
        }
        o << "];\n\n";

        o << "export const ROLES = [\n";
        for (int r = 0; r < kNumNamRoles; ++r)
            o << "  { key: " << str (namRoleKey ((NamRole) r)) << ", name: " << str (namRoleName ((NamRole) r))
              << ", colour: " << str (slotColour (SlotType::nam, (NamRole) r)) << " },\n";
        o << "];\n\n";

        o << "export const TOOLS = [\n";
        for (int t = 0; t < kNumTools; ++t)
            o << "  { key: " << str (toolKey ((Tool) t)) << ", name: " << str (toolName ((Tool) t))
              << ", colour: " << str (toolColour ((Tool) t)) << " },\n";
        o << "];\n\n";

        o << "export const DRIVE_DEFAULTS = [\n";
        for (int i = 0; i < (int) DriveModel::count; ++i)
        {
            const auto d = driveDefaults ((DriveModel) i);
            o << "  { name: " << str (driveModelName ((DriveModel) i)) << ", gain: " << num (d.gainDb)
              << ", level: " << num (d.levelDb) << ", tone: " << num (d.tone) << ", body: " << num (d.body) << " },\n";
        }
        o << "];\n\n";

        o << "export const REVERB_DEFAULTS = [\n";
        for (int i = 0; i < (int) ReverbType::count; ++i)
        {
            const auto d = reverbDefaults ((ReverbType) i);
            o << "  { name: " << str (reverbTypeName ((ReverbType) i)) << ", time: " << num (d.timeSec)
              << ", size: " << num (d.size) << ", boing: " << num (d.boing) << ", tone: " << num (d.tone)
              << ", mix: " << num (d.mix) << " },\n";
        }
        o << "];\n\n";

        const auto l = defaultLayout();
        o << "/// Der Werksaufbau: welcher Slot welchen Typ hat und wo er steht.\n";
        o << "export const DEFAULT_LAYOUT = [\n";
        for (int lane = 0; lane < kNumLanes; ++lane)
            for (int p = 0; p < l.length[(size_t) lane]; ++p)
            {
                const int s = l.lane[(size_t) lane][(size_t) p];
                o << "  { slot: " << s << ", type: " << str (slotTypeKey (l.type[(size_t) s]))
                  << ", role: " << str (namRoleKey (l.role[(size_t) s]))
                  << ", lane: " << lane << ", pos: " << p << " },\n";
            }
        o << "];\n";

        return o.str();
    }
}

NMP_TEST (MockDefs_ReglerKommenAusDemKern)
{
    const std::string path = std::string (NAMPLIFY_UI_DIR) + "/mock-defs.js";
    const auto wanted = generate();

    if (const char* w = std::getenv ("NAMPLIFY_WRITE_MOCK"); w != nullptr && w[0] == '1')
    {
        std::ofstream out (path, std::ios::binary);
        out << wanted;
        std::printf ("      geschrieben: %s\n", path.c_str());
    }

    std::ifstream in (path, std::ios::binary);
    std::stringstream have;
    have << in.rdbuf();

    // Git legt Textdateien unter Windows mit CRLF ab. Verglichen wird der
    // Inhalt, nicht das Zeilenende.
    std::string text = have.str();
    text.erase (std::remove (text.begin(), text.end(), '\r'), text.end());

    CHECK_MSG (text == wanted,
               "ui/mock-defs.js passt nicht mehr zum Kern: neu schreiben mit "
               "NAMPLIFY_WRITE_MOCK=1 build/tests/namplify_tests MockDefs");
}
