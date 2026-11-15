// Befehle der Brücke, Teil 2.

// Für die Geräteauswahl braucht dieser Teil JUCEs Standalone-Hülle.
// Include-Reihenfolge wie in JUCEs eigener Standalone-Übersetzungseinheit.
#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
 #include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
 #include <juce_audio_devices/juce_audio_devices.h>
 #include <juce_gui_extra/juce_gui_extra.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

#include "WebBridge.h"
#include "Resources.h"
#include <namplify/model/WavFile.h>
#include <memory>
#include <string>
#include <vector>

// Pfade unter Windows nicht aus schmalen Strings bauen, die sind dort kein
// UTF-8.
#include "../util/Pfad.h"

namespace nmpl
{

namespace
{
    juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> items)
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        for (const auto& item : items) o->setProperty (item.first, item.second);
        return juce::var (o.get());
    }

    juce::var get (const juce::var& v, const char* key)
    {
        if (auto* o = v.getDynamicObject())
            if (o->hasProperty (key)) return o->getProperty (key);
        return {};
    }

    juce::String getString (const juce::var& v, const char* key) { return get (v, key).toString(); }
    double getNumber (const juce::var& v, const char* key) { return (double) get (v, key); }
    bool   getBool (const juce::var& v, const char* key) { return (bool) get (v, key); }

    juce::var ok (juce::var payload = {})
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        o->setProperty ("ok", true);
        if (! payload.isVoid()) o->setProperty ("data", payload);
        return juce::var (o.get());
    }

    juce::var fail (const juce::String& message)
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        o->setProperty ("ok", false);
        o->setProperty ("error", message);
        return juce::var (o.get());
    }
}

// Regler

juce::var WebBridge::cmdParamSet (const juce::var& request)
{
    const auto cmd = getString (request, "cmd");
    const auto id  = getString (request, "id");
    const int index = nmp::paramIndexFromId (id.toRawUTF8());

    if (index < 0) return fail ("There is no such control: " + id);

    auto* param = proc.state().getParameter (id);
    if (param == nullptr) return fail ("There is no such control: " + id);

    const auto& d = proc.effectiveDesc (index);

    if (cmd == "param.set")
    {
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                                                    (float) getNumber (request, "unit")));
        return ok (makeObject ({
            { "unit", param->getValue() },
            { "value", d.fromUnit (param->getValue()) },
            { "text", juce::String (juce::CharPointer_UTF8 (
                  d.toText (d.fromUnit (param->getValue())).c_str())) } }));
    }

    if (cmd == "param.begin")
    {
        // Eine Ziehbewegung ist eine Geste, damit der Host bei der
        // Automationsaufnahme nicht für jeden Frame einen Punkt schreibt.
        param->beginChangeGesture();
        return ok();
    }

    if (cmd == "param.end")
    {
        param->endChangeGesture();
        return ok();
    }

    if (cmd == "param.reset")
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (d.toUnit (d.def));
        param->endChangeGesture();
        return ok (makeObject ({ { "unit", param->getValue() },
                                 { "value", d.def },
                                 { "text", juce::String (juce::CharPointer_UTF8 (d.toText (d.def).c_str())) } }));
    }

    if (cmd == "param.learn")
    {
        proc.midi().armLearn (index);
        return ok();
    }

    if (cmd == "param.learnCancel")
    {
        proc.midi().cancelLearn();
        return ok();
    }

    if (cmd == "param.forget")
    {
        proc.settings().removeAssignment (index);
        proc.midi().refreshMappings();
        proc.bumpStructure();
        return ok();
    }

    return fail ("Unbekannter Reglerbefehl: " + cmd);
}

// Slots

int WebBridge::slotFrom (const juce::var& request)
{
    const auto v = get (request, "slot");
    if (v.isVoid()) return -1;
    const int s = (int) v;
    return s >= 0 && s < nmp::kNumSlots ? s : -1;
}

namespace
{
    nmp::SlotType typeFrom (const juce::var& request)
    {
        return nmp::slotTypeFromKey (getString (request, "type").toRawUTF8());
    }

