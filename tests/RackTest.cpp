// Die freien Slots, eine oder zwei Spuren.

#include "TestSignals.h"
#include "namplify/engine/Rack.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace nmp;
using namespace nmptest;

namespace
{
    /// Größter Sprung von einem Sample zum nächsten. Ein Knacken ist ein
    /// Sprung, der nicht aus dem Signal kommt.
    double largestStep (const std::vector<float>& v, int from, int to)
    {
        double worst = 0.0;
        for (int i = std::max (1, from); i < to; ++i)
            worst = std::max (worst, (double) std::abs (v[(size_t) i] - v[(size_t) (i - 1)]));
        return worst;
    }

    /// Ein Aufbau aus einer Liste je Spur: { Typ, … }. Die Slots werden der
    /// Reihe nach vergeben.
    Layout makeLayout (std::initializer_list<SlotType> a, std::initializer_list<SlotType> b = {})
    {
        Layout l;
        int slot = 0;
        for (auto t : a) { l.type[(size_t) slot] = t; l.lane[0][(size_t) l.length[0]++] = (int8_t) slot++; }
        for (auto t : b) { l.type[(size_t) slot] = t; l.lane[1][(size_t) l.length[1]++] = (int8_t) slot++; }
        return l;
    }

    void enableAll (Rack& r) { for (int s = 0; s < kNumSlots; ++s) r.setEnabled (s, true); }

    /// Lange genug laufen lassen, bis ein angeforderter Umbau getauscht ist.
    void settle (Rack& r, const RackMix& mix = {}, int frames = 4096)
    {
        Buf warm (frames);
        runRack (r, warm, mix);
    }

    int peakIndex (const std::vector<float>& v)
    {
        int at = 0;
        float best = 0.0f;
        for (int i = 0; i < (int) v.size(); ++i)
            if (std::abs (v[(size_t) i]) > best) { best = std::abs (v[(size_t) i]); at = i; }
        return at;
    }
}

NMP_TEST (Platz_GrenzenGeltenUndNichtsGehtVerloren)
{
    // Fünf NAM angefordert, vier erlaubt. Der fünfte Slot bleibt leer.
    auto l = makeLayout ({ SlotType::nam, SlotType::nam, SlotType::nam, SlotType::nam, SlotType::nam,
                           SlotType::delay, SlotType::delay, SlotType::delay });
    l.sanitise();

    CHECK (l.count (SlotType::nam) == slotTypeLimit (SlotType::nam));
    CHECK (l.count (SlotType::delay) == slotTypeLimit (SlotType::delay));
    CHECK (slotTypeLimit (SlotType::nam) == 4);
    CHECK (slotTypeLimit (SlotType::ir) == 4);

    // Jeder belegte Slot steht genau einmal in genau einer Spur.
    int seen[kNumSlots] = {};
    for (int ln = 0; ln < kNumLanes; ++ln)
        for (int p = 0; p < l.length[(size_t) ln]; ++p) ++seen[l.lane[(size_t) ln][(size_t) p]];
    for (int s = 0; s < kNumSlots; ++s)
        CHECK_MSG (seen[s] == (l.type[(size_t) s] == SlotType::empty ? 0 : 1),
                   "Platz " + std::to_string (s + 1) + " steht " + std::to_string (seen[s]) + "-mal in der Kette");

    // Ein belegter Slot ohne Position kommt hinten an, sonst gäbe es einen
    // Block, den man weder sieht noch hört.
    Layout lost;
    lost.type[5] = SlotType::reverb;
    lost.sanitise();
    int lane = -1, pos = -1;
    CHECK (lost.find (5, lane, pos));

    // Eine Spur fasst höchstens zehn.
    Layout full;
    for (int s = 0; s < kNumSlots; ++s) { full.type[(size_t) s] = (SlotType) (1 + s % 12); }
    full.length[0] = 0;
    full.sanitise();
    CHECK (full.length[0] <= kMaxLaneSlots);
    CHECK (full.length[1] <= kMaxLaneSlots);
}

