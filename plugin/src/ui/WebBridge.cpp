#include "WebBridge.h"
#include "Resources.h"
#include <namplify/blocks/Drive.h>
#include <namplify/blocks/Reverb.h>

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#endif

namespace nmpl
{

namespace
{
    juce::DynamicObject* obj (juce::var& v)
    {
        if (auto* o = v.getDynamicObject()) return o;
        auto* o = new juce::DynamicObject();
        v = juce::var (o);
        return o;
    }

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

    /// Beschreibung eines Reglers für die UI, gleiche Form für feste Parameter
    /// und Slot-Regler.
    juce::var describe (const nmp::ParamDesc& d)
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        o->setProperty ("id", juce::String (d.id));
        o->setProperty ("name", juce::String (juce::CharPointer_UTF8 (d.name)));
        o->setProperty ("kind", d.kind == nmp::ParamKind::boolean ? "bool"
                              : d.kind == nmp::ParamKind::choice ? "choice"
                              : d.kind == nmp::ParamKind::logarithmic ? "log" : "linear");
        o->setProperty ("min", d.min);
        o->setProperty ("max", d.max);
        o->setProperty ("def", d.def);
        o->setProperty ("unit", juce::String (d.unit));

        if (d.kind == nmp::ParamKind::choice)
        {
            juce::Array<juce::var> choices;
            for (int c = 0; c < d.numChoices; ++c)
                choices.add (juce::String (juce::CharPointer_UTF8 (d.choices[c])));
            o->setProperty ("choices", choices);
        }
        return juce::var (o.get());
    }

    juce::String dataUri (const juce::MemoryBlock& block, const juce::String& mime)
    {
        return "data:" + mime + ";base64,"
             + juce::Base64::toBase64 (block.getData(), block.getSize());
    }
}

WebBridge::WebBridge (NamplifyProcessor& p, Emit e) : proc (p), emit (std::move (e))
{
    startTimerHz (30);
}

WebBridge::~WebBridge()
{
    stopTimer();
    setInputMetering (false);
    proc.setTunerWatched (false);
}

void WebBridge::setVisible (bool v)
{
    visible = v;
    if (visible) lastStructure = -1;   // beim Sichtbarwerden neu einlesen
    proc.setTunerWatched (visible && miniTuner);
}

juce::String WebBridge::libImageUrl (const juce::File& image) const
{
    if (image == juce::File{}) return {};

    const auto root = proc.library().root();
    if (! image.isAChildOf (root)) return {};

    // Pfadteile einzeln kodieren. addEscapeChars über den ganzen Pfad würde
    // jeden Schrägstrich zu %2F machen.
    juce::StringArray parts;
    parts.addTokens (image.getRelativePathFrom (root), "/", "");

    juce::StringArray escaped;
    for (const auto& part : parts)
        escaped.add (juce::URL::addEscapeChars (part, false));

    return "lib-image/" + escaped.joinIntoString ("/");
}

juce::var WebBridge::buildParamDefs() const
{
    juce::Array<juce::var> out;
    const int first = (int) nmp::ParamId::firstSlotParam;

    for (int i = 0; i < nmp::kNumParams; ++i)
    {
        auto v = describe (nmp::paramDesc (i));
        auto* o = v.getDynamicObject();
        o->setProperty ("index", i);

        // Slot-Regler geben ihren Slot und ihre Position an (k, -1 = An/Aus).
        // Die Bedeutung steht beim Typ (types[].controls).
        if (i >= first)
        {
            o->setProperty ("slot", (i - first) / nmp::kParamsPerSlot);
            o->setProperty ("k", (i - first) % nmp::kParamsPerSlot - 1);
        }
        out.add (v);
    }

    return out;
}

juce::var WebBridge::buildParams() const
{
    juce::DynamicObject::Ptr o (new juce::DynamicObject());

    for (int i = 0; i < nmp::kNumParams; ++i)
    {
        const auto* d = &proc.effectiveDesc (i);
        const char* key = nmp::paramDesc (i).id;
        auto* param = proc.state().getParameter (key);
        const float unit = param != nullptr ? param->getValue() : d->toUnit (d->def);

        const float real = d->fromUnit (unit);
        o->setProperty (key, makeObject ({
            { "unit", unit },
            { "value", real },
            { "text", juce::String (juce::CharPointer_UTF8 (d->toText (real).c_str())) } }));
    }

    return juce::var (o.get());
}

