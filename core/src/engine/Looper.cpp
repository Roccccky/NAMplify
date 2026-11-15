#include "namplify/engine/Looper.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

const char* loopStateName (LoopState s)
{
    switch (s)
    {
        case LoopState::empty:       return "EMPTY";
        case LoopState::recording:   return "RECORDING";
        case LoopState::playing:     return "PLAYING";
        case LoopState::stopped:     return "STOPPED";
        default:                     return "EMPTY";
    }
}

void Looper::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;
    maxSamples = (int) (sampleRate * kMaxSeconds) + 4096;
    peakStride = std::max (1, maxSamples / kPeakPoints);
    envHop     = std::max (64, maxSamples / kEnvPoints);
    crossfade  = std::max (16, (int) (sampleRate * 0.004));   // 4 ms Naht

    dryL.assign ((size_t) std::max (16, info.maxBlockSize), 0.0f);
    dryR.assign ((size_t) std::max (16, info.maxBlockSize), 0.0f);

    for (auto& t : tracks)
    {
        t.left .assign ((size_t) maxSamples, 0.0f);
        t.right.assign ((size_t) maxSamples, 0.0f);
        t.vol.reset (sampleRate, 0.03);
        t.vol.setCurrentAndTarget (t.volTarget.load());
        // 5 ms Blende.
        t.play.reset (sampleRate, 0.005);
        t.play.setCurrentAndTarget (0.0f);
    }

    reset();
}

void Looper::reset()
{
    for (auto& t : tracks)
    {
        std::fill (t.left.begin(),  t.left.end(),  0.0f);
        std::fill (t.right.begin(), t.right.end(), 0.0f);
        std::fill (t.peaks, t.peaks + kPeakPoints, 0.0f);
        std::fill (t.env, t.env + kEnvPoints, 0.0f);
        t.state.store ((int) LoopState::empty);
        t.rawLength = t.loopStart = t.loopEnd = t.writePos = 0;
        t.anchor = 0.0; t.waiting = false; t.contentStart = -1;
        t.target = 0; t.cycleAtRec = 0.0;
        t.playPos = 0.0;
        t.tailToGo = 0;
        t.finalizing = false;
        t.isMaster = false;
        t.resumeOnEnable = false;
        t.play.setCurrentAndTarget (0.0f);
        t.uiPos.store (0.0);
    }
    cycle = recordedCycle = 0.0;
    phase = 0.0;
}

void Looper::setTrackVolume (int t, float v)
{
    if (t < 0 || t >= kTracks) return;
    tracks[t].volTarget.store (clamp (v, 0.0f, 1.0f), std::memory_order_relaxed);
}

float Looper::trackVolume (int t) const
{
    if (t < 0 || t >= kTracks) return 0.0f;
    return tracks[t].volTarget.load (std::memory_order_relaxed);
}

bool Looper::anyRecording() const { return recordingTrack() >= 0; }

bool Looper::tailActive() const
{
    for (const auto& t : tracks)
    {
        // Eine laufende Blende oder der Nachlauf einer gerade geschlossenen
        // Aufnahme brauchen weitere Puffer.
        if (t.play.getCurrent() > 0.0f || t.play.isSmoothing()) return true;
        if (t.finalizing) return true;
    }
    return false;
}

int Looper::recordingTrack() const
{
    for (int i = 0; i < kTracks; ++i)
    {
        const auto s = (LoopState) tracks[i].state.load (std::memory_order_relaxed);
        if (s == LoopState::recording) return i;
    }
    return -1;
}

int Looper::oldestPlayingTrack() const
{
    for (int i = 0; i < kTracks; ++i)
    {
        const auto s = (LoopState) tracks[i].state.load (std::memory_order_relaxed);
        if (s == LoopState::playing) return i;
    }
    return -1;
}