    nmp::NamRole roleFrom (const juce::var& request)
    {
        const auto key = getString (request, "role");
        return key.isEmpty() ? nmp::NamRole::amp : nmp::namRoleFromKey (key.toRawUTF8());
    }
}

juce::var WebBridge::cmdSlot (const juce::var& request, const juce::String& cmd)
{
    if (cmd == "slot.add")
    {
        const auto type = typeFrom (request);
        if (type == nmp::SlotType::empty) return fail ("There is no such type.");

        const int slot = proc.addSlot (type, roleFrom (request),
                                       (int) getNumber (request, "lane"), (int) getNumber (request, "pos"));
        if (slot < 0) return fail ("The slot could not be added.");
        return ok (makeObject ({ { "slot", slot } }));
    }

    if (cmd == "slot.place")
    {
        // Ziel für eine Datei aus dem Browser: der offene Slot, wenn er passt,
        // sonst ein passender oder ein neuer (slotForFile).
        const int placed = proc.slotForFile (typeFrom (request), roleFrom (request),
                                             get (request, "slot").isVoid() ? -1 : (int) get (request, "slot"));
        if (placed < 0) return fail ("There is no room for it.");
        return ok (makeObject ({ { "slot", placed } }));
    }

    const int slot = slotFrom (request);
    if (slot < 0) return fail ("There is no such slot.");

    if (cmd == "slot.remove") { proc.removeSlot (slot); return ok(); }
    if (cmd == "slot.move")
    {
        proc.moveSlot (slot, (int) getNumber (request, "lane"), (int) getNumber (request, "pos"));
        return ok();
    }
    if (cmd == "slot.type") { proc.setSlotType (slot, typeFrom (request)); return ok(); }
    if (cmd == "slot.role") { proc.setSlotRole (slot, roleFrom (request)); return ok(); }

    if (cmd == "slot.clear")
    {
        proc.clearSlot (slot);
        return ok();
    }

    if (cmd == "slot.load")
    {
        const auto relative = getString (request, "path");
        const auto file = proc.library().root().getChildFile (relative);

        if (! file.existsAsFile()) return fail ("That file no longer exists.");

        SlotFile info;
        info.relativePath = relative;
        info.bankTitle   = getString (request, "bank");
        info.variantName = getString (request, "variant");
        info.gear        = getString (request, "gear");
        // Dateiname des Bildes aus der Bibliothek, die UI schickt ihn nicht
        // mit.
        info.imageFile   = proc.library().imageForFile (file).getFileName();

        proc.loadIntoSlot (slot, file, info);
        return ok();
    }

    return fail ("Unknown slot command: " + cmd);
}

// Bibliothek

juce::var WebBridge::buildLibrary (nmp::SlotType type, nmp::NamRole role) const
{
    juce::Array<juce::var> banks;

    for (const auto* bank : proc.library().banksFor (type, role))
    {
        juce::Array<juce::var> variants;
        for (const auto& v : bank->variants)
            variants.add (makeObject ({
                { "name", v.name },
                { "size", v.size },
                { "path", v.file.getRelativePathFrom (proc.library().root()) },
                { "modelId", v.modelId } }));

        const auto image = libImageUrl (bank->image);

        banks.add (makeObject ({
            { "title", bank->title },
            { "author", bank->author },
            { "gear", bank->gear },
            { "toneId", bank->toneId },
            { "image", image },
            { "path", bank->dir.getRelativePathFrom (proc.library().root()) },
            { "variants", variants } }));
    }

    return banks;
}

