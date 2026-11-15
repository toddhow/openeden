#ifndef OPENMW_MWPHYSICS_NEUTRALPROJECTILE_H
#define OPENMW_MWPHYSICS_NEUTRALPROJECTILE_H

// Task 0178 (combat stage S5-6, bow and arrows): the flight of one projectile, as pure functions. A Shot is a point
// with a velocity under constant gravity; step() advances it by one frame and says what it met first on the way: a
// static body (asked of a WorldRay, which the physics system answers with a Jolt ray cast), the capsule of an actor or
// of the player (analytic segment against capsule), or nothing. No Jolt, Ptr or ESM type: a test drives it with plain
// numbers, and NeutralCombatBridge owns the shots and tells the pack how they ended.
//
// Integration: semi-implicit Euler, one step per frame, with the whole step's segment tested for a hit (an arrow at
// 6,000 units a second moves 100 units a frame, so a point test would tunnel through everything). The arc is
// deterministic for a given sequence of frame times.
//
// Threading: main thread only. Allocation: none.

#include "neutralsweep.hpp"

#include <openeden/game/combat_rules.hpp>

#include <cstdint>
#include <span>

namespace MWPhysics::NeutralProjectile
{
    struct Vec
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    /// One projectile in flight.
    struct Shot
    {
        std::uint32_t mId = 0; ///< the core's number (the sink's handle), never 0 for a live shot
        openeden::sdk::game::CombatSubject mOwner;
        std::uint32_t mToken = 0; ///< the pack's number
        Vec mPosition;
        Vec mVelocity; ///< units a second
        float mGravity = 0.f; ///< units a second squared, pulling down
        float mRangeLeft = 0.f; ///< path length still to go
        float mTravelled = 0.f;
        float mAge = 0.f; ///< seconds in flight
        float mStickDepth = 0.f;
        float mStickSeconds = 0.f;
    };

    /// Answers whether the segment from p0 to p1 meets a static body (geometry, terrain, a closed door) and where, as
    /// the fraction along it in [0, 1].
    class WorldRay
    {
    public:
        virtual ~WorldRay() = default;
        virtual bool cast(const Vec& p0, const Vec& p1, double& fraction) = 0;
    };

    enum class Outcome : std::uint8_t
    {
        Flying = 0,
        HitActor = 1,
        HitPlayer = 2,
        HitWorld = 3,
        Expired = 4, ///< the range was used up
    };

    struct StepResult
    {
        Outcome mOutcome = Outcome::Flying;
        openeden::sdk::game::ActorHandle mActor; ///< when mOutcome == HitActor
        Vec mAt; ///< where it ended (the shot's new position when flying)
        Vec mDirection; ///< unit vector of the velocity at the end of the step (0,0,0 for a zero velocity)
    };

    /// The fraction in [0, 1] along p0 -> p1 at which the segment first touches the upright capsule (feet at
    /// candidate.mZ, radius and height as given), or a negative number for no touch (also for a non-finite input). A
    /// segment that starts inside the capsule touches at 0.
    [[nodiscard]] double segmentCapsule(const Vec& p0, const Vec& p1, const NeutralSweep::Candidate& capsule);

    /// Advances `shot` by `dt` seconds (non-finite or not above 0: no change, Flying). A shot never hits its owner
    /// (an actor owner's capsule, or the player when the owner is the player). The actor and world hits compete by
    /// distance along the step; a world hit that is nearer wins, a tie goes to the actor. `world` may be null (nothing
    /// static is hit). On a hit the shot's position is the hit point and its age and travelled path include the part
    /// step; on Expired the position is where the range ran out.
    StepResult step(Shot& shot, float dt, std::span<const NeutralSweep::Candidate> candidates, WorldRay* world);

    /// The speed of the shot now, units a second.
    [[nodiscard]] float speedOf(const Shot& shot);
}

#endif
