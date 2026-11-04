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

        // What is committed now: held items that are not leaving and have not been freed by this plan, items that are
        // loading, and the items this plan starts loading. `count` is how many such items there are.
        struct Committed
        {
            std::uint64_t mBytes = 0;
            std::size_t mCount = 0;
        };

        bool isCommitted(const ResidencyItem& item, std::uint8_t gone) noexcept
        {
            if (gone == goneAdmitted)
                return true;
            if (gone != 0)
                return false;
            return item.mLoading || (item.mHeld && !item.mLeaving);
        }

        Committed committedNow(std::span<const ResidencyItem> items, const std::vector<std::uint8_t>& gone) noexcept
        {
            Committed result;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                if (!isCommitted(items[i], gone[i]))
                    continue;
                result.mBytes = addSaturating(result.mBytes, items[i].mBytes);
                ++result.mCount;
            }
            return result;
        }

        // Would `committed` minus `freed` minus `released` plus `need` stay at or under `limit`?
        bool fits(std::uint64_t committed, std::uint64_t freed, std::uint64_t released, std::uint64_t need,
            std::uint64_t limit) noexcept
        {
            const std::uint64_t gone = addSaturating(freed, released);
            const std::uint64_t base = committed >= gone ? committed - gone : 0;
            return addSaturating(base, need) <= limit;
        }

        // Fills `victims` with the held items that may be freed, least urgent first: those not wanted before those
        // wanted, in each class the larger key first, then the smaller index. A wanted victim must be replaceable;
        // for a candidate (`forCandidate`) it must also be clearly less urgent than the candidate, unless the
        // candidate has no fallback (`candidateHasFallback` false), which outranks any replaceable item.
        void collectVictims(std::span<const ResidencyItem> items, const std::vector<std::uint8_t>& gone,
            bool forCandidate, float candidateKey, bool candidateHasFallback, float margin,
            std::vector<std::uint32_t>& victims)
        {
            victims.clear();
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const ResidencyItem& item = items[i];
                if (gone[i] != 0 || !item.mHeld || item.mPinned || item.mLeaving)
                    continue;
                if (item.mWanted)
                {
                    if (!item.mReplaceable)
                        continue;
                    if (forCandidate && candidateHasFallback)
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
        // Admission fills up to the low water mark and pressure starts above the budget: an estimate that turns out
        // a little low does not start pressure, and what pressure frees is not taken back at once.
        // Rounded, not cut: 0.9f is a little below 0.9, and 9 MiB of a 10 MiB budget must fit.
        const auto lowWaterBytes = static_cast<std::uint64_t>(std::llround(static_cast<double>(budget) * lowWater));

        // 1. Idle items go.
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const ResidencyItem& item = items[i];
            if (item.mHeld && !item.mWanted && !item.mPinned && !item.mLeaving
                && item.mIdleFrames >= params.mKeepFrames)
            {
                plan.mGone[i] = goneFreed;
                plan.mFree.push_back(static_cast<std::uint32_t>(i));
            }
        }

        // 2. Loads.
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
        if (params.mNearestFirst)
        {
            std::sort(plan.mScratch.begin(), plan.mScratch.end(), [&items](std::uint32_t a, std::uint32_t b) {
                // No fallback first: such an item leaves a hole when it is not drawn.
                if (items[a].mReplaceable != items[b].mReplaceable)
                    return !items[a].mReplaceable;
                const float ka = urgencyKey(items[a].mPriority);
                const float kb = urgencyKey(items[b].mPriority);
                if (ka != kb)
                    return ka < kb;
                return a < b;
            });
        }
        std::uint32_t started = 0;
        for (const std::uint32_t candidate : plan.mScratch)
        {
            if (inFlight >= params.mMaxLoadsInFlight || started >= params.mMaxLoadsPerCall
                || plan.mBlocked >= params.mMaxBlockedPerCall)
                break;
            const ResidencyItem& item = items[candidate];
            bool admit = budget == 0;
            if (!admit)
            {
                const Committed committed = committedNow(items, plan.mGone);
                admit = committed.mCount == 0
                    || fits(committed.mBytes, 0, item.mReleasesBytes, item.mBytes, lowWaterBytes);
                if (!admit)
                {
                    collectVictims(items, plan.mGone, true, urgencyKey(item.mPriority), item.mReplaceable, margin,
                        plan.mVictims);
                    std::uint64_t freed = 0;
                    std::size_t taken = 0;
                    for (; taken < plan.mVictims.size(); ++taken)
                    {
                        freed = addSaturating(freed, items[plan.mVictims[taken]].mBytes);
                        if (fits(committed.mBytes, freed, item.mReleasesBytes, item.mBytes, lowWaterBytes))
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

        // 3. Pressure: above the budget, free down to the low water mark, never the last committed item.
        if (budget > 0)
        {
            Committed committed = committedNow(items, plan.mGone);
            if (committed.mBytes > budget)
            {
                collectVictims(items, plan.mGone, false, 0.f, true, margin, plan.mVictims);
                std::uint64_t freed = 0;
                std::size_t left = committed.mCount;
                for (const std::uint32_t victim : plan.mVictims)
                {
                    if (fits(committed.mBytes, freed, 0, 0, lowWaterBytes) || left <= 1)
                        break;
                    freed = addSaturating(freed, items[victim].mBytes);
                    plan.mGone[victim] = goneFreed;
                    plan.mFree.push_back(victim);
                    --left;
                }
            }
        }

        const Committed after = committedNow(items, plan.mGone);
        plan.mHeldAfter = after.mBytes;
        plan.mOverBudget = budget > 0 && after.mBytes > budget;
    }

    bool squareWithinRange(double x, double y, double originX, double originY, double side, double range) noexcept
    {
        if (!(range > 0) || !std::isfinite(range))
            return false;
        // The farthest point of the square from (x, y) is a corner: on each axis the end farther from the point.
        const double dx = std::max(std::abs(x - originX), std::abs(x - (originX + side)));
        const double dy = std::max(std::abs(y - originY), std::abs(y - (originY + side)));
        const double far = std::sqrt(dx * dx + dy * dy);
        return std::isfinite(far) && far <= range;
    }
}

namespace Lod
{
    std::string describeBytes(std::uint64_t bytes)
    {
        constexpr std::uint64_t kib = 1024;
        constexpr std::uint64_t mib = kib * kib;
        if (bytes < kib)
            return std::to_string(bytes) + " bytes";
        if (bytes < mib)
            return std::to_string((bytes + kib / 2) / kib) + " KiB";
        if (bytes < 10 * mib && bytes % mib != 0)
        {
            const std::uint64_t tenths = (bytes * 10 + mib / 2) / mib;
            return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MiB";
        }
        return std::to_string((bytes + mib / 2) / mib) + " MiB";
    }
}
