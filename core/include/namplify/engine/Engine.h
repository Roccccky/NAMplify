#pragma once
// Die gesamte Signalkette.
//
// Eingang → Eingangspegel → [Slots: Spur A, bei DUAL auch B → Mixer] → Looper →
// Drums → Spread → Ausgangspegel → MUTE → globaler EQ → Ausgang
//
// Das Stimmgerät greift direkt hinter dem Eingangspegel ab und funktioniert
// daher auch bei MUTE. MUTE sitzt am Ende und schaltet auch Beat und Loops
// stumm. Amp, Hall und Delay laufen weiter und klingen normal aus.

#include "Params.h"
#include "Rack.h"
#include "Transport.h"
#include "Tuner.h"
#include "Looper.h"
#include "Recorder.h"
#include "DrumMachine.h"
#include "Spread.h"
#include "GlobalEq.h"

#include <array>
#include <chrono>
#include <atomic>
#include <vector>

namespace nmp
{

/// Feste Werkzeuge am Ende der Kette, nicht in den Slots.
enum class Tool : int { looper = 0, drums, recorder, count };
inline constexpr int kNumTools = (int) Tool::count;
const char* toolKey (Tool);     ///< wird in Presets gespeichert, nicht ändern
const char* toolName (Tool);    ///< Name auf Kachel und Karte
const char* toolColour (Tool);  ///< siehe slotColour

class Engine
{
public:
    Engine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /// Audio-Thread. Stereo, in place.
    void process (float* left, float* right, int numFrames);

    /// Message-Thread. Räumt auf, was der Audio-Thread abgelegt hat, und
    /// bereitet vor, was er als Nächstes braucht.
    void serviceBackground();

    // Zugriff
    ParamSet&    params()    { return paramSet; }
    Transport&   transport() { return clock; }
    Tuner&       tuner()     { return tunerUnit; }
    Looper&      looper()    { return looperUnit; }
    DrumMachine& drums()     { return drumUnit; }
    Recorder&    recorder()  { return recorderUnit; }
    GlobalEq&    globalEq()  { return eqOut; }
    Rack&        rack()      { return rackUnit; }

    // Slots
    void setLayout (const Layout& l) { rackUnit.requestLayout (l); }
    const Layout& layout() const     { return rackUnit.layout(); }
    NamBlock* namFor (int slot) const { return rackUnit.nam (slot); }
    CabBlock* irFor (int slot) const  { return rackUnit.ir (slot); }

    /// Setzt die Regler eines Slots auf die Werkswerte seines Typs und schaltet
    /// ihn ein. Nur für Tests und Tools ohne Host, im Plugin laufen die Werte
    /// über die Host-Parameter.
    void resetSlotControls (int slot, SlotType);

    // Werkzeuge am Ende
    void setToolShown (Tool, bool);
    bool isToolShown (Tool t) const { return toolShown[(size_t) t].load (std::memory_order_relaxed); }

    // Engine-Einstellungen
    void  setEngineLite (bool lite);
    bool  engineLite() const { return liteMode; }
    /// Hat eines der geladenen Captures eine LITE-Variante?
    bool  anyModelHasLite();

    /// Oversampling-Faktor der Captures (1, 2, 4). Gilt ab dem nächsten Laden
    /// (NamModel::load). Wird vom Lade-Thread gelesen.
    void  setCaptureOversampling (int factor) { captureOs.store (factor >= 4 ? 4 : (factor >= 2 ? 2 : 1)); }
    int   captureOversampling() const { return captureOs.load(); }

    void  setInterfaceDbu (float dbu);
    float interfaceDbu() const { return refDbu; }

    // Anzeigen
    float inputLevel()  const { return meterIn.load (std::memory_order_relaxed); }
    float outputLevel() const { return meterOut.load (std::memory_order_relaxed); }
    float cpuLoad()     const { return cpu.load (std::memory_order_relaxed); }
    int   latencySamples() const { return rackUnit.latencySamples(); }

    double sampleRate() const { return srate; }
    int    maxBlockSize() const { return maxBlock; }

private:
    void applyParams();
    void applySlot (int slot);

    ParamSet paramSet;

    double srate = 48000.0;
    int    maxBlock = 512;
    int    looperLatency = -1;
    std::atomic<int> captureOs { 1 };

    Rack       rackUnit;
    RackMix    mix;

    // Werkzeuge am Ende
    Looper        looperUnit;
    DrumMachine   drumUnit;
    Recorder      recorderUnit;
    std::array<std::atomic<bool>, kNumTools> toolShown {};

    Spread     spreadUnit;
    GlobalEq   eqOut;
    Tuner      tunerUnit;
    Transport  clock;

    LinearSmoothed inGain, outGain, muteFade;
    BallisticMeter ballIn, ballOut;
    std::vector<float> tunerMono;
    /// Eigener Puffer für den Beat. Er wird erst hinter dem Looper
    /// dazugemischt, damit er nicht mit aufgenommen wird.
    std::vector<float> drumL, drumR;

    std::atomic<float> meterIn { 0.0f }, meterOut { 0.0f }, cpu { 0.0f };
    BallisticMeter cpuBallistic;

    float refDbu = 12.0f;
    bool  liteMode = false;
};

} // namespace nmp
