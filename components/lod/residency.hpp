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
//    reach only the live paging range), so freeing it leaves no hole. The governing rule (task 0060, second review):
//    an item with no fallback (not replaceable) is never blocked, never freed and never evicted; the budget governs
//    the replaceable items only, and an overrun the items with no fallback cause is reported, not fixed with holes;
//  - pinned: a hand-over is in progress that the client finishes itself (it is still fading, or the live objects have
//    not taken its cells back): it is never freed by the planner;
//  - leaving: the planner freed it in an earlier call and the client is handing it over; its bytes are still in
//    memory but already counted as gone (they are not committed), so they never cause more pressure;
//  - committed bytes: held items that are not leaving, items that are loading (their estimate) and the items this
//    plan starts loading, less what the loading and admitted ones release once drawn (mReleasesBytes);
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
//     admitted, and so is every candidate with no fallback, whatever the budget says. A replaceable candidate is
//     admitted when the committed bytes minus what drawing it releases (mReleasesBytes: the items it replaces) plus its
//     estimate stay at or below the admission limit, mLowWater times the budget: admission stops below the pressure
//     line, so an estimate that turns out a little low does not start pressure. If it does not fit, the planner looks
//     for victims: held items that are not pinned or leaving and not wanted (any priority), then wanted replaceable
//     ones at least mSwapMargin times less urgent than the candidate. The victims are taken least urgent first and are
//     freed only if freeing them makes the candidate fit (all or nothing); otherwise the candidate is blocked and
//     counted. A candidate whose loads failed before (mFailures) never looks for victims: it waits until it fits, so a
//     load that fails again costs no other item its place. At most mMaxBlockedPerCall victim searches run in one call
//     (each is a pass over the items); later candidates are still admitted when they fit without victims, and every
//     candidate left out is counted in mBlocked. A candidate is always admitted when nothing is committed, however
//     large: an empty far field is worse than one that is over budget.
//  3. Pressure. If the budget is set and the committed bytes (less what loading and admitted items release) exceed
//     it, victims (held, not pinned or leaving: unwanted ones, then wanted replaceable ones, any priority) are freed,
//     least urgent first, until the committed bytes are at most mLowWater times the budget. The last committed item is
//     never freed this way, so an item admitted by the empty-field rule stays (the plan then says it is over budget).
//     If the budget still cannot be met (everything left has no fallback, is pinned, loading or the last one) the plan
//     says it is over budget (mOverBudget, mHeldAfter), and the client reports it.

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
        /// Loads of it that failed since it last loaded: such a candidate is admitted only when it fits without
        /// freeing anything, so a load that fails again never costs another item its place (the client also waits
        /// longer after each failure, failureCooldownFrames).
        std::uint32_t mFailures = 0;
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
        /// Victim searches one call runs (each costs a pass over the items); candidates after them are still admitted
        /// when they fit without victims, and every one left out is counted.
        std::uint32_t mMaxBlockedPerCall = 8;
        /// Candidates by urgency (no fallback first, then priority); false: by index, the order the clients used
        /// before the planner (they pass false while no budget is set, so nothing changes then).
        bool mNearestFirst = true;
    };

    struct ResidencyPlan
    {
        std::vector<std::uint32_t> mFree; // indices to free, in the order they were chosen
        std::vector<std::uint32_t> mLoad; // indices to start loading, in the order they were taken
        // Committed bytes once the plan is applied (frees done, loads counted, releases subtracted).
        std::uint64_t mHeldAfter = 0;
        std::uint32_t mBlocked = 0; // replaceable candidates the budget left out this call
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

    /// squareWithinRange with hysteresis, for an item's `mReplaceable` from frame to frame (task 0060, second review):
    /// a square that was not replaceable (`previous` false) becomes so only when it lies within the range less a
    /// margin (replaceableMargin), and one that was stays so while it lies within the range itself. The margin is
    /// inside the range only, never outside: a square reaching past the range is never replaceable, since the live
    /// draws do not reach its far part and freeing it would leave a hole there.
    bool squareReplaceable(
        bool previous, double x, double y, double originX, double originY, double side, double range) noexcept;

    /// The hysteresis margin of squareReplaceable, in world units: 5 percent of the range, at least a quarter of a
    /// cell (2048 units), so a camera swaying at the edge does not flip the flag every frame.
    double replaceableMargin(double range) noexcept;

    /// Frames a client waits before asking again for an item whose load failed `failures` times in a row: the base,
    /// doubled for each further failure, at most 16 times the base. 0 failures: 0.
    std::int32_t failureCooldownFrames(std::int32_t baseFrames, std::uint32_t failures) noexcept;

    /// A byte count for the log: "640 KiB", "24 MiB", "1.5 MiB" (one decimal below 10 MiB when not whole), "512 bytes".
    std::string describeBytes(std::uint64_t bytes);
}

#endif
