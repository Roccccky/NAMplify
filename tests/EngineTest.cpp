// Die Engine als Ganzes, dazu der Lasttest: kein Aussetzer bei kleinem Puffer
// mit drei Captures, Faltung, Hall und offenem Stimmgerät.

#include "TestSignals.h"
#include "namplify/engine/Engine.h"

#include <fstream>
#include <chrono>
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
    std::filesystem::path scratch()
    {
        auto p = std::filesystem::temp_directory_path() / "namplify-tests";
        std::error_code ec;
        std::filesystem::create_directories (p, ec);
        return p;
    }

    /// Ein Capture mit der Architektur Linear: eine Impulsantwort mit
    /// deutlichem Klang. Für Tests, die ein hörbares, vorhersagbares Ergebnis
    /// brauchen.
    std::filesystem::path writeToneNam (const std::string& name, double sr,
                                        double resHz, float gain)
    {
        const int n = 384;
        std::vector<float> ir ((size_t) n, 0.0f);
        ir[0] = 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            ir[(size_t) i] += (float) (0.4 * std::exp (-resHz / 8.0 * t)
                                       * std::sin (kTwoPi * resHz * t));
        }
        float peak = 0.0f;
        for (float v : ir) peak = std::max (peak, std::abs (v));
        for (auto& v : ir) v *= gain / peak;

        const auto path = scratch() / (name + ".nam");
        std::ofstream f (path);
        f << "{\n  \"version\": \"0.5.4\",\n  \"architecture\": \"Linear\",\n";
        f << "  \"config\": { \"receptive_field\": " << n << ", \"bias\": true },\n";
        f << "  \"sample_rate\": " << (long long) sr << ",\n";
        f << "  \"weights\": [";
        for (int i = 0; i < n; ++i) f << (i ? "," : "") << ir[(size_t) i];
        f << ", 0.0]\n}\n";
        return path;
    }

    /// Ein ladbares Capture mit der Architektur LSTM. Eine lineare
    /// Impulsantwort kostet kaum Rechenzeit. Ein LSTM mit zwei Lagen à 32
    /// Einheiten braucht je Sample rund 12 000 Multiplikationen, ähnlich wie
    /// ein echtes NAM-Capture.
    std::filesystem::path writeLstmNam (const std::string& name, int hidden, int layers,
                                        double sampleRate, unsigned seed)
    {
        // Gewichtszahl, genau wie der Kern sie einliest:
        //   je Lage: 4H·(in+H) + 4H + H + H,  danach Kopf: H + 1
        int count = 0;
        for (int i = 0; i < layers; ++i)
        {
            const int in = (i == 0) ? 1 : hidden;
            count += 4 * hidden * (in + hidden) + 4 * hidden + hidden + hidden;
        }
        count += hidden + 1;

        Xorshift rng (seed);
        const auto path = scratch() / (name + ".nam");
        std::ofstream f (path);

        f << "{\n  \"version\": \"0.5.4\",\n  \"architecture\": \"LSTM\",\n";
        f << "  \"config\": { \"num_layers\": " << layers
          << ", \"input_size\": 1, \"hidden_size\": " << hidden << " },\n";
        f << "  \"sample_rate\": " << (long long) sampleRate << ",\n";
        f << "  \"metadata\": { \"loudness\": -21.5, \"input_level_dbu\": 12.0,"
             " \"output_level_dbu\": 15.0 },\n";
        f << "  \"weights\": [";
        for (int i = 0; i < count; ++i)
            f << (i ? "," : "") << (rng.nextBipolar() * 0.15f);
        f << "]\n}\n";

        return path;
    }

    std::vector<float> makeCabIr (double sr, double seconds)
    {
        const int n = (int) (sr * seconds);
        std::vector<float> ir ((size_t) n, 0.0f);
        Xorshift rng (4711u);
        float lp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            lp += 0.28f * (rng.nextBipolar() - lp);
            ir[(size_t) i] = lp * (float) std::exp (-6.0 * i / sr);
        }
        ir[0] += 0.7f;
        return ir;
    }

    /// Die drei A2-Captures aus tests/fixtures/a2, wie sie der TONE3000-Katalog
    /// liefert.
    std::vector<std::filesystem::path> a2Fixtures()
    {
        const std::filesystem::path dir = std::filesystem::path (NAMPLIFY_TEST_FIXTURES) / "a2";
        return { dir / "a2-amp-test.nam", dir / "a2-amp-cab-test.nam", dir / "a2-am-test-2.nam" };
    }

    /// Wo im vollen Brett was liegt.
    enum RigSlot { rigGate, rigComp, rigDrive, rigPedal, rigAmp, rigCab, rigEq,
                   rigOutboard, rigChorus, rigDelay, rigReverb, rigCount };

    /// Einen Regler eines Slots auf einen echten Wert stellen.
    void setCtl (Engine& e, int slot, int k, float value)
    {
        const auto t = e.layout().type[(size_t) slot];
        e.params().set (slotControlParam (slot, k), slotControl (t, k).toUnit (value));
    }

    /// Alles einschalten, was der Lasttest braucht. Ohne captures stehen drei
    /// LSTM-Netze der gegebenen Größe auf den Capture-Slots, sonst diese
    /// Dateien.
    void loadFullRig (Engine& e, double sr, int hidden, int layers,
                      const std::vector<std::filesystem::path>& captures = {},
                      int oversampling = 1)
    {
        std::string err;

        Layout l;
        const SlotType types[rigCount] = { SlotType::gate, SlotType::comp, SlotType::drive,
                                           SlotType::nam, SlotType::nam, SlotType::ir, SlotType::eq,
                                           SlotType::nam, SlotType::chorus, SlotType::delay,
                                           SlotType::reverb };
        for (int s = 0; s < rigCount; ++s)
        {
            l.type[(size_t) s] = types[s];
            l.lane[0][(size_t) s] = (int8_t) s;
        }
        l.length[0] = rigCount;
        l.role[rigPedal] = NamRole::pedal;
        l.role[rigAmp] = NamRole::amp;
        l.role[rigOutboard] = NamRole::outboard;
        e.setLayout (l);
        for (int s = 0; s < rigCount; ++s) e.resetSlotControls (s, types[s]);

        const int namSlots[3] = { rigPedal, rigAmp, rigOutboard };
        const char* names[3] = { "perf-pedal", "perf-amp", "perf-out" };

        unsigned seed = 11u;
        for (int i = 0; i < 3; ++i)
        {
            const auto path = captures.empty() ? writeLstmNam (names[i], hidden, layers, sr, seed += 7u)
                                               : captures[(size_t) i % captures.size()];
            auto model = NamModel::load (path, sr, e.maxBlockSize(), err, oversampling);
            if (model) e.namFor (namSlots[i])->publishModel (std::move (model));
        }

        const auto ir = makeCabIr (sr, 0.4);
        e.irFor (rigCab)->prepareIr (ir.data(), (int) ir.size());

        for (auto t : { Tool::drums, Tool::looper, Tool::recorder }) e.setToolShown (t, true);

        auto& p = e.params();
        p.set (ParamId::drumsOn, 1.0f);
        setCtl (e, rigGate, ctl::gateThreshold, -60.0f);
        p.set (ParamId::spreadMs, 12.0f);
        e.globalEq().setEnabled (true);
    }
}

