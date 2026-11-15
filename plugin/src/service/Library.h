#pragma once
// Die Bibliothek auf der Festplatte.
//
// Dokumente/NAMplify/
//   NAM/Models/     <Bank>.namplify/  <variante>.nam + .t3k.json + Bild
//   NAM/IRs/        <Bank>.namplify/  <variante>.wav + …
//   NAM/Pedals/     …
//   NAM/Outboard/   …
//   Presets/        Rig-Presets
//   Recordings/     gespeicherte Aufnahmen
//
// Eine Bank ist ein Ordner und entspricht einem Tone, die Dateien darin sind
// seine Varianten. Die .t3k.json enthält den Dateinamen des Bildes, keine URL,
// damit es auch offline angezeigt wird.

#include <juce_core/juce_core.h>
#include <namplify/engine/Slots.h>
#include <vector>

namespace nmpl
{

enum class LibraryFolder { models = 0, irs, pedals, outboard, count };

const char* libraryFolderName (LibraryFolder);

struct LibraryVariant
{
    juce::File   file;          ///< .nam oder .wav
    juce::String name;          ///< lesbarer Name der Variante
    juce::String size;          ///< standard · lite · feather · nano · custom
    int          modelId = 0;
};

struct LibraryBank
{
    juce::File    dir;
    juce::String  title;        ///< Banktitel
    juce::String  author;
    juce::String  gear;         ///< amp · amp-cab · cab · pedal · outboard
    juce::File    image;        ///< leer, wenn keins da ist
    int           toneId = 0;
    juce::String  page;         ///< Seite der Bank auf TONE3000, leer wenn unbekannt
    LibraryFolder folder = LibraryFolder::models;
    std::vector<LibraryVariant> variants;

    bool isValid() const { return ! variants.empty(); }
};

class Library
{
public:
    Library();

    /// Wurzel der Bibliothek, wird bei Bedarf angelegt.
    juce::File root() const { return rootDir; }
    juce::File folder (LibraryFolder) const;
    juce::File presetsDir() const   { return rootDir.getChildFile ("Presets"); }
    juce::File recordingsDir() const { return rootDir.getChildFile ("Recordings"); }

    void rescan();

    const std::vector<LibraryBank>& banks (LibraryFolder f) const
    { return bankList[(int) f]; }

    /// Bild der Bank, in der diese Datei liegt. Leer, wenn keins da ist. Die UI
    /// fragt hier nach und reicht keine Bildpfade zurück.
    juce::File imageForFile (const juce::File&) const;

    /// Bänke, die ein Slot dieses Typs mit dieser Rolle zeigt.
    std::vector<const LibraryBank*> banksFor (nmp::SlotType, nmp::NamRole) const;

    /// Zielordner für eine Datei dieser Kategorie.
    static LibraryFolder folderForGear (const juce::String& gear);

    /// Eigene Datei importieren, sie wird als eigene Bank abgelegt. gear leer:
    /// die Kategorie des Ordners. SPACE liegt bei OUTBOARD und gibt sich
    /// deshalb selbst an.
    const LibraryBank* importFile (const juce::File& source, LibraryFolder,
                                   juce::String& error, const juce::String& gear = {});

    /// Ganze Bank löschen, ohne Rückfrage, aber nur innerhalb der Bibliothek.
    bool deleteBank (const juce::File& bankDir, juce::String& error);
    /// Eine einzelne Variante löschen.
    bool deleteVariant (const juce::File& file, juce::String& error);

    /// Heruntergeladene Variante ablegen.
    juce::File storeDownload (LibraryFolder, const juce::String& bankTitle, int toneId,
                              const juce::String& variantName, int modelId,
                              const juce::String& size, const juce::String& gear,
                              const juce::String& author, const juce::String& page,
                              const juce::MemoryBlock& data, const juce::String& extension,
                              juce::String& error);

    /// Bild einer Bank ablegen.
    void storeImage (const juce::File& bankDir, const juce::MemoryBlock& data,
                     const juce::String& extension);

    /// Liegt diese Variante schon hier? (LED im Browser)
    bool hasModel (int modelId) const;
    bool hasTone (int toneId) const;
    const LibraryBank* findBankByToneId (int toneId) const;
    /// Bank, in der diese Datei liegt, nullptr wenn keine.
    const LibraryBank* bankForFile (const juce::File& file) const;

    /// Die drei mitgelieferten IRs beim ersten Start ablegen.
    void installBundledIrs();

private:
    void scanFolder (LibraryFolder);
    static juce::String sanitiseName (const juce::String&);
    bool isInsideLibrary (const juce::File&) const;

    juce::File rootDir;
    std::vector<LibraryBank> bankList[(int) LibraryFolder::count];
};

} // namespace nmpl
