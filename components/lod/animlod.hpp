#ifndef OPENMW_COMPONENTS_LOD_ANIMLOD_H
#define OPENMW_COMPONENTS_LOD_ANIMLOD_H

// OpenEden world LOD (task 0096, stage d): the animation LOD of the far (object paging) chunks. The pure part: how
// often an animated far object's controllers run, from its distance to the camera, and which objects run in a frame
// under a budget of controllers. Nothing here knows the scene graph; MWRender::AnimatedChunk and DistantObjects use it.
//
// An object runs at a rate that falls with distance: every frame (Full), every fourth (Quarter), every sixteenth
// (Sixteenth), and not at all beyond the stop distance (Stopped). The animation never loses its phase: the time of
// the frames an object skipped waits in AnimItem::pendingDt and is handed over, whole, at its next run, so an object
// that changes rate (or stops and starts again) shows the same pose as a copy running every frame would.
//
// The tier boundaries are fractions of the stop distance and are first guesses for a look, not measurements.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Lod
{
    enum class AnimTier : std::uint8_t
    {
        Full,
        Quarter,
        Sixteenth,
        Stopped
    };

    /// Where the rate drops: Full up to this share of the stop distance, then Quarter up to animQuarterEnd, then
    /// Sixteenth up to the stop distance.
    inline constexpr float animFullEnd = 0.25f;
    inline constexpr float animQuarterEnd = 0.6f;
    /// A tier is kept until the distance is this share past (or before) its boundary, so an object standing near a
    /// boundary does not change rate every frame.
    inline constexpr float animHysteresis = 0.1f;
    /// The periods of the tiers, in frames.
    inline constexpr unsigned animQuarterPeriod = 4;
    inline constexpr unsigned animSixteenthPeriod = 16;
    /// An object that has waited this many periods is run whatever its phase says (it was left out by the budget).
    inline constexpr unsigned animStarvationPeriods = 2;

    /// The frames between two runs of a tier; 0 for Stopped.
    constexpr unsigned animTierPeriod(AnimTier tier) noexcept
    {
        switch (tier)
        {
            case AnimTier::Full:
                return 1;
            case AnimTier::Quarter:
                return animQuarterPeriod;
            case AnimTier::Sixteenth:
                return animSixteenthPeriod;
            case AnimTier::Stopped:
                break;
        }
        return 0;
    }

    /// The tier of an object `distance` units from the camera, given the tier it had (for the hysteresis). A bad
    /// distance or stop distance stops the animation.
    inline AnimTier chooseAnimTier(float distance, AnimTier current, float stopDistance) noexcept
    {
        if (!std::isfinite(distance) || !std::isfinite(stopDistance) || stopDistance <= 0.f)
            return AnimTier::Stopped;
        const float boundary[3] = { animFullEnd * stopDistance, animQuarterEnd * stopDistance, stopDistance };
        int tier = static_cast<int>(current);
        while (tier < 3 && distance > boundary[tier] * (1.f + animHysteresis))
            ++tier;
        while (tier > 0 && distance < boundary[tier - 1] * (1.f - animHysteresis))
            --tier;
        return static_cast<AnimTier>(tier);
    }

    /// The distance the animation LOD uses: on the ground plane (the horizontal offset dx, dy between an object and
    /// the camera; the height difference is left out), so a camera high above the land, as in the whole-map view, does
    /// not stop animation that is near in the plane (task 0096 stage e, toddhow 2026-11-11). Computed in double, so
    /// a huge offset does not overflow; a non-finite offset gives a non-finite distance, which stops the animation.
    inline float animHorizontalDistance(double dx, double dy) noexcept
    {
        return static_cast<float>(std::sqrt(dx * dx + dy * dy));
    }

    /// A stable phase 0 to 15 for an object id (a reference number), so objects of one tier do not all run on the same
    /// frame. A mix of the id's bits (the finalizer of splitmix64); no state, no randomness.
    inline std::uint8_t animPhaseOf(std::uint64_t id) noexcept
    {
        id += 0x9e3779b97f4a7c15ull;
        id = (id ^ (id >> 30)) * 0xbf58476d1ce4e5b9ull;
        id = (id ^ (id >> 27)) * 0x94d049bb133111ebull;
        id ^= id >> 31;
        return static_cast<std::uint8_t>(id & 15u);
    }

    /// The start time, in seconds, of an animation group of an object: 0 for the group every controller of a model
    /// shares (phase group 0, as the live objects), and for any other phase group a stable value in [0, 3600) from the
    /// object id and the group, where the live objects (Anim::AutoPlay) take std::rand(): no unseeded randomness here.
    inline float animStartTime(std::uint64_t id, int phaseGroup) noexcept
    {
        if (phaseGroup == 0)
            return 0.f;
        std::uint64_t h
            = id * 0x9e3779b97f4a7c15ull + static_cast<std::uint64_t>(static_cast<std::uint32_t>(phaseGroup));
        h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
        h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
        h ^= h >> 31;
        return static_cast<float>(h % 3600000ull) / 1000.f;
    }

    /// What the planner keeps for one animated object between frames.
    struct AnimItem
    {
        /// Set by the caller each frame: the distance from the camera.
        float distance = 0.f;
        AnimTier tier = AnimTier::Stopped;
        /// Frames since the object last ran (stopped objects count as long waiting, so they run at once on return).
        std::uint16_t sinceRun = 0;
        /// How many controllers one run of the object costs (its share of the budget); at least 1.
        std::uint16_t controllers = 1;
        /// From animPhaseOf.
        std::uint8_t phase = 0;
        /// The game time that passed since the object last ran.
        double pendingDt = 0.0;
    };

    /// One object to run this frame, with the time to give its controllers.
    struct AnimRun
    {
        std::uint32_t index = 0;
        float dt = 0.f;
    };

    /// The controllers that may still run this frame. `total` is what a frame starts with: an object that costs more
    /// than all of it still runs, when it is the first of the frame.
    struct AnimBudget
    {
        unsigned total = 0;
        unsigned left = 0;
    };

    /// Plans one frame for one chunk's objects: adds `dt` to each object's waiting time, chooses its tier from
    /// `distance`, and appends to `runs` (without reallocating when it has room for items.size()) the objects that run
    /// this frame, charging their controllers to `budget`. Objects are visited from a start that moves with the frame
    /// number, so a budget that cannot run everyone leaves out a different object each frame. The caller gives the
    /// object `AnimRun::dt` (its waiting time, now cleared).
    inline void planAnimFrame(std::span<AnimItem> items, double dt, float stopDistance, unsigned frame,
        AnimBudget& budget, std::vector<AnimRun>& runs)
    {
        const std::size_t count = items.size();
        if (count == 0)
            return;
        const double step = std::isfinite(dt) && dt > 0.0 ? dt : 0.0;
        const std::size_t start = frame % count;
        for (std::size_t n = 0; n < count; ++n)
        {
            const std::size_t index = (start + n) % count;
            AnimItem& item = items[index];
            item.pendingDt += step;
            item.tier = chooseAnimTier(item.distance, item.tier, stopDistance);
            const unsigned period = animTierPeriod(item.tier);
            if (period == 0)
            {
                item.sinceRun = static_cast<std::uint16_t>(animStarvationPeriods * animSixteenthPeriod);
                continue;
            }
            if (item.sinceRun < 0xffff)
                ++item.sinceRun;
            const bool due = period == 1 || (frame + item.phase) % period == 0
                || item.sinceRun >= animStarvationPeriods * period;
            if (!due)
                continue;
            const unsigned cost = std::max<unsigned>(item.controllers, 1);
            if (cost > budget.left && budget.left != budget.total)
                continue;
            budget.left -= std::min(cost, budget.left);
            runs.push_back(AnimRun{ static_cast<std::uint32_t>(index), static_cast<float>(item.pendingDt) });
            item.pendingDt = 0.0;
            item.sinceRun = 0;
        }
    }
}

#endif
