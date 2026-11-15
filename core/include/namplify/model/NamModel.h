#pragma once
// Ein geladenes Capture. Der NAM-Kern bleibt unverändert. Diese Klasse ergänzt
// Samplerate-Umrechnung, Lautheitsmessung, Umschalten zwischen FULL und LITE
// und knackfreies Laden im laufenden Betrieb.

#include "../util/Types.h"
#include "../dsp/Resampler.h"
#include "../dsp/AllpassHalfband.h"
#include "LoudnessProbe.h"

#include <memory>
#include <string>
#include <filesystem>
#include <vector>

namespace nam { class DSP; class SlimmableModel; }

namespace nmp
{

struct NamModelInfo
{
    std::string path;
    std::string architecture;

    /// Laut Datei. −1 = nicht angegeben.
    double fileSampleRate = -1.0;
    /// Tatsächliche Rate des Modells. Ohne Angabe 48 kHz, wie im
    /// NAM-Referenz-Plugin.
    double modelSampleRate = 48000.0;
    double hostSampleRate  = 48000.0;
    bool   resampling = false;

    bool   hasLoudness = false;    double loudnessDb    = 0.0;
    bool   hasInputLevel = false;  double inputLevelDbu = 0.0;
    bool   hasOutputLevel = false; double outputLevelDbu = 0.0;

    /// Enthält die Datei eine LITE-Variante? (A2)
    bool   hasLite = false;

    LoudnessMeasurement measurement;

    int    latencySamples = 0;

    /// Tatsächlicher Oversampling-Faktor. Ein LSTM lässt sich nicht aufteilen
    /// und bleibt bei 1.
    int    oversampling = 1;
};

class NamModel
{
public:
    NamModel();
    ~NamModel();

    NamModel (const NamModel&) = delete;
    NamModel& operator= (const NamModel&) = delete;

    /// Lädt und misst. Läuft im Lade-Thread. Bei Fehler nullptr, Grund in
    /// error.
    ///
    /// oversampling (1, 2, 4) wird über N Kopien des Netzes umgesetzt, die
    /// abwechselnd jeden N-ten Wert bekommen. Bei Netzen aus dilatierten
    /// Faltungen (WaveNet, ConvNet, Linear, A2) entspricht das exakt dem Netz
    /// mit N-fach gestreckten Abständen bei N-facher Rate. Ein LSTM rechnet
    /// sequenziell und bleibt bei 1.
    static std::unique_ptr<NamModel> load (const std::filesystem::path& path,
                                           double hostSampleRate, int maxBlockSize,
                                           std::string& error, int oversampling = 1);

    /// Neue Rate oder Puffergröße. Nicht aus dem Audio-Thread.
    void prepare (double hostSampleRate, int maxBlockSize);

    void reset();

    /// Ein Mono-Block, in place.
    void process (float* mono, int numFrames);

    /// FULL ⇄ LITE. Thread-sicher, aber nicht echtzeitfähig (baut ein neues
    /// Netz). Vom Message-Thread aufrufen.
    void setLite (bool lite);
    bool isLite() const { return liteActive; }

    const NamModelInfo& info() const { return details; }
    int latencySamples() const { return details.latencySamples; }

private:
    static void workerThunk (void* ctx, float* buffer, int numFrames);
    void runModel (float* buffer, int numFrames);

    /// Setzt die Puffergröße aller Kopien. Alloziert, Lade-Thread.
    void resetNets (int maxFrames);

    std::unique_ptr<nam::DSP> dsp;
    nam::SlimmableModel*      slim = nullptr;

    /// Weitere Kopien für das Oversampling (Kopie 0 ist dsp).
    std::vector<std::unique_ptr<nam::DSP>> phases;
    std::vector<nam::SlimmableModel*>      phaseSlims;
    CaptureOversampler os;
    std::vector<float> fastBuf, phaseBuf;
    int netMaxFrames = 512;

    RoundTripResampler rt;
    NamModelInfo details;
    bool liteActive = false;
    int  maxBlock = 512;
};

} // namespace nmp
