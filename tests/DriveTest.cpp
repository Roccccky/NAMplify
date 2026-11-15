// Drive-Modelle: Obertöne und Frequenzgang.

#include "TestSignals.h"
#include "namplify/blocks/Drive.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace nmp;
using namespace nmptest;

namespace
{
    DriveBlock makeDrive (DriveModel m, double sr = 48000.0)
    {
        DriveBlock d;
        const auto def = driveDefaults (m);
        d.setModel (m);
        d.setGainDb (def.gainDb);
        d.setLevelDb (def.levelDb);
        d.setTone (def.tone);
        d.setBody (def.body);
        d.prepare ({ sr, 512 });
        return d;
    }
}

NMP_TEST (Zerre_ErzeugtGeradeObertoene)
{
    // Eine punktsymmetrische Kennlinie erzeugt nur ungerade Obertöne und klingt
    // nach Rechteck. Die zweite Harmonische macht den Ton voll.
    const double sr = 48000.0;
    const double f0 = 220.0;

    for (int mi = 0; mi < (int) DriveModel::count; ++mi)
    {
        const auto model = (DriveModel) mi;
        auto d = makeDrive (model, sr);

        Buf buf (1 << 17);
        buf.fillSine (f0, sr, 0.25f);
        runBlock (d, buf);

        const auto sp = analyse (buf.l, 1 << 15, 16, sr);

        const double h1 = sp.at (f0);
        double even = 0.0, odd = 0.0;
        for (int k = 2; k <= 12; ++k)
        {
            const double m = sp.at (f0 * k);
            if (k % 2 == 0) even += m * m; else odd += m * m;
        }

        const double evenDb = 20.0 * std::log10 (std::sqrt (even) / std::max (1.0e-12, h1));
        const double oddDb  = 20.0 * std::log10 (std::sqrt (odd)  / std::max (1.0e-12, h1));

        std::printf ("        %-7s  H1 %.4f   gerade %+.1f dB   ungerade %+.1f dB\n",
                     driveModelName (model), h1, evenDb, oddDb);

        // 1. Es muss überhaupt zerren.
        // 2. Die geraden Obertöne dürfen nicht weit unter den ungeraden liegen.
        // Bei einer punktsymmetrischen Kennlinie fehlten sie ganz.
        CHECK_MSG (oddDb > -50.0, std::string (driveModelName (model)) + ": kaum Obertöne");
        CHECK_MSG (evenDb > oddDb - 12.0,
                   std::string (driveModelName (model))
                   + ": geradzahlige Obertöne " + fmt (oddDb - evenDb)
                   + " dB unter den ungeradzahligen: die Kennlinie ist zu"
                     " symmetrisch, das klingt hohl");
    }
}

NMP_TEST (Zerre_HatKeineSpiegelfrequenzen)
{
    // 7 kHz hinein: alle echten Obertöne liegen bei 14 kHz und darüber. Was
    // darunter auftaucht, ist Aliasing.
    const double sr = 48000.0;
    const double f0 = 7000.0;

    for (int mi = 0; mi < (int) DriveModel::count; ++mi)
    {
        const auto model = (DriveModel) mi;
        auto d = makeDrive (model, sr);

        Buf buf (1 << 17);
        buf.fillSine (f0, sr, 0.5f);
        runBlock (d, buf);

        const auto sp = analyse (buf.l, 1 << 15, 16, sr);

        const double h1 = sp.at (f0);
        const double aliasEnergy = sp.bandEnergy (250.0, 6000.0);
        const double aliasDb = 10.0 * std::log10 (std::max (1.0e-24, aliasEnergy)
                                                  / std::max (1.0e-24, h1 * h1));

        std::printf ("        %-7s  Spiegelfrequenzen %+.1f dB\n", driveModelName (model), aliasDb);

        CHECK_MSG (aliasDb < -60.0,
                   std::string (driveModelName (model)) + ": " + fmt (aliasDb)
                   + " dB unter dem Grundton: das sirrt bei Akkorden in hoher Lage");
    }
}