NMP_TEST (Motor_WerkszustandIstEinDurchgang)
{
    // Frisch geöffnet ist die Kette leer. Dann darf die Engine das Signal weder
    // färben noch verschlucken.
    Engine e;
    e.prepare (48000.0, 256);

    CHECK (e.layout().length[0] == 0 && e.layout().length[1] == 0);
    CHECK (e.layout().count (SlotType::drive) == 0);
    CHECK (e.layout().count (SlotType::reverb) == 0);
    CHECK (! e.isToolShown (Tool::looper));

    Buf buf (8192);
    buf.fillNoise (0.2f, 5u);
    const auto before = buf.l;

    int pos = 0;
    while (pos < buf.size())
    {
        const int n = std::min (256, buf.size() - pos);
        e.process (buf.l.data() + pos, buf.r.data() + pos, n);
        pos += n;
    }

    // Nach dem Einschwingen der Pegelglättung muss es Wert für Wert dasselbe sein.
    double worst = 0.0;
    for (size_t i = 4096; i < before.size(); ++i)
        worst = std::max (worst, (double) std::abs (buf.l[i] - before[i]));

    CHECK_MSG (worst < 1.0e-5, "der Werkszustand färbt: bis zu " + fmt (worst) + " Unterschied");
}

NMP_TEST (Motor_FaehrtNichtAn)
{
    // Kein Pegelsprung beim Start.
    const double sr = 48000.0;

    Engine e;
    e.prepare (sr, 64);
    loadFullRig (e, sr, 16, 1);
    e.params().set (ParamId::drumsOn, 0.0f);   // sonst misst man das Schlagzeug

    Buf buf (4096);
    buf.fillSine (220.0, sr, 0.25f);

    int pos = 0;
    while (pos < buf.size())
    {
        e.process (buf.l.data() + pos, buf.r.data() + pos, 64);
        pos += 64;
    }

    // Der allererste Block darf weder still noch übersteuert sein.
    const double first = rms (buf.l, 0, 64);
    const double later = rms (buf.l, 2048, 2112);

    std::printf ("        erster Block %.1f dB, später %.1f dB\n",
                 gainToDb (first), gainToDb (later));

    CHECK_MSG (first > later * 0.15,
               "der erste Block ist " + fmt (gainToDb (first) - gainToDb (later))
               + " dB leiser: da wird aus der Stille hochgeblendet");
    CHECK_MSG (first < later * 6.0,
               "der erste Block ist " + fmt (gainToDb (first) - gainToDb (later))
               + " dB lauter: das ist ein Einschaltknall");

    for (float v : buf.l) CHECK (std::isfinite (v));
}

