#include "PluginEditor.h"
#include "ui/Resources.h"
#include <cmath>
#include <memory>

namespace nmpl
{

NamplifyEditor::NamplifyEditor (NamplifyProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setImageRoot (proc.library().root());

    // Events an die Seite, nur wenn sie sichtbar ist.
    auto emit = [this] (const juce::Identifier& id, const juce::var& payload)
    {
        if (browser != nullptr) browser->emitEventIfBrowserIsVisible (id, payload);
    };

    bridge = std::make_unique<WebBridge> (proc, emit);

    using Options = juce::WebBrowserComponent::Options;

    auto options = Options{}
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([] (const auto& path) { return provideResource (path); },
                               juce::URL (juce::WebBrowserComponent::getResourceProviderRoot())
                                   .getOrigin())
        .withNativeFunction ("nmp",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                if (bridge == nullptr) { completion (juce::var()); return; }

                const juce::var request = args.isEmpty() ? juce::var() : args[0];
                bridge->handle (request,
                    [completion] (juce::var reply) mutable { completion (reply); });
            })
        .withUserScript ("window.__NAMPLIFY_EMBEDDED__ = true;");

   #if JUCE_WINDOWS
    // WebView2 braucht einen beschreibbaren Datenordner. Standardmäßig legt es
    // ihn neben die EXE, und unter Program Files darf ein normaler Benutzer
    // dort nicht schreiben. temp wird aufgeräumt, daher der
    // Anwendungsdatenordner des Benutzers.
    auto webDaten = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("NAMplify").getChildFile ("WebView2");
    webDaten.createDirectory();

    options = options.withBackend (Options::Backend::webview2)
                     .withWinWebView2Options (
                         Options::WinWebView2{}.withUserDataFolder (webDaten));
   #endif

    browser = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*browser);
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);

    // Festes Seitenverhältnis, abgeleitet von der kleinsten Größe.
    const double ratio = (double) kWindowSteps[0].width / (double) kWindowSteps[0].height;
    setResizeLimits (kWindowSteps[0].width, kWindowSteps[0].height, 6000, (int) (6000 / ratio));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (ratio);

    // Standardgröße ist die größte Stufe.
    applyWindowStep (kNumWindowSteps - 1);

    bridge->setSizeRequest ([this] (int step) { applyWindowStep (step); });

    if (proc.isStandalone())
    {
        dragStrip = std::make_unique<TitleDragStrip>();
        addAndMakeVisible (*dragStrip);
    }
}

NamplifyEditor::TitleDragStrip::TitleDragStrip()
{
    setInterceptsMouseClicks (true, false);   // Klicks ja, keine Kinder
}

void NamplifyEditor::TitleDragStrip::mouseDown (const juce::MouseEvent& e)
{
    if (auto* top = getTopLevelComponent()) dragger.startDraggingComponent (top, e);
}

void NamplifyEditor::TitleDragStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (auto* top = getTopLevelComponent()) dragger.dragComponent (top, e, nullptr);
}

void NamplifyEditor::applyWindowStep (int step)
{
    step = juce::jlimit (0, kNumWindowSteps - 1, step);

    if (step < (int) std::size (kWindowSteps))
    {
        setSize (kWindowSteps[step].width, kWindowSteps[step].height);
        centreWindow();
        return;
    }

    // Die letzte Stufe füllt den Bildschirm im Rahmen des Seitenverhältnisses.
    // Ohne Bildschirm (Host ohne GUI, Tests) bleibt es bei der vorletzten.
    const double ratio = (double) kWindowSteps[0].width / (double) kWindowSteps[0].height;

    auto area = juce::Rectangle<int>();
    if (auto* display = juce::Desktop::getInstance().getDisplays()
                            .getPrimaryDisplay())
        area = display->userArea;

    if (area.isEmpty())
    {
        const auto& last = kWindowSteps[std::size (kWindowSteps) - 1];
        setSize (last.width, last.height);
        centreWindow();
        return;
    }

    int w = area.getWidth();
    int h = (int) std::round (w / ratio);
    if (h > area.getHeight())
    {
        h = area.getHeight();
        w = (int) std::round (h * ratio);
    }

    setSize (w, h);
    centreWindow();
}

/// Nach jedem Größenwechsel zentrieren. centreWithSize nutzt den verfügbaren
/// Bereich (ohne Menüleiste bzw. Taskleiste). Verzögert, weil das Fenster erst
/// wächst, wenn der Inhalt seine neue Größe gemeldet hat.
void NamplifyEditor::centreWindow()
{
    if (! proc.isStandalone()) return;   // im Plugin gehört das Fenster dem Host

    juce::Component::SafePointer<NamplifyEditor> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe == nullptr) return;
        if (auto* top = safe->getTopLevelComponent())
            top->centreWithSize (top->getWidth(), top->getHeight());
    });
}

NamplifyEditor::~NamplifyEditor()
{
    browser.reset();
    bridge.reset();
}

void NamplifyEditor::paint (juce::Graphics& g)
{
    // Hintergrund, solange die Seite lädt. Gleicher Wert wie --metal-1 in
    // ui/theme.css und in DarkTitleBar.mm, damit oben keine Kante entsteht.
    g.fillAll (juce::Colour (0xff121212));
}

void NamplifyEditor::resized()
{
    // Die Seite spart die Titelleiste aus, siehe titleBarHeight.
    if (browser != nullptr)
        browser->setBounds (getLocalBounds().withTrimmedTop (titleBarHeight));

    // Der Zieh-Streifen liegt über dem Editor, nicht über dem Browser.
    if (dragStrip != nullptr)
        dragStrip->setBounds (getLocalBounds().removeFromTop (titleBarHeight));
}

void NamplifyEditor::visibilityChanged()      { updateVisibility(); }
void NamplifyEditor::parentHierarchyChanged() { updateVisibility(); }

void NamplifyEditor::updateVisibility()
{
    if (bridge != nullptr) bridge->setVisible (isShowing());
}

} // namespace nmpl
