#include "Tone3000.h"
#include "Settings.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace nmpl
{

namespace
{
    const juce::String kApiBase { "https://www.tone3000.com" };

    juce::String base64Url (const juce::MemoryBlock& block)
    {
        return juce::Base64::toBase64 (block.getData(), block.getSize())
                   .replaceCharacter ('+', '-')
                   .replaceCharacter ('/', '_')
                   .removeCharacters ("=");
    }

    juce::String randomBase64Url (int numBytes)
    {
        juce::MemoryBlock block ((size_t) numBytes);
        auto& rng = juce::Random::getSystemRandom();
        for (int i = 0; i < numBytes; ++i)
            static_cast<juce::uint8*> (block.getData())[i] = (juce::uint8) rng.nextInt (256);
        return base64Url (block);
    }

    juce::String sha256Base64Url (const juce::String& input)
    {
        const juce::SHA256 hash (input.toRawUTF8(), (size_t) input.getNumBytesAsUTF8());
        return base64Url (hash.getRawData());
    }

    /// Kategorie in Suchparameter übersetzen. IR ist ein Format, keine
    /// Kategorie.
    void applyCategory (T3kCategory c, juce::String& gears, juce::String& format)
    {
        gears.clear();
        format.clear();

        switch (c)
        {
            case T3kCategory::amp:      gears = "amp"; break;
            case T3kCategory::fullrig:  gears = "amp-cab"; break;
            case T3kCategory::ir:       format = "ir"; break;   // nicht gears=ir
            case T3kCategory::pedals:   gears = "pedal"; break;
            case T3kCategory::outboard: gears = "outboard"; break;
            case T3kCategory::space:    gears = "space"; break;
            case T3kCategory::all:
            default: break;
        }
    }

    /// Passt der Tone in die Kategorie? TONE3000 filtert grob: unter format=ir
    /// kommen auch Räume (space), unter einer Kategorie auch deren IRs. Deshalb
    /// wird jede Antwort nach Kategorie und Format nachgefiltert.
    bool fitsCategory (const T3kTone& t, T3kCategory c)
    {
        const bool ir = t.format == "ir";
        switch (c)
        {
            case T3kCategory::amp:      return t.gear == "amp" && ! ir;
            case T3kCategory::fullrig:  return t.gear == "amp-cab" && ! ir;
            case T3kCategory::pedals:   return t.gear == "pedal" && ! ir;
            case T3kCategory::outboard: return t.gear == "outboard";
            case T3kCategory::space:    return t.gear == "space";
            case T3kCategory::ir:       return ir && (t.gear == "cab" || t.gear.isEmpty() || t.gear == "ir");
            case T3kCategory::all:
            default:                    return true;
        }
    }

    juce::StringArray varToStrings (const juce::var& v)
    {
        juce::StringArray out;
        if (auto* arr = v.getArray())
            for (const auto& item : *arr)
            {
                if (auto* o = item.getDynamicObject())
                    out.add (o->getProperty ("name").toString());
                else
                    out.add (item.toString());
            }
        return out;
    }

    T3kTone parseTone (const juce::var& v)
    {
        T3kTone t;
        auto* o = v.getDynamicObject();
        if (o == nullptr) return t;

        t.id             = (int) o->getProperty ("id");
        t.title          = o->getProperty ("title").toString();
        t.gear           = o->getProperty ("gear").toString();
        t.format         = o->getProperty ("format").toString();
        t.url            = o->getProperty ("url").toString();
        t.description    = o->getProperty ("description").toString();
        t.modelsCount    = (int) o->getProperty ("models_count");
        t.downloadsCount = (int) o->getProperty ("downloads_count");
        t.favoritesCount = (int) o->getProperty ("favorites_count");
        t.isFavorite     = (bool) o->getProperty ("is_favorite");

        if (auto* user = o->getProperty ("user").getDynamicObject())
            t.author = user->getProperty ("username").toString();

        // images enthält vollständige URLs, verkleinerte Varianten gibt es
        // nicht.
        if (auto* imgs = o->getProperty ("images").getArray())
            if (! imgs->isEmpty()) t.imageUrl = (*imgs)[0].toString();

        // Alte Schreibweise vereinheitlichen.
        if (t.gear == "full-rig") t.gear = "amp-cab";

        t.sizes = varToStrings (o->getProperty ("sizes"));
        t.makes = varToStrings (o->getProperty ("makes"));
        t.tags  = varToStrings (o->getProperty ("tags"));
        return t;
    }
}

const char* t3kCategoryName (T3kCategory c)
{
    switch (c)
    {
        case T3kCategory::all:      return "ALL";
        case T3kCategory::amp:      return "AMP";
        case T3kCategory::fullrig:  return "FULLRIG";
        case T3kCategory::ir:       return "IR";
        case T3kCategory::pedals:   return "PEDALS";
        case T3kCategory::outboard: return "OUTBOARD";
        case T3kCategory::space:    return "SPACES";
        default:                    return "ALL";
    }
}

const char* t3kCategoryColour (T3kCategory c)
{
    // Farbe nach Ziel: Amp und Full Rig rot, IR gelb, Pedals grün, Outboard
    // violett.
    switch (c)
    {
        case T3kCategory::amp:
        case T3kCategory::fullrig:  return "amp";
        case T3kCategory::ir:       return "cab";
        case T3kCategory::pedals:   return "pedal";
        case T3kCategory::outboard: return "outboard";
        case T3kCategory::space:    return "space";
        case T3kCategory::all:
        default:                    return "neutral";
    }
}

namespace
{
    /// Redirect-URL aus plugin/CMakeLists.txt, damit Code und Info.plist
    /// übereinstimmen.
   #ifndef NAMPLIFY_T3K_REDIRECT
    #define NAMPLIFY_T3K_REDIRECT "namplify://t3k-callback"
   #endif
    const char* const kRedirectUri = NAMPLIFY_T3K_REDIRECT;

    juce::File loginHandoffFile (Settings& s)
    {
        return s.directory().getChildFile ("t3k-login.json");
    }

    /// Teile einer Rückkehr aus namplify://t3k-callback?code=…
    struct CallbackParts
    {
        juce::String code, state, error;
        bool canceled = false;   ///< im Browser abgebrochen
    };

    CallbackParts parseCallback (const juce::String& url)
    {
        CallbackParts out;

        const auto query = url.fromFirstOccurrenceOf ("?", false, false);
        for (const auto& pair : juce::StringArray::fromTokens (query, "&", ""))
        {
            const auto key   = juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false));
            const auto value = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));

            if (key == "code")  out.code  = value;
            if (key == "state") out.state = value;
            if (key == "error") out.error = value;
            if (key == "canceled") out.canceled = value == "true";
        }

        return out;
    }

    /// Alle Tone3000-Instanzen dieses Prozesses. Die Rückkehr kommt beim
    /// Programm an und muss die wartende Instanz finden.
    std::mutex& instanceLock()
    {
        static std::mutex m;
        return m;
    }

    std::vector<Tone3000*>& instances()
    {
        static std::vector<Tone3000*> v;
        return v;
    }

    /// Wird das Standalone durch die Rückkehr gestartet, kommt das Apple-Event
    /// vor der ersten Instanz. Die Rückkehr wartet dann hier.
    juce::String& parkedCallback()
    {
        static juce::String s;
        return s;
    }
}

