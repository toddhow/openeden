#ifndef CSM_PHYSICS_STUDIOWORLD_H
#define CSM_PHYSICS_STUDIOWORLD_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include <components/misc/constants.hpp>

#include "joltruntime.hpp"

namespace CSMPhysics
{
    /// Eden Studio's one Jolt world (task 0030; plan section 6.1, decision 0007). Lengths are Morrowind units, as the
    /// game's world, so debug lines land on the rendered geometry unconverted; extensions see it in meters through
    /// PhysicsServiceImpl and can only read it (decision 0003).
    ///
    /// Steps are fixed (1/60 s, the game's mPhysicsDt), run on the calling thread with a single-threaded job system and
    /// a preallocated temp allocator, and never read a clock: the same calls give the same results. Owns the Jolt
    /// system and every body; bodies are handed out as JPH::BodyID. Main thread only.
    class StudioWorld
    {
    public:
        static constexpr float sUnitsPerMeter = Constants::UnitsPerMeter;
        static constexpr float sStepSeconds = 1.0f / 60.0f;
        /// Equal to MWPhysics::Layers::WORLD and DYNAMIC_WORLD (apps/openmw/mwphysics/joltlayers.hpp), so a query mask
        /// means the same in the game and here.
        static constexpr JPH::ObjectLayer sLayerWorld = 1 << 0;
        static constexpr JPH::ObjectLayer sLayerDynamicWorld = 1 << 8;

        enum class Transport
        {
            Paused,
            Playing
        };

        /// Told around every step. beforeStep and afterStep may throw: the world logs it and goes on.
        class StepObserver
        {
        public:
            virtual ~StepObserver() = default;
            virtual void beforeStep(float dt) = 0;
            virtual void afterStep(float dt) = 0;
        };

        StudioWorld();
        ~StudioWorld();

        StudioWorld(const StudioWorld&) = delete;
        StudioWorld& operator=(const StudioWorld&) = delete;

        JPH::PhysicsSystem& system() { return mSystem; }

        /// \throws std::invalid_argument if a half extent is below Jolt's convex radius (0.05).
        JPH::BodyID addStaticBox(const JPH::RVec3& centre, const JPH::Vec3& halfExtents);
        /// \throws std::invalid_argument if radius <= 0.
        JPH::BodyID addDynamicSphere(const JPH::RVec3& centre, float radius);
        /// Unknown or already removed IDs do nothing.
        void removeBody(JPH::BodyID id);
        std::size_t bodyCount() const { return mBodies.size(); }
        /// The body's centre of mass; zero for an unknown ID.
        JPH::RVec3 position(JPH::BodyID id) const;
        /// Removes every body and sets stepIndex() to 0.
        void clear();

        Transport transport() const { return mTransport; }
        void play() { mTransport = Transport::Playing; }
        void pause() { mTransport = Transport::Paused; }
        /// One step, whatever the transport state.
        void stepOnce();
        /// One step if Playing. \return whether it stepped.
        bool tick();

        std::uint64_t stepIndex() const { return mStepIndex; }
        /// Grows whenever bodies are added or removed or a step runs.
        std::uint64_t revision() const { return mRevision; }

        void setStepObserver(StepObserver* observer) { mObserver = observer; }

    private:
        class BroadPhaseLayers;
        class ObjectVsBroadPhase;
        class ObjectPairs;

        // Declared first: Jolt must be running before, and while, the members below exist.
        JoltRuntime mRuntime;
        std::unique_ptr<BroadPhaseLayers> mBroadPhaseLayers;
        std::unique_ptr<ObjectVsBroadPhase> mObjectVsBroadPhase;
        std::unique_ptr<ObjectPairs> mObjectPairs;
        JPH::TempAllocatorImpl mTempAllocator;
        JPH::JobSystemSingleThreaded mJobSystem;
        JPH::PhysicsSystem mSystem;
        std::set<JPH::BodyID> mBodies;
        Transport mTransport = Transport::Paused;
        std::uint64_t mStepIndex = 0;
        std::uint64_t mRevision = 0;
        StepObserver* mObserver = nullptr;
    };
}

#endif
