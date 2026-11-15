#include "Library.h"
#include <NamplifyResources.h>
#include <algorithm>
#include <functional>
#include <vector>

namespace nmpl
{

const char* libraryFolderName (LibraryFolder f)
{
    switch (f)
    {
        case LibraryFolder::models:   return "Models";
        case LibraryFolder::irs:      return "IRs";
        case LibraryFolder::pedals:   return "Pedals";
        case LibraryFolder::outboard: return "Outboard";
        default:                      return "Models";
    }
}

Library::Library()
{
    rootDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                  .getChildFile ("NAMplify");

    for (int i = 0; i < (int) LibraryFolder::count; ++i)
        folder ((LibraryFolder) i).createDirectory();

    presetsDir().createDirectory();
    recordingsDir().createDirectory();
}

juce::File Library::folder (LibraryFolder f) const
{
    return rootDir.getChildFile ("NAM").getChildFile (libraryFolderName (f));
}

bool Library::isInsideLibrary (const juce::File& f) const
{
    // Gelöscht wird nur innerhalb dieses Ordners.
    return f.isAChildOf (rootDir);
}

juce::String Library::sanitiseName (const juce::String& s)
{
    auto out = s.retainCharacters ("abcdefghijklmnopqrstuvwxyz"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "0123456789 -_()&+.,'").trim();
    if (out.isEmpty()) out = "Untitled";
    return out.substring (0, 96);
}

void Library::rescan()
{
    for (int i = 0; i < (int) LibraryFolder::count; ++i)
        scanFolder ((LibraryFolder) i);
}

void Library::scanFolder (LibraryFolder f)
{
    auto& list = bankList[(int) f];
    list.clear();

    const auto dir = folder (f);
    if (! dir.isDirectory()) return;

    for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.namplify",
                                                            juce::File::findDirectories))
    {
        LibraryBank bank;
        bank.dir = entry.getFile();
        bank.folder = f;
        bank.title = bank.dir.getFileNameWithoutExtension();

        for (const auto& fileEntry : juce::RangedDirectoryIterator (bank.dir, false, "*",
                                                                    juce::File::findFiles))
        {
            const auto file = fileEntry.getFile();
            const auto ext = file.getFileExtension().toLowerCase();

            if (ext != ".nam" && ext != ".wav") continue;

            LibraryVariant v;
            v.file = file;
            v.name = file.getFileNameWithoutExtension();

            // Metadaten: gleicher Name plus .t3k.json
            const auto sidecar = bank.dir.getChildFile (file.getFileNameWithoutExtension() + ".t3k.json");
            if (sidecar.existsAsFile())
            {
                const auto json = juce::JSON::parse (sidecar);
                if (auto* o = json.getDynamicObject())
                {
                    if (o->hasProperty ("modelName")) v.name = o->getProperty ("modelName").toString();
                    if (o->hasProperty ("size"))      v.size = o->getProperty ("size").toString();
                    if (o->hasProperty ("modelId"))   v.modelId = (int) o->getProperty ("modelId");
                    if (o->hasProperty ("toneTitle")) bank.title = o->getProperty ("toneTitle").toString();
                    if (o->hasProperty ("author"))    bank.author = o->getProperty ("author").toString();
                    if (o->hasProperty ("gear"))      bank.gear = o->getProperty ("gear").toString();
                    if (o->hasProperty ("toneId"))    bank.toneId = (int) o->getProperty ("toneId");
                    if (o->hasProperty ("page"))      bank.page = o->getProperty ("page").toString();

                    // Dateiname statt URL.
                    if (o->hasProperty ("imageUrl"))
                    {
                        const auto img = o->getProperty ("imageUrl").toString();
                        if (img.isNotEmpty() && ! img.startsWithIgnoreCase ("http"))
                        {
                            const auto candidate = bank.dir.getChildFile (img);
                            if (candidate.existsAsFile()) bank.image = candidate;
                        }
                    }
                }
            }

            bank.variants.push_back (v);
        }

        if (bank.image == juce::File{})
        {
            for (const auto& imgEntry : juce::RangedDirectoryIterator (bank.dir, false, "*.jpg;*.jpeg;*.png",
                                                                       juce::File::findFiles))
            {
                bank.image = imgEntry.getFile();
                break;
            }
        }

        if (bank.gear.isEmpty())
        {
            switch (f)
            {
                case LibraryFolder::irs:      bank.gear = "cab"; break;
                case LibraryFolder::pedals:   bank.gear = "pedal"; break;
                case LibraryFolder::outboard: bank.gear = "outboard"; break;
                default:                      bank.gear = "amp"; break;
            }
        }

        std::sort (bank.variants.begin(), bank.variants.end(),
                   [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });

        if (bank.isValid()) list.push_back (std::move (bank));
    }

    std::sort (list.begin(), list.end(),
               [] (const auto& a, const auto& b) { return a.title.compareNatural (b.title) < 0; });
}