juce::var WebBridge::buildTypes() const
{
    const auto& layout = proc.rig().layout;
    juce::Array<juce::var> out;

    for (int t = 1; t < nmp::kNumSlotTypes; ++t)
    {
        const auto type = (nmp::SlotType) t;

        juce::Array<juce::var> controls;
        for (int k = 0; k < nmp::slotControlCount (type); ++k)
        {
            auto c = describe (nmp::slotControl (type, k));
            if (type == nmp::SlotType::nam)
            {
                // Rollen, die den Regler zeigen (aus namControlShown).
                juce::Array<juce::var> roles;
                for (int r = 0; r < nmp::kNumNamRoles; ++r)
                    if (nmp::namControlShown ((nmp::NamRole) r, k))
                        roles.add (juce::String (nmp::namRoleKey ((nmp::NamRole) r)));
                c.getDynamicObject()->setProperty ("roles", roles);
            }
            controls.add (c);
        }

        out.add (makeObject ({
            { "key", juce::String (nmp::slotTypeKey (type)) },
            { "name", juce::String (nmp::slotTypeName (type)) },
            { "colour", juce::String (nmp::slotColour (type, nmp::NamRole::amp)) },
            { "limit", nmp::slotTypeLimit (type) },
            { "used", layout.count (type) },
            { "takesFiles", slotTakesFiles (type) },
            { "controls", controls } }));
    }

    return out;
}

juce::var WebBridge::buildRoles() const
{
    juce::Array<juce::var> out;
    for (int r = 0; r < nmp::kNumNamRoles; ++r)
    {
        const auto role = (nmp::NamRole) r;
        out.add (makeObject ({
            { "key", juce::String (nmp::namRoleKey (role)) },
            { "name", juce::String (nmp::namRoleName (role)) },
            { "colour", juce::String (nmp::slotColour (nmp::SlotType::nam, role)) } }));
    }
    return out;
}

juce::var WebBridge::buildTools() const
{
    juce::Array<juce::var> out;
    for (int t = 0; t < nmp::kNumTools; ++t)
    {
        const auto tool = (nmp::Tool) t;
        out.add (makeObject ({
            { "key", juce::String (nmp::toolKey (tool)) },
            { "name", juce::String (nmp::toolName (tool)) },
            { "colour", juce::String (nmp::toolColour (tool)) },
            { "shown", proc.rig().tools[(size_t) t] } }));
    }
    return out;
}

juce::var WebBridge::buildSlots() const
{
    const auto& rig = proc.rig();
    const auto& layout = rig.layout;
    juce::Array<juce::var> out;

    for (int s = 0; s < nmp::kNumSlots; ++s)
    {
        const auto type = layout.type[(size_t) s];
        const auto role = layout.role[(size_t) s];

        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        o->setProperty ("slot", s);
        o->setProperty ("type", juce::String (nmp::slotTypeKey (type)));

        if (type == nmp::SlotType::empty) { out.add (juce::var (o.get())); continue; }

        int lane = 0, pos = 0;
        layout.find (s, lane, pos);
        o->setProperty ("lane", lane);
        o->setProperty ("pos", pos);
        o->setProperty ("colour", juce::String (nmp::slotColour (type, role)));
        o->setProperty ("onParam", juce::String (nmp::paramDesc (nmp::slotOnParam (s)).id));

        juce::Array<juce::var> ctlIds;
        for (int k = 0; k < nmp::slotControlCount (type); ++k)
            ctlIds.add (juce::String (nmp::paramDesc (nmp::slotControlParam (s, k)).id));
        o->setProperty ("params", ctlIds);

        // Name auf der Kachel: bei NAM die Rolle, sonst der Typ.
        if (type == nmp::SlotType::nam)
        {
            o->setProperty ("role", juce::String (nmp::namRoleKey (role)));
            o->setProperty ("label", juce::String (nmp::namRoleName (role)));
        }
        else
            o->setProperty ("label", juce::String (nmp::slotTypeName (type)));

        if (slotTakesFiles (type))
        {
            const auto& f = rig.files[(size_t) s];
            juce::DynamicObject::Ptr sv (new juce::DynamicObject());
            sv->setProperty ("bank", f.bankTitle);
            sv->setProperty ("variant", f.variantName);
            sv->setProperty ("gear", f.gear);
            sv->setProperty ("empty", f.isEmpty());

            // Das Bild kommt aus der Bibliothek. Der Slot kennt nur die Datei,
            // das Bild der Bank kann später ankommen (beim Download von
            // TONE3000 der Normalfall).
            if (! f.isEmpty())
            {
                const auto file  = proc.library().root().getChildFile (f.relativePath);
                sv->setProperty ("image", libImageUrl (proc.library().imageForFile (file)));
                // Angespielt und noch nicht in der Bibliothek: die Kachel zeigt
                // PREVIEW.
                sv->setProperty ("preview", file.isAChildOf (proc.library().root().getChildFile ("Audition")));

                // Für INFO auf der Karte: Autor und Link.
                if (const auto* bank = proc.library().bankForFile (file))
                {
                    sv->setProperty ("author", bank->author);
                    sv->setProperty ("toneId", bank->toneId);
                    sv->setProperty ("page", bank->page);
                }
            }

            if (type == nmp::SlotType::nam)
                if (auto* nam = proc.engine().namFor (s))
                    if (const auto* info = nam->modelInfo())
                        sv->setProperty ("model", makeObject ({
                            { "architecture", juce::String (info->architecture) },
                            { "sampleRate", info->fileSampleRate },
                            { "modelRate", info->modelSampleRate },
                            { "resampling", info->resampling },
                            { "hasLite", info->hasLite },
                            { "oversampling", info->oversampling },
                            { "latency", info->latencySamples },
                            { "measured", info->measurement.valid },
                            { "measuredDb", info->measurement.measuredDb },
                            { "trimDb", info->measurement.trimDb },
                            { "fileLoudness", info->measurement.fileHadLoudness },
                            { "fileLoudnessDb", info->measurement.fileLoudnessDb },
                            { "hasInputLevel", info->hasInputLevel },
                            { "inputLevelDbu", info->inputLevelDbu } }));

            o->setProperty ("file", juce::var (sv.get()));
        }

        out.add (juce::var (o.get()));
    }

    return out;
}

