// Ein Capture-Slot als Ganzes: Blende und Pegel.

#include "TestSignals.h"
#include "namplify/blocks/NamBlock.h"
#include "namplify/model/NamModel.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace nmp;
using namespace nmptest;

namespace
{
    /// Ein Capture, das Stille liefert. Am Ausgang steht dann nur der trockene
    /// Anteil, und man sieht, was VOL mit ihm macht.
    std::filesystem::path writeSilentNam()
    {
        auto dir = std::filesystem::temp_directory_path() / "namplify-tests";
        std::error_code ec;
        std::filesystem::create_directories (dir, ec);
        const auto path = dir / "stumm.nam";
        std::ofstream f (path);
        f << "{ \"version\": \"0.5.4\", \"architecture\": \"Linear\","
             " \"config\": { \"receptive_field\": 4, \"bias\": true },"
             " \"sample_rate\": 48000, \"weights\": [0, 0, 0, 0, 0] }\n";
        return path;
    }

    double runLevel (float volDb)
    {
        NamBlock slot (NamBlock::Role::pedal);
        slot.prepare ({ 48000.0, 256 });
        slot.setOutputMode (NamBlock::Output::raw);
        slot.setMix (0.5f);
        slot.setLevelDb (volDb);

        std::string error;
        slot.publishModel (NamModel::load (writeSilentNam(), 48000.0, 256, error));

        std::vector<float> l (256), r (256);
        double sum = 0.0;
        int counted = 0;
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < 256; ++i)
                l[(size_t) i] = r[(size_t) i] = 0.25f * (float) std::sin (kTwoPi * 440.0 * (b * 256 + i) / 48000.0);

            StereoBlock block { l.data(), r.data(), 256 };
            slot.process (block, {});

            if (b >= 20)
                for (float v : l) { sum += (double) v * v; ++counted; }
        }
        return std::sqrt (sum / counted);
    }
}

NMP_TEST (Platz_VolWirktNachDerBlende)
{
    // VOL sitzt hinter der Blende: +6 dB verdoppeln das ganze Signal, auch bei
    // MIX 50 %.
    const double flat = runLevel (0.0f);
    const double up   = runLevel (6.0f);

    CHECK_MSG (flat > 1.0e-3, "am Ausgang kommt nichts an");
    CHECK_NEAR (up / flat, std::pow (10.0, 6.0 / 20.0), 0.01);
}

namespace
{
    /// Ein Capture, das durchreicht: Ausgang gleich Eingang.
    std::filesystem::path writeIdentityNam()
    {
        auto dir = std::filesystem::temp_directory_path() / "namplify-tests";
        const auto path = dir / "durch.nam";
        std::ofstream f (path);
        f << "{ \"version\": \"0.5.4\", \"architecture\": \"Linear\","
             " \"config\": { \"receptive_field\": 1, \"bias\": true },"
             " \"sample_rate\": 48000, \"weights\": [1, 0] }\n";
        return path;
    }

    double blendedGainDb (double freq, int oversampling)
    {
        NamBlock slot (NamBlock::Role::pedal);
        slot.prepare ({ 48000.0, 256 });
        slot.setOutputMode (NamBlock::Output::raw);
        slot.setMix (0.5f);

        std::string error;
        slot.publishModel (NamModel::load (writeIdentityNam(), 48000.0, 256, error, oversampling));
        CHECK (slot.modelInfo() != nullptr && slot.modelInfo()->oversampling == oversampling);

        std::vector<float> l (256), r (256);
        double in = 0.0, out = 0.0;
        for (int b = 0; b < 60; ++b)
        {
            for (int i = 0; i < 256; ++i)
                l[(size_t) i] = r[(size_t) i] = 0.25f * (float) std::sin (kTwoPi * freq * (b * 256 + i) / 48000.0);
            if (b >= 30) for (float v : l) in += (double) v * v;

            StereoBlock block { l.data(), r.data(), 256 };
            slot.process (block, {});

            if (b >= 30) for (float v : l) out += (double) v * v;
        }
        return 10.0 * std::log10 (out / in);
    }
}

NMP_TEST (Platz_UeberabgetastetBleibtDieBlendeOhneKammfilter)
{
    // Das Netz rechnet mit Oversampling und kommt gut drei Samples später
    // heraus. Nähme der trockene Anteil nicht denselben Weg, ergäbe MIX 50 %
    // einen Kammfilter mit der ersten Kerbe um 7 kHz. Mit einem durchreichenden
    // Capture muss die Blende über den ganzen Hörbereich eben bleiben.
    for (int factor : { 2, 4 })
    {
        double worst = 0.0;
        for (double f : { 200.0, 1000.0, 5000.0, 7000.0, 10000.0, 15000.0 })
            worst = std::max (worst, std::abs (blendedGainDb (f, factor)));

        std::printf ("        %dx: Blende 50 %% weicht höchstens %.3f dB ab\n", factor, worst);
        CHECK_MSG (worst < 0.1, std::to_string (factor) + "x: " + fmt (worst) + " dB: ein Kammfilter");
    }
}
