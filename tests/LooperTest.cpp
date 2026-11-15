// Looper, Spread und das Delay am TIME-Regler.

#include "TestSignals.h"
#include <chrono>
#include "namplify/engine/Looper.h"
#include "namplify/engine/Spread.h"
#include "namplify/blocks/Delay.h"
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
    /// Ein kurzer Anschlag, an dem sich die Eins wiederfinden lässt.
    void putClick (Buf& b, int at, float amp = 0.9f)
    {
        for (int i = 0; i < 40 && at + i < b.size(); ++i)
        {
            const float e = amp * (float) std::exp (-0.18 * i);
            b.l[(size_t) (at + i)] += e;
            b.r[(size_t) (at + i)] += e;
        }
    }

    /// Anteil eines einzelnen Tons in einem Abschnitt (Goertzel). Der Abschnitt
    /// darf jede Länge haben.
    double tonAt (const std::vector<float>& v, int from, int count, double hz, double sr)
    {
        const double w = kTwoPi * hz / sr;
        const double c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const double s0 = (double) v[(size_t) (from + i)] + c * s1 - s2;
            s2 = s1; s1 = s0;
        }
        return 2.0 * std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / std::max (1, count);
    }

    int firstPeak (const std::vector<float>& v, int from, int to, float thresh)
    {
        for (int i = from; i < to && i < (int) v.size(); ++i)
            if (std::abs (v[(size_t) i]) > thresh) return i;
        return -1;
    }
}

NMP_TEST (Looper_DieEinsLandetAufDerEins)
{
    // Was auf die Eins gespielt wurde, landet auf der Eins. Die Latenz der
    // Anlage darf sich nicht als Versatz in die Schleife schreiben.
    const double sr = 48000.0;
    const int blockSize = 64;
    const int latency = 512;          // Latenz der Anlage

    Looper lp;
    lp.prepare ({ sr, blockSize });
    lp.setEnabled (true);
    lp.setLatencyCompensation (latency);
    lp.setTrackVolume (0, 1.0f);

    // Der Spieler tritt bei t = 0 und schlägt bei t = 0 an. Hier kommt der
    // Anschlag erst latency Samples später an.
    const int loopLen = (int) (sr * 1.0);
    Buf in (loopLen * 3);

    // Durchgehendes Material, sonst schneidet Auto-Cut die Stille dazwischen
    // weg und der Test misst Auto-Cut statt der Latenz.
    for (int i = 0; i < in.size(); ++i)
        in.l[(size_t) i] = in.r[(size_t) i] = (float) (0.10 * std::sin (kTwoPi * 147.0 * i / sr));

    putClick (in, latency);                       // die Eins
    putClick (in, latency + loopLen / 2, 0.5f);   // die Drei

    int pos = 0;
    lp.tap (0);                                   // Aufnahme an
    bool closed = false;

    Buf out (loopLen * 3);

    while (pos + blockSize <= in.size())
    {
        // Bei genau loopLen wird die Schleife geschlossen.
        if (! closed && pos >= loopLen) { lp.tap (0); closed = true; }

        std::copy (in.l.begin() + pos, in.l.begin() + pos + blockSize, out.l.begin() + pos);
        std::copy (in.r.begin() + pos, in.r.begin() + pos + blockSize, out.r.begin() + pos);

        auto v = out.view (pos, blockSize);
        lp.process (v, {});
        pos += blockSize;
    }

    const auto info = lp.trackInfo (0);
    CHECK_MSG (info.state == LoopState::playing, "die Schleife wurde nicht geschlossen");

    std::printf ("        Schleifenlänge %.3f s (Soll %.3f s)\n",
                 info.lengthSeconds, (double) loopLen / sr);
    CHECK_NEAR (info.lengthSeconds, (double) loopLen / sr, 0.02);

    // Die Wiedergabe beginnt nach dem Nachlauf: beim Schließen plus Latenz plus
    // Nahtüberblendung. Dort muss der Anschlag stehen, ohne Versatz um die
    // Latenz.
    const int playStart = loopLen + latency + lp.seamCrossfadeSamples();

    const int peak = firstPeak (out.l, playStart - 600, playStart + loopLen / 4, 0.30f);
    CHECK_MSG (peak >= 0, "in der Wiedergabe ist kein Anschlag zu finden");
    if (peak < 0) return;

    const int offset = peak - playStart;
    std::printf ("        Anschlag %d Werte hinter der Eins (Laufzeit war %d)\n", offset, latency);

    CHECK_MSG (std::abs (offset) < 96,
               "der Anschlag liegt " + std::to_string (offset)
               + " Werte neben der Eins: die Laufzeit hat sich in die Schleife geschrieben");

    // Die Wiederholung genau eine Schleifenlänge später.
    const int second = firstPeak (out.l, peak + loopLen - 400, peak + loopLen + 400, 0.30f);
    CHECK_MSG (second >= 0, "die Schleife wiederholt sich nicht");
    if (second >= 0)
    {
        std::printf ("        Abstand der Anschläge: %d (Soll %d)\n", second - peak, loopLen);
        CHECK_MSG (std::abs ((second - peak) - loopLen) < 64,
                   "der Abstand ist " + std::to_string (second - peak) + " statt "
                   + std::to_string (loopLen));
    }
}