struct Tone3000::Impl : private juce::Timer
{
    explicit Impl (Settings& s) : settings (s)
    {
        pool = std::make_unique<juce::ThreadPool> (
            juce::ThreadPoolOptions{}.withNumberOfThreads (3)
                                     .withThreadName ("TONE3000"));
    }

    ~Impl() override
    {
        stopTimer();
        pool->removeAllJobs (true, 3000);
    }

    Settings& settings;
    std::unique_ptr<juce::ThreadPool> pool;

    /// Laufende Anmeldung, wartet auf die Rückkehr über das Betriebssystem.
    struct PendingLogin
    {
        juce::String state, verifier;
        juce::WaitableEvent arrived { true };   ///< manuell zurückzusetzen
        bool ok = false;
        juce::String error;
    };

    std::mutex loginLock;
    std::shared_ptr<PendingLogin> pending;

    /// Tauscht den Code gegen die Tokens und speichert die Sitzung. Läuft im
    /// Netzwerk-Thread.
    void finishLogin (const CallbackParts&, const juce::String& verifier,
                      std::shared_ptr<PendingLogin>);

    std::mutex tokenLock;
    juce::String accessToken;
    juce::int64  expiresAt = 0;

    /// Immer nur ein Refresh gleichzeitig. TONE3000 tauscht dabei das ganze
    /// Paar, der alte Refresh-Token gilt danach nicht mehr. Zwei parallele
    /// Anfragen mit abgelaufenem Token würden sonst denselben Refresh-Token
    /// schicken, und die zweite würde abgelehnt.
    std::mutex refreshLock;

