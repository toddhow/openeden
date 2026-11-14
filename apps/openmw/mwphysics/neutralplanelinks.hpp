#ifndef OPENMW_MWPHYSICS_NEUTRALPLANELINKS_H
#define OPENMW_MWPHYSICS_NEUTRALPLANELINKS_H

// Task 0159 (task 0135, stage S6e): links through the one-sided walls of a neutral (Skyrim) interior.
//
// The movement of a neutral actor (NeutralMover, and the player's MovementSolver) sweeps a capsule with back faces
// ignored, so a plane of triangles that all face one way stops what comes from its front and lets through what comes
// from its back. The Skyrim room shells and loft walls of the Bannered Mare are made of such planes (the west stairs
// end at one: the player walks through it, from the stairs into the room, and is stopped coming back). Recast, which
// builds the navigation mesh, has no notion of a side: it sees a solid wall, and the navmesh of the stairs ends in
// front of it, so a path to anything behind it does not exist, although a walker gets there.
//
// This module finds those crossings. For every wall triangle of an object it takes sample points along the wall's foot,
// finds the floor a walker stands on each side (a ray down), and asks the mover's own sweeper whether the capsule gets
// from one side to the other. Each passable crossing is a one-way link between the two floor points, which the game
// gives the navigator as an off-mesh connection (the same mechanism as a door's), so the navmesh agrees with the mover.
// Nothing here knows Jolt, the navigator, a Ptr or ESM: the world is the `World` interface below.
//
// Threading: main thread (Scene::addObject, in the navigator pass after all the cell's objects have a physics body).
// Allocation: the result vector only.
// Determinism: only the arguments decide anything.

#include "neutralmover.hpp"

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>

#include <cstddef>
#include <span>
#include <vector>

namespace MWPhysics::NeutralPlaneLinks
{
    /// A triangle whose normal tilts from the horizontal by less than this (z component of the unit normal) is a wall.
    inline constexpr float kWallMaxNormalZ = 0.25f;
    /// A wall triangle narrower than this on the ground plane is not sampled (a post, a sliver).
    inline constexpr float kMinWallWidth = 16.f;
    /// Distance along a wall between samples.
    inline constexpr float kSampleSpacing = 48.f;
    /// How far each end of a crossing is from the wall's plane: the capsule's radius and a margin, so the end lies
    /// where the navmesh (eroded by the agent's radius) has a polygon within reach of the connection's own radius.
    inline constexpr float kStandOff = 28.f;
    /// Links closer than this at both ends are one link.
    inline constexpr float kMergeDistance = 24.f;
    /// At most this many links come from one object (a long wall of a big shell).
    inline constexpr std::size_t kMaxLinksPerObject = 48;
    /// A floor lower or higher than the foot of the wall by more than this is not the floor of the wall (kStepUp and
    /// kStepDown of the mover).
    inline constexpr float kFloorReach = NeutralMover::kStepUp;

    struct Triangle
    {
        vsg::vec3 mV[3];
    };

    struct Link
    {
        vsg::vec3 mFrom; ///< feet on the floor on one side of the wall
        vsg::vec3 mTo; ///< feet on the floor on the other side
    };

    /// The world as this module sees it: the mover's sweeper, and whether the capsule stands somewhere with nothing in
    /// it.
    class World : public NeutralMover::Sweeper
    {
    public:
        [[nodiscard]] virtual bool fits(const vsg::vec3& feet) const = 0;
    };

    /// The passable one-sided crossings of the walls among `triangles` (world space, game units). A non-finite or
    /// degenerate triangle is skipped. Deterministic order (the triangles' order, then along each wall).
    [[nodiscard]] std::vector<Link> find(std::span<const Triangle> triangles, const World& world);
}

#endif