NMP_TEST (Looper_KeineKlicksAnDerNaht)
{
    // Keine Klicks an Kanten, Übergängen und beim Ausschalten.
    const double sr = 48000.0;
    const int blockSize = 128;

    Looper lp;
    lp.prepare ({ sr, blockSize });
    lp.setEnabled (true);
    lp.setLatencyCompensation (0);
    lp.setTrackVolume (0, 1.0f);

    // Ein durchgehender Ton, dessen Periode nicht ganzzahlig in die
    // Schleifenlänge passt. Dann trifft am Umlauf ein Sprung auf den nächsten.
    const int loopLen = (int) (sr * 0.731);
    Buf in (loopLen * 4);
    for (int i = 0; i < in.size(); ++i)
        in.l[(size_t) i] = in.r[(size_t) i] = (float) (0.5 * std::sin (kTwoPi * 217.0 * i / sr));

    Buf out (in.size());
    int pos = 0;
    lp.tap (0);
    bool closed = false;

    while (pos + blockSize <= in.size())
    {
        if (! closed && pos >= loopLen) { lp.tap (0); closed = true; }
        std::copy (in.l.begin() + pos, in.l.begin() + pos + blockSize, out.l.begin() + pos);
        std::copy (in.r.begin() + pos, in.r.begin() + pos + blockSize, out.r.begin() + pos);
        auto v = out.view (pos, blockSize);
        lp.process (v, {});
        pos += blockSize;
    }

    // Ab hier läuft nur noch die Schleife (der Eingang ist gleich geblieben,
    // aber die Naht liegt im wiedergegebenen Teil).
    const int from = loopLen + loopLen / 2;
    const int to   = std::min (in.size() - 1, from + loopLen * 2);

    double worst = 0.0;
    int worstAt = 0;
    for (int i = from + 1; i < to; ++i)
    {
        const double d = std::abs (out.l[(size_t) i] - out.l[(size_t) (i - 1)]);
        if (d > worst) { worst = d; worstAt = i; }
    }

    // Zwei überlagerte Sinusse bei 217 Hz mit Vollausschlag 1 springen um
    // höchstens 2π·217/48000 ≈ 0,029.
    std::printf ("        größter Sprung in der Wiedergabe: %.4f (bei %d)\n", worst, worstAt);
    CHECK_MSG (worst < 0.06, "an der Naht springt es um " + fmt (worst) + ": das klickt");
}

NMP_TEST (Looper_KeinKnacksBeimAusschalten)
{
    // Der Fußschalter darf die Wiedergabe nicht hart abschneiden. Die Engine
    // ruft weiter process, solange tailActive() es verlangt. Das wird hier
    // nachgestellt.
    const double sr = 48000.0;
    const int blockSize = 128;

    Looper lp;
    lp.prepare ({ sr, blockSize });
    lp.setEnabled (true);
    lp.setLatencyCompensation (0);
    lp.setTrackVolume (0, 1.0f);

    const int loopLen = (int) (sr * 0.5);
    Buf in (loopLen * 3);
    for (int i = 0; i < in.size(); ++i)
        in.l[(size_t) i] = in.r[(size_t) i] = (float) (0.5 * std::sin (kTwoPi * 217.0 * i / sr));

    Buf out (in.size());
    int pos = 0;
    lp.tap (0);
    bool closed = false;

    while (pos + blockSize <= in.size())
    {
        if (! closed && pos >= loopLen) { lp.tap (0); closed = true; }
        std::copy (in.l.begin() + pos, in.l.begin() + pos + blockSize, out.l.begin() + pos);
        std::copy (in.r.begin() + pos, in.r.begin() + pos + blockSize, out.r.begin() + pos);
        auto v = out.view (pos, blockSize);
        lp.process (v, {});
        pos += blockSize;
    }

    CHECK_MSG (lp.trackInfo (0).state == LoopState::playing, "die Schleife läuft nicht");

    // Der Spieler hört auf, ab hier steht im Ausgang nur die Schleife. Mitten
    // in dieser Strecke geht das Pedal aus. Die Engine ruft weiter process,
    // solange tailActive() es verlangt.
    Buf nach ((int) (sr * 0.3));
    int at = 0;
    const int ausAt = (int) (sr * 0.1);
    bool aus = false;
    int leerlauf = 0;

    while (at + blockSize <= nach.size())
    {
        if (! aus && at >= ausAt) { lp.setEnabled (false); aus = true; }
        if (aus && ! lp.tailActive() && ++leerlauf > 3) break;

        auto v = nach.view (at, blockSize);
        if (! aus || lp.tailActive() || leerlauf > 0) lp.process (v, {});
        at += blockSize;
    }

    CHECK_MSG (at > ausAt + blockSize, "nach dem Ausschalten kam kein Puffer mehr");

    double worst = 0.0;
    int worstAt = 0;
    for (int i = 1; i < at; ++i)
    {
        const double d = std::abs (nach.l[(size_t) i] - nach.l[(size_t) (i - 1)]);
        if (d > worst) { worst = d; worstAt = i; }
    }

    std::printf ("        Ausschalten bei %d: größter Sprung %.4f (bei %d von %d)\n",
                 ausAt, worst, worstAt, at);

    // Zwei überlagerte Sinusse bei 217 Hz springen von Sample zu Sample um
    // höchstens 2π·217/48000 ≈ 0,029, die Blende legt kaum etwas drauf. Ohne
    // Blende stünde hier der volle Ausschlag der Schleife.
    CHECK_MSG (worst < 0.06, "beim Ausschalten springt es um " + fmt (worst) + ": das knackt");

    // Danach ist Ruhe.
    float rest = 0.0f;
    for (int i = std::max (0, at - blockSize); i < at; ++i)
        rest = std::max (rest, std::abs (nach.l[(size_t) i]));
    CHECK_MSG (rest < 1.0e-4f, "der Ausklang hört nicht auf");
    CHECK_MSG (! lp.tailActive(), "der Looper meldet ewig, es klinge noch etwas aus");
}

