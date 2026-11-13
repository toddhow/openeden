#ifndef OPENMW_MWPHYSICS_NEUTRALPROCESS_H
#define OPENMW_MWPHYSICS_NEUTRALPROCESS_H

// Task 0154 (decision 0017, stage S7 "mechanics-lite", process levels): how often a neutral actor's body is stepped,
// by how far it is from the player. Actors near the player step every frame (High); farther ones step on a longer
// period with the time they were skipped handed over at the step (Middle, Low), so the path they take is the same,
// walked in bigger strides.
//
// Pure: no Jolt, Ptr or ESM type, no clock (the caller gives the frame time), no randomness (the phase of each actor
// is a number the caller gives), no allocation. Main thread only (PhysicsSystem::updateNeutralActors).
//
// The numbers are NAMED ESTIMATES, not measurements of Skyrim: a Skyrim exterior cell is 4096 units, so High is the
// actor's neighbourhood of one cell around the player and Middle the second ring; an interior is smaller than High, so
// everyone in it is High. The periods are chosen so a walking actor (82 units a second) moves at most about 4 units
// (Middle) or 16 units (Low) between two steps.

#include <cstdint>

namespace MWPhysics::NeutralProcess
{
    enum class Level : std::uint8_t
    {
        High = 0,
        Middle = 1,
        Low = 2,
    };

    struct Config
    {
        float mHighRadius = 4096.f; ///< game units: up to this far from the player an actor is High
        float mMiddleRadius = 8192.f; ///< up to this far Middle, beyond Low
        float mMiddlePeriod = 1.f / 20.f; ///< seconds between two steps of a Middle actor
        float mLowPeriod = 1.f / 5.f; ///< seconds between two steps of a Low actor
        /// A level changes towards a nearer one at the radius and towards a farther one only this fraction beyond it,
        /// so an actor on a ring does not flicker between two levels.
        float mHysteresis = 0.1f;
    };

    /// The level for a squared horizontal distance, with no memory. A non-finite or negative value is Low.
    [[nodiscard]] Level classify(float distanceSquared, const Config& config);

    /// The level for a squared distance when the actor was `previous` last time (hysteresis).
    [[nodiscard]] Level reclassify(Level previous, float distanceSquared, const Config& config);

    /// Seconds between two steps of a level (0 for High: every frame).
    [[nodiscard]] float periodOf(Level level, const Config& config);

    /// One actor's step schedule. Allocation free.
    class Schedule
    {
    public:
        /// Starts the actor at `phase` in [0, 1) of its period, so actors of one level do not all step in one frame.
        /// A value outside the range (or not finite) is 0.
        void seed(float phase, Level level, const Config& config);

        /// Adds `dt` (a negative or non-finite value counts as 0) and decides: true when the body should step now,
        /// with `stepSeconds` the time to step by (all the time since the last step). High steps every frame with
        /// `dt`. When the actor's level changes to High while time is pending, the pending time is stepped at once.
        [[nodiscard]] bool advance(Level level, float dt, const Config& config, float& stepSeconds);

        [[nodiscard]] float pending() const { return mPending; }

    private:
        float mPending = 0.f;
    };
}

#endif
