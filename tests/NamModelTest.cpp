// Der NAM-Kern und was NAMplify um ihn herum baut.
//
// Die Tests erzeugen ihre Capture-Dateien selbst. Die Architektur Linear gehört
// zum Kern und beschreibt eine Impulsantwort. Damit lässt sich der ganze Weg
// prüfen (Registrierung, Laden, Ratenumrechnung, Lautheitsausgleich), ohne
// fremde Dateien.

#include "TestSignals.h"
#include "namplify/model/NamModel.h"
#include "namplify/model/LoudnessProbe.h"
#include "namplify/model/WavFile.h"

#include <NAM/model_config.h>
#include <NAM/get_dsp.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace nmp;
using namespace nmptest;

namespace
{
    std::filesystem::path scratchDir()
    {
        auto p = std::filesystem::temp_directory_path() / "namplify-tests";
        std::error_code ec;
        std::filesystem::create_directories (p, ec);
        return p;
    }

    /// Schreibt eine .nam-Datei mit der Architektur Linear. Gewichte sind [h0 …
    /// h(n−1), bias], so liest der Kern sie ein.
    std::filesystem::path writeLinearNam (const std::string& name,
                                          const std::vector<float>& ir,
                                          double sampleRate,
                                          const std::string& metadata = {})
    {
        const auto path = scratchDir() / (name + ".nam");
        std::ofstream f (path);

        f << "{\n  \"version\": \"0.5.4\",\n  \"architecture\": \"Linear\",\n";
        f << "  \"config\": { \"receptive_field\": " << ir.size() << ", \"bias\": true },\n";
        f << "  \"sample_rate\": " << (long long) sampleRate << ",\n";
        if (! metadata.empty()) f << "  \"metadata\": " << metadata << ",\n";
        f << "  \"weights\": [";
        for (size_t i = 0; i < ir.size(); ++i) f << (i ? ", " : "") << ir[i];
        f << ", 0.0]\n}\n";

        return path;
    }

    /// Die drei A2-Captures aus tests/fixtures/a2, wie sie aus dem Katalog
    /// kommen.
    std::vector<std::filesystem::path> a2Fixtures()
    {
        const std::filesystem::path dir = std::filesystem::path (NAMPLIFY_TEST_FIXTURES) / "a2";
        return { dir / "a2-amp-test.nam", dir / "a2-amp-cab-test.nam", dir / "a2-am-test-2.nam" };
    }

    /// Eine Impulsantwort mit deutlichem Klang: zwei Resonanzen und ein Deckel.
    /// Bei falsch umgerechneter Samplerate wandern die Resonanzen, das soll der
    /// Test erkennen.
    std::vector<float> makeToneIr (double sampleRate, float gain)
    {
        const int n = 512;
        std::vector<float> ir ((size_t) n, 0.0f);

        auto addRes = [&] (double f, double q, double amp)
        {
            const double decay = kPi * f / q;
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sampleRate;
                ir[(size_t) i] += (float) (amp * std::exp (-decay * t) * std::sin (kTwoPi * f * t));
            }
        };

        ir[0] = 0.6f;
        addRes (1200.0, 9.0, 0.30);
        addRes (3400.0, 14.0, 0.18);

        float peak = 0.0f;
        for (float v : ir) peak = std::max (peak, std::abs (v));
        for (auto& v : ir) v *= gain / std::max (1.0e-6f, peak);

        return ir;
    }
}

NMP_TEST (NamKern_AlleArchitekturenSindRegistriert)
{
    // Deshalb wird der Kern nicht als statische Bibliothek gelinkt: die
    // Architekturen registrieren sich über statische Objekte, auf die nichts
    // direkt verweist. Ein Linker, der aufräumt, entfernt sie, und get_dsp
    // wirft dann zur Laufzeit.
    const char* wanted[] = { "WaveNet", "LSTM", "ConvNet", "Linear",
                             "Sequential", "SlimmableContainer" };

    for (const char* name : wanted)
        CHECK_MSG (nam::ConfigParserRegistry::instance().has (name),
                   std::string ("Architektur \"") + name + "\" ist nicht registriert:"
                   " der Kern wurde als Bibliothek gebunden, statt in das Ziel übersetzt"
                   " zu werden (siehe cmake/NamCore.cmake)");
}

