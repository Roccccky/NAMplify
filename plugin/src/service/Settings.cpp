#include "Settings.h"
#include <namplify/engine/Params.h>

namespace nmpl
{

namespace
{
    juce::File settingsFile()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  #if JUCE_MAC
                       .getChildFile ("Application Support")
                  #endif
                       .getChildFile ("NAMplify");
        dir.createDirectory();
        return dir.getChildFile ("settings.json");
    }
}

Settings::Settings()
{
    file = settingsFile();
    curve = nmp::factoryCurve (0).curve;
    load();
}

Settings::~Settings() { save(); }

juce::var Settings::curveToVar (const nmp::EqCurve& c)
{
    juce::Array<juce::var> bands;
    for (int i = 0; i < nmp::EqCurve::kBands; ++i)
    {
        juce::DynamicObject::Ptr b (new juce::DynamicObject());
        b->setProperty ("freq", c.bands[i].freq);
        b->setProperty ("gain", c.bands[i].gainDb);
        b->setProperty ("q", c.bands[i].q);
        b->setProperty ("shape", (int) c.bands[i].shape);
        bands.add (juce::var (b.get()));
    }
    return bands;
}

nmp::EqCurve Settings::curveFromVar (const juce::var& v)
{
    nmp::EqCurve c = nmp::factoryCurve (0).curve;
    if (auto* arr = v.getArray())
    {
        for (int i = 0; i < juce::jmin (arr->size(), nmp::EqCurve::kBands); ++i)
        {
            if (auto* o = (*arr)[i].getDynamicObject())
            {
                c.bands[i].freq   = (float) (double) o->getProperty ("freq");
                c.bands[i].gainDb = (float) (double) o->getProperty ("gain");
                c.bands[i].q      = (float) (double) o->getProperty ("q");
                c.bands[i].shape  = (nmp::FilterShape) (int) o->getProperty ("shape");
            }
        }
    }
    return c;
}

void Settings::load()
{
    if (! file.existsAsFile()) return;

    const auto json = juce::JSON::parse (file);
    auto* o = json.getDynamicObject();
    if (o == nullptr) return;

    auto getInt = [&] (const char* k, int def) { return o->hasProperty (k) ? (int) o->getProperty (k) : def; };
    auto getBool = [&] (const char* k, bool def) { return o->hasProperty (k) ? (bool) o->getProperty (k) : def; };
    auto getStr = [&] (const char* k) { return o->hasProperty (k) ? o->getProperty (k).toString() : juce::String{}; };

    startBehaviour = (OnStart) juce::jlimit (0, 2, getInt ("onStart", 0));
    ranBefore      = getBool ("hasRun", false);
    tuner          = (TunerDisplay) juce::jlimit (0, 1, getInt ("tunerDisplay", 0));
    tunerMute      = getBool ("tunerMute", true);
    lite           = getBool ("engineLite", false);
    oversampling   = juce::jlimit (1, 4, getInt ("captureOversampling", 1));
    if (oversampling == 3) oversampling = 2;
    dbu           = (float) (o->hasProperty ("interfaceDbu") ? (double) o->getProperty ("interfaceDbu") : 12.0);

    eqOn      = getBool ("eqEnabled", false);
    curveName = getStr ("eqCurveName");
    if (curveName.isEmpty()) curveName = "FLAT";
    if (o->hasProperty ("eqCurve")) curve = curveFromVar (o->getProperty ("eqCurve"));

    custom.clear();
    if (auto* arr = o->getProperty ("userCurves").getArray())
        for (const auto& item : *arr)
            if (auto* c = item.getDynamicObject())
                custom.push_back ({ c->getProperty ("name").toString(),
                                    curveFromVar (c->getProperty ("bands")) });

    midiMap.clear();
    if (auto* arr = o->getProperty ("midi").getArray())
        for (const auto& item : *arr)
            if (auto* m = item.getDynamicObject())
            {
                // Nach ID, nicht nach Nummer, die Nummern hängen an der
                // Reihenfolge der Parameterliste. Zuordnungen aus Format 1 (nur
                // "param" als Nummer) entfallen.
                const auto id = m->getProperty ("paramId").toString();
                const int index = id.isEmpty() ? -1 : nmp::paramIndexFromId (id.toRawUTF8());
                if (index >= 0)
                    midiMap.push_back ({ (int) m->getProperty ("cc"),
                                         (int) m->getProperty ("channel"), index });
            }

    refreshToken = getStr ("t3kRefresh");
    userName     = getStr ("t3kUser");
}

