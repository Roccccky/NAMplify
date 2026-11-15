#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <atomic>
#include <memory>

namespace nmpl
{

/// Das Stimmgerät läuft in einem eigenen Thread. Der Audio-Thread legt nur
/// Samples ab, die Erkennung (eine FFT über einige tausend Werte) würde dort
/// Aussetzer und im Message-Thread Ruckeln verursachen.
class NamplifyProcessor::TunerWorker : private juce::Thread
{
public:
    explicit TunerWorker (nmp::Engine& e) : juce::Thread ("NAMplify Tuner"), engine (e)
    { startThread (juce::Thread::Priority::low); }

    ~TunerWorker() override { signalThreadShouldExit(); stopThread (2000); }

    void setActive (bool a) { active.store (a); }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            if (active.load()) engine.tuner().analyse();
            wait (active.load() ? 22 : 120);
        }
    }

    nmp::Engine& engine;
    std::atomic<bool> active { false };
};

NamplifyProcessor::NamplifyProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NAMPLIFY", createParameterLayout (slotTypes)),
      presetManager (lib),
      modelLoader (audioEngine),
      t3k (prefs),
      midiHandler (prefs)
{
    params = collectParameters (apvts);

    firstRun = ! prefs.hasRunBefore();

    // Beim ersten Start die drei mitgelieferten IRs in die Bibliothek legen.
    if (firstRun) lib.installBundledIrs();
    lib.rescan();

    audioEngine.setInterfaceDbu (prefs.interfaceDbu());
    audioEngine.setEngineLite (prefs.engineLite());
    audioEngine.setCaptureOversampling (prefs.captureOversampling());
    audioEngine.globalEq().setEnabled (prefs.eqEnabled());
    audioEngine.globalEq().setCurve (prefs.eqCurve());

    midiHandler.refreshMappings();

    modelLoader.setCallback ([this] (const Loader::Result& r) { afterLoad (r); });

    tunerWorker = std::make_unique<TunerWorker> (audioEngine);

    // JUCEs eigenes Eingangs-Mute im Standalone wird in Standalone.cpp
    // abgeschaltet.

    startTimerHz (30);
}

NamplifyProcessor::~NamplifyProcessor()
{
    stopTimer();
    tunerWorker.reset();
}

bool NamplifyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()
        && ! in.isDisabled())
        return false;

    return true;
}

void NamplifyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    audioEngine.prepare (sampleRate, samplesPerBlock);
    audioEngine.setInterfaceDbu (prefs.interfaceDbu());
    audioEngine.setEngineLite (prefs.engineLite());

    applyRigToEngine();
    setLatencySamples (audioEngine.latencySamples());

    // Nach einem Samplerate-Wechsel müssen die Captures neu vorbereitet werden.
    modelLoader.requestReload();
}

void NamplifyProcessor::releaseResources() {}

void NamplifyProcessor::syncParamsToEngine()
{
    auto& set = audioEngine.params();
    for (int i = 0; i < nmp::kNumParams; ++i)
        set.set ((nmp::ParamId) i, readParam (params, (nmp::ParamId) i));
}

void NamplifyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, juce::jmin (ch, juce::jmax (0, numIn - 1)), 0, numSamples);

    if (monoInput.load (std::memory_order_relaxed) && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    if (numOut < 2 && numOut > 0)
        buffer.setSize (2, numSamples, true, true, true);

    // Tempo vom Host-Transport (Hosts senden Plugins keine MIDI-Clock).
    if (auto* head = getPlayHead())
    {
        if (const auto pos = head->getPosition())
        {
            const auto bpm = pos->getBpm();
            const auto ppq = pos->getPpqPosition();
            if (bpm.hasValue())
                audioEngine.transport().setHostInfo (pos->getIsPlaying(), *bpm,
                                                     ppq.hasValue() ? *ppq : 0.0);
        }
    }

    // MIDI
    midiHandler.rebuildIfNeeded();
    juce::MidiBuffer outgoing;
    midiHandler.processBlock (midiMessages, outgoing, audioEngine.transport());
    midiMessages.swapWith (outgoing);

    syncParamsToEngine();

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    audioEngine.process (left, right, numSamples);

    if (buffer.getNumChannels() > 1 && right == left)
        buffer.copyFrom (1, 0, left, numSamples);
}

void NamplifyProcessor::applyRigToEngine()
{
    rigState.layout.sanitise();
    audioEngine.setLayout (rigState.layout);
    for (int s = 0; s < nmp::kNumSlots; ++s)
        slotTypes[(size_t) s].store ((int) rigState.layout.type[(size_t) s], std::memory_order_relaxed);

    for (int t = 0; t < nmp::kNumTools; ++t)
        audioEngine.setToolShown ((nmp::Tool) t, rigState.tools[(size_t) t]);

    for (int i = 0; i < 4; ++i)
        audioEngine.looper().setTrackVolume (i, rigState.looperVolumes[(size_t) i]);

    enforceBoardConsistency();
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    setLatencySamples (audioEngine.latencySamples());
}

