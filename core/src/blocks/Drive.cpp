#include "namplify/blocks/Drive.h"

namespace nmp
{

const char* driveModelName (DriveModel m)
{
    switch (m)
    {
        case DriveModel::scream: return "SCREAM";
        case DriveModel::warm:   return "WARM";
        case DriveModel::clean:  return "CLEAN";
        case DriveModel::crush:  return "CRUSH";
        default:                 return "SCREAM";
    }
}

DriveDefaults driveDefaults (DriveModel m)
{
    switch (m)
    {
        case DriveModel::scream: return { 14.0f,  4.0f, 0.55f, 0.50f };
        case DriveModel::warm:   return { 12.0f, -0.2f, 0.50f, 0.45f };
        case DriveModel::clean:  return {  9.0f, -4.3f, 0.62f, 0.35f };
        case DriveModel::crush:  return { 22.0f, -2.5f, 0.45f, 0.60f };
        default:                 return { 14.0f,  4.0f, 0.55f, 0.50f };
    }
}

void DriveBlock::Channel::resetAll()
{
    preShelf.reset(); preHp.reset(); preLp.reset(); midPeak.reset(); toneLp.reset(); toneShelf.reset();
    stage2Hp.reset(); stage3Hp.reset(); interLp1.reset(); interLp2.reset();
    bodyPeak.reset(); sideHp.reset(); sideLp.reset(); tightHp.reset();
    dcBlock.reset();
    dcA = dcB = 0.0f;
}

void DriveBlock::prepare (const PrepareInfo& info)
{
    sampleRate = info.sampleRate;

    // 4-fach. 2-fach reicht bei 22 dB Gain und Akkorden in hoher Lage nicht,
    // dann entsteht Aliasing.
    os .prepare (4, info.maxBlockSize);
    osR.prepare (4, info.maxBlockSize);
    osRate = sampleRate * 4.0;

    driveGain.reset (sampleRate, 0.02);
    outLevel .reset (sampleRate, 0.02);

    dirty = true;
    updateCoefficients();
    reset();
}

void DriveBlock::reset()
{
    os.reset();
    osR.reset();
    chL.resetAll();
    chR.resetAll();
    driveGain.setCurrentAndTarget (dbToGain (gainDb));
    outLevel .setCurrentAndTarget (dbToGain (levelDb));
}

void DriveBlock::setModel (DriveModel m)
{
    if (m == currentModel) return;
    currentModel = m;
    dirty = true;
}

void DriveBlock::updateCoefficients()
{
    if (! dirty) return;
    dirty = false;

    const double sr = osRate;

    // DC-Filter: die asymmetrische Kennlinie erzeugt einen Gleichanteil, der
    // die nächste Stufe verschieben würde.
    const auto dc = designHighPass (18.0, 0.707, sr);

    auto setup = [&] (Channel& c)
    {
        c.dcBlock.setCoeffs (dc);

        switch (currentModel)
        {
            case DriveModel::scream:
            {
                // BODY bestimmt, wie viel Bass in die Verzerrung geht. Der Rest
                // bleibt sauber.
                const double bodyHz = mapToLog (1.0f - body, 180.0f, 950.0f);
                c.preShelf.setCoeffs (designHighShelf (bodyHz, 0.6, 12.0, sr));
                c.preHp   .setCoeffs (designHighPass  (bodyHz * 0.45, 0.707, sr));

                // Tiefpass vor der Verzerrung, wie in der Schaltung: das
                // Feedback der Stufe fällt oberhalb weniger kHz ab.
                c.preLp.setCoeffs (designLowPass (4200.0, 0.6, sr));

                // Mittenanhebung, typisch für SCREAM.
                c.midPeak.setCoeffs (designBell (720.0, 0.9, 4.5, sr));

                const double toneHz = mapToLog (tone, 700.0f, 6500.0f);
                c.toneLp.setCoeffs (designLowPass (toneHz, 0.707, sr));
                break;
            }

            case DriveModel::warm:
            {
                // Vollband, ohne Mittenanhebung.
                c.preHp   .setCoeffs (designHighPass (28.0, 0.707, sr));
                c.stage2Hp.setCoeffs (designHighPass (58.0, 0.707, sr));   // Koppelglied
                c.interLp1.setCoeffs (designLowPass (11000.0, 0.707, sr));

                const double bodyHz = mapToLog (body, 60.0f, 200.0f);
                c.bodyPeak.setCoeffs (designLowShelf (bodyHz, 0.7, mapTo (body, -2.0f, 4.0f), sr));

                // TONE ist hier ein Shelf, kein Tiefpass. Der Klang bleibt
                // offen, auch wenn er dunkler wird.
                c.toneShelf.setCoeffs (designHighShelf (2200.0, 0.6, mapTo (tone, -9.0f, 6.0f), sr));
                c.toneLp   .setCoeffs (designLowPass (12000.0, 0.707, sr));
                break;
            }

            case DriveModel::clean:
            {
                // Die Verzerrung läuft parallel zum trockenen Signal. Der
                // Parallelweg wird unten beschnitten, aber nur so weit, dass
                // tiefe Töne noch Verzerrung bekommen.
                const double sideHz = mapToLog (1.0f - body, 120.0f, 500.0f);
                c.sideHp.setCoeffs (designHighPass (sideHz, 0.5, sr));

                // Auch hier Tiefpass vor der Begrenzung.
                c.preLp.setCoeffs (designLowPass (5200.0, 0.6, sr));

                const double toneHz = mapToLog (tone, 1800.0f, 9000.0f);
                c.sideLp.setCoeffs (designLowPass (toneHz, 0.707, sr));
                break;
            }

            case DriveModel::crush:
            default:
            {
                // Tight: Bass unterhalb dieser Grenze geht nicht in die
                // Verzerrung.
                c.tightHp .setCoeffs (designHighPass (mapToLog (0.5f, 90.0f, 170.0f), 0.707, sr));
                c.stage2Hp.setCoeffs (designHighPass (150.0, 0.707, sr));
                c.stage3Hp.setCoeffs (designHighPass (200.0, 0.707, sr));

                // Etwas Tiefpass zwischen den Stufen gegen Fizz.
                c.interLp1.setCoeffs (designLowPass (7500.0, 0.707, sr));
                c.interLp2.setCoeffs (designLowPass (6200.0, 0.707, sr));

                // Der vorne entfernte Bass kommt hinten wieder dazu. Straff,
                // aber nicht dünn.
                c.bodyPeak.setCoeffs (designLowShelf (mapToLog (body, 80.0f, 160.0f),
                                                      0.8, mapTo (body, 0.0f, 8.0f), sr));

                const double toneHz = mapToLog (tone, 2200.0f, 8000.0f);
                c.toneLp   .setCoeffs (designLowPass (toneHz, 0.707, sr));
                c.toneShelf.setCoeffs (designBell (3200.0, 1.1, mapTo (tone, -3.0f, 5.0f), sr));
                break;
            }
        }
    };

    setup (chL);
    setup (chR);
}

// Ausgangspegel je Typ, damit ein Drive vor dem Amp um wenige dB anhebt
// (geprüft in Zerre_HebtWieEinTreter). Gemessen mit Sinus, −18 dBFS RMS,
// Werkseinstellung. CLEAN mischt nur bei und braucht keinen Ausgleich.
namespace
{
    constexpr float kScreamOut = 0.85f * 0.2512f;   // −12 dB
    constexpr float kWarmOut   = 0.80f * 0.5623f;   // −5 dB
    constexpr float kCrushOut  = 0.55f * 0.6310f;   // −4 dB
}

float DriveBlock::processScream (Channel& c, float x, float drive)
{
    float v = c.preShelf.process (x);
    v = c.preHp.process (v);
    v = c.preLp.process (v);
    v *= drive;

    // Diodenpaar, asymmetrisch bestückt.
    v = shape::diodePair (v);

    v = c.dcBlock.process (v);
    v = c.midPeak.process (v);
    v = c.toneLp.process (v);

    // Ausgang auf Pedal-Pegel, wenige dB über dem Eingang.
    return v * kScreamOut;
}

float DriveBlock::processWarm (Channel& c, float x, float drive)
{
    float v = c.preHp.process (x);

    // Erste Stufe: leise fast linear, für die Anschlagsdynamik.
    v = shape::triode (v * drive * 0.55f, 0.18f);
    v = c.stage2Hp.process (v);
    v = c.interLp1.process (v);

    // Zweite Stufe, schwächer angesteuert.
    v = shape::triode (v * (0.7f + drive * 0.25f), -0.12f);

    v = c.dcBlock.process (v);
    v = c.bodyPeak.process (v);
    v = c.toneShelf.process (v);
    v = c.toneLp.process (v);

    return v * kWarmOut;
}

float DriveBlock::processClean (Channel& c, float x, float drive)
{
    // Das trockene Signal bleibt vollständig erhalten.
    const float dry = x;

    // Parallel dazu eine harte Kante. Der Parallelweg gibt nur den Anteil über
    // einer Schwelle aus (shape::excess), bei leisem Spiel also nichts. So
    // entsteht kein Kammfilter mit dem trockenen Signal. Unterschiedliche
    // Schwellen ergeben gerade Obertöne.
    float side = c.sideHp.process (x);
    side = c.preLp.process (side);
    side = shape::excess (side * drive, 0.30f, 0.22f);
    side = shape::hardAsym (side * 2.4f);
    side = c.sideLp.process (side);
    side = c.dcBlock.process (side);

    // Trockener Weg bleibt bei 1.
    return dry + side * 0.42f;
}

float DriveBlock::processCrush (Channel& c, float x, float drive)
{
    float v = c.tightHp.process (x);

    v = shape::softAsym (v * drive * 0.5f, 0.35f);
    v = c.stage2Hp.process (v);
    v = c.interLp1.process (v);

    v = shape::softAsym (v * (1.0f + drive * 0.08f), 0.22f);
    v = c.stage3Hp.process (v);
    v = c.interLp2.process (v);

    v = shape::softAsym (v * 1.8f, 0.12f);

    v = c.dcBlock.process (v);
    v = c.bodyPeak.process (v);
    v = c.toneShelf.process (v);
    v = c.toneLp.process (v);

    return v * kCrushOut;
}

void DriveBlock::process (StereoBlock& block, const BlockContext&)
{
    updateCoefficients();

    driveGain.setTarget (dbToGain (gainDb));
    outLevel .setTarget (dbToGain (levelDb));

    const int n = block.numFrames;

    float* upL = os .upsample (block.left,  n);
    float* upR = osR.upsample (block.right, n);

    const int nUp = n * os.factor();
    const int f   = os.factor();

    // Der Drive-Wert wird pro Sample der Grundrate geholt und über die
    // Zwischenwerte gehalten.
    for (int i = 0; i < n; ++i)
    {
        const float g = driveGain.next();

        for (int k = 0; k < f; ++k)
        {
            const int j = i * f + k;
            switch (currentModel)
            {
                case DriveModel::scream:
                    upL[j] = processScream (chL, upL[j], g);
                    upR[j] = processScream (chR, upR[j], g);
                    break;
                case DriveModel::warm:
                    upL[j] = processWarm (chL, upL[j], g);
                    upR[j] = processWarm (chR, upR[j], g);
                    break;
                case DriveModel::clean:
                    upL[j] = processClean (chL, upL[j], g);
                    upR[j] = processClean (chR, upR[j], g);
                    break;
                case DriveModel::crush:
                default:
                    upL[j] = processCrush (chL, upL[j], g);
                    upR[j] = processCrush (chR, upR[j], g);
                    break;
            }
        }
    }

    (void) nUp;

    os .downsample (block.left,  n);
    osR.downsample (block.right, n);

    for (int i = 0; i < n; ++i)
    {
        const float lv = outLevel.next();
        block.left[i]  *= lv;
        block.right[i] *= lv;
    }
}

} // namespace nmp