juce::File Library::imageForFile (const juce::File& file) const
{
    if (file == juce::File{}) return {};

    const auto bankDir = file.getParentDirectory();

    for (int f = 0; f < (int) LibraryFolder::count; ++f)
        for (const auto& bank : bankList[f])
            if (bank.dir == bankDir) return bank.image;

    // Keine Bank: die Datei liegt woanders in der Bibliothek, z. B. im
    // Audition-Ordner. Zuerst ein Bild mit dem Namen der Datei, dann irgendeins
    // aus dem Ordner.
    if (bankDir.isAChildOf (rootDir))
    {
        const auto stem = file.getFileNameWithoutExtension();
        for (const char* ext : { ".jpg", ".jpeg", ".png" })
        {
            const auto beside = bankDir.getChildFile (stem + "-art" + ext);
            if (beside.existsAsFile()) return beside;
            const auto same = bankDir.getChildFile (stem + ext);
            if (same.existsAsFile()) return same;
        }

        // Im Audition-Ordner liegen viele fremde Bilder, dort gilt nur das
        // eigene.
        if (bankDir.getFileName() != "Audition")
            for (const auto& e : juce::RangedDirectoryIterator (bankDir, false, "*.jpg;*.jpeg;*.png",
                                                                juce::File::findFiles))
                return e.getFile();
    }

    return {};
}

std::vector<const LibraryBank*> Library::banksFor (nmp::SlotType type, nmp::NamRole role) const
{
    std::vector<const LibraryBank*> out;

    auto addFrom = [&] (LibraryFolder f, const std::function<bool (const LibraryBank&)>& pred)
    {
        for (const auto& b : bankList[(int) f])
            if (pred (b)) out.push_back (&b);
    };

    auto isFullRig = [] (const LibraryBank& b) { return b.gear == "amp-cab" || b.gear == "full-rig"; };

    if (type == nmp::SlotType::ir)
    {
        // Räume, die als IR abgelegt wurden, gehören zu SPACE, nicht zu den
        // Boxen.
        addFrom (LibraryFolder::irs, [] (const auto& b) { return b.gear != "space"; });
        return out;
    }

    if (type != nmp::SlotType::nam) return out;

    // Die Rolle der Karte filtert die Bänke: AMP zeigt Amps, FULL RIG zeigt
    // Full Rigs. Ein Full Rig bringt seine Box mit und gehört nicht unter AMP.
    switch (role)
    {
        case nmp::NamRole::amp:
            addFrom (LibraryFolder::models, [&] (const auto& b) { return ! isFullRig (b); });
            break;
        case nmp::NamRole::fullrig:
            addFrom (LibraryFolder::models, isFullRig);
            break;
        case nmp::NamRole::pedal:
            addFrom (LibraryFolder::pedals, [] (const auto& b) { return b.gear != "space"; });
            break;
        // Räume liegen bei OUTBOARD und werden über die Kategorie
        // unterschieden, wie AMP und AMP+CAB im selben Ordner.
        case nmp::NamRole::outboard:
            addFrom (LibraryFolder::outboard, [] (const auto& b) { return b.gear != "space"; });
            break;
        case nmp::NamRole::space:
            addFrom (LibraryFolder::outboard, [] (const auto& b) { return b.gear == "space"; });
            addFrom (LibraryFolder::irs,      [] (const auto& b) { return b.gear == "space"; });
            break;
        default: break;
    }

    return out;
}

LibraryFolder Library::folderForGear (const juce::String& gear)
{
    const auto g = gear.toLowerCase();
    if (g == "pedal")    return LibraryFolder::pedals;
    if (g == "outboard" || g == "space") return LibraryFolder::outboard;
    if (g == "cab" || g == "ir") return LibraryFolder::irs;
    return LibraryFolder::models;   // amp, amp-cab, full-rig, experimental
}