juce::var WebBridge::cmdLibrary (const juce::var& request, const juce::String& cmd)
{
    if (cmd == "library.list")
    {
        // Nach Slot (die Karte fragt, was passt) oder nach Typ und Rolle (der
        // Browser zeigt eine Abteilung).
        const int slot = slotFrom (request);
        if (slot >= 0)
        {
            const auto& l = proc.rig().layout;
            return ok (buildLibrary (l.type[(size_t) slot], l.role[(size_t) slot]));
        }
        return ok (buildLibrary (typeFrom (request), roleFrom (request)));
    }

    if (cmd == "library.reveal")
    {
        proc.library().root().revealToUser();
        return ok (makeObject ({ { "path", proc.library().root().getFullPathName() } }));
    }

    if (cmd == "library.deleteBank")
    {
        const auto dir = proc.library().root().getChildFile (getString (request, "path"));
        juce::String error;
        if (! proc.library().deleteBank (dir, error)) return fail (error);
        proc.bumpStructure();
        return ok();
    }

    if (cmd == "library.deleteVariant")
    {
        const auto file = proc.library().root().getChildFile (getString (request, "path"));
        juce::String error;
        if (! proc.library().deleteVariant (file, error)) return fail (error);
        proc.bumpStructure();
        return ok();
    }

    if (cmd == "library.import")
    {
        const int slot = slotFrom (request);
        if (slot < 0) return fail ("There is no such slot.");

        const auto type = proc.rig().layout.type[(size_t) slot];
        const auto role = proc.rig().layout.role[(size_t) slot];
        if (! slotTakesFiles (type)) return fail ("This slot does not take a file.");

        const bool wantsIr = type == nmp::SlotType::ir;
        const juce::String filter = wantsIr ? "*.wav"
                                  : (role == nmp::NamRole::outboard || role == nmp::NamRole::space ? "*.nam;*.wav" : "*.nam");

        chooser = std::make_unique<juce::FileChooser> (
            wantsIr ? "Choose impulse response" : "Choose capture",
            juce::File::getSpecialLocation (juce::File::userHomeDirectory), filter);

        auto* self = this;
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
            [self, slot, type, role] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File{}) return;

                const auto folder = type == nmp::SlotType::ir        ? LibraryFolder::irs
                                  : role == nmp::NamRole::pedal      ? LibraryFolder::pedals
                                  : role == nmp::NamRole::outboard
                                    || role == nmp::NamRole::space   ? LibraryFolder::outboard
                                                                     : LibraryFolder::models;

                juce::String error;
                const auto* bank = self->proc.library().importFile (file, folder, error,
                    type == nmp::SlotType::nam && role == nmp::NamRole::space ? juce::String ("space") : juce::String());

                if (bank == nullptr)
                {
                    // Falscher Dateityp wird gemeldet.
                    self->proc.notify (error, "error");
                    return;
                }

                SlotFile info;
                info.relativePath = bank->variants.front().file
                                        .getRelativePathFrom (self->proc.library().root());
                info.bankTitle   = bank->title;
                info.variantName = bank->variants.front().name;
                // Eine eigene Datei bekommt die Rolle des Slots, auf den sie
                // gelegt wurde.
                info.gear        = type == nmp::SlotType::ir ? juce::String ("cab")
                                 : role == nmp::NamRole::fullrig ? juce::String ("amp-cab")
                                 : juce::String (nmp::namRoleKey (role));

                self->proc.loadIntoSlot (slot, bank->variants.front().file, info);
                self->proc.bumpStructure();
            });

        return ok();
    }

    return fail ("Unbekannter Bibliotheksbefehl: " + cmd);
}

// Presets

juce::var WebBridge::cmdPreset (const juce::var& request, const juce::String& cmd)
{
    if (cmd == "preset.list")   return ok (buildPresets());
    if (cmd == "preset.load")   { proc.loadPreset (getString (request, "name")); return ok(); }
    if (cmd == "preset.save")
    {
        // Angespielte Captures kommen beim Speichern in die Bibliothek, damit
        // das Preset nicht auf den Audition-Ordner zeigt.
        juce::Array<juce::var> kept;
        int notKept = 0;
        for (int s = 0; s < nmp::kNumSlots; ++s)
        {
            if (! isPreview (s)) continue;
            juce::String error;
            const auto info = keepPreview (s, error);
            if (info.isVoid()) ++notKept;
            else               kept.add (info);
        }

        proc.savePreset (getString (request, "name"));
        return ok (makeObject ({ { "kept", kept }, { "notKept", notKept } }));
    }
    if (cmd == "preset.delete") { proc.deletePreset (getString (request, "name")); return ok(); }
    if (cmd == "preset.step")   { proc.stepPreset ((int) getNumber (request, "dir")); return ok(); }
    if (cmd == "preset.reset")  { proc.resetRig(); return ok(); }

    return fail ("Unbekannter Presetbefehl: " + cmd);
}

