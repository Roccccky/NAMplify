#include "PresetManager.h"
#include "Library.h"
#include <algorithm>

namespace nmpl
{

namespace { const char* kExtension = ".namplifyrig"; }

PresetManager::PresetManager (Library& l) : library (l) { rescan(); }

juce::String PresetManager::sanitise (const juce::String& s)
{
    auto out = s.retainCharacters ("abcdefghijklmnopqrstuvwxyz"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "0123456789 -_()&+.,'").trim();
    return out.isEmpty() ? juce::String ("Preset") : out.substring (0, 64);
}

void PresetManager::rescan()
{
    list.clear();

    const auto dir = library.presetsDir();
    if (! dir.isDirectory()) return;

    for (const auto& e : juce::RangedDirectoryIterator (dir, false, juce::String ("*") + kExtension,
                                                        juce::File::findFiles))
        list.push_back ({ e.getFile().getFileNameWithoutExtension(), e.getFile() });

    std::sort (list.begin(), list.end(),
               [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });
}

int PresetManager::currentIndex() const
{
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == current) return (int) i;
    return -1;
}

bool PresetManager::save (const juce::String& name, const juce::var& params, const RigState& rig,
                          juce::String& error)
{
    const auto safe = sanitise (name);
    const auto file = library.presetsDir().getChildFile (safe + kExtension);

    juce::DynamicObject::Ptr o (new juce::DynamicObject());
    o->setProperty ("format", 1);
    o->setProperty ("name", safe);
    o->setProperty ("params", params);
    o->setProperty ("rig", rig.toVar());

    if (! file.replaceWithText (juce::JSON::toString (juce::var (o.get()), false, 6)))
    {
        error = "The preset could not be written.";
        return false;
    }

    rescan();
    current = safe;
    return true;
}

bool PresetManager::load (const juce::String& name, juce::var& params, RigState& rig,
                          juce::String& error)
{
    for (const auto& p : list)
    {
        if (p.name != name) continue;

        const auto json = juce::JSON::parse (p.file);
        auto* o = json.getDynamicObject();
        if (o == nullptr) { error = "That preset is damaged."; return false; }

        params = o->getProperty ("params");
        rig    = RigState::fromVar (o->getProperty ("rig"), params);
        current = name;
        return true;
    }

    error = "There is no such preset.";
    return false;
}

bool PresetManager::remove (const juce::String& name, juce::String& error)
{
    for (const auto& p : list)
    {
        if (p.name != name) continue;

        if (! p.file.deleteFile()) { error = "The preset could not be deleted."; return false; }
        if (current == name) current.clear();
        rescan();
        return true;
    }

    error = "There is no such preset.";
    return true;
}

juce::String PresetManager::neighbour (int direction) const
{
    if (list.empty()) return {};

    const int n = (int) list.size();
    int index = currentIndex();

    if (index < 0) return list[direction >= 0 ? 0 : (size_t) (n - 1)].name;

    // Am Ende umlaufend.
    index = ((index + direction) % n + n) % n;
    return list[(size_t) index].name;
}

juce::String PresetManager::byProgram (int program) const
{
    // Programm 1 lädt das erste Preset, Programm 2 das zweite, in der
    // Reihenfolge der Liste.
    if (program < 0 || program >= (int) list.size()) return {};
    return list[(size_t) program].name;
}

} // namespace nmpl
