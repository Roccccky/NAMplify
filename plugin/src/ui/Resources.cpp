#include "Resources.h"
#include <NamplifyResources.h>
#include <cstring>
#include <vector>

namespace nmpl
{

namespace
{
    juce::File imageRoot;

    juce::String mimeFor (const juce::String& name)
    {
        const auto ext = name.fromLastOccurrenceOf (".", false, false).toLowerCase();
        if (ext == "html") return "text/html; charset=utf-8";
        if (ext == "css")  return "text/css; charset=utf-8";
        if (ext == "js")   return "text/javascript; charset=utf-8";
        if (ext == "json") return "application/json; charset=utf-8";
        if (ext == "svg")  return "image/svg+xml";
        if (ext == "png")  return "image/png";
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "wav")  return "audio/wav";
        return "application/octet-stream";
    }

    std::vector<std::byte> toBytes (const void* data, size_t size)
    {
        std::vector<std::byte> out (size);
        std::memcpy (out.data(), data, size);
        return out;
    }
}

void setImageRoot (const juce::File& f) { imageRoot = f; }

std::optional<juce::WebBrowserComponent::Resource> provideResource (const juce::String& path)
{
    auto name = path.fromLastOccurrenceOf ("/", false, false);
    if (name.isEmpty() || path == "/") name = "index.html";

    // Bilder aus der Bibliothek. Ausgeliefert wird nur, was unterhalb der
    // Bibliothek liegt.
    if (path.startsWith ("/lib-image/") && imageRoot != juce::File{})
    {
        const auto relative = juce::URL::removeEscapeChars (path.fromFirstOccurrenceOf ("/lib-image/", false, false));
        const auto file = imageRoot.getChildFile (relative);

        if (file.existsAsFile() && file.isAChildOf (imageRoot))
        {
            juce::MemoryBlock block;
            if (file.loadFileAsData (block))
                return juce::WebBrowserComponent::Resource {
                    toBytes (block.getData(), block.getSize()),
                    mimeFor (file.getFileName()) };
        }
        return std::nullopt;
    }

    for (int i = 0; i < NamplifyResources::namedResourceListSize; ++i)
    {
        if (juce::String (NamplifyResources::originalFilenames[i]) != name) continue;

        int size = 0;
        if (const char* data = NamplifyResources::getNamedResource (
                NamplifyResources::namedResourceList[i], size))
            return juce::WebBrowserComponent::Resource { toBytes (data, (size_t) size),
                                                         mimeFor (name) };
    }

    return std::nullopt;
}

} // namespace nmpl
