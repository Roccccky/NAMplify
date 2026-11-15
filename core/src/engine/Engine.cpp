#include "namplify/engine/Engine.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace nmp
{

const char* toolKey (Tool t)
{
    switch (t)
    {
        case Tool::looper:   return "LOOPER";
        case Tool::drums:    return "DRUMS";
        case Tool::recorder: return "RECORDER";
        default:             return "?";
    }
}

const char* toolName (Tool t)
{
    // Anzeigename. Der Preset-Schlüssel (toolKey) bleibt unverändert.
    switch (t)
    {
        case Tool::looper:   return "LOOPER";
        case Tool::drums:    return "BEATMACHINE";
        case Tool::recorder: return "RECORDER";
        default:             return "?";
    }
}

const char* toolColour (Tool t)
{
    switch (t)
    {
        case Tool::looper:   return "#d9d9d9";   // helles Grau, der Looper zeigt die Farben seiner Spuren
        case Tool::drums:    return "#949aa3";   // Grau
        case Tool::recorder: return "#d24a3c";   // Rot
        default:             return "#888888";
    }
}

Engine::Engine()
{
    for (auto& t : toolShown) t.store (false);

    // Werkszustand: leere Kette.
    const auto l = defaultLayout();
    rackUnit.requestLayout (l);
    for (int s = 0; s < kNumSlots; ++s)
        if (l.type[(size_t) s] != SlotType::empty) resetSlotControls (s, l.type[(size_t) s]);
}

void Engine::resetSlotControls (int slot, SlotType t)
{
    if (slot < 0 || slot >= kNumSlots) return;
    paramSet.set (slotOnParam (slot), t == SlotType::empty ? 0.0f : 1.0f);
    for (int k = 0; k < kSlotControls; ++k)
    {
        const auto& d = slotControl (t, k);
        paramSet.set (slotControlParam (slot, k), k < slotControlCount (t) ? d.toUnit (d.def) : 0.0f);
    }
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    srate    = sampleRate;
    maxBlock = std::max (16, maxBlockSize);

    const PrepareInfo info { srate, maxBlock };

    // Die Blöcke der Slots bereitet Rack::prepare vor.
    rackUnit.prepare (info);

    looperUnit.prepare (info);
    drumUnit.prepare (info);
    drumL.assign ((size_t) maxBlock, 0.0f);
    drumR.assign ((size_t) maxBlock, 0.0f);
    recorderUnit.prepare (info);

    spreadUnit.prepare (srate, maxBlock);
    eqOut.prepare (srate);
    tunerUnit.prepare (srate);
    clock.prepare (srate);

    inGain  .reset (srate, 0.03);
    outGain .reset (srate, 0.03);
    // Mute blendet in 15 ms.
    muteFade.reset (srate, 0.015);

    inGain  .setCurrentAndTarget (1.0f);
    outGain .setCurrentAndTarget (1.0f);
    muteFade.setCurrentAndTarget (paramSet.getBool (ParamId::mute) ? 0.0f : 1.0f);

    ballIn .reset (srate, 0.003, 0.30);
    ballOut.reset (srate, 0.003, 0.30);

    // Schnell hoch, langsam runter, damit die CPU-Anzeige lesbar bleibt.
    cpuBallistic.reset (1.0 / (double) maxBlock * srate, 0.05, 0.9);

    tunerMono.assign ((size_t) maxBlock, 0.0f);

    looperLatency = rackUnit.latencySamples();
    looperUnit.setLatencyCompensation (looperLatency);

    reset();
}

void Engine::reset()
{
    rackUnit.reset();
    looperUnit.reset();
    drumUnit.reset();
    recorderUnit.reset();
    spreadUnit.reset();
    eqOut.reset();
    tunerUnit.reset();
    clock.reset();
}

void Engine::setToolShown (Tool t, bool on)
{
    toolShown[(size_t) t].store (on, std::memory_order_relaxed);
    if (t == Tool::recorder) recorderUnit.setEnabled (on);
    // Der Looper bekommt seinen Zustand in applyParams (sichtbar und
    // eingeschaltet).
}

void Engine::setEngineLite (bool lite)
{
    liteMode = lite;
    for (auto& n : rackUnit.namPool()) n.setLite (lite);
}

bool Engine::anyModelHasLite()
{
    for (auto& n : rackUnit.namPool())
        if (const auto* i = n.modelInfo(); i != nullptr && i->hasLite) return true;
    return false;
}

void Engine::setInterfaceDbu (float dbu)
{
    refDbu = clamp (dbu, -20.0f, 40.0f);
    for (auto& n : rackUnit.namPool()) n.setInterfaceDbu (refDbu);
}

void Engine::serviceBackground()
{
    for (auto& n : rackUnit.namPool()) { n.collectRetired(); n.reconcileLite(); }
    for (auto& c : rackUnit.irPool()) c.collectRetired();
    recorderUnit.serviceAllocations();

    const int lat = rackUnit.latencySamples();
    if (lat != looperLatency)
    {
        looperLatency = lat;
        looperUnit.setLatencyCompensation (lat);
    }
}

void Engine::applySlot (int slot)
{
    const auto type = rackUnit.liveType (slot);
    Block* blk = rackUnit.liveBlock (slot);
    if (type == SlotType::empty || blk == nullptr) { rackUnit.setEnabled (slot, false); return; }

    auto& p = paramSet;
    auto v = [&] (int k) { return slotControl (type, k).fromUnit (p.get (slotControlParam (slot, k))); };
    auto b = [&] (int k) { return v (k) >= 0.5f; };
    auto i = [&] (int k) { return (int) std::lround (v (k)); };

    bool on = p.getBool (slotOnParam (slot));

    switch (type)
    {
        case SlotType::gate:
        {
            auto& g = *static_cast<GateBlock*> (blk);
            g.setThresholdDb (v (ctl::gateThreshold));
            // Am linken Anschlag ist das Gate aus.
            on = on && ! g.isOff();
            break;
        }

        case SlotType::comp:
        {
            auto& c = *static_cast<CompBlock*> (blk);
            c.setSustain (v (ctl::compSustain));
            c.setLevelDb (v (ctl::compLevel));
            break;
        }

        case SlotType::drive:
        {
            auto& d = *static_cast<DriveBlock*> (blk);
            d.setModel ((DriveModel) i (ctl::driveModel));
            d.setGainDb (v (ctl::driveGain));
            d.setLevelDb (v (ctl::driveLevel));
            d.setTone (v (ctl::driveTone));
            d.setBody (v (ctl::driveBody));
            break;
        }

        case SlotType::nam:
        {
            auto& n = *static_cast<NamBlock*> (blk);
            const auto role = rackUnit.liveRole (slot);
            n.setRole ((NamBlock::Role) (int) role);
            n.setInputDb (v (ctl::namGain));
            n.setLevelDb (v (ctl::namLevel));
            // Regler, die die Karte nicht zeigt, wirken nicht
            // (namControlShown): kein MIX beim Amp, PEDAL nimmt den
            // Eingangspegel immer aus der Datei.
            n.setMix (namControlShown (role, ctl::namMix) ? v (ctl::namMix) : 1.0f);
            n.setOutputMode (namControlShown (role, ctl::namNorm)
                                 ? (NamBlock::Output) (int) std::lround (v (ctl::namNorm))
                                 : NamBlock::Output::normalized);
            n.setToneStack (v (ctl::namBass), v (ctl::namMid), v (ctl::namTreble));
            break;
        }

        case SlotType::eq:
        {
            auto& e = *static_cast<EqBlock*> (blk);
            e.setMode ((EqMode) i (ctl::eqMode));
            e.setThreeBand (v (ctl::eq3Bass), v (ctl::eq3Mid), v (ctl::eq3Treble));
            for (int k = 0; k < 7; ++k) e.setBand7 (k, v (ctl::eq7Band0 + k));
            break;
        }

        case SlotType::ir:
            static_cast<CabBlock*> (blk)->setOutputDb (v (ctl::irOutput));
            break;

        case SlotType::chorus:
        {
            auto& c = *static_cast<ChorusBlock*> (blk);
            c.setRate (v (ctl::chorusRate));
            c.setDepth (v (ctl::chorusDepth));
            c.setMix (v (ctl::chorusMix));
            break;
        }

        case SlotType::flanger:
        {
            auto& f = *static_cast<FlangerBlock*> (blk);
            f.setRate (v (ctl::flangerRate));
            f.setDepth (v (ctl::flangerDepth));
            f.setFeedback (v (ctl::flangerFeedback));
            f.setManual (v (ctl::flangerManual));
            f.setMix (v (ctl::flangerMix));
            break;
        }

        case SlotType::phaser:
        {
            auto& f = *static_cast<PhaserBlock*> (blk);
            f.setRate (v (ctl::phaserRate));
            f.setDepth (v (ctl::phaserDepth));
            f.setMix (v (ctl::phaserMix));
            break;
        }

        case SlotType::tremolo:
        {
            auto& t = *static_cast<TremoloBlock*> (blk);
            t.setRate (v (ctl::tremoloRate));
            t.setDepth (v (ctl::tremoloDepth));
            t.setSync (b (ctl::tremoloSync), (NoteDiv) i (ctl::tremoloDiv));
            break;
        }

        case SlotType::delay:
        {
            auto& d = *static_cast<DelayBlock*> (blk);
            d.setType ((DelayType) i (ctl::delayType));
            d.setTimeMs (v (ctl::delayTime));
            d.setFeedback (v (ctl::delayFeedback));
            d.setMix (v (ctl::delayMix));
            d.setSync (b (ctl::delaySync), (NoteDiv) i (ctl::delayDiv));
            break;
        }

        case SlotType::reverb:
        {
            auto& r = *static_cast<ReverbBlock*> (blk);
            r.setType ((ReverbType) i (ctl::reverbType));
            r.setTimeSec (v (ctl::reverbTime));
            r.setSize (v (ctl::reverbSize));
            r.setBoing (v (ctl::reverbBoing));
            r.setTone (v (ctl::reverbTone));
            r.setMix (v (ctl::reverbMix));
            break;
        }

        default: break;
    }

    rackUnit.setEnabled (slot, on);
}

void Engine::applyParams()
{
    auto& p = paramSet;

    for (int s = 0; s < kNumSlots; ++s) applySlot (s);

    mix.dual       = p.getBool (ParamId::dualOn);
    mix.source     = (LaneSource) clamp (p.getInt (ParamId::laneBSource), 0, (int) LaneSource::count - 1);
    mix.splitAfter = p.getInt (ParamId::splitAfter);
    mix.mergeAfter = p.getInt (ParamId::mergeAfter);
    mix.levelDb[0] = p.get (ParamId::laneALevel);
    mix.levelDb[1] = p.get (ParamId::laneBLevel);
    mix.pan[0]     = p.get (ParamId::laneAPan);
    mix.pan[1]     = p.get (ParamId::laneBPan);
    mix.solo[0]    = p.getBool (ParamId::laneASolo);
    mix.solo[1]    = p.getBool (ParamId::laneBSolo);
    mix.invert[0]  = p.getBool (ParamId::laneAInvert);
    mix.invert[1]  = p.getBool (ParamId::laneBInvert);

    drumUnit.setMode ((DrumMode) p.getInt (ParamId::drumsMode));
    drumUnit.setPattern (p.getInt (ParamId::drumsPattern));
    drumUnit.setClickPattern (p.getInt (ParamId::drumsClickPattern));
    drumUnit.setBpm (p.get (ParamId::drumsBpm));
    drumUnit.setVolume (p.get (ParamId::drumsVolume));
    drumUnit.setSync (p.getBool (ParamId::syncDrums));

    looperUnit.setSync (p.getBool (ParamId::syncLooper));
    // Fußschalter der Karte. Der Looper muss davon wissen, um auszublenden
    // statt abzubrechen.
    looperUnit.setEnabled (isToolShown (Tool::looper) && p.getBool (ParamId::looperOn));

    // SPREAD erzeugt aus einer Spur zwei Seiten. Mit DUAL gibt es die schon,
    // dann ist SPREAD aus.
    spreadUnit.setMs (mix.dual ? Spread::kOffMs : p.get (ParamId::spreadMs));

    inGain .setTarget (dbToGain (p.get (ParamId::inGain)));
    outGain.setTarget (dbToGain (p.get (ParamId::outGain)));
    muteFade.setTarget (p.getBool (ParamId::mute) ? 0.0f : 1.0f);
}

void Engine::process (float* left, float* right, int numFrames)
{
    const auto t0 = std::chrono::steady_clock::now();

    applyParams();

    StereoBlock block { left, right, numFrames };
    const auto ctx = clock.context ((double) paramSet.get (ParamId::drumsBpm), numFrames);

    // Eingangspegel
    float peakIn = 0.0f;
    for (int i = 0; i < numFrames; ++i)
    {
        const float g = inGain.next();
        left[i]  *= g;
        right[i] *= g;
        peakIn = std::max (peakIn, std::max (std::abs (left[i]), std::abs (right[i])));
    }
    meterIn.store (ballIn.pushBlock (peakIn, numFrames), std::memory_order_relaxed);

    // Stimmgerät: direkt hinter dem Eingangspegel, funktioniert dadurch auch
    // bei MUTE.
    if ((int) tunerMono.size() < numFrames) tunerMono.resize ((size_t) numFrames);
    for (int i = 0; i < numFrames; ++i)
        tunerMono[(size_t) i] = 0.5f * (left[i] + right[i]);
    tunerUnit.push (tunerMono.data(), numFrames);

    // Slots in einer oder zwei Spuren
    rackUnit.process (block, ctx, mix);

    // Looper und Drums am Ende. Der Beat läuft in einen eigenen Puffer und wird
    // erst hinter dem Looper dazugemischt, damit er nicht mit aufgenommen wird.
    const bool beatLaeuft = isToolShown (Tool::drums)
                            && paramSet.getBool (ParamId::drumsOn);

    // Takt vor dem Beat ablesen: drumUnit.process schiebt die Phase über den
    // ganzen Block, der Looper braucht den Stand am Blockanfang. Mit Länge und
    // Phase kann eine Aufnahme auf der Eins beginnen.
    looperUnit.setExternalClock (beatLaeuft ? drumUnit.barSamples() : 0.0,
                                 beatLaeuft ? drumUnit.barPhase()   : 0.0);

    // Läuft auch ausgeschaltet weiter, solange ein Schlag ausklingt.
    drumUnit.setEnabled (beatLaeuft);
    const bool beatKlingtAus = beatLaeuft || drumUnit.tailActive();

    if (beatKlingtAus)
    {
        // Der Host darf größere Puffer schicken als angemeldet.
        if ((int) drumL.size() < numFrames)
        { drumL.resize ((size_t) numFrames, 0.0f); drumR.resize ((size_t) numFrames, 0.0f); }

        std::fill (drumL.begin(), drumL.begin() + numFrames, 0.0f);
        std::fill (drumR.begin(), drumR.begin() + numFrames, 0.0f);
        StereoBlock beat { drumL.data(), drumR.data(), numFrames };
        drumUnit.process (beat, ctx);
    }

    // Läuft auch ausgeschaltet weiter, solange etwas ausklingt (Blende,
    // Nachlauf einer gerade geschlossenen Aufnahme).
    if (isToolShown (Tool::looper)
        && (paramSet.getBool (ParamId::looperOn) || looperUnit.tailActive()))
        looperUnit.process (block, ctx);

    if (beatKlingtAus)
        for (int i = 0; i < numFrames; ++i) { left[i] += drumL[(size_t) i]; right[i] += drumR[(size_t) i]; }

    // Spread
    spreadUnit.process (block);

    // Ausgangspegel, dann Mute
    for (int i = 0; i < numFrames; ++i)
    {
        const float g = outGain.next() * muteFade.next();
        left[i]  *= g;
        right[i] *= g;
    }

    // globaler EQ
    eqOut.process (block);

    float peakOut = 0.0f;
    for (int i = 0; i < numFrames; ++i)
        peakOut = std::max (peakOut, std::max (std::abs (left[i]), std::abs (right[i])));
    meterOut.store (ballOut.pushBlock (peakOut, numFrames), std::memory_order_relaxed);

    // Recorder nimmt den Ausgang auf
    if (isToolShown (Tool::recorder) && paramSet.getBool (ParamId::recorderOn))
        recorderUnit.process (block, ctx);

    clock.advance (numFrames);

    // CPU-Last
    const auto t1 = std::chrono::steady_clock::now();
    const double spent = std::chrono::duration<double> (t1 - t0).count();
    const double available = (double) numFrames / srate;
    if (available > 0.0)
        cpu.store (cpuBallistic.push ((float) (spent / available)), std::memory_order_relaxed);
}

} // namespace nmp
