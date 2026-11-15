#pragma once
// Dunkle Titelleiste. Unter macOS hängt die Leiste am Erscheinungsbild des
// Programms, unter Windows an einem Fensterattribut. Nur für das Standalone,
// ein Plugin ändert das Fenster des Hosts nicht.

#include <juce_gui_basics/juce_gui_basics.h>

namespace nmpl
{

/// Setzt das Erscheinungsbild auf dunkel (macOS) bzw. die Titelleiste dieses
/// Fensters auf dunkel (Windows). Sonst passiert nichts.
///
/// Rückgabe: Höhe der Titelleiste über dem Inhalt in Punkten. Unter macOS liegt
/// sie mit FullSizeContentView über dem Inhalt (rund 28 Punkte mit den
/// Fensterknöpfen), die Webseite muss diesen Bereich aussparen. Unter Windows
/// liegt die Leiste neben dem Inhalt, dort ist es 0.
///
/// Der Parameter ist ComponentPeer statt Component, weil
/// juce_IncludeModuleHeaders.h #define Component juce::Component setzt.
int useDarkTitleBar (::juce::ComponentPeer* peer);

} // namespace nmpl