NMP_TEST (NamKern_LaedtEineDateiUndRechnetSie)
{
    const double sr = 48000.0;
    const auto ir = makeToneIr (sr, 0.5f);
    const auto path = writeLinearNam ("plain", ir, sr);

    std::string error;
    auto model = NamModel::load (path, sr, 512, error);

    CHECK_MSG (model != nullptr, "Laden fehlgeschlagen: " + error);
    if (! model) return;

    CHECK (model->info().architecture == "Linear");
    CHECK_NEAR (model->info().fileSampleRate, sr, 0.5);
    CHECK (! model->info().resampling);
    CHECK (! model->info().hasLite);

    // Einheitsimpuls hinein, die Impulsantwort muss herauskommen.
    std::vector<float> sig (1024, 0.0f);
    sig[0] = 1.0f;
    model->process (sig.data(), (int) sig.size());

    double worst = 0.0;
    for (size_t i = 0; i < ir.size(); ++i)
        worst = std::max (worst, (double) std::abs (sig[i] - ir[i]));

    CHECK_MSG (worst < 1.0e-4, "Impulsantwort weicht um " + fmt (worst) + " ab");
}

NMP_TEST (NamKern_SagtWasLosIstStattZuStuerzen)
{
    const auto bad = scratchDir() / "kaputt.nam";
    { std::ofstream f (bad); f << "das ist kein JSON"; }

    std::string error;
    auto model = NamModel::load (bad, 48000.0, 512, error);

    CHECK (model == nullptr);
    CHECK_MSG (! error.empty(), "es gibt keine Begründung: der Nutzer sähe nur, dass nichts passiert");

    std::string error2;
    auto missing = NamModel::load (scratchDir() / "gibtsnicht.nam", 48000.0, 512, error2);
    CHECK (missing == nullptr);
    CHECK (! error2.empty());

    // Eine WAV-Datei auf einem NAM-Slot ist der häufigste Fehlgriff.
    const auto wav = scratchDir() / "falsch.nam";
    {
        std::string e;
        std::vector<float> z (100, 0.0f);
        writeWav24 (wav, z.data(), z.data(), 100, 48000.0, e);
    }
    std::string error3;
    auto wrongType = NamModel::load (wav, 48000.0, 512, error3);
    CHECK (wrongType == nullptr);
    CHECK (! error3.empty());
}