juce::var WebBridge::buildLanes() const
{
    const auto& layout = proc.rig().layout;
    juce::Array<juce::var> lanes;
    for (int l = 0; l < nmp::kNumLanes; ++l)
    {
        juce::Array<juce::var> lane;
        for (int p = 0; p < layout.length[(size_t) l]; ++p)
            lane.add ((int) layout.lane[(size_t) l][(size_t) p]);
        lanes.add (lane);
    }
    return lanes;
}

juce::var WebBridge::buildPresets() const
{
    juce::Array<juce::var> list;
    for (const auto& p : proc.presets().presets()) list.add (p.name);

    return makeObject ({ { "list", list },
                         { "current", proc.presets().currentName() } });
}

juce::var WebBridge::buildSettings() const
{
    auto& s = proc.settings();

    juce::Array<juce::var> curves;
    for (int i = 0; i < nmp::numFactoryCurves(); ++i)
        curves.add (makeObject ({
            { "name", juce::String (nmp::factoryCurve (i).name) },
            { "description", juce::String (juce::CharPointer_UTF8 (nmp::factoryCurve (i).description)) },
            { "factory", true } }));

    for (const auto& c : s.userCurves())
        curves.add (makeObject ({ { "name", c.name }, { "description", juce::var() },
                                  { "factory", false } }));

    juce::Array<juce::var> bands;
    const auto& curve = s.eqCurve();
    for (int i = 0; i < nmp::EqCurve::kBands; ++i)
        bands.add (makeObject ({
            { "freq", curve.bands[i].freq },
            { "gain", curve.bands[i].gainDb },
            { "q", curve.bands[i].q },
            { "shape", (int) curve.bands[i].shape },
            { "shapeName", juce::String (nmp::filterShapeName (curve.bands[i].shape)) } }));

    juce::Array<juce::var> assignments;
    for (const auto& a : s.assignments())
    {
        if (a.paramIndex < 0 || a.paramIndex >= nmp::kNumParams) continue;
        assignments.add (makeObject ({
            { "cc", a.ccNumber },
            { "channel", a.channel },
            { "param", juce::String (nmp::paramDesc (a.paramIndex).id) },
            { "paramName", juce::String (juce::CharPointer_UTF8 (
                  nmp::paramDesc (a.paramIndex).name)) } }));
    }

    return makeObject ({
        { "onStart", (int) s.onStart() },
        { "tunerDisplay", (int) s.tunerDisplay() },
        { "interfaceDbu", s.interfaceDbu() },
        { "engineLite", s.engineLite() },
        { "captureOversampling", s.captureOversampling() },
        { "eqEnabled", s.eqEnabled() },
        { "eqCurveName", s.eqCurveName() },
        { "eqCurves", curves },
        { "eqBands", bands },
        { "midiAssignments", assignments },
        { "libraryPath", proc.library().root().getFullPathName() },
        { "libraryFolderName", proc.library().root().getFileName() },
        { "version", juce::String (NAMPLIFY_VERSION_LABEL) } });
}