NMP_TEST (Platz_WerKeinenTypWechseltBehaeltSeinenBlock)
{
    // Ein Capture, eine Hallfahne, ein laufendes Delay: wer seinen Typ behält,
    // behält seinen Block, auch an anderer Stelle der Kette.
    Rack r;
    r.prepare ({ 48000.0, 256 });

    auto l = makeLayout ({ SlotType::nam, SlotType::delay, SlotType::ir });
    r.requestLayout (l);
    Block* nam = r.block (0);
    Block* del = r.block (1);
    CHECK (nam != nullptr && del != nullptr);

    // Delay vor den Amp ziehen, und eine zweite NAM dazu.
    Layout m = r.layout();
    m.lane[0][0] = 1; m.lane[0][1] = 0;
    m.type[3] = SlotType::nam; m.lane[0][3] = 3; m.length[0] = 4;
    r.requestLayout (m);

    CHECK (r.block (0) == nam);
    CHECK (r.block (1) == del);
    CHECK (r.block (3) != nullptr && r.block (3) != nam);
    CHECK (r.nam (3) != nullptr);
    CHECK (r.ir (0) == nullptr);   // Slot 1 ist NAM, keine Box
}

NMP_TEST (Kette_UmsortierenKnacktNicht)
{
    // Umsortieren darf nicht knacken und keinen Effekt kurzzeitig doppelt oder
    // gar nicht rechnen.
    const double sr = 48000.0;
    const int block = 128;

    Rack r;
    r.requestLayout (makeLayout ({ SlotType::delay, SlotType::drive }));
    r.prepare ({ sr, block });
    enableAll (r);

    auto& del = *static_cast<DelayBlock*> (r.block (0));
    auto& drv = *static_cast<DriveBlock*> (r.block (1));
    del.setMix (0.4f);
    del.setFeedback (0.4f);
    drv.setModel (DriveModel::warm);

    const int n = 48000;
    Buf buf (n);
    buf.fillSine (220.0, sr, 0.3f);

    int pos = 0;
    const int swapAt = n / 2;
    while (pos < n)
    {
        if (pos <= swapAt && pos + block > swapAt)
        {
            // DRIVE vor das DELAY ziehen
            Layout m = r.layout();
            std::swap (m.lane[0][0], m.lane[0][1]);
            r.requestLayout (m);
        }
        auto v = buf.view (pos, std::min (block, n - pos));
        r.process (v, {}, {});
        pos += block;
    }

    // Gemessen gegen den eigenen Grundzustand: der größte Sprung rund um den
    // Zug darf nicht größer sein als der größte Sprung anderswo im Ton.
    const double around   = largestStep (buf.l, swapAt - 1000, swapAt + 3000);
    const double baseline = largestStep (buf.l, n / 6, n / 6 + 4000);

    std::printf ("        größter Sprung: um den Zug %.4f, sonst %.4f\n", around, baseline);
    CHECK_MSG (around <= baseline * 1.25 + 1.0e-4,
               "um den Zug herum springt es auf " + fmt (around)
               + ", sonst nur auf " + fmt (baseline) + ": das ist ein Knacken");

    const double duringDip = rms (buf.l, swapAt, swapAt + 400);
    const double normal    = rms (buf.l, n / 6, n / 6 + 400);
    std::printf ("        Pegel während des Zuges: %.1f dB gegen sonst\n",
                 gainToDb (duringDip) - gainToDb (normal));
    CHECK_MSG (gainToDb (duringDip) - gainToDb (normal) > -12.0,
               "die Senke beim Umsortieren ist zu tief oder zu lang");
}

