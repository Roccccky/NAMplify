#include "namplify/blocks/Reverb.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

const char* reverbTypeName (ReverbType t)
{
    switch (t)
    {
        case ReverbType::room:   return "ROOM";
        case ReverbType::hall:   return "HALL";
        case ReverbType::plate:  return "PLATE";
        case ReverbType::spring: return "SPRING";
        default:                 return "PLATE";
    }
}

ReverbDefaults reverbDefaults (ReverbType t)
{
    switch (t)
    {
        case ReverbType::room:   return { 0.90f, 0.35f, 0.0f,  0.62f, 0.30f };
        case ReverbType::hall:   return { 2.60f, 0.55f, 0.0f,  0.50f, 0.22f };
        case ReverbType::plate:  return { 1.80f, 0.45f, 0.0f,  0.72f, 0.18f };
        case ReverbType::spring: return { 2.20f, 0.0f,  0.62f, 0.30f, 0.32f };
        default:                 return { 1.80f, 0.45f, 0.0f,  0.72f, 0.18f };
    }
}

namespace
{
    /// Grundlängen der acht Leitungen in ms. Ohne einfache Verhältnisse
    /// zueinander, sonst fallen Eigenmoden zusammen und es klingelt metallisch.
    const float kRoomLen[8]  = { 13.7f, 17.3f, 21.1f, 25.9f, 29.3f, 33.7f, 37.1f, 41.3f };
    const float kHallLen[8]  = { 31.7f, 38.3f, 47.9f, 53.9f, 62.3f, 71.3f, 79.7f, 89.9f };
    const float kPlateLen[8] = {  7.3f,  9.7f, 11.9f, 13.9f, 16.1f, 18.7f, 21.3f, 24.1f };

    /// Diffusoren, links und rechts verschieden.
    const int kDiffLenL[4] = { 142, 379, 107, 277 };
    const int kDiffLenR[4] = { 131, 401,  97, 293 };

    /// Bezugsfrequenz der Nachhallzeit. RT60 wird in der Raumakustik
    /// üblicherweise bei 500 Hz angegeben, weil Höhen schneller ausklingen.
    constexpr double kRtReferenceHz = 500.0;

    /// Schnelle Hadamard-Mischung über acht Werte, danach 1/√8. Verlustfrei,
    /// die Energie hängt nur an den Feedback-Faktoren. Damit lässt sich die
    /// Nachhallzeit vorab berechnen.
    inline void hadamard8 (float* v)
    {
        for (int s = 1; s < 8; s <<= 1)
            for (int i = 0; i < 8; i += s << 1)
                for (int j = i; j < i + s; ++j)
                {
                    const float a = v[j], b = v[j + s];
                    v[j] = a + b;
                    v[j + s] = a - b;
                }

        constexpr float inv = 0.35355339f;   // 1/√8
        for (int i = 0; i < 8; ++i) v[i] *= inv;
    }
}

void ReverbBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;

    for (auto& l : lines) l.prepare (sampleRate, 0.140);
    erLine.prepare (sampleRate, 0.150);

    springDelayL.prepare (sampleRate, 0.120);
    springDelayR.prepare (sampleRate, 0.120);

    // Hochpass im nassen Weg.
    const auto hp = designHighPass (85.0, 0.707, sampleRate);
    wetHpL.setCoeffs (hp);
    wetHpR.setCoeffs (hp);

    mixSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTarget (mix);

    seedModulation();

    dirty = true;
    rebuild();
    reset();
}

void ReverbBlock::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& d : dampState) d = 0.0f;
    for (auto& d : diffL) d.reset();
    for (auto& d : diffR) d.reset();
    erLine.reset();
    erLp.reset();
    erLpR.reset();
    for (auto& a : springL) a.reset();
    for (auto& a : springR) a.reset();
    springDelayL.reset();
    springDelayR.reset();
    springDampL = springDampR = 0.0f;
    springBpL.reset();
    springBpR.reset();
    wetHpL.reset();
    wetHpR.reset();
}

void ReverbBlock::setType (ReverbType t)
{
    if (t == currentType) return;
    currentType = t;
    dirty = true;
}

