// EQ: die gezeichnete Kurve gegen das, was der Kern rechnet.

#include "TestSignals.h"
#include "namplify/blocks/Eq.h"
#include "namplify/engine/GlobalEq.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace nmp;
using namespace nmptest;

namespace
{
    /// Frequenzgang eines Prozessors messen: Impuls hinein, FFT heraus.
    std::vector<double> measureResponse (const std::function<void (float*, float*, int)>& run2,
                                         double sr, int order, std::vector<double>& freqs)
    {
        // Links und rechts brauchen eigene Puffer. Mit demselben Zeiger zweimal
        // läuft das Signal durch beide Filterhälften, und der gemessene Gang
        // ist quadriert.
        const int n = 1 << order;
        std::vector<float> imp ((size_t) n, 0.0f), other ((size_t) n, 0.0f);
        imp[0] = 1.0f;
        other[0] = 1.0f;
        run2 (imp.data(), other.data(), n);

        RealFft fft;
        fft.prepare (order);
        std::vector<float> re ((size_t) fft.numBins()), im ((size_t) fft.numBins());
        fft.forward (imp.data(), re.data(), im.data());

        std::vector<double> mag ((size_t) fft.numBins());
        freqs.resize ((size_t) fft.numBins());
        for (int k = 0; k < fft.numBins(); ++k)
        {
            mag[(size_t) k] = std::hypot (re[(size_t) k], im[(size_t) k]);
            freqs[(size_t) k] = (double) k * sr / n;
        }
        return mag;
    }
}

NMP_TEST (GrafikEq_ZweiNachbarbaenderErgebenEineGlatteKurve)
{
    // Zwei benachbarte angehobene Bänder müssen eine glatte Kurve ergeben,
    // keine Doppelspitze.
    const double sr = 48000.0;

    for (int band = 0; band < 6; ++band)
    {
        for (float gain : { 6.0f, 12.0f })
        {
            EqBlock eq;
            eq.setMode (EqMode::sevenBand);
            eq.prepare ({ sr, 512 });
            eq.setBand7 (band, gain);
            eq.setBand7 (band + 1, gain);

            const double f1 = kEq7Freqs[band];
            const double f2 = kEq7Freqs[band + 1];

            // Zwischen den beiden Mitten darf die Kurve nicht einbrechen.
            double atF1 = gainToDb (eq.magnitudeAt (f1));
            double atF2 = gainToDb (eq.magnitudeAt (f2));
            double lowest = 1.0e9;
            for (int s = 1; s < 32; ++s)
            {
                const double f = f1 * std::pow (f2 / f1, (double) s / 32.0);
                lowest = std::min (lowest, gainToDb (eq.magnitudeAt (f)));
            }

            const double dip = std::min (atF1, atF2) - lowest;
            if (band == 0 && gain > 11.0f)
                std::printf ("        %.0f/%.0f Hz bei %+.0f dB: Spitzen %+.1f/%+.1f, Einbruch %.2f dB\n",
                             f1, f2, gain, atF1, atF2, dip);

            CHECK_MSG (dip < 1.0,
                       "zwischen " + fmt (f1) + " und " + fmt (f2) + " Hz bricht die Kurve um "
                       + fmt (dip) + " dB ein: das ist eine Doppelspitze, keine glatte Kurve");
        }
    }
}

NMP_TEST (GrafikEq_GerechnetesUndGezeichnetesStimmenUeberein)
{
    const double sr = 48000.0;

    EqBlock eq;
    eq.setMode (EqMode::sevenBand);
    eq.prepare ({ sr, 1 << 15 });
    const float gains[7] = { 6.0f, -4.0f, 9.0f, 0.0f, -12.0f, 3.0f, 7.0f };
    for (int i = 0; i < 7; ++i) eq.setBand7 (i, gains[i]);

    std::vector<double> freqs;
    const auto mag = measureResponse (
        [&] (float* l, float* r, int n) { StereoBlock b { l, r, n }; eq.process (b, {}); },
        sr, 15, freqs);

    double worst = 0.0;
    for (size_t k = 1; k < mag.size(); ++k)
    {
        if (freqs[k] < 25.0 || freqs[k] > 16000.0) continue;
        const double drawn = gainToDb (eq.magnitudeAt (freqs[k]));
        worst = std::max (worst, std::abs (gainToDb (mag[k]) - drawn));
    }

    CHECK_MSG (worst < 0.1, "bis zu " + fmt (worst) + " dB Unterschied zwischen"
                            " gerechneter und gezeichneter Kurve");
}

