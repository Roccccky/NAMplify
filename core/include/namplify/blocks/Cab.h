#pragma once
// CAB: Faltung mit einer Impulsantwort. Mono, das Ergebnis liegt auf beiden
// Kanälen. Ein IR-Wechsel wird übergeblendet.

#include "Block.h"
#include "../dsp/Convolver.h"
#include "../util/Smoothed.h"
#include <memory>
#include <atomic>
#include <vector>

namespace nmp
{

class CabBlock : public Block
{
public:
    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    void setOutputDb (float db) { outputDb = clamp (db, -12.0f, 12.0f); }

    /// Neue IR vorbereiten. Alloziert, daher nur vom Lade-Thread aufrufen. Der
    /// Wechsel selbst passiert im Audio-Thread mit Überblendung.
    void prepareIr (const float* samples, int numSamples);

    /// Audio-Thread: vorbereitete IR übernehmen, falls vorhanden.
    void commitPendingIr();

    /// Message-Thread: abgelöste IR freigeben.
    void collectRetired();

    bool hasIr() const { return active != nullptr && active->hasIr(); }
    int  irLength() const { return active ? active->irLength() : 0; }
    int  latencySamples() const override { return 0; }

private:
    double sampleRate = 48000.0;
    int    maxBlock = 512;
    float  outputDb = 0.0f;

    std::unique_ptr<Convolver> active, incoming, retired;
    std::atomic<Convolver*>    pending { nullptr };
    std::unique_ptr<Convolver> pendingOwner;

    LinearSmoothed swapFade;   // 0 = alt, 1 = neu
    bool swapping = false;

    std::vector<float> mono, monoOld;
    LinearSmoothed outGain;
};

} // namespace nmp
