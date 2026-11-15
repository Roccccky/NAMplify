#pragma once
// Looper mit vier Stereospuren, bis 60 s pro Spur.
//
// - Das Eingangssignal ist um die Latenz der Kette verzögert. Die Schleife wird
// deshalb um genau diese Strecke versetzt aus der Aufnahme geschnitten, und
// beim Schließen wird entsprechend länger aufgenommen.
// - An der Naht wird mit dem Signal übergeblendet, das hinter dem Schleifenende
// weiterläuft. Keine Klicks an Kanten, Übergängen und beim Ausschalten.
// - Auto-Cut entfernt Stille am Anfang und Ende der ersten Aufnahme. Die
// Aufnahme bleibt vollständig erhalten, nur die Kanten werden verschoben und
// lassen sich zurückziehen.

#include "../blocks/Block.h"
#include "../util/Smoothed.h"
#include <atomic>
#include <algorithm>
#include <vector>

namespace nmp
{

// Vier Zustände, kein Overdub. Für eine zweite Schicht nimmt man die nächste
// Spur, dann bleiben Lautstärke, Stopp und Löschen pro Schicht getrennt.
// Taster: aufnehmen, schließen, anhalten, weiterlaufen. Lang drücken löscht.
enum class LoopState : int { empty = 0, recording, playing, stopped };

const char* loopStateName (LoopState s);

struct LoopTrackInfo
{
    LoopState state = LoopState::empty;
    int   rawLength = 0;        ///< was aufgenommen wurde
    int   loopStart = 0;        ///< Kante unten
    int   loopEnd = 0;          ///< Kante oben
    float volume = 0.8f;
    double positionUnit = 0.0;  ///< 0 … 1 innerhalb der Schleife
    double lengthSeconds = 0.0;
    double lengthBeats = 0.0;   ///< in Vierteln, wenn ein Zyklus bekannt ist
};

class Looper : public Block
{
public:
    static constexpr int    kTracks = 4;
    static constexpr double kMaxSeconds = 60.0;
    static constexpr int    kPeakPoints = 2048;
    /// Stützstellen der Hüllkurve für die Tempo- und Loop-Erkennung. Bei 48 kHz
    /// etwa ein Wert pro 11 ms, bei höheren Raten wächst der Schritt mit.
    static constexpr int    kEnvPoints  = 6144;

    void prepare (const PrepareInfo&) override;
    void reset() override;
    void process (StereoBlock&, const BlockContext&) override;

    // Bedienung
    /// Kurz treten: aufnehmen, schließen, anhalten, weiterlaufen.
    void tap (int track);
    void erase (int track);       ///< lang drücken
    void eraseAll();

    void setTrackVolume (int t, float v);
    float trackVolume (int t) const;

    void setSync (bool on) { syncOn = on; }
    void setEnabled (bool on);    ///< Fußschalter der Karte

    /// Solange etwas ausklingt, muss process() weiter aufgerufen werden, auch
    /// wenn der Fußschalter schon aus ist. Sonst wird die Wiedergabe hart
    /// abgeschnitten.
    bool tailActive() const;

    /// Solange nichts aufgenommen ist, gibt die Beat Machine die Zykluslänge
    /// vor.
    void setExternalBarSamples (double s) { externalBar = s; }

    /// Taktlänge und Position im Takt (phase01, 0 … 1). Die Phase wird
    /// gebraucht, damit eine Aufnahme auf der Eins beginnt.
    void setExternalClock (double barSamples, double phase01)
    {
        externalBar = barSamples;
        externalPhase = barSamples > 0.0 ? phase01 * barSamples : -1.0;
    }

    /// Latenz der gesamten Kette in Samples.
    void setLatencyCompensation (int samples) { latency = std::max (0, samples); }

    // Kanten verschieben
    void setLoopEdges (int track, int start, int end);

    // Infos für die Anzeige
    LoopTrackInfo trackInfo (int t) const;
    /// Wellenform: out bekommt numPoints Spitzenwerte 0 … 1.
    void waveform (int track, float* out, int numPoints) const;