juce::var WebBridge::buildEngineInfo() const
{
    auto& e = proc.engine();

    juce::Array<juce::var> drives;
    for (int i = 0; i < (int) nmp::DriveModel::count; ++i)
    {
        const auto d = nmp::driveDefaults ((nmp::DriveModel) i);
        drives.add (makeObject ({ { "name", juce::String (nmp::driveModelName ((nmp::DriveModel) i)) },
                                  { "gain", d.gainDb }, { "level", d.levelDb },
                                  { "tone", d.tone }, { "body", d.body } }));
    }

    juce::Array<juce::var> reverbs;
    for (int i = 0; i < (int) nmp::ReverbType::count; ++i)
    {
        const auto d = nmp::reverbDefaults ((nmp::ReverbType) i);
        reverbs.add (makeObject ({ { "name", juce::String (nmp::reverbTypeName ((nmp::ReverbType) i)) },
                                   { "time", d.timeSec }, { "size", d.size },
                                   { "boing", d.boing }, { "tone", d.tone }, { "mix", d.mix } }));
    }

    return makeObject ({
        { "sampleRate", e.sampleRate() },
        { "blockSize", e.maxBlockSize() },
        { "latency", e.latencySamples() },
        { "lite", e.engineLite() },
        { "anyLite", e.anyModelHasLite() },
        { "standalone", proc.isStandalone() },
        // Werkseinstellungen der Drive- und Reverb-Typen kommen vom Plugin.
        { "driveDefaults", drives },
        { "reverbDefaults", reverbs } });
}

juce::var WebBridge::buildSync()
{
    lastStructure = proc.structureVersion();

    return makeObject ({
        { "paramDefs", buildParamDefs() },
        { "params", buildParams() },
        { "slots", buildSlots() },
        { "lanes", buildLanes() },
        { "types", buildTypes() },
        { "roles", buildRoles() },
        { "tools", buildTools() },
        { "limits", makeObject ({ { "slots", nmp::kNumSlots },
                                  { "laneSlots", nmp::kMaxLaneSlots },
                                  { "lanes", nmp::kNumLanes } }) },
        { "zoom", proc.rig().zoom },
        { "presets", buildPresets() },
        { "settings", buildSettings() },
        { "engine", buildEngineInfo() },
        { "t3k", makeObject ({ { "hasKey", Tone3000::hasClientId() },
                               { "connected", proc.tone3000().isConnected() },
                               { "user", proc.tone3000().userName() } }) },
        { "firstRun", proc.isFirstRun() },
        { "structure", lastStructure } });
}

juce::var WebBridge::buildMeters()
{
    auto& e = proc.engine();

    juce::DynamicObject::Ptr o (new juce::DynamicObject());
    o->setProperty ("in", e.inputLevel());
    o->setProperty ("out", e.outputLevel());
    o->setProperty ("cpu", e.cpuLoad());
    o->setProperty ("muted", proc.isMuted());
    o->setProperty ("latency", e.latencySamples());

    // Stimmgerät
    const auto r = e.tuner().reading();
    o->setProperty ("tuner", makeObject ({
        { "has", r.hasPitch },
        { "freq", r.frequencyHz },
        { "cents", r.cents },
        { "note", juce::String (r.noteName) },
        { "octave", r.octave },
        { "confidence", r.confidence } }));

    // Schrittanzeige
    juce::Array<juce::var> flashes;
    for (int i = 0; i < nmp::DrumMachine::kSteps; ++i) flashes.add (e.drums().stepFlash (i));

    o->setProperty ("drums", makeObject ({
        { "step", e.drums().currentStep() },
        { "flashes", flashes },
        { "bpm", e.drums().effectiveBpm() } }));

    // Looper
    juce::Array<juce::var> tracks;
    for (int i = 0; i < nmp::Looper::kTracks; ++i)
    {
        const auto info = e.looper().trackInfo (i);
        tracks.add (makeObject ({
            { "state", juce::String (nmp::loopStateName (info.state)) },
            { "pos", info.positionUnit },
            { "vol", info.volume },
            { "seconds", info.lengthSeconds },
            { "beats", info.lengthBeats },
            { "rawLength", info.rawLength },
            { "loopStart", info.loopStart },
            { "loopEnd", info.loopEnd } }));
    }
    o->setProperty ("looper", tracks);
    o->setProperty ("looperRecording", e.looper().recordingTrack());
    o->setProperty ("looperOldest", e.looper().oldestPlayingTrack());

    // Recorder
    o->setProperty ("recorder", makeObject ({
        { "state", (int) e.recorder().state() },
        { "pos", e.recorder().positionUnit() },
        { "seconds", e.recorder().lengthSeconds() },
        { "has", e.recorder().hasTake() },
        { "full", e.recorder().isFull() },
        { "l", e.recorder().levelLeft() },
        { "r", e.recorder().levelRight() } }));

    // Tempoquelle, damit die UI anzeigen kann, ob ein Sync-Signal ankommt.
    const auto source = e.transport().source();
    o->setProperty ("clock", makeObject ({
        { "source", source == nmp::Transport::Source::host ? "host"
                  : source == nmp::Transport::Source::midiClock ? "midi" : "none" },
        { "running", e.transport().isRunning() },
        { "bpm", e.transport().bpm() } }));

    return juce::var (o.get());
}

