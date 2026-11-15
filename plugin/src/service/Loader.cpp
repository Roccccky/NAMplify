#include "Loader.h"
#include "../util/Pfad.h"
#include <namplify/model/WavFile.h>
#include <cmath>
#include <string>
#include <vector>

namespace nmpl
{

Loader::Loader (nmp::Engine& e) : juce::Thread ("NAMplify Loader"), engine (e)
{
    startThread (juce::Thread::Priority::normal);
}

Loader::~Loader()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
}

void Loader::request (const Request& r)
{
    {
        std::lock_guard<std::mutex> lock (queueLock);

        // Pro Slot zählt nur die letzte Anforderung.
        for (auto it = queue.begin(); it != queue.end();)
            if (it->slot == r.slot) it = queue.erase (it);
            else ++it;

        queue.push_back (r);
    }
    wake.signal();
}

void Loader::requestClear (int slot)
{
    Request r;
    r.slot = slot;
    r.clear = true;
    request (r);
}

void Loader::requestReload()
{
    for (int i = 0; i < nmp::kNumSlots; ++i)
    {
        if (lastFile[(size_t) i] == juce::File()) continue;
        Request r;
        r.slot = i;
        r.file = lastFile[(size_t) i];
        r.info = lastLoaded[(size_t) i];
        request (r);
    }
}

void Loader::run()
{
    while (! threadShouldExit())
    {
        Request r;
        bool have = false;
        {
            std::lock_guard<std::mutex> lock (queueLock);
            if (! queue.empty()) { r = queue.front(); queue.pop_front(); have = true; }
        }

        if (! have) { wake.wait (200); continue; }

        busy.store (true);
        const auto result = handle (r);
        busy.store (false);

        if (callback)
        {
            auto cb = callback;
            juce::MessageManager::callAsync ([cb, result] { cb (result); });
        }
    }
}

Loader::Result Loader::handle (const Request& r)
{
    Result out;
    out.slot = r.slot;
    out.info = r.info;

    const int slot = r.slot;
    if (slot < 0 || slot >= nmp::kNumSlots)
    {
        out.error = "There is no such slot.";
        return out;
    }

    // Die Blöcke des angeforderten Aufbaus. Ein Slot, der gerade den Typ
    // gewechselt hat, bekommt seine Datei so in den neuen Block.
    auto* ir  = engine.irFor (slot);
    auto* nam = engine.namFor (slot);

    if (r.clear)
    {
        if (ir != nullptr) ir->prepareIr (nullptr, 0);
        else if (nam != nullptr) nam->requestClear();

        lastLoaded[(size_t) slot] = SlotFile{};
        lastFile[(size_t) slot] = juce::File();

        out.ok = true;
        out.cleared = true;
        return out;
    }

    if (! r.file.existsAsFile())
    {
        out.error = "That file no longer exists.";
        return out;
    }

    const auto ext = r.file.getFileExtension().toLowerCase();

    // Impulsantwort
    if (ext == ".wav")
    {
        nmp::WavData wav;
        std::string err;
        if (! nmp::readWav (toPath (r.file), wav, err))
        {
            out.error = juce::String ("The impulse could not be read: ")
                      + juce::String (err);
            return out;
        }

        auto mono = wav.toMono();

        // IR auf die Samplerate der Engine umrechnen.
        if (wav.sampleRate > 0.0 && std::abs (wav.sampleRate - engine.sampleRate()) > 1.0)
        {
            nmp::SincResampler rs;
            rs.prepare (wav.sampleRate, engine.sampleRate(), (int) mono.size());
            std::vector<float> converted ((size_t) rs.maxOutputFor ((int) mono.size()) + 8, 0.0f);
            const int n = rs.process (mono.data(), (int) mono.size(), converted.data());
            converted.resize ((size_t) juce::jmax (1, n));
            mono.swap (converted);
        }

        // Auf eine halbe Sekunde begrenzen.
        const int maxLen = (int) (engine.sampleRate() * 0.5);
        if ((int) mono.size() > maxLen) mono.resize ((size_t) maxLen);

        if (ir != nullptr)
            ir->prepareIr (mono.data(), (int) mono.size());
        else if (nam != nullptr)
            nam->publishIr (std::move (mono));
        else
        {
            out.error = "This slot does not take an impulse.";
            return out;
        }

        out.ok = true;
        out.isIr = true;
        lastLoaded[(size_t) slot] = r.info;
        lastFile[(size_t) slot] = r.file;
        return out;
    }

    // Capture
    if (ext != ".nam")
    {
        out.error = "That is neither a capture (.nam) nor an impulse (.wav).";
        return out;
    }

    if (nam == nullptr)
    {
        out.error = "This slot takes an impulse, not a capture.";
        return out;
    }

    std::string error;
    auto model = nmp::NamModel::load (toPath (r.file),
                                      engine.sampleRate(), engine.maxBlockSize(), error,
                                      engine.captureOversampling());
    if (! model)
    {
        out.error = juce::String (juce::CharPointer_UTF8 (error.c_str()));
        return out;
    }

    out.model = model->info();
    nam->publishModel (std::move (model));

    out.ok = true;
    lastLoaded[(size_t) slot] = r.info;
    lastFile[(size_t) slot] = r.file;
    return out;
}

} // namespace nmpl