namespace
{
    nmp::ParamId toolParam (nmp::Tool t)
    {
        switch (t)
        {
            case nmp::Tool::looper:   return nmp::ParamId::looperOn;
            case nmp::Tool::drums:    return nmp::ParamId::drumsOn;
            case nmp::Tool::recorder: return nmp::ParamId::recorderOn;
            default:                  return nmp::ParamId::count;
        }
    }

    juce::String slotLabel (const nmp::Layout& l, int slot)
    {
        const auto t = l.type[(size_t) slot];
        return t == nmp::SlotType::nam ? juce::String (nmp::namRoleName (l.role[(size_t) slot]))
                                       : juce::String (nmp::slotTypeName (t));
    }
}

void NamplifyProcessor::enforceBoardConsistency()
{
    // Beim Start alles ausschalten, was nicht auf dem Board liegt. Leere Slots
    // rechnen ohnehin nicht, der Schalter steht trotzdem auf aus, damit der
    // Host dasselbe zeigt.
    auto off = [this] (nmp::ParamId id)
    {
        if (auto* p = params[(size_t) id])
            if (p->getValue() > 0.5f) setParamUnit (id, 0.0f);
    };

    for (int s = 0; s < nmp::kNumSlots; ++s)
        if (rigState.layout.type[(size_t) s] == nmp::SlotType::empty)
            off (nmp::slotOnParam (s));

    for (int t = 0; t < nmp::kNumTools; ++t)
        if (! rigState.tools[(size_t) t])
            off (toolParam ((nmp::Tool) t));
}

void NamplifyProcessor::commitLayout()
{
    rigState.layout.sanitise();
    for (int s = 0; s < nmp::kNumSlots; ++s)
    {
        slotTypes[(size_t) s].store ((int) rigState.layout.type[(size_t) s], std::memory_order_relaxed);
        if (! slotTakesFiles (rigState.layout.type[(size_t) s]))
            rigState.files[(size_t) s].clear();
    }

    audioEngine.setLayout (rigState.layout);

    // Die Texte der Slot-Regler hängen vom Typ ab, der Host soll sie neu lesen.
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    setLatencySamples (audioEngine.latencySamples());
    bumpStructure();
}

void NamplifyProcessor::setSlotValue (int slot, int k, float value)
{
    const auto type = rigState.layout.type[(size_t) slot];
    if (k < 0 || k >= nmp::slotControlCount (type)) return;
    setParamUnit (nmp::slotControlParam (slot, k), nmp::slotControl (type, k).toUnit (value));
}

void NamplifyProcessor::resetSlotControls (int slot)
{
    const auto type = rigState.layout.type[(size_t) slot];
    for (int k = 0; k < nmp::kSlotControls; ++k)
    {
        if (k < nmp::slotControlCount (type))
        {
            const auto& d = nmp::slotControl (type, k);
            setParamUnit (nmp::slotControlParam (slot, k), d.toUnit (d.def));
        }
        else
            setParamUnit (nmp::slotControlParam (slot, k), 0.0f);
    }
    setParamUnit (nmp::slotOnParam (slot), type == nmp::SlotType::empty ? 0.0f : 1.0f);
}

int NamplifyProcessor::irAfter (int slot) const
{
    const auto& l = rigState.layout;
    int lane = 0, pos = 0;
    if (! l.find (slot, lane, pos)) return -1;

    for (int p = pos + 1; p < l.length[(size_t) lane]; ++p)
    {
        const int s = l.lane[(size_t) lane][(size_t) p];
        if (s >= 0 && l.type[(size_t) s] == nmp::SlotType::ir) return s;
    }
    return -1;
}

int NamplifyProcessor::addSlot (nmp::SlotType type, nmp::NamRole role, int lane, int pos, bool announce)
{
    auto& l = rigState.layout;
    if (type == nmp::SlotType::empty) return -1;

    lane = juce::jlimit (0, nmp::kNumLanes - 1, lane);

    if (l.count (type) >= nmp::slotTypeLimit (type))
    {
        // Die Grenze hängt an der CPU-Last (Slots.h) und wird angezeigt.
        if (announce)
            notify (juce::String (nmp::slotTypeLimit (type)) + " " + nmp::slotTypeName (type)
                    + " slots are the most the rig can run", "warn");
        return -1;
    }

    const int slot = l.firstFree();
    if (slot < 0) { if (announce) notify ("All slots are in use", "warn"); return -1; }

    // An die angetippte Position: eine Lücke wird gefüllt, eine belegte
    // Position schiebt bis zur nächsten Lücke.
    if (! l.insertAt (lane, pos, slot))
    {
        if (announce) notify ("That lane is full", "warn");
        return -1;
    }

    l.type[(size_t) slot] = type;
    l.role[(size_t) slot] = role;

    rigState.files[(size_t) slot].clear();
    commitLayout();
    // Erst nach dem Aufbau: der Loader leert den Block, den der Slot jetzt
    // bekommt, auch wenn er vorher ein Capture enthielt.
    modelLoader.requestClear (slot);

    // Der neue Slot ist eingeschaltet und hat die Werkswerte seines Typs.
    resetSlotControls (slot);

    // Keine Box: ein neuer NAM-Slot hat noch keine Rolle. Die Box kommt, sobald
    // ein Amp geladen (loadIntoSlot) oder AMP als Rolle gewählt wird.
    juce::ignoreUnused (announce);

    return slot;
}

