#include "neutralweathersound.hpp"

#include <algorithm>
#include <cmath>

namespace MWWorld::NeutralWeatherSound
{
    namespace
    {
        float finiteOr(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }
    }

    float thunderDelaySeconds(unsigned distanceIndex)
    {
        const unsigned index = std::min(distanceIndex, kThunderMaxDistanceIndex);
        return kThunderBaseDelaySeconds + kThunderDelayPerStepSeconds * static_cast<float>(index);
    }

    float thunderVolume(float baseVolume, unsigned distanceIndex)
    {
        const unsigned index = std::min(distanceIndex, kThunderMaxDistanceIndex);
        const float base = std::clamp(finiteOr(baseVolume, 0.f), 0.f, 1.f);
        return std::clamp(base * (1.f - kThunderVolumePerStepFalloff * static_cast<float>(index)), 0.f, 1.f);
    }

    const std::string& pickFile(const Slot& slot, std::uint32_t counter)
    {
        static const std::string none;
        if (slot.files.empty())
            return none;
        return slot.files[counter % slot.files.size()];
    }

    bool DelayedThunder::push(float delaySeconds, std::uint32_t fileIndex, float volume, unsigned distanceIndex)
    {
        if (mCount >= mEntries.size())
            return false;
        Entry& entry = mEntries[mCount++];
        entry.remaining = std::max(finiteOr(delaySeconds, 0.f), 0.f);
        entry.due = Due{ fileIndex, volume, distanceIndex };
        return true;
    }

    std::size_t DelayedThunder::advance(float seconds, Due* out, std::size_t capacity)
    {
        const float elapsed = std::max(finiteOr(seconds, 0.f), 0.f);
        // Time moves on for all of them, then the due ones leave earliest due time first (the one that reached zero
        // with the most time to spare was due first); the rest keep their place.
        struct Ready
        {
            std::size_t index;
            float overdue;
        };
        std::array<Ready, kMaxPendingThunder> ready{};
        std::size_t readyCount = 0;
        for (std::size_t i = 0; i < mCount; ++i)
        {
            mEntries[i].remaining -= elapsed;
            if (mEntries[i].remaining <= 0.f)
                ready[readyCount++] = Ready{ i, -mEntries[i].remaining };
        }
        // Most overdue first; the queue order breaks ties (a stable insertion sort of at most eight).
        for (std::size_t i = 1; i < readyCount; ++i)
            for (std::size_t j = i; j > 0 && ready[j].overdue > ready[j - 1].overdue; --j)
                std::swap(ready[j], ready[j - 1]);
        const std::size_t taken = std::min(readyCount, capacity);
        std::array<bool, kMaxPendingThunder> removed{};
        for (std::size_t i = 0; i < taken; ++i)
        {
            out[i] = mEntries[ready[i].index].due;
            removed[ready[i].index] = true;
        }
        std::size_t kept = 0;
        for (std::size_t i = 0; i < mCount; ++i)
            if (!removed[i])
                mEntries[kept++] = mEntries[i];
        mCount = kept;
        return taken;
    }

    float gustIntervalSeconds(float wind)
    {
        const float w = std::clamp(finiteOr(wind, 0.f), 0.f, 1.f);
        return kGustSecondsAtNoWind + (kGustSecondsAtFullWind - kGustSecondsAtNoWind) * w;
    }

    bool GustTimer::advance(float seconds, float wind)
    {
        mElapsed += std::max(finiteOr(seconds, 0.f), 0.f);
        if (mElapsed < gustIntervalSeconds(wind))
            return false;
        mElapsed = 0.f;
        ++mCount;
        return true;
    }

    LoopWish loopWish(const Slot& slot, float blendVolume)
    {
        if (slot.empty() || !slot.loop)
            return {};
        const float volume = slot.volume * finiteOr(blendVolume, 0.f);
        if (!(volume > 0.f) || !std::isfinite(volume))
            return {};
        return LoopWish{ slot.files.front(), std::min(volume, 1.f) };
    }
}
