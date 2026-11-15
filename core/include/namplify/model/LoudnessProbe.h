#pragma once
// Lautheitsmessung.
//
// Der Lautheitswert in .nam-Dateien stammt aus dem Training und streut über
// verschiedene Trainer-Versionen um bis zu 20 dB, manche Dateien haben keinen.
// Deshalb wird jedes Capture beim Laden selbst gemessen: ein festes Testsignal
// durch, Bewertung nach ITU-R BS.1770, die Differenz zum Zielpegel ist der
// Ausgleich. Messwert und Ausgleich stehen in den Einstellungen.

#include "../util/Types.h"
#include "../dsp/Biquad.h"
#include <functional>
#include <vector>

namespace nmp
{

/// Zielpegel für alle Captures. −18 dB wie im NAM-Referenz-Plugin.
inline constexpr double kNormalizationTargetDb = -18.0;

/// Das Testsignal. Fest vorgegeben und gitarrenähnlich (gezupfte Töne über den
/// ganzen Tonumfang, mit Anschlag, Ausklang und einem Akkord), weil ein
/// nichtlineares Capture auf Sinus oder Rauschen anders reagiert. Spitzenpegel
/// −12 dBFS.
std::vector<float> makeLoudnessProbe (double sampleRate);

/// K-Bewertung nach ITU-R BS.1770 (Höhenanhebung + Hochpass).
class KWeighting
{
public:
    void prepare (double sampleRate);
    void reset();
    float process (float x) { return hp.process (shelf.process (x)); }

private:
    Biquad shelf, hp;
};

/// K-bewerteter Effektivwert in dB.
double measureKWeightedDb (const float* data, int numFrames, double sampleRate);

/// Ergebnis einer Messung, wie es in den Einstellungen erscheint.
struct LoudnessMeasurement
{
    bool   valid = false;
    double measuredDb = 0.0;   ///< Messwert des Testsignals
    double trimDb = 0.0;       ///< daraus berechneter Ausgleich
    double fileLoudnessDb = 0.0;
    bool   fileHadLoudness = false;
};

/// Misst ein Capture. runner bekommt einen Puffer und schreibt in place zurück,
/// wie in der Kette. Läuft im Lade-Thread.
LoudnessMeasurement measureModelLoudness (double sampleRate,
                                          const std::function<void (float*, int)>& runner);

} // namespace nmp
