// Grundbausteine: FFT, Faltung, Resampler, Oversampling, Biquad.

#include "TestFramework.h"
#include "namplify/util/Fft.h"
#include "namplify/util/Random.h"
#include "namplify/dsp/Biquad.h"
#include "namplify/dsp/Convolver.h"
#include "namplify/dsp/Resampler.h"
#include "namplify/dsp/Oversampler.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace nmp;

NMP_TEST (Fft_HinUndZurueckIstIdentisch)
{
    for (int order = 4; order <= 12; ++order)
    {
        RealFft fft;
        fft.prepare (order);

        const int n = 1 << order;
        Xorshift rng (1234u + (unsigned) order);

        std::vector<float> in ((size_t) n), out ((size_t) n);
        std::vector<float> re ((size_t) fft.numBins()), im ((size_t) fft.numBins());

        for (auto& v : in) v = rng.nextBipolar();

        fft.forward (in.data(), re.data(), im.data());
        fft.inverse (re.data(), im.data(), out.data());

        double worst = 0.0;
        for (int i = 0; i < n; ++i)
            worst = std::max (worst, (double) std::abs (in[(size_t) i] - out[(size_t) i]));

        CHECK_MSG (worst < 1.0e-4, "Ordnung " + std::to_string (order) + ": Abweichung " + nmptest::fmt (worst));
    }
}

NMP_TEST (Fft_SinusLandetImRichtigenFach)
{
    RealFft fft;
    fft.prepare (10);
    const int n = 1 << 10;

    std::vector<float> in ((size_t) n);
    std::vector<float> re ((size_t) fft.numBins()), im ((size_t) fft.numBins());

    const int bin = 37;
    for (int i = 0; i < n; ++i)
        in[(size_t) i] = (float) std::sin (kTwoPi * bin * i / n);

    fft.forward (in.data(), re.data(), im.data());

    for (int k = 0; k < fft.numBins(); ++k)
    {
        const double mag = std::hypot (re[(size_t) k], im[(size_t) k]) / (n * 0.5);
        if (k == bin) CHECK_NEAR (mag, 1.0, 1.0e-3);
        else          CHECK_LT (mag, 1.0e-3);
    }
}

NMP_TEST (Faltung_StimmtMitDerDirektenRechnungUeberein)
{
    // Faltung gegen die direkte Berechnung.
    Xorshift rng (777u);

    for (int irLen : { 1, 17, 64, 65, 300, 1024, 1025, 4096, 12000 })
    {
        std::vector<float> ir ((size_t) irLen);
        for (auto& v : ir) v = rng.nextBipolar() * 0.3f;

        const int nSamples = 3000;
        std::vector<float> input ((size_t) nSamples);
        for (auto& v : input) v = rng.nextBipolar();

        // direkt
        std::vector<float> want ((size_t) nSamples, 0.0f);
        for (int n = 0; n < nSamples; ++n)
        {
            double acc = 0.0;
            for (int t = 0; t < irLen; ++t)
                if (n - t >= 0) acc += (double) ir[(size_t) t] * input[(size_t) (n - t)];
            want[(size_t) n] = (float) acc;
        }

        // Durch den Convolver, in ungleich großen Blöcken. So fallen falsch
        // gerechnete Blockgrenzen auf.
        Convolver conv;
        conv.prepare (512);
        conv.setImpulseResponse (ir.data(), irLen);

        std::vector<float> got = input;
        const int chunks[] = { 1, 7, 64, 33, 128, 511, 5, 256 };
        int pos = 0, ci = 0;
        while (pos < nSamples)
        {
            const int c = std::min (chunks[ci++ % 8], nSamples - pos);
            conv.process (got.data() + pos, c);
            pos += c;
        }

        double worst = 0.0, scale = 0.0;
        for (int n = 0; n < nSamples; ++n)
        {
            worst = std::max (worst, (double) std::abs (got[(size_t) n] - want[(size_t) n]));
            scale = std::max (scale, (double) std::abs (want[(size_t) n]));
        }

        CHECK_MSG (worst < scale * 1.0e-4 + 1.0e-5,
                   "IR-Länge " + std::to_string (irLen) + ": Abweichung " + nmptest::fmt (worst)
                   + " bei Vollausschlag " + nmptest::fmt (scale));
    }
}

