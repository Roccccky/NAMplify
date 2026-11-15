#include "namplify/engine/Spread.h"
#include <algorithm>
#include <cmath>

namespace nmp
{

void Spread::prepare (double sr, int)
{
    sampleRate = sr;
    line.prepare (sampleRate, 0.040);
    amount.reset (sampleRate, 0.05);
    amount.setCurrentAndTarget (0.0f);
    builtMs = -1.0f;
    rebuild (targetMs);
    reset();
}

void Spread::reset()
{
    for (auto& a : ap) a.reset();
    line.reset();
}

void Spread::rebuild (float ms)
{
    if (std::abs (ms - builtMs) < 0.01f) return;
    builtMs = ms;

    // Vier Allpässe mit Längen ohne einfache Verhältnisse, damit keine
    // gemeinsamen Eigenmoden entstehen.
    const float total = ms * 0.001f * (float) sampleRate;
    const float share[kStages] = { 0.41f, 0.27f, 0.19f, 0.13f };
    const float g[kStages]     = { 0.62f, -0.58f, 0.55f, -0.5f };

    for (int i = 0; i < kStages; ++i)
        ap[i].prepare (std::max (3, (int) std::lround (total * share[i])), g[i]);
}

void Spread::process (StereoBlock& block)
{
    rebuild (targetMs);

    // Am linken Anschlag ist der Block aus.
    amount.setTarget (isOff() ? 0.0f : 0.75f);

    if (! amount.isSmoothing() && amount.getCurrent() <= 0.0f) return;

    for (int i = 0; i < block.numFrames; ++i)
    {
        const float l = block.left[i];
        const float r = block.right[i];

        const float mid  = 0.5f * (l + r);
        const float side = 0.5f * (l - r);

        float d = mid;
        for (auto& a : ap) d = a.process (d);

        const float k = amount.next();
        const float s = side + d * k;

        // Mitte unverändert, damit auch die Mono-Summe.
        block.left[i]  = mid + s;
        block.right[i] = mid - s;
    }
}

} // namespace nmp