NMP_TEST (Motor_RiegelSitztHintenUndDasStimmgeraetHoertWeiter)
{
    // MUTE sitzt am Ende der Kette. Das Stimmgerät hört trotzdem weiter, weil
    // es direkt hinter dem Eingangspegel abgreift.
    const double sr = 48000.0;

    Engine e;
    e.prepare (sr, 256);
    e.params().set (ParamId::mute, 1.0f);

    // Ein sauberer Ton hinein.
    Buf buf (48000);
    for (int i = 0; i < buf.size(); ++i)
    {
        const double t = (double) i / sr;
        float v = 0.0f;
        for (int h = 1; h <= 8; ++h)
            v += (float) (0.3 / h * std::exp (-1.2 * t) * std::sin (kTwoPi * 220.0 * h * t));
        buf.l[(size_t) i] = buf.r[(size_t) i] = v;
    }

    int pos = 0;
    while (pos < buf.size())
    {
        const int n = std::min (256, buf.size() - pos);
        e.process (buf.l.data() + pos, buf.r.data() + pos, n);
        pos += n;
        e.tuner().analyse();
    }

    // Am Ausgang ist es still.
    const double out = rms (buf.l, 4096, buf.size());
    CHECK_MSG (out < 1.0e-6, "trotz MUTE kommt noch etwas heraus (" + fmt (gainToDb (out)) + " dB)");

    // Das Stimmgerät hört trotzdem.
    const auto r = e.tuner().reading();
    CHECK_MSG (r.hasPitch, "das Stimmgerät hört bei MUTE nichts: dann sitzt der Riegel vorn");
    if (r.hasPitch)
    {
        std::printf ("        bei MUTE erkannt: %s%d, %.2f Hz\n", r.noteName, r.octave, r.frequencyHz);
        CHECK_NEAR (r.frequencyHz, 220.0f, 2.0f);
    }
}

namespace
{
    /// Sechs Sekunden bei 64er Puffer mit mitlaufendem Stimmgerät, die
    /// Rechenzeit je Puffer gegen die Frist gemessen.
    void checkDeadline (Engine& e, double sr, int blockSize)
    {
        for (int s : { rigPedal, rigAmp, rigOutboard })
            CHECK_MSG (e.namFor (s) != nullptr && e.namFor (s)->modelInfo() != nullptr,
                       "die Captures wurden nicht geladen");

        // Erst warmlaufen lassen, der erste Puffer füllt noch Caches.
        Buf warm (blockSize * 200);
        warm.fillSine (220.0, sr, 0.2f);
        for (int p = 0; p < warm.size(); p += blockSize)
            e.process (warm.l.data() + p, warm.r.data() + p, blockSize);

        const int blocks = (int) (sr * 6.0) / blockSize;    // sechs Sekunden
        std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize);