    // Rate-Limit
    std::mutex rateLock;
    std::deque<juce::int64> recentCalls;
    juce::int64 blockedUntil = 0;

    void timerCallback() override {}

    /// Wartet, bis wieder eine Anfrage erlaubt ist. Limit 100 pro Minute,
    /// gebremst wird ab 90.
    void throttle()
    {
        for (;;)
        {
            juce::int64 waitMs = 0;
            {
                std::lock_guard<std::mutex> lock (rateLock);
                const auto now = juce::Time::currentTimeMillis();

                while (! recentCalls.empty() && now - recentCalls.front() > 60000)
                    recentCalls.pop_front();

                if (blockedUntil > now)
                    waitMs = blockedUntil - now;
                else if ((int) recentCalls.size() >= 90)
                    waitMs = 60000 - (now - recentCalls.front()) + 50;
                else
                    { recentCalls.push_back (now); return; }
            }
            juce::Thread::sleep ((int) juce::jlimit<juce::int64> (10, 2000, waitMs));
        }
    }

    void noteRateLimited (int retryAfterSeconds)
    {
        std::lock_guard<std::mutex> lock (rateLock);
        const auto wait = retryAfterSeconds > 0 ? retryAfterSeconds : 5;
        blockedUntil = juce::Time::currentTimeMillis() + wait * 1000;
    }

    bool haveFreshToken (juce::String& token)
    {
        std::lock_guard<std::mutex> lock (tokenLock);
        // 60 Sekunden vor Ablauf erneuern.
        if (accessToken.isEmpty() || juce::Time::currentTimeMillis() >= expiresAt - 60000)
            return false;
        token = accessToken;
        return true;
    }

    juce::String ensureAccessToken (juce::String& error)
    {
        juce::String token;
        if (haveFreshToken (token)) return token;

        std::lock_guard<std::mutex> once (refreshLock);

        // Nach dem Warten ist der Token eventuell schon erneuert.
        if (haveFreshToken (token)) return token;

        // Die Sitzungsdatei teilen sich Standalone und alle Plugin-Instanzen.
        // Hat ein anderer Prozess inzwischen erneuert, steht dort schon das
        // neue Paar.
        settings.reloadT3kSession();
        auto refresh = settings.t3kRefreshToken();
        if (refresh.isEmpty()) { error = "Not signed in."; return {}; }

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            juce::String body = "grant_type=refresh_token&refresh_token="
                              + juce::URL::addEscapeChars (refresh, false)
                              + "&client_id=" + juce::URL::addEscapeChars (Tone3000::clientId(), false);

            int status = 0;
            const auto reply = postForm ("/api/v1/oauth/token", body, status, error);

            if (status == 200)
            {
                const auto json = juce::JSON::parse (reply);
                auto* o = json.getDynamicObject();
                if (o == nullptr || o->getProperty ("access_token").toString().isEmpty())
                {
                    error = "Unreadable answer while refreshing the session.";
                    return {};
                }

                const auto newRefresh = o->getProperty ("refresh_token").toString();
                if (newRefresh.isNotEmpty())
                    settings.setT3kSession (newRefresh, settings.t3kUserName());

                std::lock_guard<std::mutex> lock (tokenLock);
                accessToken = o->getProperty ("access_token").toString();
                expiresAt   = juce::Time::currentTimeMillis()
                            + (juce::int64) ((double) o->getProperty ("expires_in") * 1000.0);
                return accessToken;
            }

            // Nur eine Ablehnung beendet die Sitzung. Netzfehler, Rate-Limit
            // oder Serverfehler sagen nichts über den Token.
            if (status != 400 && status != 401)
            {
                if (error.isEmpty())
                    error = "TONE3000 could not refresh the session (" + juce::String (status) + ").";
                return {};
            }

            // Abgelehnt. War der Token nur veraltet, weil ein anderer Prozess
            // gerade getauscht hat, gilt dessen neues Paar.
            if (settings.reloadT3kSession()
                && settings.t3kRefreshToken().isNotEmpty()
                && settings.t3kRefreshToken() != refresh)
            {
                refresh = settings.t3kRefreshToken();
                error.clear();
                continue;
            }
            break;
        }

