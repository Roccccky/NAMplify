#pragma once
// TONE3000-API.
//
// - IRs filtert man über format=ir, nicht über gears=ir (das liefert eine leere
// Liste).
// - full-rig heißt in der API amp-cab.
// - Aus dem Katalog kommen nur A2-Modelle (architecture=2). Der Filter gilt nur
// für NAM, IRs haben keine Architektur.
//
// Rate-Limit: 100 Anfragen pro Minute. Gedrosselt wird hier schon ab 90.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>

namespace nmpl
{

class Settings;

struct T3kTone
{
    int          id = 0;
    juce::String title, gear, format, author, url, imageUrl, description;
    int          modelsCount = 0, downloadsCount = 0, favoritesCount = 0;
    bool         isFavorite = false;
    juce::StringArray sizes, makes, tags;
};

struct T3kModel
{
    int          id = 0;
    int          toneId = 0;
    juce::String name, size, architecture, url;
};

struct T3kPage
{
    std::vector<T3kTone> tones;
    int page = 1, pageSize = 25, total = 0, totalPages = 1;
};

/// Tabs im Browser: Suche, heruntergeladene Tones, Favoriten. saved ist
/// /tones/downloaded, nicht /tones/created (selbst veröffentlichte Tones).
enum class T3kTab { browse = 0, saved, favourites };

/// Kategorien: ALL, AMP, FULLRIG, IR, PEDALS, OUTBOARD
enum class T3kCategory { all = 0, amp, fullrig, ir, pedals, outboard, space, count };

const char* t3kCategoryName (T3kCategory);
/// Die Farbe zeigt, wohin geladen wird.
const char* t3kCategoryColour (T3kCategory);

struct T3kQuery
{
    T3kTab      tab = T3kTab::browse;
    T3kCategory category = T3kCategory::all;
    juce::String text;
    juce::String sort { "trending" };   ///< best-match · newest · oldest · trending · downloads-all-time
    int page = 1;
};

class Tone3000
{
public:
    explicit Tone3000 (Settings&);
    ~Tone3000();

    /// Ist ein Client-Schlüssel einkompiliert? Ohne ihn zeigt der Browser einen
    /// Hinweis.
    static bool hasClientId();
    static juce::String clientId();

    bool         isConnected() const;
    juce::String userName() const;

    /// Anmeldung über OAuth mit PKCE. Öffnet den Browser und wartet auf die
    /// Rückkehr über namplify://…, die das Betriebssystem zustellt.
    void connect (std::function<void (bool ok, juce::String error)>);
    void disconnect();

    /// Redirect-URL, wie bei TONE3000 eingetragen: namplify://t3k-callback.
    /// Wird für die Info.plist, die Anmeldung und den Token-Tausch verwendet.
    static juce::String redirectUri();
    static bool isCallbackUrl (const juce::String&);

    /// Eine eingegangene Rückkehr zustellen. false, wenn niemand gewartet hat.
    /// Wartet ein Plugin in einem Host, landet die Rückkehr im Standalone, das
    /// den Tausch mit den abgelegten PKCE-Daten abschließt. Das Ergebnis steht
    /// danach in den gemeinsamen Einstellungen.
    static bool deliverCallback (const juce::String& url);

    void search (const T3kQuery&, std::function<void (bool, T3kPage, juce::String)>);
    /// Varianten einer Bank. nam entscheidet über den A2-Filter, IRs haben
    /// keine Architektur.
    void listModels (int toneId, bool nam,
                     std::function<void (bool, std::vector<T3kModel>, juce::String)>);
    void setFavourite (int toneId, bool on, std::function<void (bool, juce::String)>);

    /// Lädt eine Variante. progress bekommt 0…1 oder −1, wenn die Größe
    /// unbekannt ist.
    void download (const T3kModel&,
                   std::function<void (bool, juce::MemoryBlock, juce::String)> done,
                   std::function<void (float)> progress = {});

    void fetchImage (const juce::String& url,
                     std::function<void (bool, juce::MemoryBlock, juce::String)>);

    void cancelAll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace nmpl
