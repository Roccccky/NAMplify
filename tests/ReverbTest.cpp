// Hall: Nachhallzeit je Typ (Soll gegen Ist), dazu Präsenz, MIX, TONE und die
// Streuung der Feder.

#include "TestSignals.h"
#include "namplify/blocks/Reverb.h"
#include "namplify/dsp/Biquad.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace nmp;
using namespace nmptest;

namespace
{
    std::unique_ptr<ReverbBlock> makeReverb (ReverbType t, float timeSec, float tone,
                                             float mix, double sr = 48000.0)
    {
        auto rv = std::make_unique<ReverbBlock>();
        const auto def = reverbDefaults (t);
        rv->setType (t);
        rv->setTimeSec (timeSec);
        rv->setSize (def.size);
        rv->setBoing (def.boing);
        rv->setTone (tone);
        rv->setMix (mix);
        rv->prepare ({ sr, 256 });
        return rv;
    }

    /// Nur der nasse Anteil. Der trockene Weg steht fest auf eins und hängt
    /// nicht am MIX, deshalb ist die Differenz zweier Läufe mit verschiedenem
    /// MIX genau der Hall.
    std::vector<float> wetOnly (ReverbType t, float timeSec, float tone,
                                const std::vector<float>& input, double sr = 48000.0)
    {
        auto dry = makeReverb (t, timeSec, tone, 0.0f, sr);
        auto wet = makeReverb (t, timeSec, tone, 1.0f, sr);

        Buf a ((int) input.size()), b ((int) input.size());
        a.l = a.r = input;
        b.l = b.r = input;

        runBlock (*dry, a);
        runBlock (*wet, b);

        std::vector<float> out ((size_t) input.size());
        for (size_t i = 0; i < out.size(); ++i) out[i] = b.l[i] - a.l[i];
        return out;
    }

    /// Nachhallzeit nach Schroeder: rückwärts aufintegrieren, die Strecke von
    /// −5 dB bis −25 dB messen und auf −60 dB hochrechnen.
    double measureRt60 (std::vector<float> ir, double sr)
    {
        // Auf ein Band um 200 … 800 Hz eingrenzen. Die Dämpfung wirkt oben, die
        // Zeit ist für tiefe Anteile ausgelegt und wird dort gemessen.
        Biquad hp, lp1, lp2;
        hp .setCoeffs (designHighPass (200.0, 0.707, sr));
        lp1.setCoeffs (designLowPass  (800.0, 0.707, sr));
        lp2.setCoeffs (designLowPass  (800.0, 0.707, sr));
        for (auto& v : ir) v = lp2.process (lp1.process (hp.process (v)));

        std::vector<double> back ((size_t) ir.size() + 1, 0.0);
        for (int i = (int) ir.size() - 1; i >= 0; --i)
            back[(size_t) i] = back[(size_t) i + 1] + (double) ir[(size_t) i] * ir[(size_t) i];

        const double total = back[0];
        if (total <= 0.0) return 0.0;

        auto timeAt = [&] (double db) -> double
        {
            const double target = total * std::pow (10.0, db / 10.0);
            for (size_t i = 0; i < back.size(); ++i)
                if (back[i] <= target) return (double) i / sr;
            return -1.0;
        };

        const double t5  = timeAt (-5.0);
        const double t25 = timeAt (-25.0);
        if (t5 < 0.0 || t25 <= t5) return 0.0;

        return (t25 - t5) * 3.0;
    }

    /// Spektraler Schwerpunkt eines Abschnitts als Maß für die Helligkeit.
    double centroid (const std::vector<float>& sig, int from, int len, double sr)
    {
        int order = 0;
        while ((1 << order) < len) ++order;
        if (from + (1 << order) > (int) sig.size()) return 0.0;

        const auto sp = analyse (sig, from, order, sr);

        double num = 0.0, den = 0.0;
        for (size_t k = 1; k < sp.mag.size(); ++k)
        {
            const double f = (double) k * sp.binHz;
            if (f < 120.0 || f > 12000.0) continue;
            const double m = sp.mag[k] * sp.mag[k];
            num += f * m;
            den += m;
        }
        return den > 0.0 ? num / den : 0.0;
    }
}