        settings.clearT3kSession();
        error = "The TONE3000 session has expired. Please sign in again.";
        return {};
    }

    juce::String postForm (const juce::String& path, const juce::String& body,
                           int& status, juce::String& error)
    {
        throttle();

        juce::URL url (kApiBase + path);
        url = url.withPOSTData (body);

        juce::StringPairArray responseHeaders;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders ("Content-Type: application/x-www-form-urlencoded")
                           .withConnectionTimeoutMs (15000)
                           .withResponseHeaders (&responseHeaders)
                           .withStatusCode (&status);

        auto stream = url.createInputStream (options);
        if (stream == nullptr) { error = "Not connected to TONE3000."; return {}; }

        const auto text = stream->readEntireStreamAsString();
        if (status == 429)
        {
            noteRateLimited (responseHeaders["Retry-After"].getIntValue());
            error = "TONE3000 is rate limiting. Try again in a moment.";
        }
        return text;
    }

    /// Authentifizierte Anfrage, bei 401 mit erzwungenem Refresh.
    juce::String apiGet (const juce::String& path, int& status, juce::String& error)
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const auto token = ensureAccessToken (error);
            if (token.isEmpty()) return {};

            throttle();

            juce::StringPairArray headers;
            auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                               .withExtraHeaders ("Authorization: Bearer " + token)
                               .withConnectionTimeoutMs (20000)
                               .withResponseHeaders (&headers)
                               .withStatusCode (&status);

            auto stream = juce::URL (kApiBase + path).createInputStream (options);
            if (stream == nullptr) { error = "Not connected to TONE3000."; return {}; }

            const auto text = stream->readEntireStreamAsString();

            if (status == 401 && attempt == 0)
            {
                std::lock_guard<std::mutex> lock (tokenLock);
                expiresAt = 0;      // erzwingt den Refresh beim nächsten Versuch
                continue;
            }

            if (status == 429)
            {
                noteRateLimited (headers["Retry-After"].getIntValue());
                error = "TONE3000 is rate limiting. Try again in a moment.";
                return {};
            }

            if (status < 200 || status >= 300)
            {
                error = "TONE3000 answered with " + juce::String (status) + ".";
                return {};
            }

            return text;
        }

        return {};
    }

    bool apiPutOrDelete (const juce::String& path, bool isDelete, juce::String& error)
    {
        const auto token = ensureAccessToken (error);
        if (token.isEmpty()) return false;

        throttle();

        int status = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders ("Authorization: Bearer " + token)
                           .withConnectionTimeoutMs (15000)
                           .withStatusCode (&status)
                           .withHttpRequestCmd (isDelete ? "DELETE" : "PUT");

        auto stream = juce::URL (kApiBase + path).createInputStream (options);
        if (stream == nullptr) { error = "Not connected to TONE3000."; return false; }
        stream->readEntireStreamAsString();

        if (status < 200 || status >= 300)
        {
            error = "TONE3000 answered with " + juce::String (status) + ".";
            return false;
        }
        return true;
    }
};

Tone3000::Tone3000 (Settings& s) : impl (std::make_unique<Impl> (s))
{
    // Die Rückkehr aus dem Browser kommt beim Programm an, deshalb registriert
    // sich jede Instanz.
    juce::String parked;
    {
        std::lock_guard<std::mutex> lock (instanceLock());
        instances().push_back (this);
        std::swap (parked, parkedCallback());
    }

    if (parked.isNotEmpty())
        juce::MessageManager::callAsync ([parked] { Tone3000::deliverCallback (parked); });
}

Tone3000::~Tone3000()
{
    std::lock_guard<std::mutex> lock (instanceLock());
    auto& all = instances();
    all.erase (std::remove (all.begin(), all.end(), this), all.end());
}

bool Tone3000::hasClientId()
{
#ifdef NAMPLIFY_T3K_CLIENT_ID
    return juce::String (NAMPLIFY_T3K_CLIENT_ID).isNotEmpty();
#else
    return false;
#endif
}