void ReverbBlock::rebuild()
{
    if (! dirty) return;
    dirty = false;

    const float sr = (float) sampleRate;

    // Dämpfung pro Leitung, aus ihrer Länge abgeleitet (nach Jot). Ein
    // gemeinsamer Dämpfer würde kurze Leitungen pro Sekunde stärker dämpfen als
    // lange, und die gemessene Zeit stimmte nicht mehr.
    //
    // Pro Leitung gilt:
    //   tiefe Frequenzen: Abfall pro Runde = 10^(−3·d/T_tief)
    //   hohe Frequenzen:  Abfall pro Runde = 10^(−3·d/T_hoch)
    // Daraus folgt für den Einpol-Tiefpass
    //   (1−c)/(1+c) = 10^(−3·d·(1/T_hoch − 1/T_tief))
    //
    // TONE ist das Verhältnis der beiden Zeiten.
    const float hfRatio = mapToLog (tone, 0.12f, 0.95f);

    if (currentType == ReverbType::spring)
    {
        // Federhall. Hohe Anteile laufen schneller durch die Feder als tiefe,
        // ein Anschlag kommt als abwärts laufender Zirp zurück. Nachgebaut mit
        // einer Reihe gedehnter Allpässe.
        //
        // Ein Allpass (a + z^−M)/(1 + a·z^−M) verzögert bei positivem a die
        // Höhen stärker, der Zirp liefe aufwärts. Mit negativem a läuft er
        // abwärts.
        const float a = -mapTo (boing, 0.30f, 0.74f);
        constexpr int m = 5;

        // Schleifenlänge bei der Bezugsfrequenz. Die Gruppenlaufzeit eines
        // Allpasses ist bei 500 Hz ein Vielfaches seiner Ordnung. Mit dem
        // Mittelwert gerechnet läge die Nachhallzeit um gut 10 % daneben.
        const float wRef   = (float) (kTwoPi * kRtReferenceHz / sampleRate);
        const float apTau  = (float) m * (1.0f - a * a)
                           / (1.0f + 2.0f * a * std::cos ((float) m * wRef) + a * a);
        const float springLoopSec = (0.028f * 1.035f) + (float) kSpringAp * apTau / sr;
        {
            const float k = std::pow (10.0f, -3.0f * springLoopSec
                                              * (1.0f / (hfRatio * timeSec) - 1.0f / timeSec));
            springDampCoeff = clamp ((1.0f - k) / (1.0f + k), 0.0f, 0.995f);
        }

        for (int i = 0; i < kSpringAp; ++i)
        {
            // prepare() legt nur neu an, wenn nötig, der Inhalt bleibt
            // erhalten.
            springL[i].prepare (m);
            springR[i].prepare (m);
            springL[i].a = a;
            springR[i].a = a * 0.97f;        // leicht verschieden für echtes Stereo
        }

        springDelaySamples = 0.028f * sr;

        // Arbeitsband der Feder, ohne Anhebung. Ein Filter mit +4 dB in einer
        // Schleife mit 0,9 Feedback würde aufschwingen.
        const auto shp = designHighPass (160.0, 0.707, sampleRate);
        springBpL.setCoeffs (shp);
        springBpR.setCoeffs (shp);

        // Die Schleife ist viel länger als die Verzögerungsleitung: die
        // Allpass-Reihe hat im Mittel kSpringAp · m Samples Gruppenlaufzeit.
        springFb = std::pow (10.0f, -3.0f * springLoopSec / timeSec);

        // Ausgleich für die Dämpfung des Hochpasses bei der Bezugsfrequenz.
        const float loss = (float) shp.magnitudeAt (kRtReferenceHz, sampleRate);
        springFb = clamp (springFb / std::max (0.05f, loss), 0.0f, 0.995f);

        wetTrim = std::sqrt (std::max (0.02f, 1.0f - springFb * springFb)) * 1.80f;
        erMix = 0.0f;
        return;
    }

    // Leitungslängen
    const float* base = currentType == ReverbType::room ? kRoomLen
                      : currentType == ReverbType::hall ? kHallLen
                                                        : kPlateLen;

    const float scale = mapTo (size, 0.55f, 1.5f);

    float gSum = 0.0f;
    for (int i = 0; i < kLines; ++i)
    {
        lineDelay[i] = base[i] * scale * 0.001f * sr;
        lineDelay[i] = clamp (lineDelay[i], 8.0f, (float) lines[i].maxDelaySamples() - 64.0f);

        // Feedback aus der Länge: nach T Sekunden ist jede Leitung um 60 dB
        // gefallen.
        const float dSec = lineDelay[i] / sr;

        lineGain[i] = clamp (std::pow (10.0f, -3.0f * dSec / timeSec), 0.0f, 0.9995f);

        const float k = std::pow (10.0f, -3.0f * dSec
                                          * (1.0f / (hfRatio * timeSec) - 1.0f / timeSec));
        dampCoeff[i] = clamp ((1.0f - k) / (1.0f + k), 0.0f, 0.995f);

        gSum += lineGain[i];
    }

    const float gMean = gSum / kLines;

    // Gleiche Präsenz bei gleichem Reglerstand.
    wetTrim = std::sqrt (std::max (0.02f, 1.0f - gMean * gMean));

    // Pegelausgleich je Typ, gemessen (ReverbTest vergleicht die vier Pegel).
    switch (currentType)
    {
        case ReverbType::room:  wetTrim *= 1.80f; break;
        case ReverbType::hall:  wetTrim *= 2.10f; break;
        case ReverbType::plate:
        default:                wetTrim *= 2.25f; break;
    }

    // Diffusion. PLATE: dicht und kräftig. ROOM und HALL streuen weniger, damit
    // die Erstreflexionen hörbar bleiben.
    const float diffG = currentType == ReverbType::plate ? 0.72f : 0.55f;
    for (int i = 0; i < kDiffuse; ++i)
    {
        const int sl  = std::max (4, (int) std::lround (kDiffLenL[i] * (sr / 44100.0f) * scale));
        const int sr_ = std::max (4, (int) std::lround (kDiffLenR[i] * (sr / 44100.0f) * scale));

        // Nur bei geänderter Länge, prepare() löscht den Speicher.
        if (diffL[i].length() != sl) diffL[i].prepare (sl, diffG); else diffL[i].setGain (diffG);
        if (diffR[i].length() != sr_) diffR[i].prepare (sr_, diffG); else diffR[i].setGain (diffG);
    }

    // Erstreflexionen
    for (int i = 0; i < kErTaps; ++i)
    {
        erTapL[i] = erGainL[i] = erTapR[i] = erGainR[i] = 0.0f;
    }

    if (currentType == ReverbType::room)
    {
        // Kleiner Raum: früh und nah.
        const float tL[6] = {  6.1f, 10.7f, 14.9f, 19.3f, 24.7f, 31.1f };
        const float tR[6] = {  7.3f,  9.9f, 16.3f, 21.1f, 26.9f, 29.3f };
        const float gL[6] = {  0.72f, -0.58f, 0.47f, -0.38f, 0.29f, -0.22f };
        const float gR[6] = { -0.68f,  0.61f, -0.44f, 0.36f, -0.27f, 0.20f };

        for (int i = 0; i < 6; ++i)
        {
            erTapL[i]  = tL[i] * scale * 0.001f * sr;
            erTapR[i]  = tR[i] * scale * 0.001f * sr;
            erGainL[i] = gL[i];
            erGainR[i] = gR[i];
        }
        erMix = 0.55f;
    }
    else if (currentType == ReverbType::hall)
    {
        // Unregelmäßiges Muster, links und rechts deutlich verschieden.
        const float tL[8] = { 17.9f, 26.3f, 34.1f, 43.7f, 51.1f, 63.7f, 74.3f, 88.1f };
        const float tR[8] = { 21.3f, 29.9f, 31.7f, 47.3f, 58.9f, 67.1f, 79.9f, 83.3f };
        const float gL[8] = { 0.62f, -0.54f, 0.46f, 0.38f, -0.33f, 0.27f, -0.21f, 0.16f };
        const float gR[8] = { -0.58f, 0.51f, 0.43f, -0.41f, 0.30f, -0.25f, 0.19f, -0.15f };

        for (int i = 0; i < 8; ++i)
        {
            erTapL[i]  = tL[i] * scale * 0.001f * sr;
            erTapR[i]  = tR[i] * scale * 0.001f * sr;
            erGainL[i] = gL[i];
            erGainR[i] = gR[i];
        }
        erMix = 0.45f;
    }
    else
    {
        erMix = 0.0f;   // eine Platte hat keine Raumgeometrie
    }

    // Modulation der Leitungen gegen stehende Eigenmoden. HALL moduliert am
    // stärksten.
    const float modMs = currentType == ReverbType::hall  ? 0.55f
                      : currentType == ReverbType::plate ? 0.18f
                                                         : 0.30f;
    // Auch die Erstreflexionen laufen durch TONE, sonst hätte der Regler bei
    // ROOM kaum Wirkung.
    {
        const auto elp = designLowPass (mapToLog (tone, 1400.0f, 16000.0f), 0.707, sampleRate);
        erLp .setCoeffs (elp);
        erLpR.setCoeffs (elp);
    }

    for (int i = 0; i < kLines; ++i)
        modDepth[i] = modMs * 0.001f * sr;
}

