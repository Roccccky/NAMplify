#include "namplify/engine/Transport.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

void Transport::prepare (double sr)
{
    sampleRate = sr;
    reset();
}

void Transport::reset()
{
    samplesSinceClock = 0.0;
    totalSamples = 0.0;
    currentSource = Source::none;
    running = false;
    smoothedBpm = 120.0;
    hostPpq = clockPpq = 0.0;
    intervalCount = intervalWrite = pulseCount = 0;
    lastPulseSamples = -1.0;
}

void Transport::setHostInfo (bool playing, double bpm, double ppqPosition)
{
    if (bpm <= 1.0) return;

    currentSource = Source::host;
    running = playing;
    hostPpq = ppqPosition;

    // Trotzdem glätten, manche Hosts melden bei Tempoautomation pro Block
    // leicht andere Werte.
    if (std::abs (bpm - smoothedBpm) > 8.0) smoothedBpm = bpm;
    else                                    smoothedBpm += 0.25 * (bpm - smoothedBpm);
}

void Transport::clearHostInfo()
{
    if (currentSource == Source::host)
    {
        currentSource = Source::none;
        running = false;
    }
}

void Transport::pushInterval (double seconds)
{
    intervals[intervalWrite] = seconds;
    intervalWrite = (intervalWrite + 1) % kIntervalHistory;
    if (intervalCount < kIntervalHistory) ++intervalCount;
}

void Transport::midiClockPulse()
{
    if (currentSource == Source::host) return;   // Host hat Vorrang

    if (lastPulseSamples >= 0.0)
    {
        const double dt = (totalSamples - lastPulseSamples) / sampleRate;

        // Abstände außerhalb von 30 … 300 BPM verwerfen.
        if (dt > 1.0 / (24.0 * 300.0 / 60.0) * 0.5 && dt < 60.0 / (24.0 * 30.0) * 2.0)
            pushInterval (dt);
    }
    lastPulseSamples = totalSamples;

    ++pulseCount;
    clockPpq += 1.0 / 24.0;

    if (intervalCount >= 8)
    {
        // Median statt Mittelwert, einzelne verspätete Impulse ändern ihn
        // nicht.
        double sorted[kIntervalHistory];
        std::copy (intervals, intervals + intervalCount, sorted);
        std::sort (sorted, sorted + intervalCount);
        const double median = sorted[intervalCount / 2];

        if (median > 1.0e-6)
        {
            const double bpm = 60.0 / (median * 24.0);
            if (bpm > 20.0 && bpm < 400.0)
            {
                currentSource = Source::midiClock;
                if (std::abs (bpm - smoothedBpm) > 15.0) smoothedBpm = bpm;
                else                                     smoothedBpm += 0.08 * (bpm - smoothedBpm);
            }
        }
    }

    samplesSinceClock = 0.0;
}

void Transport::midiStart()    { running = true;  clockPpq = 0.0; }
void Transport::midiContinue() { running = true; }
void Transport::midiStop()     { running = false; }

void Transport::advance (int numFrames)
{
    totalSamples      += numFrames;
    samplesSinceClock += numFrames;

    // Eine halbe Sekunde ohne Impuls: keine Clock mehr.
    if (currentSource == Source::midiClock && samplesSinceClock > sampleRate * 0.5)
    {
        currentSource = Source::none;
        running = false;
        intervalCount = intervalWrite = pulseCount = 0;
        lastPulseSamples = -1.0;
    }

    if (currentSource == Source::midiClock && running)
        clockPpq += (double) numFrames / sampleRate * (smoothedBpm / 60.0);
}

BlockContext Transport::context (double fallbackBpm, int numFrames) const
{
    BlockContext c;
    c.numFramesTotal   = numFrames;
    c.clockRunning     = currentSource != Source::none && running;
    c.transportPlaying = running;
    c.bpm              = c.clockRunning ? smoothedBpm : fallbackBpm;
    c.ppqPosition      = currentSource == Source::host ? hostPpq : clockPpq;
    return c;
}

} // namespace nmp
