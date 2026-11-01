#pragma once

// Deterministic multi-harmonic continuous seeded noise generator (milestone C5, task 0023).
// Provides analytical C^1 continuity, multi-rate invariance, and zero heap allocations.

#include "opas/math/Vec3.hpp"

#include <cstdint>

namespace opas::style
{

    // Fast, deterministic 64-bit integer hash (SplitMix64).
    [[nodiscard]] constexpr std::uint64_t hash64(std::uint64_t x) noexcept
    {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31U);
    }

    // Maps uint64 to uniform double in [0, 1).
    [[nodiscard]] constexpr double toUnitDouble(std::uint64_t x) noexcept
    {
        return static_cast<double>(x >> 11U) * (1.0 / static_cast<double>(1ULL << 53U));
    }

    // Maps uint64 to uniform double in [-1, 1].
    [[nodiscard]] constexpr double toSignedDouble(std::uint64_t x) noexcept
    {
        return toUnitDouble(x) * 2.0 - 1.0;
    }

    // Continuous, bounded multi-harmonic noise in [-1.0, 1.0].
    // Evaluated analytically: bit-identical for the same inputs across any time step size dt.
    [[nodiscard]] double noise1D(double timeS, double freqHz, std::uint64_t seed, std::uint32_t channel = 0);

    // 2D continuous noise vector with components in [-1.0, 1.0].
    [[nodiscard]] math::Vec3 noise2D(double timeS, double freqHz, std::uint64_t seed, std::uint32_t channel = 0);

} // namespace opas::style
