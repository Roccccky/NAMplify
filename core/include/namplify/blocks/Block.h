#pragma once
// Gemeinsame Basis aller Blöcke in der Kette.

#include "../util/Types.h"

namespace nmp
{

/// Transport und Tempo für einen Block.
struct BlockContext
{
    double bpm            = 120.0;  ///< Tempo in BPM, geglättet
    bool   clockRunning   = false;  ///< externe Clock aktiv?
    double ppqPosition    = 0.0;    ///< Position in Vierteln
    bool   transportPlaying = false;
    int    numFramesTotal = 0;      ///< Puffergröße des Hosts (nur Anzeige)
};

class Block
{
public:
    virtual ~Block() = default;

    virtual void prepare (const PrepareInfo&) = 0;
    virtual void reset() = 0;
    virtual void process (StereoBlock&, const BlockContext&) = 0;

    /// Feste Latenz des Blocks, auch im Bypass. Sie darf sich beim Umschalten
    /// nicht ändern, sonst verschiebt sich die Kette.
    virtual int latencySamples() const { return 0; }

    /// Beim Einschalten aus dem Zustand "aus". Standard: Zustand löschen, damit
    /// z. B. ein Delay keine alten Reste abspielt.
    virtual void onEnable() { reset(); }
};

} // namespace nmp