juce::String Tone3000::clientId()
{
#ifdef NAMPLIFY_T3K_CLIENT_ID
    return juce::String (NAMPLIFY_T3K_CLIENT_ID);
#else
    return {};
#endif
}

bool Tone3000::isConnected() const { return impl->settings.t3kRefreshToken().isNotEmpty(); }
juce::String Tone3000::userName() const { return impl->settings.t3kUserName(); }

void Tone3000::disconnect()
{
    {
        std::lock_guard<std::mutex> lock (impl->tokenLock);
        impl->accessToken.clear();
        impl->expiresAt = 0;
    }
    impl->settings.clearT3kSession();
}

void Tone3000::cancelAll() { impl->pool->removeAllJobs (true, 2000); }


// Anmeldung.
//
// Redirect über das eigene URL-Schema namplify://t3k-callback (bei TONE3000
// eingetragen). Mit http://127.0.0.1:<port> erschien die Anmeldeseite nicht.
//
// Das Betriebssystem stellt die Rückkehr zu. Unter macOS trägt das Standalone
// das Schema in seiner Info.plist ein (CFBundleURLTypes, siehe
// plugin/CMakeLists.txt), die Rückkehr kommt als Apple-Event über
// NamplifyStandaloneApp::anotherInstanceStarted.
//
// Als Plugin in einem Host kann der Prozess kein URL-Schema registrieren, die
// Rückkehr landet im Standalone. Deshalb legt jede Anmeldung state und
// code_verifier in einer Datei neben den Einstellungen ab. Wer die Rückkehr
// bekommt, schließt den Tausch ab, und das Ergebnis steht in den gemeinsamen
// Einstellungen, die das Plugin beim Warten liest.

bool Tone3000::isCallbackUrl (const juce::String& url)
{
    return url.unquoted().trim().startsWithIgnoreCase (kRedirectUri);
}

juce::String Tone3000::redirectUri() { return kRedirectUri; }

void Tone3000::connect (std::function<void (bool, juce::String)> done)
{
    if (! hasClientId())
    {
        // Ohne Schlüssel den Grund anzeigen.
        if (done) done (false, "This build carries no TONE3000 key. "
                               "Rebuild with -DNAMPLIFY_T3K_CLIENT_ID=t3k_pub_….");
        return;
    }

    auto* p = impl.get();

    const auto verifier  = randomBase64Url (32);
    const auto challenge = sha256Base64Url (verifier);
    const auto state     = randomBase64Url (16);

    auto login = std::make_shared<Impl::PendingLogin>();
    login->state    = state;
    login->verifier = verifier;

    {
        std::lock_guard<std::mutex> lock (p->loginLock);
        p->pending = login;
    }

    // PKCE-Daten für den Fall, dass ein anderer Prozess die Rückkehr bekommt.
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        o->setProperty ("state", state);
        o->setProperty ("verifier", verifier);
        o->setProperty ("started", juce::Time::currentTimeMillis());
        loginHandoffFile (p->settings).replaceWithText (juce::JSON::toString (juce::var (o.get())));
    }

    // Bisheriger Token, daran erkennt der Wartende, dass ein anderer Prozess
    // fertig ist.
    const auto tokenBefore = p->settings.t3kRefreshToken();

    juce::URL authorize (kApiBase + "/api/v1/oauth/authorize");
    authorize = authorize.withParameter ("client_id", Tone3000::clientId())
                         .withParameter ("redirect_uri", kRedirectUri)
                         .withParameter ("response_type", "code")
                         .withParameter ("code_challenge", challenge)
                         .withParameter ("code_challenge_method", "S256")
                         .withParameter ("state", state);

    authorize.launchInDefaultBrowser();

    p->pool->addJob ([p, done, login, tokenBefore]
    {
        // Drei Minuten.
        const auto deadline = juce::Time::currentTimeMillis() + 180000;
        bool ok = false;
        juce::String error;

        for (;;)
        {
            if (login->arrived.wait (250))
            {
                ok = login->ok;
                error = login->error;
                break;
            }

            // Hat das Standalone den Tausch erledigt?
            if (p->settings.reloadT3kSession()
                && p->settings.t3kRefreshToken().isNotEmpty()
                && p->settings.t3kRefreshToken() != tokenBefore)
            {
                ok = true;
                break;
            }

            if (juce::Time::currentTimeMillis() > deadline)
            {
                error = "The sign-in did not come back.";
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock (p->loginLock);
            if (p->pending == login) p->pending.reset();
        }
        loginHandoffFile (p->settings).deleteFile();

        juce::MessageManager::callAsync ([done, ok, error]
        { if (done) done (ok, error); });
    });
}

