#pragma once
// Einstellungen, die nicht ins Preset gehören. Sie betreffen den Rechner oder
// die Abhöre, nicht den Klang:
// - Startverhalten und Einführung
// - Stimmgerät-Anzeige und gespeicherter Mute-Zustand
// - globaler EQ mit eigenen Kurven
// - FULL/LITE und Oversampling
// - Referenzpegel des Interfaces
// - Zugangsdaten und MIDI-Zuordnungen

#include <juce_data_structures/juce_data_structures.h>
#include <namplify/engine/GlobalEq.h>
#include <vector>

namespace nmpl
{

enum class OnStart { ask = 0, stayMuted, goLive };
enum class TunerDisplay { leds = 0, strobe };

struct MidiAssignment
{
    int ccNumber = -1;
    int channel = 0;        ///< 0 = alle
    int paramIndex = -1;
};

struct NamedCurve
{
    juce::String name;
    nmp::EqCurve curve;
};

class Settings
{
public:
    Settings();
    ~Settings();

    void save();

    // Start
    OnStart onStart() const { return startBehaviour; }
    void setOnStart (OnStart v) { startBehaviour = v; save(); }


    bool hasRunBefore() const { return ranBefore; }
    void markHasRun() { ranBefore = true; save(); }

    // Stimmgerät
    TunerDisplay tunerDisplay() const { return tuner; }
    void setTunerDisplay (TunerDisplay v) { tuner = v; save(); }

    /// Öffnen schaltet stumm, wie beim letzten Stimmen eingestellt.
    bool tunerMutePreference() const { return tunerMute; }
    void setTunerMutePreference (bool v) { tunerMute = v; save(); }

    // Engine
    bool engineLite() const { return lite; }
    void setEngineLite (bool v) { lite = v; save(); }

    /// Oversampling der Captures (1, 2, 4). Hängt wie FULL/LITE am Rechner,
    /// nicht am Preset.
    int  captureOversampling() const { return oversampling; }
    void setCaptureOversampling (int v) { oversampling = v >= 4 ? 4 : (v >= 2 ? 2 : 1); save(); }

    float interfaceDbu() const { return dbu; }
    void setInterfaceDbu (float v) { dbu = juce::jlimit (-20.0f, 40.0f, v); save(); }

    // globaler EQ
    bool eqEnabled() const { return eqOn; }
    void setEqEnabled (bool v) { eqOn = v; save(); }

    const nmp::EqCurve& eqCurve() const { return curve; }
    void setEqCurve (const nmp::EqCurve& c, const juce::String& name);

    juce::String eqCurveName() const { return curveName; }

    const std::vector<NamedCurve>& userCurves() const { return custom; }
    void saveUserCurve (const juce::String& name, const nmp::EqCurve&);
    bool deleteUserCurve (const juce::String& name);

    // MIDI
    const std::vector<MidiAssignment>& assignments() const { return midiMap; }
    /// Ein Regler pro CC, ein CC pro Regler.
    void learnAssignment (int cc, int channel, int paramIndex);
    void removeAssignment (int paramIndex);
    void clearAssignments();

    // TONE3000
    juce::String t3kRefreshToken() const { return refreshToken; }
    juce::String t3kUserName() const { return userName; }
    void setT3kSession (const juce::String& refresh, const juce::String& name);
    void clearT3kSession();

    /// Nur die TONE3000-Sitzung neu aus der Datei lesen. Die Anmeldung kann in
    /// einem anderen Prozess abgeschlossen werden (die Rückkehr aus dem Browser
    /// landet im Standalone, auch wenn ein Plugin wartet). Beide lesen dieselbe
    /// Datei. Gibt true zurück, wenn sich etwas geändert hat.
    bool reloadT3kSession();

    /// Ordner der Einstellungen. Dort legt die Anmeldung ihre PKCE-Daten ab.
    juce::File directory() const { return file.getParentDirectory(); }

private:
    void load();
    static juce::var curveToVar (const nmp::EqCurve&);
    static nmp::EqCurve curveFromVar (const juce::var&);

    juce::File file;

    OnStart      startBehaviour = OnStart::ask;
    bool         ranBefore = false;
    TunerDisplay tuner = TunerDisplay::leds;
    bool         tunerMute = true;
    bool         lite = false;
    int          oversampling = 1;
    float        dbu = 12.0f;

    bool          eqOn = false;
    juce::String  curveName { "FLAT" };
    nmp::EqCurve  curve;
    std::vector<NamedCurve> custom;

    std::vector<MidiAssignment> midiMap;

    juce::String refreshToken, userName;
    /// Hat dieser Prozess die Sitzung seit dem letzten Schreiben geändert?
    /// Sonst gilt beim Schreiben der Stand aus der Datei, siehe save().
    bool sessionChanged = false;
};

} // namespace nmpl
