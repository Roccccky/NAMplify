#include "DarkTitleBar.h"
#include <cmath>

#if JUCE_MAC
 #import <Cocoa/Cocoa.h>
#endif

#if JUCE_WINDOWS
 #include <windows.h>
 #include <dwmapi.h>
 #pragma comment (lib, "dwmapi.lib")
#endif

namespace nmpl
{

int useDarkTitleBar ([[maybe_unused]] ::juce::ComponentPeer* peer)
{
   #if JUCE_MAC
    // 1. Das Erscheinungsbild gilt für das ganze Programm, damit auch Menüs und
    // Dialoge dunkel sind.
    if (@available (macOS 10.14, *))
        [NSApp setAppearance: [NSAppearance appearanceNamed: NSAppearanceNameDarkAqua]];

    // 2. Die Leiste selbst: transparent, ohne Titel, der Fensterhintergrund in
    // der Farbe der Seite. Die Fensterknöpfe liegen darauf.
    if (auto* nsView = (NSView*) (peer != nullptr ? peer->getNativeHandle() : nullptr))
    {
        if (NSWindow* w = [nsView window])
        {
            w.titlebarAppearsTransparent = YES;
            w.titleVisibility            = NSWindowTitleHidden;
            w.styleMask |= NSWindowStyleMaskFullSizeContentView;

            // Gleicher Wert wie --metal-1 in ui/theme.css.
            w.backgroundColor = [NSColor colorWithSRGBRed: 0x12 / 255.0
                                                    green: 0x12 / 255.0
                                                     blue: 0x12 / 255.0
                                                    alpha: 1.0];

            // Tatsächliche Höhe der Leiste, hängt von der Systemeinstellung ab.
            const auto frame   = [w frame];
            const auto content = [w contentRectForFrameRect: frame];
            const int  bar = (int) std::round (frame.size.height - content.size.height);
            return bar > 0 ? bar : 28;
        }
    }
   #endif

   #if JUCE_WINDOWS
    // Windows: 20 ist DWMWA_USE_IMMERSIVE_DARK_MODE, 35 ist DWMWA_CAPTION_COLOR
    // (ab Windows 11, gleiche Farbe wie die Seite). Vor Windows 10 20H1 hieß
    // das erste Attribut 19. Beide setzen, unbekannte Attribute liefern nur
    // einen Fehler.
    if (peer != nullptr)
    {
        auto hwnd = (HWND) peer->getNativeHandle();
        const BOOL dark = TRUE;
        DwmSetWindowAttribute (hwnd, 20, &dark, sizeof dark);
        DwmSetWindowAttribute (hwnd, 19, &dark, sizeof dark);

        // COLORREF ist 0x00BBGGRR, gleiches --metal-1 wie unter macOS.
        const COLORREF caption = 0x00121212;
        DwmSetWindowAttribute (hwnd, 35, &caption, sizeof caption);
    }
   #endif

    // Sonst (und unter Windows) liegt die Leiste neben dem Inhalt.
    return 0;
}

} // namespace nmpl
