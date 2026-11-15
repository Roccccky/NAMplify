// Eigene Standalone-Hülle, aus zwei Gründen:
//
// 1. JUCEs Standalone-Fenster hat einen eigenen Rückkopplungsschutz,
// standardmäßig an und in eigenen Einstellungen gespeichert. Er sitzt vor der
// Kette und schaltet auch das Stimmgerät stumm. Hier wird er abgeschaltet,
// NAMplify hat ein eigenes Mute.
//
// 2. JUCEs Fenster hat einen eigenen Options-Knopf in der Titelleiste. Die
// Einstellungen sollen nur einen Zugang haben.
//
// Außerdem hält die Hülle Gerät und Eingang zusammen (syncInputMode): ein
// aktiver Eingangskanal heißt Mono, zwei heißen Stereo.

// Include-Reihenfolge wie in JUCEs eigener Standalone-Übersetzungseinheit.
// juce_StandaloneFilterWindow.h setzt die Modul-Header voraus.
#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "service/Tone3000.h"
#include "ui/DarkTitleBar.h"
#include "PluginEditor.h"
#include <memory>

namespace nmpl
{

class NamplifyStandaloneWindow final : public juce::DocumentWindow,
                                       private juce::ChangeListener
{
public:
    NamplifyStandaloneWindow (const juce::String& title, juce::PropertySet* settings)
        : juce::DocumentWindow (title, juce::Colour (0xff131417), allButtons)
    {
        setUsingNativeTitleBar (true);

        // Vor dem Holder lesen, danach steht dort das gerade geöffnete Gerät.
        const auto saved = settings != nullptr ? settings->getXmlValue ("audioSetup") : nullptr;
        firstContact     = saved == nullptr;
        // Ohne diesen Eintrag wurden noch keine Eingangskanäle gewählt. JUCE
        // nimmt dann die ersten zwei, und die Gitarre an Eingang 1 wird mit
        // einem leeren Kanal gemischt.
        channelsUnchosen = saved == nullptr || ! saved->hasAttribute ("audioInputChannels");

        holder = std::make_unique<juce::StandalonePluginHolder> (settings, false);

        // JUCEs Eingangs-Mute aus.
        holder->getMuteInputValue().setValue (false);

        // Das Gerät kann nach dem Fenster kommen (die Mikrofon-Abfrage läuft
        // asynchron), daher über den ChangeListener.
        holder->deviceManager.addChangeListener (this);
        syncInputMode();

        if (auto* editor = holder->processor->createEditorAndMakeActive())
        {
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
        }

        if (settings != nullptr)
        {
            const auto stored = settings->getValue ("windowState");
            if (stored.isNotEmpty()) restoreWindowStateFromString (stored);
            else                     centreWithSize (getWidth(), getHeight());
        }
        else
        {
            centreWithSize (getWidth(), getHeight());
        }

        setVisible (true);

        // Erst jetzt, die Titelleiste gehört zu einem Fenster, das es vor
        // setVisible noch nicht gibt. Sie meldet ihre Höhe, so weit rückt die
        // Seite nach unten.
        if (auto* ed = dynamic_cast<NamplifyEditor*> (getContentComponent()))
            ed->setTitleBarHeight (useDarkTitleBar (getPeer()));
        else
            useDarkTitleBar (getPeer());

        // Mindestgröße und Seitenverhältnis müssen am Fenster gesetzt werden,
        // nicht am Inhalt. Gezogen wird am Rahmen, und der fragt seinen eigenen
        // Constrainer.
        const double ratio = (double) kWindowSteps[0].width / (double) kWindowSteps[0].height;
        setResizeLimits (kWindowSteps[0].width, kWindowSteps[0].height,
                         6000, (int) (6000 / ratio));
        if (auto* c = getConstrainer()) c->setFixedAspectRatio (ratio);
    }

    ~NamplifyStandaloneWindow() override
    {
        holder->deviceManager.removeChangeListener (this);

        if (auto* settings = holder->settings.get())
            settings->setValue ("windowState", getWindowStateAsString());

        clearContentComponent();
        holder->deletePlugin();
        holder.reset();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplicationBase::quit();
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { syncInputMode(); }

    /// Hält Eingang und Gerät zusammen.
    ///
    /// Erster Start: 48 kHz und 128 Samples, wenn das Gerät das kann
    /// (A2-Captures sind auf 48 kHz trainiert), und ein Eingangskanal (Eingang
    /// 1). Die Einstellungen zeigen, was das Gerät tatsächlich macht.
    ///
    /// Wurden Gerät und Rate schon gewählt, aber noch keine Kanäle, wird nur
    /// der Eingang auf Kanal 1 gesetzt.
    ///
    /// Danach: ist genau ein Kanal aktiv, geht er auf beide Seiten.
    void syncInputMode()
    {
        auto& dm = holder->deviceManager;
        auto* device = dm.getCurrentAudioDevice();
        if (device == nullptr) return;

        if (channelsUnchosen)
        {
            channelsUnchosen = false;
            auto setup = dm.getAudioDeviceSetup();

            if (device->getInputChannelNames().size() > 0)
            {
                setup.useDefaultInputChannels = false;
                setup.inputChannels.clear();
                setup.inputChannels.setBit (0);
            }

            if (firstContact)
            {
                if (device->getAvailableSampleRates().contains (48000.0)) setup.sampleRate = 48000.0;
                if (device->getAvailableBufferSizes().contains (128))     setup.bufferSize = 128;
            }

            firstContact = false;
            dm.setAudioDeviceSetup (setup, true);

            device = dm.getCurrentAudioDevice();    // nach dem Öffnen ein anderes Objekt
            if (device == nullptr) return;
        }

        if (auto* proc = dynamic_cast<NamplifyProcessor*> (holder->processor.get()))
            proc->setStandaloneMonoInput (device->getActiveInputChannels().countNumberOfSetBits() == 1);
    }

    std::unique_ptr<juce::StandalonePluginHolder> holder;
    bool firstContact = false, channelsUnchosen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamplifyStandaloneWindow)
};

class NamplifyStandaloneApp final : public juce::JUCEApplication
{
public:
    NamplifyStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "NAMplify";
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override    { return "NAMplify"; }
    const juce::String getApplicationVersion() override { return NAMPLIFY_VERSION_LABEL; }
    // Unter Windows und Linux nur eine Instanz. Dort startet das System das
    // Programm mit namplify://t3k-callback?code=… als Kommandozeile (unter
    // Linux über xdg-open und die .desktop-Datei). Nur wenn eine zweite Instanz
    // verboten ist, reicht JUCE die Kommandozeile an die laufende weiter. Unter
    // macOS kommt sie als Apple-Event.
    bool moreThanOneInstanceAllowed() override
    {
       #if JUCE_WINDOWS || JUCE_LINUX || JUCE_BSD
        return false;
       #else
        return true;
       #endif
    }

    /// Rückkehr aus dem Browser. macOS liefert namplify://t3k-callback?code=…
    /// als Apple-Event, JUCE meldet es als weitere Instanz. Die Anmeldung
    /// wartet in diesem Programm oder in einem Plugin (siehe Tone3000).
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (Tone3000::deliverCallback (commandLine))
        {
            // Nach der Anmeldung das Fenster wieder nach vorn holen.
            if (window != nullptr) window->toFront (true);
        }
    }

    void initialise (const juce::String& commandLine) override
    {
        window = std::make_unique<NamplifyStandaloneWindow> (
            getApplicationName(), appProperties.getUserSettings());

        // Mit der Rückkehr frisch gestartet: lief das Programm nicht, startet
        // der Browser es mit der Anmeldung in der Kommandozeile.
        if (commandLine.isNotEmpty()) Tone3000::deliverCallback (commandLine);
    }

    void shutdown() override
    {
        window.reset();
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override { quit(); }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<NamplifyStandaloneWindow> window;
};

} // namespace nmpl

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new nmpl::NamplifyStandaloneApp();
}

#endif