namespace
{
    /// Hilfsläufer für den Looper: schiebt Puffer durch, tippt an den
    /// gewünschten Stellen und sammelt den Ausgang.
    struct Lauf
    {
        Looper& lp;
        double sr;
        int blockSize;
        Buf out;
        int pos = 0;

        Lauf (Looper& l, double s, int bs, int laenge) : lp (l), sr (s), blockSize (bs), out (laenge) {}

        /// n Samples durchrechnen. in liefert das Eingangssignal, sonst Stille.
        void fahre (int n, const Buf* in = nullptr)
        {
            const int bis = std::min (out.size(), pos + n);
            while (pos + blockSize <= bis)
            {
                if (in != nullptr)
                {
                    std::copy (in->l.begin() + pos, in->l.begin() + pos + blockSize, out.l.begin() + pos);
                    std::copy (in->r.begin() + pos, in->r.begin() + pos + blockSize, out.r.begin() + pos);
                }
                auto v = out.view (pos, blockSize);
                lp.process (v, {});
                pos += blockSize;
            }
        }
    };
}

NMP_TEST (Looper_SchliesstAufMusikalischeLaengen)
{
    // Die zweite Spur richtet sich am Zyklus der ersten aus, auf musikalische
    // Längen: ein Viertel, ein halber, ein ganzer Zyklus oder ein Vielfaches.
    // Gemessen ab der letzten gespielten Note, nicht ab dem Tritt. Sonst käme
    // eine Pause vor dem Tritt mit in die Schleife.
    const double sr = 48000.0;
    const int blockSize = 128;
    const int zyklus = (int) (sr * 2.0);          // die erste Spur: zwei Sekunden

    struct Fall { const char* was; double spielt; double dannTritt; double soll; };
    const Fall faelle[] = {
        { "eine Viertel gespielt",        0.45, 0.50, 0.5 },   // ¼ Zyklus
        { "eine halbe gespielt",          0.95, 1.00, 1.0 },   // ½ Zyklus
        { "einen ganzen Zyklus",          1.90, 2.00, 2.0 },
        { "zwei Zyklen",                  3.90, 4.00, 4.0 },
        { "kurz gespielt, spät getreten", 1.95, 3.20, 2.0 },   // die Pause zählt nicht
    };

    for (const auto& f : faelle)
    {
        Looper lp;
        lp.prepare ({ sr, blockSize });
        lp.setEnabled (true);
        lp.setLatencyCompensation (0);

        // Spur 1 setzt den Zyklus: zwei Sekunden durchgehender Ton.
        Buf ton ((int) (sr * 12.0));
        for (int i = 0; i < ton.size(); ++i)
            ton.l[(size_t) i] = ton.r[(size_t) i] = (float) (0.4 * std::sin (kTwoPi * 196.0 * i / sr));

        Lauf lauf (lp, sr, blockSize, ton.size());
        lp.tap (0);
        lauf.fahre (zyklus, &ton);
        lp.tap (0);
        lauf.fahre ((int) (sr * 0.1), &ton);
        CHECK_MSG (lp.trackInfo (0).state == LoopState::playing, "Spur 1 läuft nicht");

        // Spur 2: getreten wird vor der Eins, gespielt ab der Eins. Der Tritt
        // meldet die Aufnahme an, sie beginnt auf der Eins. Würde der Tritt als
        // Anfang zählen, mäße der Test den Vorlauf statt der Länge.
        lp.tap (1);

        // Bis zur nächsten Eins fahren.
        {
            const double c = lp.cycleSamples();
            const double jetzt = std::fmod (lp.phaseSamples(), c);
            lauf.fahre ((int) (c - jetzt), nullptr);
        }

        const int spielt = (int) (sr * f.spielt);
        const int tritt  = (int) (sr * f.dannTritt);

        // Eingang nur, solange gespielt wird, danach Stille.
        Buf zweite (ton.size());
        const int von = lauf.pos;
        for (int i = 0; i < spielt && von + i < zweite.size(); ++i)
            zweite.l[(size_t) (von + i)] = zweite.r[(size_t) (von + i)]
                = (float) (0.4 * std::sin (kTwoPi * 660.0 * i / sr));

        lauf.fahre (tritt, &zweite);
        lp.tap (1);
        lauf.fahre ((int) (sr * 5.0), &zweite);

        const auto info = lp.trackInfo (1);
        const double felder = info.lengthSeconds / ((double) zyklus / sr);
        std::printf ("        %-30s → %.3f Zyklen (Soll %.2f)\n", f.was, felder, f.soll / 2.0);
        CHECK_MSG (info.state == LoopState::playing,
                   std::string (f.was) + ": die Spur läuft nicht");
        CHECK_MSG (std::abs (felder - f.soll / 2.0) < 0.02,
                   std::string (f.was) + ": " + fmt (felder) + " Zyklen statt "
                   + fmt (f.soll / 2.0));
    }
}

