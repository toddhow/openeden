#include "studioworld.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
// clang-format on

#include <components/debug/debuglog.hpp>

namespace CSMPhysics
{
    namespace
    {
        // Sizes of the editor's world. Small: it holds test bodies for now (task 0030).
        constexpr unsigned cMaxBodies = 4096;
        constexpr unsigned cMaxBodyPairs = 4096;
        constexpr unsigned cMaxContactConstraints = 4096;
        // As the game: the temporary memory of an update is taken once, so no step allocates (physicssystem.cpp).
        constexpr int cTempAllocatorBytes = 10 * 1024 * 1024;
        constexpr int cMaxPhysicsJobs = 256;

        const JPH::BroadPhaseLayer sBroadPhaseStatic{ 0 };
        const JPH::BroadPhaseLayer sBroadPhaseMoving{ 1 };
        constexpr unsigned cNumBroadPhaseLayers = 2;
    }

    class StudioWorld::BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
    {
    public:
        unsigned int GetNumBroadPhaseLayers() const override { return cNumBroadPhaseLayers; }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
        {
            return layer == sLayerWorld ? sBroadPhaseStatic : sBroadPhaseMoving;
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
        {
            return layer == sBroadPhaseStatic ? "STATIC" : "MOVING";
        }
#endif
    };

    class StudioWorld::ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
        {
            // Static things only meet moving ones; moving things meet everything.
            return layer == sLayerWorld ? broadPhase == sBroadPhaseMoving : true;
        }
    };

