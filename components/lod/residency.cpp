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

        // Does a load in flight (or one this plan starts) count its estimate? Not for an item with no fallback whose
        // loads failed before: it is admitted whatever the budget says, and a load that fails again must not start
        // pressure on the others (third review, finding 3). Its bytes count once it is held.
        bool loadCounts(const ResidencyItem& item) noexcept
        {
            return item.mReplaceable || item.mFailures == 0;
        }

        Committed committedNow(std::span<const ResidencyItem> items, const std::vector<std::uint8_t>& gone) noexcept
        {
            Committed result;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                if (!isCommitted(items[i], gone[i]))
                    continue;
                ++result.mCount;
                const bool loading = gone[i] == goneAdmitted || items[i].mLoading;
                if (loading && !loadCounts(items[i]))
                    continue;
                result.mBytes = addSaturating(result.mBytes, items[i].mBytes);
                if (loading)
                    result.mReleases = addSaturating(result.mReleases, items[i].mReleasesBytes);
            }
            return result;
        }

        // Is a wanted item that is not held or loading held back by a cooldown? The eviction cooldown holds back
        // replaceable items only (one with no fallback would leave its square undrawn; third review, finding 1); the
        // failure back-off holds back every item.
        bool coolingDown(const ResidencyItem& item) noexcept
        {
            return item.mFailureCooldown > 0 || (item.mCooldown > 0 && item.mReplaceable);
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
        plan.mCooling = 0;
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
            else if (!item.mHeld && item.mWanted)
            {
                if (coolingDown(item))
                    ++plan.mCooling;
                else
                    plan.mScratch.push_back(static_cast<std::uint32_t>(i));
            }
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
            if (loadCounts(item))
            {
                committed.mBytes = addSaturating(committed.mBytes, item.mBytes);
                committed.mReleases = addSaturating(committed.mReleases, item.mReleasesBytes);
            }
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
        // What an overrun is made of: the committed bytes of the items the budget cannot touch (no fallback).
        plan.mNoFallbackBytes = 0;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (!items[i].mReplaceable && isCommitted(items[i], plan.mGone[i]))
            {
                const bool loading = plan.mGone[i] == goneAdmitted || items[i].mLoading;
                if (!loading || loadCounts(items[i]))
                    plan.mNoFallbackBytes = addSaturating(plan.mNoFallbackBytes, items[i].mBytes);
            }
        }
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

    bool clusterReplaceable(bool previous, bool liveObjectsPage, double x, double y, double originX, double originY,
        double side, double range) noexcept
    {
        return liveObjectsPage && squareReplaceable(previous, x, y, originX, originY, side, range);
    }

    bool cancelEviction(bool evicted, bool drawn, bool replaceable) noexcept
    {
        return evicted && drawn && !replaceable;
    }

    RefinedChild classifyRefinedChild(bool drawn, bool liveDrawsSquare, bool failed, bool visible) noexcept
    {
        if (drawn)
            return RefinedChild::Drawn;
        if (liveDrawsSquare)
            return RefinedChild::LiveDraws;
        if (failed)
            return RefinedChild::Failed;
        return visible ? RefinedChild::Coming : RefinedChild::OutOfView;
    }

    bool refinedParentMayGo(std::span<const RefinedChild> children) noexcept
    {
        for (const RefinedChild child : children)
        {
            if (child == RefinedChild::Coming || child == RefinedChild::Failed)
                return false;
        }
        return true;
    }

    void refinementReleases(std::span<const RefinementTile> tiles, std::vector<std::uint32_t>& missing,
        std::vector<std::uint64_t>& releases)
    {
        missing.assign(tiles.size(), 0);
        releases.assign(tiles.size(), 0);
        // The nearest ancestor of a tile, or none; a bad index ends the walk.
        const auto parentOf = [&tiles](std::int32_t index) -> std::int32_t {
            const std::int32_t parent = tiles[static_cast<std::size_t>(index)].mParent;
            return parent >= 0 && static_cast<std::size_t>(parent) < tiles.size() ? parent : -1;
        };
        // A coarsening: a drawn tile that is no longer chosen goes once its nearest chosen ancestor is drawn, so it is
        // that ancestor's release. A refinement: a chosen tile not yet drawn is missing from its nearest drawn
        // ancestor that is no longer chosen.
        for (std::size_t j = 0; j < tiles.size(); ++j)
        {
            const RefinementTile& tile = tiles[j];
            if (!tile.mDrawn && !tile.mChosen)
                continue;
            for (std::int32_t p = parentOf(static_cast<std::int32_t>(j)); p >= 0; p = parentOf(p))
            {
                const RefinementTile& parent = tiles[static_cast<std::size_t>(p)];
                if (tile.mDrawn && !tile.mChosen && parent.mChosen)
                {
                    if (!parent.mDrawn)
                        releases[static_cast<std::size_t>(p)] += tile.mDrawBytes;
                    break;
                }
                if (!tile.mDrawn && tile.mChosen && parent.mDrawn && !parent.mChosen)
                {
                    ++missing[static_cast<std::size_t>(p)];
                    break;
                }
            }
        }
        // Each missing tile gets an even share of the ancestor it replaces, unless that ancestor stays for good.
        for (std::size_t j = 0; j < tiles.size(); ++j)
        {
            if (tiles[j].mDrawn || !tiles[j].mChosen)
                continue;
            for (std::int32_t p = parentOf(static_cast<std::int32_t>(j)); p >= 0; p = parentOf(p))
            {
                const auto parent = static_cast<std::size_t>(p);
                if (tiles[parent].mDrawn && !tiles[parent].mChosen)
                {
                    if (missing[parent] > 0 && !tiles[parent].mStays)
                        releases[j] += tiles[parent].mDrawBytes / missing[parent];
                    break;
                }
            }
        }
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

    bool groundDrawnOver(double x, double y, double range, std::span<const GroundSquare> drawn, double originX,
        double originY, double side, double cellSize) noexcept
    {
        if (!(cellSize > 0) || !std::isfinite(cellSize) || !(side > 0) || !std::isfinite(side)
            || !std::isfinite(originX) || !std::isfinite(originY))
            return false;
        const double cellCount = side / cellSize;
        const double rounded = std::round(cellCount);
        // A whole number of cells, at most maxGroundCellsPerSide a side (the work is at most 1024 cell tests).
        constexpr double wholeTolerance = 1e-6;
        if (!(rounded >= 1) || rounded > static_cast<double>(maxGroundCellsPerSide)
            || std::abs(cellCount - rounded) > wholeTolerance)
            return false;
        const auto n = static_cast<std::uint32_t>(rounded);
        for (std::uint32_t j = 0; j < n; ++j)
        {
            for (std::uint32_t i = 0; i < n; ++i)
            {
                const double cellX = originX + cellSize * static_cast<double>(i);
                const double cellY = originY + cellSize * static_cast<double>(j);
                // The live terrain draws a cell when all of it lies inside the paging range.
                if (squareWithinRange(x, y, cellX, cellY, cellSize, range))
                    continue;
                // Else a drawn tile must hold the cell's centre.
                const double centreX = cellX + 0.5 * cellSize;
                const double centreY = cellY + 0.5 * cellSize;
                bool covered = false;
                for (const GroundSquare& tile : drawn)
                {
                    if (centreX >= tile.mOriginX && centreX < tile.mOriginX + tile.mSide && centreY >= tile.mOriginY
                        && centreY < tile.mOriginY + tile.mSide)
                    {
                        covered = true;
                        break;
                    }
                }
                if (!covered)
                    return false;
            }
        }
        return true;
    }

    std::int32_t stepGroundFrames(std::int32_t frames, bool groundDrawn) noexcept
    {
        if (!groundDrawn)
            return 0;
        return frames < std::numeric_limits<std::int32_t>::max() ? frames + 1 : frames;
    }

    bool clusterGroundFallback(
        bool evictFar, bool liveObjectsPage, bool squareWhollyInRange, std::int32_t groundFrames) noexcept
    {
        return evictFar && liveObjectsPage && !squareWhollyInRange && groundFrames >= groundStableFrames;
    }

    bool clusterWaitsForGround(bool evictFar, bool liveObjectsPage, bool squareWhollyInRange, std::int32_t groundFrames,
        std::int32_t missingFrames) noexcept
    {
        return evictFar && liveObjectsPage && !squareWhollyInRange && groundFrames < groundStableFrames
            && missingFrames < groundWaitFrames;
    }

    ClusterGroundStep stepClusterGround(ClusterGroundState& state, const ClusterGroundInput& input) noexcept
    {
        ClusterGroundStep step;
        if (!input.mRuleApplies)
        {
            state = ClusterGroundState{};
            return step;
        }
        // Wholly inside the paging range the live terrain draws the ground (groundDrawnOver is trivially true there),
        // so the count goes on and the frame the square leaves the range it is already stable.
        const bool drawn = input.mWholly || input.mGroundDrawn;
        state.mGroundFrames = stepGroundFrames(state.mGroundFrames, drawn);
        state.mMissingFrames = stepGroundFrames(state.mMissingFrames, !drawn);
        step.mFallback = clusterGroundFallback(true, true, input.mWholly, state.mGroundFrames);
        // A wait only helps when the budget is pressing: the cluster would otherwise be admitted as an item with no
        // fallback, loaded whatever the budget says, and given up again a moment later. With room to spare it is simply
        // loaded (the review of the far eviction, finding 1: the wait delayed every newly chosen cluster by half a
        // second at the tier default budget, which is never reached).
        step.mWaits = input.mPressure
            && clusterWaitsForGround(true, true, input.mWholly, state.mGroundFrames, state.mMissingFrames);
        return step;
    }

    bool budgetIsPressing(std::uint64_t heldAfterBytes, std::uint32_t blocked, std::uint64_t itemBytes,
        std::uint64_t budgetBytes) noexcept
    {
        if (budgetBytes == 0)
            return false;
        return blocked > 0 || addSaturating(heldAfterBytes, itemBytes) > budgetBytes;
    }

    double groundTestRange(double pagingRange) noexcept
    {
        // The most the eye can have moved when the kept answer is used: groundRetestDistance on each axis.
        return pagingRange - groundRetestDistance * std::sqrt(2.0);
    }

    GroundTestKey groundTestKeyFor(std::uint64_t version, double eyeX, double eyeY, double pagingRange) noexcept
    {
        return GroundTestKey{ true, version, eyeX, eyeY, pagingRange };
    }

    bool groundTestStale(
        const GroundTestKey& key, std::uint64_t version, double eyeX, double eyeY, double pagingRange) noexcept
    {
        // `!(a < b)` so that a position that is not a number is stale.
        return !key.mValid || key.mVersion != version || key.mPagingRange != pagingRange
            || !(std::abs(eyeX - key.mEyeX) < groundRetestDistance)
            || !(std::abs(eyeY - key.mEyeY) < groundRetestDistance);
    }
}