NMP_TEST (Faltung_MeldetKeineLatenz)
{
    // Ein Einheitsimpuls muss beim ersten Sample wieder herauskommen, sonst
    // stimmt die an den Host gemeldete Latenz nicht.
    std::vector<float> ir (256, 0.0f);
    ir[0] = 1.0f;
    ir[100] = 0.5f;

    Convolver conv;
    conv.prepare (512);
    conv.setImpulseResponse (ir.data(), (int) ir.size());
    CHECK (conv.latencySamples() == 0);

    std::vector<float> sig (512, 0.0f);
    sig[0] = 1.0f;
    conv.process (sig.data(), 512);

    CHECK_NEAR (sig[0], 1.0f, 1.0e-6);
    CHECK_NEAR (sig[100], 0.5f, 1.0e-6);
}

NMP_TEST (Umtaster_HaeltDenTonUndDenPegel)
{
    // Ein Sinus, der bei anderer Rate wieder herauskommt, muss dieselbe
    // Frequenz und denselben Pegel haben.
    struct Pair { double a, b; };
    const Pair pairs[] = { { 48000.0, 44100.0 }, { 44100.0, 48000.0 },
                           { 96000.0, 48000.0 }, { 48000.0, 96000.0 },
                           { 48000.0, 48000.0 } };

    for (auto p : pairs)
    {
        SincResampler rs;
        rs.prepare (p.a, p.b, 512);

        const double freq = 440.0;
        const int nIn = 48000;
        std::vector<float> in ((size_t) nIn);
        for (int i = 0; i < nIn; ++i)
            in[(size_t) i] = (float) std::sin (kTwoPi * freq * i / p.a);

        std::vector<float> out ((size_t) rs.maxOutputFor (nIn) + 16, 0.0f);

        int produced = 0, pos = 0;
        while (pos < nIn)
        {
            const int c = std::min (512, nIn - pos);
            produced += rs.process (in.data() + pos, c, out.data() + produced);
            pos += c;
        }

        // Erwartete Anzahl
        CHECK_NEAR ((double) produced, (double) nIn * p.b / p.a, 8.0);

        // Pegel in der Mitte, weit weg von den Rändern
        const int from = produced / 4, to = produced * 3 / 4;
        double peak = 0.0, energy = 0.0;
        for (int i = from; i < to; ++i)
        {
            peak = std::max (peak, (double) std::abs (out[(size_t) i]));
            energy += (double) out[(size_t) i] * out[(size_t) i];
        }
        const double rms = std::sqrt (energy / (to - from));

        CHECK_NEAR (peak, 1.0, 0.02);
        CHECK_NEAR (rms, 0.7071, 0.01);

        // Nulldurchgänge zählen → Frequenz
        int zc = 0;
        for (int i = from + 1; i < to; ++i)
            if ((out[(size_t) (i - 1)] < 0.0f) != (out[(size_t) i] < 0.0f)) ++zc;

        const double seconds = (double) (to - from) / p.b;
        const double measured = zc / (2.0 * seconds);
        CHECK_NEAR (measured, freq, 1.0);
    }
}

NMP_TEST (HinUndZurueck_LiefertImmerGenauSovieleWerte)
{
    // Der Host bekommt je Puffer genau so viele Samples, wie er hineingegeben
    // hat. Eine Abweichung um ±1 je Puffer führt mit der Zeit zu einem
    // Aussetzer.
    RoundTripResampler rt;
    rt.prepare (48000.0, 44100.0, 512);

    auto identity = [] (void*, float*, int) {};

    std::vector<float> buf;
    for (int blockSize : { 512, 32, 129, 64, 1, 480 })
    {
        buf.assign ((size_t) blockSize, 0.0f);
        for (int rep = 0; rep < 40; ++rep)
        {
            for (int i = 0; i < blockSize; ++i) buf[(size_t) i] = 0.1f;
            rt.process (buf.data(), blockSize, identity, nullptr);
        }
    }

    // Nach dem Einschwingen darf nichts mehr auf null hängen.
    CHECK_GT (std::abs (buf.back()), 0.05f);
    CHECK_GT (rt.latencySamples(), 0);
}