    class StudioWorld::ObjectPairs final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
        {
            return !(a == sLayerWorld && b == sLayerWorld);
        }
    };

    StudioWorld::StudioWorld()
        : mBroadPhaseLayers(std::make_unique<BroadPhaseLayers>())
        , mObjectVsBroadPhase(std::make_unique<ObjectVsBroadPhase>())
        , mObjectPairs(std::make_unique<ObjectPairs>())
        , mTempAllocator(cTempAllocatorBytes)
        , mJobSystem(cMaxPhysicsJobs)
    {
        mSystem.Init(cMaxBodies, 0, cMaxBodyPairs, cMaxContactConstraints, *mBroadPhaseLayers, *mObjectVsBroadPhase,
            *mObjectPairs);
        // The game's gravity: Morrowind's constant, in units (physicssystem.cpp).
        mSystem.SetGravity(JPH::Vec3(0, 0, -Constants::GravityConst * sUnitsPerMeter));

        CMPhysics::HostedWorldConfig cmConfig;
        cmConfig.unitsPerMeter = sUnitsPerMeter;
        cmConfig.stepSeconds = sStepSeconds;
        cmConfig.movingLayer = sLayerDynamicWorld;
        cmConfig.staticLayer = sLayerWorld;
        cmConfig.maxBodies = cMaxBodies;
        cmConfig.scaleSystemTolerances = true; // Jolt's tolerances are in meters, this world is not
        std::string error;
        mCmWorld = CMPhysics::HostedJoltWorld::create(mSystem, cmConfig, &error);
        if (mCmWorld == nullptr)
            throw std::runtime_error("StudioWorld: the CreativeMotion adapter was refused: " + error);
    }

    StudioWorld::~StudioWorld() = default;

    opas::physics::PhysicsWorld& StudioWorld::cmWorld()
    {
        return *mCmWorld;
    }

    JPH::BodyID StudioWorld::addStaticBox(const JPH::RVec3& centre, const JPH::Vec3& halfExtents)
    {
        // A box thinner than its convex radius is not a valid Jolt shape.
        if (!(halfExtents.GetX() >= JPH::cDefaultConvexRadius) || !(halfExtents.GetY() >= JPH::cDefaultConvexRadius)
            || !(halfExtents.GetZ() >= JPH::cDefaultConvexRadius))
            throw std::invalid_argument("StudioWorld::addStaticBox: a half extent is below the convex radius");
        const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(halfExtents);
        const JPH::BodyCreationSettings settings(
            shape, centre, JPH::Quat::sIdentity(), JPH::EMotionType::Static, sLayerWorld);
        const JPH::BodyID id = mSystem.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        if (id.IsInvalid())
            throw std::runtime_error("StudioWorld::addStaticBox: the world has no room for another body");
        mBodies.insert(id);
        mSystem.OptimizeBroadPhase();
        ++mRevision;
        return id;
    }

    JPH::BodyID StudioWorld::addDynamicSphere(const JPH::RVec3& centre, float radius)
    {
        if (!(radius > 0.0f) || !std::isfinite(radius))
            throw std::invalid_argument("StudioWorld::addDynamicSphere: the radius must be positive");
        const JPH::RefConst<JPH::Shape> shape = new JPH::SphereShape(radius);
        const JPH::BodyCreationSettings settings(
            shape, centre, JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, sLayerDynamicWorld);
        const JPH::BodyID id = mSystem.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
        if (id.IsInvalid())
            throw std::runtime_error("StudioWorld::addDynamicSphere: the world has no room for another body");
        mBodies.insert(id);
        ++mRevision;
        return id;
    }

    void StudioWorld::removeBody(JPH::BodyID id)
    {
        if (mBodies.erase(id) == 0)
            return;
        JPH::BodyInterface& bodies = mSystem.GetBodyInterface();
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
        ++mRevision;
    }

    JPH::RVec3 StudioWorld::position(JPH::BodyID id) const
    {
        if (mBodies.find(id) == mBodies.end())
            return JPH::RVec3::sZero();
        return mSystem.GetBodyInterface().GetCenterOfMassPosition(id);
    }

    void StudioWorld::clear()
    {
        JPH::BodyInterface& bodies = mSystem.GetBodyInterface();
        for (const JPH::BodyID& id : mBodies)
        {
            bodies.RemoveBody(id);
            bodies.DestroyBody(id);
        }
        // Returns false only inside an adapter step (a CreativeMotion step listener); clear() must not be called there.
        mCmWorld->destroyAll();
        mBodies.clear();
        mStepIndex = 0;
        ++mRevision;
    }

    void StudioWorld::addPreStepHook(PreStepHook* hook)
    {
        if (hook == nullptr || std::find(mPreStepHooks.begin(), mPreStepHooks.end(), hook) != mPreStepHooks.end())
            return;
        mPreStepHooks.push_back(hook);
    }

    void StudioWorld::removePreStepHook(PreStepHook* hook)
    {
        mPreStepHooks.erase(std::remove(mPreStepHooks.begin(), mPreStepHooks.end(), hook), mPreStepHooks.end());
    }

    void StudioWorld::stepOnce()
    {
        // Indexed, not a range loop: a hook must not add or remove hooks, but a bad one cannot invalidate the loop.
        for (std::size_t i = 0; i < mPreStepHooks.size(); ++i)
        {
            try
            {
                mPreStepHooks[i]->beforeStep();
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Physics pre-step hook failed: " << e.what();
            }
            catch (...)
            {
                Log(Debug::Error) << "Physics pre-step hook failed";
            }
        }

        if (mObserver != nullptr)
        {
            try
            {
                mObserver->beforeStep(sStepSeconds);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Physics step observer failed before a step: " << e.what();
            }
            catch (...)
            {
                Log(Debug::Error) << "Physics step observer failed before a step";
            }
        }

        mCmWorld->beginStep();
        const JPH::EPhysicsUpdateError error = mSystem.Update(sStepSeconds, 1, &mTempAllocator, &mJobSystem);
        if (error != JPH::EPhysicsUpdateError::None)
            Log(Debug::Error) << "Physics step reported an error (capacity of the editor's world exceeded), code "
                              << static_cast<unsigned>(error);
        mCmWorld->endStep();
        ++mStepIndex;
        ++mRevision;

        if (mObserver != nullptr)
        {
            try
            {
                mObserver->afterStep(sStepSeconds);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Physics step observer failed after a step: " << e.what();
            }
            catch (...)
            {
                Log(Debug::Error) << "Physics step observer failed after a step";
            }
        }
    }

    bool StudioWorld::tick()
    {
        if (mTransport != Transport::Playing)
            return false;
        stepOnce();
        return true;
    }
}