void Looper::startRecording (Track& t, int index)
{
    std::fill (t.left.begin(),  t.left.end(),  0.0f);
    std::fill (t.right.begin(), t.right.end(), 0.0f);
    std::fill (t.peaks, t.peaks + kPeakPoints, 0.0f);
    std::fill (t.env, t.env + kEnvPoints, 0.0f);

    t.writePos = 0;
    t.rawLength = 0;
    t.loopStart = 0;
    t.loopEnd = 0;
    t.playPos = 0.0;
    t.tailToGo = 0;
    t.finalizing = false;
    t.target = 0;
    t.play.setCurrentAndTarget (0.0f);

    // Ohne vorhandene Aufnahme gibt die Beat Machine den Zyklus vor.
    if (cycle <= 0.0 && externalBar > 0.0) { cycle = externalBar; recordedCycle = externalBar; }

    t.isMaster = cycle <= 0.0;
    t.cycleAtRec = cycle;

    // Mit Clock beginnt der Inhalt auf der Eins (Vorlauf). Aufgenommen wird ab
    // sofort, was vor der Eins liegt, lässt sich über die Kante zurückholen.
    // Sonst wäre jede weitere Spur um die Verspätung des Tritts versetzt.
    t.waiting = ! t.isMaster;
    t.contentStart = t.isMaster ? latency : -1;
    if (t.isMaster) t.anchor = phase;

    t.state.store ((int) LoopState::recording);
    (void) index;
}

void Looper::closeLoop (Track& t, int index)
{
    // Latenzausgleich: so lange weiter aufnehmen, wie das Signal unterwegs ist.
    t.tailToGo = latency + crossfade;
    t.target = 0;

    // Quantisiertes Schließen: mit Clock endet die Aufnahme auf der nächsten
    // musikalischen Länge (Viertel, halber, ganzer Zyklus oder Vielfaches),
    // gemessen ab der letzten gespielten Note statt ab dem Tritt. Ist das Ziel
    // noch nicht erreicht, wird bis dorthin weiter aufgenommen, sonst
    // zurückgeschnitten.
    if (cycle > 0.0 && t.contentStart >= 0 && ! t.waiting)
    {
        const int relPos = std::max (0, t.writePos - t.contentStart);
        const int nachhall = (int) (sampleRate * 0.03);
        const int ae = musicalEnd (t, t.writePos) - t.contentStart;
        const int p = (ae > 0 && ae + nachhall < relPos) ? ae + nachhall : relPos;

        const double G = cycle / 4.0;              // ein Viertel des Zyklus
        double ziel;
        if      (p <= 1.5 * G)              ziel = G;
        else if (p <= 3.0 * G)              ziel = 2.0 * G;
        else if (2.0 * p <= 3.0 * cycle)    ziel = cycle;
        else ziel = std::round ((double) p / cycle) * cycle;

        t.target = std::max (1, (int) std::lround (ziel));

        // Weiter aufnehmen, bis Ziel und Naht erfasst sind.
        const int gebraucht = t.contentStart + t.target + crossfade - t.writePos;
        t.tailToGo = std::max (t.tailToGo, gebraucht);
    }

    t.finalizing = true;
    (void) index;
}

void Looper::applyAutoCut (Track& t)
{
    // Stille am Anfang und Ende finden. Nur die Kanten werden verschoben, die
    // Aufnahme bleibt vollständig. Die Kanten bewegen sich nur nach innen,
    // sonst käme der Nachlauf des Latenzausgleichs mit in die Schleife.
    const int from = t.loopStart;
    const int to   = t.loopEnd;
    if (to - from < 16) return;

    float peak = 0.0f;
    for (int i = from; i < to; ++i)
        peak = std::max (peak, std::max (std::abs (t.left[(size_t) i]), std::abs (t.right[(size_t) i])));

    if (peak < 1.0e-4f) return;

    const float thresh = std::max (peak * 0.02f, 3.0e-4f);
    const int guard = (int) (sampleRate * 0.010);   // 10 ms vor dem Einsatz stehen lassen

    int first = from;
    while (first < to
           && std::max (std::abs (t.left[(size_t) first]), std::abs (t.right[(size_t) first])) < thresh)
        ++first;

    int last = to - 1;
    while (last > first
           && std::max (std::abs (t.left[(size_t) last]), std::abs (t.right[(size_t) last])) < thresh)
        --last;

    if (last - first < (int) (sampleRate * 0.05)) return;   // zu wenig übrig

    t.loopStart = std::max (from, first - guard);
    t.loopEnd   = std::min (to,   last + guard + 1);
}