NMP_TEST (Capture_KlingtBeiJederAbtastrateGleich)
{
    // Ein Capture muss bei jeder Samplerate gleich klingen. Trägt die Datei
    // eine Trainingsrate und läuft das Interface mit einer anderen, wäre der
    // Klang sonst verstimmt und alle Filter säßen falsch.
    const double modelRate = 48000.0;
    const auto ir = makeToneIr (modelRate, 0.5f);
    const auto path = writeLinearNam ("rate", ir, modelRate);

    // Der Sollgang: die Impulsantwort selbst, analytisch ausgewertet.
    auto responseOfIr = [&] (double hz)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < ir.size(); ++i)
        {
            const double w = kTwoPi * hz * (double) i / modelRate;
            re += ir[i] * std::cos (w);
            im -= ir[i] * std::sin (w);
        }
        return std::hypot (re, im);
    };

    const double testFreqs[] = { 200.0, 600.0, 1200.0, 2000.0, 3400.0, 6000.0 };

    for (double hostRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        std::string error;
        auto model = NamModel::load (path, hostRate, 256, error);
        CHECK_MSG (model != nullptr, error);
        if (! model) continue;

        CHECK (model->info().resampling == (std::abs (hostRate - modelRate) > 1.0));

        for (double f : testFreqs)
        {
            model->reset();

            const int n = (int) (hostRate * 0.6);
            std::vector<float> sig ((size_t) n);
            for (int i = 0; i < n; ++i)
                sig[(size_t) i] = (float) std::sin (kTwoPi * f * i / hostRate);

            int pos = 0;
            while (pos < n) { const int c = std::min (256, n - pos); model->process (sig.data() + pos, c); pos += c; }

            // Pegel in der Mitte
            const double got  = rms (sig, n / 3, n * 2 / 3) * std::sqrt (2.0);
            const double want = responseOfIr (f);

            const double db = gainToDb (got) - gainToDb (want);
            CHECK_MSG (std::abs (db) < 0.6,
                       "bei " + fmt (hostRate) + " Hz Host-Rate und " + fmt (f)
                       + " Hz: " + fmt (db) + " dB daneben: das Capture klingt bei"
                         " dieser Rate anders");

            // Die Tonhöhe darf sich nicht verschieben.
            int zc = 0;
            for (int i = n / 3 + 1; i < n * 2 / 3; ++i)
                if ((sig[(size_t) (i - 1)] < 0.0f) != (sig[(size_t) i] < 0.0f)) ++zc;
            const double seconds  = (double) (n / 3) / hostRate;
            const double measured = zc / (2.0 * seconds);

            CHECK_MSG (std::abs (measured / f - 1.0) < 0.01,
                       "bei " + fmt (hostRate) + " Hz Host-Rate kommt " + fmt (f)
                       + " Hz als " + fmt (measured) + " Hz heraus: das Capture ist verstimmt");
        }
    }
}

NMP_TEST (Lautheit_GleichtCapturesAneinanderAn)
{
    // Zehn Captures, deren Pegel um mehr als 30 dB auseinanderliegen, wie in
    // einer echten Sammlung. Nach dem Ausgleich müssen sie gleich laut sein.
    const double sr = 48000.0;

    struct Row { std::string name; double before, after; };
    std::vector<Row> rows;

    const float gains[] = { 0.02f, 0.05f, 0.1f, 0.2f, 0.35f, 0.5f, 0.7f, 1.0f, 1.6f, 2.4f };

    for (int i = 0; i < 10; ++i)
    {
        const auto ir = makeToneIr (sr, gains[i]);
        const auto path = writeLinearNam ("loud" + std::to_string (i), ir, sr);

        std::string error;
        auto model = NamModel::load (path, sr, 512, error);
        CHECK_MSG (model != nullptr, error);
        if (! model) continue;

        const auto& m = model->info().measurement;
        CHECK_MSG (m.valid, "Messung fehlgeschlagen");

        rows.push_back ({ "loud" + std::to_string (i), m.measuredDb, m.measuredDb + m.trimDb });
    }

    double loA = 1.0e9, hiA = -1.0e9, loB = 1.0e9, hiB = -1.0e9;
    for (const auto& r : rows)
    {
        loA = std::min (loA, r.before); hiA = std::max (hiA, r.before);
        loB = std::min (loB, r.after);  hiB = std::max (hiB, r.after);
    }

    std::printf ("        vor dem Ausgleich: %.1f dB Streuung   danach: %.2f dB\n",
                 hiA - loA, hiB - loB);

    CHECK_GT (hiA - loA, 25.0);                        // Ausgangslage war wirklich ungleich
    CHECK_MSG (hiB - loB < 0.1,
               "nach dem Ausgleich bleiben " + fmt (hiB - loB) + " dB Streuung");
}

