#pragma once
// Deterministischer Zufallsgenerator, liefert auf jedem Rechner dieselbe Folge.
// Für reproduzierbare Lautheitsmessung und Tests.

#include <cstdint>

namespace nmp
{

class Xorshift
{
public:
    explicit Xorshift (std::uint32_t seed = 0x9E3779B9u) : state (seed ? seed : 1u) {}

    std::uint32_t nextUInt()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /// −1 … +1, gleichverteilt.
    float nextBipolar() { return (float) ((double) (std::int32_t) nextUInt() / 2147483648.0); }

    /// 0 … 1
    float nextUnit() { return (float) ((double) nextUInt() / 4294967296.0); }

    void seed (std::uint32_t s) { state = s ? s : 1u; }

private:
    std::uint32_t state;
};

} // namespace nmp