    double cycleSamples() const { return cycle; }
    /// Zähler seit der Eins, nur für Tests.
    double phaseSamples() const { return phase; }
    /// Länge der Überblendung an der Naht. Für die Anzeige der Kante und den
    /// Nachlauf beim Schließen.
    int    seamCrossfadeSamples() const { return crossfade; }
    bool   anyRecording() const;
    int    recordingTrack() const;
    int    oldestPlayingTrack() const;

private:
    struct Track
    {
        std::vector<float> left, right;
        std::atomic<int>   state { (int) LoopState::empty };

        int   rawLength = 0;
        int   loopStart = 0, loopEnd = 0;
        int   writePos = 0;
        double playPos = 0.0;
        int   tailToGo = 0;           ///< Nachlauf für den Laufzeitausgleich
        bool  finalizing = false;
        bool  isMaster = false;

        /// Phasenwert, an dem der Inhalt beginnt. Daraus wird die Leseposition
        /// berechnet, damit eine angehaltene Spur synchron bleibt.
        double anchor = 0.0;
        /// Vorlauf: es wird schon aufgenommen, aber die Eins ist noch nicht
        /// erreicht.
        bool  waiting = false;
        /// Beginn des Inhalts in der Aufnahme (−1 = noch offen).
        int   contentStart = -1;
        /// Quantisiertes Ziel beim Schließen (0 = frei, ohne Clock).
        int   target = 0;
        /// Zykluslänge zur Aufnahmezeit, für Tempoänderungen.
        double cycleAtRec = 0.0;

        bool  resumeOnEnable = false; ///< lief, als das Pedal ausgeschaltet wurde

        float peaks[kPeakPoints] = {};
        /// Hüllkurve in gleichmäßigen Schritten, für die Tempo- und
        /// Loop-Erkennung und das Ende des gespielten Teils.
        float env[kEnvPoints] = {};
        LinearSmoothed vol;
        /// Blende der Wiedergabe beim Stoppen, Ausschalten und Wiedereinstieg.
        /// Unabhängig von der Lautstärke.
        LinearSmoothed play;
        std::atomic<float> volTarget { 0.8f };
        std::atomic<double> uiPos { 0.0 };

        int loopLength() const { return std::max (1, loopEnd - loopStart); }
    };

    void startRecording (Track&, int index);
    void closeLoop (Track&, int index);
    void finalize (Track&, int index, double phaseNow);
    void applyAutoCut (Track&);
    void openPlayGate (Track&);
    void updatePeak (Track&, int pos, float value);

    /// Ende des musikalisch gespielten Teils. Kurze Geräusche am Ende (z. B.
    /// der Griff zur Maus) werden übersprungen, wenn davor eine deutliche Lücke
    /// liegt.
    int  musicalEnd (const Track&, int upTo) const;
    /// Stärkste Periodizität der Hüllkurve in Samples (0 = nichts gefunden).
    int  guessPeriod (const Track&, int from, int to) const;
    /// Beat-Länge in Samples, 60 … 200 BPM (0 = nichts). Gemessen an den
    /// Anschlägen, nicht am Pegel.
    int  guessBeat (const Track&, int from, int to) const;
    /// Ob eine Spur Inhalt hat.
    bool anyContent() const;

    double sampleRate = 48000.0;
    int    maxSamples = 0;
    int    peakStride = 1;
    int    latency = 0;
    int    crossfade = 0;

    bool   syncOn = false;
    bool   enabled = false;

    int    envHop = 512;           ///< Abtastwerte je Stützstelle der Hüllkurve

    double cycle = 0.0;            ///< laufender Zyklus in Abtastwerten
    double recordedCycle = 0.0;    ///< wie lang er beim Aufnehmen war
    double externalBar = 0.0;
    double externalPhase = -1.0;   ///< Position im Takt der Beat Machine

    /// Gemeinsamer Zähler für alle Spuren. Jede Spur merkt sich, wo ihr Inhalt
    /// beginnt. So bleiben die Spuren synchron, und eine wieder gestartete Spur
    /// steigt an der richtigen Stelle ein.
    double phase = 0.0;

    /// Kopie des Eingangssignals, bevor die Spuren ihre Wiedergabe dazumischen.
    /// Sonst würde Spur 2 die Wiedergabe von Spur 1 mit aufnehmen.
    std::vector<float> dryL, dryR;

    Track tracks[kTracks];
};

} // namespace nmp