void Settings::save()
{
    juce::DynamicObject::Ptr o (new juce::DynamicObject());

    o->setProperty ("onStart", (int) startBehaviour);
    o->setProperty ("hasRun", ranBefore);
    o->setProperty ("tunerDisplay", (int) tuner);
    o->setProperty ("tunerMute", tunerMute);
    o->setProperty ("engineLite", lite);
    o->setProperty ("captureOversampling", oversampling);
    o->setProperty ("interfaceDbu", (double) dbu);

    o->setProperty ("eqEnabled", eqOn);
    o->setProperty ("eqCurveName", curveName);
    o->setProperty ("eqCurve", curveToVar (curve));

    juce::Array<juce::var> curves;
    for (const auto& c : custom)
    {
        juce::DynamicObject::Ptr item (new juce::DynamicObject());
        item->setProperty ("name", c.name);
        item->setProperty ("bands", curveToVar (c.curve));
        curves.add (juce::var (item.get()));
    }
    o->setProperty ("userCurves", curves);

    juce::Array<juce::var> midi;
    for (const auto& m : midiMap)
    {
        juce::DynamicObject::Ptr item (new juce::DynamicObject());
        item->setProperty ("cc", m.ccNumber);
        item->setProperty ("channel", m.channel);
        item->setProperty ("paramId", juce::String (nmp::paramDesc (m.paramIndex).id));
        midi.add (juce::var (item.get()));
    }
    o->setProperty ("midi", midi);

    // Die TONE3000-Sitzung nicht mit einem alten Stand überschreiben.
    // Standalone und alle Plugin-Instanzen schreiben dieselbe Datei, und
    // TONE3000 tauscht beim Erneuern das ganze Token-Paar. Ein Prozess mit
    // altem Stand würde einen verbrauchten Refresh-Token zurückschreiben.
    // Deshalb gilt die Datei, solange dieser Prozess die Sitzung nicht selbst
    // geändert hat.
    if (! sessionChanged) reloadT3kSession();
    sessionChanged = false;

    o->setProperty ("t3kRefresh", refreshToken);
    o->setProperty ("t3kUser", userName);

    file.replaceWithText (juce::JSON::toString (juce::var (o.get()), false, 6));
}

void Settings::setEqCurve (const nmp::EqCurve& c, const juce::String& name)
{
    curve = c;
    curveName = name.isNotEmpty() ? name : juce::String ("CUSTOM");
    save();
}

void Settings::saveUserCurve (const juce::String& name, const nmp::EqCurve& c)
{
    if (name.isEmpty()) return;

    for (auto& u : custom)
        if (u.name == name) { u.curve = c; curveName = name; save(); return; }

    custom.push_back ({ name, c });
    curveName = name;
    save();
}

bool Settings::deleteUserCurve (const juce::String& name)
{
    for (auto it = custom.begin(); it != custom.end(); ++it)
        if (it->name == name)
        {
            custom.erase (it);
            if (curveName == name) curveName = "CUSTOM";
            save();
            return true;
        }
    return false;
}

void Settings::learnAssignment (int cc, int channel, int paramIndex)
{
    if (cc < 0 || cc > 127 || paramIndex < 0) return;

    // Ein Regler pro CC, ein CC pro Regler: der CC verliert seinen alten Regler
    // und der Regler seinen alten CC.
    for (auto it = midiMap.begin(); it != midiMap.end();)
    {
        if (it->paramIndex == paramIndex || (it->ccNumber == cc && it->channel == channel))
            it = midiMap.erase (it);
        else
            ++it;
    }

    midiMap.push_back ({ cc, channel, paramIndex });
    save();
}

void Settings::removeAssignment (int paramIndex)
{
    for (auto it = midiMap.begin(); it != midiMap.end();)
        if (it->paramIndex == paramIndex) it = midiMap.erase (it);
        else ++it;
    save();
}

void Settings::clearAssignments() { midiMap.clear(); save(); }

void Settings::setT3kSession (const juce::String& refresh, const juce::String& name)
{
    refreshToken = refresh;
    userName = name;
    sessionChanged = true;
    save();
}

bool Settings::reloadT3kSession()
{
    if (! file.existsAsFile()) return false;

    // Das var muss leben, solange o gelesen wird.
    const auto json = juce::JSON::parse (file);
    auto* o = json.getDynamicObject();
    if (o == nullptr) return false;

    const auto r = o->hasProperty ("t3kRefresh") ? o->getProperty ("t3kRefresh").toString() : juce::String{};
    const auto u = o->hasProperty ("t3kUser")    ? o->getProperty ("t3kUser").toString()    : juce::String{};

    if (r == refreshToken && u == userName) return false;

    refreshToken = r;
    userName     = u;
    return true;
}

void Settings::clearT3kSession()
{
    refreshToken.clear();
    userName.clear();
    sessionChanged = true;
    save();
}

} // namespace nmpl