        double worstUs = 0.0, totalUs = 0.0;
        int overruns = 0;
        const double budgetUs = 1.0e6 * blockSize / sr;

        Xorshift rng (17u);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double t = (double) (b * blockSize + i) / sr;
                const float v = (float) (0.25 * std::sin (kTwoPi * 220.0 * t)
                                       + 0.08 * std::sin (kTwoPi * 660.0 * t)
                                       + 0.02 * rng.nextBipolar());
                l[(size_t) i] = r[(size_t) i] = v;
            }

            const auto t0 = std::chrono::steady_clock::now();
            e.process (l.data(), r.data(), blockSize);
            const auto t1 = std::chrono::steady_clock::now();

            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count();
            worstUs = std::max (worstUs, us);
            totalUs += us;
            if (us > budgetUs) ++overruns;

            // Das Stimmgerät rechnet wie im Betrieb auf einem eigenen Thread.
            // Das darf den Audio-Thread nichts kosten.
            if ((b % 8) == 0) e.tuner().analyse();

            e.serviceBackground();
        }

        const double meanUs = totalUs / blocks;
        std::printf ("        Puffer %d Werte = %.0f µs Frist\n", blockSize, budgetUs);
        std::printf ("        Mittel %.0f µs (%.0f %%), Spitze %.0f µs (%.0f %%), %d Überschreitungen\n",
                     meanUs, 100.0 * meanUs / budgetUs, worstUs, 100.0 * worstUs / budgetUs, overruns);

        CHECK_MSG (meanUs < budgetUs * 0.7,
                   "im Mittel " + fmt (100.0 * meanUs / budgetUs) + " % der Frist");
        // Einzelne Überschreitungen zählen nur auf einem eigenen Rechner. Auf
        // geteilten CI-Maschinen (CI gesetzt) hält der Host die VM gelegentlich
        // für Dutzende Millisekunden an (gemessen: 35 ms Spitze bei 37 % Mittel
        // auf macos-15). Das Mittel oben fängt echte Verschlechterungen auch
        // dort.
        if (std::getenv ("CI") == nullptr)
            CHECK_MSG (overruns <= blocks / 200,
                       std::to_string (overruns) + " von " + std::to_string (blocks)
                       + " Blöcken reißen die Frist");
    }
}

NMP_TEST (Motor_HaeltDieFristBeiKleinemPuffer)
{
    // Drei LSTM-Netze à ~12 000 Multiplikationen je Sample: eine feste Last,
    // vergleichbar zwischen Versionen.
    const double sr = 48000.0;
    Engine e;
    e.prepare (sr, 64);
    loadFullRig (e, sr, 32, 2);
    checkDeadline (e, sr, 64);
}

NMP_TEST (Motor_HaeltDieFristMitVierA2CapturesDual)
{
    // Obergrenze (slotTypeLimit): vier A2-Captures FULL, zwei davon als zweiter
    // Amp mit eigener Box in Spur B, dazu das übrige Brett. Das muss bei 64er
    // Puffer halten.
    const double sr = 48000.0;
    Engine e;
    e.prepare (sr, 64);
    loadFullRig (e, sr, 0, 0, a2Fixtures());

    Layout l = e.layout();
    const int ampB = rigCount, cabB = rigCount + 1;
    l.type[(size_t) ampB] = SlotType::nam;
    l.role[(size_t) ampB] = NamRole::amp;
    l.type[(size_t) cabB] = SlotType::ir;
    l.lane[1][0] = (int8_t) ampB;
    l.lane[1][1] = (int8_t) cabB;
    l.length[1] = 2;
    e.setLayout (l);
    e.resetSlotControls (ampB, SlotType::nam);
    e.resetSlotControls (cabB, SlotType::ir);
    e.params().set (ParamId::dualOn, 1.0f);

    CHECK (e.layout().count (SlotType::nam) == 4);

    std::string err;
    auto model = NamModel::load (a2Fixtures()[1], sr, e.maxBlockSize(), err);
    CHECK_MSG (model != nullptr, err);
    if (model) e.namFor (ampB)->publishModel (std::move (model));
    const auto ir = makeCabIr (sr, 0.4);
    e.irFor (cabB)->prepareIr (ir.data(), (int) ir.size());

    checkDeadline (e, sr, 64);
}