NMP_TEST (Looper_ZweiteSpurBeginntAufDerEins)
{
    // Die erste Spur setzt den Zyklus, alle weiteren richten sich daran aus,
    // auch an derselben Eins. Wer zu spät tritt, liegt nicht dauerhaft daneben:
    // die zweite Spur beginnt dort, wo der Zyklus umschlägt.
    const double sr = 48000.0;
    const int blockSize = 128;
    const int zyklus = (int) (sr * 1.0);

    Looper lp;
    lp.prepare ({ sr, blockSize });
    lp.setEnabled (true);
    lp.setLatencyCompensation (0);
    lp.setTrackVolume (0, 0.0f);                  // Spur 1 soll nicht mithören
    lp.setTrackVolume (1, 1.0f);

    Buf ton ((int) (sr * 10.0));
    for (int i = 0; i < ton.size(); ++i)
        ton.l[(size_t) i] = ton.r[(size_t) i] = (float) (0.3 * std::sin (kTwoPi * 196.0 * i / sr));

    Lauf lauf (lp, sr, blockSize, ton.size());
    lp.tap (0);
    lauf.fahre (zyklus, &ton);
    lp.tap (0);
    lauf.fahre (blockSize * 4, &ton);
    CHECK_MSG (lp.trackInfo (0).state == LoopState::playing, "Spur 1 läuft nicht");

    // Absichtlich 0,37 Zyklen hinter der Eins treten und erst auf der nächsten
    // Eins anschlagen.
    const double phaseJetzt = std::fmod (lp.phaseSamples(), lp.cycleSamples());
    const int bisEins = (int) (lp.cycleSamples() - phaseJetzt);
    const int daneben = (int) (0.37 * zyklus);

    Buf zweite (ton.size());
    lauf.fahre (daneben, &zweite);                 // etwas warten
    lp.tap (1);                                    // zu spät getreten

    // Der Anschlag kommt auf der nächsten Eins.
    const int einsBei = lauf.pos + (bisEins - daneben + zyklus) % zyklus;
    putClick (zweite, einsBei, 0.9f);
    for (int i = 0; i < zyklus; ++i)
        if (einsBei + i < zweite.size())
            zweite.l[(size_t) (einsBei + i)] += (float) (0.15 * std::sin (kTwoPi * 660.0 * i / sr));

    lauf.fahre (zyklus + daneben, &zweite);
    lp.tap (1);
    lauf.fahre ((int) (sr * 3.0), nullptr);

    const auto info = lp.trackInfo (1);
    CHECK_MSG (info.state == LoopState::playing, "Spur 2 läuft nicht");

    // Jetzt läuft nur Spur 2 (Spur 1 steht auf null). Der Anschlag muss auf der
    // Eins des Zyklus stehen, nicht 0,37 Zyklen daneben.
    const int von = lauf.pos - (int) (sr * 2.0);
    const int peak = firstPeak (lauf.out.l, von, lauf.out.size(), 0.30f);
    CHECK_MSG (peak >= 0, "in der Wiedergabe ist kein Anschlag zu finden");
    if (peak < 0) return;

    // Wo lag die Eins zu diesem Zeitpunkt? Der Zähler sagt es.
    const double phaseAmPeak = std::fmod (lp.phaseSamples() - (lauf.pos - peak), lp.cycleSamples());
    const double abstand = std::min (phaseAmPeak, lp.cycleSamples() - phaseAmPeak);

    std::printf ("        Anschlag %.0f Werte neben der Eins (daneben getreten: %d)\n",
                 abstand, daneben);
    CHECK_MSG (abstand < 600.0,
               "die zweite Spur liegt " + fmt (abstand) + " Werte neben der Eins");
}

NMP_TEST (Looper_RaetBeatUndSchleifeAusDemMaterial)
{
    // Eine frei eingespielte Schleife endet selten genau dort, wo sie
    // musikalisch endet. Ohne Clock wird anhand der Anschläge geschätzt.
    const double sr = 48000.0;
    const int blockSize = 128;

    // 120 BPM: ein Beat sind 0,5 s, ein Takt 2,0 s. Gespielt werden vier Takte,
    // der Fuß kommt 7 % zu spät.
    const double beat = sr * 0.5;
    const int takte = 4;
    const int gespielt = (int) (beat * 4 * takte);

    Looper lp;
    lp.prepare ({ sr, blockSize });
    lp.setEnabled (true);
    lp.setLatencyCompensation (0);

    Buf in ((int) (sr * 14.0));
    // Auf jede Achtel ein Anschlag. Der Takt steckt in den Anschlägen, nicht im
    // Pegel.
    for (int k = 0; k * (int) (beat / 2) < gespielt; ++k)
    {
        const int at = k * (int) (beat / 2);
        const float amp = (k % 8 == 0) ? 0.9f : (k % 2 == 0 ? 0.6f : 0.35f);
        for (int i = 0; i < (int) (beat / 3) && at + i < in.size(); ++i)
        {
            const double e = amp * std::exp (-6.0 * i / beat);
            const float v = (float) (e * std::sin (kTwoPi * 180.0 * i / sr));
            in.l[(size_t) (at + i)] += v;
            in.r[(size_t) (at + i)] += v;
        }
    }

    Lauf lauf (lp, sr, blockSize, in.size());
    lp.tap (0);
    lauf.fahre (gespielt + (int) (0.07 * gespielt), &in);   // zu spät getreten
    lp.tap (0);

    // Rechenzeit der Schätzung messen. Sie läuft im Audio-Thread, in dem
    // Puffer, in dem die Aufnahme schließt. Bei 128 Samples und 48 kHz ist die
    // Frist 2,67 ms. Dafür ist guessPeriod auf höchstens 700 Stützstellen
    // begrenzt.
    double schlimmster = 0.0;
    for (int k = 0; k < 80; ++k)
    {
        const auto t0 = std::chrono::steady_clock::now();
        lauf.fahre (blockSize, &in);
        const double ms = std::chrono::duration<double, std::milli> (
                              std::chrono::steady_clock::now() - t0).count();
        schlimmster = std::max (schlimmster, ms);
    }
    std::printf ("        teuerster Puffer beim Schließen: %.3f ms (Frist %.2f ms)\n",
                 schlimmster, blockSize / sr * 1000.0);
    CHECK_MSG (schlimmster < blockSize / sr * 1000.0,
               "das Raten sprengt die Frist: " + fmt (schlimmster) + " ms");

    const auto info = lp.trackInfo (0);
    const double takteRaus = info.lengthSeconds / 2.0;
    std::printf ("        gespielt %d Takte, getreten bei %.2f, geraten: %.3f Takte\n",
                 takte, (double) gespielt * 1.07 / (beat * 4), takteRaus);

    CHECK_MSG (std::abs (takteRaus - std::round (takteRaus)) < 0.03,
               "die Länge ist auf keinen ganzen Takt gerastet: "
               + fmt (takteRaus) + " Takte");
    CHECK_MSG (std::abs (takteRaus - takte) < 0.5,
               "es wurden " + fmt (takteRaus) + " Takte statt " + fmt ((double) takte));
}

