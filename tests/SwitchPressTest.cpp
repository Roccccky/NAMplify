// Fußschalter über MIDI: auf einem An/Aus-Parameter zählt der Tritt, nicht der
// Wert. Jede der drei Schalterarten muss mit einem Tritt je Umschalten
// auskommen.

#include "TestSignals.h"
#include "namplify/util/SwitchPress.h"

#include <string>
#include <vector>

using namespace nmp;
using namespace nmptest;

namespace
{
    /// Spielt Werte mit Zeitpunkten durch und zählt, wie oft der Parameter
    /// umspringt. on ist der Stand am Anfang.
    bool play (const std::vector<std::pair<int, double>>& stream, bool on, int& flips)
    {
        SwitchPress press;
        flips = 0;
        for (const auto& [value, ms] : stream)
        {
            const bool before = on;
            switch (press.feed (value, ms))
            {
                case SwitchPress::Action::toggle: on = ! on; break;
                case SwitchPress::Action::off:    on = false; break;
                case SwitchPress::Action::none:   break;
            }
            if (on != before) ++flips;
        }
        return on;
    }
}

NMP_TEST (Midi_TasterSchaltetJeTrittEinmal)
{
    // 127 beim Treten, 0 beim Loslassen, 150 ms dazwischen. Direkt
    // durchgereicht wäre das Pedal nur an, solange der Fuß draufsteht.
    int flips = 0;
    const bool on = play ({ { 127, 0.0 }, { 0, 150.0 } }, false, flips);
    CHECK_MSG (on, "nach einem Tritt ist das Pedal wieder aus");
    CHECK (flips == 1);

    // Zweiter Tritt zwei Sekunden später: aus.
    const bool off = play ({ { 127, 0.0 }, { 0, 150.0 }, { 127, 2000.0 }, { 0, 2140.0 } }, false, flips);
    CHECK (! off);
    CHECK (flips == 2);
}

NMP_TEST (Midi_RastenderSchalterSchaltetMitDemZweitenTrittAus)
{
    // 127 beim einen Tritt, 0 beim nächsten, Sekunden später. Mit der Regel aus
    // dem TONE3000-Plugin allein bräuchte das Ausschalten zwei Tritte.
    int flips = 0;
    const bool on = play ({ { 127, 0.0 } }, false, flips);
    CHECK (on);
    const bool off = play ({ { 127, 0.0 }, { 0, 3000.0 } }, false, flips);
    CHECK_MSG (! off, "der zweite Tritt eines rastenden Schalters schaltet nicht aus");
    CHECK (flips == 2);

    // Weiter im Wechsel.
    const bool again = play ({ { 127, 0.0 }, { 0, 3000.0 }, { 127, 6000.0 } }, false, flips);
    CHECK (again);
    CHECK (flips == 3);
}

NMP_TEST (Midi_SchalterMitNurEinemWertSchaltetJedesMal)
{
    // Immer 0: manche Controller senden je Tritt denselben niedrigen Wert.
    int flips = 0;
    const bool zeros = play ({ { 0, 0.0 }, { 0, 1000.0 }, { 0, 2000.0 } }, false, flips);
    CHECK (zeros);
    CHECK (flips == 3);

    // Immer 127.
    const bool highs = play ({ { 127, 0.0 }, { 127, 1000.0 } }, false, flips);
    CHECK (! highs);
    CHECK (flips == 2);
}