const LibraryBank* Library::importFile (const juce::File& source, LibraryFolder f,
                                        juce::String& error, const juce::String& gear)
{
    if (! source.existsAsFile()) { error = "That file no longer exists."; return nullptr; }

    const auto ext = source.getFileExtension().toLowerCase();
    const bool wantsNam = f != LibraryFolder::irs;

    if (ext != ".nam" && ext != ".wav")
    {
        error = "That is neither a capture (.nam) nor an impulse (.wav).";
        return nullptr;
    }
    if (wantsNam && ext == ".wav" && f != LibraryFolder::outboard)
    {
        error = "This slot takes a capture (.nam).";
        return nullptr;
    }

    const auto title = sanitiseName (source.getFileNameWithoutExtension());
    auto bankDir = folder (f).getChildFile (title + ".namplify");

    int suffix = 2;
    while (bankDir.exists())
        bankDir = folder (f).getChildFile (title + " " + juce::String (suffix++) + ".namplify");

    if (! bankDir.createDirectory())
    {
        error = "The folder could not be created.";
        return nullptr;
    }

    const auto target = bankDir.getChildFile (source.getFileName());
    if (! source.copyFileTo (target))
    {
        bankDir.deleteRecursively();
        error = "The file could not be copied.";
        return nullptr;
    }

    // Metadaten, damit die Karte einen lesbaren Namen zeigt.
    juce::DynamicObject::Ptr side (new juce::DynamicObject());
    side->setProperty ("toneTitle", title);
    side->setProperty ("modelName", source.getFileNameWithoutExtension());
    side->setProperty ("author", "");
    side->setProperty ("gear", gear.isNotEmpty() ? gear
                             : f == LibraryFolder::irs ? "cab"
                             : f == LibraryFolder::pedals ? "pedal"
                             : f == LibraryFolder::outboard ? "outboard" : "amp");
    side->setProperty ("size", "custom");
    side->setProperty ("toneId", 0);

    bankDir.getChildFile (target.getFileNameWithoutExtension() + ".t3k.json")
        .replaceWithText (juce::JSON::toString (juce::var (side.get())));

    scanFolder (f);

    for (const auto& b : bankList[(int) f])
        if (b.dir == bankDir) return &b;

    error = "The bank was created but could not be found again.";
    return nullptr;
}

bool Library::deleteBank (const juce::File& bankDir, juce::String& error)
{
    if (! isInsideLibrary (bankDir))
    {
        error = "That is not inside the library. Nothing was deleted.";
        return false;
    }
    if (! bankDir.isDirectory()) { error = "That bank no longer exists."; return true; }

    if (! bankDir.deleteRecursively())
    {
        error = "The folder could not be deleted.";
        return false;
    }

    rescan();
    return true;
}

bool Library::deleteVariant (const juce::File& file, juce::String& error)
{
    if (! isInsideLibrary (file))
    {
        error = "That is not inside the library. Nothing was deleted.";
        return false;
    }
    if (! file.existsAsFile()) return true;

    const auto bankDir = file.getParentDirectory();
    const auto sidecar = bankDir.getChildFile (file.getFileNameWithoutExtension() + ".t3k.json");

    if (! file.deleteFile()) { error = "The file could not be deleted."; return false; }
    sidecar.deleteFile();

    // Ohne Varianten wird auch die Bank entfernt.
    bool anyLeft = false;
    for (const auto& e : juce::RangedDirectoryIterator (bankDir, false, "*.nam;*.wav",
                                                        juce::File::findFiles))
    {
        juce::ignoreUnused (e);
        anyLeft = true;
        break;
    }
    if (! anyLeft) bankDir.deleteRecursively();

    rescan();
    return true;
}

juce::File Library::storeDownload (LibraryFolder f, const juce::String& bankTitle, int toneId,
                                   const juce::String& variantName, int modelId,
                                   const juce::String& size, const juce::String& gear,
                                   const juce::String& author, const juce::String& page,
                                   const juce::MemoryBlock& data, const juce::String& extension,
                                   juce::String& error)
{
    const auto title = sanitiseName (bankTitle);
    auto bankDir = folder (f).getChildFile (title + ".namplify");

    // Eine Bank ist über ihre ID eindeutig, nicht über den Namen.
    if (auto* existing = findBankByToneId (toneId))
        bankDir = existing->dir;

    if (! bankDir.exists() && ! bankDir.createDirectory())
    {
        error = "The folder could not be created.";
        return {};
    }

    const auto safeVariant = sanitiseName (variantName.isNotEmpty() ? variantName : "Model");
    const auto target = bankDir.getChildFile (safeVariant + extension);

    if (! target.replaceWithData (data.getData(), data.getSize()))
    {
        error = "The file could not be written.";
        return {};
    }

    juce::DynamicObject::Ptr side (new juce::DynamicObject());
    side->setProperty ("toneId", toneId);
    side->setProperty ("toneTitle", bankTitle);
    side->setProperty ("modelName", variantName);
    side->setProperty ("modelId", modelId);
    side->setProperty ("size", size);
    side->setProperty ("gear", gear);
    side->setProperty ("author", author);
    // URL der Tone-Seite, wie TONE3000 sie liefert (enthält den Namen, lässt
    // sich nicht aus der ID bauen).
    if (page.isNotEmpty()) side->setProperty ("page", page);

    // Liegt schon ein Bild in der Bank, seinen Dateinamen eintragen.
    for (const auto& e : juce::RangedDirectoryIterator (bankDir, false, "*.jpg;*.jpeg;*.png",
                                                        juce::File::findFiles))
    {
        side->setProperty ("imageUrl", e.getFile().getFileName());
        break;
    }

    bankDir.getChildFile (safeVariant + ".t3k.json")
        .replaceWithText (juce::JSON::toString (juce::var (side.get())));

    scanFolder (f);
    return target;
}