NMP_TEST (Zerre_HatKeinLochImKoerper)
{
    // Gemessen bei kleinem Pegel, damit der Frequenzgang und nicht die
    // Kennlinie gemessen wird.
    const double sr = 48000.0;

    const double freqs[] = { 70, 82, 98, 110, 131, 147, 165, 196, 220, 262,
                             294, 330, 392, 440, 523, 660, 880, 1200, 1600 };
    constexpr int nf = (int) (sizeof (freqs) / sizeof (freqs[0]));

    for (int mi = 0; mi < (int) DriveModel::count; ++mi)
    {
        const auto model = (DriveModel) mi;

        double resp[nf];
        for (int i = 0; i < nf; ++i)
        {
            auto d = makeDrive (model, sr);
            Buf buf (1 << 15);
            buf.fillSine (freqs[i], sr, 0.01f);
            runBlock (d, buf);
            resp[i] = 20.0 * std::log10 (std::max (1.0e-12, rms (buf.l, 1 << 13, 1 << 15)));
        }

        // Auf 1 kHz beziehen
        double ref = resp[nf - 2];
        std::printf ("        %-7s ", driveModelName (model));
        for (int i = 0; i < nf; ++i) std::printf ("%+.0f ", resp[i] - ref);
        std::printf ("\n");

        // Ein Loch ist ein lokales Minimum, das links und rechts deutlich
        // überragt wird. Ein gleichmäßiger Abfall zu den Tiefen zählt nicht,
        // der ist gewollt.
        for (int i = 1; i < nf - 1; ++i)
        {
            if (freqs[i] < 70.0 || freqs[i] > 400.0) continue;
            const double dip = std::min (resp[i - 1], resp[i + 1]) - resp[i];
            CHECK_MSG (dip < 3.0,
                       std::string (driveModelName (model)) + ": Einbruch von "
                       + fmt (dip) + " dB bei " + fmt (freqs[i]) + " Hz");
        }

        // Der Bassbereich darf nicht ausgehöhlt sein.
        const double bodyDb = resp[8] - ref;   // 220 Hz gegen 1 kHz
        CHECK_MSG (bodyDb > -18.0,
                   std::string (driveModelName (model)) + ": bei 220 Hz "
                   + fmt (bodyDb) + " dB unter 1 kHz: da bleibt kein Körper übrig");
    }
}

NMP_TEST (Zerre_WerkswerteStehenNurAnEinerStelle)
{
    // Erwartete Werkswerte je Modell.
    struct Row { DriveModel m; float gain, level, tone, body; };
    const Row want[] = {
        { DriveModel::scream, 14.0f,  4.0f, 0.55f, 0.50f },
        { DriveModel::warm,   12.0f, -0.2f, 0.50f, 0.45f },
        { DriveModel::clean,   9.0f, -4.3f, 0.62f, 0.35f },
        { DriveModel::crush,  22.0f, -2.5f, 0.45f, 0.60f },
    };

    for (auto w : want)
    {
        const auto d = driveDefaults (w.m);
        CHECK_NEAR (d.gainDb,  w.gain,  1.0e-4);
        CHECK_NEAR (d.levelDb, w.level, 1.0e-4);
        CHECK_NEAR (d.tone,    w.tone,  1.0e-4);
        CHECK_NEAR (d.body,    w.body,  1.0e-4);
    }
}

NMP_TEST (Zerre_HebtWieEinTreter)
{
    // Ein Overdrive vor dem Amp hebt um ein paar dB. Bei Werkseinstellung und
    // −18 dBFS RMS am Eingang darf er den Amp nicht zwölf dB heißer anfahren
    // als die Gitarre selbst, sonst wird der Klang matschig.
    struct Row { DriveModel m; float lo, hi; };
    const Row want[] = {
        { DriveModel::scream, 0.0f, 6.0f },
        { DriveModel::warm,   0.0f, 6.0f },
        { DriveModel::clean, -6.0f, 0.0f },
        { DriveModel::crush,  0.0f, 6.0f },
    };

    for (auto w : want)
    {
        auto d = makeDrive (w.m);
        Buf b (48000);
        b.fillSine (196.0, 48000.0, std::pow (10.0f, -18.0f / 20.0f) * 1.41421f);
        const double in = rms (b.l, 24000, 48000);
        runBlock (d, b);
        const double gainDb = 20.0 * std::log10 (rms (b.l, 24000, 48000) / in);
        CHECK_MSG (gainDb >= w.lo && gainDb <= w.hi,
                   std::string (driveModelName (w.m)) + " hebt um " + std::to_string (gainDb) + " dB");
    }
}
