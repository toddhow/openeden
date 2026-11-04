#ifndef OPENMW_COMPONENTS_LOD_RESIDENCY_H
#define OPENMW_COMPONENTS_LOD_RESIDENCY_H

// OpenEden world LOD (task 0060, the streaming core): which items of the far field (terrain tile draws, object
// clusters) are held in memory, which are loaded next and which are freed, given a memory budget. Pure code: no GPU,
// no clock, no thread, no allocation after the plan's vectors have grown once. One call depends only on its arguments;
// a run of the game is not repeatable plan for plan, because the clients' inputs (when a load completes, how far a
// fade has gone) depend on timing.
//
// The vocabulary:
//  - an item is a tile or a cluster; the caller keeps one per item, in a fixed order, and passes the same array every
//    frame (the index is the item's identity and the tie-break of every order below);
//  - wanted: the camera's selection wants it drawn this frame;
//  - replaceable: something else draws the same area when it is not held (the live objects or the live terrain, which
//    reach only the live paging range), so freeing it leaves no hole; a wanted item that is not replaceable is never
//    freed to make room, and it outranks replaceable ones when room is short;
//  - pinned: a hand-over is in progress that the client finishes itself (it is still fading, or the live objects have
//    not taken its cells back): it is never freed by the planner;
//  - leaving: the planner freed it in an earlier call and the client is handing it over; its bytes are still in
//    memory but already counted as gone (they are not committed), so they never cause more pressure;
//  - committed bytes: held items that are not leaving, items that are loading (their estimate) and the items this
//    plan starts loading;
//  - priority: how urgent the item is, lower is more urgent (the caller uses the distance to the camera in world
//    units; ties break by index; NaN is the least urgent value).
//
// The rules, in the order the planner applies them:
//  1. Idle items go. A held item that is not wanted, not pinned, not leaving and has been unwanted for at least
//     mKeepFrames frames is freed whatever the budget says. mKeepFrames is the hysteresis against freeing what the
//     camera is about to want again.
//  2. Loads. Items that are wanted, not held, not loading and out of cooldown are candidates: with mNearestFirst, the
//     ones with no fallback (not replaceable) first, then by priority, then index; without it, by index. They are
//     taken while fewer than mMaxLoadsInFlight loads run (loads already running count). With no budget every one is
//     admitted. With a budget, a candidate is admitted when the committed bytes minus what drawing it releases
//     (mReleasesBytes: the items it replaces) plus its estimate stay at or below the admission limit, mLowWater times
//     the budget: admission stops below the pressure line, so an estimate that turns out a little low does not start
//     pressure. If it does not fit, the planner looks for victims: held items that are not pinned or leaving and not
//     wanted (any priority), then wanted replaceable ones, which must be at least mSwapMargin times less urgent than
//     the candidate unless the candidate has no fallback. The victims are taken least urgent first and are freed only
//     if freeing them makes the candidate fit (all or nothing); otherwise the candidate is blocked and counted. After
//     mMaxBlockedPerCall blocked candidates the planner stops looking (a bound on the work of one call). A candidate
//     is always admitted when nothing is committed and nothing is loading, however large: an empty far field is worse
//     than one that is over budget.
//  3. Pressure. If the budget is set and the committed bytes exceed it, victims (the same kinds as above, any
//     priority) are freed, least urgent first, until the committed bytes are at most mLowWater times the budget. The
//     last committed item is never freed this way, so an item admitted by the empty-field rule stays (the plan then
//     says it is over budget). If the budget still cannot be met (everything left is wanted and not replaceable,
//     pinned, loading or the last one) the plan says it is over budget.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Lod
{
    struct ResidencyItem
    {
        /// Held: what the item holds. Not held: the estimate of what loading it will hold.
        std::uint64_t mBytes = 0;
        /// Held bytes of other items that go once this one is drawn (a parent tile replacing its pinned children).
        /// Admission subtracts it, so a replacement that lowers what is held is not blocked by what it replaces.
        std::uint64_t mReleasesBytes = 0;
        /// Lower is more urgent. NaN counts as the least urgent.
        float mPriority = 0;
        /// Frames since the item was last wanted; 0 while it is wanted.
        std::int32_t mIdleFrames = 0;
        /// Frames before an item that is not held may be requested again (after a failure or a free).
        std::int32_t mCooldown = 0;
        bool mHeld = false;
        bool mLoading = false;
        bool mWanted = false;
        bool mReplaceable = true;
        bool mPinned = false;
        /// Held, freed by an earlier plan and still being handed over (see the vocabulary above).
        bool mLeaving = false;
    };

    struct ResidencyParams
    {
        /// What may be held, in bytes; 0 means no budget.
        std::uint64_t mBudgetBytes = 0;
        /// Admission fills up to this share of the budget, and pressure frees down to it. In (0, 1]; values outside
        /// are clamped.
        float mLowWater = 0.9f;
        /// An unwanted item stays this many frames before rule 1 frees it. 0: at once.
        std::int32_t mKeepFrames = 0;
        /// A wanted replaceable victim must have a priority of at least this many times the candidate's (and more
        /// than it), so that two items of about the same urgency do not swap places. At least 1.
        float mSwapMargin = 1.25f;
        /// Loads that may run at once, those already loading included.
        std::uint32_t mMaxLoadsInFlight = 2;
        /// Loads the plan may start in one call.
        std::uint32_t mMaxLoadsPerCall = 2;
        /// Blocked candidates one call looks at before it stops (each costs a pass over the items).
        std::uint32_t mMaxBlockedPerCall = 8;
        /// Candidates by urgency (no fallback first, then priority); false: by index, the order the clients used
        /// before the planner (they pass false while no budget is set, so nothing changes then).
        bool mNearestFirst = true;
    };

    struct ResidencyPlan
    {
        std::vector<std::uint32_t> mFree; // indices to free, in the order they were chosen
        std::vector<std::uint32_t> mLoad; // indices to start loading, in the order they were taken
        std::uint64_t mHeldAfter = 0; // committed bytes once the plan is applied (frees done, loads counted)
        std::uint32_t mBlocked = 0; // candidates that did not fit
        bool mOverBudget = false;

    private:
        friend void planResidency(std::span<const ResidencyItem>, const ResidencyParams&, ResidencyPlan&);
        std::vector<std::uint32_t> mScratch;
        std::vector<std::uint32_t> mVictims;
        std::vector<std::uint8_t> mGone;
    };

    /// Fills `plan` (its vectors are cleared and reused; they allocate only while they grow to the item count).
    void planResidency(std::span<const ResidencyItem> items, const ResidencyParams& params, ResidencyPlan& plan);

    /// Is every point of the square [originX, originX + side] x [originY, originY + side] within `range` of (x, y),
    /// measured on the ground plane? The live terrain and the live objects page by the horizontal distance to a chunk
    /// (Terrain::Paging::updateView), so a far-field item is replaceable only when all of it lies inside the paging
    /// range. A range that is not positive or not finite gives false.
    bool squareWithinRange(double x, double y, double originX, double originY, double side, double range) noexcept;

    /// A byte count for the log: "640 KiB", "24 MiB", "1.5 MiB" (one decimal below 10 MiB when not whole), "512 bytes".
    std::string describeBytes(std::uint64_t bytes);
}

#endif
