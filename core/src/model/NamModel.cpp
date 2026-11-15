#include "namplify/model/NamModel.h"

#include <NAM/get_dsp.h>
#include <NAM/dsp.h>
#include <NAM/slimmable.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace nmp
{

NamModel::NamModel() = default;
NamModel::~NamModel() = default;

void NamModel::workerThunk (void* ctx, float* buffer, int numFrames)
{
    static_cast<NamModel*> (ctx)->runModel (buffer, numFrames);
}

void NamModel::runModel (float* buffer, int numFrames)
{
    if (! dsp || numFrames <= 0) return;

    const int factor = details.oversampling;
    if (factor == 1)
    {
        NAM_SAMPLE* in[1]  = { buffer };
        NAM_SAMPLE* out[1] = { buffer };
        dsp->process (in, out, numFrames);
        return;
    }

    // Hochrechnen, auf N Kopien verteilen, jede durch ihr Netz, wieder
    // zusammensetzen, herunterrechnen. In Stücken, die in die beim Laden
    // angelegten Puffer passen.
    for (int done = 0; done < numFrames;)
    {
        const int m = std::min (numFrames - done, netMaxFrames);
        float* x = buffer + done;

        os.upsample (x, m, fastBuf.data());

        for (int p = 0; p < factor; ++p)
        {
            float* ph = phaseBuf.data();
            for (int k = 0; k < m; ++k) ph[k] = fastBuf[(size_t) (k * factor + p)];

            NAM_SAMPLE* io[1] = { ph };
            (p == 0 ? dsp : phases[(size_t) p - 1])->process (io, io, m);

            for (int k = 0; k < m; ++k) fastBuf[(size_t) (k * factor + p)] = ph[k];
        }

        os.downsample (fastBuf.data(), m, x);
        done += m;
    }
}

void NamModel::resetNets (int maxFrames)
{
    netMaxFrames = std::max (16, maxFrames);
    dsp->Reset (details.modelSampleRate, netMaxFrames);
    for (auto& p : phases) p->Reset (details.modelSampleRate, netMaxFrames);

    const int factor = details.oversampling;
    os.prepare (factor, netMaxFrames);
    fastBuf.assign ((size_t) (netMaxFrames * factor), 0.0f);
    phaseBuf.assign ((size_t) netMaxFrames, 0.0f);
}

namespace
{
    /// Lässt sich das Netz aufteilen? Nur dilatierte Faltungen mit punktweisen
    /// Aktivierungen, kein Zustand von Sample zu Sample.
    bool splittable (const std::string& architecture, const nlohmann::json& config)
    {
        if (architecture == "WaveNet" || architecture == "ConvNet" || architecture == "Linear")
            return true;

        if (architecture == "SlimmableContainer" && config.contains ("submodels"))
        {
            for (const auto& sub : config["submodels"])
            {
                const auto& m = sub.contains ("model") ? sub["model"] : sub;
                const auto arch = m.value ("architecture", std::string());
                if (! splittable (arch, m.value ("config", nlohmann::json::object())))
                    return false;
            }
            return ! config["submodels"].empty();
        }

        return false;
    }
}

std::unique_ptr<NamModel> NamModel::load (const std::filesystem::path& path,
                                          double hostSampleRate, int maxBlockSize,
                                          std::string& error, int oversampling)
{
    auto model = std::unique_ptr<NamModel> (new NamModel());

    nam::dspData data;
    try
    {
        // Ohne prewarm, das Modell wird gleich mit der richtigen Blockgröße
        // angefahren.
        model->dsp = nam::get_dsp (path, data, nam::DspLoadOptions { false });
    }
    catch (const nam::NamFileValidationError& e)
    {
        error = std::string ("Not a valid .nam file: ") + e.what();
        return nullptr;
    }
    catch (const std::exception& e)
    {
        error = std::string ("Capture could not be loaded: ") + e.what();
        return nullptr;
    }

    if (! model->dsp) { error = "Capture could not be loaded"; return nullptr; }

    auto& d = model->details;
    // string() wäre unter Windows die Codepage statt UTF-8 und wirft bei
    // unbekannten Zeichen außerhalb des try oben.
    const auto utf8 = path.u8string();
    d.path.assign (utf8.begin(), utf8.end());
    d.architecture = data.architecture;

    const double reported = model->dsp->GetExpectedSampleRate();
    d.fileSampleRate  = reported;
    d.modelSampleRate = reported > 0.0 ? reported : 48000.0;

    d.hasLoudness = model->dsp->HasLoudness();
    if (d.hasLoudness) d.loudnessDb = model->dsp->GetLoudness();

    d.hasInputLevel = model->dsp->HasInputLevel();
    if (d.hasInputLevel) d.inputLevelDbu = model->dsp->GetInputLevel();

    d.hasOutputLevel = model->dsp->HasOutputLevel();
    if (d.hasOutputLevel) d.outputLevelDbu = model->dsp->GetOutputLevel();

    // LITE-Variante: das Modell muss SlimmableModel implementieren und mehr als
    // eine Größe anbieten. A1-Modelle scheitern schon am dynamic_cast.
    model->slim = dynamic_cast<nam::SlimmableModel*> (model->dsp.get());
    d.hasLite = model->slim != nullptr && ! model->slim->GetSlimmableSizeBreakpoints().empty();

    // Oversampling: weitere Kopien aus denselben Daten
    d.oversampling = 1;
    if (oversampling > 1 && splittable (data.architecture, data.config))
    {
        const int factor = oversampling >= 4 ? 4 : 2;
        try
        {
            for (int i = 1; i < factor; ++i)
            {
                nam::dspData copy = data;
                auto extra = nam::get_dsp (copy, nam::DspLoadOptions { false });
                if (! extra) throw std::runtime_error ("no model");
                model->phaseSlims.push_back (dynamic_cast<nam::SlimmableModel*> (extra.get()));
                model->phases.push_back (std::move (extra));
            }
            d.oversampling = factor;
        }
        catch (const std::exception&)
        {
            // Ohne Kopien wird mit einfacher Rate gerechnet.
            model->phases.clear();
            model->phaseSlims.clear();
        }
    }

    // Messen, bei der Trainingsrate des Modells. Bei der Host-Rate gemessen,
    // ergäbe dasselbe Capture bei 44,1 und 48 kHz unterschiedliche Ausgleiche.
    {
        model->resetNets (512);

        d.measurement = measureModelLoudness (d.modelSampleRate,
            [&] (float* buf, int n) { model->runModel (buf, n); });

        d.measurement.fileHadLoudness = d.hasLoudness;
        d.measurement.fileLoudnessDb  = d.loudnessDb;

        // Fehlgeschlagene Messung: Wert aus der Datei als Ersatz.
        if (! d.measurement.valid && d.hasLoudness)
        {
            d.measurement.measuredDb = d.loudnessDb;
            d.measurement.trimDb     = kNormalizationTargetDb - d.loudnessDb;
            d.measurement.valid      = true;
        }
    }

    model->prepare (hostSampleRate, maxBlockSize);
    return model;
}

void NamModel::prepare (double hostSampleRate, int maxBlockSize)
{
    maxBlock = std::max (1, maxBlockSize);

    details.hostSampleRate = hostSampleRate;
    rt.prepare (hostSampleRate, details.modelSampleRate, maxBlock);

    details.resampling     = ! rt.isPassthrough();
    details.latencySamples = rt.latencySamples();

    if (dsp)
    {
        // Anfahren hier im Lade-Thread, damit der erste Audio-Block sauber
        // startet.
        resetNets (rt.maxInnerFrames());
    }
}

void NamModel::reset()
{
    rt.reset();
    if (dsp) resetNets (rt.maxInnerFrames());
}

void NamModel::process (float* mono, int numFrames)
{
    if (! dsp || numFrames <= 0) return;
    rt.process (mono, numFrames, &NamModel::workerThunk, this);
}

void NamModel::setLite (bool lite)
{
    if (! slim || ! details.hasLite || lite == liteActive) return;
    liteActive = lite;
    slim->SetSlimmableSize (lite ? 0.0 : 1.0);
    for (auto* s : phaseSlims)
        if (s != nullptr) s->SetSlimmableSize (lite ? 0.0 : 1.0);
}

} // namespace nmp