void ReverbBlock::seedModulation()
{
    Xorshift rng (0xB007u);
    for (int i = 0; i < kLines; ++i)
    {
        modInc[i]   = (0.31 + 0.17 * rng.nextUnit()) / sampleRate;
        modPhase[i] = rng.nextUnit();
    }
}

void ReverbBlock::processFdn (StereoBlock& block)
{
    const int n = block.numFrames;

    for (int i = 0; i < n; ++i)
    {
        const float dryL = block.left[i];
        const float dryR = block.right[i];
        const float in   = 0.5f * (dryL + dryR);

        // Erstreflexionen
        float erL = 0.0f, erR = 0.0f;
        if (erMix > 0.0f)
        {
            erLine.write (in);
            for (int t = 0; t < kErTaps; ++t)
            {
                if (erGainL[t] != 0.0f) erL += erGainL[t] * erLine.readAt (erTapL[t]);
                if (erGainR[t] != 0.0f) erR += erGainR[t] * erLine.readAt (erTapR[t]);
            }
            erL = erLp.process (erL);
            erR = erLpR.process (erR);
        }

        // Diffusion
        float dl = in, dr = in;
        for (int d = 0; d < kDiffuse; ++d) { dl = diffL[d].process (dl); dr = diffR[d].process (dr); }

        // Lesen und dämpfen. Die Dämpfung sitzt hinter dem Lesen, damit auch
        // der erste Durchlauf gefiltert ist.
        float v[kLines];
        for (int k = 0; k < kLines; ++k)
        {
            modPhase[k] += modInc[k];
            if (modPhase[k] >= 1.0) modPhase[k] -= 1.0;
            const float m = (float) std::sin (kTwoPi * modPhase[k]) * modDepth[k];

            const float raw = lines[k].readAt (lineDelay[k] + m);
            dampState[k] = flushDenormal (raw + dampCoeff[k] * (dampState[k] - raw));
            v[k] = dampState[k];
        }

        // Ausgang: vier Leitungen links, vier rechts, mit wechselndem
        // Vorzeichen. So sehen beide Seiten verschiedene Moden.
        const float wetL = (v[0] - v[3] + v[4] - v[7]) * 0.5f;
        const float wetR = (v[1] + v[2] - v[5] - v[6]) * 0.5f;

        // mischen und zurückschreiben
        hadamard8 (v);

        for (int k = 0; k < kLines; ++k)
            lines[k].write (v[k] * lineGain[k] + (k < 4 ? dl : dr) * 0.35f);

        const float m = mixSm.next();

        float outL = wetHpL.process (wetL + erL * erMix) * wetTrim;
        float outR = wetHpR.process (wetR + erR * erMix) * wetTrim;

        // Trockener Weg bleibt bei 1.
        block.left[i]  = dryL + outL * m;
        block.right[i] = dryR + outR * m;
    }
}