NMP_TEST (Motor_HaeltDieFristMitDreiA2CapturesFull)
{
    // Dieselbe Messung mit A2 in voller Netzgröße (8 Kanäle) auf allen drei
    // Slots, wie der Katalog sie liefert.
    const double sr = 48000.0;
    Engine e;
    e.prepare (sr, 64);
    loadFullRig (e, sr, 0, 0, a2Fixtures());
    checkDeadline (e, sr, 64);
}

NMP_TEST (Motor_HaeltDieFristMitDreiA2CapturesLite)
{
    // LITE wird global an der Engine umgeschaltet. Ein Slot übernimmt beim
    // Laden die Stellung des Schalters.
    const double sr = 48000.0;
    Engine e;
    e.prepare (sr, 64);
    loadFullRig (e, sr, 0, 0, a2Fixtures());
    e.setEngineLite (true);
    checkDeadline (e, sr, 64);
}

namespace
{
    /// Mittlere Rechenzeit je 64er Puffer für drei A2-Captures, gemessen wie in
    /// checkDeadline, ohne Prüfung der Frist.
    double meanCostUs (int oversampling, bool lite)
    {
        const double sr = 48000.0;
        const int block = 64;
        Engine e;
        e.prepare (sr, block);
        loadFullRig (e, sr, 0, 0, a2Fixtures(), oversampling);
        e.setEngineLite (lite);

        Buf buf (block * 1500);
        buf.fillSine (220.0, sr, 0.2f);
        for (int p = 0; p < block * 200; p += block)       // warmlaufen, nachziehen
        {
            e.process (buf.l.data() + p, buf.r.data() + p, block);
            e.serviceBackground();
        }

        double best = 1.0e30;
        for (int round = 0; round < 3; ++round)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int p = 0; p < buf.size(); p += block)
                e.process (buf.l.data() + p, buf.r.data() + p, block);
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min (best, std::chrono::duration<double, std::micro> (t1 - t0).count()
                                   / (buf.size() / block));
        }
        return best;
    }
}

NMP_TEST (Motor_UeberabtastungKostetWasDerFaktorSagt)
{
    // Oversampling ist optional, die Frist gilt für die Grundeinstellung.
    // Geprüft wird deshalb das Verhältnis, gemessen im selben Lauf auf
    // derselben Maschine. Eine feste Frist hinge auf geteilten CI-Rechnern zu
    // stark am Rechner.
    //
    // - 2× darf höchstens das 2,5-Fache von 1× kosten: das Netz rechnet
    // doppelt, der Rest der Kette nicht.
    // - 4× darf höchstens das 4,5-Fache von 1× kosten, dieselbe Regel mit LITE.
    // Ob 4× LITE billiger ist als 1× FULL, hängt an der Maschine: auf arm64
    // kostet LITE 13 % von FULL, auf x86 unter Windows 26 %.
    const double full1 = meanCostUs (1, false);
    const double full2 = meanCostUs (2, false);
    const double lite1 = meanCostUs (1, true);
    const double lite4 = meanCostUs (4, true);

    std::printf ("        je 64er Puffer: FULL 1× %.0f µs, 2× %.0f µs (%.2f-fach) · "
                 "LITE 1× %.0f µs, 4× %.0f µs (%.2f-fach)\n",
                 full1, full2, full2 / full1, lite1, lite4, lite4 / lite1);

    CHECK_MSG (full2 < full1 * 2.5, "2× kostet das " + fmt (full2 / full1) + "-Fache von 1×");
    CHECK_MSG (lite4 < lite1 * 4.5, "4× kostet das " + fmt (lite4 / lite1) + "-Fache von 1×");
}

