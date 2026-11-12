#include "physicssystem.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/SimShapeFilter.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/resource/niffilemanager.hpp>
#include <components/resource/physicsshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/refdata.hpp"

#include "../mwrender/joltdebugdraw.hpp"

#include "../mwworld/class.hpp"

#include "actor.hpp"
#include "joltlayers.hpp"

#include "heightfield.hpp"
#include "movementsolver.hpp"
#include "mtphysics.hpp"
#include "neutralactor.hpp"
#include "neutralmover.hpp"
#include "object.hpp"
#include "projectile.hpp"
#include "skyrimshape.hpp"
#include "ragdollhost.hpp"
#include "water.hpp"

#include "joltcallbacks.hpp"
#include "joltfilters.hpp"
#include "joltlisteners.hpp"

using namespace JPH;

// Callback for traces
static void TraceImpl(const char* inFMT, ...)
{
    // Format the message
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);

    Log(Debug::Info) << "Jolt Trace: " << buffer;
}

#ifdef JPH_ENABLE_ASSERTS

// Callback for Jolt asserts
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint inLine)
{
    Log(Debug::Error) << "Jolt Assert: " << inFile << ":" << inLine << ": (" << inExpression << ") "
                      << (inMessage != nullptr ? inMessage : "");

    // Prevent breakpoint, better to log than exit/crash in debug mode usually
    // Jolt has a tendancy to complain about alot of things even if they work fine
    return true;
}

#endif

namespace
{
    class AlwaysCollideSimShapeFilter final : public JPH::SimShapeFilter
    {
    public:
        bool ShouldCollide(const JPH::Body&, const JPH::Shape*, const JPH::SubShapeID&, const JPH::Body&,
            const JPH::Shape*, const JPH::SubShapeID&) const override
        {
            return true;
        }
    };

    AlwaysCollideSimShapeFilter sSimShapeFilter;

    void simCollideBodyVsBody(const JPH::Body& inBody1, const JPH::Body& inBody2,
        JPH::Mat44Arg inCenterOfMassTransform1, JPH::Mat44Arg inCenterOfMassTransform2,
        JPH::CollideShapeSettings& ioCollideShapeSettings, JPH::CollideShapeCollector& ioCollector,
        const JPH::ShapeFilter& inShapeFilter)
    {
        // Sensors only need overlap information, so a single deepest hit is enough.
        if (inBody1.IsSensor() || inBody2.IsSensor())
        {
            JPH::ClosestHitCollisionCollector<JPH::CollideShapeCollector> closestHitCollector;
            JPH::PhysicsSystem::sDefaultSimCollideBodyVsBody(inBody1, inBody2, inCenterOfMassTransform1,
                inCenterOfMassTransform2, ioCollideShapeSettings, closestHitCollector, inShapeFilter);
            if (closestHitCollector.HadHit())
                ioCollector.AddHit(closestHitCollector.mHit);
            return;
        }

        JPH::PhysicsSystem::sDefaultSimCollideBodyVsBody(inBody1, inBody2, inCenterOfMassTransform1,
            inCenterOfMassTransform2, ioCollideShapeSettings, ioCollector, inShapeFilter);
    }

    constexpr std::array<JPH::ObjectLayer, 12> sRaycastObjectLayers
        = { MWPhysics::Layers::WORLD, MWPhysics::Layers::DOOR, MWPhysics::Layers::ACTOR, MWPhysics::Layers::HEIGHTMAP,
              MWPhysics::Layers::PROJECTILE, MWPhysics::Layers::WATER, MWPhysics::Layers::CAMERA_ONLY,
              MWPhysics::Layers::VISUAL_ONLY, MWPhysics::Layers::DYNAMIC_WORLD, MWPhysics::Layers::DEBRIS,
              MWPhysics::Layers::SENSOR, MWPhysics::Layers::RAGDOLL };

    int getQueryObjectMask(int mask, int group)
    {
        int result = 0;
        MWPhysics::JoltObjectLayerPairFilter layerPairFilter;

        for (const JPH::ObjectLayer objectLayer : sRaycastObjectLayers)
        {
            if ((mask & static_cast<int>(objectLayer)) == 0)
                continue;

            if (group != 0xff)
            {
                bool groupMatches = false;
                for (const JPH::ObjectLayer queryLayer : sRaycastObjectLayers)
                {
                    if ((group & static_cast<int>(queryLayer)) == 0)
                        continue;
                    if (layerPairFilter.ShouldCollide(queryLayer, objectLayer))
                    {
                        groupMatches = true;
                        break;
                    }
                }

                if (!groupMatches)
                    continue;
            }

            result |= static_cast<int>(objectLayer);
        }

        return result;
    }

    std::uint32_t getQueryBroadPhaseMask(int objectMask, const MWPhysics::JoltBPLayerInterface& bpLayerInterface)
    {
        std::uint32_t result = 0;
        for (const JPH::ObjectLayer objectLayer : sRaycastObjectLayers)
        {
            if ((objectMask & static_cast<int>(objectLayer)) == 0)
                continue;
            const JPH::BroadPhaseLayer broadphaseLayer = bpLayerInterface.GetBroadPhaseLayer(objectLayer);
            const auto broadPhaseLayerIndex = static_cast<std::uint32_t>(broadphaseLayer.GetValue());
            if (broadPhaseLayerIndex < 32)
                result |= 1u << broadPhaseLayerIndex;
        }

        return result;
    }

    void handleJump(const MWWorld::Ptr& ptr)
    {
        if (!ptr.getClass().isActor())
            return;
        if (ptr.getClass().getMovementSettings(ptr).mPosition[2] == 0)
            return;
        const bool isPlayer = (ptr == MWMechanics::getPlayer());
        // Advance acrobatics and set flag for GetPCJumping
        if (isPlayer)
        {
            ptr.getClass().skillUsageSucceeded(ptr, ESM::Skill::Acrobatics, ESM::Skill::Acrobatics_Jump);
            MWBase::Environment::get().getWorld()->getPlayer().setJumping(true);
        }

        // Decrease fatigue
        if (!isPlayer || !MWBase::Environment::get().getWorld()->getGodModeState())
        {
            const MWWorld::Store<ESM::GameSetting>& gmst
                = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();
            const float fFatigueJumpBase = gmst.find("fFatigueJumpBase")->mValue.getFloat();
            const float fFatigueJumpMult = gmst.find("fFatigueJumpMult")->mValue.getFloat();
            const float normalizedEncumbrance = std::min(1.f, ptr.getClass().getNormalizedEncumbrance(ptr));
            const float fatigueDecrease = fFatigueJumpBase + normalizedEncumbrance * fFatigueJumpMult;
            MWMechanics::DynamicStat<float> fatigue = ptr.getClass().getCreatureStats(ptr).getFatigue();
            fatigue.setCurrent(fatigue.getCurrent() - fatigueDecrease);
            ptr.getClass().getCreatureStats(ptr).setFatigue(fatigue);
        }
        ptr.getClass().getMovementSettings(ptr).mPosition[2] = 0;
    }

}

namespace MWPhysics
{
    bool JoltContactListener::isActiveProjectile(const PtrHolder* ptr)
    {
        const auto* projectile = dynamic_cast<const Projectile*>(ptr);
        return projectile != nullptr && projectile->isActive();
    }

    bool JoltContactListener::isActorOrProjectile(const PtrHolder* ptr)
    {
        return dynamic_cast<const Projectile*>(ptr) != nullptr || dynamic_cast<const Actor*>(ptr) != nullptr;
    }

    // If you take larger steps than 1 / 60th of a second you need to do multiple collision steps in order to keep the
    // simulation stable. Do 1 collision step per 1 / 60th of a second (round up).
    static const int cCollisionSteps = 1;

    // This is the max amount of rigid bodies that you can add to the physics system
    static const unsigned int cMaxBodies = 65536;

    // This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the
    // default settings.
    static const unsigned int cNumBodyMutexes = 0;

    // This is the max amount of body pairs that can be queued at any time (the broad phase will detect overlapping
    // body pairs based on their bounding boxes and will insert them into a queue for the narrowphase). If you make this
    // buffer too small the queue will fill up and the broad phase jobs will start to do narrow phase work. This is
    // slightly less efficient.
    static const unsigned int cMaxBodyPairs = 65536;

    // This is the maximum size of the contact constraint buffer. If more contacts (collisions between bodies) are
    // detected than this number then these contacts will be ignored and bodies will start interpenetrating / fall
    // through the world.
    static const unsigned int cMaxContactConstraints = 10240;

    namespace
    {
        LockingPolicy detectLockingPolicy()
        {
            if (Settings::physics().mAsyncNumThreads < 1)
                return LockingPolicy::NoLocks;

            return LockingPolicy::AllowSharedLocks;
        }

        unsigned getNumThreads(LockingPolicy lockingPolicy)
        {
            switch (lockingPolicy)
            {
                case LockingPolicy::NoLocks:
                    return 0;
                case LockingPolicy::AllowSharedLocks:
                {
                    const int configuredThreads = Settings::physics().mAsyncNumThreads;
                    const unsigned requested = configuredThreads > 0 ? static_cast<unsigned>(configuredThreads) : 0u;
                    return std::min(requested, std::thread::hardware_concurrency());
                }
            }

            throw std::runtime_error("Unsupported LockingPolicy: "
                + std::to_string(static_cast<std::underlying_type_t<LockingPolicy>>(lockingPolicy)));
        }
    }