NMP_TEST (GlobalerEq_GezeichneteKurveIstDieEchte)
{
    // Die Bänder überlappen: zwei benachbarte Anhebungen ergeben in der Mitte
    // mehr als jede für sich. Die Kurve zeigt die echte Summe.
    const double sr = 48000.0;

    GlobalEq eq;
    eq.prepare (sr);
    eq.setEnabled (true);

    EqCurve c;
    c.bands[0] = { 60.0f,   0.0f, 0.7f, FilterShape::lowCut };
    c.bands[1] = { 180.0f,  5.0f, 1.2f, FilterShape::bell };
    c.bands[2] = { 320.0f,  5.0f, 1.2f, FilterShape::bell };   // Nachbar, überlappt
    c.bands[3] = { 900.0f, -7.0f, 2.5f, FilterShape::bell };
    c.bands[4] = { 2500.0f, 4.0f, 0.9f, FilterShape::highShelf };
    c.bands[5] = { 6000.0f,-3.0f, 1.4f, FilterShape::bell };
    c.bands[6] = { 13000.0f,0.0f, 0.7f, FilterShape::highCut };
    eq.setCurve (c);

    std::vector<double> freqs;
    const auto mag = measureResponse (
        [&] (float* l, float* r, int n) { StereoBlock b { l, r, n }; eq.process (b); },
        sr, 15, freqs);

    double worst = 0.0;
    for (size_t k = 1; k < mag.size(); ++k)
    {
        if (freqs[k] < 25.0 || freqs[k] > 18000.0) continue;
        worst = std::max (worst, std::abs (gainToDb (mag[k]) - eq.magnitudeDbAt (freqs[k])));
    }

    CHECK_MSG (worst < 0.1, "bis zu " + fmt (worst) + " dB Unterschied");

    // Die Überlappung findet wirklich statt: zwischen 180 und 320 Hz steht mehr
    // als die 5 dB eines einzelnen Bandes.
    const double between = eq.magnitudeDbAt (240.0);
    std::printf ("        zwei Bänder à +5 dB ergeben bei 240 Hz %+.1f dB\n", between);
    CHECK_GT (between, 6.0);
}

NMP_TEST (GlobalerEq_MitgelieferteKurvenSindZurueckhaltend)
{
    // Mitgelieferte Kurven bleiben zurückhaltend, keine fährt ±12 dB auf.
    for (int i = 0; i < numFactoryCurves(); ++i)
    {
        const auto& fc = factoryCurve (i);
        for (int b = 0; b < EqCurve::kBands; ++b)
            CHECK_MSG (std::abs (fc.curve.bands[b].gainDb) <= 6.0f,
                       std::string (fc.name) + ": Band " + std::to_string (b) + " fährt "
                       + fmt (fc.curve.bands[b].gainDb) + " dB auf");
    }

    // FLAT muss wirklich gerade sein.
    const auto* flat = findFactoryCurve ("FLAT");
    CHECK (flat != nullptr);
    if (flat)
    {
        GlobalEq eq;
        eq.prepare (48000.0);
        eq.setCurve (flat->curve);
        for (double f : { 40.0, 200.0, 1000.0, 5000.0, 15000.0 })
            CHECK_NEAR (eq.magnitudeDbAt (f), 0.0, 0.01);
    }
}

NMP_TEST (DreibandEq_IstMusikalisch)
{
    const double sr = 48000.0;
    EqBlock eq;
    eq.setMode (EqMode::threeBand);
    eq.prepare ({ sr, 512 });

    eq.setThreeBand (12.0f, 0.0f, 0.0f);
    CHECK_GT (gainToDb (eq.magnitudeAt (60.0)), 10.0);
    CHECK_LT (std::abs (gainToDb (eq.magnitudeAt (5000.0))), 0.5);

    eq.setThreeBand (0.0f, 0.0f, 12.0f);
    CHECK_GT (gainToDb (eq.magnitudeAt (10000.0)), 10.0);
    CHECK_LT (std::abs (gainToDb (eq.magnitudeAt (80.0))), 0.5);

    eq.setThreeBand (0.0f, -12.0f, 0.0f);
    CHECK_LT (gainToDb (eq.magnitudeAt (800.0)), -10.0);
}