NMP_TEST (Kette_AusgeschalteterBlockHaeltSeineLaufzeit)
{
    // Ein Block, dessen Latenz sich mit dem Fußschalter ändert, verschiebt beim
    // Treten die ganze Kette.
    const double sr = 48000.0;

    Rack r;
    r.requestLayout (makeLayout ({ SlotType::drive }));
    r.prepare ({ sr, 256 });

    const int lat = r.block (0)->latencySamples();
    CHECK_GT (lat, 0);

    r.setEnabled (0, true);
    settle (r);
    CHECK_MSG (r.latencySamples() == lat, "die gemeldete Laufzeit hängt am Fußschalter");
    r.setEnabled (0, false);
    settle (r);
    CHECK_MSG (r.latencySamples() == lat, "die gemeldete Laufzeit hängt am Fußschalter");

    Buf buf (4096);
    buf.l[0] = buf.r[0] = 1.0f;
    runRack (r, buf);

    const int peakAt = peakIndex (buf.l);
    const float peak = std::abs (buf.l[(size_t) peakAt]);
    std::printf ("        überbrückt: Impuls bei %d, Blocklaufzeit %d, Höhe %.4f\n", peakAt, lat, peak);

    CHECK_MSG (peakAt == lat, "überbrückt kommt der Impuls bei " + std::to_string (peakAt)
                              + " statt bei " + std::to_string (lat));
    CHECK_NEAR (peak, 1.0f, 1.0e-5f);
}

NMP_TEST (Kette_AbschaltenKnacktNicht)
{
    // Ein Fußschalter blendet über und schneidet nicht.
    const double sr = 48000.0;
    const int blockSize = 128;

    Rack r;
    r.requestLayout (makeLayout ({ SlotType::drive }));
    r.prepare ({ sr, blockSize });
    r.setEnabled (0, true);

    // Rauschen zeigt, wenn im trockenen Weg etwas anderes steht als das, was
    // gerade hereinkommt. Ein Sinus ist sich dafür zu ähnlich.
    Buf buf ((int) (sr * 1.0));
    buf.fillNoise (0.35f, 7u);

    const int aus = (int) (sr * 0.5);
    int pos = 0;
    while (pos + blockSize <= buf.size())
    {
        if (pos == aus) r.setEnabled (0, false);
        auto v = buf.view (pos, blockSize);
        r.process (v, {}, {});
        pos += blockSize;
    }

    const double around   = largestStep (buf.l, aus - 64, aus + (int) (sr * 0.05));
    const double baseline = largestStep (buf.l, (int) (sr * 0.1), (int) (sr * 0.35));

    std::printf ("        Abschalten: größter Sprung %.4f, sonst %.4f\n", around, baseline);
    CHECK_MSG (around <= baseline * 1.25 + 1.0e-4,
               "beim Abschalten springt es auf " + fmt (around) + ", sonst nur auf "
               + fmt (baseline) + ": das ist ein Knacken");
}

NMP_TEST (Kette_UeberbrueckterWegHaeltDieKanaeleAuseinander)
{
    // Der trockene Weg ist je Kanal eine eigene Verzögerung. Mit einer
    // gemeinsamen läse der rechte Kanal aus dem Verlauf des linken.
    Rack r;
    r.requestLayout (makeLayout ({ SlotType::drive }));
    r.prepare ({ 48000.0, 256 });
    r.setEnabled (0, false);
    settle (r);

    Buf buf (4096);
    buf.l[0] = 1.0f;
    runRack (r, buf);

    float rechts = 0.0f;
    for (int i = 0; i < 4096; ++i) rechts = std::max (rechts, std::abs (buf.r[(size_t) i]));

    std::printf ("        links ein Impuls, rechts steht %.6f\n", rechts);
    CHECK_MSG (rechts < 1.0e-6f,
               "der linke Kanal läuft in den rechten über: dort steht " + fmt (rechts));
}

NMP_TEST (Kette_LeererPlatzRechnetNichtMit)
{
    // Ein Slot ohne Typ ist überbrückt und rechnet nicht mit, auch wenn sein
    // Schalter an ist.
    Rack r;
    r.requestLayout (Layout{});
    r.prepare ({ 48000.0, 256 });
    enableAll (r);

    Buf buf (8192);
    buf.fillNoise (0.3f, 1u);
    const auto before = buf.l;
    runRack (r, buf);

    for (size_t i = 0; i < before.size(); ++i)
        CHECK_NEAR (buf.l[i], before[i], 1.0e-9f);
    CHECK (r.latencySamples() == 0);
}