NMP_TEST (Lautheit_UeberEineEchteSammlung)
{
    // Lautheit über eine echte Sammlung von Captures: wie weit streuen sie nach
    // dem Ausgleich noch?
    //
    // Ohne NAMPLIFY_MODEL_DIR werden die drei A2-Captures aus tests/fixtures/a2
    // gemessen. Zeigt NAMPLIFY_MODEL_DIR auf einen Ordner mit .nam-Dateien,
    // wird dieser Ordner gemessen.
    const char* env = std::getenv ("NAMPLIFY_MODEL_DIR");
    const std::string dir = env != nullptr ? std::string (env)
                                           : (std::filesystem::path (NAMPLIFY_TEST_FIXTURES) / "a2").string();

    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto& e : std::filesystem::recursive_directory_iterator (dir, ec))
        if (e.is_regular_file (ec) && e.path().extension() == ".nam")
            files.push_back (e.path());

    if (files.empty()) SKIP (std::string ("keine .nam-Dateien unter ") + dir);

    std::vector<double> before, after, fileClaim;
    int failed = 0;

    for (const auto& f : files)
    {
        std::string error;
        auto model = NamModel::load (f, 48000.0, 512, error);
        if (! model) { ++failed; continue; }

        const auto& m = model->info().measurement;
        if (! m.valid) { ++failed; continue; }

        before.push_back (m.measuredDb);
        after .push_back (m.measuredDb + m.trimDb);
        if (m.fileHadLoudness) fileClaim.push_back (m.fileLoudnessDb);
    }

    if (before.size() < 3) SKIP ("zu wenige lesbare Captures");

    auto spread = [] (std::vector<double> v)
    {
        std::sort (v.begin(), v.end());
        return v.back() - v.front();
    };

    std::printf ("        %d Captures, %d nicht lesbar\n", (int) before.size(), failed);
    std::printf ("        Streuung roh:                %.1f dB\n", spread (before));
    if (! fileClaim.empty())
        std::printf ("        Streuung nach Dateiangabe:   %.1f dB  (%d Dateien tragen eine)\n",
                     spread (fileClaim), (int) fileClaim.size());
    std::printf ("        Streuung nach dem Ausgleich: %.2f dB\n", spread (after));

    CHECK_MSG (spread (after) < 1.0,
               "nach dem Ausgleich bleiben " + fmt (spread (after)) + " dB Streuung");
}

NMP_TEST (NamKern_A2AusDemKatalogLaedtMitZweiNetzgroessen)
{
    // Der Katalog liefert A2. Eine solche Datei ist ein SlimmableContainer mit
    // Dateiversion 0.7.0 und muss vom Kern geladen werden.
    for (const auto& path : a2Fixtures())
    {
        std::string error;
        auto model = NamModel::load (path, 48000.0, 512, error);
        CHECK_MSG (model != nullptr, path.filename().string() + ": " + error);
        if (! model) continue;

        const auto& info = model->info();
        CHECK (info.architecture == "SlimmableContainer");
        CHECK_MSG (info.hasLite, path.filename().string() + " bietet keine zweite Netzgröße an");
        CHECK_NEAR (info.fileSampleRate, 48000.0, 0.5);
        CHECK (! info.resampling);
        CHECK (info.hasLoudness);
        CHECK (info.measurement.valid);

        // FULL und LITE müssen beide rechnen und verschieden klingen, sonst
        // wäre der Schalter wirkungslos.
        auto run = [&] (bool lite)
        {
            model->setLite (lite);
            model->reset();
            std::vector<float> sig (4800);
            for (size_t i = 0; i < sig.size(); ++i)
                sig[i] = 0.3f * (float) std::sin (kTwoPi * 220.0 * (double) i / 48000.0);
            model->process (sig.data(), (int) sig.size());
            return sig;
        };

        const auto full = run (false);
        const auto lite = run (true);

        double energy = 0.0, diff = 0.0;
        bool finite = true;
        for (size_t i = 2400; i < full.size(); ++i)
        {
            finite = finite && std::isfinite (full[i]) && std::isfinite (lite[i]);
            energy += (double) full[i] * full[i];
            diff   += (double) (full[i] - lite[i]) * (full[i] - lite[i]);
        }

        CHECK (finite);
        CHECK_MSG (energy > 1.0e-3, path.filename().string() + ": FULL bleibt stumm");
        CHECK_MSG (diff > 1.0e-6 * energy, path.filename().string() + ": LITE klingt wie FULL");
    }

    // Der Eingangspegel steht bei A2 wie bei A1 in den Metadaten.
    std::string error;
    auto withLevels = NamModel::load (a2Fixtures()[1], 48000.0, 512, error);
    CHECK_MSG (withLevels != nullptr, error);
    if (withLevels)
    {
        CHECK (withLevels->info().hasInputLevel);
        CHECK_NEAR (withLevels->info().inputLevelDbu, 15.0, 1.0e-6);
    }
}