// Looper und Recorder

juce::var WebBridge::cmdLooper (const juce::var& request, const juce::String& cmd)
{
    auto& looper = proc.engine().looper();
    const int track = juce::jlimit (0, nmp::Looper::kTracks - 1, (int) getNumber (request, "track"));

    if (cmd == "looper.tap")    { looper.tap (track); return ok(); }
    if (cmd == "looper.erase")  { looper.erase (track); return ok(); }
    if (cmd == "looper.eraseAll") { looper.eraseAll(); return ok(); }

    if (cmd == "looper.volume")
    {
        const float v = juce::jlimit (0.0f, 1.0f, (float) getNumber (request, "value"));
        looper.setTrackVolume (track, v);
        proc.rig().looperVolumes[(size_t) track] = v;
        return ok();
    }

    if (cmd == "looper.edges")
    {
        looper.setLoopEdges (track, (int) getNumber (request, "start"),
                                    (int) getNumber (request, "end"));
        return ok();
    }

    if (cmd == "looper.waveform")
    {
        const int points = juce::jlimit (16, 2048, (int) getNumber (request, "points"));
        std::vector<float> data ((size_t) points, 0.0f);
        looper.waveform (track, data.data(), points);

        juce::Array<juce::var> out;
        for (float v : data) out.add (v);

        const auto info = looper.trackInfo (track);
        return ok (makeObject ({ { "peaks", out },
                                 { "rawLength", info.rawLength },
                                 { "loopStart", info.loopStart },
                                 { "loopEnd", info.loopEnd },
                                 { "cycle", looper.cycleSamples() },
                                 { "sampleRate", proc.engine().sampleRate() } }));
    }

    return fail ("Unbekannter Looperbefehl: " + cmd);
}

juce::var WebBridge::cmdRecorder (const juce::var& request, const juce::String& cmd)
{
    auto& rec = proc.engine().recorder();

    if (cmd == "recorder.tap")   { rec.tap(); return ok(); }
    if (cmd == "recorder.erase") { rec.erase(); return ok(); }

    if (cmd == "recorder.waveform")
    {
        const int points = juce::jlimit (16, 2048, (int) getNumber (request, "points"));
        std::vector<float> data ((size_t) points, 0.0f);
        rec.waveform (data.data(), points);

        juce::Array<juce::var> out;
        for (float v : data) out.add (v);
        return ok (makeObject ({ { "peaks", out }, { "seconds", rec.lengthSeconds() } }));
    }

    if (cmd == "recorder.save")
    {
        if (! rec.hasTake()) return fail ("There is no take on the tape.");

        const auto name = juce::Time::getCurrentTime().formatted ("NAMplify %Y-%m-%d %H.%M.%S");
        const auto file = proc.library().recordingsDir().getChildFile (name + ".wav");

        std::string error;
        if (! rec.save (toPath (file), error))
            return fail (juce::String (juce::CharPointer_UTF8 (error.c_str())));

        proc.notify ("Take saved: " + file.getFileName());
        return ok (makeObject ({ { "path", file.getFullPathName() } }));
    }

    return fail ("Unbekannter Recorderbefehl: " + cmd);
}

// Einstellungen

