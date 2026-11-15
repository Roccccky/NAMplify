#pragma once
// Ein Capture auf einem NAM-Slot. Bis zu vier laufen gleichzeitig
// (slotTypeLimit). Die Rolle (AMP, PEDAL, OUTBOARD, FULL RIG) bestimmt nur, was
// außen herum passiert. Verarbeitung mono, Ergebnis auf beiden Kanälen.
//
// Geladen wird im Lade-Thread. Der Audio-Thread übernimmt das fertige Modell
// und blendet vom alten zum neuen über.

#include "Block.h"
#include "../dsp/Biquad.h"
#include "../dsp/Convolver.h"
#include "../model/NamModel.h"
#include "../dsp/AllpassHalfband.h"
#include "../util/Smoothed.h"
#include <atomic>
#include <memory>
#include <vector>

namespace nmp
{

class NamBlock : public Block
{
public:
    /// Gleiche Reihenfolge wie NamRole in Slots.h.
    enum class Role { amp, pedal, outboard, fullrig, space };

    NamBlock() = default;
    explicit NamBlock (Role r) : role (r) {}

    /// Audio-Thread, pro Puffer. Die Rolle kann wechseln, ohne dass das Capture
    /// wechselt.
    void setRole (Role r) { role = r; }
    Role getRole() const  { return role; }

    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;
    int  latencySamples() const override;
    void onEnable() override;

    // Lade-Thread
    void publishModel (std::unique_ptr<NamModel>);
    void publishIr (std::vector<float> samples);   ///< IR auf einem NAM-Slot (OUTBOARD)
    void requestClear();

    // Message-Thread
    void collectRetired();

    /// Übernimmt die Netzgröße aus setLite auch für ein Capture, das beim
    /// Umschalten noch auf seinen ersten Audio-Block wartet.
    void reconcileLite();

    /// Infos zum geladenen Capture. Kommen aus einer Kopie, die der Lade-Thread
    /// beim Übergeben ablegt, damit die Anzeige auch ohne laufendes Audio
    /// stimmt.
    const NamModelInfo* modelInfo() const { return published ? &publishedInfo : nullptr; }
    bool hasModel() const { return published; }
    bool hasIr() const    { return activeIr != nullptr && activeIr->hasIr(); }

    // Regler
    /// GAIN vor dem Netz (zusätzlich zum Eingangspegel aus der Datei).
    void setInputDb (float db) { inputDb = clamp (db, -24.0f, 24.0f); }
    void setLevelDb (float db) { levelDb = clamp (db, -12.0f, 12.0f); }
    void setMix (float m)      { mix = clamp (m, 0.0f, 1.0f); }
    /// Ausgangspegel des Captures: roh, kalibriert oder normalisiert.
    enum class Output { raw, calibrated, normalized };
    void setOutputMode (Output m) { outputMode = m; }
    void setToneStack (float bassDb, float midDb, float trebleDb);

    /// Referenzpegel des Interfaces in dBu.
    void setInterfaceDbu (float dbu) { interfaceDbu = dbu; }
    void setLite (bool lite);

    /// Messung beim Laden und der daraus berechnete Ausgleich, für die
    /// Einstellungen.
    LoudnessMeasurement measurement() const;

private:
    void commitPending();
    void rebuildToneStack();

    Role  role = Role::amp;
    double sampleRate = 48000.0;
    int    maxBlock = 512;

    std::unique_ptr<NamModel> active, outgoing, retired;
    std::unique_ptr<NamModel> pendingOwner;
    std::atomic<NamModel*>    pendingModel { nullptr };

    std::unique_ptr<Convolver> activeIr, retiredIr, pendingIrOwner;
    std::atomic<Convolver*>    pendingIr { nullptr };

    std::atomic<bool> clearRequested { false };

    NamModelInfo publishedInfo;
    bool         published = false;

    LinearSmoothed swapFade;
    bool swapping = false;

    std::vector<float> mono, monoOld;

    float inputDb = 0.0f, levelDb = 0.0f, mix = 1.0f, interfaceDbu = 12.0f;
    Output outputMode = Output::normalized;
    bool  liteWanted = false;

    float toneBassDb = 0.0f, toneMidDb = 0.0f, toneTrebleDb = 0.0f;
    bool  toneDirty = true;
    Biquad toneBass, toneMid, toneTreble;

    /// postGain ist der Lautheitsausgleich und gilt nur für das nasse Signal.
    /// volGain (VOL) wirkt nach dem Mix.
    LinearSmoothed preGain, postGain, volGain, mixSm;

    /// Bei Oversampling kommt das Netzsignal einige Samples später heraus
    /// (Halbband, minimalphasig). Das trockene Signal läuft durch dieselben
    /// Filter, damit der Mix keinen Kammfilter ergibt.
    /// [Faktor 2 | 4][links | rechts], vorab angelegt, damit beim Faktorwechsel
    /// im Audio-Thread nichts alloziert wird.
    CaptureOversampler dryAlign[2][2];
    std::vector<float> dryFast;
    void alignDry (StereoBlock&, int factor);
};

} // namespace nmp