NMP_TEST (Hall_TrifftDieNachhallzeit)
{
    const double sr = 48000.0;

    struct Case { ReverbType t; float sec; };
    const Case cases[] = {
        { ReverbType::room,  0.6f }, { ReverbType::room,  0.9f }, { ReverbType::room,  2.0f },
        { ReverbType::hall,  1.5f }, { ReverbType::hall,  2.6f }, { ReverbType::hall,  5.0f },
        { ReverbType::plate, 0.8f }, { ReverbType::plate, 1.8f }, { ReverbType::plate, 4.0f },
        { ReverbType::spring,1.2f }, { ReverbType::spring,2.2f },
    };

    for (auto c : cases)
    {
        const int n = (int) (sr * (c.sec * 2.5 + 0.5));
        std::vector<float> imp ((size_t) n, 0.0f);
        imp[0] = 1.0f;

        const auto ir = wetOnly (c.t, c.sec, 0.5f, imp, sr);
        const double measured = measureRt60 (ir, sr);

        std::printf ("        %-7s  Soll %.2f s   Ist %.2f s   (%+.0f %%)\n",
                     reverbTypeName (c.t), c.sec, measured,
                     100.0 * (measured / c.sec - 1.0));

        CHECK_MSG (std::abs (measured / c.sec - 1.0) < 0.22,
                   std::string (reverbTypeName (c.t)) + ": Soll " + fmt (c.sec)
                   + " s, gemessen " + fmt (measured) + " s");
    }
}

NMP_TEST (Hall_MixGibtHallDazuStattLautstaerkeWegzunehmen)
{
    const double sr = 48000.0;
    const int n = 1 << 17;

    for (int ti = 0; ti < (int) ReverbType::count; ++ti)
    {
        const auto t = (ReverbType) ti;
        const auto def = reverbDefaults (t);

        double prev = 0.0;
        for (float mix : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto rv = makeReverb (t, def.timeSec, def.tone, mix, sr);
            Buf buf (n);
            buf.fillNoise (0.2f, 7u);
            runBlock (*rv, buf);

            const double level = rms (buf.l, n / 2, n);
            if (mix > 0.0f)
                CHECK_MSG (level >= prev * 0.995,
                           std::string (reverbTypeName (t)) + ": bei MIX " + fmt (mix)
                           + " ist es leiser geworden (" + fmt (gainToDb (level) - gainToDb (prev))
                           + " dB): MIX soll Hall dazugeben, nicht Pegel wegnehmen");
            prev = level;
        }
    }
}

NMP_TEST (Hall_KurzeZeitenWerdenNichtDuenn)
{
    // ROOM und HALL müssen bei gleichem Reglerstand gleich präsent sein.
    const double sr = 48000.0;
    const int n = 1 << 17;

    std::vector<float> noise ((size_t) n);
    Xorshift rng (11u);
    for (auto& v : noise) v = rng.nextBipolar() * 0.2f;

    double lvl[4];
    for (int ti = 0; ti < (int) ReverbType::count; ++ti)
    {
        const auto t = (ReverbType) ti;
        const auto def = reverbDefaults (t);
        const auto wet = wetOnly (t, def.timeSec, def.tone, noise, sr);
        lvl[ti] = gainToDb (rms (wet, n / 2, n));
        std::printf ("        %-7s  nasser Pegel %+.1f dB  (TIME %.2f s)\n",
                     reverbTypeName (t), lvl[ti], def.timeSec);
    }

    const double spread = *std::max_element (lvl, lvl + 4) - *std::min_element (lvl, lvl + 4);
    CHECK_MSG (spread < 4.0,
               "Die vier Geräte liegen " + fmt (spread)
               + " dB auseinander: bei gleichem Reglerstand soll das nicht sein");

    // Der kurze darf gegen den langen nicht abfallen.
    CHECK_MSG (std::abs (lvl[(int) ReverbType::room] - lvl[(int) ReverbType::hall]) < 3.0,
               "ROOM und HALL liegen " + fmt (lvl[0] - lvl[1]) + " dB auseinander");
}

NMP_TEST (Hall_ToneWirktUeberDenGanzenWeg)
{
    const double sr = 48000.0;
    const int n = 1 << 17;

    std::vector<float> noise ((size_t) n);
    Xorshift rng (23u);
    for (auto& v : noise) v = rng.nextBipolar() * 0.2f;

    for (int ti = 0; ti < (int) ReverbType::count; ++ti)
    {
        const auto t = (ReverbType) ti;
        const auto def = reverbDefaults (t);

        double c[5];
        int i = 0;
        std::printf ("        %-7s ", reverbTypeName (t));
        for (float tone : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const auto wet = wetOnly (t, def.timeSec, tone, noise, sr);
            c[i] = centroid (wet, n / 2, 1 << 14, sr);
            std::printf (" %.0f", c[i]);
            ++i;
        }
        std::printf (" Hz\n");

        for (int k = 1; k < 5; ++k)
            CHECK_MSG (c[k] > c[k - 1] * 1.04,
                       std::string (reverbTypeName (t)) + ": zwischen TONE "
                       + fmt (0.25 * (k - 1)) + " und " + fmt (0.25 * k)
                       + " ändert sich fast nichts (" + fmt (c[k - 1]) + " → " + fmt (c[k])
                       + " Hz): der Regler muss über seinen ganzen Weg etwas bewirken");
    }
}