NMP_TEST (Looper_NimmtWederBeatNochNachbarspurMitAuf)
{
    // Fremdes Signal kann auf zwei Wegen in eine Schleife geraten: über die
    // Beat-Maschine, wenn sie vor dem Looper im selben Pfad spielt, und über
    // die Nachbarspur, weil alle Spuren nacheinander in denselben Block
    // schreiben. Gemessen wird der zweite Fall. Den ersten trennt die Engine
    // mit einem eigenen Puffer für den Beat.
    const double sr = 48000.0;
    const int blockSize = 128;

    Looper lp;
    lp.prepare ({ sr, blockSize });
    lp.setEnabled (true);
    lp.setLatencyCompensation (0);
    lp.setTrackVolume (0, 1.0f);
    lp.setTrackVolume (1, 1.0f);

    const int loopLen = (int) (sr * 0.5);

    // Spur 1 bekommt einen kräftigen Ton bei 300 Hz.
    Buf ersteAufnahme (loopLen * 2);
    for (int i = 0; i < ersteAufnahme.size(); ++i)
        ersteAufnahme.l[(size_t) i] = ersteAufnahme.r[(size_t) i]
            = (float) (0.5 * std::sin (kTwoPi * 300.0 * i / sr));

    Buf out (ersteAufnahme.size());
    int pos = 0;
    lp.tap (0);
    bool zu = false;
    while (pos + blockSize <= ersteAufnahme.size())
    {
        if (! zu && pos >= loopLen) { lp.tap (0); zu = true; }
        std::copy (ersteAufnahme.l.begin() + pos, ersteAufnahme.l.begin() + pos + blockSize, out.l.begin() + pos);
        std::copy (ersteAufnahme.r.begin() + pos, ersteAufnahme.r.begin() + pos + blockSize, out.r.begin() + pos);
        auto v = out.view (pos, blockSize);
        lp.process (v, {});
        pos += blockSize;
    }
    CHECK_MSG (lp.trackInfo (0).state == LoopState::playing, "Spur 1 läuft nicht");

    // Spur 2 wird aufgenommen, während Spur 1 läuft. Der Eingang trägt jetzt
    // nur einen Ton bei 900 Hz.
    Buf zweite (loopLen * 3);
    for (int i = 0; i < zweite.size(); ++i)
        zweite.l[(size_t) i] = zweite.r[(size_t) i]
            = (float) (0.5 * std::sin (kTwoPi * 900.0 * i / sr));

    Buf out2 (zweite.size());
    pos = 0;
    lp.tap (1);
    zu = false;
    while (pos + blockSize <= zweite.size())
    {
        if (! zu && pos >= loopLen) { lp.tap (1); zu = true; }
        std::copy (zweite.l.begin() + pos, zweite.l.begin() + pos + blockSize, out2.l.begin() + pos);
        std::copy (zweite.r.begin() + pos, zweite.r.begin() + pos + blockSize, out2.r.begin() + pos);
        auto v = out2.view (pos, blockSize);
        lp.process (v, {});
        pos += blockSize;
    }
    CHECK_MSG (lp.trackInfo (1).state == LoopState::playing, "Spur 2 läuft nicht");

    // Spur 1 stumm schalten und hören, was Spur 2 gespeichert hat. Steht dort
    // 300 Hz, hat sie die Nachbarspur mit aufgenommen.
    lp.setTrackVolume (0, 0.0f);
    Buf still ((int) (sr * 1.0));
    runBlock (lp, still, blockSize);

    const int von = (int) (sr * 0.5);          // Lautstärke ist durchgeblendet
    const int bis = still.size();
    const double bei300 = tonAt (still.l, von, bis - von, 300.0, sr);
    const double bei900 = tonAt (still.l, von, bis - von, 900.0, sr);

    std::printf ("        in Spur 2: 900 Hz %.4f, 300 Hz (Nachbarin) %.4f\n", bei900, bei300);
    CHECK_MSG (bei900 > 0.05, "Spur 2 hat gar nichts aufgenommen");
    CHECK_MSG (bei300 < bei900 * 0.05,
               "Spur 2 hat die Nachbarspur mitgeschnitten: 300 Hz steht bei "
               + fmt (bei300) + " gegen " + fmt (bei900));
}

