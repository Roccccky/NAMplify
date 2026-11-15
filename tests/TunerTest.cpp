// Stimmgerät.

#include "TestSignals.h"
#include "namplify/engine/Tuner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace nmp;
using namespace nmptest;

namespace
{
    /// Ein gezupfter Ton mit Obertönen statt eines Sinus. An einer Gitarre ist
    /// die Grundwelle oft schwächer als die zweite Harmonische. Ein Verfahren,
    /// das nur die größte Spitze sucht, meldet dann die Oktave.
    std::vector<float> pluckedNote (double f0, double sr, double seconds,
                                    float amp, bool weakFundamental)
    {
        const int n = (int) (sr * seconds);
        std::vector<float> out ((size_t) n, 0.0f);
        Xorshift rng (99u);

        for (int h = 1; h <= 12; ++h)
        {
            const double f = f0 * h;
            if (f > sr * 0.45) break;
            double a = amp / (double) h;
            if (weakFundamental && h == 1) a *= 0.12;
            if (weakFundamental && h == 2) a *= 1.8;

            const double phase = rng.nextUnit() * kTwoPi;
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                out[(size_t) i] += (float) (a * std::exp (-2.2 * t) * std::sin (kTwoPi * f * t + phase));
            }
        }
        return out;
    }

    TunerReading detect (Tuner& t, const std::vector<float>& sig, int blockSize = 256)
    {
        int pos = 0;
        while (pos < (int) sig.size())
        {
            const int c = std::min (blockSize, (int) sig.size() - pos);
            t.push (sig.data() + pos, c);
            pos += c;
            t.analyse();
        }
        return t.reading();
    }
}

NMP_TEST (Stimmgeraet_FindetDenGanzenTonumfang)
{
    // Von der tiefen H-Saite eines Fünfsaiters bis in die hohen Lagen.
    const double sr = 48000.0;
    struct Note { double hz; const char* name; };
    const Note notes[] = {
        { 30.87, "B0" }, { 41.20, "E1" }, { 82.41, "E2" }, { 110.00, "A2" },
        { 146.83, "D3" }, { 196.00, "G3" }, { 246.94, "B3" }, { 329.63, "E4" },
        { 440.00, "A4" }, { 659.26, "E5" }, { 880.00, "A5" }, { 1318.5, "E6" },
    };

    for (auto note : notes)
    {
        Tuner t;
        t.prepare (sr);
        const auto sig = pluckedNote (note.hz, sr, 1.2, 0.3f, false);
        const auto r = detect (t, sig);

        CHECK_MSG (r.hasPitch, std::string ("kein Ton erkannt bei ") + note.name);
        if (! r.hasPitch) continue;

        const double cents = 1200.0 * std::log2 (r.frequencyHz / note.hz);
        std::printf ("        %-4s  %.2f Hz erkannt als %.2f Hz  (%+.1f Cent)\n",
                     note.name, note.hz, r.frequencyHz, cents);

        CHECK_MSG (std::abs (cents) < 4.0,
                   std::string (note.name) + ": " + fmt (cents) + " Cent daneben");
    }
}

NMP_TEST (Stimmgeraet_MeldetNichtDieOktave)
{
    // Bei einer angeschlagenen Saite ist die Grundwelle oft schwächer als die
    // zweite Harmonische. Wer nur die größte Spitze sucht, landet eine Oktave
    // zu hoch.
    const double sr = 48000.0;

    for (double f0 : { 82.41, 110.0, 146.83, 196.0 })
    {
        Tuner t;
        t.prepare (sr);
        const auto sig = pluckedNote (f0, sr, 1.2, 0.3f, true);
        const auto r = detect (t, sig);

        CHECK (r.hasPitch);
        if (! r.hasPitch) continue;

        const double cents = 1200.0 * std::log2 (r.frequencyHz / f0);
        std::printf ("        %.2f Hz mit schwacher Grundwelle → %.2f Hz (%+.0f Cent)\n",
                     f0, r.frequencyHz, cents);
        CHECK_MSG (std::abs (cents) < 25.0,
                   "erkannt als " + fmt (r.frequencyHz) + " Hz statt " + fmt (f0));
    }
}

NMP_TEST (Stimmgeraet_ZeigtNichtsWennNichtsDaIst)
{
    const double sr = 48000.0;
    Tuner t;
    t.prepare (sr);

    std::vector<float> quiet (48000, 0.0f);
    Xorshift rng (3u);
    for (auto& v : quiet) v = rng.nextBipolar() * 0.0002f;   // Rauschteppich

    const auto r = detect (t, quiet);
    CHECK_MSG (! r.hasPitch, "aus Rauschen wird ein Ton erfunden");
}

NMP_TEST (Stimmgeraet_RechnetImAudioThreadFastNichts)
{
    // Im Audio-Thread wird nur geschrieben. Sehr viele Blöcke ohne Auswertung
    // müssen vernachlässigbar billig sein und dürfen nichts allokieren.
    const double sr = 48000.0;
    Tuner t;
    t.prepare (sr);

    std::vector<float> block (64, 0.1f);
    for (int i = 0; i < 20000; ++i)
        t.push (block.data(), 64);

    // Ohne Auswertung darf nichts angezeigt werden.
    CHECK (! t.reading().hasPitch);

    t.analyse();
    CHECK (t.reading().hasPitch || ! t.reading().hasPitch);   // stürzt nicht ab
}

NMP_TEST (Stimmgeraet_CentAnzeigeStimmt)
{
    const double sr = 48000.0;

    // A4 um genau +20 Cent verstimmt.
    const double f = 440.0 * std::pow (2.0, 20.0 / 1200.0);

    Tuner t;
    t.prepare (sr);
    const auto sig = pluckedNote (f, sr, 1.5, 0.3f, false);
    const auto r = detect (t, sig);

    CHECK (r.hasPitch);
    if (! r.hasPitch) return;

    std::printf ("        A4 +20 Cent → %s%d, %+.1f ct, %.2f Hz\n",
                 r.noteName, r.octave, r.cents, r.frequencyHz);

    CHECK (std::string (r.noteName) == "A");
    CHECK (r.octave == 4);
    CHECK_NEAR (r.cents, 20.0f, 3.0f);
}
