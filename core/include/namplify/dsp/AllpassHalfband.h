#pragma once
// Oversampling für Captures, ohne Latenz.
//
// Ein Capture erzeugt Obertöne. Bei 2- oder 4-facher Rate haben sie Platz, auf
// dem Rückweg filtert das Halbband sie weg, statt sie zurückfalten zu lassen.
//
// Anders als der FIR-Halbband in Oversampler.h ist dieser minimalphasig: zwei
// Ketten von Allpässen erster Ordnung in Polyphasenform (Bauart wie HIIR von
// Laurent de Soras). Die Gruppenlaufzeit bleibt im Hörbereich unter einem
// Sample der Grundrate, gemeldet wird keine. So ändert sich die Latenz der
// Kette beim Umschalten nicht.
//
// Die Koeffizienten werden berechnet (elliptischer Entwurf über den Nome, wie
// in HIIRs PolyphaseIir2Designer).

#include <vector>

namespace nmp
{

/// Koeffizienten für ein Halbband aus numCoefs Allpässen. transition ist die
/// Übergangsbreite bezogen auf die hohe Rate (0 … 0,5).
std::vector<double> designAllpassHalfband (int numCoefs, double transition);

/// Eine Stufe: Faktor 2 hoch und runter, mono. Gerade Koeffizienten im ersten
/// Zweig, ungerade im zweiten.
class AllpassHalfband
{
public:
    void prepare (int numCoefs, double transition);
    void reset();

    /// n Werte → 2n Werte.
    void upsample (const float* in, int n, float* out);
    /// 2n Werte → n Werte.
    void downsample (const float* in, int n, float* out);

private:
    struct Chain
    {
        std::vector<double> coef, x, y;
        double process (double in);
        void clear();
    };

    // Getrennte Zustände für hoch und runter.
    Chain upA, upB, downA, downB;
};

/// Faktor 1, 2 oder 4 für ein Mono-Signal. Die erste Stufe hat die steile
/// Flanke, die zweite kommt mit wenigen Koeffizienten aus.
class CaptureOversampler
{
public:
    /// maxFrames: größter Block in der Grundrate. Speicher wird hier angelegt,
    /// im Audio-Thread nicht mehr.
    void prepare (int factor, int maxFrames);
    void reset();

    int factor() const { return oversampleFactor; }

    /// n Werte → n·Faktor Werte nach `out`.
    void upsample (const float* in, int n, float* out);
    /// n·Faktor Werte → n Werte nach `out`.
    void downsample (const float* in, int n, float* out);

private:
    AllpassHalfband first, second;
    std::vector<float> scratch;
    int oversampleFactor = 1;
};

} // namespace nmp