void Looper::finalize (Track& t, int index, double ph)
{
    t.finalizing = false;

    // Inhaltsbeginn: bei der freien ersten Aufnahme um die Latenz versetzt,
    // sonst die Eins des Zyklus plus Latenz.
    const int contentStart = clamp (t.contentStart >= 0 ? t.contentStart : latency,
                                    0, std::max (0, t.rawLength - 1));

    int len = t.target > 0 ? t.target
                           : t.rawLength - contentStart - crossfade;

    if (len < (int) (sampleRate * 0.05))
    {
        t.state.store ((int) LoopState::empty);
        t.rawLength = 0;
        t.contentStart = -1;
        return;
    }

    t.loopStart = contentStart;
    t.loopEnd   = std::min (t.rawLength, contentStart + len);
    len = t.loopLength();

    if (t.target > 0)
    {
        // Bereits quantisiert, die Länge steht seit dem Tritt fest.
    }
    else if (t.isMaster)
    {
        applyAutoCut (t);
        len = t.loopLength();

        // Mit Sync auf ganze Takte runden.
        if (syncOn && externalBar > 0.0)
        {
            const double bars = std::max (1.0, std::round ((double) len / externalBar));
            len = (int) std::lround (bars * externalBar);
            t.loopEnd = std::min (t.rawLength, t.loopStart + len);
            len = t.loopLength();
        }
        else if (externalBar > 0.0)
        {
            // Zyklus der Beat Machine: auf ein Vielfaches runden.
            const double n = std::max (1.0, std::round ((double) len / externalBar));
            len = (int) std::lround (n * externalBar);
            t.loopEnd = std::min (t.rawLength, t.loopStart + len);
            len = t.loopLength();
        }
        else
        {
            // Ohne Clock wird geschätzt, beides auf der Hüllkurve:
            // - Wiederholt sich das Material? Dann auf ganze Wiederholungen
            // begrenzen, höchstens 8 % Korrektur.
            // - Wo liegt der Beat? Dann auf ganze Takte runden, sonst auf ganze
            // Beats, höchstens 6 %.
            // Greift nur bei eindeutigem Ergebnis, sonst bleibt die Länge wie
            // gespielt.
            const int P = guessPeriod (t, t.loopStart, t.loopEnd);
            if (P > 0)
            {
                const int k = std::max (1, (len + P / 2) / P);
                const int kandidat = k * P;
                if (std::abs (kandidat - len) * 12 < len
                    && t.loopStart + kandidat <= t.rawLength)
                    len = kandidat;
            }

            const int beat = guessBeat (t, t.loopStart, t.loopEnd);
            if (beat > 0)
            {
                const int takt = beat * 4;
                const int aufTakt = std::max (takt,  ((len + takt / 2) / takt) * takt);
                const int aufBeat = std::max (beat, ((len + beat / 2) / beat) * beat);
                const int kandidat = (std::abs (aufTakt - len) * 16 < len) ? aufTakt
                                   : (std::abs (aufBeat - len) * 16 < len) ? aufBeat : 0;
                if (kandidat > 0 && t.loopStart + kandidat <= t.rawLength) len = kandidat;
            }

            // Durch acht teilbar, damit das Viertelraster der nächsten Spuren
            // aufgeht.
            len &= ~7;
            t.loopEnd = std::min (t.rawLength, t.loopStart + std::max (1, len));
            len = t.loopLength();
        }

        cycle = recordedCycle = (double) len;
    }
    else if (recordedCycle > 0.0)
    {
        // Weitere Spuren richten sich nach der ersten.
        const double n = std::max (1.0, std::round ((double) len / recordedCycle));
        len = (int) std::lround (n * recordedCycle);
        t.loopEnd = std::min (t.rawLength, t.loopStart + std::max (1, len));
    }

    // Der Anker liegt am Inhaltsbeginn: bei freier Aufnahme jetzt, mit Clock
    // seit der Eins. ph ist der Zählerstand in diesem Sample, nicht am
    // Pufferanfang, sonst begänne die Wiedergabe um bis zu einen halben Puffer
    // zu spät.
    if (t.isMaster && t.target <= 0) t.anchor = ph;

    t.playPos = 0.0;
    openPlayGate (t);
    t.state.store ((int) LoopState::playing);
    (void) index;
}