NMP_TEST (Dual_ZweiGleicheSpurenKlingenWieEine)
{
    // DUAL macht breiter oder dicker, nicht lauter: zwei gleiche Spuren in der
    // Mitte ergeben genau dasselbe wie eine allein.
    const double sr = 48000.0;

    auto run = [&] (bool dual)
    {
        Rack r;
        r.requestLayout (dual ? makeLayout ({ SlotType::drive }, { SlotType::drive })
                              : makeLayout ({ SlotType::drive }));
        r.prepare ({ sr, 256 });
        enableAll (r);
        RackMix mix;
        mix.dual = dual;
        settle (r, mix);

        Buf buf (24000);
        buf.fillSine (196.0, sr, 0.3f);
        runRack (r, buf, mix);
        return buf.l;
    };

    const auto one = run (false);
    const auto two = run (true);

    double worst = 0.0;
    for (size_t i = 2000; i < one.size(); ++i) worst = std::max (worst, (double) std::abs (one[i] - two[i]));
    std::printf ("        eine Spur gegen zwei gleiche: größter Unterschied %.2e\n", worst);
    CHECK_MSG (worst < 1.0e-4, "zwei gleiche Spuren klingen anders als eine: Unterschied " + fmt (worst));
}

NMP_TEST (Dual_SpurenKommenGleichzeitigAn)
{
    // Spur A hat einen Drive (Latenz durch Oversampling), Spur B nichts. Kämen
    // beide zu verschiedenen Zeiten an, stünde der Impuls zweimal am Ausgang
    // und die Summe kämmt. Er muss einmal kommen, mit der Latenz von A.
    Rack r;
    r.requestLayout (makeLayout ({ SlotType::drive }, {}));
    r.prepare ({ 48000.0, 256 });
    r.setEnabled (0, false);           // überbrückt: der Impuls kommt unverändert durch
    RackMix mix;
    mix.dual = true;
    settle (r, mix);

    const int lat = r.block (0)->latencySamples();
    CHECK_GT (lat, 0);
    CHECK (r.latencySamples() == lat);

    Buf buf (4096);
    buf.l[0] = buf.r[0] = 1.0f;
    runRack (r, buf, mix);

    int hits = 0;
    for (int i = 0; i < 4096; ++i) if (std::abs (buf.l[(size_t) i]) > 0.05f) ++hits;
    const int at = peakIndex (buf.l);
    std::printf ("        Impuls kommt %d-mal, bei %d (Laufzeit A %d), Höhe %.3f\n",
                 hits, at, lat, buf.l[(size_t) at]);

    CHECK_MSG (hits == 1, "der Impuls kommt " + std::to_string (hits) + "-mal: die Spuren sind nicht ausgerichtet");
    CHECK (at == lat);
    CHECK_NEAR (buf.l[(size_t) at], 1.0f, 1.0e-4f);
}

