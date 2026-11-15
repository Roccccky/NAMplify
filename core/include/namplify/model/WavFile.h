#pragma once
// WAV lesen und schreiben, ohne externe Bibliothek. Gelesen werden
// 8/16/24/32-Bit-Integer und 32/64-Bit-Float mit beliebiger Kanalzahl (auch
// WAVE_FORMAT_EXTENSIBLE). Geschrieben wird 24-Bit-Stereo.

#include "../util/Types.h"
#include <string>
#include <filesystem>
#include <vector>

namespace nmp
{

struct WavData
{
    std::vector<std::vector<float>> channels;
    double sampleRate = 0.0;

    int numChannels() const { return (int) channels.size(); }
    int numFrames()   const { return channels.empty() ? 0 : (int) channels[0].size(); }

    /// Alle Kanäle zu Mono mischen (für IRs).
    std::vector<float> toMono() const;
};

/// Liest eine WAV-Datei. Bei Fehler false und der Grund in error, ohne
/// Exception.
bool readWav (const std::filesystem::path& path, WavData& out, std::string& error);

/// Schreibt Stereo als 24-Bit-WAV.
bool writeWav24 (const std::filesystem::path& path,
                 const float* left, const float* right, int numFrames,
                 double sampleRate, std::string& error);

} // namespace nmp