NMP_TEST (Looper_KantenRastenAufMusikalischeWerte)
{
    // Anfang und Ende einer fertigen Schleife lassen sich verschieben, gerastet
    // in Sechzehnteln des Zyklus, gemessen von der Kante, die stehen bleibt.
    // Die UI rechnet beim Ziehen genauso.
    const double sr = 48000.0;
    const int blockSize = 128;

    Looper lp;
    lp.prepare ({ sr, blockSize });
    lp.setEnabled (true);
    lp.setLatencyCompensation (0);

    const int loopLen = (int) (sr * 2.0);
    Buf in (loopLen * 2);
    for (int i = 0; i < in.size(); ++i)
        in.l[(size_t) i] = in.r[(size_t) i] = (float) (0.4 * std::sin (kTwoPi * 196.0 * i / sr));

    Buf out (in.size());
    int pos = 0;
    lp.tap (0);
    bool closed = false;
    while (pos + blockSize <= in.size())
    {
        if (! closed && pos >= loopLen) { lp.tap (0); closed = true; }
        std::copy (in.l.begin() + pos, in.l.begin() + pos + blockSize, out.l.begin() + pos);
        std::copy (in.r.begin() + pos, in.r.begin() + pos + blockSize, out.r.begin() + pos);
        auto v = out.view (pos, blockSize);
        lp.process (v, {});
        pos += blockSize;
    }

    const auto vorher = lp.trackInfo (0);
    const double zyklus = lp.cycleSamples();
    CHECK_MSG (zyklus > 0.0, "ohne Zyklus gibt es kein Raster");
    const double raster = zyklus / 16.0;

    // Das Ende nach innen ziehen, mitten zwischen zwei Rasterlinien.
    const int ziel = vorher.loopStart + (int) std::lround (raster * 9.4);
    lp.setLoopEdges (0, vorher.loopStart, ziel);
    const auto nachher = lp.trackInfo (0);

    const double felder = (double) (nachher.loopEnd - nachher.loopStart) / raster;
    std::printf ("        Raster %.1f Werte, gezogen auf %.2f Felder, gerastet auf %.3f\n",
                 raster, 9.4, felder);
    CHECK_MSG (std::abs (felder - std::round (felder)) < 0.01,
               "die Kante ist nicht eingerastet: " + fmt (felder) + " Felder");
    CHECK_MSG (std::abs (felder - 9.0) < 0.01,
               "sie ist auf das falsche Feld gerastet: " + fmt (felder));

    // Die andere Richtung: der Anfang rastet auf das Ende zu. Mit dem neuen
    // Raster gerechnet: die erste Spur setzt den Zyklus, wird sie kürzer, wird
    // der Zyklus es auch. Die UI holt den Zyklus vor jedem Ziehen neu.
    const double raster2 = lp.cycleSamples() / 16.0;
    const int zielAnfang = nachher.loopEnd - (int) std::lround (raster2 * 5.6);
    lp.setLoopEdges (0, zielAnfang, nachher.loopEnd);
    const auto dritte = lp.trackInfo (0);
    const double felder2 = (double) (dritte.loopEnd - dritte.loopStart) / raster2;
    std::printf ("        Anfang gezogen auf %.2f Felder, gerastet auf %.3f\n", 5.6, felder2);
    CHECK_MSG (std::abs (felder2 - 6.0) < 0.01,
               "der Anfang ist nicht auf sechs Felder gerastet: " + fmt (felder2));
}

NMP_TEST (Looper_AutoCutNimmtStilleWegUndGibtSieZurueck)
{
    // Auto-Cut entfernt Stille am Anfang und Ende der ersten Aufnahme. Das
    // Entfernte bleibt sichtbar und lässt sich zurückholen.
    const double sr = 48000.0;
    const int blockSize = 128;

    Looper lp;
    lp.prepare ({ sr, blockSize });
    lp.setEnabled (true);
    lp.setLatencyCompensation (0);

    const int silence = (int) (sr * 0.4);
    const int music   = (int) (sr * 1.2);
    Buf in (silence * 2 + music + blockSize * 4);

    for (int i = 0; i < music; ++i)
        in.l[(size_t) (silence + i)] = in.r[(size_t) (silence + i)]
            = (float) (0.4 * std::sin (kTwoPi * 196.0 * i / sr));

    Buf out (in.size());
    int pos = 0;
    lp.tap (0);
    bool closed = false;
    const int total = silence * 2 + music;

    while (pos + blockSize <= in.size())
    {
        if (! closed && pos >= total) { lp.tap (0); closed = true; }
        std::copy (in.l.begin() + pos, in.l.begin() + pos + blockSize, out.l.begin() + pos);
        std::copy (in.r.begin() + pos, in.r.begin() + pos + blockSize, out.r.begin() + pos);
        auto v = out.view (pos, blockSize);
        lp.process (v, {});
        pos += blockSize;
    }

    const auto info = lp.trackInfo (0);
    std::printf ("        aufgenommen %.2f s, Schleife %.2f s (Musik war %.2f s)\n",
                 (double) info.rawLength / sr, info.lengthSeconds, (double) music / sr);

    CHECK_MSG (info.lengthSeconds < (double) total / sr * 0.8,
               "die Stille wurde nicht weggeschnitten");
    CHECK_MSG (info.lengthSeconds > (double) music / sr * 0.9,
               "es wurde zu viel weggeschnitten");

    // Zurückholbar: der Mitschnitt ist vollständig, die Kanten lassen sich
    // wieder nach außen ziehen.
    CHECK_MSG (info.rawLength >= total,
               "der Mitschnitt wurde beschnitten: dann ist nichts zurückzuholen");

    lp.setLoopEdges (0, 0, info.rawLength);
    const auto restored = lp.trackInfo (0);
    CHECK_NEAR (restored.lengthSeconds, (double) info.rawLength / sr, 0.01);
}