    PhysicsSystem::PhysicsSystem(Resource::ResourceSystem* resourceSystem, const ToUTF8::StatelessUtf8Encoder* encoder)
        : mNifFileManager(std::make_unique<Resource::NifFileManager>(resourceSystem->getVFS(), encoder))
        , mShapeManager(std::make_unique<Resource::PhysicsShapeManager>(
              resourceSystem->getVFS(), mNifFileManager.get(), Settings::cells().mCacheExpiryDelay))
        , mResourceSystem(resourceSystem)
        , mDebugDrawEnabled(false)
        , mTimeAccum(0.0f)
        , mProjectileId(0)
        , mWaterHeight(0)
        , mWaterEnabled(false)
        , mPhysicsDt(1.f / 60.f)
    {
        // Task 0110 (neutral world stage N3a-4b): with the neutral world on, a Skyrim SE model gets a collision mesh
        // from its visual triangles. Set before any shape is requested; off, the manager is exactly what it was.
        if (Settings::game().mNeutralWorld)
            mShapeManager->setSceneShapeReader(SkyrimShape::makeSceneShapeReader(Settings::game().mNeutralHavokCollision));

        // Register allocation hook and callbacks
        JPH::RegisterDefaultAllocator();
        JPH::Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

        // Create a factory
        JPH::Factory::sInstance = new JPH::Factory();

        // Register all Jolt physics types
        JPH::RegisterTypes();

        // Mark this as main thread for profiling
        JPH_PROFILE_START("Main");

        // Check if a user decided to override a physics system FPS
        if (const char* env = getenv("OPENMW_PHYSICS_FPS"))
        {
            if (const auto physFramerate = Misc::StringUtils::toNumeric<float>(env);
                physFramerate.has_value() && *physFramerate > 0)
            {
                mPhysicsDt = 1.f / *physFramerate;
                Log(Debug::Warning) << "Warning: using custom physics framerate (" << *physFramerate << " FPS).";
            }
        }

        // We need a temp allocator for temporary allocations during the physics update.
        // Pre-allocatings 25 MB to avoid having to do allocations during the physics update.
        mMemoryAllocator = std::make_unique<JPH::TempAllocatorImpl>(25 * 1024 * 1024);

        // Now we can create the actual physics system.
        mPhysicsSystem.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, mBPLayerInterface,
            mObjectVsBPLayerFilter, mObjectVsObjectLayerFilter);
        mPhysicsSystem.SetSimShapeFilter(&sSimShapeFilter);
        mPhysicsSystem.SetSimCollideBodyVsBody(simCollideBodyVsBody);
        mPhysicsSystem.SetContactListener(&mContactListener);
        mPhysicsSystem.SetGravity(JPH::Vec3(0, 0, Constants::GravityConst * Constants::UnitsPerMeter));

