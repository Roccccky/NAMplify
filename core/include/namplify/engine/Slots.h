#pragma once
// Freie Slots.
//
// Ein Slot ist leer, bis er einen Typ bekommt (NAM, IR, Drive, Delay …).
// Regler, Bereiche, Werkswerte und Namen der Typen stehen hier. Host, Presets,
// MIDI und UI lesen sie von hier.
//
// Slots haben feste Nummern, damit die Automation beim Verschieben in der Kette
// mitgeht. Die Position steht im Layout.

#include "Params.h"
#include <array>
#include <cstdint>

namespace nmp
{

/// Mögliche Typen eines Slots. Die Nummern werden in Presets gespeichert: nur
/// hinten anfügen.
enum class SlotType : int
{
    empty = 0,
    gate,
    comp,
    drive,
    nam,        ///< ein Capture, die Rolle sagt, was es ist
    eq,
    ir,         ///< Impulsantwort
    chorus,
    flanger,
    phaser,
    tremolo,
    delay,
    reverb,
    count
};

inline constexpr int kNumSlotTypes = (int) SlotType::count;

/// Rolle eines NAM-Slots. Bestimmt Farbe, Regler und Verhalten, die
/// Verarbeitung ist dieselbe.
enum class NamRole : int
{
    amp = 0,    ///< Klangregelung hinter dem Netz, NORM, bekommt automatisch eine Box
    pedal,      ///< Eingangspegel immer aus der Datei, LEVEL und MIX
    outboard,   ///< hinter der Box, nimmt auch eine IR
    fullrig,    ///< Amp mit Box, braucht keine IR
    space,      ///< Raum oder Hall (TONE3000 "space"), MIX, nimmt auch eine IR
    count
};

inline constexpr int kNumNamRoles = (int) NamRole::count;

// Grenzen. kNumSlots und kSlotControls stehen in Params.h, weil die
// Host-Parameter daraus gebaut werden.

/// Spuren: A immer, B nur bei DUAL.
inline constexpr int kNumLanes = 2;

/// Maximale Anzahl Slots pro Spur.
inline constexpr int kMaxLaneSlots = 12;

/// Maximale Anzahl Blöcke eines Typs.
///
/// NAM und IR sind teuer. Bei 64er Puffer brauchen drei A2-Captures im Schnitt
/// 16 % eines Kerns (FULL) bzw. 6 % (LITE). Vier bleiben damit sicher im
/// Zeitbudget, auch mit 2-fachem Oversampling bei 128er Puffer. Für alle
/// anderen Typen reichen zwei.
int slotTypeLimit (SlotType);

// Regler je Typ

namespace ctl
{
    enum Gate    { gateThreshold };
    enum Comp    { compSustain, compLevel };
    enum Drive   { driveModel, driveGain, driveLevel, driveTone, driveBody };
    enum Nam     { namGain, namLevel, namMix, namBass, namMid, namTreble, namNorm };
    enum Eq      { eqMode, eq3Bass, eq3Mid, eq3Treble, eq7Band0 };   // … eq7Band0 + 6
    enum Ir      { irOutput };
    enum Chorus  { chorusRate, chorusDepth, chorusMix };
    enum Flanger { flangerRate, flangerDepth, flangerFeedback, flangerManual, flangerMix };
    enum Phaser  { phaserRate, phaserDepth, phaserMix };
    enum Tremolo { tremoloRate, tremoloDepth, tremoloSync, tremoloDiv };
    enum Delay   { delayType, delayTime, delayFeedback, delayMix, delaySync, delayDiv };
    enum Reverb  { reverbType, reverbTime, reverbSize, reverbBoing, reverbTone, reverbMix };
}

/// Anzahl Regler eines Typs (ohne An/Aus).
int slotControlCount (SlotType);

/// Regler k eines Typs. Die ID stammt aus Version 1 (drive_gain, amp_bass …),
/// darüber finden alte Presets ihre Werte.
const ParamDesc& slotControl (SlotType, int k);

/// Kurzname (wird in Presets gespeichert, nicht ändern) und Anzeigename.
const char* slotTypeKey (SlotType);
const char* slotTypeName (SlotType);
SlotType    slotTypeFromKey (const char* key);

/// Zeigt die Karte eines NAM-Slots mit dieser Rolle den Regler k? Nicht
/// gezeigte Regler wirken auch nicht (Engine::applySlot fragt dieselbe
/// Funktion). MIX gibt es bei PEDAL und OUTBOARD, die Klangregelung bei AMP und
/// FULL RIG. PEDAL nimmt den Eingangspegel immer aus der Datei und zeigt kein
/// NORM.
bool namControlShown (NamRole, int k);

const char* namRoleKey (NamRole);
const char* namRoleName (NamRole);

/// Farbe eines Slots (Kachel, Karte, Fußschalter, Browser). Steht hier, damit
/// Plugin und Demo dieselbe Quelle haben (ui/mock-defs.js wird von
/// tests/MockDefsTest.cpp erzeugt).
const char* slotColour (SlotType, NamRole);
NamRole     namRoleFromKey (const char* key);

/// Ob zwei Rollen denselben Platz meinen. AMP und FULL RIG sind beide der Amp:
/// beim Durchhören wird immer derselbe getauscht, mit oder ohne Box.
bool sharesPlace (NamRole, NamRole);

/// Aufbau: welcher Slot welchen Typ hat und wo er in welcher Spur steht.
///
/// Wird im Message-Thread gebaut und als Ganzes übergeben. Der Audio-Thread
/// tauscht ihn im Tiefpunkt einer kurzen Absenkung.
struct Layout
{
    std::array<SlotType, kNumSlots> type {};
    std::array<NamRole,  kNumSlots> role {};

