#include "idlefidget.hpp"

#include <algorithm>
#include <cmath>

namespace HavokPack::Fidget
{
    namespace
    {
        // splitmix64: a fixed integer mix, so that neighbouring reference numbers give unrelated results.
        std::uint64_t mix(std::uint64_t id)
        {
            std::uint64_t z = id + 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        // A number in [0, 1) from 24 bits of the mix.
        float unit(std::uint64_t z, int shift)
        {
            return static_cast<float>((z >> shift) & 0xFFFFFFu) / 16777216.f;
        }
    }

    Schedule scheduleFor(std::uint64_t id)
    {
        const std::uint64_t z = mix(id);
        Schedule s;
        s.mFirst = 2.f + 8.f * unit(z, 0);
        s.mPeriod = 12.f + 12.f * unit(z, 24);
        return s;
    }

    std::size_t variantFor(std::uint64_t id)
    {
        return static_cast<std::size_t>((mix(id) >> 48) % kVariants.size());
    }

    Play at(const Schedule& schedule, float time, float duration, float ramp)
    {
        Play none;
        if (!std::isfinite(time) || !std::isfinite(duration) || !(duration > 0.f) || !(schedule.mPeriod > 0.f)
            || !std::isfinite(schedule.mFirst) || !std::isfinite(schedule.mPeriod))
            return none;
        const float since = time - schedule.mFirst;
        if (since < 0.f)
            return none;
        const float period = std::max(schedule.mPeriod, duration + 3.f);
        const float u = since - std::floor(since / period) * period;
        if (u >= duration)
            return none;
        float weight = 1.f;
        if (ramp > 0.f)
            weight = std::clamp(std::min(u, duration - u) / ramp, 0.f, 1.f);
        return { weight, u };
    }
}
