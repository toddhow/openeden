#ifndef CSM_PHYSICS_STUDIOWORLD_H
#define CSM_PHYSICS_STUDIOWORLD_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

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

#include <components/cmphysics/hostedjoltworld.hpp>
#include <opas/physics/PhysicsWorld.hpp>

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

        /// Something that must act on the world just before a step, outside it (task 0065): the push sandbox applies
        /// its queued impulses here. Run by stepOnce() before the observer's beforeStep and before the adapter's
        /// beginStep, so a call into cmWorld() from here is not inside a step. May throw: the world logs it and goes
        /// on.
        class PreStepHook
        {
        public:
            virtual ~PreStepHook() = default;
            virtual void beforeStep() = 0;
        };

        StudioWorld();
        ~StudioWorld();

        StudioWorld(const StudioWorld&) = delete;
        StudioWorld& operator=(const StudioWorld&) = delete;

        JPH::PhysicsSystem& system() { return mSystem; }

        /// CreativeMotion's view of this world (task 0035, stage c): the same adapter the game uses, in meters, over
        /// the editor's own system, so a ragdoll built through it lives in the one world. Created with the world; its
        /// bodies are not counted by bodyCount() and are all removed by clear(). Step listeners run inside stepOnce().
        opas::physics::PhysicsWorld& cmWorld();

        /// \throws std::invalid_argument if a half extent is below Jolt's convex radius (0.05).
        JPH::BodyID addStaticBox(const JPH::RVec3& centre, const JPH::Vec3& halfExtents);
        /// \throws std::invalid_argument if radius <= 0.
        JPH::BodyID addDynamicSphere(const JPH::RVec3& centre, float radius);
        /// Unknown or already removed IDs do nothing.
        void removeBody(JPH::BodyID id);
        std::size_t bodyCount() const { return mBodies.size(); }
        /// True while  id is a body this world added and has not removed (clear() removes them all).
        bool hasBody(JPH::BodyID id) const { return mBodies.find(id) != mBodies.end(); }
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

        /// Hooks run in registration order. Not owned: the owner removes its hook before it is destroyed. Adding one
        /// twice, or removing one that is not there, does nothing. Not to be called from inside a hook.
        void addPreStepHook(PreStepHook* hook);
        void removePreStepHook(PreStepHook* hook);

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
        // After the system: it keeps a reference to it and removes its bodies from it when destroyed.
        std::unique_ptr<CMPhysics::HostedJoltWorld> mCmWorld;
        std::set<JPH::BodyID> mBodies;
        Transport mTransport = Transport::Paused;
        std::uint64_t mStepIndex = 0;
        std::uint64_t mRevision = 0;
        StepObserver* mObserver = nullptr;
        std::vector<PreStepHook*> mPreStepHooks;
    };
}

#endif