void NamplifyProcessor::removeSlot (int slot)
{
    if (slot < 0 || slot >= nmp::kNumSlots) return;
    auto& l = rigState.layout;

    int lane = 0, pos = 0;
    if (! l.find (slot, lane, pos)) return;

    // Box entfernen, während ein Amp liegt, ist erlaubt, es gibt nur einen
    // Hinweis.
    if (l.type[(size_t) slot] == nmp::SlotType::ir)
        for (int p = 0; p < pos; ++p)
        {
            const int s = l.lane[(size_t) lane][(size_t) p];
            if (s >= 0 && l.type[(size_t) s] == nmp::SlotType::nam && l.role[(size_t) s] == nmp::NamRole::amp)
            {
                int other = -1;
                for (int q = pos + 1; q < l.length[(size_t) lane]; ++q)
                {
                    const int t = l.lane[(size_t) lane][(size_t) q];
                    if (t >= 0 && l.type[(size_t) t] == nmp::SlotType::ir) other = t;
                }
                if (other < 0) notify ("AMP now runs without a cab", "warn");
                break;
            }
        }

    // Es bleibt eine Lücke, der Rest bleibt an seiner Position.
    l.removeAt (lane, pos);

    l.type[(size_t) slot] = nmp::SlotType::empty;
    rigState.files[(size_t) slot].clear();
    modelLoader.requestClear (slot);
    setParamUnit (nmp::slotOnParam (slot), 0.0f);
    commitLayout();
}

void NamplifyProcessor::moveSlot (int slot, int lane, int pos)
{
    auto& l = rigState.layout;
    int fromLane = 0, fromPos = 0;
    if (slot < 0 || slot >= nmp::kNumSlots || ! l.find (slot, fromLane, fromPos)) return;

    lane = juce::jlimit (0, nmp::kNumLanes - 1, lane);

    // Entfernen lässt eine Lücke, Einfügen füllt eine oder schiebt. Geht es
    // nicht, bleibt alles unverändert.
    auto before = l;
    l.removeAt (fromLane, fromPos);
    if (! l.insertAt (lane, pos, slot))
    {
        l = before;
        notify ("That lane is full", "warn");
        return;
    }

    commitLayout();
}

void NamplifyProcessor::setSlotType (int slot, nmp::SlotType type)
{
    auto& l = rigState.layout;
    int lane = 0, pos = 0;
    if (slot < 0 || slot >= nmp::kNumSlots || ! l.find (slot, lane, pos)) return;
    if (l.type[(size_t) slot] == type) return;

    if (type == nmp::SlotType::empty) { removeSlot (slot); return; }

    if (l.count (type) >= nmp::slotTypeLimit (type))
    {
        notify (juce::String (nmp::slotTypeLimit (type)) + " " + nmp::slotTypeName (type)
                + " slots are the most the rig can run", "warn");
        bumpStructure();   // die UI setzt ihr Menü zurück
        return;
    }

    // Anderer Typ, andere Regler: Werkswerte des neuen Typs. Die Nummer und
    // damit die Position in der Kette bleiben.
    l.type[(size_t) slot] = type;
    l.role[(size_t) slot] = nmp::NamRole::amp;
    rigState.files[(size_t) slot].clear();
    commitLayout();
    modelLoader.requestClear (slot);
    resetSlotControls (slot);
    // Keine Box, siehe addSlot.
}