void Looper::tap (int index)
{
    if (index < 0 || index >= kTracks) return;
    auto& t = tracks[index];

    switch ((LoopState) t.state.load())
    {
        case LoopState::empty:       startRecording (t, index); break;
        case LoopState::recording:   closeLoop (t, index); break;
        // Anhalten. Die Blende läuft in process().
        case LoopState::playing:     t.state.store ((int) LoopState::stopped); break;
        // Einstieg an der Position des gemeinsamen Zählers, nicht bei der
        // eigenen Null.
        case LoopState::stopped:     openPlayGate (t);
                                     t.state.store ((int) LoopState::playing); break;
        default: break;
    }
}

void Looper::erase (int index)
{
    if (index < 0 || index >= kTracks) return;
    auto& t = tracks[index];

    const bool wasMaster = t.isMaster;

    t.state.store ((int) LoopState::empty);
    t.rawLength = t.loopStart = t.loopEnd = t.writePos = 0;
    t.playPos = 0.0;
    t.finalizing = false;
    t.tailToGo = 0;
    t.isMaster = false;
    t.waiting = false;
    t.contentStart = -1;
    t.target = 0;
    t.resumeOnEnable = false;
    // Blende ebenfalls zurücksetzen, sonst meldet tailActive dauerhaft einen
    // Ausklang.
    t.play.setCurrentAndTarget (0.0f);
    std::fill (t.peaks, t.peaks + kPeakPoints, 0.0f);

    if (wasMaster)
    {
        // Der Zyklus gehörte dieser Spur. Eine andere Spur übernimmt ihn, sonst
        // ist er wieder offen.
        bool found = false;
        for (auto& o : tracks)
            if ((LoopState) o.state.load() != LoopState::empty) { o.isMaster = true; found = true; break; }

        if (! found) { cycle = recordedCycle = 0.0; }
    }
}

void Looper::eraseAll() { for (int i = 0; i < kTracks; ++i) erase (i); }

void Looper::setEnabled (bool on)
{
    if (on == enabled) return;
    enabled = on;

    if (! enabled)
    {
        for (int i = 0; i < kTracks; ++i)
        {
            auto& t = tracks[i];
            const auto st = (LoopState) t.state.load();

            // Beim Ausschalten werden laufende Aufnahmen geschlossen, nicht
            // verworfen.
            if (st == LoopState::recording) { closeLoop (t, i); continue; }

            // Laufende Spuren werden angehalten und gemerkt, damit sie beim
            // Einschalten synchron wieder einsteigen.
            if (st == LoopState::playing)
            {
                t.resumeOnEnable = true;
                t.state.store ((int) LoopState::stopped);
            }
        }
        return;
    }

    // Wieder an: was lief, steigt an der aktuellen Zählerposition wieder ein.
    for (auto& t : tracks)
    {
        if (! t.resumeOnEnable) continue;
        t.resumeOnEnable = false;
        if ((LoopState) t.state.load() != LoopState::stopped) continue;
        openPlayGate (t);
        t.state.store ((int) LoopState::playing);
    }
}

