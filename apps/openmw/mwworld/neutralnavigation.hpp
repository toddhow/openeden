#ifndef OPENMW_MWWORLD_NEUTRALNAVIGATION_H
#define OPENMW_MWWORLD_NEUTRALNAVIGATION_H

#include <optional>

#include <components/detournavigator/agentbounds.hpp>

#include <openeden/game/world_view.hpp>

namespace JPH
{
    class Shape;
}

// Task 0134 (milestone M-S3 stage 1, docs/design/skyrim-se-implementation-plan.md 4.4): what a neutral (game pack)
// world tells the navigator, the pure part.
//
// The navigator (components/detournavigator) already receives a neutral cell's objects (PhysicsSystem::addModelObject
// and addBoxObject are added by Scene's addObject), its land (Scene::loadCell's neutral branch) and nothing else. Two
// decisions are made here so that they can be tested without a world:
//
//  - the size of the agent the navmesh is built for. Morrowind's default (about 29 x 28 half extents, from the vanilla
//    human collision box) is too fat for a Skyrim interior: the Bannered Mare's stairs have a flat tread about 78 units
//    wide, and recast's erosion by 29 units removes them (measured in game, docs/tasks/0134-skyrim-navmesh.md). A neutral
//    session therefore builds its navmesh for `[Game] neutral pathfind half extents`.
//  - the water of a neutral exterior square, which Morrowind gives the navigator for every exterior and a neutral
//    exterior did not (Scene::loadCell leaves the global water state alone, task 0129).
//
// Main thread only. No state.

namespace MWWorld
{
    /// The agent the navmesh is built for. `configuredDefault` is Morrowind's `[Game] actor collision shape type` and
    /// `default actor pathfind half extents`; `physicsActor` is the actor's own physics bounds, given only for an actor
    /// that has a physics actor and stands in an interior (the existing rule); `neutralHalfExtents` is given only when
    /// the session is a game pack session (the store holds the neutral stubs) and then wins: same shape type as the default, the neutral half extents.
    [[nodiscard]] DetourNavigator::AgentBounds choosePathfindingAgentBounds(
        const DetourNavigator::AgentBounds& configuredDefault,
        const std::optional<DetourNavigator::AgentBounds>& physicsActor,
        const std::optional<vsg::vec3>& neutralHalfExtents);

    /// Whether a neutral object's collision mesh is given to the navigator. The pack's collision is the model's visual
    /// triangles (task 0110), which for plants, trees and carried things are leaf cards and clutter no actor is
    /// stopped by in the original game: flora, trees, items and lights are left out (trees: measured in Whiterun, they
    /// cut streets into islands as the stand-in boxes did, docs/tasks/0134 stage 2; a walker can still be stopped by a
    /// trunk through physics); everything else (statics, furniture, doors, containers, activators, unknown kinds) blocks
    /// as before. Physics is not affected.
    [[nodiscard]] bool neutralKindBlocksNavigation(openeden::sdk::game::BaseKind kind);

    /// Whether a neutral object's physics shape is the stand-in box (`Neutral::insertObjectPhysics`: the box of the
    /// bounds an object gets when its model gives no collision triangles, such as an effect or marker model with no
    /// surface). That box is only a guess for the physics body: it is not a surface that an actor walks on or is
    /// stopped by, and giving it to the navigator cut Whiterun's streets into islands (task 0134, docs/tasks/0134):
    /// 38 fire effects, 35 chimney smokes, markers and the stockade barricades near the Bannered Mare each left an
    /// unwalkable pillar of recast voxels. The box is the shape of a `Resource::makeBoxShape` (a Jolt box, perhaps
    /// shifted by a RotatedTranslatedShape); a model's own mesh is a mesh and never matches. Physics is not affected.
    [[nodiscard]] bool neutralShapeIsStandInBox(const JPH::Shape& shape);

    /// A water plane for the navigator: the side of the square it covers and its height.
    struct NeutralNavigatorWater
    {
        int mCellSize = 0;
        float mLevel = 0.f;
    };

    /// The water the navigator gets for a neutral exterior square, or nothing. Only a square that has water and a
    /// heightfield (Morrowind's rule: water without ground gets none) with a finite level and a positive size.
    [[nodiscard]] std::optional<NeutralNavigatorWater> neutralNavigatorWater(
        bool hasWater, bool hasHeightfield, float level, int cellSize);
}

#endif