bool Tone3000::deliverCallback (const juce::String& rawUrl)
{
    const auto url = rawUrl.unquoted().trim();
    if (! isCallbackUrl (url)) return false;

    const auto parts = parseCallback (url);

    std::lock_guard<std::mutex> lock (instanceLock());

    if (instances().empty())
    {
        parkedCallback() = url;
        return true;
    }

    // 1. Wartet hier eine Instanz mit genau diesem state?
    for (auto* t : instances())
    {
        auto* p = t->impl.get();
        std::shared_ptr<Impl::PendingLogin> login;
        {
            std::lock_guard<std::mutex> l (p->loginLock);
            login = p->pending;
        }

        if (login != nullptr && login->state == parts.state)
        {
            p->finishLogin (parts, login->verifier, login);
            return true;
        }
    }

    // 2. Sonst wartet ein anderer Prozess (z. B. ein Plugin). Mit seinen
    // abgelegten PKCE-Daten wird der Tausch hier abgeschlossen.
    auto* t = instances().front();
    auto* p = t->impl.get();

    const auto handoff = juce::JSON::parse (loginHandoffFile (p->settings));
    if (auto* o = handoff.getDynamicObject())
    {
        if (o->getProperty ("state").toString() == parts.state)
        {
            p->finishLogin (parts, o->getProperty ("verifier").toString(), nullptr);
            return true;
        }
    }

    return false;
}

void Tone3000::Impl::finishLogin (const CallbackParts& parts, const juce::String& verifier,
                                  std::shared_ptr<PendingLogin> login)
{
    auto report = [login] (bool ok, juce::String error)
    {
        if (login == nullptr) return;
        login->ok = ok;
        login->error = std::move (error);
        login->arrived.signal();
    };

    // Im Browser abgebrochen ist kein Fehler, der Browser geht zurück in den
    // Ausgangszustand.
    if (parts.canceled && parts.code.isEmpty()) { report (false, "canceled"); return; }
    if (parts.error.isNotEmpty()) { report (false, "TONE3000 refused: " + parts.error); return; }
    if (parts.code.isEmpty())     { report (false, "The sign-in came back without a code."); return; }

    auto* p = this;

    pool->addJob ([p, parts, verifier, report]
    {
        // Token-Tausch
        juce::String body = "grant_type=authorization_code"
                            "&code=" + juce::URL::addEscapeChars (parts.code, false)
                          + "&code_verifier=" + juce::URL::addEscapeChars (verifier, false)
                          + "&redirect_uri=" + juce::URL::addEscapeChars (kRedirectUri, false)
                          + "&client_id=" + juce::URL::addEscapeChars (Tone3000::clientId(), false);

        int status = 0;
        juce::String error;
        const auto reply = p->postForm ("/api/v1/oauth/token", body, status, error);

        if (status != 200)
        {
            report (false, error.isNotEmpty()
                               ? error
                               : "The key exchange failed (" + juce::String (status) + ").");
            return;
        }

        const auto json = juce::JSON::parse (reply);
        auto* o = json.getDynamicObject();
        if (o == nullptr) { report (false, "Unreadable answer while signing in."); return; }

        {
            std::lock_guard<std::mutex> lock (p->tokenLock);
            p->accessToken = o->getProperty ("access_token").toString();
            p->expiresAt = juce::Time::currentTimeMillis()
                         + (juce::int64) ((double) o->getProperty ("expires_in") * 1000.0);
        }

        const auto refresh = o->getProperty ("refresh_token").toString();

        // Angemeldeter Benutzer, wird am Tab angezeigt.
        juce::String who;
        {
            int st = 0;
            juce::String err;
            const auto userJson = p->apiGet ("/api/v1/user", st, err);
            const auto user = juce::JSON::parse (userJson);   // hält das Objekt am Leben
            if (auto* u = user.getDynamicObject())
                who = u->getProperty ("username").toString();
        }

        p->settings.setT3kSession (refresh, who);
        loginHandoffFile (p->settings).deleteFile();

        report (true, {});
    });
}