void NamplifyProcessor::setSlotRole (int slot, nmp::NamRole role)
{
    auto& l = rigState.layout;
    if (slot < 0 || slot >= nmp::kNumSlots || l.type[(size_t) slot] != nmp::SlotType::nam) return;
    if (l.role[(size_t) slot] == role) return;

    // Die Rolle filtert die Bänke. Passt die geladene Datei nicht zur neuen
    // Rolle, wird sie entfernt.
    auto& f = rigState.files[(size_t) slot];
    if (! f.isEmpty() && roleForGear (f.gear) != role)
    {
        f.clear();
        modelLoader.requestClear (slot);
    }

    l.role[(size_t) slot] = role;
    commitLayout();

    // MIX gibt es nur bei PEDAL und OUTBOARD, die Klangregelung nur bei AMP und
    // FULL RIG. Auf Werkswerte setzen, damit keine unsichtbare Einstellung
    // mitwirkt.
    if (role == nmp::NamRole::pedal || role == nmp::NamRole::outboard || role == nmp::NamRole::space)
        setSlotValue (slot, nmp::ctl::namMix, 1.0f);
    else
    {
        setSlotValue (slot, nmp::ctl::namBass, 0.0f);
        setSlotValue (slot, nmp::ctl::namMid, 0.0f);
        setSlotValue (slot, nmp::ctl::namTreble, 0.0f);
    }

    if (role == nmp::NamRole::amp) ensureIrAfter (slot, true);
    else if (role == nmp::NamRole::fullrig)
    {
        // Ein Full Rig bringt die Box mit, die IR dahinter wird ausgeschaltet.
        const int ir = irAfter (slot);
        if (ir >= 0) setParamUnit (nmp::slotOnParam (ir), 0.0f);
    }
}

void NamplifyProcessor::ensureIrAfter (int slot, bool announce)
{
    int ir = irAfter (slot);

    if (ir < 0)
    {
        // Ein Amp bekommt automatisch eine Box.
        int lane = 0, pos = 0;
        if (! rigState.layout.find (slot, lane, pos)) return;
        ir = addSlot (nmp::SlotType::ir, nmp::NamRole::amp, lane, pos + 1, false);
        if (ir < 0) return;   // Grenze erreicht, der Amp läuft ohne

        // Mit Hinweis, sonst wundert man sich über die zweite Karte.
        if (announce) notify ("IR came along with AMP");
    }

    if (rigState.files[(size_t) ir].isEmpty())
    {
        const auto cabs = lib.banksFor (nmp::SlotType::ir, nmp::NamRole::amp);
        if (! cabs.empty() && ! cabs.front()->variants.empty())
        {
            const auto& bank = *cabs.front();
            const auto& variant = bank.variants.front();

            SlotFile cabInfo;
            cabInfo.relativePath = variant.file.getRelativePathFrom (lib.root());
            cabInfo.bankTitle = bank.title;
            cabInfo.variantName = variant.name;
            cabInfo.gear = bank.gear;
            cabInfo.imageFile = bank.image.getFileName();

            loadIntoSlot (ir, variant.file, cabInfo);
        }
    }

    // Die Box nur einschalten, wenn eine Datei geladen ist.
    setParamUnit (nmp::slotOnParam (ir), rigState.files[(size_t) ir].isEmpty() ? 0.0f : 1.0f);
}

void NamplifyProcessor::setToolShown (nmp::Tool tool, bool shown, bool announce)
{
    const auto t = (size_t) tool;
    if (t >= (size_t) nmp::kNumTools || rigState.tools[t] == shown) return;

    rigState.tools[t] = shown;
    audioEngine.setToolShown (tool, shown);
    setParamUnit (toolParam (tool), shown ? 1.0f : 0.0f);

    if (shown && tool == nmp::Tool::looper && ! rigState.tools[(size_t) nmp::Tool::drums])
    {
        // Der Looper bringt die Drums mit, er braucht ihren Takt.
        setToolShown (nmp::Tool::drums, true, false);
        if (announce) notify (juce::String (nmp::toolName (nmp::Tool::drums)) + " came along with LOOPER");
    }

    setLatencySamples (audioEngine.latencySamples());
    bumpStructure();
}

void NamplifyProcessor::setZoom (int z)
{
    rigState.zoom = juce::jlimit (0, 2, z);
}

void NamplifyProcessor::resetParam (nmp::ParamId id, float value)
{
    const auto& d = nmp::paramDesc (id);
    setParamUnit (id, d.toUnit (value));
}

void NamplifyProcessor::setParamUnit (nmp::ParamId id, float unit)
{
    if (auto* p = params[(size_t) id])
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, unit));
        p->endChangeGesture();
    }
}

void NamplifyProcessor::requestSlotFile (int slot)
{
    const auto& s = rigState.files[(size_t) slot];
    if (s.isEmpty()) { modelLoader.requestClear (slot); return; }

    Loader::Request request;
    request.slot = slot;
    request.file = lib.root().getChildFile (s.relativePath);
    request.info = s;
    modelLoader.request (request);
}