void Looper::setLoopEdges (int index, int start, int end)
{
    if (index < 0 || index >= kTracks) return;
    auto& t = tracks[index];
    if (t.rawLength <= 0) return;

    start = clamp (start, 0, t.rawLength - 1);
    end   = clamp (end, start + (int) (sampleRate * 0.02), t.rawLength);

    // Anfang und Ende einer fertigen Schleife sind verschiebbar, gerastet auf
    // Sechzehntel des Zyklus. Gerastet wird die Länge, gemessen von der Kante,
    // die stehen bleibt, so wie die UI das Raster und die Länge anzeigt.
    const double grid = cycle / 16.0;
    if (grid >= 8.0)
    {
        const bool startBewegt = (start != t.loopStart);
        const bool endeBewegt  = (end   != t.loopEnd);
        const double roh = (double) (end - start);
        const double n = std::max (1.0, std::round (roh / grid));
        const int gerastet = std::max (1, (int) std::lround (n * grid));

        // Der Rand der Aufnahme hat Vorrang vor dem Raster: ganz nach außen
        // gezogen ergibt die ganze Aufnahme.
        if (startBewegt && ! endeBewegt)
        {
            // Das Ende bleibt stehen, der Anfang rastet darauf zu. Ganz außen
            // bleibt ganz außen.
            if (start > 0) start = std::max (0, end - gerastet);
        }
        else if (end < t.rawLength)
        {
            end = std::min (t.rawLength, start + gerastet);
        }

        start = clamp (start, 0, t.rawLength - 1);
        end   = clamp (end, start + (int) (sampleRate * 0.02), t.rawLength);
    }

    t.loopStart = start;
    t.loopEnd   = end;

    if (t.isMaster) cycle = recordedCycle = (double) t.loopLength();
    if (t.playPos >= (double) t.loopLength()) t.playPos = 0.0;
}

void Looper::openPlayGate (Track& t)
{
    // Der Anfang einer Schleife wird nicht eingeblendet, sonst würde der
    // Anschlag auf der Eins weich. Klingt noch etwas aus (schnell aus und
    // wieder an), geht die Blende von dort weiter.
    if (! enabled) return;
    if (t.play.getCurrent() <= 0.0f) t.play.setCurrentAndTarget (1.0f);
}

void Looper::updatePeak (Track& t, int pos, float value)
{
    const float a = std::abs (value);
    const int bucket = clamp (pos / peakStride, 0, kPeakPoints - 1);
    t.peaks[bucket] = std::max (t.peaks[bucket], a);
    // Hüllkurve mit feinerem Raster als die Anzeige.
    const int e = clamp (pos / envHop, 0, kEnvPoints - 1);
    t.env[e] = std::max (t.env[e], a);
}

bool Looper::anyContent() const
{
    for (const auto& t : tracks)
        if ((LoopState) t.state.load (std::memory_order_relaxed) != LoopState::empty
            && t.rawLength > 0 && t.loopEnd > t.loopStart)
            return true;
    return false;
}

// Erkennung auf der Hüllkurve statt auf dem Signal, eine Autokorrelation über
// Millionen Samples wäre im Audio-Thread zu teuer.

int Looper::musicalEnd (const Track& t, int upTo) const
{
    const int cEnd = std::min (upTo / envHop, kEnvPoints - 1);
    if (cEnd < 0) return 0;

    float mx = 0.0f;
    for (int c = 0; c <= cEnd; ++c) mx = std::max (mx, t.env[c]);
    if (mx < 1.0e-4f) return 0;

    // Relative Schwelle (7 % vom Maximum), damit Hall- und Delay-Ausklang nicht
    // als Spiel zählen.
    const float thr = std::max (3.0e-4f, mx * 0.07f);

    const int blipMax = (int) (sampleRate * 0.45) / envHop;  // kürzer: Versehen
    const int gapMin  = (int) (sampleRate * 0.50) / envHop;  // länger: Pause

    int c = cEnd;
    for (int versuch = 0; versuch < 3; ++versuch)
    {
        while (c >= 0 && t.env[c] < thr) --c;               // Stille überspringen
        if (c < 0) return 0;

        const int blockEnde = c;
        while (c >= 0 && t.env[c] >= thr) --c;              // Block abmessen
        const int blockLen = blockEnde - c;

        int c2 = c;
        while (c2 >= 0 && t.env[c2] < thr) --c2;            // Lücke davor
        const bool frueher = c2 >= 0;
        const int  luecke = c - c2;

        // Ein kurzer Block hinter einer deutlichen Lücke zählt nicht als Spiel.
        if (blockLen <= blipMax && frueher && luecke >= gapMin) continue;
        return std::min (upTo, (blockEnde + 1) * envHop);
    }
    return upTo;
}