NMP_TEST (Wav_OhneFuellbyteWirdGanzGelesen)
{
    // Manche Impulsantworten im Katalog haben einen ungeraden Datenblock, und
    // das von RIFF verlangte Füllbyte fehlt, obwohl die Größenangabe im Header
    // es mitzählt. JUCE 9 liest so eine Datei mit null Samples, die Box wäre
    // stumm überbrückt. Unser Leser liest, was da ist.
    const int frames = 257;                  // 24 bit mono → 771 Byte, ungerade
    std::vector<unsigned char> b;
    auto u32 = [&] (unsigned v) { for (int i = 0; i < 4; ++i) b.push_back ((unsigned char) (v >> (8 * i))); };
    auto u16 = [&] (unsigned v) { b.push_back ((unsigned char) v); b.push_back ((unsigned char) (v >> 8)); };
    auto tag = [&] (const char* t) { b.insert (b.end(), t, t + 4); };

    const unsigned dataSize = (unsigned) frames * 3u;
    tag ("RIFF"); u32 (36u + dataSize + 1u); tag ("WAVE");
    tag ("fmt "); u32 (16); u16 (1); u16 (1); u32 (48000); u32 (48000 * 3); u16 (3); u16 (24);
    tag ("data"); u32 (dataSize);
    for (int i = 0; i < frames; ++i)
    {
        const int v = (i == 0) ? 4194304 : 0;       // 0,5 am Anfang, dann Stille
        b.push_back ((unsigned char) v); b.push_back ((unsigned char) (v >> 8)); b.push_back ((unsigned char) (v >> 16));
    }
    // kein Füllbyte

    const auto path = scratchDir() / "ohne-fuellbyte.wav";
    { std::ofstream f (path, std::ios::binary); f.write ((const char*) b.data(), (std::streamsize) b.size()); }

    WavData wav;
    std::string error;
    CHECK_MSG (readWav (path, wav, error), error);
    CHECK (wav.channels.size() == 1);
    if (wav.channels.size() == 1)
    {
        CHECK ((int) wav.channels[0].size() == frames);
        CHECK_NEAR (wav.channels[0][0], 0.5, 1.0e-6);
    }
}

NMP_TEST (NamKern_A2LiteRechnetBilligerAlsFull)
{
    // LITE ist für schwache Rechner gedacht und muss weniger kosten als FULL.
    std::string error;
    auto model = NamModel::load (a2Fixtures()[0], 48000.0, 64, error);
    CHECK_MSG (model != nullptr, error);
    if (! model) return;

    auto cost = [&] (bool lite)
    {
        model->setLite (lite);
        model->reset();
        std::vector<float> block (64);
        double best = 1.0e30;
        for (int round = 0; round < 5; ++round)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < 750; ++b)              // eine Sekunde bei 48 kHz
            {
                for (int i = 0; i < 64; ++i)
                    block[(size_t) i] = 0.3f * (float) std::sin (kTwoPi * 220.0 * (double) (b * 64 + i) / 48000.0);
                model->process (block.data(), 64);
            }
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min (best, std::chrono::duration<double, std::milli> (t1 - t0).count());
        }
        return best;
    };

    const double full = cost (false);
    const double lite = cost (true);
    std::printf ("        eine Sekunde Signal: FULL %.1f ms, LITE %.1f ms (%.0f %%)\n",
                 full, lite, 100.0 * lite / full);

    CHECK_MSG (lite < full * 0.8,
               "LITE kostet " + fmt (100.0 * lite / full) + " % von FULL");
}

