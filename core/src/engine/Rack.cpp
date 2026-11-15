#include "namplify/engine/Rack.h"
#include <algorithm>
#include <type_traits>

namespace nmp
{

namespace
{
    /// Maximale Latenz eines Slots oder einer Spur ohne Allokation im
    /// Audio-Thread. Die aufwendigste Kette heute (zwei Drives à 16, vier
    /// Captures mit 4× à 5) liegt weit darunter.
    constexpr int kMaxLatency = 2048;
}

Rack::Rack()
{
    live.bind.fill (nullptr);
    requestedBind.fill (nullptr);
    enabled.fill (false);
}

void Rack::prepare (const PrepareInfo& info)
{
    prepared = info;

    // Alle Blöcke werden vorbereitet, belegt oder nicht, damit im Audio-Thread
    // nichts alloziert wird.
    auto prep = [&info] (auto& pool) { for (auto& b : pool) b.prepare (info); };
    prep (gates); prep (comps); prep (drives); prep (nams); prep (eqs); prep (irs);
    prep (choruses); prep (flangers); prep (phasers); prep (tremolos); prep (delays); prep (reverbs);

    const auto n = (size_t) info.maxBlockSize;
    dryL.assign (n, 0.0f); dryR.assign (n, 0.0f);
    bL.assign (n, 0.0f);   bR.assign (n, 0.0f);

    for (auto& s : state)
    {
        s.fade.reset (info.sampleRate, 0.015);
        s.fade.setCurrentAndTarget (0.0f);
        // Platz vorab reservieren, prepare(0) behält die Kapazität.
        s.dryDelayL.prepare (kMaxLatency); s.dryDelayL.prepare (0);
        s.dryDelayR.prepare (kMaxLatency); s.dryDelayR.prepare (0);
        s.builtLatency = -1;
        s.wasOff = true;
    }

    for (int l = 0; l < kNumLanes; ++l)
    {
        alignL[l].prepare (kMaxLatency); alignL[l].prepare (0);
        alignR[l].prepare (kMaxLatency); alignR[l].prepare (0);
        builtAlign[l] = 0;
        gainL[l].reset (info.sampleRate, 0.02);
        gainR[l].reset (info.sampleRate, 0.02);
        gainL[l].setCurrentAndTarget (0.5f);
        gainR[l].setCurrentAndTarget (0.5f);
    }

    dip.reset (info.sampleRate, 0.004);
    dip.setCurrentAndTarget (1.0f);
    dipState = Dip::none;

    // Ein Aufbau, der vor dem ersten prepare kam, gilt jetzt sofort.
    if (auto* p = pending.exchange (nullptr)) applySwap (*p);

    reset();
}

void Rack::reset()
{
    auto rst = [] (auto& pool) { for (auto& b : pool) b.reset(); };
    rst (gates); rst (comps); rst (drives); rst (nams); rst (eqs); rst (irs);
    rst (choruses); rst (flangers); rst (phasers); rst (tremolos); rst (delays); rst (reverbs);

    for (auto& s : state) { s.dryDelayL.reset(); s.dryDelayR.reset(); }
    for (int l = 0; l < kNumLanes; ++l) { alignL[l].reset(); alignR[l].reset(); }
    dip.setCurrentAndTarget (1.0f);
    dipState = Dip::none;
}

Block* Rack::takeFree (SlotType t, const std::array<Block*, kNumSlots>& inUse)
{
    auto pick = [&inUse] (auto& pool) -> Block*
    {
        for (auto& b : pool)
        {
            Block* p = &b;
            if (std::find (inUse.begin(), inUse.end(), p) == inUse.end()) return p;
        }
        return nullptr;
    };

    switch (t)
    {
        case SlotType::gate:    return pick (gates);
        case SlotType::comp:    return pick (comps);
        case SlotType::drive:   return pick (drives);
        case SlotType::nam:     return pick (nams);
        case SlotType::eq:      return pick (eqs);
        case SlotType::ir:      return pick (irs);
        case SlotType::chorus:  return pick (choruses);
        case SlotType::flanger: return pick (flangers);
        case SlotType::phaser:  return pick (phasers);
        case SlotType::tremolo: return pick (tremolos);
        case SlotType::delay:   return pick (delays);
        case SlotType::reverb:  return pick (reverbs);
        default:                return nullptr;
    }
}

void Rack::requestLayout (const Layout& in)
{
    Layout l = in;
    l.sanitise();

    // Gleicher Typ, gleicher Block.
    std::array<Block*, kNumSlots> bind {};
    bind.fill (nullptr);
    for (int s = 0; s < kNumSlots; ++s)
        if (l.type[(size_t) s] != SlotType::empty && l.type[(size_t) s] == requested.type[(size_t) s])
            bind[(size_t) s] = requestedBind[(size_t) s];

    for (int s = 0; s < kNumSlots; ++s)
        if (l.type[(size_t) s] != SlotType::empty && bind[(size_t) s] == nullptr)
        {
            bind[(size_t) s] = takeFree (l.type[(size_t) s], bind);
            // Kann nach sanitise nicht fehlen. Falls doch, bleibt der Slot
            // leer.
            if (bind[(size_t) s] == nullptr) l.type[(size_t) s] = SlotType::empty;
        }

    if (bind != requestedBind || l.lane != requested.lane || l.length != requested.length
        || l.role != requested.role || l.type != requested.type)
    {
        requested = l;
        requestedBind = bind;

        auto& box = mailbox[(size_t) mailboxNext];
        box.layout = l;
        box.bind   = bind;
        pending.store (&box, std::memory_order_release);
        mailboxNext = (mailboxNext + 1) % (int) mailbox.size();
    }
}

Block* Rack::block (int slot) const
{
    return slot >= 0 && slot < kNumSlots ? requestedBind[(size_t) slot] : nullptr;
}

NamBlock* Rack::nam (int slot) const
{
    if (slot < 0 || slot >= kNumSlots || requested.type[(size_t) slot] != SlotType::nam) return nullptr;
    return static_cast<NamBlock*> (requestedBind[(size_t) slot]);
}

CabBlock* Rack::ir (int slot) const
{
    if (slot < 0 || slot >= kNumSlots || requested.type[(size_t) slot] != SlotType::ir) return nullptr;
    return static_cast<CabBlock*> (requestedBind[(size_t) slot]);
}

void Rack::applySwap (const Bound& b)
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        if (b.bind[(size_t) s] == live.bind[(size_t) s]) continue;