    /// Pro Spur die Slot-Nummern in Kettenreihenfolge, -1 = leer.
    std::array<std::array<int8_t, kMaxLaneSlots>, kNumLanes> lane {};
    std::array<int, kNumLanes> length {};

    Layout();

    /// Stellt einen gültigen Aufbau her: jeder belegte Slot genau einmal in
    /// genau einer Spur, keine Spur zu lang, kein Typ über seiner Grenze.
    /// Überzählige Slots werden geleert.
    void sanitise();

    /// Spur und Position eines Slots. false, wenn er nicht in der Kette liegt.
    bool find (int slot, int& laneOut, int& posOut) const;

    /// Anzahl Slots dieses Typs.
    int count (SlotType) const;

    /// Erster Slot ohne Typ, -1 wenn alle belegt sind.
    int firstFree() const;

    // Positionen mit Lücken. Eine Spur hat kMaxLaneSlots feste Positionen.
    // Lücken (-1) sind erlaubt und werden nicht gerechnet. length ist die
    // Position hinter dem letzten belegten Slot.

    /// Legt slot an Position pos. Eine Lücke wird gefüllt, sonst rückt alles
    /// bis zur nächsten Lücke eins nach rechts. false, wenn rechts kein Platz
    /// mehr frei ist.
    bool insertAt (int laneIndex, int pos, int slot);

    /// Entfernt den Slot an pos und lässt eine Lücke. Die übrigen bleiben
    /// stehen, Lücken am Ende fallen weg.
    void removeAt (int laneIndex, int pos);

    /// Ob in der Spur noch eine Position frei ist.
    bool hasRoom (int laneIndex) const;
};

/// Werksaufbau: leere Kette.
Layout defaultLayout();

/// Der Slot in der Kette, der eine Datei dieses Typs und dieser Rolle aufnimmt,
/// oder -1, wenn ein neuer gebraucht wird.
///
/// Der gewählte Slot preferred nimmt sie, wenn er denselben Typ hat und bei NAM
/// leer ist oder denselben Platz meint (sharesPlace). Sonst der erste passende
/// Slot der Kette, ein leerer vor einem belegten. filled[s]: auf Slot s liegt
/// schon eine Datei.
int placeForFile (const Layout&, const std::array<bool, kNumSlots>& filled,
                  SlotType, NamRole, int preferred);

} // namespace nmp
