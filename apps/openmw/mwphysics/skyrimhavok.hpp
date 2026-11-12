#ifndef OPENMW_MWPHYSICS_SKYRIMHAVOK_H
#define OPENMW_MWPHYSICS_SKYRIMHAVOK_H

// Task 0144 (plan section 4.9): Jolt shapes from the `bhk` collision data of a Skyrim SE model. The pure half (reading
// the blocks into plain shapes) is components/nifscene/havok.hpp; this is the Jolt half, in the host layer for the same
// reason as skyrimshape.hpp. Only used with `[Game] neutral havok collision` (and `[Game] neutral world`); off, the
// visual-triangle path of skyrimshape.hpp is exactly what it was.
//
// The result is one static shape per model in the model's own frame (game units, Z up), like the visual-triangle
// shape: every body's node transform and rigid body transform is baked in. Several bodies, lists and transform shapes
// become a JPH::StaticCompoundShape.
//
// Threads: pure with respect to engine state (reads the Scene it is given, builds Jolt shapes, which is thread safe
// once the factory exists), so it may run on any thread the shape loader runs on.
//
// CLEAN ROOM: see docs/research/0144-havok-collision-provenance.md. No Havok SDK type or header is used.

#include <cstdint>
#include <string>

#include <Jolt/Jolt.h>

#include <vsg/core/ref_ptr.h>

#include <components/nifscene/scene.hpp>
#include <components/resource/physicsshapemanager.hpp>

namespace MWPhysics::SkyrimHavok
{
    struct BuildReport
    {
        std::uint32_t mBodies = 0; // bodies that became part of the shape
        std::uint32_t mBodiesNotSolid = 0; // bodies left out because the game does not collide with them
        std::uint32_t mJoltShapes = 0; // Jolt shapes made (leaves and compounds)
        std::uint32_t mHullsRefused = 0; // convex hulls Jolt could not build (flat, degenerate)
    };

    enum class Outcome : std::uint8_t
    {
        Shape, ///< `shape` is set
        NoCollision, ///< the model has bhk bodies and none of them is solid: it has no collision, as in the game
        Unusable, ///< no bhk data, or something this reader or Jolt could not represent: use the fallback
    };

    /// Builds the model's collision from `scene.mHavok` (read with SceneOptions::mReadHavok). `why` says what made it
    /// Unusable.
    Outcome makeShape(const NifScene::Scene& scene, vsg::ref_ptr<Resource::PhysicsShape>& shape, BuildReport& report,
        std::string& why);
}

#endif
