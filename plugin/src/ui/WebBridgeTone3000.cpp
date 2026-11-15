// TONE3000-Browser, Teil 3 der Brücke. Es lädt immer nur eine Zeile
// gleichzeitig, damit sich zwei schnelle Klicks nicht gegenseitig die Datei
// wegnehmen.

#include "WebBridge.h"
#include <namplify/model/WavFile.h>
#include <vector>

namespace nmpl
{

namespace
{
    juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> items)
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        for (const auto& item : items) o->setProperty (item.first, item.second);
        return juce::var (o.get());
    }

    juce::var get (const juce::var& v, const char* key)
    {
        if (auto* o = v.getDynamicObject())
            if (o->hasProperty (key)) return o->getProperty (key);
        return {};
    }

    juce::String getString (const juce::var& v, const char* key) { return get (v, key).toString(); }
    double getNumber (const juce::var& v, const char* key) { return (double) get (v, key); }
    bool   getBool (const juce::var& v, const char* key) { return (bool) get (v, key); }

    juce::var ok (juce::var payload = {})
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        o->setProperty ("ok", true);
        if (! payload.isVoid()) o->setProperty ("data", payload);
        return juce::var (o.get());
    }

    juce::var fail (const juce::String& message)
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        o->setProperty ("ok", false);
        o->setProperty ("error", message);
        return juce::var (o.get());
    }

    /// Ziel einer Kategorie: Typ und Rolle des Slots, der sie aufnimmt.
    nmp::SlotType typeForGear (const juce::String& gear, const juce::String& format)
    {
        // Ein Raum kommt immer auf einen NAM-Slot mit der Rolle SPACE, als
        // Capture oder als IR (wie OUTBOARD).
        const auto g = gear.toLowerCase();
        if (g == "space") return nmp::SlotType::nam;
        if (format == "ir") return nmp::SlotType::ir;
        return g == "cab" ? nmp::SlotType::ir : nmp::SlotType::nam;
    }

    juce::var toneToVar (const T3kTone& t, const Library& lib)
    {
        juce::Array<juce::var> sizes;
        for (const auto& s : t.sizes) sizes.add (s);

        const bool here = lib.hasTone (t.id);

        return makeObject ({
            { "id", t.id },
            { "title", t.title },
            { "gear", t.gear },
            { "format", t.format },
            { "author", t.author },
            { "image", t.imageUrl },
            { "models", t.modelsCount },
            { "downloads", t.downloadsCount },
            { "favorites", t.favoritesCount },
            { "isFavorite", t.isFavorite },
            { "sizes", sizes },
            { "url", t.url },
            { "type", juce::String (nmp::slotTypeKey (typeForGear (t.gear, t.format))) },
            { "role", juce::String (nmp::namRoleKey (roleForGear (t.gear))) },
            { "here", here } });
    }
}

void WebBridge::storeBankImage (const juce::File& target, int modelId, const juce::String& imageUrl)
{
    // Bild der Bank als Datei ablegen, damit die Karte es auch offline zeigt.
    // Hat die Bank schon eins, bleibt es. War das Capture angespielt, wird sein
    // Bild aus dem Audition-Ordner übernommen.
    auto& lib = proc.library();
    if (lib.imageForFile (target) != juce::File{}) return;

    const auto bankDir = target.getParentDirectory();
    const auto auditionArt = lib.root().getChildFile ("Audition")
                                 .getChildFile ("t3k-" + juce::String (modelId) + "-art.jpg");
    juce::MemoryBlock art;

    if (auditionArt.existsAsFile() && auditionArt.loadFileAsData (art) && art.getSize() > 0)
    {
        lib.storeImage (bankDir, art, ".jpg");
    }
    else if (imageUrl.isNotEmpty())
    {
        proc.tone3000().fetchImage (imageUrl,
            [this, bankDir] (bool okImage, juce::MemoryBlock image, juce::String)
            {
                if (! okImage) return;
                proc.library().storeImage (bankDir, image, ".jpg");
                proc.bumpStructure();
            });
    }
}

bool WebBridge::isPreview (int slot) const
{
    if (slot < 0 || slot >= nmp::kNumSlots) return false;
    const auto& f = proc.rig().files[(size_t) slot];
    const auto& root = proc.library().root();
    return ! f.isEmpty() && root.getChildFile (f.relativePath).isAChildOf (root.getChildFile ("Audition"));
}

