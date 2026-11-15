#pragma once
// Lade-Thread.
//
// Ein Capture laden heißt: Datei lesen, Netz aufbauen, anfahren und messen. Das
// dauert bis zu einige Zehntelsekunden. Deshalb läuft es in einem eigenen
// Thread mit Warteschlange. Fertige Modelle werden an die Engine übergeben, die
// sie im nächsten Block übernimmt und überblendet.

#include <juce_events/juce_events.h>
#include <namplify/engine/Engine.h>
#include "RigState.h"
#include <functional>
#include <deque>
#include <mutex>
#include <atomic>

namespace nmpl
{

class Loader : private juce::Thread
{
public:
    struct Request
    {
        int          slot = 0;     ///< Slot 0 … kNumSlots-1
        juce::File   file;
        SlotFile     info;
        bool         clear = false;
    };

    struct Result
    {
        int          slot = 0;     ///< Slot 0 … kNumSlots-1
        SlotFile     info;
        bool         ok = false;
        juce::String error;
        bool         cleared = false;
        bool         isIr = false;
        nmp::NamModelInfo model;
    };

    using Callback = std::function<void (const Result&)>;

    Loader (nmp::Engine&);
    ~Loader() override;

    void setCallback (Callback cb) { callback = std::move (cb); }

    void request (const Request&);
    void requestClear (int slot);

    /// Nach einem Samplerate-Wechsel oder geändertem Referenzpegel müssen die
    /// geladenen Captures neu vorbereitet bzw. gemessen werden.
    void requestReload();

    bool isBusy() const { return busy.load(); }

private:
    void run() override;
    Result handle (const Request&);

    nmp::Engine& engine;
    Callback callback;

    std::mutex queueLock;
    std::deque<Request> queue;
    juce::WaitableEvent wake;
    std::atomic<bool> busy { false };

    /// Zuletzt geladene Datei je Slot, für das erneute Vorbereiten.
    SlotFile lastLoaded[(size_t) nmp::kNumSlots];
    juce::File lastFile[(size_t) nmp::kNumSlots];
};

} // namespace nmpl