void NamplifyProcessor::loadIntoSlot (int slot, const juce::File& file, const SlotFile& info)
{
    if (slot < 0 || slot >= nmp::kNumSlots) return;
    auto& l = rigState.layout;
    const auto type = l.type[(size_t) slot];

    if (! slotTakesFiles (type))
    {
        notify (slotLabel (l, slot) + " does not take a file", "warn");
        return;
    }

    const bool isWav = file.hasFileExtension (".wav");
    if (type == nmp::SlotType::ir && ! isWav)
    {
        notify ("An IR slot takes an impulse (.wav), not a capture", "warn");
        return;
    }

    const bool sameBank = rigState.files[(size_t) slot].bankTitle == info.bankTitle
                       && info.bankTitle.isNotEmpty();

    // Eine Capture-Datei bringt ihre Rolle mit: ein Full Rig von TONE3000
    // landet als FULL RIG.
    if (type == nmp::SlotType::nam && ! isWav && info.gear.isNotEmpty())
    {
        const auto role = roleForGear (info.gear);
        if (role != l.role[(size_t) slot])
        {
            l.role[(size_t) slot] = role;
            commitLayout();
        }
    }

    rigState.files[(size_t) slot] = info;

    Loader::Request request;
    request.slot = slot;
    request.file = file;
    request.info = info;
    modelLoader.request (request);

    // Was nach dem Laden gilt
    setParamUnit (nmp::slotOnParam (slot), 1.0f);

    if (type == nmp::SlotType::ir)
    {
        if (! sameBank) setSlotValue (slot, nmp::ctl::irOutput, 0.0f);
    }
    else
    {
        const auto role = l.role[(size_t) slot];
        setSlotValue (slot, nmp::ctl::namGain, 0.0f);       // GAIN zurücksetzen

        if (! sameBank)
        {
            setSlotValue (slot, nmp::ctl::namLevel, 0.0f);  // LEVEL nur bei anderer Bank
            setSlotValue (slot, nmp::ctl::namMix, 1.0f);
            // Beim Bankwechsel gehen BASS, MID und TREBLE auf null, damit der
            // Vergleich zweier Captures nicht von der Klangregelung abhängt.
            setSlotValue (slot, nmp::ctl::namBass, 0.0f);
            setSlotValue (slot, nmp::ctl::namMid, 0.0f);
            setSlotValue (slot, nmp::ctl::namTreble, 0.0f);
        }

        if (role == nmp::NamRole::fullrig)
        {
            const int ir = irAfter (slot);
            if (ir >= 0) setParamUnit (nmp::slotOnParam (ir), 0.0f);
        }
        else if (role == nmp::NamRole::amp)
            ensureIrAfter (slot, true);
    }

    bumpStructure();
}

int NamplifyProcessor::slotForFile (nmp::SlotType type, nmp::NamRole role, int preferred)
{
    const auto& l = rigState.layout;

    // Ein vorhandener Slot, wenn einer passt (nmp::placeForFile, getestet in
    // tests/RackTest.cpp): ein Pedal überschreibt keinen Amp, ein Full Rig
    // tauscht den Amp und umgekehrt. Die Box schaltet dann loadIntoSlot.
    std::array<bool, nmp::kNumSlots> filled {};
    for (int s = 0; s < nmp::kNumSlots; ++s)
        filled[(size_t) s] = ! rigState.files[(size_t) s].isEmpty();

    if (const int s = nmp::placeForFile (l, filled, type, role, preferred); s >= 0)
        return s;

    // Neuer Slot in Spur A. Ein Capture kommt vor die erste Box.
    int pos = l.length[0];
    if (type == nmp::SlotType::nam && role != nmp::NamRole::outboard && role != nmp::NamRole::space)
        for (int p = 0; p < l.length[0]; ++p)
        {
            const int s = l.lane[0][(size_t) p];
            if (s >= 0 && l.type[(size_t) s] == nmp::SlotType::ir) { pos = p; break; }
        }

    // Ein Pedal kommt vor den ersten Amp, in eine Lücke direkt davor, sonst
    // rückt der Amp eins weiter.
    if (type == nmp::SlotType::nam && role == nmp::NamRole::pedal)
        for (int p = 0; p < l.length[0]; ++p)
        {
            const int s = l.lane[0][(size_t) p];
            if (s < 0 || l.type[(size_t) s] != nmp::SlotType::nam) continue;
            const auto r = l.role[(size_t) s];
            if (r == nmp::NamRole::pedal || r == nmp::NamRole::outboard || r == nmp::NamRole::space) continue;
            pos = (p > 0 && l.lane[0][(size_t) p - 1] < 0) ? p - 1 : p;
            break;
        }

    // In einer leeren Kette steht ein Amp in der Mitte, davor Platz für Gate,
    // Drive und Pedale, dahinter für Box und Effekte.
    if (l.length[0] == 0)
        pos = type == nmp::SlotType::nam && role == nmp::NamRole::pedal ? 2
            : type == nmp::SlotType::nam && (role == nmp::NamRole::outboard || role == nmp::NamRole::space)
                ? nmp::kMaxLaneSlots * 2 / 3   // OUTBOARD und SPACE hinten, hinter Amp und Box
            : type == nmp::SlotType::nam ? nmp::kMaxLaneSlots / 3
            : nmp::kMaxLaneSlots / 2;

    return addSlot (type, role, 0, pos, true);
}