juce::var WebBridge::keepPreview (int slot, juce::String& error)
{
    // Angespieltes Capture in die Bibliothek übernehmen, ohne zweiten Download
    // und ohne neu zu laden. Nur der Slot zeigt danach auf die Datei in der
    // Bibliothek, Regler und Klang bleiben unverändert.
    if (! isPreview (slot)) { error = "This slot holds no preview."; return {}; }

    auto& lib = proc.library();
    auto& f = proc.rig().files[(size_t) slot];
    const auto file = lib.root().getChildFile (f.relativePath);

    const auto side = juce::JSON::parse (file.getSiblingFile (file.getFileNameWithoutExtension() + ".t3k.json"));
    if (side.getDynamicObject() == nullptr)
    { error = "This preview is from an older version. Get it from the TONE3000 list instead."; return {}; }

    juce::MemoryBlock data;
    if (! file.existsAsFile() || ! file.loadFileAsData (data) || data.getSize() == 0)
    { error = "The preview file could not be read."; return {}; }

    const int toneId  = (int) getNumber (side, "toneId");
    const int modelId = (int) getNumber (side, "modelId");
    const auto gear   = getString (side, "gear");

    const auto target = lib.storeDownload (Library::folderForGear (gear), getString (side, "toneTitle"),
                                           toneId, getString (side, "modelName"), modelId,
                                           getString (side, "size"), gear, getString (side, "author"),
                                           getString (side, "page"), data,
                                           file.getFileExtension().toLowerCase(), error);
    if (target == juce::File{}) return {};

    storeBankImage (target, modelId, getString (side, "imageUrl"));
    f.relativePath = target.getRelativePathFrom (lib.root());

    return makeObject ({ { "slot", slot }, { "tone", toneId }, { "model", modelId } });
}

void WebBridge::t3kFinishDownload (int toneId, const juce::String& title, const juce::String& gear,
                                   const juce::String& author, const juce::String& page,
                                   const juce::String& imageUrl,
                                   const T3kModel& model, const juce::MemoryBlock& data,
                                   bool alsoLoad, bool storeInLibrary, int preferredSlot,
                                   Completion done)
{
    const bool isIr = model.url.containsIgnoreCase (".wav");
    const auto type = typeForGear (gear, isIr ? "ir" : "");
    const auto folder = Library::folderForGear (gear);

    // Endung aus der URL.
    juce::String extension = ".nam";
    {
        const auto path = juce::URL (model.url).getSubPath();
        const auto last = path.fromLastOccurrenceOf ("/", false, false);
        if (last.containsChar ('.'))
        {
            const auto e = "." + last.fromLastOccurrenceOf (".", false, false).toLowerCase();
            if (e == ".wav" || e == ".nam") extension = e;
        }
    }

    juce::File target;
    int slot = -1;

    if (storeInLibrary)
    {
        juce::String error;
        target = proc.library().storeDownload (folder, title, toneId, model.name, model.id,
                                               model.size, gear, author, page, data, extension, error);
        if (target == juce::File{})
        {
            t3kBusy.store (false);
            if (done) done (fail (error));
            return;
        }

        storeBankImage (target, model.id, imageUrl);
    }
    else
    {
        // Anspielen: die Datei kommt direkt ins passende Pedal, ohne in die
        // Bibliothek aufgenommen zu werden. Der Audition-Ordner liegt in der
        // Bibliothek, aber außerhalb von NAM/: die Bankliste sieht ihn nicht,
        // die Bildauslieferung (libImageUrl) schon.
        auto dir = proc.library().root().getChildFile ("Audition");
        dir.createDirectory();
        target = dir.getChildFile ("t3k-" + juce::String (model.id) + extension);
        target.replaceWithData (data.getData(), data.getSize());

        // Metadaten für STORE, damit das Capture später ohne zweiten Download
        // übernommen werden kann.
        juce::DynamicObject::Ptr side (new juce::DynamicObject());
        side->setProperty ("toneId", toneId);
        side->setProperty ("toneTitle", title);
        side->setProperty ("modelId", model.id);
        side->setProperty ("modelName", model.name);
        side->setProperty ("size", model.size);
        side->setProperty ("url", model.url);
        side->setProperty ("gear", gear);
        side->setProperty ("author", author);
        side->setProperty ("page", page);
        side->setProperty ("imageUrl", imageUrl);
        target.getSiblingFile (target.getFileNameWithoutExtension() + ".t3k.json")
            .replaceWithText (juce::JSON::toString (juce::var (side.get())));

        // Bild daneben als <name>-art.jpg, so findet es Library::imageForFile.
        // Liegt es schon da, wird es nicht noch einmal geladen.
        const auto art = target.getSiblingFile (target.getFileNameWithoutExtension() + "-art.jpg");
        if (imageUrl.isNotEmpty() && ! art.existsAsFile())
        {
            proc.tone3000().fetchImage (imageUrl,
                [this, art] (bool okImage, juce::MemoryBlock image, juce::String)
                {
                    if (! okImage) return;
                    art.replaceWithData (image.getData(), image.getSize());
                    proc.bumpStructure();
                });
        }
    }

    if (alsoLoad)
    {
        SlotFile info;
        info.relativePath = storeInLibrary
                              ? target.getRelativePathFrom (proc.library().root())
                              : target.getFullPathName();
        info.bankTitle   = title;
        info.variantName = model.name;
        info.gear        = gear == "full-rig" ? "amp-cab" : gear;

        // Angespieltes bleibt außerhalb der Bibliothek, der Ladeweg ist
        // derselbe.
        slot = proc.slotForFile (type, roleForGear (gear), preferredSlot);
        if (slot >= 0) proc.loadIntoSlot (slot, target, info);
    }

    proc.library().rescan();
    proc.bumpStructure();

    t3kBusy.store (false);
    if (done) done (ok (makeObject ({ { "path", target.getFullPathName() },
                                      { "slot", slot } })));
}