NMP_TEST (Feder_BoingtWirklich)
{
    // Beim Federhall kommt ein Anschlag als abwärts laufender Zirp zurück.
    //
    // Gemessen wird der Laufzeitunterschied zwischen den Bändern, nicht ob es
    // dunkler wird (das täte jede Dämpfung). Streuung heißt, dass hohe Anteile
    // schneller durch die Feder laufen als tiefe. Maß ist der zeitliche
    // Schwerpunkt der Energie je Band. Das Ausschwingen der Messfilter stört
    // nicht, weil beide Bänder gleich behandelt werden und nur der Unterschied
    // zählt.
    const double sr = 48000.0;
    const int n = 1 << 15;

    std::vector<float> imp ((size_t) n, 0.0f);
    imp[0] = 1.0f;

    auto arrivalMs = [&] (const std::vector<float>& ir, double centreHz, double windowMs) -> double
    {
        Biquad hp1, hp2, lp1, lp2;
        hp1.setCoeffs (designHighPass (centreHz / 1.4, 0.707, sr));
        hp2.setCoeffs (designHighPass (centreHz / 1.4, 0.707, sr));
        lp1.setCoeffs (designLowPass  (centreHz * 1.4, 0.707, sr));
        lp2.setCoeffs (designLowPass  (centreHz * 1.4, 0.707, sr));

        const int win = (int) (sr * windowMs * 0.001);
        double num = 0.0, den = 0.0;
        for (int i = 0; i < win && i < n; ++i)
        {
            float v = ir[(size_t) i];
            v = lp2.process (lp1.process (hp2.process (hp1.process (v))));
            const double e = (double) v * v;
            num += e * (double) i;
            den += e;
        }
        return den > 0.0 ? 1000.0 * (num / den) / sr : -1.0;
    };

    auto spreadOf = [&] (const std::vector<float>& ir) -> double
    {
        return arrivalMs (ir, 500.0, 160.0) - arrivalMs (ir, 4000.0, 160.0);
    };

    const auto springLo = wetOnly (ReverbType::spring, 2.2f, 0.30f, imp, sr);
    const auto plateIr  = wetOnly (ReverbType::plate,  2.2f, 0.30f, imp, sr);

    // Dieselbe Feder ohne Streuung: gleiche Leitung, gleiche Dämpfung, nur
    // BOING ganz zurück. Der Unterschied ist allein die Streuung.
    std::vector<float> springNoBoing, springFullBoing;
    {
        auto mk = [&] (float boing, float mix)
        {
            auto rv = std::make_unique<ReverbBlock>();
            rv->setType (ReverbType::spring);
            rv->setTimeSec (2.2f);
            rv->setBoing (boing);
            rv->setTone (0.30f);
            rv->setMix (mix);
            rv->prepare ({ sr, 256 });
            return rv;
        };
        for (float boing : { 0.0f, 1.0f })
        {
            auto dry = mk (boing, 0.0f);
            auto wet = mk (boing, 1.0f);
            Buf A (n), B (n);
            A.l = A.r = imp; B.l = B.r = imp;
            runBlock (*dry, A); runBlock (*wet, B);
            std::vector<float> out ((size_t) n);
            for (int i = 0; i < n; ++i) out[(size_t) i] = B.l[(size_t) i] - A.l[(size_t) i];
            (boing == 0.0f ? springNoBoing : springFullBoing) = out;
        }
    }

    const double sSpread  = spreadOf (springLo);
    const double pSpread  = spreadOf (plateIr);
    const double b0Spread = spreadOf (springNoBoing);
    const double b1Spread = spreadOf (springFullBoing);

    std::printf ("        Laufzeitunterschied 500 Hz gegen 4 kHz:\n");
    std::printf ("          SPRING (BOING 0.62)  %+.1f ms\n", sSpread);
    std::printf ("          SPRING (BOING 0.00)  %+.1f ms\n", b0Spread);
    std::printf ("          SPRING (BOING 1.00)  %+.1f ms\n", b1Spread);
    std::printf ("          PLATE  (ohne Feder)  %+.1f ms\n", pSpread);

    CHECK_MSG (sSpread > pSpread + 4.0,
               "die Feder streut nicht mehr als eine Platte (" + fmt (sSpread) + " gegen "
               + fmt (pSpread) + " ms): dann klingt es nach Blechkasten und nicht nach Feder");

    CHECK_MSG (b1Spread > b0Spread + 3.0,
               "BOING ändert die Streuung kaum (" + fmt (b0Spread) + " → " + fmt (b1Spread)
               + " ms): der Regler regelt dann nichts");

    // Der Zirp muss hörbar abwärts laufen: der Schwerpunkt fällt.
    const int win = 1 << 11;
    const double c0 = centroid (springLo, win, win, sr);
    const double c1 = centroid (springLo, win * 4, win, sr);
    std::printf ("        Schwerpunkt: %.0f → %.0f Hz\n", c0, c1);
    CHECK_MSG (c1 < c0 * 0.92, "der Zirp läuft nicht abwärts");
}