void NamplifyProcessor::clearSlot (int slot)
{
    if (slot < 0 || slot >= nmp::kNumSlots) return;
    rigState.files[(size_t) slot].clear();
    modelLoader.requestClear (slot);
    bumpStructure();
}

void NamplifyProcessor::afterLoad (const Loader::Result& r)
{
    if (! r.ok && ! r.error.isEmpty())
    {
        notify (r.error, "error");
        if (r.slot >= 0 && r.slot < nmp::kNumSlots
            && rigState.files[(size_t) r.slot].relativePath == r.info.relativePath)
            rigState.files[(size_t) r.slot].clear();
    }

    setLatencySamples (audioEngine.latencySamples());
    bumpStructure();
}

const nmp::ParamDesc& NamplifyProcessor::effectiveDesc (int i) const
{
    const int first = (int) nmp::ParamId::firstSlotParam;
    if (i >= first && i < nmp::kNumParams)
    {
        const int slot = (i - first) / nmp::kParamsPerSlot;
        const int k    = (i - first) % nmp::kParamsPerSlot - 1;
        const auto type = rigState.layout.type[(size_t) slot];
        if (k >= 0 && k < nmp::slotControlCount (type)) return nmp::slotControl (type, k);
    }
    return nmp::paramDesc (i);
}

juce::var NamplifyProcessor::paramsToVar() const
{
    juce::DynamicObject::Ptr o (new juce::DynamicObject());
    for (int i = 0; i < nmp::kNumParams; ++i)
        if (auto* p = params[(size_t) i])
            o->setProperty (nmp::paramDesc (i).id, (double) p->getValue());
    return juce::var (o.get());
}

void NamplifyProcessor::applyParamsFromVar (const juce::var& v)
{
    auto* o = v.getDynamicObject();
    if (o == nullptr) return;

    for (int i = 0; i < nmp::kNumParams; ++i)
    {
        const juce::Identifier key (nmp::paramDesc (i).id);
        if (! o->hasProperty (key)) continue;
        setParamUnit ((nmp::ParamId) i, (float) (double) o->getProperty (key));
    }
}

void NamplifyProcessor::loadPreset (const juce::String& name)
{
    juce::var storedParams;
    RigState loaded;
    juce::String error;

    if (! presetManager.load (name, storedParams, loaded, error))
    {
        notify (error, "error");
        return;
    }

    rigState = loaded;
    applyRigToEngine();
    applyParamsFromVar (storedParams);
    enforceBoardConsistency();

    // Geladene Dateien wieder in ihre Slots laden.
    for (int i = 0; i < nmp::kNumSlots; ++i)
    {
        auto& s = rigState.files[(size_t) i];
        if (! s.isEmpty() && ! lib.root().getChildFile (s.relativePath).existsAsFile())
        {
            notify (s.bankTitle + " is no longer in the library", "warn");
            s.clear();
        }
        requestSlotFile (i);
    }

    // Damit der Host das neue Programm anzeigt.
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    bumpStructure();
}

void NamplifyProcessor::savePreset (const juce::String& name)
{
    juce::String error;
    if (! presetManager.save (name, paramsToVar(), rigState, error))
        notify (error, "error");
    else
        bumpStructure();
}

void NamplifyProcessor::deletePreset (const juce::String& name)
{
    juce::String error;
    if (! presetManager.remove (name, error)) notify (error, "error");
    bumpStructure();
}

void NamplifyProcessor::stepPreset (int direction)
{
    const auto next = presetManager.neighbour (direction);
    if (next.isNotEmpty()) loadPreset (next);
}

void NamplifyProcessor::resetRig()
{
    // Alles auf Werkszustand.
    for (int i = 0; i < nmp::kNumParams; ++i)
    {
        const auto& d = nmp::paramDesc (i);
        setParamUnit ((nmp::ParamId) i, d.toUnit (d.def));
    }

    rigState = RigState{};
    for (int i = 0; i < nmp::kNumSlots; ++i)
        modelLoader.requestClear (i);

    presetManager.setCurrentName ({});
    applyRigToEngine();
    for (int i = 0; i < nmp::kNumSlots; ++i)
        if (rigState.layout.type[(size_t) i] != nmp::SlotType::empty) resetSlotControls (i);
    bumpStructure();
}

void NamplifyProcessor::notify (const juce::String& text, const juce::String& kind)
{
    std::lock_guard<std::mutex> lock (noticeLock);
    // Eine neue Einblendung ersetzt die alte.
    notices.clear();
    notices.push_back ({ text, kind });
}

bool NamplifyProcessor::takeNotice (Notice& out)
{
    std::lock_guard<std::mutex> lock (noticeLock);
    if (notices.empty()) return false;
    out = notices.front();
    notices.pop_front();
    return true;
}

void NamplifyProcessor::setMuted (bool m)
{
    setParamUnit (nmp::ParamId::mute, m ? 1.0f : 0.0f);
}

