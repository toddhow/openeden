#ifndef OPENMW_MWWORLD_NEUTRALNAVIGATION_H
#define OPENMW_MWWORLD_NEUTRALNAVIGATION_H

#include <optional>
#include <span>

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

    /// Task 0159: the same for a given kind of cell. In an INTERIOR the carried things (items: crates, plates, bottles
    /// on the floor) stop a walking capsule as much as a chair does (the mover steps onto them and is left perched on
    /// top: Lucia on a crate in the Bannered Mare cellar), so they are obstacles for the navmesh too; plants, trees and
    /// lights stay out. Exteriors are as `neutralKindBlocksNavigation` says.
    [[nodiscard]] bool neutralKindBlocksNavigationIn(openeden::sdk::game::BaseKind kind, bool interior);

    /// Whether a neutral object's physics shape is the stand-in box (`Neutral::insertObjectPhysics`: the box of the
    /// bounds an object gets when its model gives no collision triangles, such as an effect or marker model with no
    /// surface). That box is only a guess for the physics body: it is not a surface that an actor walks on or is
    /// stopped by, and giving it to the navigator cut Whiterun's streets into islands (task 0134, docs/tasks/0134):
    /// 38 fire effects, 35 chimney smokes, markers and the stockade barricades near the Bannered Mare each left an
    /// unwalkable pillar of recast voxels. The box is the shape of a `Resource::makeBoxShape` (a Jolt box, perhaps
    /// shifted by a RotatedTranslatedShape); a model's own mesh is a mesh and never matches. Physics is not affected.
    [[nodiscard]] bool neutralShapeIsStandInBox(const JPH::Shape& shape);

    /// Task 0157: the floor height under one end of a neutral door's off-mesh connection. A ray down from the point can
    /// fall into a crack between two floor meshes (the Bannered Mare's upstairs floor has seams: a ray at (-333, 417)
    /// reaches the ground floor 100 units lower, and the connection then started below the room it opens into and was
    /// useless). The caller casts a few rays round the point; this picks the hit nearest to the door's own height
    /// `doorZ` (a Skyrim door's origin is on its floor) among those within `kNeutralDoorFloorBand` of it, or nothing,
    /// in which case the caller uses `doorZ` and lets the navmesh snap. Hits that are not finite are ignored.
    inline constexpr float kNeutralDoorFloorBand = 64.f;
    [[nodiscard]] std::optional<float> chooseNeutralDoorFloor(float doorZ, std::span<const std::optional<float>> hits);

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

    /// Task 0159: the navmesh agent of a walking neutral actor whose capsule has radius `capsuleRadius`. The capsule is
    /// round, so the agent is a CYLINDER of that radius (recast's radius is then the capsule's own). The session's
    /// shape type (an axis aligned box by default) takes the box's corner, radius times the square root of two: for the
    /// 22 unit capsule 31 units, which erodes the 72 unit wide open stairs of the Bannered Mare to a single cell and
    /// loses the last steps of the west flight. The horizontal size is the larger of the capsule and the session's
    /// agent, as `MWPhysics::NeutralPath::agentHalfExtents` says.
    [[nodiscard]] DetourNavigator::AgentBounds neutralActorAgentBounds(
        const DetourNavigator::AgentBounds& session, float capsuleRadius);
}

#endif