void Tone3000::search (const T3kQuery& q, std::function<void (bool, T3kPage, juce::String)> done)
{
    auto* p = impl.get();

    p->pool->addJob ([p, q, done]
    {
        juce::String gears, format;
        applyCategory (q.category, gears, format);

        juce::String path;

        // SAVED: heruntergeladene Tones (/tones/downloaded), nicht selbst
        // veröffentlichte (/tones/created).
        if (q.tab == T3kTab::saved)           path = "/api/v1/tones/downloaded?";
        else if (q.tab == T3kTab::favourites) path = "/api/v1/tones/favorited?";
        else                                  path = "/api/v1/tones/search?";

        juce::StringArray params;
        params.add ("page=" + juce::String (juce::jmax (1, q.page)));
        params.add ("page_size=25");

        if (q.tab == T3kTab::browse)
        {
            if (q.text.isNotEmpty())
                params.add ("query=" + juce::URL::addEscapeChars (q.text, false));
            params.add ("sort=" + q.sort);
            // gears/sizes/tags/makes werden mit Unterstrich getrennt, creators
            // mit Komma. Hier ist immer nur eine Kategorie aktiv.
            if (gears.isNotEmpty())  params.add ("gears=" + gears);
            if (format.isNotEmpty()) params.add ("format=" + format);
            // Nur A2. Der Filter gilt nur für NAM, IRs kommen trotzdem. Im
            // reinen IR-Tab entfällt er.
            if (format != "ir") params.add ("architecture=2");
        }
        else
        {
            // created/favorited kennen gear (Einzahl), nicht gears.
            if (gears.isNotEmpty()) params.add ("gear=" + gears);
        }

        path += params.joinIntoString ("&");

        int status = 0;
        juce::String error;
        const auto reply = p->apiGet (path, status, error);

        T3kPage page;
        bool ok = false;

        if (error.isEmpty())
        {
            const auto json = juce::JSON::parse (reply);
            if (auto* o = json.getDynamicObject())
            {
                page.page       = (int) o->getProperty ("page");
                page.pageSize   = (int) o->getProperty ("page_size");
                page.total      = (int) o->getProperty ("total");
                page.totalPages = juce::jmax (1, (int) o->getProperty ("total_pages"));

                if (auto* arr = o->getProperty ("data").getArray())
                    for (const auto& item : *arr)
                    {
                        auto tone = parseTone (item);
                        if (fitsCategory (tone, q.category)) page.tones.push_back (std::move (tone));
                    }

                ok = true;
            }
            else
            {
                error = "Unreadable answer from TONE3000.";
            }
        }

        juce::MessageManager::callAsync ([done, ok, page, error]
        { if (done) done (ok, page, error); });
    });
}

void Tone3000::listModels (int toneId, bool nam,
                           std::function<void (bool, std::vector<T3kModel>, juce::String)> done)
{
    auto* p = impl.get();

    p->pool->addJob ([p, toneId, nam, done]
    {
        // Eine Bank kann A1- und A2-Varianten haben, geholt wird nur A2. Bei
        // IR-Bänken entfällt der Filter.
        int status = 0;
        juce::String error;
        const auto reply = p->apiGet ("/api/v1/models?tone_id=" + juce::String (toneId)
                                      + "&page=1&page_size=300"
                                      + (nam ? "&architecture=2" : ""), status, error);

        std::vector<T3kModel> models;
        bool ok = false;

        if (error.isEmpty())
        {
            const auto json = juce::JSON::parse (reply);
            if (auto* o = json.getDynamicObject())
            {
                if (auto* arr = o->getProperty ("data").getArray())
                    for (const auto& item : *arr)
                        if (auto* m = item.getDynamicObject())
                        {
                            T3kModel model;
                            model.id           = (int) m->getProperty ("id");
                            model.toneId       = (int) m->getProperty ("tone_id");
                            model.name         = m->getProperty ("name").toString();
                            model.size         = m->getProperty ("size").toString();
                            model.architecture = m->getProperty ("architecture_version").toString();
                            model.url          = m->getProperty ("model_url").toString();
                            models.push_back (model);
                        }
                ok = true;
            }
            else error = "Unreadable answer from TONE3000.";
        }

        juce::MessageManager::callAsync ([done, ok, models, error]
        { if (done) done (ok, models, error); });
    });
}

