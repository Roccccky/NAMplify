#pragma once
// Ressourcen für die eingebettete UI. Alles unter ui/ ist einkompiliert und
// wird über den ursprünglichen Dateinamen gefunden, damit die Pfade im Browser
// (Demo) und im Plugin gleich sind.

#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>

namespace nmpl
{

std::optional<juce::WebBrowserComponent::Resource> provideResource (const juce::String& path);

/// Bilder aus der Bibliothek: Dateien werden nur unterhalb dieses Ordners
/// ausgeliefert.
void setImageRoot (const juce::File&);

} // namespace nmpl