int Looper::guessPeriod (const Track& t, int from, int to) const
{
    // Schrittweite so wählen, dass höchstens 700 Stützstellen bleiben. Der
    // Aufwand bleibt damit bei rund 200 000 Multiplikationen, unabhängig von
    // der Länge.
    constexpr int kMaxN = 700;
    const int c0 = from / envHop;
    const int c1 = std::min (to / envHop, kEnvPoints - 1);
    const int roh = c1 - c0;
    if (roh < 96) return 0;

    const int dec = std::max (2, (roh + kMaxN - 1) / kMaxN);
    const int n = roh / dec;
    if (n < 48) return 0;

    float v[kMaxN];
    double mittel = 0.0;
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int j = 0; j < dec; ++j) m = std::max (m, t.env[c0 + i * dec + j]);
        v[i] = m; mittel += m;
    }
    mittel /= n;

    double norm = 0.0;
    for (int i = 0; i < n; ++i) { const double d = v[i] - mittel; norm += d * d; }
    if (norm < 1.0e-6) return 0;

    // Unter 0,8 s keine Schleife.
    const int minLag = std::max (4, (int) (sampleRate * 0.8) / (envHop * dec));
    int best = 0; double bestR = 0.4;              // Mindestähnlichkeit
    for (int lag = minLag; lag <= n / 2; ++lag)
    {
        double r = 0.0; const int m = n - lag;
        for (int i = 0; i < m; ++i) r += (v[i] - mittel) * (v[i + lag] - mittel);
        r /= norm * ((double) m / (double) n);
        if (r > bestR) { bestR = r; best = lag; }
    }
    return best * envHop * dec;
}

int Looper::guessBeat (const Track& t, int from, int to) const
{
    // Anschläge statt Pegel: die Hüllkurve wird abgeleitet und
    // einweggleichgerichtet.
    constexpr int kMaxN = 1500;
    const int c0 = from / envHop;
    const int c1 = std::min (to / envHop, kEnvPoints - 1);
    int n = c1 - c0;
    if (n < 96) return 0;
    if (n > kMaxN) n = kMaxN;

    float v[kMaxN];
    double mittel = 0.0;
    v[0] = 0.0f;
    for (int i = 1; i < n; ++i)
    {
        const float d = t.env[c0 + i] - t.env[c0 + i - 1];
        v[i] = d > 0.0f ? d : 0.0f;
        mittel += v[i];
    }
    mittel /= n;

    double norm = 0.0;
    for (int i = 0; i < n; ++i) { const double d = v[i] - mittel; norm += d * d; }
    if (norm < 1.0e-8) return 0;

    const int lo = std::max (2, (int) (sampleRate * 60.0 / 200.0) / envHop);  // 200 BPM
    const int hi = std::min (n / 2, (int) (sampleRate * 60.0 /  60.0) / envHop);
    if (hi <= lo) return 0;

    int best = 0; double bestR = 0.25;             // Mindestdeutlichkeit
    for (int lag = lo; lag <= hi; ++lag)
    {
        double r = 0.0; const int m = n - lag;
        for (int i = 0; i < m; ++i) r += (v[i] - mittel) * (v[i + lag] - mittel);
        r /= norm * ((double) m / (double) n);
        if (r > bestR) { bestR = r; best = lag; }
    }
    return best * envHop;
}