NMP_TEST (Looper_LeereSpurGibtNichtsVonSichUndLoeschenGeht)
{
    const double sr = 48000.0;
    Looper lp;
    lp.prepare ({ sr, 128 });
    lp.setEnabled (true);

    Buf buf (4096);
    buf.fillSine (220.0, sr, 0.3f);
    const auto before = buf.l;

    runBlock (lp, buf, 128);
    for (size_t i = 0; i < before.size(); ++i)
        CHECK_NEAR (buf.l[i], before[i], 1.0e-9f);

    // Aufnehmen, schließen, löschen. Danach muss Ruhe sein.
    lp.tap (0);
    Buf rec (24000);
    rec.fillSine (330.0, sr, 0.3f);
    runBlock (lp, rec, 128);
    lp.tap (0);
    runBlock (lp, rec, 128);

    CHECK (lp.trackInfo (0).state == LoopState::playing);

    lp.erase (0);
    CHECK (lp.trackInfo (0).state == LoopState::empty);
    CHECK (lp.cycleSamples() == 0.0);

    Buf after (4096);
    after.fillSine (220.0, sr, 0.3f);
    const auto ref = after.l;
    runBlock (lp, after, 128);
    for (size_t i = 0; i < ref.size(); ++i)
        CHECK_NEAR (after.l[i], ref[i], 1.0e-9f);
}

NMP_TEST (Spread_MonoSummeBleibtUnveraendert)
{
    // Spread macht das Bild breiter, ohne es zur Seite zu kippen und ohne zu
    // färben. Die Mono-Summe bleibt unverändert.
    const double sr = 48000.0;

    for (float ms : { 1.0f, 5.0f, 12.0f, 25.0f })
    {
        Spread sp;
        sp.prepare (sr, 256);
        sp.setMs (ms);

        Buf buf (1 << 15);
        buf.fillNoise (0.3f, 9u);
        const auto mono = buf.l;         // links und rechts sind gleich

        int pos = 0;
        while (pos < buf.size())
        {
            const int n = std::min (256, buf.size() - pos);
            auto v = buf.view (pos, n);
            sp.process (v);
            pos += n;
        }

        // Die Mono-Summe muss exakt gleich sein.
        double worst = 0.0;
        for (int i = 4096; i < buf.size(); ++i)
            worst = std::max (worst, (double) std::abs (0.5f * (buf.l[(size_t) i] + buf.r[(size_t) i])
                                                        - mono[(size_t) i]));

        CHECK_MSG (worst < 1.0e-5,
                   "bei " + fmt (ms) + " ms bleibt in der Mono-Summe " + fmt (worst) + " übrig");

        // Aus einem Mono-Signal muss tatsächlich Breite entstehen.
        double diff = 0.0;
        for (int i = 4096; i < buf.size(); ++i)
            diff += std::abs (buf.l[(size_t) i] - buf.r[(size_t) i]);
        diff /= (buf.size() - 4096);

        if (ms <= Spread::kOffMs)
            CHECK_MSG (diff < 1.0e-6, "am linken Anschlag muss der Block aus sein");
        else
            CHECK_MSG (diff > 0.02, "bei " + fmt (ms) + " ms entsteht keine Breite");
    }
}

NMP_TEST (Delay_AmZeitreglerZiehenVerstimmtNicht)
{
    // Am TIME-Regler ziehen darf nicht knacken und die stehenden Wiederholungen
    // nicht in der Tonhöhe verziehen.
    const double sr = 48000.0;
    const int blockSize = 64;

    DelayBlock d;
    d.setType (DelayType::digital);
    d.setTimeMs (400.0f);
    d.setFeedback (0.75f);
    d.setMix (1.0f);
    d.prepare ({ sr, blockSize });

    // Ein kurzer Ton hinein, danach Stille. Es laufen nur noch die
    // Wiederholungen.
    Buf buf ((int) (sr * 6.0));
    for (int i = 0; i < (int) (sr * 0.25); ++i)
    {
        const double t = (double) i / sr;
        const float env = (float) std::exp (-6.0 * t);
        buf.l[(size_t) i] = buf.r[(size_t) i] = (float) (0.6 * env * std::sin (kTwoPi * 440.0 * t));
    }

    // Ab 1,5 s wird langsam am Regler gezogen, über zwei Sekunden.
    int pos = 0;
    while (pos + blockSize <= buf.size())
    {
        const double t = (double) pos / sr;
        if (t > 1.5 && t < 3.5)
            d.setTimeMs ((float) mapTo ((float) ((t - 1.5) / 2.0), 400.0f, 900.0f));

        auto v = buf.view (pos, blockSize);
        d.process (v, {});
        pos += blockSize;
    }

    // Frequenz der Wiederholungen während des Ziehens, gemessen über das
    // Spektrum. Bei überlagerten Wiederholungen mit verschiedener Phase sind
    // Nulldurchgänge unzuverlässig.
    const auto sp = analyse (buf.l, (int) (sr * 2.2), 16, sr);
    double best = 0.0, measured = 0.0;
    for (size_t k = 1; k < sp.mag.size(); ++k)
    {
        const double f = (double) k * sp.binHz;
        if (f < 100.0 || f > 4000.0) continue;
        if (sp.mag[k] > best) { best = sp.mag[k]; measured = f; }
    }

    std::printf ("        Wiederholungen während des Ziehens: %.1f Hz (Soll 440), Höhe %.4f\n",
                 measured, best);
    CHECK_MSG (std::abs (measured / 440.0 - 1.0) < 0.03,
               "die Wiederholungen laufen bei " + fmt (measured)
               + " Hz statt bei 440: das Delay verstimmt beim Verstellen");

    // Es darf dabei nicht knacken.
    double worst = 0.0, baseline = 0.0;
    for (int i = (int) (sr * 1.6); i < (int) (sr * 3.4); ++i)
        worst = std::max (worst, (double) std::abs (buf.l[(size_t) i] - buf.l[(size_t) (i - 1)]));
    for (int i = (int) (sr * 0.9); i < (int) (sr * 1.4); ++i)
        baseline = std::max (baseline, (double) std::abs (buf.l[(size_t) i] - buf.l[(size_t) (i - 1)]));

    std::printf ("        größter Sprung: beim Ziehen %.4f, sonst %.4f\n", worst, baseline);
    CHECK_MSG (worst < baseline * 1.4 + 1.0e-4, "beim Ziehen knackt es");
}