// Die Engine setzt in jedem Puffer alle Parameter, auch ohne Änderung
// (Engine::applyParams). Gleiche Werte müssen den Hall unverändert lassen. Ein
// Neuaufbau je Puffer setzt Diffusoren, Federkette und Modulation zurück und
// knattert im Puffertakt.
NMP_TEST (Hall_ParameterJedenPufferKostetNichts)
{
    constexpr double sr = 48000.0;
    constexpr int    chunk = 64;
    const int        n = (int) (1.5 * sr);

    for (auto type : { ReverbType::room, ReverbType::hall,
                       ReverbType::plate, ReverbType::spring })
    {
        const auto def = reverbDefaults (type);

        auto make = [&]
        {
            auto rv = std::make_unique<ReverbBlock>();
            rv->setType (type);
            rv->setTimeSec (def.timeSec);
            rv->setSize (def.size);
            rv->setBoing (def.boing);
            rv->setTone (def.tone);
            rv->setMix (1.0f);
            rv->prepare ({ sr, chunk });
            return rv;
        };

        // Ein Anschlag, dann Stille: nur die Fahne.
        std::vector<float> imp ((size_t) n, 0.0f);
        imp[0] = 1.0f;

        Buf ruhig (n), gepusht (n);
        ruhig.l = ruhig.r = imp;
        gepusht.l = gepusht.r = imp;

        auto a = make();
        runBlock (*a, ruhig, chunk);

        auto b = make();
        {
            BlockContext ctx;
            int pos = 0;
            while (pos < n)
            {
                const int c = std::min (chunk, n - pos);
                // Wie die Engine: jeden Puffer dieselben Werte.
                b->setTimeSec (def.timeSec);
                b->setSize (def.size);
                b->setBoing (def.boing);
                b->setTone (def.tone);
                b->setMix (1.0f);

                auto v = gepusht.view (pos, c);
                b->process (v, ctx);
                pos += c;
            }
        }

        double worst = 0.0;
        int    at = 0;
        for (int i = 0; i < n; ++i)
        {
            const double d = std::fabs ((double) ruhig.l[(size_t) i] - gepusht.l[(size_t) i]);
            if (d > worst) { worst = d; at = i; }
        }

        std::printf ("        %-7s größter Unterschied %.2e (bei %.0f ms)\n",
                     reverbTypeName (type), worst, 1000.0 * at / sr);

        CHECK_MSG (worst < 1e-6,
                   std::string (reverbTypeName (type))
                   + ": die Fahne hängt davon ab, ob die Parameter jeden Puffer "
                     "hereinkommen: dann legt sich der Hall im Puffertakt neu aus");
    }
}

NMP_TEST (Hall_WerkswerteStehenNurAnEinerStelle)
{
    struct Row { ReverbType t; float time, size, boing, tone, mix; };
    const Row want[] = {
        { ReverbType::room,   0.90f, 0.35f, 0.0f,  0.62f, 0.30f },
        { ReverbType::hall,   2.60f, 0.55f, 0.0f,  0.50f, 0.22f },
        { ReverbType::plate,  1.80f, 0.45f, 0.0f,  0.72f, 0.18f },
        { ReverbType::spring, 2.20f, 0.0f,  0.62f, 0.30f, 0.32f },
    };

    for (auto w : want)
    {
        const auto d = reverbDefaults (w.t);
        CHECK_NEAR (d.timeSec, w.time,  1.0e-4);
        CHECK_NEAR (d.size,    w.size,  1.0e-4);
        CHECK_NEAR (d.boing,   w.boing, 1.0e-4);
        CHECK_NEAR (d.tone,    w.tone,  1.0e-4);
        CHECK_NEAR (d.mix,     w.mix,   1.0e-4);
    }
}