void Looper::process (StereoBlock& block, const BlockContext& ctx)
{
    (void) ctx;

    // Ändert sich der Zyklus (Beat Machine umgestellt), laufen bestehende
    // Schleifen im neuen Tempo mit.
    double playRate = 1.0;
    if (recordedCycle > 0.0 && externalBar > 0.0 && syncOn)
    {
        const double bars = std::max (1.0, std::round (recordedCycle / std::max (1.0, externalBar)));
        const double target = bars * externalBar;
        if (target > 16.0) playRate = recordedCycle / target;
    }

    // Gemeinsamer Zähler. Läuft die Beat Machine, folgt er ihrer Phase: ohne
    // Aufnahme wird er gesetzt, sonst höchstens um einen halben Sample pro
    // Puffer nachgezogen, damit laufende Schleifen nicht springen.
    if (externalBar > 1.0 && externalPhase >= 0.0)
    {
        double d = externalPhase - std::fmod (phase, externalBar);
        if (d >  externalBar * 0.5) d -= externalBar;
        if (d < -externalBar * 0.5) d += externalBar;
        phase += anyContent() ? clamp (d, -0.5, 0.5) : d;
    }
    const double phase0 = phase;

    // Erst das Eingangssignal sichern, dann mischen.
    const int mitgeschnitten = std::min (block.numFrames, (int) dryL.size());
    std::copy (block.left,  block.left  + mitgeschnitten, dryL.begin());
    std::copy (block.right, block.right + mitgeschnitten, dryR.begin());

    for (int i = 0; i < kTracks; ++i)
    {
        auto& t = tracks[i];
        const auto st = (LoopState) t.state.load (std::memory_order_relaxed);
        if (st == LoopState::empty) continue;

        t.vol.setTarget (t.volTarget.load (std::memory_order_relaxed));

        for (int n = 0; n < block.numFrames; ++n)
        {
            const float inL = n < (int) dryL.size() ? dryL[(size_t) n] : 0.0f;
            const float inR = n < (int) dryR.size() ? dryR[(size_t) n] : 0.0f;
            const double ph = phase0 + (double) n;

            // aufnehmen
            if (st == LoopState::recording || t.finalizing)
            {
                // Vorlauf endet auf der Eins. Hier wird nur der Inhaltsbeginn
                // festgehalten, um die Latenz versetzt.
                if (t.waiting && cycle > 1.0 && std::fmod (ph, cycle) < 1.0)
                {
                    t.contentStart = t.writePos + latency;
                    t.anchor = ph;
                    t.waiting = false;
                }

                if (t.writePos < maxSamples)
                {
                    t.left [(size_t) t.writePos] = inL;
                    t.right[(size_t) t.writePos] = inR;
                    updatePeak (t, t.writePos, std::max (std::abs (inL), std::abs (inR)));
                    ++t.writePos;
                    t.rawLength = t.writePos;
                }
                else if (! t.finalizing)
                {
                    closeLoop (t, i);
                }

                if (t.finalizing && --t.tailToGo <= 0)
                {
                    finalize (t, i, ph);
                    continue;
                }
                if (st == LoopState::recording) continue;
            }

            const int len = t.loopLength();
            if (len <= 1) continue;

            const auto now = (LoopState) t.state.load (std::memory_order_relaxed);
            if (now == LoopState::recording) continue;

            // Blende der Wiedergabe: offen, wenn die Spur läuft und das Pedal
            // an ist, sonst geht sie zu. Gelesen wird, bis sie ganz zu ist.
            const bool spielt = enabled && now == LoopState::playing;
            t.play.setTarget (spielt ? 1.0f : 0.0f);
            if (! spielt && t.play.getCurrent() <= 0.0f) continue;

            // Abspielen mit Überblendung an der Naht. Die Position kommt aus
            // dem gemeinsamen Zähler. playRate dehnt die Schleife bei
            // geändertem Tempo.
            const double lenT = playRate > 0.0 ? (double) len / playRate : (double) len;
            double rel = std::fmod (ph - t.anchor, lenT);
            if (rel < 0.0) rel += lenT;
            t.playPos = std::min (rel * playRate, (double) len - 1.0e-6);

            const double p = t.playPos;
            const int    i0 = (int) p;
            const float  fr = (float) (p - i0);

            auto readAt = [&] (int idx, int chan) -> float
            {
                idx = clamp (idx, 0, t.rawLength - 1);
                return chan == 0 ? t.left[(size_t) idx] : t.right[(size_t) idx];
            };

            // Die Überblendung liegt am Ende der Schleife: der Ausklang blendet
            // in das, was vor dem Schleifenanfang aufgenommen wurde. So bleibt
            // der Anschlag auf der Eins unverändert.
            const int xf = std::min (crossfade, t.loopStart);

            auto sample = [&] (int chan) -> float
            {
                const int a = t.loopStart + i0;
                const float s0 = readAt (a, chan);
                const float s1 = readAt (a + 1, chan);
                float v = s0 + (s1 - s0) * fr;

                if (xf > 0 && i0 >= len - xf)
                {
                    const float x = (float) ((p - (double) (len - xf)) / xf);   // 0 … 1
                    const int b = t.loopStart + i0 - len;                       // Vorlauf
                    const float t0 = readAt (b, chan);
                    const float t1 = readAt (b + 1, chan);
                    const float pv = t0 + (t1 - t0) * fr;
                    v = v * (1.0f - x) + pv * x;
                }
                else if (xf <= 0 && i0 < crossfade && t.loopEnd + crossfade <= t.rawLength)
                {
                    // Kein Vorlauf vorhanden (Kante ganz vorn): Anfang mit dem
                    // einblenden, was hinter dem Ende weiterläuft.
                    const float x = (float) (p / crossfade);
                    const int b = t.loopEnd + i0;
                    const float t0 = readAt (b, chan);
                    const float t1 = readAt (b + 1, chan);
                    const float tv = t0 + (t1 - t0) * fr;
                    v = v * x + tv * (1.0f - x);
                }
                return v;
            };

            const float g = t.vol.next() * t.play.next();
            const float outL = sample (0) * g;
            const float outR = sample (1) * g;

            block.left[n]  += outL;
            block.right[n] += outR;
        }

        t.uiPos.store (t.loopLength() > 1 ? t.playPos / (double) t.loopLength() : 0.0,
                       std::memory_order_relaxed);
    }

    // Einmal pro Puffer, nicht pro Spur.
    phase = phase0 + (double) block.numFrames;
}