NMP_TEST (HinUndZurueck_BeiGleicherRateOhneUmweg)
{
    RoundTripResampler rt;
    rt.prepare (48000.0, 48000.0, 512);
    CHECK (rt.isPassthrough());
    CHECK (rt.latencySamples() == 0);

    std::vector<float> buf (256);
    Xorshift rng (5u);
    for (auto& v : buf) v = rng.nextBipolar();
    const auto copy = buf;

    rt.process (buf.data(), 256, [] (void*, float*, int) {}, nullptr);

    for (size_t i = 0; i < buf.size(); ++i)
        CHECK_NEAR (buf[i], copy[i], 1.0e-9);
}

NMP_TEST (Ueberabtastung_LaesstDasSignalUnveraendert)
{
    // Ohne Kennlinie dazwischen muss hoch und wieder runter dasselbe Signal
    // ergeben, bis auf die gemeldete Verzögerung.
    for (int factor : { 2, 4 })
    {
        Oversampler os;
        os.prepare (factor, 256);

        const int n = 8192;
        std::vector<float> in ((size_t) n), out ((size_t) n);
        for (int i = 0; i < n; ++i)
            in[(size_t) i] = (float) (std::sin (kTwoPi * 300.0 * i / 48000.0) * 0.5
                                    + std::sin (kTwoPi * 1900.0 * i / 48000.0) * 0.3);

        int pos = 0;
        while (pos < n)
        {
            const int c = std::min (256, n - pos);
            float* up = os.upsample (in.data() + pos, c);
            (void) up;
            os.downsample (out.data() + pos, c);
            pos += c;
        }

        const int lat = os.latencySamples();
        CHECK_GT (lat, 0);

        double worst = 0.0;
        for (int i = 2000; i < n - lat; ++i)
            worst = std::max (worst, (double) std::abs (out[(size_t) (i + lat)] - in[(size_t) i]));

        CHECK_MSG (worst < 0.01, "Faktor " + std::to_string (factor)
                                 + ": Abweichung " + nmptest::fmt (worst)
                                 + " bei Latenz " + std::to_string (lat));
    }
}

NMP_TEST (Biquad_GerechneterGangDeckSichMitDerFormel)
{
    // Die UI zeichnet die Kurve mit derselben Formel. Beide müssen
    // übereinstimmen.
    const double sr = 48000.0;

    struct Case { FilterShape shape; double f, q, g; };
    const Case cases[] = {
        { FilterShape::bell,      1000.0, 1.0,  6.0 },
        { FilterShape::bell,       220.0, 4.0, -9.0 },
        { FilterShape::lowShelf,   120.0, 0.7,  8.0 },
        { FilterShape::highShelf, 6000.0, 0.7, -7.0 },
        { FilterShape::lowCut,      80.0, 0.7,  0.0 },
        { FilterShape::highCut,  10000.0, 0.7,  0.0 },
    };

    RealFft fft;
    const int order = 15, n = 1 << order;
    fft.prepare (order);

    for (auto c : cases)
    {
        const auto co = designShape (c.shape, c.f, c.q, c.g, sr);

        Biquad bq;
        bq.setCoeffs (co);
        bq.reset();

        std::vector<float> imp ((size_t) n, 0.0f);
        imp[0] = 1.0f;
        bq.processBlock (imp.data(), n);

        std::vector<float> re ((size_t) fft.numBins()), im ((size_t) fft.numBins());
        fft.forward (imp.data(), re.data(), im.data());

        double worstDb = 0.0;
        for (int k = 1; k < fft.numBins() - 1; ++k)
        {
            const double f = (double) k * sr / n;
            if (f < 20.0 || f > 18000.0) continue;

            const double measured = std::hypot (re[(size_t) k], im[(size_t) k]);
            const double wanted   = co.magnitudeAt (f, sr);
            worstDb = std::max (worstDb, std::abs (gainToDb (measured) - gainToDb (wanted)));
        }

        CHECK_MSG (worstDb < 0.05, std::string (filterShapeName (c.shape))
                   + " bei " + nmptest::fmt (c.f) + " Hz: bis zu "
                   + nmptest::fmt (worstDb) + " dB Unterschied");
    }
}