void ReverbBlock::processSpring (StereoBlock& block)
{
    const int n = block.numFrames;

    for (int i = 0; i < n; ++i)
    {
        const float dryL = block.left[i];
        const float dryR = block.right[i];
        const float in   = 0.5f * (dryL + dryR);

        float l = springDelayL.readAt (springDelaySamples);
        float r = springDelayR.readAt (springDelaySamples * 1.07f);

        // Hier entsteht der Zirp: die Reihe verzögert tiefe Anteile stärker als
        // hohe.
        for (int a = 0; a < kSpringAp; ++a) l = springL[a].process (l);
        for (int a = 0; a < kSpringAp; ++a) r = springR[a].process (r);

        l = springBpL.process (l);
        r = springBpR.process (r);

        springDampL = flushDenormal (l + springDampCoeff * (springDampL - l));
        springDampR = flushDenormal (r + springDampCoeff * (springDampR - r));

        springDelayL.write (in * 0.5f + springDampR * springFb);
        springDelayR.write (in * 0.5f + springDampL * springFb);

        const float m = mixSm.next();

        const float outL = wetHpL.process (springDampL) * wetTrim;
        const float outR = wetHpR.process (springDampR) * wetTrim;

        block.left[i]  = dryL + outL * m;
        block.right[i] = dryR + outR * m;
    }
}

void ReverbBlock::process (StereoBlock& block, const BlockContext&)
{
    rebuild();
    mixSm.setTarget (mix);

    if (currentType == ReverbType::spring) processSpring (block);
    else                                   processFdn (block);
}

} // namespace nmp