juce::var WebBridge::cmdSettings (const juce::var& request, const juce::String& cmd)
{
    auto& s = proc.settings();

    if (cmd == "settings.get") return ok (buildSettings());

    if (cmd == "settings.set")
    {
        auto* o = request.getDynamicObject();
        if (o == nullptr) return fail ("Nichts zu setzen.");

        if (o->hasProperty ("onStart"))
            s.setOnStart ((OnStart) juce::jlimit (0, 2, (int) getNumber (request, "onStart")));


        if (o->hasProperty ("tunerDisplay"))
            s.setTunerDisplay ((TunerDisplay) juce::jlimit (0, 1,
                                (int) getNumber (request, "tunerDisplay")));

        if (o->hasProperty ("tunerMute")) s.setTunerMutePreference (getBool (request, "tunerMute"));

        if (o->hasProperty ("interfaceDbu"))
        {
            s.setInterfaceDbu ((float) getNumber (request, "interfaceDbu"));
            proc.engine().setInterfaceDbu (s.interfaceDbu());
            // Der Referenzpegel geht in die Messung ein, also neu messen.
            proc.loader().requestReload();
        }

        proc.bumpStructure();
        return ok (buildSettings());
    }

    if (cmd == "settings.midiClear")
    {
        s.clearAssignments();
        proc.midi().refreshMappings();
        proc.bumpStructure();
        return ok();
    }

    if (cmd == "settings.checkUpdate")
    {
        // Ohne veröffentlichte Versionsliste gibt es noch nichts zu
        // vergleichen.
        return ok (makeObject ({ { "result", "none-published" },
                                 { "version", juce::String (NAMPLIFY_VERSION_LABEL) } }));
    }

    return fail ("Unbekannter Einstellungsbefehl: " + cmd);
}

// Globaler EQ

juce::var WebBridge::cmdEq (const juce::var& request, const juce::String& cmd)
{
    auto& s = proc.settings();
    auto& eq = proc.engine().globalEq();

    if (cmd == "eq.enable")
    {
        const bool on = getBool (request, "on");
        s.setEqEnabled (on);
        eq.setEnabled (on);
        return ok();
    }

    if (cmd == "eq.band")
    {
        const int index = juce::jlimit (0, nmp::EqCurve::kBands - 1, (int) getNumber (request, "band"));
        auto curve = s.eqCurve();
        auto& b = curve.bands[index];

        auto* o = request.getDynamicObject();
        if (o != nullptr)
        {
            if (o->hasProperty ("freq"))  b.freq   = juce::jlimit (20.0f, 20000.0f, (float) getNumber (request, "freq"));
            if (o->hasProperty ("gain"))  b.gainDb = juce::jlimit (-24.0f, 24.0f, (float) getNumber (request, "gain"));
            if (o->hasProperty ("q"))     b.q      = juce::jlimit (0.1f, 12.0f, (float) getNumber (request, "q"));
            if (o->hasProperty ("shape")) b.shape  = (nmp::FilterShape) juce::jlimit (
                                                        0, (int) nmp::FilterShape::count - 1,
                                                        (int) getNumber (request, "shape"));
        }

        // Sobald die Kurve verändert wird, heißt sie wieder CUSTOM.
        s.setEqCurve (curve, "CUSTOM");
        eq.setCurve (curve);
        return ok (buildSettings());
    }

    if (cmd == "eq.select")
    {
        const auto name = getString (request, "name");

        if (const auto* factory = nmp::findFactoryCurve (name.toStdString()))
        {
            s.setEqCurve (factory->curve, name);
            eq.setCurve (factory->curve);
        }
        else
        {
            bool found = false;
            for (const auto& c : s.userCurves())
                if (c.name == name) { s.setEqCurve (c.curve, name); eq.setCurve (c.curve); found = true; break; }
            if (! found) return fail ("There is no such curve.");
        }

        // Eine Kurve wählen schaltet den EQ ein.
        s.setEqEnabled (true);
        eq.setEnabled (true);

        proc.notify ("EQ curve: " + name);
        proc.bumpStructure();
        return ok (buildSettings());
    }

    if (cmd == "eq.save")
    {
        const auto name = getString (request, "name").trim();
        if (name.isEmpty()) return fail ("The curve needs a name.");
        if (nmp::findFactoryCurve (name.toStdString()) != nullptr)
            return fail ("A factory curve already carries that name.");

        s.saveUserCurve (name, s.eqCurve());
        proc.notify ("EQ curve saved: " + name);
        proc.bumpStructure();
        return ok (buildSettings());
    }

    if (cmd == "eq.delete")
    {
        const auto name = getString (request, "name");
        if (! s.deleteUserCurve (name)) return fail ("There is no such curve.");
        proc.notify ("EQ curve deleted: " + name);
        proc.bumpStructure();
        return ok (buildSettings());
    }

    if (cmd == "eq.response")
    {
        // Frequenzgang aus dem Kern, die UI zeichnet genau diese Werte.
        juce::Array<juce::var> out;
        if (auto* arr = get (request, "freqs").getArray())
            for (const auto& f : *arr)
                out.add (eq.magnitudeDbAt ((double) f));

        return ok (makeObject ({ { "db", out } }));
    }

    return fail ("Unbekannter EQ-Befehl: " + cmd);
}