// Die Engine setzt in jedem Puffer alle Parameter. Gleiche Werte müssen gleich
// klingen, sonst hängt der Klang an der Puffergröße und knattert im Puffertakt.
NMP_TEST (Delay_ParameterJedenPufferKostetNichts)
{
    constexpr double sr = 48000.0;
    constexpr int    chunk = 64;
    const int        n = (int) (2.0 * sr);

    for (auto type : { DelayType::digital, DelayType::tape, DelayType::analog,
                       DelayType::ping })
    {
        auto make = [&]
        {
            auto d = std::make_unique<DelayBlock>();
            d->setType (type);
            d->setTimeMs (375.0f);
            d->setFeedback (0.55f);
            d->setMix (0.5f);
            d->setSync (false, NoteDiv::quarter);
            d->prepare ({ sr, chunk });
            return d;
        };

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
                b->setType (type);
                b->setTimeMs (375.0f);
                b->setFeedback (0.55f);
                b->setMix (0.5f);
                b->setSync (false, NoteDiv::quarter);

                auto v = gepusht.view (pos, c);
                b->process (v, ctx);
                pos += c;
            }
        }

        double worst = 0.0;
        for (int i = 0; i < n; ++i)
            worst = std::max (worst, (double) std::fabs (ruhig.l[(size_t) i] - gepusht.l[(size_t) i]));

        std::printf ("        %-9s größter Unterschied %.2e\n", delayTypeName (type), worst);
        CHECK_MSG (worst < 1e-6,
                   std::string (delayTypeName (type))
                   + ": der Klang hängt davon ab, ob die Parameter jeden Puffer hereinkommen");
    }
}

NMP_TEST (Delay_SchaukeltSichNichtAuf)
{
    // Bei viel Feedback darf sich nichts aufschaukeln. Späte Wiederholungen
    // werden weder schärfer als das Original noch wachsen sie zu einem Wummern
    // an.
    const double sr = 48000.0;

    for (int ti = 0; ti < (int) DelayType::count; ++ti)
    {
        DelayBlock d;
        d.setType ((DelayType) ti);
        d.setTimeMs (250.0f);
        d.setFeedback (0.95f);        // ganz aufgedreht
        d.setMix (1.0f);
        d.prepare ({ sr, 256 });

        Buf buf ((int) (sr * 20.0));
        for (int i = 0; i < (int) (sr * 0.3); ++i)
        {
            const double t = (double) i / sr;
            float v = 0.0f;
            for (int h = 1; h <= 10; ++h)
                v += (float) (0.35 / h * std::exp (-5.0 * t) * std::sin (kTwoPi * 220.0 * h * t));
            buf.l[(size_t) i] = buf.r[(size_t) i] = v;
        }

        runBlock (d, buf, 256);

        const double early = rms (buf.l, (int) (sr * 0.5), (int) (sr * 1.5));
        const double late  = rms (buf.l, (int) (sr * 15.0), (int) (sr * 18.0));

        // Nichts wächst.
        CHECK_MSG (late <= early * 1.05,
                   std::string (delayTypeName ((DelayType) ti)) + ": nach 15 s ist es "
                   + fmt (gainToDb (late) - gainToDb (early)) + " dB lauter geworden");

        // Der Klang wird nicht schärfer: Anteil über 3 kHz gegen den Anteil
        // darunter, früh gegen spät.
        auto brightness = [&] (int from, int len)
        {
            const auto sp = analyse (buf.l, from, 14, sr);
            const double hi = sp.bandEnergy (3000.0, 16000.0);
            const double lo = sp.bandEnergy (100.0, 3000.0);
            (void) len;
            return lo > 0.0 ? hi / lo : 0.0;
        };

        const double b0 = brightness ((int) (sr * 0.6), 1 << 14);
        const double b1 = brightness ((int) (sr * 12.0), 1 << 14);

        std::printf ("        %-8s  Pegel %+.1f dB, Helligkeit %.4f → %.4f\n",
                     delayTypeName ((DelayType) ti),
                     gainToDb (late) - gainToDb (early), b0, b1);

        CHECK_MSG (b1 <= b0 * 1.2 + 1.0e-6,
                   std::string (delayTypeName ((DelayType) ti))
                   + ": die späten Wiederholungen sind schärfer als die frühen");

        for (float v : buf.l) CHECK (std::isfinite (v));
    }
}