void Tone3000::setFavourite (int toneId, bool on, std::function<void (bool, juce::String)> done)
{
    auto* p = impl.get();
    p->pool->addJob ([p, toneId, on, done]
    {
        juce::String error;
        const bool ok = p->apiPutOrDelete ("/api/v1/tones/" + juce::String (toneId) + "/favorite",
                                           ! on, error);
        juce::MessageManager::callAsync ([done, ok, error] { if (done) done (ok, error); });
    });
}

void Tone3000::download (const T3kModel& model,
                         std::function<void (bool, juce::MemoryBlock, juce::String)> done,
                         std::function<void (float)> progress)
{
    auto* p = impl.get();

    p->pool->addJob ([p, model, done, progress]
    {
        juce::String error;
        const auto token = p->ensureAccessToken (error);

        if (token.isEmpty())
        {
            juce::MessageManager::callAsync ([done, error]
            { if (done) done (false, {}, error); });
            return;
        }

        p->throttle();

        // model_url braucht denselben Bearer-Header wie die API.
        int status = 0;
        juce::StringPairArray headers;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders ("Authorization: Bearer " + token)
                           .withConnectionTimeoutMs (30000)
                           .withResponseHeaders (&headers)
                           .withStatusCode (&status);

        auto stream = juce::URL (model.url).createInputStream (options);

        if (stream == nullptr || status < 200 || status >= 300)
        {
            const juce::String msg = stream == nullptr
                                   ? juce::String ("Not connected to TONE3000.")
                                   : "TONE3000 answered with " + juce::String (status) + ".";
            juce::MessageManager::callAsync ([done, msg] { if (done) done (false, {}, msg); });
            return;
        }

        const auto total = stream->getTotalLength();

        juce::MemoryBlock data;
        juce::MemoryOutputStream out (data, false);

        char buffer[16384];
        juce::int64 got = 0;

        // Der Timeout oben gilt nur für den Verbindungsaufbau. Eigene Frist,
        // damit eine hängende Antwort keinen Netzwerk-Thread blockiert.
        const auto deadline = juce::Time::getMillisecondCounter() + 120000;
        bool timedOut = false;

        for (;;)
        {
            if (juce::Time::getMillisecondCounter() > deadline) { timedOut = true; break; }

            const int read = stream->read (buffer, sizeof buffer);
            if (read <= 0) break;
            out.write (buffer, (size_t) read);
            got += read;

            if (progress)
            {
                // −1, wenn die Größe unbekannt ist.
                const float v = total > 0 ? (float) ((double) got / (double) total) : -1.0f;
                juce::MessageManager::callAsync ([progress, v] { progress (v); });
            }
        }

        out.flush();

        if (timedOut)
        {
            juce::MessageManager::callAsync ([done]
            { if (done) done (false, {}, "The download stalled."); });
            return;
        }

        if (data.getSize() == 0)
        {
            juce::MessageManager::callAsync ([done]
            { if (done) done (false, {}, "The file arrived empty."); });
            return;
        }

        juce::MessageManager::callAsync ([done, data] { if (done) done (true, data, {}); });
    });
}

void Tone3000::fetchImage (const juce::String& url,
                           std::function<void (bool, juce::MemoryBlock, juce::String)> done)
{
    if (url.isEmpty()) { if (done) done (false, {}, "Kein Bild."); return; }

    auto* p = impl.get();
    p->pool->addJob ([url, done]
    {
        // Bilder liegen bei Supabase und brauchen keinen Header.
        int status = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs (20000)
                           .withStatusCode (&status);

        auto stream = juce::URL (url).createInputStream (options);
        if (stream == nullptr || status < 200 || status >= 300)
        {
            juce::MessageManager::callAsync ([done] { if (done) done (false, {}, "Image could not be reached."); });
            return;
        }

        juce::MemoryBlock data;
        stream->readIntoMemoryBlock (data);

        juce::MessageManager::callAsync ([done, data] { if (done) done (data.getSize() > 0, data, {}); });
    });
}

} // namespace nmpl