// Audiogerät (nur Standalone)

juce::var WebBridge::buildAudioDevices() const
{
   #if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr)
        return makeObject ({ { "available", false } });

    auto& dm = holder->deviceManager;
    auto* type = dm.getCurrentDeviceTypeObject();
    auto* device = dm.getCurrentAudioDevice();

    // Die aktuelle Auswahl aus dem Setup lesen, nicht aus dem Gerätenamen.
    // getName() liefert einen Namen für das offene Gerät, der nur auf eine der
    // beiden Listen (Eingang oder Ausgang) passt.
    const auto setup = dm.getAudioDeviceSetup();
    const auto namenOder = [device] (const juce::String& ausSetup)
    {
        // Leer heißt Standardgerät, dann stimmt der Gerätename.
        return ausSetup.isNotEmpty() ? ausSetup
             : (device != nullptr ? device->getName() : juce::String());
    };

    auto toVar = [] (const juce::StringArray& a)
    {
        juce::Array<juce::var> out;
        for (const auto& s : a) out.add (s);
        return juce::var (out);
    };

    juce::Array<juce::var> rates, sizes;
    if (device != nullptr)
    {
        for (double r : device->getAvailableSampleRates()) rates.add (r);
        for (int b : device->getAvailableBufferSizes()) sizes.add (b);
    }

    juce::Array<juce::var> types;
    for (auto* t : dm.getAvailableDeviceTypes()) types.add (t->getTypeName());

    // Eingangskanäle des Geräts mit der aktuellen Auswahl: ein aktiver Kanal
    // geht auf beide Seiten, zwei sind Stereo.
    juce::Array<juce::var> channels;
    if (device != nullptr)
    {
        const auto names  = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i)
            channels.add (makeObject ({ { "index", i },
                                        { "name", names[i].isNotEmpty() ? names[i] : "Input " + juce::String (i + 1) },
                                        { "active", active[i] } }));
    }

    // Auswahlfelder nur, wenn es etwas auszuwählen gibt.
    return makeObject ({
        { "available", true },
        { "types", types },
        { "currentType", type != nullptr ? type->getTypeName() : juce::String() },
        // Ausgang vor Eingang, wie im Systemdialog.
        { "outputs", type != nullptr ? toVar (type->getDeviceNames (false)) : juce::var() },
        { "inputs",  type != nullptr ? toVar (type->getDeviceNames (true))  : juce::var() },
        { "currentOutput", namenOder (setup.outputDeviceName) },
        { "currentInput",  namenOder (setup.inputDeviceName) },
        { "sampleRates", rates },
        { "bufferSizes", sizes },
        { "currentRate", device != nullptr ? device->getCurrentSampleRate() : 0.0 },
        { "currentBuffer", device != nullptr ? device->getCurrentBufferSizeSamples() : 0 },
        { "inputChannels", channels },
        // ASIO und manche andere Treiber haben ein eigenes Bedienfeld.
        { "hasControlPanel", device != nullptr && device->hasControlPanel() },
        { "midiInputs", [&dm]
            {
                juce::Array<juce::var> out;
                for (const auto& d : juce::MidiInput::getAvailableDevices())
                    out.add (makeObject ({ { "name", d.name },
                                           { "id", d.identifier },
                                           { "enabled", dm.isMidiInputDeviceEnabled (d.identifier) } }));
                return juce::var (out);
            }() },
        { "canPairBluetooth", juce::BluetoothMidiDevicePairingDialogue::isAvailable() } });
   #else
    return makeObject ({ { "available", false } });
   #endif
}

