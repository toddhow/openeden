#ifndef OPENMW_MWPHYSICS_NEUTRALSWEEP_H
#define OPENMW_MWPHYSICS_NEUTRALSWEEP_H

// Task 0171 (combat stage S5-3): the shape test of a melee swing, as a pure function. A SweepRequest (a forward arc in
// front of the attacker: reach, half angle, a height band) is tested against the upright capsules of the actors and the
// player in the one physics world; the nearest hits come back in a deterministic order. No Jolt, Ptr or ESM type, so a
// test drives it with plain numbers; PhysicsSystem fills the candidates from its neutral actors and the player.
//
// The test is geometric (a capsule against a wedge of a disc), not a Jolt shape cast: neutral actors and the player are
// upright capsules whose positions the physics system already holds, and a wedge test is exact and allocation free. It
// does not look for a wall between the attacker and the target (decision for toddhow in task 0171).
//
// Threading: main thread only (called from the writer's requestSweep inside combatUpdate). Allocation: none.

#include <openeden/game/combat_rules.hpp>

#include <cstddef>
#include <span>

namespace MWPhysics::NeutralSweep
{
    /// One body the arc can hit: an upright capsule standing at (x, y, z) with its feet at z.
    struct Candidate
    {
        openeden::sdk::game::SweepTarget mTarget = openeden::sdk::game::SweepTarget::Actor;
        openeden::sdk::game::ActorHandle mActor; ///< valid when mTarget == Actor
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        float mRadius = 0.f;
        float mHeight = 0.f; ///< feet to top, >= 2 * radius
    };

    /// The attacker, left out of the result.
    struct Self
    {
        bool mPlayer = false;
        openeden::sdk::game::ActorHandle mActor;
    };

    /// Fills `out` (room for at least kMaxSweepHits) with the candidates the arc touches, nearest first. Ties break by
    /// target kind (actors before the player), then by the actor's placement id and session, so the order never depends
    /// on the order of `candidates`. A candidate whose numbers are not finite is skipped. Returns the number written
    /// (at most min(out.size(), kMaxSweepHits)); `dropped`, when given, receives how many more would have qualified.
    std::size_t arcSweep(const openeden::sdk::game::SweepRequest& request, const Self& self,
        std::span<const Candidate> candidates, std::span<openeden::sdk::game::SweepHit> out,
        std::size_t* dropped = nullptr);
}

#endif
