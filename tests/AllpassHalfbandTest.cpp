// Oversampling der Captures: das Halbbandfilter lässt den hörbaren Bereich
// durch und sperrt, was sonst zurückfaltet.

#include "TestSignals.h"
#include "namplify/dsp/AllpassHalfband.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace nmp;
using namespace nmptest;

namespace
{
    /// Pegel einer Frequenz in dB (Goertzel über ein Hann-Fenster).
    double levelDb (const std::vector<float>& x, size_t from, double freq, double rate)
    {
        const size_t n = x.size() - from;
        const double w = kTwoPi * freq / rate;
        double re = 0.0, im = 0.0, wsum = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) (n - 1));
            re += x[from + i] * win * std::cos (w * (double) i);
            im -= x[from + i] * win * std::sin (w * (double) i);
            wsum += win;
        }
        const double amp = 2.0 * std::sqrt (re * re + im * im) / wsum;
        return 20.0 * std::log10 (std::max (amp, 1.0e-12));
    }

    std::vector<float> tone (double freq, double rate, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i) v[(size_t) i] = 0.5f * (float) std::sin (kTwoPi * freq * i / rate);
        return v;
    }
}

NMP_TEST (Ueberabtastung_CaptureLaesstDenHoerbereichDurch)
{
    // Hoch und wieder runter: bis 20 kHz darf sich der Pegel nicht ändern.
    for (int factor : { 2, 4 })
    {
        double worst = 0.0;
        for (double f : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
        {
            const int n = 48000;
            CaptureOversampler os;
            os.prepare (factor, n);
            const auto in = tone (f, 48000.0, n);
            std::vector<float> fast ((size_t) (n * factor)), back ((size_t) n);
            os.upsample (in.data(), n, fast.data());
            os.downsample (fast.data(), n, back.data());
            const double d = levelDb (back, 4800, f, 48000.0) - levelDb (in, 4800, f, 48000.0);
            worst = std::max (worst, std::abs (d));
        }
        std::printf ("        %dx: größte Abweichung bis 20 kHz %.4f dB\n", factor, worst);
        CHECK_MSG (worst < 0.1, std::to_string (factor) + "x: " + fmt (worst) + " dB im Durchlass");
    }
}

NMP_TEST (Ueberabtastung_CaptureSperrtWasZurueckfaltet)
{
    // Herunter: ein Ton über 28 kHz faltet nach der Teilung unter 20 kHz und
    // muss dort weg sein. Hoch: das Spiegelbild eines Tons liegt bei 48 kHz
    // minus Ton und muss ebenso weg sein.
    for (int factor : { 2, 4 })
    {
        const double fastRate = 48000.0 * factor;
        double worstDown = -300.0, worstUp = -300.0;

        for (double f : { 28000.0, 30000.0, 36000.0, 44000.0 })
        {
            const int n = 48000;
            CaptureOversampler os;
            os.prepare (factor, n);
            const auto in = tone (f, fastRate, n * factor);
            std::vector<float> out ((size_t) n);
            os.downsample (in.data(), n, out.data());
            const double alias = 48000.0 - f;
            worstDown = std::max (worstDown, levelDb (out, 4800, alias, 48000.0) - levelDb (in, 4800 * factor, f, fastRate));
        }

        for (double f : { 4000.0, 12000.0, 18000.0, 20000.0 })
        {
            const int n = 48000;
            CaptureOversampler os;
            os.prepare (factor, n);
            const auto in = tone (f, 48000.0, n);
            std::vector<float> fast ((size_t) (n * factor));
            os.upsample (in.data(), n, fast.data());
            worstUp = std::max (worstUp, levelDb (fast, 4800 * factor, 48000.0 - f, fastRate)
                                         - levelDb (fast, 4800 * factor, f, fastRate));
        }

        std::printf ("        %dx: Faltung beim Herunterrechnen %.1f dB, Spiegel beim Hochrechnen %.1f dB\n",
                     factor, worstDown, worstUp);
        CHECK_MSG (worstDown < -90.0, std::to_string (factor) + "x: Faltung nur " + fmt (worstDown) + " dB");
        CHECK_MSG (worstUp   < -90.0, std::to_string (factor) + "x: Spiegel nur " + fmt (worstUp) + " dB");
    }
}

NMP_TEST (Ueberabtastung_CaptureHatKaumLaufzeit)
{
    // Minimalphasig: die Gruppenlaufzeit bei 1 kHz bleibt unter fünf Samples
    // der Grundrate (2×: 3,4, 4×: 4,5). Sie wird nicht als Latenz gemeldet,
    // weil der trockene Anteil einer Blende denselben Weg nimmt
    // (NamBlock::alignDry).
    for (int factor : { 2, 4 })
    {
        const int n = 48000;
        const double f = 1000.0;
        CaptureOversampler os;
        os.prepare (factor, n);
        const auto in = tone (f, 48000.0, n);
        std::vector<float> fast ((size_t) (n * factor)), back ((size_t) n);
        os.upsample (in.data(), n, fast.data());
        os.downsample (fast.data(), n, back.data());

        // Kreuzkorrelation mit Bruchteilen eines Samples.
        double best = -1.0e30, bestLag = 0.0;
        for (double lag = 0.0; lag <= 8.0; lag += 0.01)
        {
            double acc = 0.0;
            for (int i = 4800; i < n; ++i)
                acc += back[(size_t) i] * 0.5 * std::sin (kTwoPi * f * (i - lag) / 48000.0);
            if (acc > best) { best = acc; bestLag = lag; }
        }
        std::printf ("        %dx: Laufzeit bei 1 kHz %.2f Werte\n", factor, bestLag);
        CHECK_MSG (bestLag < 5.0, std::to_string (factor) + "x: " + fmt (bestLag) + " Werte Laufzeit");
    }
}