        // Detect number of wanted async threads, if 0 then use single threaded job system
        unsigned numThreads = getNumThreads(detectLockingPolicy());
        if (numThreads > 0)
        {
            mPhysicsJobSystem
                = std::make_unique<JPH::JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, numThreads);
        }
        else
        {
            mPhysicsJobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(cMaxPhysicsJobs);
        }

        // Create a job scheduler responsible for simulating the game world (actors etc)
        mTaskScheduler = std::make_unique<PhysicsTaskScheduler>(
            mPhysicsDt, &mPhysicsSystem, getJoltDebugDrawer(), mPhysicsJobSystem.get());
    }

    PhysicsSystem::~PhysicsSystem()
    {
        Log(Debug::Info) << "Destroying physics system...";

        // The ragdoll adapter's bodies go before anything else, while the system is still whole.
        mRagdollHost.reset();
        mActorRagdolls.clear();

        mWaterInstance.reset();
        mTaskScheduler->releaseSharedStates();
        mHeightFields.clear();
        mObjects.clear();
        mNeutralBridge.clear(); // silently: the pack may be unloaded already
        mNeutralActors.clear();
        mActors.clear();
        mProjectiles.clear();

        // Pre-emptive end jobs system
        mPhysicsJobSystem.reset();

        // Unregisters all types with the factory and cleans up the defaults
        UnregisterTypes();

        // Destroy the factory
        delete Factory::sInstance;
        Factory::sInstance = nullptr;

        // Signal to end profiling
        JPH_PROFILE_END();
    }

    Resource::PhysicsShapeManager* PhysicsSystem::getShapeManager()
    {
        return mShapeManager.get();
    }

    void PhysicsSystem::pruneCache()
    {
        // The caches compare against a reference time in seconds; vsgopenmw's renderer keeps no frame clock to share.
        const double referenceTime
            = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        mShapeManager->updateCache(referenceTime);
        mNifFileManager->updateCache(referenceTime);
    }

    bool PhysicsSystem::toggleDebugRendering()
    {
        mDebugDrawEnabled = !mDebugDrawEnabled;

        if (mJoltDebugDrawer)
            mJoltDebugDrawer->setDebugMode(mDebugDrawEnabled);
        return mDebugDrawEnabled;
    }

    bool PhysicsSystem::spawnDebugRagdoll(const MWWorld::ConstPtr& ptr, std::string& message)
    {
        if (!Settings::physics().mCreativeMotionRagdolls)
        {
            message = "creativemotion ragdolls is off (see [Physics] in settings.cfg)";
            return false;
        }

        // Bodies are added while the actor job may be querying: take the same lock stepSimulation takes.
        std::scoped_lock lock(mTaskScheduler->getSimulationMutex());

        if (mRagdollHost == nullptr)
        {
            mRagdollHost = RagdollHost::create(mPhysicsSystem, Constants::UnitsPerMeter, mPhysicsDt, &message);
            if (mRagdollHost == nullptr)
                return false;
        }

        const ESM::Position& position = ptr.getRefData().getPosition();
        if (!mRagdollHost->spawn(
                JPH::RVec3(position.pos[0], position.pos[1], position.pos[2]), position.rot[2], &message))
            return false;

        mRagdollHost->setProgressLogging(true);
        const RagdollStatus status = mRagdollHost->status();
        Log(Debug::Info) << "Ragdoll: " << status.bodies << " bodies at " << position.pos[0] << " " << position.pos[1]
                         << " " << position.pos[2];
        message = "Ragdoll spawned";
        return true;
    }

    void PhysicsSystem::removeDebugRagdoll()
    {
        if (mRagdollHost == nullptr)
            return;
        std::scoped_lock lock(mTaskScheduler->getSimulationMutex());
        mRagdollHost->remove();
    }

    bool PhysicsSystem::startActorRagdoll(
        const MWWorld::Ptr& ptr, ActorRagdollHandle& handle, std::string& message, const ActorStartPose* pose,
        const double* velocityUnits)
    {
        if (!Settings::physics().mCreativeMotionRagdolls)
        {
            message = "creativemotion ragdolls is off";
            return false;
        }
        if (mActors.find(ptr.mRef) == mActors.end())
        {
            message = "the actor has no physics body";
            return false;
        }
        if (mActorRagdolls.find(ptr.mRef) != mActorRagdolls.end())
        {
            message = "the actor already has a ragdoll";
            return false;
        }

        // Bodies are added while the actor job may be querying: take the same lock stepSimulation takes.
        std::scoped_lock lock(mTaskScheduler->getSimulationMutex());

        if (mRagdollHost == nullptr)
        {
            mRagdollHost = RagdollHost::create(mPhysicsSystem, Constants::UnitsPerMeter, mPhysicsDt, &message);
            if (mRagdollHost == nullptr)
                return false;
        }

        const ESM::Position& position = ptr.getRefData().getPosition();
        // The actor's own velocity (inertia, including a fall), game units per second.
        const vsg::vec3& force = mActors.find(ptr.mRef)->second->getInertialForce();
        // The caller may know better (how the actor moved while it lived: the inertia is zero on the ground).
        const double velocity[3] = { velocityUnits != nullptr ? velocityUnits[0] : force.x,
            velocityUnits != nullptr ? velocityUnits[1] : force.y,
            velocityUnits != nullptr ? velocityUnits[2] : force.z };
        handle = mRagdollHost->spawnActor(
            JPH::RVec3(position.pos[0], position.pos[1], position.pos[2]), position.rot[2], velocity, &message, pose);
        if (!handle.isValid())
            return false;
        // Developer trace: where every actor ragdoll is, about every two seconds, in the log (task 0106).
        if (std::getenv("OPENEDEN_RAGDOLL_TRACE") != nullptr)
            mRagdollHost->setProgressLogging(true);
        mActorRagdolls.emplace(ptr.mRef, handle);
        return true;
    }

    bool PhysicsSystem::readActorRagdoll(ActorRagdollHandle handle, std::span<RagdollBoneState> out) const
    {
        return mRagdollHost != nullptr && mRagdollHost->readActor(handle, out);
    }

    bool PhysicsSystem::hasActorRagdoll(ActorRagdollHandle handle) const
    {
        return mRagdollHost != nullptr && mRagdollHost->hasActor(handle);
    }

    bool PhysicsSystem::actorRagdollAtRest(ActorRagdollHandle handle, double pelvis[3]) const
    {
        if (mRagdollHost == nullptr || !mRagdollHost->hasActor(handle) || !mRagdollHost->actorStatus(handle).allAsleep)
            return false;
        std::array<RagdollBoneState, 2 * cMaxStartPoseBones> states;
        const std::span<const std::string> names = mRagdollHost->actorBodyBones();
        if (names.size() > states.size() || !mRagdollHost->readActor(handle, states))
            return false;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (names[i] != "Pelvis")
                continue;
            for (int k = 0; k < 3; ++k)
                pelvis[k] = states[i].mPosition[k];
            return true;
        }
        return false;
    }

    bool PhysicsSystem::groundBelow(const vsg::vec3& from, float maxDrop, float& groundZ) const
    {
        const RayCastingResult result = castRay(from, vsg::vec3(from.x, from.y, from.z - maxDrop), {}, {},
            Layers::WORLD | Layers::HEIGHTMAP | Layers::DOOR);
        if (!result.mHit)
            return false;
        groundZ = result.mHitPos.z;
        return true;
    }

    void PhysicsSystem::stopActorRagdoll(const MWWorld::Ptr& ptr)
    {
        const auto found = mActorRagdolls.find(ptr.mRef);
        if (found == mActorRagdolls.end())
            return;
        if (mRagdollHost != nullptr)
        {
            std::scoped_lock lock(mTaskScheduler->getSimulationMutex());
            mRagdollHost->removeActor(found->second);
        }
        mActorRagdolls.erase(found);
    }

    std::span<const std::string> PhysicsSystem::actorRagdollBones() const
    {
        if (mRagdollHost == nullptr)
            return {};
        return mRagdollHost->actorBodyBones();
    }

    std::size_t PhysicsSystem::actorRagdollCount() const
    {
        return mRagdollHost == nullptr ? 0 : mRagdollHost->actorRagdollCount();
    }

    void PhysicsSystem::setDebugGeometry(Render::DebugGeometry& geometry)
    {
        mJoltDebugDrawer = std::make_unique<MWRender::JoltDebugDrawer>(geometry, &mPhysicsSystem, mDebugDrawEnabled);
        mTaskScheduler->setDebugDrawer(mJoltDebugDrawer.get());
    }

    void PhysicsSystem::markAsNonSolid(const MWWorld::ConstPtr& ptr)
    {
        ObjectMap::iterator found = mObjects.find(ptr.mRef);
        if (found == mObjects.end())
            return;

        found->second->setSolid(false);
    }

    bool PhysicsSystem::isOnSolidGround(const MWWorld::Ptr& actor) const
    {
        const Actor* physactor = getActor(actor);
        if (!physactor || !physactor->getOnGround() || !physactor->getCollisionMode())
            return false;

        const auto obj = physactor->getStandingOnPtr();
        if (obj.isEmpty())
            return true; // assume standing on terrain (which is a non-object, so not collision tracked)

        ObjectMap::const_iterator foundObj = mObjects.find(obj.mRef);
        if (foundObj == mObjects.end())
            return false;

        if (!foundObj->second->isSolid())
            return false;

        return true;
    }

    void PhysicsSystem::getWorldBounds(vsg::vec3& low, vsg::vec3& high) const
    {
        const JPH::AABox bounds = mPhysicsSystem.GetBounds();
        low = Misc::Convert::toOsg(bounds.mMin);
        high = Misc::Convert::toOsg(bounds.mMax);
    }

    RayCastingResult PhysicsSystem::castRay(const vsg::vec3& from, const vsg::vec3& to,
        const std::vector<MWWorld::ConstPtr>& ignore, const std::vector<MWWorld::Ptr>& targets, int mask,
        int group) const
    {
        if (from == to)
        {
            RayCastingResult result;
            result.mHit = false;
            return result;
        }

        JoltTargetBodiesFilter bodyFilter;

        for (const auto& ptr : ignore)
        {
            if (!ptr.isEmpty())
            {
                const Actor* actor = getActor(ptr);
                if (actor)
                    bodyFilter.IgnoreBody(actor->getPhysicsBody());
                else
                {
                    const Object* object = getObject(ptr);
                    if (object)
                        bodyFilter.IgnoreBody(object->getPhysicsBody());
                }
            }
        }

        if (!targets.empty())
        {
            for (const MWWorld::Ptr& target : targets)
            {
                const Actor* actor = getActor(target);
                if (actor)
                    bodyFilter.PushTarget(actor->getPhysicsBody());
            }
        }

        JPH::RVec3 rayOrigin = Misc::Convert::toJolt<JPH::RVec3>(from);
        auto diff = to - from;
        JPH::RRayCast ray(rayOrigin, Misc::Convert::toJolt<JPH::Vec3>(diff));

        const int queryObjectMask = getQueryObjectMask(mask, group);
        if (queryObjectMask == 0)
        {
            RayCastingResult result;
            result.mHit = false;
            return result;
        }

        const std::uint32_t queryBroadPhaseMask = getQueryBroadPhaseMask(queryObjectMask, mBPLayerInterface);
        if (queryBroadPhaseMask == 0)
        {
            RayCastingResult result;
            result.mHit = false;
            return result;
        }
        MaskedBroadPhaseLayerFilter broadphaseLayerFilter(queryBroadPhaseMask);
        MaskedObjectLayerFilter objectLayerFilter(queryObjectMask);

        // Cast ray and return closest hit
        JPH::RayCastResult ioHit;
        const bool didRayHit = mPhysicsSystem.GetNarrowPhaseQuery().CastRay(
            ray, ioHit, broadphaseLayerFilter, objectLayerFilter, bodyFilter);

        RayCastingResult result;
        result.mHit = didRayHit;
        if (result.mHit)
        {
            auto outPosition = ray.GetPointOnRay(ioHit.mFraction);
            result.mHitPos = Misc::Convert::toOsg(outPosition);
            JPH::BodyLockRead lock(mPhysicsSystem.GetBodyLockInterface(), ioHit.mBodyID);
            if (lock.Succeeded())
            {
                const JPH::Body& hitBody = lock.GetBody();
                result.mHitNormal
                    = Misc::Convert::toOsg(hitBody.GetWorldSpaceSurfaceNormal(ioHit.mSubShapeID2, outPosition));

                PtrHolder* ptrHolder = Misc::Convert::toPointerFromUserData<PtrHolder>(hitBody.GetUserData());
                if (ptrHolder)
                    result.mHitObject = ptrHolder->getPtr();
            }
        }
        return result;
    }

    RayCastingResult PhysicsSystem::castSphere(
        const vsg::vec3& from, const vsg::vec3& to, float radius, int mask, int group) const
    {
        JPH::SphereShape sphere(radius);
        sphere.SetEmbedded();

        JPH::RMat44 transFrom = JPH::RMat44::sIdentity();
        transFrom.SetTranslation(Misc::Convert::toJolt<JPH::RVec3>(from));

        JPH::ShapeCastSettings settings;
        settings.mUseShrunkenShapeAndConvexRadius = true;
        settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
        settings.mBackFaceModeConvex = JPH::EBackFaceMode::IgnoreBackFaces;

        JPH::Vec3 scale = JPH::Vec3::sReplicate(1.0f);
        JPH::RShapeCast shapeCast(&sphere, scale, transFrom, Misc::Convert::toJolt<JPH::Vec3>(to - from));

        const int queryObjectMask = getQueryObjectMask(mask, group);
        if (queryObjectMask == 0)
        {
            RayCastingResult result;
            result.mHit = false;
            return result;
        }

        const std::uint32_t queryBroadPhaseMask = getQueryBroadPhaseMask(queryObjectMask, mBPLayerInterface);
        if (queryBroadPhaseMask == 0)
        {
            RayCastingResult result;
            result.mHit = false;
            return result;
        }
        MaskedBroadPhaseLayerFilter broadphaseLayerFilter(queryBroadPhaseMask);
        MaskedObjectLayerFilter objectLayerFilter(queryObjectMask);

        ClosestConvexResultCallback callback(transFrom.GetTranslation());
        mPhysicsSystem.GetNarrowPhaseQuery().CastShape(
            shapeCast, settings, transFrom.GetTranslation(), callback, broadphaseLayerFilter, objectLayerFilter);

        RayCastingResult result;
        result.mHit = callback.hasHit();
        if (result.mHit)
        {
            result.mHitPos = callback.mHitPointWorld;
            result.mHitNormal = Misc::Convert::toOsg(callback.mHitNormalWorld);
            if (auto* ptrHolder = static_cast<PtrHolder*>(mTaskScheduler->getUserPointer(callback.mHitCollisionObject)))
                result.mHitObject = ptrHolder->getPtr();
        }
        return result;
    }

    bool PhysicsSystem::getLineOfSight(const MWWorld::ConstPtr& actor1, const MWWorld::ConstPtr& actor2) const
    {
        if (actor1 == actor2)
            return true;

        const auto it1 = mActors.find(actor1.mRef);
        const auto it2 = mActors.find(actor2.mRef);
        if (it1 == mActors.end() || it2 == mActors.end())
            return false;

        return mTaskScheduler->getLineOfSight(it1->second, it2->second);
    }

    bool PhysicsSystem::isOnGround(const MWWorld::Ptr& actor)
    {
        Actor* physactor = getActor(actor);
        return physactor && physactor->getOnGround() && physactor->getCollisionMode();
    }

    bool PhysicsSystem::canMoveToWaterSurface(const MWWorld::ConstPtr& actor, const float waterlevel)
    {
        const auto* physactor = getActor(actor);
        return physactor && physactor->canMoveToWaterSurface(waterlevel, &mPhysicsSystem);
    }

    vsg::vec3 PhysicsSystem::getHalfExtents(const MWWorld::ConstPtr& actor) const
    {
        const Actor* physactor = getActor(actor);
        if (physactor)
            return physactor->getHalfExtents();
        else
            return vsg::vec3();
    }

    vsg::vec3 PhysicsSystem::getOriginalHalfExtents(const MWWorld::ConstPtr& actor) const
    {
        if (const Actor* physactor = getActor(actor))
            return physactor->getOriginalHalfExtents();
        else
            return vsg::vec3();
    }

    vsg::vec3 PhysicsSystem::getRenderingHalfExtents(const MWWorld::ConstPtr& actor) const
    {
        const Actor* physactor = getActor(actor);
        if (physactor)
            return physactor->getRenderingHalfExtents();
        else
            return vsg::vec3();
    }

    vsg::dbox PhysicsSystem::getBoundingBox(const MWWorld::ConstPtr& object) const
    {
        const Object* physobject = getObject(object);
        if (!physobject)
            return vsg::dbox();

        JPH::BodyID bodyId = physobject->getPhysicsBody();
        if (bodyId.IsInvalid())
            return vsg::dbox();

        JPH::BodyLockRead lock(mPhysicsSystem.GetBodyLockInterface(), bodyId);
        if (!lock.Succeeded())
            return vsg::dbox();

        const JPH::Body& body = lock.GetBody();

        // Get body world space translation + shape local space bound
        vsg::vec3 translation = Misc::Convert::toOsg(body.GetCenterOfMassTransform().GetTranslation());
        JPH::AABox bounds = body.GetShape()->GetLocalBounds();

        // Convert local space bounds to world space
        vsg::vec3 min = vsg::vec3(bounds.mMin.GetX(), bounds.mMin.GetY(), bounds.mMin.GetZ()) + translation;
        vsg::vec3 max = vsg::vec3(bounds.mMax.GetX(), bounds.mMax.GetY(), bounds.mMax.GetZ()) + translation;

        return vsg::dbox(vsg::dvec3(min), vsg::dvec3(max));
    }

    vsg::vec3 PhysicsSystem::getCollisionObjectPosition(const MWWorld::ConstPtr& actor) const
    {
        const Actor* physactor = getActor(actor);
        if (physactor)
            return physactor->getCollisionObjectPosition();
        else
            return vsg::vec3();
    }

    std::vector<ContactPoint> PhysicsSystem::getCollisionsPoints(
        const MWWorld::ConstPtr& ptr, int collisionGroup, int collisionMask) const
    {
        JPH::BodyID me;

        auto found = mObjects.find(ptr.mRef);
        if (found != mObjects.end())
            me = found->second->getPhysicsBody();
        else
            return {};

        if (me.IsInvalid())
            return {};

        JPH::ShapeRefC shape;
        JPH::RMat44 transform;

        // Scoped lock so we can read then discard
        {
            JPH::BodyLockRead lock(mPhysicsSystem.GetBodyLockInterface(), me);
            if (!lock.Succeeded())
                return {};

            const JPH::Body& body = lock.GetBody();
            transform = body.GetCenterOfMassTransform();
            shape = body.GetShape();
        }

        // This sets layer filters to avoid collisions with static geometry in those cases, and allows with
        // actors->actors etc if needed this is important to prevent Jolt comlaining that two triangle mesh shapes
        // cannot collide
        const JPH::ObjectLayer collisionLayer = static_cast<JPH::ObjectLayer>(
            std::clamp(collisionGroup, 0, static_cast<int>(std::numeric_limits<JPH::ObjectLayer>::max())));
        JPH::DefaultBroadPhaseLayerFilter broadphaseLayerFilter
            = mPhysicsSystem.GetDefaultBroadPhaseLayerFilter(collisionLayer);
        MaskedObjectLayerFilter objectLayerFilter(collisionMask);

        JPH::CollideShapeSettings settings;
        settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;
        settings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;
        settings.mCollectFacesMode = JPH::ECollectFacesMode::NoFaces;

        // WARNING: you cannot collide mesh->mesh shapes in Jolt, so this should filter to avoid that (only collide with
        // actors etc)
        auto scale = JPH::Vec3::sReplicate(1.0f);
        ContactTestResultCallback resultCallback(&mPhysicsSystem, me);
        mPhysicsSystem.GetNarrowPhaseQuery().CollideShape(shape, scale, transform, settings, JPH::RVec3::sZero(),
            resultCallback, broadphaseLayerFilter, objectLayerFilter);
        return resultCallback.mResult;
    }

    std::vector<MWWorld::Ptr> PhysicsSystem::getCollisions(
        const MWWorld::ConstPtr& ptr, int collisionGroup, int collisionMask) const
    {
        std::vector<MWWorld::Ptr> actors;
        for (auto& [actor, point, normal] : getCollisionsPoints(ptr, collisionGroup, collisionMask))
            actors.emplace_back(actor);
        return actors;
    }

    vsg::vec3 PhysicsSystem::traceDown(const MWWorld::Ptr& ptr, const vsg::vec3& position, float maxHeight)
    {
        ActorMap::iterator found = mActors.find(ptr.mRef);
        if (found == mActors.end())
            return ptr.getRefData().getPosition().asVec3();
        return MovementSolver::traceDown(ptr, position, found->second.get(), &mPhysicsSystem, maxHeight);
    }

    void PhysicsSystem::optimize()
    {
        mPhysicsSystem.OptimizeBroadPhase();
    }

    void PhysicsSystem::addHeightField(
        const float* heights, int x, int y, int size, int verts, float minH, float maxH, const vsg::Object* holdObject)
    {
        mHeightFields[std::make_pair(x, y)]
            = std::make_unique<HeightField>(heights, x, y, size, verts, minH, maxH, holdObject, mTaskScheduler.get());
    }

    void PhysicsSystem::removeHeightField(int x, int y)
    {
        HeightFieldMap::iterator heightfield = mHeightFields.find(std::make_pair(x, y));
        if (heightfield != mHeightFields.end())
            mHeightFields.erase(heightfield);
    }

    const HeightField* PhysicsSystem::getHeightField(int x, int y) const
    {
        const auto heightField = mHeightFields.find(std::make_pair(x, y));
        if (heightField == mHeightFields.end())
            return nullptr;
        return heightField->second.get();
    }

    void PhysicsSystem::addObject(
        const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh, vsg::dquat rotation, int collisionType)
    {
        if (ptr.mRef->mData.mPhysicsPostponed)
            return;

        VFS::Path::Normalized animationMesh(mesh);
        if (ptr.getClass().useAnim())
            animationMesh = Misc::ResourceHelpers::correctActorModelPath(mesh, mResourceSystem->getVFS());
        vsg::ref_ptr<Resource::PhysicsShapeInstance> shapeInstance = mShapeManager->getInstance(animationMesh);
        if (!shapeInstance || !shapeInstance->mCollisionShape)
        {
            return;
        }

        assert(!getObject(ptr));

        // Override collision type based on shape content.
        switch (shapeInstance->mVisualCollisionType)
        {
            case Resource::VisualCollisionType::None:
                break;
            case Resource::VisualCollisionType::Default:
                collisionType = Layers::VISUAL_ONLY;
                break;
            case Resource::VisualCollisionType::Camera:
                collisionType = Layers::CAMERA_ONLY;
                break;
        }

        auto obj = std::make_shared<Object>(ptr, shapeInstance, rotation, collisionType, mTaskScheduler.get());
        mObjects.emplace(ptr.mRef, obj);

        if (obj->isAnimated())
            mAnimatedObjects.emplace(obj.get(), false);
    }

    bool PhysicsSystem::addModelObject(
        const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh, vsg::dquat rotation, int collisionType)
    {
        if (ptr.mRef->mData.mPhysicsPostponed)
            return true; // as addObject: nothing is added now, and the caller must not add a stand-in either

        vsg::ref_ptr<Resource::PhysicsShapeInstance> shapeInstance = mShapeManager->getInstance(mesh);
        if (!shapeInstance || !shapeInstance->mCollisionShape)
            return false;

        assert(!getObject(ptr));

        auto obj = std::make_shared<Object>(ptr, shapeInstance, rotation, collisionType, mTaskScheduler.get());
        mObjects.emplace(ptr.mRef, obj);
        return true;
    }

    void PhysicsSystem::addBoxObject(const MWWorld::Ptr& ptr, const vsg::vec3& halfExtents, const vsg::vec3& centre,
        vsg::dquat rotation, int collisionType)
    {
        if (ptr.mRef->mData.mPhysicsPostponed)
            return;

        vsg::ref_ptr<Resource::PhysicsShape> shape = Resource::makeBoxShape(halfExtents, centre);
        if (shape == nullptr)
            return;
        vsg::ref_ptr<Resource::PhysicsShapeInstance> shapeInstance = Resource::makeInstance(shape);

        assert(!getObject(ptr));

        auto obj = std::make_shared<Object>(ptr, shapeInstance, rotation, collisionType, mTaskScheduler.get());
        mObjects.emplace(ptr.mRef, obj);
    }

    void PhysicsSystem::remove(const MWWorld::Ptr& ptr)
    {
        if (auto foundObject = mObjects.find(ptr.mRef); foundObject != mObjects.end())
        {
            if (mNeutralBridge.size() != 0)
                mNeutralBridge.remove(mNeutralRulesResolver ? mNeutralRulesResolver() : nullptr, ptr.mRef);
            std::erase_if(mNeutralActors, [&](const NeutralActorEntry& e) { return e.mRef == ptr.mRef; });
            mAnimatedObjects.erase(foundObject->second.get());

            mObjects.erase(foundObject);
        }
        else if (auto foundActor = mActors.find(ptr.mRef); foundActor != mActors.end())
        {
            // The actor's ragdoll goes with it (task 0106). With the setting off the map is empty: one lookup.
            if (!mActorRagdolls.empty())
                stopActorRagdoll(ptr);
            mActors.erase(foundActor);
        }
    }

    void PhysicsSystem::removeProjectile(const int projectileId)
    {
        ProjectileMap::iterator foundProjectile = mProjectiles.find(projectileId);
        if (foundProjectile != mProjectiles.end())
            mProjectiles.erase(foundProjectile);
    }

    void PhysicsSystem::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated)
    {
        if (auto foundObject = mObjects.find(old.mRef); foundObject != mObjects.end())
            foundObject->second->updatePtr(updated);
        else if (auto foundActor = mActors.find(old.mRef); foundActor != mActors.end())
            foundActor->second->updatePtr(updated);

        for (auto& [_, actor] : mActors)
        {
            if (actor->getStandingOnPtr() == old)
                actor->setStandingOnPtr(updated);
        }

        for (auto& [_, projectile] : mProjectiles)
        {
            if (projectile->getCaster() == old)
                projectile->setCaster(updated);
        }
    }

    Actor* PhysicsSystem::getActor(const MWWorld::Ptr& ptr)
    {
        ActorMap::iterator found = mActors.find(ptr.mRef);
        if (found != mActors.end())
            return found->second.get();
        return nullptr;
    }

    const Actor* PhysicsSystem::getActor(const MWWorld::ConstPtr& ptr) const
    {
        ActorMap::const_iterator found = mActors.find(ptr.mRef);
        if (found != mActors.end())
            return found->second.get();
        return nullptr;
    }

    const Object* PhysicsSystem::getObject(const MWWorld::ConstPtr& ptr) const
    {
        ObjectMap::const_iterator found = mObjects.find(ptr.mRef);
        if (found != mObjects.end())
            return found->second.get();
        return nullptr;
    }

    Projectile* PhysicsSystem::getProjectile(int projectileId) const
    {
        ProjectileMap::const_iterator found = mProjectiles.find(projectileId);
        if (found != mProjectiles.end())
            return found->second.get();
        return nullptr;
    }

    void PhysicsSystem::updateScale(const MWWorld::Ptr& ptr)
    {
        if (auto foundObject = mObjects.find(ptr.mRef); foundObject != mObjects.end())
        {
            float scale = ptr.getCellRef().getScale();
            foundObject->second->setScale(scale);
        }
        else if (auto foundActor = mActors.find(ptr.mRef); foundActor != mActors.end())
        {
            foundActor->second->updateScale();
        }
    }

    void PhysicsSystem::updateRotation(const MWWorld::Ptr& ptr, vsg::dquat rotate)
    {
        if (auto foundObject = mObjects.find(ptr.mRef); foundObject != mObjects.end())
        {
            foundObject->second->setRotation(rotate);
        }
        else if (auto foundActor = mActors.find(ptr.mRef); foundActor != mActors.end())
        {
            if (!foundActor->second->isRotationallyInvariant())
            {
                foundActor->second->setRotation(rotate);
            }
        }
    }

    void PhysicsSystem::updatePosition(const MWWorld::Ptr& ptr)
    {
        if (auto foundObject = mObjects.find(ptr.mRef); foundObject != mObjects.end())
        {
            foundObject->second->updatePosition();
        }
        else if (auto foundActor = mActors.find(ptr.mRef); foundActor != mActors.end())
        {
            foundActor->second->updatePosition();
        }
    }

    void PhysicsSystem::addActor(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh)
    {
        VFS::Path::Normalized animationMesh
            = Misc::ResourceHelpers::correctActorModelPath(mesh, mResourceSystem->getVFS());
        vsg::ref_ptr<const Resource::PhysicsShape> shape = mShapeManager->getShape(animationMesh);

        // Try to get shape from basic model as fallback for creatures
        if (!ptr.getClass().isNpc() && shape && vsg::length2(shape->mCollisionBox.mExtents) == 0)
        {
            if (animationMesh != mesh)
            {
                shape = mShapeManager->getShape(mesh);
            }
        }

        if (!shape)
            return;

        // check if Actor should spawn above water
        const MWMechanics::MagicEffects& effects = ptr.getClass().getCreatureStats(ptr).getMagicEffects();
        const bool canWaterWalk = effects.getOrDefault(ESM::MagicEffect::WaterWalking).getMagnitude() > 0;

        auto actor = std::make_shared<Actor>(
            ptr, shape, mTaskScheduler.get(), canWaterWalk, Settings::game().mActorCollisionShapeType);

        mActors.emplace(ptr.mRef, std::move(actor));
    }

    void PhysicsSystem::addNeutralActor(const MWWorld::Ptr& ptr, float heightScale, float raise)
    {
        if (getObject(ptr) != nullptr)
            return;
        const NeutralActor::Capsule capsule = NeutralActor::capsuleFor(heightScale);
        vsg::ref_ptr<Resource::PhysicsShape> shape = NeutralActor::makeShape(capsule);
        if (shape == nullptr)
            return;
        if (std::isfinite(raise) && raise != 0.f)
        {
            ESM::Position pos = ptr.getRefData().getPosition();
            pos.pos[2] += raise;
            ptr.getRefData().setPosition(pos);
        }
        // A capsule looks the same from every side, so the placement's rotation is not needed.
        auto obj = std::make_shared<Object>(
            ptr, Resource::makeInstance(shape), vsg::dquat(0.0, 0.0, 0.0, 1.0), Layers::WORLD, mTaskScheduler.get());
        mObjects.emplace(ptr.mRef, obj);

        NeutralActorEntry entry;
        entry.mCapsule = capsule;
        entry.mRef = ptr.mRef;
        entry.mStartZ = ptr.getRefData().getPosition().pos[2];
        entry.mFall.mZ = entry.mStartZ;
        mNeutralActors.push_back(entry);
        // Reserved here so that a frame that moves every actor does not allocate.
        mNeutralMoved.reserve(mNeutralActors.size());
        mNeutralTurned.reserve(mNeutralActors.size());
        mNeutralSpeeds.reserve(mNeutralActors.size());
    }

    const NeutralActor::Fall* PhysicsSystem::getNeutralActorFall(const MWWorld::ConstPtr& ptr) const
    {
        for (const NeutralActorEntry& entry : mNeutralActors)
        {
            if (entry.mRef == ptr.mRef)
                return &entry.mFall;
        }
        return nullptr;
    }

    namespace
    {
        // OPENEDEN_NEUTRAL_ACTOR_WALK=<index>,<vx>,<vy>: a developer aid (not a setting), the test seam of stage S2
        // that stands in for the pack's movement request (stage S3). Walks the neutral actor with that position in
        // the order the bodies were added, at that velocity (units a second), from the frame it is grounded.
        struct WalkAid
        {
            bool mValid = false;
            std::size_t mIndex = 0;
            NeutralMover::Request mRequest;
        };

        WalkAid readWalkAid()
        {
            WalkAid aid;
            const char* text = std::getenv("OPENEDEN_NEUTRAL_ACTOR_WALK");
            if (text == nullptr)
                return aid;
            unsigned long index = 0;
            float vx = 0.f;
            float vy = 0.f;
            if (std::sscanf(text, "%lu,%f,%f", &index, &vx, &vy) == 3 && std::isfinite(vx) && std::isfinite(vy))
            {
                aid.mValid = true;
                aid.mIndex = static_cast<std::size_t>(index);
                aid.mRequest.mVelX = vx;
                aid.mRequest.mVelY = vy;
            }
            else
                Log(Debug::Warning) << "OPENEDEN_NEUTRAL_ACTOR_WALK is not <index>,<vx>,<vy>: " << text;
            return aid;
        }
    }

    void PhysicsSystem::setNeutralActorRequest(const MWWorld::ConstPtr& ptr, const NeutralMover::Request& request)
    {
        for (NeutralActorEntry& entry : mNeutralActors)
        {
            if (entry.mRef == ptr.mRef)
            {
                entry.mRequest = request;
                return;
            }
        }
    }

    PhysicsSystem::NeutralActorEntry* PhysicsSystem::findNeutralActor(const void* key)
    {
        for (NeutralActorEntry& entry : mNeutralActors)
        {
            if (static_cast<const void*>(entry.mRef) == key)
                return &entry;
        }
        return nullptr;
    }

    void PhysicsSystem::NeutralRequestApplier::setRequest(const void* key, float velX, float velY)
    {
        if (NeutralActorEntry* entry = mPhysics.findNeutralActor(key))
        {
            entry->mFollow.cancel(); // an explicit velocity replaces a go-to, without a result
            entry->mRequest.mVelX = velX;
            entry->mRequest.mVelY = velY;
        }
    }

    void PhysicsSystem::NeutralRequestApplier::setGoal(const void* key, const openeden::sdk::game::ActorGoal& goal)
    {
        NeutralActorEntry* entry = mPhysics.findNeutralActor(key);
        if (entry == nullptr)
            return;
        entry->mRequest = {};
        const float speed = goal.speed == openeden::sdk::game::ActorSpeed::Run ? NeutralPath::kRunSpeed
                                                                               : NeutralPath::kWalkSpeed;
        const vsg::vec3 target(static_cast<float>(goal.x), static_cast<float>(goal.y), static_cast<float>(goal.z));
        static const bool trace = std::getenv("OPENEDEN_NEUTRAL_ACTOR_TRACE") != nullptr;
        if (trace)
            Log(Debug::Info) << "NACTGOTO goal " << target.x << " " << target.y << " " << target.z << " speed " << speed;
        if (!entry->mFollow.setGoal(target, speed))
            mPhysics.mNeutralBridge.reportGoal(key, openeden::sdk::game::GoalResult::NoPath); // a float overflow
    }

    void PhysicsSystem::NeutralRequestApplier::cancelGoal(const void* key)
    {
        if (NeutralActorEntry* entry = mPhysics.findNeutralActor(key))
        {
            entry->mFollow.cancel();
            entry->mRequest = {};
        }
    }

    void PhysicsSystem::NeutralRequestApplier::setHeading(const void* key, float heading)
    {
        if (NeutralActorEntry* entry = mPhysics.findNeutralActor(key))
        {
            entry->mFace = heading;
            entry->mFaceValid = true;
        }
    }

    void PhysicsSystem::announceNeutralActor(const MWWorld::Ptr& ptr, openeden::sdk::game::WorldId reference,
        openeden::sdk::game::WorldId base, std::string_view editorId)
    {
        if (!mNeutralRulesResolver || getObject(ptr) == nullptr)
            return;
        const bool known = std::any_of(mNeutralActors.begin(), mNeutralActors.end(),
            [&](const NeutralActorEntry& e) { return e.mRef == ptr.mRef; });
        if (!known)
            return;
        const ESM::Position& pos = ptr.getRefData().getPosition();
        const openeden::sdk::game::ActorHandle handle = mNeutralBridge.add(mNeutralRulesResolver(), ptr.mRef,
            reference, base, editorId, pos.pos[0], pos.pos[1], pos.pos[2], pos.rot[2]);
        static const bool trace = std::getenv("OPENEDEN_NEUTRAL_ACTOR_TRACE") != nullptr;
        if (trace)
            Log(Debug::Info) << "NACTHOOK added " << ptr.getCellRef().getRefId() << " editor id " << editorId
                             << " handle " << handle.reference << "/" << handle.session;
    }

    void PhysicsSystem::setNeutralClock(float hour, int day)
    {
        mNeutralBridge.setClock({ hour, static_cast<std::int32_t>(day), true });
    }

    void PhysicsSystem::updateNeutralActors(float dt)
    {
        mNeutralMoved.clear();
        mNeutralTurned.clear();
        mNeutralSpeeds.clear();
        if (mNeutralActors.empty())
            return;
        // Stage S3: the pack writes its movement requests first; the developer aid below, if set, still wins.
        if (mNeutralBridge.size() != 0 && mNeutralRulesResolver)
            mNeutralBridge.update(mNeutralRulesResolver(), dt, mNeutralApplier);
        static const bool trace = std::getenv("OPENEDEN_NEUTRAL_ACTOR_TRACE") != nullptr;
        static const WalkAid walkAid = readWalkAid();
        if (walkAid.mValid && walkAid.mIndex < mNeutralActors.size())
            mNeutralActors[walkAid.mIndex].mRequest = walkAid.mRequest;
        // Every actor's ray ignores every neutral actor's body, so the order of the entries cannot change a result.
        std::vector<MWWorld::ConstPtr> ignore;
        const bool reportSpeeds = Settings::game().mNeutralActorLocomotion.get(); // task 0148: measured only when used
        for (NeutralActorEntry& entry : mNeutralActors)
        {
            const auto found = mObjects.find(entry.mRef);
            if (found == mObjects.end())
                continue;
            entry.mMeasuredSpeed = 0.f;
            if (entry.mFall.mState == NeutralActor::State::Grounded)
            {
                // Stage S4: a go-to turns its path into this frame's velocity request first.
                if (entry.mFollow.active())
                    followNeutralGoal(entry, found->second->getPtr(), dt, trace);
                if (entry.mRequest.isMoving())
                {
                    entry.mFaceValid = false; // a walking actor faces where it goes
                    walkNeutralActor(entry, *found->second, dt, trace);
                }
                else if (entry.mFaceValid)
                {
                    entry.mFaceValid = false;
                    const MWWorld::Ptr ptr = found->second->getPtr();
                    ESM::Position pos = ptr.getRefData().getPosition();
                    if (pos.rot[2] != entry.mFace)
                    {
                        pos.rot[2] = entry.mFace;
                        ptr.getRefData().setPosition(pos);
                        mNeutralMoved.push_back(ptr);
                        mNeutralTurned.push_back(ptr);
                    }
                }
                // Task 0148: tell the renderer how fast it really moved (0 when blocked or standing), once it changes.
                if (reportSpeeds && entry.mMeasuredSpeed != entry.mReportedSpeed)
                {
                    entry.mReportedSpeed = entry.mMeasuredSpeed;
                    mNeutralSpeeds.push_back({ found->second->getPtr(), entry.mMeasuredSpeed });
                }
                continue;
            }
            if (entry.mFall.mState == NeutralActor::State::Lost)
                continue;
            if (ignore.empty())
            {
                ignore.reserve(mNeutralActors.size());
                for (const NeutralActorEntry& other : mNeutralActors)
                {
                    if (const auto otherObject = mObjects.find(other.mRef); otherObject != mObjects.end())
                        ignore.push_back(otherObject->second->getPtr());
                }
            }
            const MWWorld::Ptr ptr = found->second->getPtr();
            const vsg::vec3 feet = ptr.getRefData().getPosition().asVec3();

            NeutralActor::FloorProbe probe;
            const RayCastingResult hit = castRay(vsg::vec3(feet.x, feet.y, feet.z + NeutralActor::kStepUp),
                vsg::vec3(feet.x, feet.y, feet.z - NeutralActor::kProbeDepth), ignore, {},
                Layers::WORLD | Layers::HEIGHTMAP);
            if (hit.mHit)
            {
                probe.mHit = true;
                probe.mZ = hit.mHitPos.z;
            }

            // The Ptr's position is the truth (a script may have moved the actor); the state follows it.
            entry.mFall.mZ = feet.z;
            entry.mFall = NeutralActor::stepFall(entry.mFall, dt, probe);
            if (entry.mFall.mZ != feet.z)
            {
                ESM::Position pos = ptr.getRefData().getPosition();
                pos.pos[2] = entry.mFall.mZ;
                ptr.getRefData().setPosition(pos);
                mNeutralMoved.push_back(ptr);
            }
            if (trace
                && (entry.mFall.mState == NeutralActor::State::Grounded
                    || entry.mFall.mState == NeutralActor::State::Lost))
                Log(Debug::Info) << "NACTBODY " << ptr.getCellRef().getRefId() << " "
                                 << (entry.mFall.mState == NeutralActor::State::Grounded ? "grounded" : "lost")
                                 << " start z " << entry.mStartZ << " rest z " << entry.mFall.mZ << " frames "
                                 << entry.mFall.mFrames;
        }
    }

    void PhysicsSystem::followNeutralGoal(NeutralActorEntry& entry, const MWWorld::Ptr& ptr, float dt, bool trace)
    {
        // The path finder the follower asks, with this actor's capsule; built on the stack, no allocation.
        class Finder final : public NeutralPath::PathFinder
        {
        public:
            Finder(const NeutralPathFinderFn& fn, const NeutralActor::Capsule& capsule, bool trace)
                : mFn(fn)
                , mCapsule(capsule)
                , mTrace(trace)
            {
            }
            NeutralPath::FindStatus find(
                const vsg::vec3& from, const vsg::vec3& to, std::vector<vsg::vec3>& out) const override
            {
                if (!mFn)
                    return NeutralPath::FindStatus::NoPath;
                const NeutralPath::FindStatus status = mFn(from, to, mCapsule, out);
                if (mTrace)
                {
                    Log(Debug::Info) << "NACTGOTO path status " << static_cast<int>(status) << " points " << out.size()
                                     << " from " << from.x << " " << from.y << " " << from.z;
                    for (const vsg::vec3& point : out)
                        Log(Debug::Info) << "NACTGOTO waypoint " << point.x << " " << point.y << " " << point.z;
                }
                return status;
            }

        private:
            const NeutralPathFinderFn& mFn;
            NeutralActor::Capsule mCapsule;
            bool mTrace;
        };

        const Finder finder(mNeutralPathFinder, entry.mCapsule, trace);
        const vsg::vec3 feet = ptr.getRefData().getPosition().asVec3();
        const NeutralPath::StepResult result = entry.mFollow.step(feet, dt, finder);
        if (result.mSetVelocity)
        {
            entry.mRequest.mVelX = result.mVelX;
            entry.mRequest.mVelY = result.mVelY;
        }
        if (result.mOutcome == NeutralPath::Outcome::None)
            return;
        using openeden::sdk::game::GoalResult;
        const GoalResult reported = result.mOutcome == NeutralPath::Outcome::Arrived ? GoalResult::Arrived
            : result.mOutcome == NeutralPath::Outcome::NoPath                          ? GoalResult::NoPath
                                                                                       : GoalResult::Blocked;
        mNeutralBridge.reportGoal(entry.mRef, reported);
        if (trace)
            Log(Debug::Info) << "NACTGOTO result " << ptr.getCellRef().getRefId() << " "
                             << (reported == GoalResult::Arrived       ? "arrived"
                                        : reported == GoalResult::NoPath ? "no path"
                                                                         : "blocked")
                             << " at " << feet.x << " " << feet.y << " " << feet.z;
    }

    void PhysicsSystem::walkNeutralActor(NeutralActorEntry& entry, Object& object, float dt, bool trace)
    {
        const MWWorld::Ptr ptr = object.getPtr();
        ESM::Position pos = ptr.getRefData().getPosition();
        const vsg::vec3 feet = pos.asVec3();

        const NeutralMover::JoltSweeper sweeper(mPhysicsSystem, object.getShapeInstance()->mCollisionShape,
            object.getPhysicsBody(), Layers::WORLD | Layers::HEIGHTMAP | Layers::DOOR);
        const NeutralMover::Result result = NeutralMover::step(feet, entry.mRequest, dt, sweeper);
        ++entry.mWalkFrames;

        // Face the way it was asked to go, whether or not a wall let it move (a turn never needs room).
        const float heading = NeutralMover::headingOf(entry.mRequest.mVelX, entry.mRequest.mVelY);
        const bool turned = heading != pos.rot[2];
        if (turned)
            pos.rot[2] = heading;
        if (result.mMoved && dt > 0.f && Settings::game().mNeutralActorLocomotion.get())
        {
            const float dx = result.mFeet.x - feet.x;
            const float dy = result.mFeet.y - feet.y;
            entry.mMeasuredSpeed = std::sqrt(dx * dx + dy * dy) / dt;
        }
        if (result.mMoved)
        {
            pos.pos[0] = result.mFeet.x;
            pos.pos[1] = result.mFeet.y;
            pos.pos[2] = result.mFeet.z;
        }
        if (result.mMoved || turned)
        {
            ptr.getRefData().setPosition(pos);
            mNeutralMoved.push_back(ptr);
            if (turned)
                mNeutralTurned.push_back(ptr);
        }
        if (trace && (entry.mWalkFrames == 1 || entry.mWalkFrames % 30 == 0))
            Log(Debug::Info) << "NACTWALK " << ptr.getCellRef().getRefId() << " frame " << entry.mWalkFrames << " pos "
                             << pos.pos[0] << " " << pos.pos[1] << " " << pos.pos[2] << " heading " << pos.rot[2]
                             << (result.mBlocked ? " blocked" : "") << (result.mEdge ? " edge" : "");
    }

    int PhysicsSystem::addProjectile(
        const MWWorld::Ptr& caster, const vsg::vec3& position, VFS::Path::NormalizedView mesh, bool computeRadius)
    {
        vsg::ref_ptr<Resource::PhysicsShapeInstance> shapeInstance = mShapeManager->getInstance(mesh);
        assert(shapeInstance);
        float radius = computeRadius ? vsg::length(shapeInstance->mCollisionBox.mExtents) / 2.f : 1.f;

        mProjectileId++;

        auto projectile = std::make_shared<Projectile>(caster, position, radius, mTaskScheduler.get(), this);
        mProjectiles.emplace(mProjectileId, std::move(projectile));

        return mProjectileId;
    }

    void PhysicsSystem::setCaster(int projectileId, const MWWorld::Ptr& caster)
    {
        const auto foundProjectile = mProjectiles.find(projectileId);
        assert(foundProjectile != mProjectiles.end());
        auto* projectile = foundProjectile->second.get();

        projectile->setCaster(caster);
    }

    bool PhysicsSystem::toggleCollisionMode()
    {
        ActorMap::iterator found = mActors.find(MWMechanics::getPlayer().mRef);
        if (found != mActors.end())
        {
            bool cmode = found->second->getCollisionMode();
            cmode = !cmode;
            found->second->enableCollisionMode(cmode);
            // NB: Collision body isn't disabled for vanilla TCL compatibility
            return cmode;
        }

        return false;
    }

    void PhysicsSystem::queueObjectMovement(const MWWorld::Ptr& ptr, const vsg::vec3& velocity)
    {
        ActorMap::iterator found = mActors.find(ptr.mRef);
        if (found != mActors.end())
            found->second->setVelocity(velocity);
    }

    void PhysicsSystem::updateAnimatedCollisionShape(const MWWorld::Ptr& ptr)
    {
        auto foundObject = mObjects.find(ptr.mRef);
        if (foundObject == mObjects.end())
            return;

        Object* object = foundObject->second.get();
        auto animatedObject = mAnimatedObjects.find(object);
        if (animatedObject == mAnimatedObjects.end())
            return;

        std::scoped_lock lock(mTaskScheduler->getSimulationMutex());
        animatedObject->second = object->animateCollisionShapes();
    }

    void PhysicsSystem::clearQueuedMovement()
    {
        for (const auto& [_, actor] : mActors)
        {
            actor->setVelocity(vsg::vec3());
            actor->setInertialForce(vsg::vec3());
        }
    }

    void PhysicsSystem::prepareSimulation(bool willSimulate, std::vector<Simulation>& simulations)
    {
        assert(simulations.empty());
        simulations.reserve(mActors.size() + mProjectiles.size());
        const MWBase::World* world = MWBase::Environment::get().getWorld();
        for (const auto& [ref, physicActor] : mActors)
        {
            if (!physicActor->isActive())
                continue;

            auto ptr = physicActor->getPtr();
            if (!ptr.getClass().isMobile(ptr))
                continue;
            const auto& stats = ptr.getClass().getCreatureStats(ptr);
            const MWMechanics::MagicEffects& effects = stats.getMagicEffects();

            const MWWorld::CellStore& cell = *ptr.getCell();
            float waterlevel = -std::numeric_limits<float>::max();
            bool waterCollision = false;
            if (cell.getCell()->hasWater())
            {
                waterlevel = cell.getWaterLevel();
                if (physicActor->getCollisionMode())
                    waterCollision = effects.getOrDefault(ESM::MagicEffect::WaterWalking).getMagnitude() > 0;
            }

            physicActor->setCanWaterWalk(waterCollision);

            // Slow fall reduces fall speed by a factor of (effect magnitude / 200)
            const float slowFall
                = 1.f - std::clamp(effects.getOrDefault(ESM::MagicEffect::SlowFall).getMagnitude() * 0.005f, 0.f, 1.f);
            const bool isPlayer = ptr == world->getPlayerConstPtr();
            const bool godmode = isPlayer && world->getGodModeState();
            const bool inert = stats.isDead()
                || (!godmode && stats.getMagicEffects().getOrDefault(ESM::MagicEffect::Paralyze).getModifier() > 0);

            simulations.emplace_back(ActorSimulation{
                physicActor, ActorFrameData{ *physicActor, inert, waterCollision, slowFall, waterlevel, isPlayer } });

            // if the simulation will run, a jump request will be fulfilled. Update mechanics accordingly.
            if (willSimulate)
                handleJump(ptr);
        }
    }

    void PhysicsSystem::stepSimulation(float dt, bool skipSimulation)
    {
        // We cannot modify shapes at runtime while there is a body query going on (such as actor collision)
        // so we must update all animated objects first when we are guaranteed to not be having any physics queries
        {
            std::scoped_lock lock(mTaskScheduler->getSimulationMutex());
            for (auto& [animatedObject, changed] : mAnimatedObjects)
            {
                if (animatedObject->animateCollisionShapes())
                {
                    assert(mObjects.find(animatedObject->getPtr().mRef) != mObjects.end());
                    changed = true;
                }
                else
                {
                    changed = false;
                }
            }
            for (auto& [_, actor] : mActors)
            {
                actor->syncBoneHitboxes();
            }
        }

        for (const MWWorld::LiveCellRefBase* ref : Object::consumeCollidedObjects())
        {
            if (const auto it = mObjects.find(ref); it != mObjects.end())
                it->second->resetCollisions();
        }

        mTimeAccum += dt;
        mTimeAccumJolt += dt;

        if (skipSimulation)
            mTaskScheduler->resetSimulation(mActors);
        else
        {
            std::vector<Simulation>& simulations = mSimulations[mSimulationsCounter++ % mSimulations.size()];
            prepareSimulation(mTimeAccum >= mPhysicsDt, simulations);

            // Runs world simulation for required steps, modifies mTimeAccum
            mTaskScheduler->applyQueuedMovements(mTimeAccum, simulations);
        }

        // Synchronize/commit all transform updates for actors and objects
        updatePtrHolders();

        // Run dynamic body sim, broadphase updates etc
        while (mTimeAccumJolt >= mPhysicsDt)
        {
            mTimeAccumJolt -= mPhysicsDt;
            // With the ragdoll setting off there is no host, and this is the only cost: two null tests.
            if (mRagdollHost != nullptr)
                mRagdollHost->beginStep();
            mPhysicsSystem.Update(mPhysicsDt, cCollisionSteps, mMemoryAllocator.get(), mPhysicsJobSystem.get());
            if (mRagdollHost != nullptr)
                mRagdollHost->endStep();
        }

#ifdef JPH_PROFILE_ENABLED
        JPH_PROFILE_NEXTFRAME();
#endif
    }

    void PhysicsSystem::updatePtrHolders()
    {
        for (auto& [_, object] : mObjects)
            object->commitPositionChange();
    }

    void PhysicsSystem::moveActors()
    {
        auto* player = getActor(MWMechanics::getPlayer());
        const auto world = MWBase::Environment::get().getWorld();

        // copy new ptr position in temporary vector. player is handled separately as its movement might change active
        // cell.
        mActorsPositions.clear();
        if (!mActors.empty())
            mActorsPositions.reserve(mActors.size() - 1);
        for (const auto& [ptr, physicActor] : mActors)
        {
            if (physicActor.get() == player)
                continue;
            mActorsPositions.emplace_back(physicActor->getPtr(), physicActor->getSimulationPosition());
        }

        for (const auto& [ptr, pos] : mActorsPositions)
            world->moveObject(ptr, pos, false, false);

        if (player != nullptr)
            world->moveObject(player->getPtr(), player->getSimulationPosition(), false, false);
    }

    void PhysicsSystem::debugDraw()
    {
        if (mDebugDrawEnabled)
        {
            {
                std::scoped_lock lock(mTaskScheduler->getSimulationMutex());
                for (auto& [_, actor] : mActors)
                    actor->syncBoneHitboxes();
            }
            mTaskScheduler->debugDraw();
        }
    }

    bool PhysicsSystem::isActorStandingOn(const MWWorld::Ptr& actor, const MWWorld::ConstPtr& object) const
    {
        const auto physActor = mActors.find(actor.mRef);
        if (physActor != mActors.end())
            return physActor->second->getStandingOnPtr() == object;
        return false;
    }

    void PhysicsSystem::getActorsStandingOn(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const
    {
        for (const auto& [_, actor] : mActors)
        {
            if (actor->getStandingOnPtr() == object)
                out.emplace_back(actor->getPtr());
        }
    }

    bool PhysicsSystem::isObjectCollidingWith(const MWWorld::ConstPtr& object, ScriptedCollisionType type) const
    {
        auto found = mObjects.find(object.mRef);
        if (found != mObjects.end())
            return found->second->collidedWith(type);
        return false;
    }

    void PhysicsSystem::getActorsCollidingWith(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const
    {
        std::vector<MWWorld::Ptr> collisions = getCollisions(object, Layers::WORLD, Layers::ACTOR);
        out.insert(out.end(), collisions.begin(), collisions.end());
    }

    void PhysicsSystem::disableWater()
    {
        if (mWaterEnabled)
        {
            mWaterEnabled = false;
            updateWater();
        }
    }

    void PhysicsSystem::enableWater(float height)
    {
        if (!mWaterEnabled || mWaterHeight != height)
        {
            mWaterEnabled = true;
            mWaterHeight = height;
            updateWater();
        }
    }

    void PhysicsSystem::setWaterHeight(float height)
    {
        if (mWaterHeight != height)
        {
            mWaterHeight = height;
            updateWater();
        }
    }

    void PhysicsSystem::updateWater()
    {
        if (!mWaterEnabled)
        {
            mWaterInstance.reset();
            return;
        }

        mWaterInstance = std::make_unique<MWWater>(mTaskScheduler.get(), mWaterHeight);
    }

    bool PhysicsSystem::isAreaOccupiedByOtherActor(const vsg::vec3& position, const float radius,
        std::span<const MWWorld::ConstPtr> ignore, std::vector<MWWorld::Ptr>* occupyingActors) const
    {
        // Build list of ignored body IDs from theire actor ptrs
        std::vector<JPH::BodyID> ignoredObjects;
        ignoredObjects.reserve(ignore.size());
        for (const auto& v : ignore)
            if (const auto it = mActors.find(v.mRef); it != mActors.end())
                ignoredObjects.push_back(it->second->getPhysicsBody());
        std::sort(ignoredObjects.begin(), ignoredObjects.end());
        ignoredObjects.erase(std::unique(ignoredObjects.begin(), ignoredObjects.end()), ignoredObjects.end());

        // Define a tiny private class here as its not used anywhere else
        class AreaActorCollector : public JPH::CollideShapeBodyCollector
        {
        public:
            AreaActorCollector(const JPH::PhysicsSystem* inSystem, std::vector<JPH::BodyID>* ignoredBodies,
                std::vector<MWWorld::Ptr>* occupiedResult)
                : mSystem(inSystem)
                , mIgnoredBodies(ignoredBodies)
                , mOccupiedResult(occupiedResult)
            {
            }

            virtual void AddHit(const JPH::BodyID& inBodyID) override
            {
                // Check if we should ignore this body
                if (std::binary_search(mIgnoredBodies->begin(), mIgnoredBodies->end(), inBodyID))
                    return;

                // Bool-only callers need to know just if any actor occupies the area.
                if (mOccupiedResult == nullptr)
                {
                    mHasHit = true;
                    ForceEarlyOut();
                    return;
                }

                // If an output array is specified, get body ptrholder
                if (mOccupiedResult)
                {
                    JPH::BodyLockRead lock(mSystem->GetBodyLockInterface(), inBodyID);
                    if (lock.Succeeded())
                    {
                        const JPH::Body& body = lock.GetBody();
                        if (PtrHolder* holder = Misc::Convert::toPointerFromUserData<PtrHolder>(body.GetUserData()))
                            mOccupiedResult->push_back(holder->getPtr());
                    }
                }

                mHasHit = true;
            }

            bool hasHit() { return mHasHit; }

        private:
            const JPH::PhysicsSystem* mSystem;
            std::vector<JPH::BodyID>* mIgnoredBodies;
            std::vector<MWWorld::Ptr>* mOccupiedResult;
            bool mHasHit = false;
        };

        // Setup collector and collide body AABBs by the input sphere
        AreaActorCollector collector(&mPhysicsSystem, &ignoredObjects, occupyingActors);
        JPH::DefaultBroadPhaseLayerFilter broadphaseLayerFilter
            = mPhysicsSystem.GetDefaultBroadPhaseLayerFilter(Layers::ACTOR);
        mPhysicsSystem.GetBroadPhaseQuery().CollideSphere(Misc::Convert::toJolt<JPH::Vec3>(position), radius, collector,
            broadphaseLayerFilter, JPH::SpecifiedObjectLayerFilter(Layers::ACTOR));

        return collector.hasHit();
    }

    void PhysicsSystem::reportCollision(
        [[maybe_unused]] const vsg::vec3& position, [[maybe_unused]] const vsg::vec3& normal)
    {
        if (mDebugDrawEnabled && mJoltDebugDrawer)
            mJoltDebugDrawer->addCollision(position, normal);
    }

    const JPH::BodyLockInterfaceLocking& PhysicsSystem::getBodyLockInterface() const
    {
        return mPhysicsSystem.GetBodyLockInterface();
    }

    const JPH::BodyInterface& PhysicsSystem::getBodyInterface() const
    {
        return mPhysicsSystem.GetBodyInterface();
    }

    ActorFrameData::ActorFrameData(
        Actor& actor, bool inert, bool waterCollision, float slowFall, float waterlevel, bool isPlayer)
        : mActor(&actor)
        , mPosition()
        , mIsOnGround(actor.getOnGround())
        , mIsOnSlope(actor.getOnSlope())
        , mWalkingOnWater(false)
        , mInert(inert)
        , mPhysicsBody(actor.getPhysicsBody())
        , mSwimLevel(waterlevel
              - (actor.getRenderingHalfExtents().z * 2
                  * MWBase::Environment::get()
                        .getESMStore()
                        ->get<ESM::GameSetting>()
                        .find("fSwimHeightScale")
                        ->mValue.getFloat()))
        , mSlowFall(slowFall)
        , mRotation()
        , mMovement(actor.velocity())
        , mWaterlevel(waterlevel)
        , mHalfExtentsZ(actor.getHalfExtents().z)
        , mOldHeight(0)
        , mStuckFrames(0)
        , mFlying(MWBase::Environment::get().getWorld()->isFlying(actor.getPtr()))
        , mWasOnGround(actor.getOnGround())
        , mIsAquatic(actor.getPtr().getClass().isPureWaterCreature(actor.getPtr()))
        , mWaterCollision(waterCollision)
        , mSkipCollisionDetection(!actor.getCollisionMode())
        , mIsPlayer(isPlayer)
        , mCollisionMask(actor.getCollisionMask())
        , mStrideShape(actor.getStrideShape())
    {
    }

    WorldFrameData::WorldFrameData()
        : mIsInStorm(MWBase::Environment::get().getWorld()->isInStorm())
        , mStormDirection(MWBase::Environment::get().getWorld()->getStormDirection())
    {
    }

    LOSRequest::LOSRequest(const std::weak_ptr<Actor>& a1, const std::weak_ptr<Actor>& a2)
        : mResult(false)
        , mStale(false)
        , mAge(0)
    {
        // we use raw actor pointer pair to uniquely identify request
        // sort the pointer value in ascending order to not duplicate equivalent requests, eg. getLOS(A, B) and
        // getLOS(B, A)
        auto* raw1 = a1.lock().get();
        auto* raw2 = a2.lock().get();
        assert(raw1 != raw2);
        if (raw1 < raw2)
        {
            mActors = { a1, a2 };
            mRawActors = { raw1, raw2 };
        }
        else
        {
            mActors = { a2, a1 };
            mRawActors = { raw2, raw1 };
        }
    }

    bool operator==(const LOSRequest& lhs, const LOSRequest& rhs) noexcept
    {
        return lhs.mRawActors == rhs.mRawActors;
    }
}