NMP_TEST (Dual_AbzweigNimmtHinterSeinerStelleAb)
{
    // SPLIT: B bekommt, was in A hinter der gewählten Stelle anliegt. A trägt
    // ein Delay (100 ms, ohne Feedback), B nichts, A ganz links, B ganz rechts.
    // Zweigt B hinter dem Delay ab, kommt rechts das Echo, davor nur der
    // Impuls.
    const double sr = 48000.0;
    const int echo = (int) (sr * 0.1);

    auto echoRight = [&] (int splitAfter)
    {
        Rack r;
        r.requestLayout (makeLayout ({ SlotType::delay }, {}));
        r.prepare ({ sr, 256 });
        r.setEnabled (0, true);
        auto& del = *static_cast<DelayBlock*> (r.block (0));
        del.setType (DelayType::digital);
        del.setMix (1.0f);
        del.setFeedback (0.0f);
        del.setTimeMs (100.0f);

        RackMix mix;
        mix.dual = true;
        mix.source = LaneSource::split;
        mix.splitAfter = splitAfter;
        mix.pan[0] = -1.0f;
        mix.pan[1] = 1.0f;
        settle (r, mix, 16384);

        Buf buf (16384);
        buf.l[0] = buf.r[0] = 1.0f;
        runRack (r, buf, mix);

        float around = 0.0f;
        for (int i = echo - 200; i < echo + 200; ++i) around = std::max (around, std::abs (buf.r[(size_t) i]));
        return around;
    };

    const float vorn   = echoRight (0);
    const float hinten = echoRight (1);
    std::printf ("        Echo rechts: Abzweig vor dem Delay %.4f, dahinter %.4f\n", vorn, hinten);

    CHECK_MSG (vorn < 1.0e-3f, "vor dem Delay abgezweigt kommt rechts trotzdem das Echo");
    CHECK_MSG (hinten > 0.1f, "hinter dem Delay abgezweigt fehlt rechts das Echo");
}

NMP_TEST (Dual_TailSpieltFuerBeideSpuren)
{
    // SPLIT mit Zusammenführen: was in A hinter der Merge-Stelle steht, ist der
    // Tail. Er läuft nach dem Mischer, also auch über das Signal von B. A trägt
    // ein Delay, B nichts, A ganz links, B ganz rechts. Liegt das Delay im
    // Tail, hat auch rechts sein Echo, im parallelen Teil nicht.
    const double sr = 48000.0;
    const int echo = (int) (sr * 0.1);

    auto echoRight = [&] (int mergeAfter)
    {
        Rack r;
        r.requestLayout (makeLayout ({ SlotType::delay }, {}));
        r.prepare ({ sr, 256 });
        r.setEnabled (0, true);
        auto& del = *static_cast<DelayBlock*> (r.block (0));
        del.setType (DelayType::digital);
        del.setMix (1.0f);
        del.setFeedback (0.0f);
        del.setTimeMs (100.0f);

        RackMix mix;
        mix.dual = true;
        mix.source = LaneSource::split;
        mix.splitAfter = 0;
        mix.mergeAfter = mergeAfter;
        mix.pan[0] = -1.0f;
        mix.pan[1] = 1.0f;
        settle (r, mix, 16384);

        Buf buf (16384);
        buf.l[0] = buf.r[0] = 1.0f;
        runRack (r, buf, mix);

        float around = 0.0f;
        for (int i = echo - 200; i < echo + 200; ++i) around = std::max (around, std::abs (buf.r[(size_t) i]));
        return around;
    };

    const float parallel = echoRight (1);
    const float tail     = echoRight (0);
    std::printf ("        Echo rechts: Delay parallel %.4f, im Tail %.4f\n", parallel, tail);

    CHECK_MSG (parallel < 1.0e-3f, "ein Delay in Spur A wirkt auf Spur B, obwohl es vor dem Zusammenführen steht");
    CHECK_MSG (tail > 0.1f, "ein Delay im Tail wirkt nicht auf Spur B");
}

NMP_TEST (Dual_SoloHoertEineSpurWieAllein)
{
    // SOLO lässt eine Spur so hören, wie sie allein klänge: mittig und voll,
    // nicht 6 dB leiser als dieselbe Kette ohne DUAL.
    const double sr = 48000.0;

    Rack r;
    r.requestLayout (makeLayout ({ SlotType::comp }, { SlotType::drive }));
    r.prepare ({ sr, 256 });
    enableAll (r);

    RackMix mix;
    mix.dual = true;
    mix.solo[0] = true;
    mix.pan[0] = -0.7f;                // Pan zählt im Solo nicht
    settle (r, mix);

    Rack single;
    single.requestLayout (makeLayout ({ SlotType::comp }));
    single.prepare ({ sr, 256 });
    enableAll (single);
    settle (single);

    Buf a (24000), b (24000);
    a.fillSine (330.0, sr, 0.2f);
    b.fillSine (330.0, sr, 0.2f);
    runRack (r, a, mix);
    runRack (single, b);

    const double la = gainToDb (rms (a.l, 4000, 24000)), ra = gainToDb (rms (a.r, 4000, 24000));
    const double lb = gainToDb (rms (b.l, 4000, 24000));
    std::printf ("        Solo A: links %.2f dB, rechts %.2f dB: allein %.2f dB\n", la, ra, lb);
    CHECK_NEAR (la, lb, 0.05);
    CHECK_NEAR (ra, lb, 0.05);
}