NMP_TEST (Motor_LiteGiltAuchFuerEinCaptureImAnflug)
{
    // Umgeschaltet, während die Captures noch auf ihren ersten Audio-Block
    // warten (Gerät zu, Host angehalten): sie müssen trotzdem LITE rechnen,
    // sonst zeigt der Schalter etwas anderes, als gerechnet wird.
    const double sr = 48000.0;
    Engine e;
    e.prepare (sr, 64);
    loadFullRig (e, sr, 0, 0, a2Fixtures());
    e.setEngineLite (true);

    Buf buf (64 * 400);
    buf.fillSine (220.0, sr, 0.2f);
    auto costPerBlock = [&]
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (int p = 0; p < buf.size(); p += 64)
        {
            e.process (buf.l.data() + p, buf.r.data() + p, 64);
            e.serviceBackground();
        }
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro> (t1 - t0).count() / (buf.size() / 64);
    };

    costPerBlock();                          // übernehmen und nachziehen
    const double lite = costPerBlock();
    e.setEngineLite (false);
    costPerBlock();
    const double full = costPerBlock();

    std::printf ("        je Puffer: LITE %.0f µs, FULL %.0f µs\n", lite, full);
    CHECK_MSG (lite < full * 0.7,
               "nach dem Umlegen im Anflug rechnet LITE " + fmt (100.0 * lite / full) + " % von FULL");
}

NMP_TEST (Motor_BeatMaschineKnacktBeimAbschaltenNicht)
{
    // Abschalten mitten in einem Schlag würde ihn abschneiden und knacken. Der
    // Fußschalter blendet deshalb aus, und die Maschine läuft weiter, solange
    // etwas ausklingt.
    const double sr = 48000.0;
    const int blockSize = 128;

    Engine e;
    e.prepare (sr, blockSize);
    e.setToolShown (Tool::drums, true);
    e.params().set (ParamId::drumsOn, 1.0f);
    e.params().set (ParamId::drumsVolume, 1.0f);
    e.params().set (ParamId::drumsBpm, 120.0f);

    Buf buf ((int) (sr * 2.0));
    int pos = 0, aus = -1;

    while (pos + blockSize <= buf.size())
    {
        // Mitten in einen Schlag hinein abschalten, kurz nachdem der Pegel
        // angesprungen ist.
        if (aus < 0 && pos > (int) (sr * 0.6))
        {
            float lauteste = 0.0f;
            for (int i = pos - blockSize; i < pos; ++i)
                lauteste = std::max (lauteste, std::abs (buf.l[(size_t) i]));
            if (lauteste > 0.05f) { e.params().set (ParamId::drumsOn, 0.0f); aus = pos; }
        }

        e.process (buf.l.data() + pos, buf.r.data() + pos, blockSize);
        pos += blockSize;
    }

    CHECK_MSG (aus > 0, "es wurde nie ein Schlag gefunden: die Maschine ist stumm");

    double worst = 0.0; int worstAt = 0;
    for (int i = aus + 1; i < aus + (int) (sr * 0.05); ++i)
    {
        const double d = std::abs (buf.l[(size_t) i] - buf.l[(size_t) (i - 1)]);
        if (d > worst) { worst = d; worstAt = i; }
    }

    // Zum Vergleich: die übliche Änderung von Sample zu Sample mitten im
    // Spielen.
    double sonst = 0.0;
    for (int i = (int) (sr * 0.2) + 1; i < (int) (sr * 0.55); ++i)
        sonst = std::max (sonst, (double) std::abs (buf.l[(size_t) i] - buf.l[(size_t) (i - 1)]));

    std::printf ("        Abschalten bei %d: größter Sprung %.4f (bei %+d), sonst %.4f\n",
                 aus, worst, worstAt - aus, sonst);
    CHECK_MSG (worst <= sonst * 1.25 + 1.0e-4,
               "beim Abschalten springt es auf " + fmt (worst) + ", sonst nur auf "
               + fmt (sonst) + ": das ist ein Knacken");

    // Danach ist Ruhe.
    float rest = 0.0f;
    for (int i = aus + (int) (sr * 0.2); i < buf.size(); ++i)
        rest = std::max (rest, std::abs (buf.l[(size_t) i]));
    CHECK_MSG (rest < 1.0e-4f, "die Beat-Maschine hört nicht auf");
}

