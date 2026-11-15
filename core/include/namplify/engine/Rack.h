#pragma once
// Kette aus freien Slots in einer oder zwei Spuren.
//
// - Umbauten knacken nicht: der Ausgang wird über 4 ms abgesenkt, der Aufbau im
// Tiefpunkt getauscht und wieder eingeblendet.
// - Ein ausgeschalteter Block behält seine Latenz. Sein trockener Weg läuft
// durch eine gleich lange Verzögerung.
// - Beide Spuren werden auf dieselbe Latenz gebracht, sonst entsteht beim
// Zusammenmischen ein Kammfilter.
// - Alle Blöcke sind vorab angelegt (slotTypeLimit pro Typ). Im Audio-Thread
// wird nichts alloziert.

#include "Slots.h"
#include "../blocks/Dynamics.h"
#include "../blocks/Drive.h"
#include "../blocks/Eq.h"
#include "../blocks/Cab.h"
#include "../blocks/NamBlock.h"
#include "../blocks/Modulation.h"
#include "../blocks/Delay.h"
#include "../blocks/Reverb.h"
#include "../dsp/Oversampler.h"
#include "../util/Smoothed.h"
#include <array>
#include <atomic>
#include <vector>

namespace nmp
{

/// Mixer am Ende der beiden Spuren, pro Puffer gesetzt.
struct RackMix
{
    bool       dual = false;
    LaneSource source = LaneSource::input;
    int        splitAfter = 0;          ///< 0 = vor der ersten Stelle von A
    int        mergeAfter = kMaxLaneSlots;  ///< ab hier läuft A als Tail; ≥ Länge = kein Tail
    float      levelDb[kNumLanes] { 0.0f, 0.0f };
    float      pan[kNumLanes]     { 0.0f, 0.0f };   ///< −1 links … +1 rechts
    bool       solo[kNumLanes]    { false, false };
    bool       invert[kNumLanes]  { false, false };
};

class Rack
{
public:
    Rack();

    void prepare (const PrepareInfo&);
    void reset();

    // Message-Thread

    /// Übergibt einen neuen Aufbau. Er wird geprüft (Layout::sanitise), jeder
    /// belegte Slot bekommt einen Block seines Typs, getauscht wird im nächsten
    /// Tiefpunkt im Audio-Thread.
    ///
    /// Ein Slot, dessen Typ gleich bleibt, behält seinen Block samt geladenem
    /// Capture und Hallfahne.
    void requestLayout (const Layout&);

    /// Der zuletzt übergebene Aufbau (Stand Message-Thread).
    const Layout& layout() const { return requested; }

    /// Block eines Slots, nullptr wenn leer.
    Block*    block (int slot) const;
    NamBlock* nam (int slot) const;
    CabBlock* ir (int slot) const;

    /// Alle NAM-Blöcke, belegt oder nicht. Für globale Einstellungen (LITE,
    /// Referenzpegel).
    std::array<NamBlock, 4>& namPool() { return nams; }
    std::array<CabBlock, 4>& irPool()  { return irs; }

    /// Latenz, die beim nächsten Tausch gilt bzw. gerade gilt.
    int latencySamples() const { return reportedLatency.load (std::memory_order_relaxed); }

    // Audio-Thread

    /// An/Aus, pro Puffer aus den Parametern.
    void setEnabled (int slot, bool on) { if (slot >= 0 && slot < kNumSlots) enabled[(size_t) slot] = on; }

    /// Der Block, mit dem der Audio-Thread gerade rechnet (für die Regler).
    Block* liveBlock (int slot) const { return slot >= 0 && slot < kNumSlots ? live.bind[(size_t) slot] : nullptr; }
    SlotType liveType (int slot) const { return slot >= 0 && slot < kNumSlots ? live.layout.type[(size_t) slot] : SlotType::empty; }
    NamRole  liveRole (int slot) const { return slot >= 0 && slot < kNumSlots ? live.layout.role[(size_t) slot] : NamRole::amp; }

    void process (StereoBlock&, const BlockContext&, const RackMix&);

private:
    struct Bound
    {
        Layout layout;
        std::array<Block*, kNumSlots> bind {};
    };

    /// Zustand pro Slot, nicht pro Block: Blende, trockener Weg.
    struct SlotState
    {
        LinearSmoothed fade;
        PureDelay dryDelayL, dryDelayR;
        int  builtLatency = -1;
        bool wasOff = true;
    };

    Block* takeFree (SlotType, const std::array<Block*, kNumSlots>& inUse);
    /// Rechnet die Stellen from bis vor to einer Spur.
    void   runLane (int lane, StereoBlock&, const BlockContext&, int from, int to);
    int    laneLatency (int lane, int from, int to) const;
    void   applySwap (const Bound&);

    // Blöcke, vorab angelegt
    std::array<GateBlock, 2>    gates;
    std::array<CompBlock, 2>    comps;
    std::array<DriveBlock, 2>   drives;
    std::array<NamBlock, 4>     nams;
    std::array<EqBlock, 2>      eqs;
    std::array<CabBlock, 4>     irs;
    std::array<ChorusBlock, 2>  choruses;
    std::array<FlangerBlock, 2> flangers;
    std::array<PhaserBlock, 2>  phasers;
    std::array<TremoloBlock, 2> tremolos;
    std::array<DelayBlock, 2>   delays;
    std::array<ReverbBlock, 2>  reverbs;

    PrepareInfo prepared;

    // Message-Thread
    Layout requested;
    std::array<Block*, kNumSlots> requestedBind {};

    /// Ringpuffer mit vier Fächern. Der Message-Thread schreibt ins nächste und
    /// setzt den Zeiger, der Audio-Thread nimmt im Tiefpunkt das neueste.
    std::array<Bound, 4> mailbox;
    int mailboxNext = 0;
    std::atomic<Bound*> pending { nullptr };

    // Audio-Thread
    Bound live;
    std::array<bool, kNumSlots> enabled {};
    std::array<SlotState, kNumSlots> state;

    enum class Dip { none, down, up };
    Dip  dipState = Dip::none;
    LinearSmoothed dip;

    std::vector<float> dryL, dryR, bL, bR;
    PureDelay alignL[kNumLanes], alignR[kNumLanes];
    int builtAlign[kNumLanes] { -1, -1 };

    LinearSmoothed gainL[kNumLanes], gainR[kNumLanes];

    /// Aktuelles Routing der Spuren, wechselt nur im Tiefpunkt.
    RackMix route;

    std::atomic<int> reportedLatency { 0 };
};

} // namespace nmp