LoopTrackInfo Looper::trackInfo (int i) const
{
    LoopTrackInfo info;
    if (i < 0 || i >= kTracks) return info;

    const auto& t = tracks[i];
    info.state        = (LoopState) t.state.load (std::memory_order_relaxed);
    info.rawLength    = t.rawLength;
    info.loopStart    = t.loopStart;
    info.loopEnd      = t.loopEnd;
    info.volume       = t.volTarget.load (std::memory_order_relaxed);
    info.positionUnit = t.uiPos.load (std::memory_order_relaxed);
    info.lengthSeconds = (double) t.loopLength() / sampleRate;

    if (recordedCycle > 0.0 && externalBar > 0.0)
        info.lengthBeats = (double) t.loopLength() / externalBar * 4.0;

    return info;
}

void Looper::waveform (int i, float* out, int numPoints) const
{
    if (i < 0 || i >= kTracks || out == nullptr || numPoints <= 0) return;

    const auto& t = tracks[i];
    const int usedPoints = t.rawLength > 0
                         ? clamp (t.rawLength / peakStride + 1, 1, kPeakPoints)
                         : 0;

    for (int k = 0; k < numPoints; ++k)
    {
        if (usedPoints <= 0) { out[k] = 0.0f; continue; }
        const int src = clamp (k * usedPoints / numPoints, 0, kPeakPoints - 1);
        out[k] = t.peaks[src];
    }
}

} // namespace nmp