        // Neuer Block auf diesem Slot: blendet von null ein und startet leer.
        auto& st = state[(size_t) s];
        st.fade.setCurrentAndTarget (0.0f);
        st.wasOff = true;
        st.builtLatency = -1;
        st.dryDelayL.reset();
        st.dryDelayR.reset();
    }
    live = b;
}

int Rack::laneLatency (int lane, int from, int to) const
{
    int total = 0;
    const int len = std::min (to, live.layout.length[(size_t) lane]);
    for (int p = std::max (0, from); p < len; ++p)
    {
        const int s = live.layout.lane[(size_t) lane][(size_t) p];
        if (s >= 0 && live.bind[(size_t) s] != nullptr)
            total += live.bind[(size_t) s]->latencySamples();
    }
    return total;
}

void Rack::runLane (int lane, StereoBlock& io, const BlockContext& ctx, int from, int to)
{
    const int n = io.numFrames;
    const int len = std::min (to, live.layout.length[(size_t) lane]);

    for (int p = std::max (0, from); p < len; ++p)
    {
        const int slot = live.layout.lane[(size_t) lane][(size_t) p];
        if (slot < 0) continue;
        Block* blk = live.bind[(size_t) slot];
        if (blk == nullptr) continue;
        auto& s = state[(size_t) slot];

        // Die Latenz des Blocks ist fest, der trockene Weg bekommt dieselbe.
        const int lat = std::min (blk->latencySamples(), kMaxLatency);
        if (lat != s.builtLatency)
        {
            s.dryDelayL.prepare (lat);
            s.dryDelayR.prepare (lat);
            s.builtLatency = lat;
        }

        const bool on = enabled[(size_t) slot];
        s.fade.setTarget (on ? 1.0f : 0.0f);

        const bool fullyOff = ! s.fade.isSmoothing() && s.fade.getCurrent() <= 0.0f;
        const bool fullyOn  = ! s.fade.isSmoothing() && s.fade.getCurrent() >= 1.0f;

        // Der trockene Weg läuft immer mit, auch wenn der Block an ist. Sonst
        // enthielte er beim Ausschalten alte Werte.
        std::copy (io.left,  io.left  + n, dryL.begin());
        std::copy (io.right, io.right + n, dryR.begin());
        s.dryDelayL.processBlock (dryL.data(), n);
        s.dryDelayR.processBlock (dryR.data(), n);

        if (fullyOff)
        {
            std::copy (dryL.begin(), dryL.begin() + n, io.left);
            std::copy (dryR.begin(), dryR.begin() + n, io.right);
            s.wasOff = true;
            continue;
        }

        if (s.wasOff && on)
        {
            // Gerade eingeschaltet: alten Inhalt verwerfen.
            blk->onEnable();
            s.wasOff = false;
        }

        blk->process (io, ctx);
        if (fullyOn) continue;

        for (int i = 0; i < n; ++i)
        {
            const float t = s.fade.next();
            io.left[i]  = io.left[i]  * t + dryL[(size_t) i] * (1.0f - t);
            io.right[i] = io.right[i] * t + dryR[(size_t) i] * (1.0f - t);
        }
    }
}

