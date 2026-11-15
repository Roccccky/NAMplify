#pragma once
// Editor: eine eingebettete Webseite.

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "ui/WebBridge.h"
#include <memory>

// juce_IncludeModuleHeaders.h setzt #define Component juce::Component. In
// Übersetzungseinheiten, die das gesehen haben (das Standalone), wird
// juce::Component sonst zu juce::juce::Component. Wird nur in JUCEs eigenen
// Wrapper-Quellen gebraucht.
#ifdef Component
 #undef Component
#endif

namespace nmpl
{

/// Drei Fenstergrößen mit festem Seitenverhältnis. Die kleinste (1194 × 834)
/// ist zugleich die Mindestgröße, darüber eine mittlere, die größte füllt den
/// Bildschirm.
struct WindowStep
{
    int width, height;
};

inline constexpr WindowStep kWindowSteps[] = { { 1194, 834 }, { 1552, 1084 } };
inline constexpr int kNumWindowSteps = 3;   ///< die dritte füllt den Bildschirm

class NamplifyEditor : public juce::AudioProcessorEditor
{
public:
    explicit NamplifyEditor (NamplifyProcessor&);
    ~NamplifyEditor() override;

    /// Auf eine der drei Größen wechseln. Die letzte füllt den Bildschirm.
    void applyWindowStep (int step);

    /// Höhe der Titelleiste (siehe titleBarHeight).
    void setTitleBarHeight (int h) { titleBarHeight = h; resized(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

private:
    void updateVisibility();
    void centreWindow();

    /// Bereich zum Ziehen des Fensters. Der WKWebView liegt als native View
    /// über allem, was JUCE zeichnet. Der Streifen funktioniert nur, weil die
    /// Webseite oben ausgespart wird (titleBarHeight). Nur im Standalone.
    class TitleDragStrip : public juce::Component
    {
    public:
        TitleDragStrip();
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        juce::ComponentDragger dragger;
    };

    NamplifyProcessor& proc;
    std::unique_ptr<WebBridge> bridge;
    std::unique_ptr<juce::WebBrowserComponent> browser;

    /// Höhe des Bereichs oben, den die Titelleiste des Systems belegt. Die
    /// Webseite wird dort ausgespart, sonst nimmt der WKWebView die
    /// Mausereignisse. Gleiche Farbe wie der Seitenhintergrund (--metal-1).
    int titleBarHeight = 0;
    std::unique_ptr<TitleDragStrip> dragStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamplifyEditor)
};

} // namespace nmpl