bool NamplifyProcessor::isMuted() const
{
    auto* p = params[(size_t) nmp::ParamId::mute];
    return p != nullptr && p->getValue() >= 0.5f;
}

void NamplifyProcessor::markFirstRunDone() { prefs.markHasRun(); }

void NamplifyProcessor::timerCallback()
{
    audioEngine.serviceBackground();

    // Stimmgerät: auf, zu, gespeicherter Mute-Zustand
    const bool tunerOpen = params[(size_t) nmp::ParamId::tuner] != nullptr
                        && params[(size_t) nmp::ParamId::tuner]->getValue() >= 0.5f;

    if (tunerOpen != tunerWasOpen)
    {
        if (tunerOpen)
        {
            // Öffnen schaltet stumm, wie beim letzten Stimmen eingestellt.
            muteBeforeTuner = isMuted();
            setMuted (prefs.tunerMutePreference());
        }
        else
        {
            // Schließen stellt den vorherigen Mute-Zustand wieder her, damit
            // ein vorher gesetztes Mute erhalten bleibt. Ausnahme: wer im
            // Stimmgerät den Mute-Taster gedrückt hat, hat entschieden.
            const bool decided = isMuted() != prefs.tunerMutePreference();
            if (decided) prefs.setTunerMutePreference (isMuted());
            else         setMuted (muteBeforeTuner);
        }
        tunerWasOpen = tunerOpen;
    }

    // Großes oder kleines Stimmgerät, solange sichtbar.
    if (tunerWorker) tunerWorker->setActive (tunerOpen || tunerWatched.load());

    handleMidiEvents();
}

const juce::String NamplifyProcessor::getProgramName (int index)
{
    const auto& list = presetManager.presets();
    return index >= 0 && index < (int) list.size() ? list[(size_t) index].name
                                                     : "Program " + juce::String (index + 1);
}

void NamplifyProcessor::handleMidiEvents()
{
    // Programmwechsel vom Host (VST3) läuft wie der über MIDI und folgt
    // demselben Schalter.
    const int program = pendingProgram.exchange (-1);
    if (program >= 0 && readParam (params, nmp::ParamId::midiProgramChange) >= 0.5f)
    {
        const auto name = presetManager.byProgram (program);
        if (name.isNotEmpty() && name != presetManager.currentName())
        {
            loadPreset (name);
            notify ("Preset " + name + " via program " + juce::String (program + 1));
        }
    }

    for (const auto& e : midiHandler.takeEvents())
    {
        switch (e.type)
        {
            case MidiHandler::Event::Type::parameter:
                if (e.paramIndex >= 0 && e.paramIndex < nmp::kNumParams)
                    setParamUnit ((nmp::ParamId) e.paramIndex, e.unitValue);
                break;

            case MidiHandler::Event::Type::toggle:
                if (e.paramIndex >= 0 && e.paramIndex < nmp::kNumParams)
                    if (auto* p = params[(size_t) e.paramIndex])
                        setParamUnit ((nmp::ParamId) e.paramIndex, p->getValue() >= 0.5f ? 0.0f : 1.0f);
                break;

            case MidiHandler::Event::Type::programChange:
            {
                const auto name = presetManager.byProgram (e.program);
                if (name.isNotEmpty())
                {
                    loadPreset (name);
                    // Wechsel über MIDI wird eingeblendet.
                    notify ("Preset " + name + " via MIDI program " + juce::String (e.program + 1));
                }
                break;
            }

            case MidiHandler::Event::Type::learned:
                if (e.paramIndex >= 0 && e.paramIndex < nmp::kNumParams)
                {
                    prefs.learnAssignment (e.cc, e.ccChannel, e.paramIndex);
                    midiHandler.refreshMappings();
                    notify ("CC " + juce::String (e.cc) + " → "
                            + juce::String (juce::CharPointer_UTF8 (
                                  nmp::paramDesc (e.paramIndex).name)));
                    bumpStructure();
                }
                break;
        }
    }
}

// Dateien im Plugin-Zustand.
//
// Der Zustand nennt jede Datei über ihren Pfad in der Bibliothek. Auf einem
// anderen Rechner oder nach dem Umbenennen einer Bank fehlt sie dort. Deshalb
// werden die Dateien, die gerade in den Slots liegen, komprimiert
// mitgespeichert. Vorher angehörte Captures gehören nicht dazu.
//
// Beim Öffnen hat die Bibliothek Vorrang. Fehlt die Datei dort, wird sie aus
// dem Zustand in einen eigenen Ordner neben den Einstellungen geschrieben und
// von dort geladen. Die Bibliothek bleibt unverändert.

namespace
{
    /// Größer ist kein Capture und keine IR.
    constexpr juce::int64 kMaxEmbeddedBytes = 32 * 1024 * 1024;

