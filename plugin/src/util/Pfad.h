#pragma once
// Pfad aus einem juce::String bauen.
//
// Unter macOS und Linux sind schmale Strings UTF-8. Unter Windows sind sie in
// der eingestellten Codepage (meist Windows-1252), path(s.toStdString())
// zerstört dort Umlaute, etwa im Benutzerordner. Deshalb unter Windows über den
// breiten String.
#include <juce_core/juce_core.h>
#include <filesystem>

namespace nmpl
{

inline std::filesystem::path toPath (const juce::String& s)
{
   #if JUCE_WINDOWS
    return std::filesystem::path (s.toWideCharPointer());
   #else
    return std::filesystem::path (s.toStdString());
   #endif
}

inline std::filesystem::path toPath (const juce::File& f)
{
    return toPath (f.getFullPathName());
}

} // namespace nmpl