NMP_TEST (Dual_UmschaltenKnacktNicht)
{
    // DUAL an und aus, mitten im Ton: der Weg wechselt im Tiefpunkt einer
    // kurzen Absenkung, nicht von einem Sample auf das nächste.
    const double sr = 48000.0;
    const int block = 128;

    Rack r;
    r.requestLayout (makeLayout ({ SlotType::drive }, { SlotType::chorus }));
    r.prepare ({ sr, block });
    enableAll (r);

    const int n = 72000;
    Buf buf (n);
    buf.fillSine (220.0, sr, 0.3f);

    RackMix mix;
    const int an = n / 3, aus = 2 * n / 3;
    int pos = 0;
    while (pos < n)
    {
        mix.dual = pos >= an && pos < aus;
        auto v = buf.view (pos, std::min (block, n - pos));
        r.process (v, {}, mix);
        pos += block;
    }

    const double baseline = largestStep (buf.l, n / 6, n / 6 + 4000);
    const double atOn  = largestStep (buf.l, an - 200, an + 3000);
    const double atOff = largestStep (buf.l, aus - 200, aus + 3000);
    std::printf ("        größter Sprung: DUAL an %.4f, aus %.4f, sonst %.4f\n", atOn, atOff, baseline);
    CHECK (atOn  <= baseline * 1.25 + 1.0e-4);
    CHECK (atOff <= baseline * 1.25 + 1.0e-4);
}

NMP_TEST (Spur_LueckenBleibenStehen)
{
    // Positionen wie an einem Hardware-Modeller. Eine Lücke wird gefüllt, eine
    // belegte Stelle schiebt bis zur nächsten Lücke, Herausnehmen lässt eine
    // Lücke, und sanitise packt nichts zusammen.
    Layout l;
    l.type[0] = SlotType::nam;
    l.type[1] = SlotType::ir;
    l.type[2] = SlotType::drive;
    l.type[3] = SlotType::gate;

    CHECK (l.insertAt (0, 4, 0));                 // Amp in die Mitte
    CHECK (l.length[0] == 5);
    for (int p = 0; p < 4; ++p) CHECK (l.lane[0][(size_t) p] == -1);
    CHECK (l.insertAt (0, 5, 1));                 // Box dahinter
    CHECK (l.insertAt (0, 2, 2));                 // Drive in eine Lücke davor
    CHECK (l.lane[0][2] == 2 && l.lane[0][4] == 0 && l.lane[0][5] == 1);

    CHECK (l.insertAt (0, 4, 3));                 // auf den Amp: schiebt bis zur Lücke
    CHECK (l.lane[0][4] == 3 && l.lane[0][5] == 0 && l.lane[0][6] == 1 && l.length[0] == 7);

    l.sanitise();
    CHECK (l.lane[0][2] == 2 && l.lane[0][4] == 3 && l.length[0] == 7);

    l.removeAt (0, 4);                            // Gate weg: Lücke, nichts rückt
    CHECK (l.lane[0][4] == -1 && l.lane[0][5] == 0 && l.length[0] == 7);
    l.removeAt (0, 6);                            // Box am Ende weg: Ende rückt heran
    CHECK (l.length[0] == 6);

    // Eine volle Spur nimmt nichts mehr an.
    Layout full;
    for (int p = 0; p < kMaxLaneSlots; ++p) { full.type[(size_t) p] = SlotType::eq; CHECK (full.insertAt (0, p, p)); }
    CHECK (! full.hasRoom (0));
    CHECK (! full.insertAt (0, 3, kMaxLaneSlots));

    // Lücken rechnen nicht: eine Spur mit Lücken klingt wie ohne.
    Rack r;
    r.prepare ({ 48000.0, 256 });
    Layout gaps;
    r.requestLayout (gaps);
    Buf b (4096);
    b.fillSine (440.0, 48000.0, 0.3f);
    const auto before = b.l;
    runRack (r, b);
    double diff = 0.0;
    for (int i = 1024; i < 4096; ++i) diff = std::max (diff, (double) std::abs (b.l[(size_t) i] - before[(size_t) i]));
    CHECK (diff < 1.0e-6);
}