NMP_TEST (Motor_DerLooperNimmtDenBeatNichtMitAuf)
{
    // Der Looper läuft nach der Beat-Maschine und nimmt sie nicht auf. Sonst
    // liefe der Beat nach der ersten Spur doppelt, und ein Musterwechsel liefe
    // gegen die alte Aufnahme.
    const double sr = 48000.0;
    const int blockSize = 256;

    Engine e;
    e.prepare (sr, blockSize);
    e.setToolShown (Tool::looper, true);
    e.setToolShown (Tool::drums, true);
    e.params().set (ParamId::drumsOn, 1.0f);
    e.params().set (ParamId::looperOn, 1.0f);
    e.params().set (ParamId::drumsVolume, 1.0f);
    e.params().set (ParamId::drumsBpm, 120.0f);

    auto fahre = [&] (Buf& b)
    {
        int pos = 0;
        while (pos < b.size())
        {
            const int n = std::min (blockSize, b.size() - pos);
            e.process (b.l.data() + pos, b.r.data() + pos, n);
            pos += n;
        }
    };

    // Erst laufen lassen und prüfen, dass der Beat zu hören ist.
    Buf vorlauf ((int) (sr * 1.0));
    fahre (vorlauf);
    const double beatPegel = rms (vorlauf.l, (int) (sr * 0.2), vorlauf.size());
    CHECK_MSG (beatPegel > 1.0e-3, "die Beat-Maschine ist stumm: dann misst die Prüfung nichts");

    // Eine Spur aufnehmen, mit stillem Eingang. Zu hören ist nur der Beat.
    e.looper().tap (0);
    Buf aufnahme ((int) (sr * 2.0));
    fahre (aufnahme);
    e.looper().tap (0);
    Buf nachlauf ((int) (sr * 0.2));
    fahre (nachlauf);

    CHECK_MSG (e.looper().trackInfo (0).state == LoopState::playing
               || e.looper().trackInfo (0).state == LoopState::empty,
               "die Aufnahme wurde nicht geschlossen");

    // Beat aus: was jetzt kommt, ist allein die Schleife.
    e.params().set (ParamId::drumsOn, 0.0f);
    Buf danach ((int) (sr * 2.0));
    fahre (danach);

    const double schleife = rms (danach.l, (int) (sr * 0.5), danach.size());
    std::printf ("        Beat allein %.5f, Schleife danach %.5f\n", beatPegel, schleife);
    CHECK_MSG (schleife < beatPegel * 0.02,
               "der Beat steht in der Schleife: " + fmt (schleife)
               + " gegen " + fmt (beatPegel) + " beim Beat allein");
}

NMP_TEST (Motor_HallfahneTreibtDieLastNichtHoch)
{
    // Ausklingende Filter und Hallfahnen dürfen die Rechenlast in der Stille
    // nicht erhöhen. Ursache wären Denormals, für die die FPU langsame
    // Sonderwege geht.
    const double sr = 48000.0;
    const int blockSize = 128;

    Engine e;
    e.prepare (sr, blockSize);
    loadFullRig (e, sr, 16, 1);
    e.params().set (ParamId::drumsOn, 0.0f);
    setCtl (e, rigReverb, ctl::reverbTime, 8.0f);
    setCtl (e, rigDelay, ctl::delayFeedback, 0.9f);

    std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize);

    auto runFor = [&] (double seconds, bool withSignal)
    {
        const int blocks = (int) (sr * seconds) / blockSize;
        double total = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double t = (double) (b * blockSize + i) / sr;
                const float v = withSignal ? (float) (0.3 * std::sin (kTwoPi * 220.0 * t)) : 0.0f;
                l[(size_t) i] = r[(size_t) i] = v;
            }
            const auto t0 = std::chrono::steady_clock::now();
            e.process (l.data(), r.data(), blockSize);
            const auto t1 = std::chrono::steady_clock::now();
            total += std::chrono::duration<double, std::micro> (t1 - t0).count();
        }
        return total / blocks;
    };

    runFor (1.0, true);                       // warmlaufen
    const double playing = runFor (2.0, true);
    const double after30 = runFor (30.0, false);   // dreißig Sekunden Stille

    std::printf ("        spielend %.0f µs, dreißig Sekunden später %.0f µs (%.0f %%)\n",
                 playing, after30, 100.0 * after30 / playing);

    // Auf geteilten CI-Maschinen (CI gesetzt) schwankt das Verhältnis ohne
    // Codeänderung zwischen etwa 0,8 und 1,4 (macos-15). Denormals kosten ein
    // Vielfaches und fallen auch mit der weiteren Grenze auf.
    const double grenze = std::getenv ("CI") == nullptr ? 1.15 : 2.0;
    CHECK_MSG (after30 < playing * grenze,
               "die Last steigt im Ausklang auf " + fmt (100.0 * after30 / playing) + " %");
}