    juce::File sessionFileFor (const Settings& prefs, const juce::String& relativePath)
    {
        const auto flat = relativePath.replaceCharacters ("/\\:", "___");
        return prefs.directory().getChildFile ("Session files").getChildFile (flat);
    }

    juce::String packFile (const juce::File& f)
    {
        juce::MemoryBlock raw;
        if (! f.loadFileAsData (raw)) return {};

        juce::MemoryOutputStream packed;
        {
            juce::GZIPCompressorOutputStream gz (packed, 9);
            gz.write (raw.getData(), raw.getSize());
        }
        return juce::Base64::toBase64 (packed.getData(), packed.getDataSize());
    }

    bool unpackTo (const juce::String& base64, const juce::File& target)
    {
        juce::MemoryOutputStream packed;
        if (! juce::Base64::convertFromBase64 (packed, base64)) return false;

        juce::MemoryInputStream in (packed.getData(), packed.getDataSize(), false);
        juce::GZIPDecompressorInputStream gz (in);
        juce::MemoryBlock raw;
        gz.readIntoMemoryBlock (raw);
        if (raw.getSize() == 0) return false;

        return target.getParentDirectory().createDirectory()
            && target.replaceWithData (raw.getData(), raw.getSize());
    }
}

void NamplifyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::DynamicObject::Ptr o (new juce::DynamicObject());
    o->setProperty ("format", 3);
    o->setProperty ("params", paramsToVar());
    o->setProperty ("rig", rigState.toVar());
    o->setProperty ("preset", presetManager.currentName());

    juce::Array<juce::var> files;
    for (int i = 0; i < nmp::kNumSlots; ++i)
    {
        const auto& s = rigState.files[(size_t) i];
        if (s.isEmpty()) continue;

        auto source = lib.root().getChildFile (s.relativePath);
        if (! source.existsAsFile()) source = sessionFileFor (prefs, s.relativePath);
        if (! source.existsAsFile() || source.getSize() > kMaxEmbeddedBytes) continue;

        const auto data = packFile (source);
        if (data.isEmpty()) continue;

        juce::DynamicObject::Ptr f (new juce::DynamicObject());
        f->setProperty ("slot", i);
        f->setProperty ("path", s.relativePath);
        f->setProperty ("gzip", data);
        files.add (juce::var (f.get()));
    }
    o->setProperty ("files", files);

    const auto text = juce::JSON::toString (juce::var (o.get()), true);
    destData.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

void NamplifyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;

    const auto json = juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, sizeInBytes));
    auto* o = json.getDynamicObject();
    if (o == nullptr) return;

    // Zuerst der Aufbau. Ein Zustand aus Version 1 übersetzt dabei seine Regler
    // auf die Slots (RigState::fromVar).
    auto storedParams = o->getProperty ("params");
    rigState = RigState::fromVar (o->getProperty ("rig"), storedParams);
    presetManager.setCurrentName (o->getProperty ("preset").toString());

    applyRigToEngine();
    applyParamsFromVar (storedParams);
    enforceBoardConsistency();

    // Mitgespeicherte Dateien, nach Pfad statt nach Slot (in Version 1 hießen
    // die Slots anders).
    juce::StringPairArray embedded;
    if (auto* arr = o->getProperty ("files").getArray())
        for (const auto& f : *arr)
            embedded.set (f.getProperty ("path", {}).toString(), f.getProperty ("gzip", {}).toString());

    for (int i = 0; i < nmp::kNumSlots; ++i)
    {
        auto& s = rigState.files[(size_t) i];
        const auto name = s.bankTitle.isNotEmpty() ? s.bankTitle : s.relativePath;

        if (s.isEmpty()) { modelLoader.requestClear (i); continue; }

        auto file = lib.root().getChildFile (s.relativePath);
        if (! file.existsAsFile())
        {
            // Anderer Rechner oder umbenannte Bank: Datei aus dem Zustand.
            // Liegt sie schon von einem früheren Öffnen dort, wird sie nicht
            // neu geschrieben.
            const auto fromSession = sessionFileFor (prefs, s.relativePath);
            const auto packed = embedded[s.relativePath];
            if (fromSession.existsAsFile()
                || (packed.isNotEmpty() && unpackTo (packed, fromSession)))
            {
                file = fromSession;
                notify (name + " came with the session. It is not in the library on this computer.");
            }
        }

        if (file.existsAsFile())
        {
            Loader::Request request;
            request.slot = i;
            request.file = file;
            request.info = s;
            modelLoader.request (request);
        }
        else
        {
            // Weder in der Bibliothek noch im Zustand (ältere Version ohne
            // mitgespeicherte Dateien): Hinweis anzeigen.
            notify (name + " is not in the library on this computer", "warn");
            s.clear();
        }
    }

    bumpStructure();
}

juce::AudioProcessorEditor* NamplifyProcessor::createEditor()
{
    return new NamplifyEditor (*this);
}

} // namespace nmpl

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nmpl::NamplifyProcessor();
}