void WebBridge::timerCallback()
{
    // Ist die Seite nicht sichtbar, wird nichts gebaut.
    if (! visible) return;

    emit ("meters", buildMeters());

    if (proc.structureVersion() != lastStructure)
    {
        lastStructure = proc.structureVersion();
        emit ("structure", juce::var (lastStructure));
    }

    Notice notice;
    while (proc.takeNotice (notice))
        emit ("notice", makeObject ({ { "text", notice.text }, { "kind", notice.kind } }));
}

void WebBridge::handle (const juce::var& request, Completion done)
{
    const auto cmd = getString (request, "cmd");

    auto reply = [&done] (juce::var v) { if (done) done (std::move (v)); };

    // Netzwerk-Befehle antworten später.
    if (cmd.startsWith ("t3k.")) { cmdT3k (request, cmd, std::move (done)); return; }

    if (cmd == "sync")     { reply (ok (buildSync())); return; }
    if (cmd == "meters")   { reply (ok (buildMeters())); return; }

    if (cmd.startsWith ("param."))    { reply (cmdParamSet (request)); return; }
    if (cmd.startsWith ("slot."))     { reply (cmdSlot (request, cmd)); return; }
    if (cmd.startsWith ("library."))  { reply (cmdLibrary (request, cmd)); return; }
    if (cmd.startsWith ("preset."))   { reply (cmdPreset (request, cmd)); return; }
    if (cmd.startsWith ("settings.")) { reply (cmdSettings (request, cmd)); return; }
    if (cmd.startsWith ("eq."))       { reply (cmdEq (request, cmd)); return; }
    if (cmd.startsWith ("looper."))   { reply (cmdLooper (request, cmd)); return; }
    if (cmd.startsWith ("recorder.")) { reply (cmdRecorder (request, cmd)); return; }
    if (cmd.startsWith ("audio."))    { reply (cmdAudio (request, cmd)); return; }

    if (cmd.startsWith ("tool."))
    {
        const auto key = getString (request, "tool");
        for (int t = 0; t < nmp::kNumTools; ++t)
            if (key.equalsIgnoreCase (nmp::toolKey ((nmp::Tool) t)))
            {
                proc.setToolShown ((nmp::Tool) t, getBool (request, "shown"));
                reply (ok());
                return;
            }
        reply (fail ("There is no such tool."));
        return;
    }

    // Die drei Stufen sind Fenstergrößen. Die Pedalgröße ergibt sich aus der
    // Fensterbreite.
    if (cmd == "window.size")
    {
        const int step = (int) getNumber (request, "step");
        proc.setZoom (step);
        if (sizeRequest) sizeRequest (step);
        reply (ok());
        return;
    }

    if (cmd == "firstrun.done")
    {
        // Erster Start erledigt, die mitgelieferten IRs werden nicht noch
        // einmal abgelegt.
        proc.markFirstRunDone();
        proc.clearFirstRun();
        reply (ok());
        return;
    }

    if (cmd == "engine.lite")
    {
        const bool lite = getBool (request, "lite");
        proc.engine().setEngineLite (lite);
        proc.settings().setEngineLite (lite);
        reply (ok());
        return;
    }

    if (cmd == "engine.oversampling")
    {
        // Die Kopien für das Oversampling entstehen beim Aufbau eines Captures,
        // also alle geladenen neu aufbauen. Der Wechsel wird übergeblendet.
        const int factor = (int) getNumber (request, "factor");
        proc.settings().setCaptureOversampling (factor);
        proc.engine().setCaptureOversampling (proc.settings().captureOversampling());
        proc.loader().requestReload();
        reply (ok (buildSettings()));
        return;
    }

    if (cmd == "tuner.watch")
    {
        // Kleines Stimmgerät an oder aus (die Seite speichert die Wahl).
        miniTuner = getBool (request, "on");
        proc.setTunerWatched (visible && miniTuner);
        reply (ok());
        return;
    }

    if (cmd == "visible")
    {
        setVisible (getBool (request, "visible"));
        reply (ok());
        return;
    }

    reply (fail ("Unknown command: " + cmd));
}

} // namespace nmpl
