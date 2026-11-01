#ifndef OPENEDEN_COMPONENTS_TESTS_CMPHYSICS_HOSTEDJOLTFIXTURE_H
#define OPENEDEN_COMPONENTS_TESTS_CMPHYSICS_HOSTEDJOLTFIXTURE_H

// Shared helpers for the HostedJoltWorld tests (task 0035, stage a).
//
// The tests play the part of the "host": they own a JPH::PhysicsSystem with its layer interfaces, a temp allocator
// and a job system, exactly as the game or Eden Studio would, and then hand the system to the adapter.

// Jolt requires Jolt.h before any other Jolt header.
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <components/cmphysics/hostedjoltworld.hpp>

#include <opas/physics/PhysicsWorld.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace CMPhysicsTest
{
    // Two object layers, like the smallest useful host: things that never move and things that do.
    constexpr JPH::ObjectLayer kLayerStatic = 0;
    constexpr JPH::ObjectLayer kLayerMoving = 1;
    constexpr JPH::BroadPhaseLayer kBroadPhaseStatic(0);
    constexpr JPH::BroadPhaseLayer kBroadPhaseMoving(1);

    // The host's step: 1/60 s, as in the game (mPhysicsDt).
    constexpr float kStepSeconds = 1.0f / 60.0f;

    // Earth gravity with Z up, in meters.
    constexpr float kGravityMps2 = 9.81f;

    // The game's gravity constant (Misc::Constants::GravityConst) and its length unit (UnitsPerMeter). The tests use a
    // round 70 for the unit, as the spec does, so that expected numbers are easy to check by hand.
    constexpr float kGameGravityConst = 8.96f;
    constexpr float kUnitsPerMeter = 70.0f;

    // Jolt's global state (allocator, factory, types) is once per process. components-tests' main() already does it;
    // this makes the tests work even if that changes, and never tears anything down because other tests share it.
    inline void ensureJoltInitialized()
    {
        if (JPH::Factory::sInstance != nullptr)
            return;
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    class TestBroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
    {
    public:
        unsigned int GetNumBroadPhaseLayers() const override { return 2; }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
        {
            return layer == kLayerStatic ? kBroadPhaseStatic : kBroadPhaseMoving;
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
        {
            return layer == kBroadPhaseStatic ? "STATIC" : "MOVING";
        }
#endif
    };

    // Static things do not collide with other static things; everything else collides.
    class TestObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
        {
            if (layer == kLayerStatic)
                return broadPhase == kBroadPhaseMoving;
            return true;
        }
    };

    class TestObjectPairs final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
        {
            return !(a == kLayerStatic && b == kLayerStatic);
        }
    };

    // Ray filters for the castRay tests: each one lets exactly one kind of body through.
    class OnlyObjectLayerFilter final : public JPH::ObjectLayerFilter
    {
    public:
        explicit OnlyObjectLayerFilter(JPH::ObjectLayer allowed)
            : mAllowed(allowed)
        {
        }

        bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer == mAllowed; }

    private:
        JPH::ObjectLayer mAllowed;
    };

    class OnlyBroadPhaseLayerFilter final : public JPH::BroadPhaseLayerFilter
    {
    public:
        explicit OnlyBroadPhaseLayerFilter(JPH::BroadPhaseLayer allowed)
            : mAllowed(allowed)
        {
        }

        bool ShouldCollide(JPH::BroadPhaseLayer layer) const override { return layer == mAllowed; }

    private:
        JPH::BroadPhaseLayer mAllowed;
    };

    // A PhysicsSystem plus everything the host owns around it. The adapter must be destroyed before this object.
    class HostWorld
    {
    public:
        explicit HostWorld(JPH::Vec3 gravity = JPH::Vec3(0.0f, 0.0f, -kGravityMps2))
            : mTempAllocator(kTempAllocatorBytes)
            , mJobSystem(JPH::cMaxPhysicsJobs)
        {
            ensureJoltInitialized();
            mSystem.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, mBroadPhaseLayers, mObjectVsBroadPhase,
                mObjectPairs);
            mSystem.SetGravity(gravity);
        }

        JPH::PhysicsSystem& system() { return mSystem; }
        JPH::BodyInterface& bodies() { return mSystem.GetBodyInterface(); }

        // The host's own PhysicsSystem::Update, one collision step, as the game's stepSimulation does.
        void update(float dtSeconds = kStepSeconds) { mSystem.Update(dtSeconds, 1, &mTempAllocator, &mJobSystem); }

        // A body the host creates itself, outside the adapter. Static boxes go on the static layer.
        JPH::BodyID addHostBox(JPH::Vec3 halfExtents, JPH::RVec3 position, bool isStatic = true)
        {
            const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(halfExtents);
            const JPH::BodyCreationSettings settings(shape, position, JPH::Quat::sIdentity(),
                isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic,
                isStatic ? kLayerStatic : kLayerMoving);
            return mSystem.GetBodyInterface().CreateAndAddBody(
                settings, isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        }

        // Every body currently in the system, in Jolt's order.
        std::vector<JPH::BodyID> allBodies() const
        {
            JPH::BodyIDVector ids;
            mSystem.GetBodies(ids);
            return std::vector<JPH::BodyID>(ids.begin(), ids.end());
        }

        // The bodies in the system that are not in `before` (used to find what the adapter created).
        std::vector<JPH::BodyID> bodiesAddedSince(const std::vector<JPH::BodyID>& before) const
        {
            std::vector<JPH::BodyID> result;
            for (const JPH::BodyID id : allBodies())
            {
                if (std::find(before.begin(), before.end(), id) == before.end())
                    result.push_back(id);
            }
            return result;
        }

        std::size_t numBodies() const { return mSystem.GetNumBodies(); }
        std::size_t numConstraints() const { return mSystem.GetConstraints().size(); }

    private:
        static constexpr unsigned int kMaxBodies = 256;
        static constexpr unsigned int kMaxBodyPairs = 1024;
        static constexpr unsigned int kMaxContactConstraints = 1024;
        // 10 MB is what Jolt's own HelloWorld sample uses; far more than these small scenes need.
        static constexpr unsigned int kTempAllocatorBytes = 10 * 1024 * 1024;

        TestBroadPhaseLayers mBroadPhaseLayers;
        TestObjectVsBroadPhase mObjectVsBroadPhase;
        TestObjectPairs mObjectPairs;
        JPH::TempAllocatorImpl mTempAllocator;
        JPH::JobSystemSingleThreaded mJobSystem;
        JPH::PhysicsSystem mSystem;
    };

    // Lets HostedJoltWorld::step() call the host's Update.
    class UpdateStepper final : public CMPhysics::SystemStepper
    {
    public:
        explicit UpdateStepper(HostWorld& host)
            : mHost(host)
        {
        }

        void update(float dtSeconds) override { mHost.update(dtSeconds); }

    private:
        HostWorld& mHost;
    };

    // A config with the test host's layers. unitsPerMeter 1 means "the host's world is in meters".
    inline CMPhysics::HostedWorldConfig makeConfig(
        float unitsPerMeter = 1.0f, CMPhysics::SystemStepper* stepper = nullptr, bool allowStatic = true)
    {
        CMPhysics::HostedWorldConfig config;
        config.unitsPerMeter = unitsPerMeter;
        config.stepSeconds = kStepSeconds;
        config.movingLayer = kLayerMoving;
        if (allowStatic)
            config.staticLayer = kLayerStatic;
        config.stepper = stepper;
        return config;
    }

    // Creates the adapter; the test fails loudly (null result) if create() refuses a valid config.
    inline std::unique_ptr<CMPhysics::HostedJoltWorld> makeAdapter(
        HostWorld& host, const CMPhysics::HostedWorldConfig& config, std::string* error = nullptr)
    {
        return CMPhysics::HostedJoltWorld::create(host.system(), config, error);
    }

    // Float view of a Jolt position (RVec3 is double precision in this project).
    inline JPH::Vec3 toFloat(JPH::RVec3 v)
    {
        return JPH::Vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ()));
    }

    inline bool isFiniteState(const opas::physics::BodyState& s)
    {
        const float values[] = { s.positionM.x, s.positionM.y, s.positionM.z, s.rotation.x, s.rotation.y, s.rotation.z,
            s.rotation.w, s.linearVelocityMps.x, s.linearVelocityMps.y, s.linearVelocityMps.z,
            s.angularVelocityRadps.x, s.angularVelocityRadps.y, s.angularVelocityRadps.z };
        return std::all_of(std::begin(values), std::end(values), [](float v) { return std::isfinite(v); });
    }

    inline opas::physics::BodyDesc sphereDesc(opas::physics::Vec3 position, float radiusM = 0.5f)
    {
        opas::physics::BodyDesc desc;
        desc.shape = opas::physics::SphereShape{ radiusM };
        desc.positionM = position;
        return desc;
    }

    inline opas::physics::BodyDesc boxDesc(opas::physics::Vec3 position, opas::physics::Vec3 halfExtents)
    {
        opas::physics::BodyDesc desc;
        desc.shape = opas::physics::BoxShape{ halfExtents };
        desc.positionM = position;
        return desc;
    }
}

#endif