void WebBridge::setInputMetering (bool on)
{
   #if JucePlugin_Build_Standalone
    if (on == inputMetering) return;
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr) { inputMetering = false; return; }

    if (on) holder->deviceManager.addAudioCallback (&inputTap);
    else    holder->deviceManager.removeAudioCallback (&inputTap);
    inputMetering = on;
   #else
    juce::ignoreUnused (on);
   #endif
}

juce::var WebBridge::cmdAudio (const juce::var& request, const juce::String& cmd)
{
   #if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr) return ok (buildAudioDevices());

    auto& dm = holder->deviceManager;

    if (cmd == "audio.devices") return ok (buildAudioDevices());

    if (cmd == "audio.set")
    {
        auto setup = dm.getAudioDeviceSetup();
        auto* o = request.getDynamicObject();

        if (o != nullptr)
        {
            if (o->hasProperty ("output"))     setup.outputDeviceName = getString (request, "output");
            if (o->hasProperty ("input"))      setup.inputDeviceName  = getString (request, "input");
            if (o->hasProperty ("sampleRate")) setup.sampleRate = getNumber (request, "sampleRate");
            if (o->hasProperty ("bufferSize")) setup.bufferSize = (int) getNumber (request, "bufferSize");
        }

        const auto error = dm.setAudioDeviceSetup (setup, true);
        if (error.isNotEmpty()) return fail (error);

        return ok (buildAudioDevices());
    }

    if (cmd == "audio.inputChannels")
    {
        // Ein oder zwei Kanäle (die Kette ist stereo), keiner heißt kein
        // Eingang.
        juce::BigInteger mask;
        if (auto* arr = get (request, "channels").getArray())
            for (const auto& c : *arr) mask.setBit ((int) c);

        const int count = mask.countNumberOfSetBits();
        if (count < 1 || count > 2) return fail ("Pick one input for mono or two for stereo.");

        auto setup = dm.getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.inputChannels = mask;

        const auto error = dm.setAudioDeviceSetup (setup, true);
        if (error.isNotEmpty()) return fail (error);

        // Der Abgriff zählt seine Kanäle beim Gerätestart neu.
        if (inputMetering) { setInputMetering (false); setInputMetering (true); }
        return ok (buildAudioDevices());
    }

    if (cmd == "audio.meter")
    {
        setInputMetering (getBool (request, "on"));
        return ok();
    }

    if (cmd == "audio.levels")
    {
        juce::Array<juce::var> levels;
        if (auto* device = dm.getCurrentAudioDevice())
            for (int i = 0; i < device->getInputChannelNames().size(); ++i)
                levels.add (inputTap.takeDb (i));
        return ok (makeObject ({ { "levels", levels } }));
    }

    if (cmd == "audio.controlPanel")
    {
        auto* device = dm.getCurrentAudioDevice();
        if (device == nullptr || ! device->hasControlPanel())
            return fail ("This driver has no control panel.");

        // Das Bedienfeld kann das Gerät umstellen, danach neu öffnen wie JUCEs
        // eigener Dialog.
        if (device->showControlPanel())
        {
            dm.closeAudioDevice();
            dm.restartLastAudioDevice();
        }
        return ok (buildAudioDevices());
    }

    if (cmd == "audio.type")
    {
        dm.setCurrentAudioDeviceType (getString (request, "type"), true);
        return ok (buildAudioDevices());
    }

    if (cmd == "audio.midiInput")
    {
        dm.setMidiInputDeviceEnabled (getString (request, "id"), getBool (request, "enabled"));
        return ok (buildAudioDevices());
    }

    if (cmd == "audio.pairBluetooth")
    {
        if (! juce::BluetoothMidiDevicePairingDialogue::isAvailable())
            return fail ("This system cannot do that.");
        juce::BluetoothMidiDevicePairingDialogue::open();
        return ok();
    }

    if (cmd == "audio.systemDialog")
    {
        // JUCEs Dialog bleibt erreichbar, er bietet die Wahl des Treibertyps.
        holder->showAudioSettingsDialog();
        return ok();
    }

    return fail ("Unbekannter Audiobefehl: " + cmd);
   #else
    juce::ignoreUnused (request, cmd);
    return ok (buildAudioDevices());
   #endif
}

} // namespace nmpl
