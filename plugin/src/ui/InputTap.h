#pragma once
// Pegel der Eingangskanäle für die Kanalwahl in den Einstellungen. Der Abgriff
// hängt direkt am Gerät, sieht also den rohen Eingang auch bei Mute. JUCE
// liefert dem Callback nur aktive Kanäle, daher zeigen nur die gewählten Kanäle
// einen Pegel.

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <cmath>

namespace nmpl
{

struct InputTap final : juce::AudioIODeviceCallback
{
    static constexpr int kMaxChannels = 64;

    /// Spitzenwert je Gerätekanal seit dem letzten Abholen.
    std::array<std::atomic<float>, kMaxChannels> peaks {};
    /// Welcher Gerätekanal an welcher Position im Callback ankommt.
    std::array<std::atomic<int>, kMaxChannels> slotToChannel {};

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                           float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int slot = 0; slot < juce::jmin (numIn, kMaxChannels); ++slot)
        {
            if (in[slot] == nullptr) continue;
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i) peak = juce::jmax (peak, std::abs (in[slot][i]));

            const int ch = slotToChannel[(size_t) slot].load (std::memory_order_relaxed);
            if (ch < 0 || ch >= kMaxChannels) continue;
            auto& p = peaks[(size_t) ch];
            if (peak > p.load (std::memory_order_relaxed)) p.store (peak, std::memory_order_relaxed);
        }

        // Nur zuhören: Ausgänge auf null, der DeviceManager mischt die Ausgänge
        // aller Callbacks.
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], numSamples);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const auto active = device->getActiveInputChannels();
        int slot = 0;
        for (int ch = 0; ch < active.getHighestBit() + 1 && slot < kMaxChannels; ++ch)
            if (active[ch]) slotToChannel[(size_t) slot++].store (ch, std::memory_order_relaxed);
        for (; slot < kMaxChannels; ++slot) slotToChannel[(size_t) slot].store (-1, std::memory_order_relaxed);
        clear();
    }

    void audioDeviceStopped() override { clear(); }

    void clear()
    {
        for (auto& p : peaks) p.store (0.0f, std::memory_order_relaxed);
    }

    /// Spitzenwert abholen (in dB, −120 als Boden) und zurücksetzen.
    float takeDb (int channel)
    {
        if (channel < 0 || channel >= kMaxChannels) return -120.0f;
        const float p = peaks[(size_t) channel].exchange (0.0f, std::memory_order_relaxed);
        return p > 1.0e-6f ? juce::jmax (-120.0f, 20.0f * std::log10 (p)) : -120.0f;
    }
};

} // namespace nmpl