NMP_TEST (Motor_CapturesWechselnOhneKnacken)
{
    // Ein Capture zu laden, während gespielt wird, darf nicht knacken und nicht
    // stocken.
    const double sr = 48000.0;
    const int blockSize = 128;

    Engine e;
    e.prepare (sr, blockSize);
    loadFullRig (e, sr, 16, 1);
    e.params().set (ParamId::drumsOn, 0.0f);
    e.params().set (slotOnParam (rigReverb), 0.0f);
    e.params().set (slotOnParam (rigDelay), 0.0f);

    Buf buf ((int) (sr * 3.0));
    buf.fillSine (220.0, sr, 0.3f);

    // Hier ein Capture mit hörbarem Ergebnis. Ein Netz mit Zufallsgewichten
    // gäbe zu wenig Pegel für eine aussagekräftige Messung.
    std::string err;
    {
        auto first = NamModel::load (writeToneNam ("swap-a", sr, 900.0, 0.9f), sr, blockSize, err);
        CHECK_MSG (first != nullptr, err);
        if (first) e.namFor (rigAmp)->publishModel (std::move (first));
    }
    const auto other = writeToneNam ("swap-b", sr, 1700.0, 0.9f);

    const int swapAt = buf.size() / 2;
    int pos = 0;
    while (pos + blockSize <= buf.size())
    {
        if (pos <= swapAt && pos + blockSize > swapAt)
        {
            auto m = NamModel::load (other, sr, blockSize, err);
            CHECK_MSG (m != nullptr, err);
            if (m) e.namFor (rigAmp)->publishModel (std::move (m));
        }

        e.process (buf.l.data() + pos, buf.r.data() + pos, blockSize);
        pos += blockSize;
        e.serviceBackground();
    }

    double around = 0.0, baseline = 0.0;
    for (int i = swapAt - 1000; i < swapAt + 4000; ++i)
        around = std::max (around, (double) std::abs (buf.l[(size_t) i] - buf.l[(size_t) (i - 1)]));
    // Bezug ist der größte Sprung im ruhigen Spiel beider Captures, vor und
    // lange nach dem Wechsel. Das Fenster um den Wechsel enthält schon das
    // zweite Capture. Gegen das erste allein gemessen, gälte ein hellerer Ton
    // als Knacken.
    for (int i = buf.size() / 5; i < buf.size() / 5 + 4000; ++i)
        baseline = std::max (baseline, (double) std::abs (buf.l[(size_t) i] - buf.l[(size_t) (i - 1)]));
    for (int i = buf.size() * 4 / 5; i < buf.size() * 4 / 5 + 4000; ++i)
        baseline = std::max (baseline, (double) std::abs (buf.l[(size_t) i] - buf.l[(size_t) (i - 1)]));

    const double level = rms (buf.l, buf.size() / 5, buf.size() / 5 + 4000);
    std::printf ("        Pegel %.1f dB, größter Sprung: beim Wechsel %.5f, sonst %.5f\n",
                 gainToDb (level), around, baseline);
    CHECK_MSG (level > 0.01, "es kommt fast nichts heraus: die Messung sagt dann nichts aus");
    CHECK_MSG (around <= baseline * 1.3 + 1.0e-4,
               "der Wechsel knackt (" + fmt (around) + " gegen " + fmt (baseline) + ")");

    for (float v : buf.l) CHECK (std::isfinite (v));
}

