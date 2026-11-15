#pragma once
// Grundtypen des DSP-Kerns, ohne Framework- oder Betriebssystem-Abhängigkeit.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <vector>

namespace nmp
{

using Sample = float;

inline constexpr double kPi     = 3.14159265358979323846;
inline constexpr double kTwoPi  = 6.28318530717958647692;

/// Kleinster Pegel, der noch nicht als Stille gilt.
inline constexpr float kSilenceGain = 1.0e-6f;   // -120 dB

// Pegel

inline float dbToGain (float db)              { return std::pow (10.0f, db * 0.05f); }
inline double dbToGain (double db)            { return std::pow (10.0,  db * 0.05); }

inline float gainToDb (float g)               { return g > kSilenceGain ? 20.0f * std::log10 (g) : -120.0f; }
inline double gainToDb (double g)             { return g > 1.0e-12     ? 20.0  * std::log10 (g) : -240.0; }

/// dB → linear, am unteren Anschlag exakt null.
inline float dbToGainWithFloor (float db, float floorDb)
{
    return db <= floorDb ? 0.0f : dbToGain (db);
}

// Bereiche

template <typename T>
inline T clamp (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

/// 0…1 auf einen Bereich abbilden und zurück.
inline float mapTo   (float unit, float lo, float hi) { return lo + (hi - lo) * unit; }
inline float mapFrom (float v,    float lo, float hi) { return hi > lo ? (v - lo) / (hi - lo) : 0.0f; }

/// Nichtlineare Abbildung für Regler mit feinerem unteren Bereich (Zeit,
/// Frequenz). Basis > 1 verschiebt die Auflösung nach unten.
inline float mapToSkewed (float unit, float lo, float hi, float skew)
{
    return lo + (hi - lo) * std::pow (unit, skew);
}
inline float mapFromSkewed (float v, float lo, float hi, float skew)
{
    if (hi <= lo || skew <= 0.0f) return 0.0f;
    return std::pow (clamp ((v - lo) / (hi - lo), 0.0f, 1.0f), 1.0f / skew);
}

/// Logarithmische Abbildung für Hz und Sekunden.
inline float mapToLog (float unit, float lo, float hi)
{
    return lo * std::pow (hi / lo, unit);
}
inline float mapFromLog (float v, float lo, float hi)
{
    return std::log (clamp (v, lo, hi) / lo) / std::log (hi / lo);
}

// Denormals. Ausklingende Feedback-Schleifen landen sonst in Denormals und
// erhöhen die CPU-Last. Jede Rückkopplung läuft durch diese Funktion.

inline float flushDenormal (float v)
{
    return std::abs (v) < 1.0e-20f ? 0.0f : v;
}

// Stereo-Block. Verweist auf fremden Speicher, besitzt nichts.

struct StereoBlock
{
    Sample* left  = nullptr;
    Sample* right = nullptr;
    int     numFrames = 0;

    Sample*       channel (int c)       { return c == 0 ? left : right; }
    const Sample* channel (int c) const { return c == 0 ? left : right; }

    void clear()
    {
        std::fill (left,  left  + numFrames, 0.0f);
        std::fill (right, right + numFrames, 0.0f);
    }
};

/// Infos für einen Block beim Vorbereiten.
struct PrepareInfo
{
    double sampleRate     = 48000.0;
    int    maxBlockSize   = 512;
};

/// Notenwerte für tempo-synchrone Parameter. Die Reihenfolge ist Teil der
/// Parameterliste: nur hinten anfügen.
enum class NoteDiv : int
{
    whole = 0,      // 1/1
    half,           // 1/2
    quarterDotted,  // 1/4.
    quarter,        // 1/4
    quarterTriplet, // 1/4T
    eighthDotted,   // 1/8.
    eighth,         // 1/8
    eighthTriplet,  // 1/8T
    sixteenthDotted,// 1/16.
    count
};

/// Länge eines Notenwertes in Vierteln.
inline double noteDivInQuarters (NoteDiv d)
{
    switch (d)
    {
        case NoteDiv::whole:           return 4.0;
        case NoteDiv::half:            return 2.0;
        case NoteDiv::quarterDotted:   return 1.5;
        case NoteDiv::quarter:         return 1.0;
        case NoteDiv::quarterTriplet:  return 2.0 / 3.0;
        case NoteDiv::eighthDotted:    return 0.75;
        case NoteDiv::eighth:          return 0.5;
        case NoteDiv::eighthTriplet:   return 1.0 / 3.0;
        case NoteDiv::sixteenthDotted: return 0.375;
        default:                       return 1.0;
    }
}

inline const char* noteDivName (NoteDiv d)
{
    switch (d)
    {
        case NoteDiv::whole:           return "1/1";
        case NoteDiv::half:            return "1/2";
        case NoteDiv::quarterDotted:   return "1/4.";
        case NoteDiv::quarter:         return "1/4";
        case NoteDiv::quarterTriplet:  return "1/4T";
        case NoteDiv::eighthDotted:    return "1/8.";
        case NoteDiv::eighth:          return "1/8";
        case NoteDiv::eighthTriplet:   return "1/8T";
        case NoteDiv::sixteenthDotted: return "1/16.";
        default:                       return "1/4";
    }
}

/// Sekunden je Notenwert bei gegebenem Tempo.
inline double noteDivSeconds (NoteDiv d, double bpm)
{
    return noteDivInQuarters (d) * 60.0 / std::max (1.0, bpm);
}

} // namespace nmp