void WebBridge::cmdT3k (const juce::var& request, const juce::String& cmd, Completion done)
{
    auto& t3k = proc.tone3000();
    auto reply = [done] (juce::var v) { if (done) done (std::move (v)); };

    if (cmd == "t3k.state")
    {
        reply (ok (makeObject ({ { "hasKey", Tone3000::hasClientId() },
                                 { "connected", t3k.isConnected() },
                                 { "user", t3k.userName() },
                                 { "busy", t3kBusy.load() },
                                 { "busyTone", busyToneId },
                                 { "busyModel", busyModelId } })));
        return;
    }

    if (cmd == "t3k.connect")
    {
        t3k.connect ([this, done] (bool okConnect, juce::String error)
        {
            proc.bumpStructure();
            if (! done) return;
            if (okConnect) done (ok (makeObject ({ { "user", proc.tone3000().userName() } })));
            else           done (fail (error));
        });
        return;
    }

    if (cmd == "t3k.disconnect")
    {
        t3k.disconnect();
        proc.bumpStructure();
        reply (ok());
        return;
    }

    if (cmd == "t3k.search")
    {
        if (! t3k.isConnected())
        {
            reply (fail (Tone3000::hasClientId()
                             ? juce::String ("not-connected")
                             : juce::String ("no-key")));
            return;
        }

        T3kQuery q;
        q.tab      = (T3kTab) juce::jlimit (0, 2, (int) getNumber (request, "tab"));
        q.category = (T3kCategory) juce::jlimit (0, (int) T3kCategory::count - 1,
                                                 (int) getNumber (request, "category"));
        q.text     = getString (request, "text");
        q.page     = juce::jmax (1, (int) getNumber (request, "page"));

        const auto sort = getString (request, "sort");
        if (sort.isNotEmpty()) q.sort = sort;

        auto& lib = proc.library();

        t3k.search (q, [done, &lib] (bool okSearch, T3kPage page, juce::String error)
        {
            if (! done) return;
            if (! okSearch) { done (fail (error)); return; }

            juce::Array<juce::var> rows;
            for (const auto& t : page.tones) rows.add (toneToVar (t, lib));

            done (ok (makeObject ({ { "rows", rows },
                                    { "page", page.page },
                                    { "totalPages", page.totalPages },
                                    { "total", page.total } })));
        });
        return;
    }

    if (cmd == "t3k.models")
    {
        const int toneId = (int) getNumber (request, "tone");
        const bool nam = getString (request, "format") != "ir";
        auto& lib = proc.library();

        t3k.listModels (toneId, nam, [done, &lib] (bool okList, std::vector<T3kModel> models, juce::String error)
        {
            if (! done) return;
            if (! okList) { done (fail (error)); return; }

            juce::Array<juce::var> rows;
            for (const auto& m : models)
                rows.add (makeObject ({
                    { "id", m.id },
                    { "name", m.name },
                    { "size", m.size },
                    { "architecture", m.architecture },
                    // Liegt diese Variante schon in der Bibliothek? Pro Datei,
                    // nicht pro Bank.
                    { "here", lib.hasModel (m.id) } }));

            done (ok (makeObject ({ { "rows", rows } })));
        });
        return;
    }

    if (cmd == "t3k.favourite")
    {
        t3k.setFavourite ((int) getNumber (request, "tone"), getBool (request, "on"),
            [done] (bool okFav, juce::String error)
            { if (done) done (okFav ? ok() : fail (error)); });
        return;
    }

    if (cmd == "t3k.image")
    {
        const auto url = getString (request, "url");

        t3k.fetchImage (url, [done] (bool okImage, juce::MemoryBlock data, juce::String error)
        {
            if (! done) return;
            if (! okImage) { done (fail (error)); return; }

            const juce::String mime = data.getSize() > 2
                                    && ((const juce::uint8*) data.getData())[0] == 0x89
                                        ? "image/png" : "image/jpeg";

            done (ok (makeObject ({ { "uri", "data:" + mime + ";base64,"
                                            + juce::Base64::toBase64 (data.getData(), data.getSize()) } })));
        });
        return;
    }

    if (cmd == "t3k.page")
    {
        // Tone-Seite im Browser öffnen, nur auf TONE3000. Die URL kommt aus
        // einer Metadaten-Datei und wird deshalb geprüft.
        const auto page = getString (request, "page");
        if (! page.startsWith ("https://www.tone3000.com/") && ! page.startsWith ("https://tone3000.com/"))
        { reply (fail ("That is not a TONE3000 page.")); return; }
        juce::URL (page).launchInDefaultBrowser();
        reply (ok());
        return;
    }

    if (cmd == "t3k.delete")
    {
        // Klick auf das Kästchen löscht die ganze Bank, ohne Rückfrage.
        const int toneId = (int) getNumber (request, "tone");
        const auto* bank = proc.library().findBankByToneId (toneId);
        if (bank == nullptr) { reply (ok()); return; }

        juce::String error;
        const auto dir = bank->dir;
        if (! proc.library().deleteBank (dir, error)) { reply (fail (error)); return; }

        proc.bumpStructure();
        reply (ok());
        return;
    }

    if (cmd == "t3k.keep")
    {
        // STORE auf der Karte.
        juce::String error;
        const auto info = keepPreview (slotFrom (request), error);
        if (info.isVoid()) { reply (fail (error)); return; }
        proc.bumpStructure();
        reply (ok (info));
        return;
    }

    if (cmd == "t3k.get" || cmd == "t3k.audition")
    {
        // Eine Zeile zur Zeit.
        bool expected = false;
        if (! t3kBusy.compare_exchange_strong (expected, true))
        {
            reply (fail ("busy"));
            return;
        }

        const int toneId = (int) getNumber (request, "tone");
        const auto title = getString (request, "title");
        const auto gear  = getString (request, "gear");
        const bool nam   = getString (request, "format") != "ir";
        const auto author = getString (request, "author");
        const auto page  = getString (request, "page");
        const auto image = getString (request, "image");
        const int wantedModel = (int) getNumber (request, "model");
        const bool store = cmd == "t3k.get";
        // Der offene Slot, wenn er passt.
        const int preferred = slotFrom (request);

        busyToneId = toneId;
        busyModelId = wantedModel;

        auto* self = this;

        auto startDownload = [self, toneId, title, gear, author, page, image, store, preferred, done]
                             (const T3kModel& model)
        {
            self->busyModelId = model.id;

            self->proc.tone3000().download (model,
                [self, toneId, title, gear, author, page, image, model, store, preferred, done]
                (bool okDl, juce::MemoryBlock data, juce::String error)
                {
                    if (! okDl)
                    {
                        self->t3kBusy.store (false);
                        self->emit ("t3kProgress", makeObject ({ { "tone", toneId },
                                                                 { "model", model.id },
                                                                 { "done", true } }));
                        if (done) done (fail (error));
                        return;
                    }

                    self->t3kFinishDownload (toneId, title, gear, author, page, image,
                                             model, data, true, store, preferred, done);

                    self->emit ("t3kProgress", makeObject ({ { "tone", toneId },
                                                             { "model", model.id },
                                                             { "done", true } }));
                },
                [self, toneId, model] (float value)
                {
                    // Fortschritt nur, wenn die Größe bekannt ist.
                    self->emit ("t3kProgress", makeObject ({ { "tone", toneId },
                                                             { "model", model.id },
                                                             { "value", value },
                                                             { "done", false } }));
                });
        };

        if (wantedModel > 0)
        {
            // Bestimmte Variante, die URL kommt aus der Variantenliste.
            proc.tone3000().listModels (toneId, nam,
                [self, wantedModel, startDownload, done]
                (bool okList, std::vector<T3kModel> models, juce::String error)
                {
                    if (! okList)
                    {
                        self->t3kBusy.store (false);
                        if (done) done (fail (error));
                        return;
                    }

                    for (const auto& m : models)
                        if (m.id == wantedModel) { startDownload (m); return; }

                    self->t3kBusy.store (false);
                    if (done) done (fail ("That variant no longer exists."));
                });
        }
        else
        {
            // Ohne Angabe die erste Variante der Bank.
            proc.tone3000().listModels (toneId, nam,
                [self, startDownload, done]
                (bool okList, std::vector<T3kModel> models, juce::String error)
                {
                    if (! okList || models.empty())
                    {
                        self->t3kBusy.store (false);
                        if (done) done (fail (okList ? juce::String ("That bank is empty.") : error));
                        return;
                    }

                    // Gibt es eine "standard"-Variante, ist sie gemeint.
                    for (const auto& m : models)
                        if (m.size == "standard") { startDownload (m); return; }

                    startDownload (models.front());
                });
        }

        return;
    }

    reply (fail ("Unbekannter TONE3000-Befehl: " + cmd));
}

} // namespace nmpl
