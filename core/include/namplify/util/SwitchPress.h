#pragma once
// Auswertung eines CC-Werts für einen Schalter.
//
// Fußschalter senden eine von drei Varianten:
// - Taster: 127 beim Drücken, 0 beim Loslassen.
// - Rastend: 127 beim einen Tritt, 0 beim nächsten.
// - Ein fester Wert pro Tritt, oft 0 oder 127.
//
// Regeln:
// - >= 64 schaltet um.
// - < 64 kurz nach >= 64 ist das Loslassen eines Tasters: nichts tun.
// - < 64 lange nach >= 64 ist der zweite Tritt eines rastenden Schalters: aus.
// - < 64 nach < 64 ist ein Schalter, der nur niedrige Werte sendet: umschalten.
//
// "Kurz" heißt unter 750 ms.

namespace nmp
{

class SwitchPress
{
public:
    enum class Action { none, toggle, off };

    static constexpr double kReleaseWindowMs = 750.0;

    /// Eingehender Wert (0 … 127) und Zeit in Millisekunden.
    Action feed (int value, double nowMs)
    {
        Action action;

        if (value >= 64)
        {
            action = Action::toggle;
            lastHighMs = nowMs;
        }
        else if (lastValue >= 64)
        {
            action = nowMs - lastHighMs < kReleaseWindowMs ? Action::none : Action::off;
        }
        else
        {
            action = Action::toggle;
        }

        lastValue = value;
        return action;
    }

private:
    int    lastValue  = -1;
    double lastHighMs = 0.0;
};

} // namespace nmp