NMP_TEST (Platz_AmpUndFullRigTeilenSichEinen)
{
    // Neue Amps ersetzen immer denselben Amp, mit Box oder ohne. Ein Full Rig
    // neben dem Amp klänge wie zwei Amps hintereinander. Ein Pedal überschreibt
    // keinen Amp.
    Layout l;
    l.type[0] = SlotType::nam; l.role[0] = NamRole::pedal;
    l.type[1] = SlotType::nam; l.role[1] = NamRole::amp;
    l.type[2] = SlotType::ir;
    l.type[3] = SlotType::nam; l.role[3] = NamRole::amp;      // Spur B bei DUAL
    l.type[4] = SlotType::nam; l.role[4] = NamRole::pedal;    // noch leer
    CHECK (l.insertAt (0, 2, 0));
    CHECK (l.insertAt (0, 4, 1));
    CHECK (l.insertAt (0, 5, 2));
    CHECK (l.insertAt (1, 4, 3));
    CHECK (l.insertAt (0, 8, 4));

    std::array<bool, kNumSlots> filled {};
    filled[0] = filled[1] = filled[2] = filled[3] = true;

    // Nichts gewählt: ein Full Rig tauscht den ersten Amp der Kette.
    CHECK (placeForFile (l, filled, SlotType::nam, NamRole::fullrig, -1) == 1);
    // Der Amp in Spur B gewählt: dort wird getauscht.
    CHECK (placeForFile (l, filled, SlotType::nam, NamRole::fullrig, 3) == 3);
    // Ein Pedal gewählt: ein Amp geht trotzdem auf den Amp, nicht aufs Pedal.
    CHECK (placeForFile (l, filled, SlotType::nam, NamRole::amp, 0) == 1);
    // Ein Amp gewählt: ein Pedal geht auf ein Pedal, ein leeres zuerst.
    CHECK (placeForFile (l, filled, SlotType::nam, NamRole::pedal, 1) == 4);
    filled[4] = true;
    CHECK (placeForFile (l, filled, SlotType::nam, NamRole::pedal, 1) == 0);
    filled[4] = false;
    // Ein leerer NAM-Slot hat noch keine Rolle und nimmt, was kommt.
    CHECK (placeForFile (l, filled, SlotType::nam, NamRole::fullrig, 4) == 4);

    // Steht schon ein Full Rig, tauscht ein reiner Amp dort.
    l.role[1] = NamRole::fullrig;
    CHECK (placeForFile (l, filled, SlotType::nam, NamRole::amp, -1) == 1);

    // Boxen gehen auf die Box. Outboard hat keinen Slot und bekommt einen
    // neuen.
    CHECK (placeForFile (l, filled, SlotType::ir, NamRole::amp, -1) == 2);
    CHECK (placeForFile (l, filled, SlotType::nam, NamRole::outboard, -1) == -1);

    CHECK (sharesPlace (NamRole::amp, NamRole::fullrig));
    CHECK (sharesPlace (NamRole::fullrig, NamRole::amp));
    CHECK (! sharesPlace (NamRole::amp, NamRole::pedal));
    CHECK (! sharesPlace (NamRole::outboard, NamRole::space));
}
