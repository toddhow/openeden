#ifndef OPENMW_MWPHYSICS_SKYRIMSHAPE_H
#define OPENMW_MWPHYSICS_SKYRIMSHAPE_H

// Task 0110 (neutral world stage N3a-4b): collision from a Skyrim SE model's visual triangles. This is the Jolt half;
// the pure half (which triangles, with which caps) is components/nifscene/collision.hpp. It lives in the host layer
// because it needs both the format (components/nifscene) and the physics resource (components/resource, Jolt), which
// the layer rules keep apart below it; the physics shape manager gets it only as a format-blind hook
// (Resource::PhysicsShapeManager::setSceneShapeReader), set by the PhysicsSystem when `[Game] neutral world` is on.
//
// The shape is one static JPH::MeshShape per model (every node's transform already baked into the triangles, game
// units, Z up), cached by the shape manager under the model's path exactly like Morrowind's. A placement's scale is
// applied by MWPhysics::Object through a JPH::ScaledShape, and its rotation and double precision position by the body.
//
// Threads: pure with respect to engine state. It reads the bytes it is given, builds a Jolt shape (Jolt's shape
// creation is thread safe once the factory exists) and updates relaxed atomic counters, so it may run on any thread the
// Morrowind physics shape loader runs on. The Jolt shape is read-only after Create().

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <Jolt/Jolt.h>

#include <vsg/core/ref_ptr.h>

#include <components/nifscene/collision.hpp>
#include <components/resource/physicsshapemanager.hpp>

namespace MWPhysics::SkyrimShape
{
    /// The static mesh shape for a collision mesh, or null (with a reason in `error`) when the mesh is empty or Jolt
    /// refuses it. The mesh is cleaned of duplicate and degenerate triangles by Jolt when its first try fails.
    vsg::ref_ptr<Resource::PhysicsShape> makeShape(const NifScene::Collision::Mesh& mesh, std::string* error = nullptr);

    /// The hook for PhysicsShapeManager: accepts Skyrim SE files (header 20.2.0.7), reads them as a Nif::Scene,
    /// flattens them, builds the collision mesh and the shape.
    std::shared_ptr<const Resource::SceneShapeReader> makeSceneShapeReader();

    /// Process-wide totals for the log line of a neutral cell. Relaxed atomics: a count, not a synchronisation.
    struct Counters
    {
        std::atomic<std::uint64_t> mModels{ 0 }; // models read as scenes for collision (not cache hits)
        std::atomic<std::uint64_t> mShapes{ 0 }; // of those, models that got a Jolt shape
        std::atomic<std::uint64_t> mNoMesh{ 0 }; // models with no collision triangles (or over the cap)
        std::atomic<std::uint64_t> mOverCap{ 0 };
        std::atomic<std::uint64_t> mJoltRefused{ 0 };
        std::atomic<std::uint64_t> mFailed{ 0 }; // models whose read threw
        std::atomic<std::uint64_t> mTriangles{ 0 };
        std::atomic<std::uint64_t> mVertices{ 0 };
        std::atomic<std::uint64_t> mBytes{ 0 }; // Jolt's own size of the shapes
        std::atomic<std::uint64_t> mMicroseconds{ 0 }; // read, flatten, mesh and shape, all models together
        std::atomic<std::uint64_t> mMaxMicroseconds{ 0 };
        std::atomic<std::uint64_t> mTrianglesDropped{ 0 };
        std::atomic<std::uint64_t> mBlendedShapesLeftOut{ 0 };
    };

    Counters& counters();

    /// "Skyrim collision: 207 models read, ...": one line for the log.
    std::string describeCounters();
}

#endif