void Rack::process (StereoBlock& io, const BlockContext& ctx, const RackMix& mixIn)
{
    const int n = io.numFrames;
    if ((int) dryL.size() < n)
    {
        dryL.resize ((size_t) n); dryR.resize ((size_t) n);
        bL.resize ((size_t) n);   bR.resize ((size_t) n);
    }

    // Absenkung bei neuem Aufbau oder geändertem Routing. DUAL an/aus, Quelle
    // von B und Abzweigposition werden wie ein Umbau behandelt und im Tiefpunkt
    // getauscht.
    static_assert (std::is_trivially_copyable_v<RackMix>);
    const bool routeChanged = mixIn.dual != route.dual
                           || (mixIn.dual && (mixIn.source != route.source
                                              || (mixIn.source == LaneSource::split
                                                  && (mixIn.splitAfter != route.splitAfter
                                                      || mixIn.mergeAfter != route.mergeAfter))));

    if (dipState == Dip::none
        && (pending.load (std::memory_order_acquire) != nullptr || routeChanged))
    {
        dipState = Dip::down;
        dip.setTarget (0.0f);
    }

    if (dipState == Dip::down && ! dip.isSmoothing())
    {
        if (auto* p = pending.exchange (nullptr, std::memory_order_acq_rel)) applySwap (*p);
        route = mixIn;
        // Neu zugeschaltete Spur B startet leer.
        if (route.dual) for (int l = 0; l < kNumLanes; ++l) { alignL[l].reset(); alignR[l].reset(); }
        dipState = Dip::up;
        dip.setTarget (1.0f);
    }
    else if (dipState == Dip::up && ! dip.isSmoothing())
    {
        dipState = Dip::none;
    }

    // Pegel, Pan, Solo und Phase gelten sofort (geglättet), nur das Routing
    // wartet auf den Tiefpunkt.
    RackMix mix = mixIn;
    mix.dual = route.dual;
    mix.source = route.source;
    mix.splitAfter = route.splitAfter;
    mix.mergeAfter = route.mergeAfter;

    const int lenA = live.layout.length[0];
    const int lenB = live.layout.length[1];

    if (! mix.dual)
    {
        runLane (0, io, ctx, 0, lenA);
        reportedLatency.store (laneLatency (0, 0, lenA), std::memory_order_relaxed);
    }
    else
    {
        StereoBlock b { bL.data(), bR.data(), n };

        // Spur A in drei Teilen: Kopf vor dem Abzweig (für beide), paralleler
        // Teil neben Spur B, Tail nach dem Mixer (für beide). Ganz parallel und
        // STEREO: kein Kopf, kein Tail.
        int split = 0, merge = lenA;
        if (mix.source == LaneSource::split)
        {
            split = clamp (mix.splitAfter, 0, lenA);
            merge = clamp (mix.mergeAfter, split, lenA);
        }

        runLane (0, io, ctx, 0, split);

        switch (mix.source)
        {
            case LaneSource::stereo:
                // A bekommt den linken Eingang, B den rechten, jeweils auf
                // beiden Seiten.
                std::copy (io.right, io.right + n, bL.begin());
                std::copy (io.right, io.right + n, bR.begin());
                std::copy (io.left,  io.left  + n, io.right);
                break;

            case LaneSource::split:
            case LaneSource::input:
            default:
                std::copy (io.left,  io.left  + n, bL.begin());
                std::copy (io.right, io.right + n, bR.begin());
                break;
        }

        runLane (0, io, ctx, split, merge);
        runLane (1, b,  ctx, 0, lenB);

        // beide Spuren auf dieselbe Latenz bringen
        const int latA = laneLatency (0, split, merge);
        const int latB = laneLatency (1, 0, lenB);
        const int latMax = std::min (std::max (latA, latB), kMaxLatency);
        const int pad[kNumLanes] { latMax - latA, latMax - latB };

        StereoBlock* lanes[kNumLanes] { &io, &b };
        for (int l = 0; l < kNumLanes; ++l)
        {
            const int d = clamp (pad[l], 0, kMaxLatency);
            if (d != builtAlign[l])
            {
                alignL[l].prepare (d);
                alignR[l].prepare (d);
                builtAlign[l] = d;
            }
            alignL[l].processBlock (lanes[l]->left,  n);
            alignR[l].processBlock (lanes[l]->right, n);
        }

        reportedLatency.store (laneLatency (0, 0, split) + latMax + laneLatency (0, merge, lenA),
                               std::memory_order_relaxed);

        // Mixer. Pan als Gewichtung: in der Mitte je ½, ganz außen 1 und 0.
        // Zwei gleiche Spuren in der Mitte sind so laut wie eine. SOLO spielt
        // eine Spur mittig und voll, damit sie nicht 6 dB leiser ist.
        const bool anySolo = mix.solo[0] || mix.solo[1];
        for (int l = 0; l < kNumLanes; ++l)
        {
            const bool audible = ! anySolo || mix.solo[l];
            const bool alone   = anySolo && mix.solo[l] && ! mix.solo[1 - l];
            const float level  = audible ? dbToGain (mix.levelDb[l]) * (mix.invert[l] ? -1.0f : 1.0f) : 0.0f;
            const float p      = clamp (mix.pan[l], -1.0f, 1.0f);
            const float wl     = alone ? 1.0f : 0.5f * (1.0f - p);
            const float wr     = alone ? 1.0f : 0.5f * (1.0f + p);
            gainL[l].setTarget (level * wl);
            gainR[l].setTarget (level * wr);
        }

        for (int i = 0; i < n; ++i)
        {
            const float aL = gainL[0].next(), aR = gainR[0].next();
            const float cL = gainL[1].next(), cR = gainR[1].next();
            const float l = io.left[i] * aL + bL[(size_t) i] * cL;
            const float r = io.right[i] * aR + bR[(size_t) i] * cR;
            io.left[i] = l;
            io.right[i] = r;
        }

        // Tail nach dem Zusammenführen, für beide Spuren
        runLane (0, io, ctx, merge, lenA);
    }

    // Absenkung anwenden
    if (dipState != Dip::none || dip.getCurrent() < 1.0f)
        for (int i = 0; i < n; ++i)
        {
            const float g = dip.next();
            io.left[i]  *= g;
            io.right[i] *= g;
        }
}

} // namespace nmp
