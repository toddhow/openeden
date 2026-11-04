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
        // loading, and the items this plan starts loading. `mCount` is how many such items there are; `mReleases`
        // what the loading and admitted ones let go once drawn (mReleasesBytes: a parent tile replacing its pinned
        // children, or children replacing their parent), which admission and pressure both subtract.
        struct Committed
        {
            std::uint64_t mBytes = 0;
            std::uint64_t mReleases = 0;
            std::size_t mCount = 0;

            std::uint64_t net() const noexcept { return mBytes >= mReleases ? mBytes - mReleases : 0; }
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
                if (gone[i] == goneAdmitted || items[i].mLoading)
                    result.mReleases = addSaturating(result.mReleases, items[i].mReleasesBytes);
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
        // for a candidate (`forCandidate`) it must also be clearly less urgent than the candidate. Only replaceable
        // candidates look for victims: one with no fallback is admitted without them.
        void collectVictims(std::span<const ResidencyItem> items, const std::vector<std::uint8_t>& gone,
            bool forCandidate, float candidateKey, float margin, std::vector<std::uint32_t>& victims)
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
        // The committed bytes are kept up to date as the plan admits and frees, so a candidate that fits costs no
        // pass over the items; only a victim search does, and at most mMaxBlockedPerCall of those run in one call.
        Committed committed = budget > 0 ? committedNow(items, plan.mGone) : Committed{};
        std::uint32_t started = 0;
        std::uint32_t searches = 0;
        for (const std::uint32_t candidate : plan.mScratch)
        {
            if (inFlight >= params.mMaxLoadsInFlight || started >= params.mMaxLoadsPerCall)
                break;
            const ResidencyItem& item = items[candidate];
            // No budget, or no fallback (nothing else would draw its square): admitted whatever the budget says. The
            // budget governs the replaceable items only; pressure below frees those if this goes over.
            bool admit = budget == 0 || !item.mReplaceable || committed.mCount == 0
                || fits(committed.net(), 0, item.mReleasesBytes, item.mBytes, lowWaterBytes);
            // A candidate whose loads failed before frees nothing for itself: it waits until it fits.
            if (!admit && item.mFailures == 0 && searches < params.mMaxBlockedPerCall)
            {
                ++searches;
                collectVictims(items, plan.mGone, true, urgencyKey(item.mPriority), margin, plan.mVictims);
                std::uint64_t freed = 0;
                std::size_t taken = 0;
                for (; taken < plan.mVictims.size(); ++taken)
                {
                    freed = addSaturating(freed, items[plan.mVictims[taken]].mBytes);
                    if (fits(committed.net(), freed, item.mReleasesBytes, item.mBytes, lowWaterBytes))
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
                        const std::uint32_t victim = plan.mVictims[v];
                        plan.mGone[victim] = goneFreed;
                        plan.mFree.push_back(victim);
                        const std::uint64_t bytes = items[victim].mBytes;
                        committed.mBytes = committed.mBytes >= bytes ? committed.mBytes - bytes : 0;
                        --committed.mCount;
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
            committed.mBytes = addSaturating(committed.mBytes, item.mBytes);
            committed.mReleases = addSaturating(committed.mReleases, item.mReleasesBytes);
            ++committed.mCount;
            ++inFlight;
            ++started;
        }

        // 3. Pressure: above the budget, free down to the low water mark, never the last committed item. What the
        // loading and admitted items release counts as gone (a coarsening or a refinement in progress).
        if (budget > 0)
        {
            committed = committedNow(items, plan.mGone);
            if (committed.net() > budget)
            {
                collectVictims(items, plan.mGone, false, 0.f, margin, plan.mVictims);
                std::uint64_t freed = 0;
                std::size_t left = committed.mCount;
                for (const std::uint32_t victim : plan.mVictims)
                {
                    if (fits(committed.net(), freed, 0, 0, lowWaterBytes) || left <= 1)
                        break;
                    freed = addSaturating(freed, items[victim].mBytes);
                    plan.mGone[victim] = goneFreed;
                    plan.mFree.push_back(victim);
                    --left;
                }
            }
        }

        const Committed after = committedNow(items, plan.mGone);
        plan.mHeldAfter = after.net();
        plan.mOverBudget = budget > 0 && after.net() > budget;
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

namespace Lod
{
    double replaceableMargin(double range) noexcept
    {
        // 5 percent of the range, at least a quarter of a cell (ESM::Land::REAL_SIZE is 8192 units).
        constexpr double share = 0.05;
        constexpr double quarterCell = 2048;
        return std::isfinite(range) ? std::max(quarterCell, share * range) : quarterCell;
    }

    bool squareReplaceable(
        bool previous, double x, double y, double originX, double originY, double side, double range) noexcept
    {
        if (previous)
            return squareWithinRange(x, y, originX, originY, side, range);
        return squareWithinRange(x, y, originX, originY, side, range - replaceableMargin(range));
    }

    std::int32_t failureCooldownFrames(std::int32_t baseFrames, std::uint32_t failures) noexcept
    {
        if (failures == 0 || baseFrames <= 0)
            return 0;
        // Doubling for each further failure, at most 16 times the base (four doublings).
        constexpr std::uint32_t maxDoublings = 4;
        const std::uint32_t doublings = std::min(failures - 1, maxDoublings);
        const std::int64_t frames = static_cast<std::int64_t>(baseFrames) << doublings;
        return static_cast<std::int32_t>(std::min<std::int64_t>(frames, std::numeric_limits<std::int32_t>::max()));
    }
}
