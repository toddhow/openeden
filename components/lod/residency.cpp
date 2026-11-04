#include "residency.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Lod
{
    namespace
    {
        constexpr std::uint8_t goneFreed = 1;
        constexpr std::uint8_t goneAdmitted = 2;

        std::uint64_t addSaturating(std::uint64_t a, std::uint64_t b) noexcept
        {
            return a > std::numeric_limits<std::uint64_t>::max() - b ? std::numeric_limits<std::uint64_t>::max()
                                                                     : a + b;
        }

        // NaN is the least urgent value.
        float urgencyKey(float priority) noexcept
        {
            return std::isnan(priority) ? std::numeric_limits<float>::infinity() : priority;
        }

        // What is held now: held items that have not been freed by this plan, items that are loading, and the items
        // this plan starts loading. `count` is how many such items there are.
        struct Held
        {
            std::uint64_t mBytes = 0;
            std::size_t mCount = 0;
        };

        Held heldNow(std::span<const ResidencyItem> items, const std::vector<std::uint8_t>& gone) noexcept
        {
            Held result;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const bool counts = gone[i] == goneAdmitted || (gone[i] == 0 && (items[i].mHeld || items[i].mLoading));
                if (!counts)
                    continue;
                result.mBytes = addSaturating(result.mBytes, items[i].mBytes);
                ++result.mCount;
            }
            return result;
        }

        // Would `held` minus `freed` plus `need` fit the budget?
        bool fits(std::uint64_t held, std::uint64_t freed, std::uint64_t need, std::uint64_t budget) noexcept
        {
            const std::uint64_t base = held >= freed ? held - freed : 0;
            return addSaturating(base, need) <= budget;
        }

        // Fills `victims` with the held items that may be freed, least urgent first: those not wanted before those
        // wanted, in each class the larger key first, then the smaller index. `candidateKey` is the key of the item a
        // swap is for (negative infinity: pressure, any wanted replaceable item qualifies).
        void collectVictims(std::span<const ResidencyItem> items, const std::vector<std::uint8_t>& gone,
            bool forCandidate, float candidateKey, float margin, std::vector<std::uint32_t>& victims)
        {
            victims.clear();
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const ResidencyItem& item = items[i];
                if (gone[i] != 0 || !item.mHeld || item.mPinned)
                    continue;
                if (item.mWanted)
                {
                    if (!item.mReplaceable)
                        continue;
                    if (forCandidate)
                    {
                        const float key = urgencyKey(item.mPriority);
                        if (!(key >= margin * candidateKey) || !(key > candidateKey))
                            continue;
                    }
                }
                victims.push_back(static_cast<std::uint32_t>(i));
            }
            std::sort(victims.begin(), victims.end(), [&items](std::uint32_t a, std::uint32_t b) {
                const ResidencyItem& x = items[a];
                const ResidencyItem& y = items[b];
                if (x.mWanted != y.mWanted)
                    return !x.mWanted; // not wanted first
                const float kx = urgencyKey(x.mPriority);
                const float ky = urgencyKey(y.mPriority);
                if (kx != ky)
                    return kx > ky;
                return a < b;
            });
        }
    }

    void planResidency(std::span<const ResidencyItem> items, const ResidencyParams& params, ResidencyPlan& plan)
    {
        plan.mFree.clear();
        plan.mLoad.clear();
        plan.mHeldAfter = 0;
        plan.mBlocked = 0;
        plan.mOverBudget = false;
        plan.mGone.assign(items.size(), 0);

        const float margin = std::max(1.f, std::isnan(params.mSwapMargin) ? 1.f : params.mSwapMargin);
        const float lowWater = std::isnan(params.mLowWater) ? 1.f : std::clamp(params.mLowWater, 0.01f, 1.f);
        const std::uint64_t budget = params.mBudgetBytes;

        // 1. Idle items go.
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const ResidencyItem& item = items[i];
            if (item.mHeld && !item.mWanted && !item.mPinned && item.mIdleFrames >= params.mKeepFrames)
            {
                plan.mGone[i] = goneFreed;
                plan.mFree.push_back(static_cast<std::uint32_t>(i));
            }
        }

        // 2. Loads, most urgent first.
        plan.mScratch.clear();
        std::uint32_t inFlight = 0;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const ResidencyItem& item = items[i];
            if (item.mLoading)
                ++inFlight;
            else if (!item.mHeld && item.mWanted && item.mCooldown <= 0)
                plan.mScratch.push_back(static_cast<std::uint32_t>(i));
        }
        std::sort(plan.mScratch.begin(), plan.mScratch.end(), [&items](std::uint32_t a, std::uint32_t b) {
            const float ka = urgencyKey(items[a].mPriority);
            const float kb = urgencyKey(items[b].mPriority);
            if (ka != kb)
                return ka < kb;
            return a < b;
        });
        std::uint32_t started = 0;
        for (const std::uint32_t candidate : plan.mScratch)
        {
            if (inFlight >= params.mMaxLoadsInFlight || started >= params.mMaxLoadsPerCall)
                break;
            const ResidencyItem& item = items[candidate];
            const Held held = heldNow(items, plan.mGone);
            bool admit = budget == 0 || held.mCount == 0 || fits(held.mBytes, 0, item.mBytes, budget);
            if (!admit)
            {
                collectVictims(items, plan.mGone, true, urgencyKey(item.mPriority), margin, plan.mVictims);
                std::uint64_t freed = 0;
                std::size_t taken = 0;
                for (; taken < plan.mVictims.size(); ++taken)
                {
                    freed = addSaturating(freed, items[plan.mVictims[taken]].mBytes);
                    if (fits(held.mBytes, freed, item.mBytes, budget))
                    {
                        ++taken;
                        admit = true;
                        break;
                    }
                }
                if (admit)
                {
                    for (std::size_t v = 0; v < taken; ++v)
                    {
                        plan.mGone[plan.mVictims[v]] = goneFreed;
                        plan.mFree.push_back(plan.mVictims[v]);
                    }
                }
            }
            if (!admit)
            {
                ++plan.mBlocked;
                continue;
            }
            plan.mGone[candidate] = goneAdmitted;
            plan.mLoad.push_back(candidate);
            ++inFlight;
            ++started;
        }

        // 3. Pressure.
        if (budget > 0)
        {
            Held held = heldNow(items, plan.mGone);
            if (held.mBytes > budget)
            {
                const auto target = static_cast<std::uint64_t>(std::floor(static_cast<double>(budget) * lowWater));
                collectVictims(items, plan.mGone, false, 0.f, margin, plan.mVictims);
                std::uint64_t freed = 0;
                for (const std::uint32_t victim : plan.mVictims)
                {
                    if (fits(held.mBytes, freed, 0, target))
                        break;
                    freed = addSaturating(freed, items[victim].mBytes);
                    plan.mGone[victim] = goneFreed;
                    plan.mFree.push_back(victim);
                }
            }
        }

        const Held after = heldNow(items, plan.mGone);
        plan.mHeldAfter = after.mBytes;
        plan.mOverBudget = budget > 0 && after.mBytes > budget;
    }
}

namespace Lod
{
    std::uint64_t tileDrawResidentBytes(std::uint32_t gridSize, std::uint64_t albedoBytes) noexcept
    {
        if (gridSize < 2)
            return albedoBytes;
        const std::uint64_t side = gridSize;
        const std::uint64_t squares = (side - 1) * (side - 1);
        // side * side * 44 stays below 2^64 up to a grid of 2^29; larger grids saturate at once, so no step can wrap.
        constexpr std::uint64_t most = std::numeric_limits<std::uint64_t>::max();
        if (side > (1ull << 29))
            return most;
        const std::uint64_t vertices = side * side * tileBytesPerVertex;
        const std::uint64_t indices = squares * 6 * tileBytesPerIndex;
        return addSaturating(addSaturating(vertices, indices), albedoBytes);
    }
}
