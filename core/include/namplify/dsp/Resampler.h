#pragma once
// Samplerate-Konvertierung.
//
// Der NAM-Kern rechnet nicht selbst um: GetExpectedSampleRate() bleibt die
// Trainingsrate. Ohne Umrechnung wäre ein 44,1-kHz-Capture an einem
// 48-kHz-Interface um etwa 1,4 Halbtöne verstimmt.

#include "../util/Types.h"
#include "Window.h"
#include <cmath>
#include <vector>

namespace nmp
{

/// Umrechnung zwischen zwei beliebigen Raten. Sinc-Kern mit Kaiser-Fenster,
/// Polyphasentabelle mit Interpolation.
class SincResampler
{
public:
    static constexpr int kTaps   = 32;    // Kernlänge
    static constexpr int kPhases = 512;   // Stützstellen je Abtastintervall

    void prepare (double srIn, double srOut, int maxInputFrames);
    void reset();

    bool isPassthrough() const { return passthrough; }

    /// Latenz in Ausgangs-Samples.
    int latencyOut() const { return passthrough ? 0 : (int) std::ceil (kTaps * 0.5 / step); }

    /// Maximale Anzahl Ausgangswerte für n Eingangswerte.
    int maxOutputFor (int n) const;

    /// Wandelt n Werte um und schreibt nach out. Rückgabe: Anzahl erzeugter
    /// Werte.
    int process (const float* in, int n, float* out);

private:
    void buildTable (double cutoffRatio);

    std::vector<float> table;      // kPhases+1 Phasen × kTaps
    std::vector<float> history;    // Eingangsverlauf inkl. Vorlauf
    double step = 1.0;             // Eingangswerte je Ausgangswert
    double pos  = 0.0;             // fraktionale Leseposition im Verlauf
    int    historyFill = 0;
    bool   passthrough = true;
};

/// Hin- und Rückumrechnung um etwas, das nur bei einer festen Rate stimmt (ein
/// NAM-Modell).
///
/// Der Rückweg läuft über eine kurze Warteschlange, damit jeder Puffer genau so
/// viele Werte liefert, wie der Host erwartet. Sonst schwankt die Anzahl um ±1
/// und es gibt mit der Zeit Aussetzer.
class RoundTripResampler
{
public:
    /// worker bekommt Werte in der inneren Rate und schreibt in place zurück.
    using Worker = void (*)(void* ctx, float* buffer, int numFrames);

    void prepare (double outerRate, double innerRate, int maxOuterBlock);
    void reset();

    bool   isPassthrough() const { return passthrough; }
    int    latencySamples() const { return reportedLatency; }
    double innerRate() const { return inner; }

    /// Maximale Anzahl innerer Werte pro Puffer, damit das Modell vorher
    /// passend vorbereitet werden kann.
    int maxInnerFrames() const { return maxInner; }

    void process (float* buffer, int numFrames, Worker worker, void* ctx);

private:
    SincResampler up, down;
    std::vector<float> innerBuf, outFifo;
    int    fifoFill = 0, reportedLatency = 0, maxInner = 0;
    double outer = 48000.0, inner = 48000.0;
    bool   passthrough = true;
};

} // namespace nmp