namespace
{
    /// Pegel einer Frequenz in dB, Hann-gefenstert (Goertzel).
    double toneDb (const std::vector<float>& x, size_t from, double freq, double rate)
    {
        const size_t n = x.size() - from;
        const double w = kTwoPi * freq / rate;
        double re = 0.0, im = 0.0, wsum = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) (n - 1));
            re += x[from + i] * win * std::cos (w * (double) i);
            im -= x[from + i] * win * std::sin (w * (double) i);
            wsum += win;
        }
        return 20.0 * std::log10 (std::max (2.0 * std::sqrt (re * re + im * im) / wsum, 1.0e-12));
    }

    /// Pegel der zurückgefalteten Obertöne eines 6,7-kHz-Tons, bezogen auf den
    /// Grundton. Die Obertöne ab dem vierten liegen über 24 kHz. Was davon im
    /// Hörbereich ankommt, ist Aliasing, keine dieser Frequenzen ist ein
    /// Vielfaches von 6,7 kHz.
    double aliasDb (const std::filesystem::path& capture, int oversampling)
    {
        std::string error;
        auto model = NamModel::load (capture, 48000.0, 512, error, oversampling);
        if (! model) return 0.0;

        const double f0 = 6700.0;
        std::vector<float> sig (48000);
        for (size_t i = 0; i < sig.size(); ++i)
            sig[i] = 0.5f * (float) std::sin (kTwoPi * f0 * (double) i / 48000.0);
        for (size_t i = 0; i < sig.size(); i += 512)
            model->process (sig.data() + i, (int) std::min<size_t> (512, sig.size() - i));

        const double fund = toneDb (sig, 9600, f0, 48000.0);
        double sum = 0.0;
        for (int k = 4; k <= 12; ++k)
        {
            double f = std::fmod (k * f0, 48000.0);
            if (f > 24000.0) f = 48000.0 - f;
            sum += std::pow (10.0, toneDb (sig, 9600, f, 48000.0) / 10.0);
        }
        return 10.0 * std::log10 (sum) - fund;
    }
}

NMP_TEST (NamKern_UeberabtastungSenktDieFaltung)
{
    // Ein Capture erzeugt Obertöne über der halben Samplerate, die unharmonisch
    // zurückfalten. Mit doppelter und vierfacher Rate müssen sie deutlich
    // leiser werden.
    const auto capture = a2Fixtures()[0];
    const double x1 = aliasDb (capture, 1);
    const double x2 = aliasDb (capture, 2);
    const double x4 = aliasDb (capture, 4);

    std::printf ("        Faltung relativ zum Grundton: 1× %.1f dB, 2× %.1f dB, 4× %.1f dB\n", x1, x2, x4);
    CHECK_MSG (x2 < x1 - 6.0, "2× senkt die Faltung nur um " + fmt (x1 - x2) + " dB");
    CHECK_MSG (x4 < x2, "4× faltet nicht weniger als 2×");
}

NMP_TEST (NamKern_UeberabtastungTeiltKeinLstm)
{
    // Ein LSTM rechnet von Sample zu Sample fort und lässt sich nicht
    // aufteilen. Es bleibt bei einfacher Rate und meldet das.
    const auto path = scratchDir() / "lstm-os.nam";
    {
        std::ofstream f (path);
        const int h = 4;
        const int count = 4 * h * (1 + h) + 4 * h + h + h + h + 1;
        f << "{ \"version\": \"0.5.4\", \"architecture\": \"LSTM\","
             " \"config\": { \"num_layers\": 1, \"input_size\": 1, \"hidden_size\": " << h << " },"
             " \"sample_rate\": 48000, \"weights\": [";
        for (int i = 0; i < count; ++i) f << (i ? "," : "") << 0.01 * (i % 7);
        f << "] }\n";
    }

    std::string error;
    auto model = NamModel::load (path, 48000.0, 512, error, 4);
    CHECK_MSG (model != nullptr, error);
    if (model) CHECK (model->info().oversampling == 1);

    auto a2 = NamModel::load (a2Fixtures()[0], 48000.0, 512, error, 4);
    CHECK_MSG (a2 != nullptr, error);
    if (a2) CHECK (a2->info().oversampling == 4);
}