void Library::storeImage (const juce::File& bankDir, const juce::MemoryBlock& data,
                          const juce::String& extension)
{
    if (! bankDir.isDirectory() || data.getSize() == 0) return;

    const auto image = bankDir.getChildFile ("cover" + extension);
    image.replaceWithData (data.getData(), data.getSize());

    // Alle Metadaten der Bank auf das Bild zeigen lassen.
    for (const auto& e : juce::RangedDirectoryIterator (bankDir, false, "*.t3k.json",
                                                        juce::File::findFiles))
    {
        const auto file = e.getFile();
        auto json = juce::JSON::parse (file);
        if (auto* o = json.getDynamicObject())
        {
            o->setProperty ("imageUrl", image.getFileName());
            file.replaceWithText (juce::JSON::toString (json));
        }
    }

    // Die eingelesene Bank sofort aktualisieren, das Bild kommt meist erst nach
    // dem Einlesen.
    for (auto& list : bankList)
        for (auto& bank : list)
            if (bank.dir == bankDir) bank.image = image;
}

bool Library::hasModel (int modelId) const
{
    if (modelId == 0) return false;
    for (int f = 0; f < (int) LibraryFolder::count; ++f)
        for (const auto& b : bankList[f])
            for (const auto& v : b.variants)
                if (v.modelId == modelId) return true;
    return false;
}

const LibraryBank* Library::bankForFile (const juce::File& file) const
{
    const auto bankDir = file.getParentDirectory();
    for (int f = 0; f < (int) LibraryFolder::count; ++f)
        for (const auto& bank : bankList[f])
            if (bank.dir == bankDir) return &bank;
    return nullptr;
}

bool Library::hasTone (int toneId) const { return findBankByToneId (toneId) != nullptr; }

const LibraryBank* Library::findBankByToneId (int toneId) const
{
    if (toneId == 0) return nullptr;
    for (int f = 0; f < (int) LibraryFolder::count; ++f)
        for (const auto& b : bankList[f])
            if (b.toneId == toneId) return &b;
    return nullptr;
}

void Library::installBundledIrs()
{
    // Die drei mitgelieferten IRs, beim ersten Start in die Bibliothek gelegt,
    // damit ein Amp immer eine Box hat. Eigene, markenfreie Impulsantworten.
    struct Bundled { const char* wav; const char* json; const char* jpg; const char* title; };

    for (int i = 0; i < NamplifyResources::namedResourceListSize; ++i)
    {
        const juce::String original { NamplifyResources::originalFilenames[i] };
        if (! original.endsWithIgnoreCase (".t3k.json")) continue;
        if (! original.startsWithIgnoreCase ("namplify-")) continue;

        int size = 0;
        const char* data = NamplifyResources::getNamedResource (
            NamplifyResources::namedResourceList[i], size);
        if (data == nullptr) continue;

        const auto json = juce::JSON::parse (juce::String::fromUTF8 (data, size));
        auto* o = json.getDynamicObject();
        if (o == nullptr) continue;

        const auto title = sanitiseName (o->getProperty ("toneTitle").toString());
        const auto bankDir = folder (LibraryFolder::irs).getChildFile (title + ".namplify");

        // Nur beim ersten Start. Gelöschte Boxen kommen nicht zurück.
        if (bankDir.exists()) continue;
        if (! bankDir.createDirectory()) continue;

        const auto base = original.dropLastCharacters (juce::String (".t3k.json").length());

        auto copyResource = [&] (const juce::String& name, const juce::File& dest)
        {
            for (int k = 0; k < NamplifyResources::namedResourceListSize; ++k)
            {
                if (juce::String (NamplifyResources::originalFilenames[k]) != name) continue;
                int sz = 0;
                if (const char* d = NamplifyResources::getNamedResource (
                        NamplifyResources::namedResourceList[k], sz))
                    dest.replaceWithData (d, (size_t) sz);
                return;
            }
        };

        copyResource (base + ".wav", bankDir.getChildFile (title + ".wav"));
        copyResource (base + ".jpg", bankDir.getChildFile (base + ".jpg"));

        bankDir.getChildFile (title + ".t3k.json")
            .replaceWithText (juce::String::fromUTF8 (data, size));
    }

    scanFolder (LibraryFolder::irs);
}

} // namespace nmpl
