#ifndef OPENMW_COMPONENTS_LOD_RESIDENCY_H
#define OPENMW_COMPONENTS_LOD_RESIDENCY_H

// OpenEden world LOD (task 0060, the streaming core): which items of the far field (terrain tile draws, object
// clusters) are held in memory, which are loaded next and which are freed, given a memory budget. Pure code: no GPU,
// no clock, no thread, no allocation after the plan's vectors have grown once. The decision depends only on the items
// and the parameters of the call, never on timing, so two runs of the same camera path make the same plans.
//
// The vocabulary:
//  - an item is a tile or a cluster; the caller keeps one per item, in a fixed order, and passes the same array every
//    frame (the index is the item's identity and the tie-break of every order below);
//  - wanted: the camera's selection wants it drawn this frame;
//  - replaceable: something else draws the same area when it is not held (the live objects or the live terrain), so
//    freeing it leaves no hole; clusters and tile draws are replaceable, a wanted item that is not replaceable is
//    never freed to make room;
//  - pinned: a hand-over is in progress (it is still fading, or the live objects have not taken its cells back): it
//    is never freed whatever the pressure;
//  - priority: how urgent the item is, lower is more urgent (the caller uses the distance to the camera in world
//    units; ties break by index; NaN is the least urgent value).
//
// The rules, in the order the planner applies them:
//  1. Idle items go. A held item that is not wanted, not pinned and has been unwanted for at least mKeepFrames frames
//     is freed whatever the budget says. mKeepFrames is the hysteresis against freeing what the camera is about to
//     want again.
//  2. Loads. Items that are wanted, not held, not loading and out of cooldown are taken nearest first (by priority,
//     then index), while fewer than mMaxLoadsInFlight loads run (loads already running count). With no budget every
//     one is admitted. With a budget, an item is admitted when what is held plus its estimate fits; if not, the
//     planner looks for victims: held, unpinned items that are not wanted (any priority), then wanted replaceable
//     ones that are at least mSwapMargin times less urgent than the candidate. The victims are taken least urgent
//     first, and are freed only if freeing them makes the candidate fit (all or nothing); otherwise the candidate is
//     blocked and counted. An item is always admitted when nothing is held and nothing is loading, however large:
//     an empty far field is worse than one that is over budget.
//  3. Pressure. If the budget is set and what is held (loads included) still exceeds it, victims (the same kinds, but
//     any priority) are freed, least urgent first, until what is held is at most mLowWater times the budget. If the
//     budget still cannot be met (everything left is wanted and not replaceable, pinned or loading) the plan says it
//     is over budget.
// Held bytes are the sum of the bytes of held items and of items that are loading (their estimate).

#include <cstdint>
#include <span>
#include <vector>

namespace Lod
{
    struct ResidencyItem
    {
        /// Held: what the item holds. Not held: the estimate of what loading it will hold.
        std::uint64_t mBytes = 0;
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
    };

    struct ResidencyParams
    {
        /// What may be held, in bytes; 0 means no budget.
        std::uint64_t mBudgetBytes = 0;
        /// After pressure, held bytes go down to this share of the budget (not just under it), so the next frame
        /// does not free again. In (0, 1]; values outside are clamped.
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
    };

    struct ResidencyPlan
    {
        std::vector<std::uint32_t> mFree; // indices to free, in the order they were chosen
        std::vector<std::uint32_t> mLoad; // indices to start loading, most urgent first
        std::uint64_t mHeldAfter = 0; // held bytes once the plan is applied (frees done, loads counted)
        std::uint32_t mBlocked = 0; // candidates that did not fit
        bool mOverBudget = false;

    private:
        friend void planResidency(std::span<const ResidencyItem>, const ResidencyParams&, ResidencyPlan&);
        std::vector<std::uint32_t> mScratch;
        std::vector<std::uint32_t> mVictims;
        std::vector<std::uint8_t> mGone;
    };

    /// What the draw of one terrain tile holds in system memory, in bytes: its vertex arrays (position 12, normal 12,
    /// texture coordinates 8 and colour 12 bytes a vertex), 32-bit indices (6 for each of the (grid - 1)^2 squares) and
    /// the ground picture when the tile draws with one (`albedoBytes`, 0 otherwise). An estimate for the planner's
    /// budget (task 0060 compares it with the working set); a grid below 2 holds only the picture.
    inline constexpr std::uint64_t tileBytesPerVertex = 12 + 12 + 8 + 12;
    inline constexpr std::uint64_t tileBytesPerIndex = 4;
    std::uint64_t tileDrawResidentBytes(std::uint32_t gridSize, std::uint64_t albedoBytes) noexcept;

    /// Fills `plan` (its vectors are cleared and reused; they allocate only while they grow to the item count).
    void planResidency(std::span<const ResidencyItem> items, const ResidencyParams& params, ResidencyPlan& plan);
}

#endif
