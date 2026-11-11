#include "ragdollhost.hpp"

#include "joltlayers.hpp"
#include "ragdollrigdata.hpp"

#include <components/debug/debuglog.hpp>

#include <opas/math/Quat.hpp>
#include <opas/math/Transform.hpp>
#include <opas/rig/Profile.hpp>
#include <opas/rig/ProfileFile.hpp>
#include <opas/rig/Rig.hpp>
#include <opas/rig/RigFile.hpp>

#include <algorithm>
#include <cmath>

namespace MWPhysics
{
    namespace
    {
        // The debug scenario makes one ragdoll; these are generous limits for it.
        // The debug ragdoll plus cMaxActorRagdolls (task 0106) humanoids of 16 bodies and 15 joints, with room to spare.
        constexpr std::uint32_t cMaxRagdollBodies = 192;
        constexpr std::uint32_t cMaxRagdollJoints = 192;

        // How often (in steps) the actor ragdolls are checked against the kill depth: about twice a second.
        constexpr unsigned cActorKillCheckSteps = 30;

        // A game rotation of `yaw` radians turns the world clockwise about Z, so as a right-handed rotation about +Z
        // it is -yaw (Misc::Convert::makeQuaternion uses the axis (0, 0, -1)).
        opas::math::Quat yawToQuat(float yawRadians)
        {
            return opas::math::fromAxisAngle(opas::math::Vec3{ 0.0, 0.0, 1.0 }, -static_cast<double>(yawRadians));
        }
    }

    // Lets the adapter's rays see only the layers a ragdoll can meet.
    class RagdollHost::LayerFilter final : public JPH::ObjectLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer layer) const override
        {
            return layer == Layers::WORLD || layer == Layers::HEIGHTMAP || layer == Layers::DOOR
                || layer == Layers::RAGDOLL;
        }
    };

    class RagdollHost::BroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
        {
            return layer == BroadPhaseLayers::WORLD || layer == BroadPhaseLayers::MOVING;
        }
    };

    RagdollHost::RagdollHost(float unitsPerMeter)
        : mUnitsPerMeter(unitsPerMeter)
        , mRayObjectFilter(std::make_unique<LayerFilter>())
        , mRayBroadPhaseFilter(std::make_unique<BroadPhaseFilter>())
    {
    }

    std::unique_ptr<RagdollHost> RagdollHost::create(
        JPH::PhysicsSystem& system, float unitsPerMeter, float stepSeconds, std::string* error)
    {
        std::unique_ptr<RagdollHost> host(new RagdollHost(unitsPerMeter));
        host->mStepSeconds = stepSeconds;

        CMPhysics::HostedWorldConfig config;
        config.unitsPerMeter = unitsPerMeter;
        config.stepSeconds = stepSeconds;
        config.movingLayer = Layers::RAGDOLL;
        // The game's Jolt gravity points up (+Z; actors fall by their own code), so ragdolls pull the other way.
        config.gravityFactor = -1.0f;
        // Jolt's tolerances are in metres; at the game's scale a ragdoll would never sleep (task 0035, stage b log).
        config.scaleSystemTolerances = true;
        config.maxBodies = cMaxRagdollBodies;
        config.maxJoints = cMaxRagdollJoints;
        config.rayBroadPhaseFilter = host->mRayBroadPhaseFilter.get();
        config.rayObjectLayerFilter = host->mRayObjectFilter.get();

        host->mWorld = CMPhysics::HostedJoltWorld::create(system, config, error);
        if (host->mWorld == nullptr)
            return nullptr;
        return host;
    }

    RagdollHost::~RagdollHost()
    {
        remove();
        for (ActorSlot& slot : mActorSlots)
            clearActorSlot(slot);
    }

    void RagdollHost::clearActorSlot(ActorSlot& slot)
    {
        if (!slot.mUsed)
            return;
        if (slot.mRagdoll.has_value())
            opas::motor::destroyRagdoll(*mWorld, *slot.mRagdoll);
        slot.mRagdoll.reset();
        slot.mBodyIds.clear();
        slot.mUsed = false;
        // A handle made before this point never matches again.
        ++slot.mGeneration;
    }

    bool RagdollHost::loadRigAndProfile(std::string* error)
    {
        if (mActorRig.has_value() && mActorProfile.has_value())
            return true;
        const auto rig = opas::rig::loadRig(humanoidRigJson(), "humanoid.rig.json");
        if (!rig.ok())
        {
            if (error != nullptr)
                *error = rig.error.toString();
            return false;
        }
        const auto profile = opas::rig::loadProfile(humanoidProfileJson(), "humanoid-default.profile.json");
        if (!profile.ok())
        {
            if (error != nullptr)
                *error = profile.error.toString();
            return false;
        }
        mActorRig = *rig.value;
        mActorProfile = *profile.value;
        mActorBodyBones.clear();
        for (const opas::rig::Body& body : mActorRig->bodies)
            mActorBodyBones.push_back(body.bone);
        return true;
    }

    ActorRagdollHandle RagdollHost::spawnActor(
        const JPH::RVec3& feetUnits, float yawRadians, const double linearVelocityUnits[3], std::string* error)
    {
        const auto fail = [&](const std::string& message) {
            if (error != nullptr)
                *error = message;
            return ActorRagdollHandle{};
        };

        std::size_t index = mActorSlots.size();
        for (std::size_t i = 0; i < mActorSlots.size(); ++i)
        {
            if (!mActorSlots[i].mUsed)
            {
                index = i;
                break;
            }
        }
        if (index == mActorSlots.size())
            return fail("all actor ragdoll slots are in use");
        if (!loadRigAndProfile(error))
            return ActorRagdollHandle{};

        opas::math::Transform rootWorld;
        rootWorld.translation
            = opas::math::Vec3{ metres(feetUnits.GetX()), metres(feetUnits.GetY()), metres(feetUnits.GetZ()) };
        rootWorld.rotation = yawToQuat(yawRadians);

        opas::motor::BuildResult built = opas::motor::buildRagdoll(*mWorld, *mActorRig, *mActorProfile, rootWorld);
        if (!built.ok())
            return fail(built.message);

        ActorSlot& slot = mActorSlots[index];
        slot.mRagdoll.emplace(std::move(*built.ragdoll));
        slot.mUsed = true;
        if (!opas::motor::makeLimp(*mWorld, *slot.mRagdoll, *mActorProfile))
            Log(Debug::Warning) << "Ragdoll: some of the death settings could not be applied";

        slot.mBodyIds.clear();
        for (const opas::motor::RagdollBody& body : slot.mRagdoll->bodies)
            slot.mBodyIds.push_back(body.id);
        slot.mStates.assign(slot.mBodyIds.size(), opas::physics::BodyState{});
        slot.mSpawnZ = feetUnits.GetZ();
        slot.mSteps = 0;
        slot.mSettledLogged = false;

        // The actor's velocity at its death, so a falling or knocked-back body does not stop dead.
        const opas::physics::Vec3 velocity{ static_cast<float>(metres(linearVelocityUnits[0])),
            static_cast<float>(metres(linearVelocityUnits[1])), static_cast<float>(metres(linearVelocityUnits[2])) };
        for (const opas::physics::BodyId id : slot.mBodyIds)
            mWorld->setVelocity(id, velocity, opas::physics::Vec3{});

        return ActorRagdollHandle{ static_cast<std::uint32_t>(index), slot.mGeneration };
    }

    bool RagdollHost::hasActor(ActorRagdollHandle handle) const
    {
        return handle.mIndex < mActorSlots.size() && mActorSlots[handle.mIndex].mUsed
            && mActorSlots[handle.mIndex].mGeneration == handle.mGeneration;
    }

    bool RagdollHost::removeActor(ActorRagdollHandle handle)
    {
        if (!hasActor(handle))
            return false;
        clearActorSlot(mActorSlots[handle.mIndex]);
        return true;
    }

    std::size_t RagdollHost::actorRagdollCount() const
    {
        std::size_t count = 0;
        for (const ActorSlot& slot : mActorSlots)
            count += slot.mUsed ? 1 : 0;
        return count;
    }

    bool RagdollHost::readActor(ActorRagdollHandle handle, std::span<RagdollBoneState> out) const
    {
        if (!hasActor(handle))
            return false;
        const ActorSlot& slot = mActorSlots[handle.mIndex];
        if (out.size() < slot.mBodyIds.size())
            return false;
        mWorld->readBodies(slot.mBodyIds, slot.mStates);
        for (std::size_t i = 0; i < slot.mBodyIds.size(); ++i)
        {
            const opas::motor::RagdollBody& body = slot.mRagdoll->bodies[i];
            const opas::physics::BodyState& state = slot.mStates[i];
            const opas::math::Quat bodyRotation{ state.rotation.x, state.rotation.y, state.rotation.z,
                state.rotation.w };
            // body = bone * body.local, so the bone is the body turned back by the offset.
            const opas::math::Quat boneRotation
                = opas::math::normalized(bodyRotation * opas::math::conjugate(body.local.rotation));
            const opas::math::Vec3 offset = opas::math::rotate(boneRotation, body.local.translation);
            RagdollBoneState& result = out[i];
            result.mPosition[0] = fromMetres(state.positionM.x - offset.x);
            result.mPosition[1] = fromMetres(state.positionM.y - offset.y);
            result.mPosition[2] = fromMetres(state.positionM.z - offset.z);
            result.mRotation[0] = boneRotation.x;
            result.mRotation[1] = boneRotation.y;
            result.mRotation[2] = boneRotation.z;
            result.mRotation[3] = boneRotation.w;
        }
        return true;
    }

    bool RagdollHost::spawn(const JPH::RVec3& feetUnits, float yawRadians, std::string* error)
    {
        remove();

        const auto fail = [&](const std::string& message) {
            if (error != nullptr)
                *error = message;
            return false;
        };

        const auto rig = opas::rig::loadRig(humanoidRigJson(), "humanoid.rig.json");
        if (!rig.ok())
            return fail(rig.error.toString());
        const auto profile = opas::rig::loadProfile(humanoidProfileJson(), "humanoid-default.profile.json");
        if (!profile.ok())
            return fail(profile.error.toString());

        // Game units become meters for the spawn pose in metres() (the adapter converts everything it is given after
        // this, in its own file); metres() and fromMetres() are the only two functions here that convert.
        opas::math::Transform rootWorld;
        rootWorld.translation
            = opas::math::Vec3{ metres(feetUnits.GetX()), metres(feetUnits.GetY()), metres(feetUnits.GetZ()) };
        rootWorld.rotation = yawToQuat(yawRadians);

        opas::motor::BuildResult built = opas::motor::buildRagdoll(*mWorld, *rig.value, *profile.value, rootWorld);
        if (!built.ok())
            return fail(built.message);

        mRagdoll.emplace(std::move(*built.ragdoll));
        // Limp: no motor holds the standing pose, so the body falls and settles.
        if (!opas::motor::makeLimp(*mWorld, *mRagdoll, *profile.value))
            Log(Debug::Warning) << "Ragdoll: some of the death settings could not be applied";

        mBodyIds.clear();
        for (const opas::motor::RagdollBody& body : mRagdoll->bodies)
            mBodyIds.push_back(body.id);
        mStates.assign(mBodyIds.size(), opas::physics::BodyState{});
        mSpawnZ = feetUnits.GetZ();
        mStepsSinceSpawn = 0;
        return true;
    }

    void RagdollHost::remove()
    {
        if (!mRagdoll.has_value())
            return;
        opas::motor::destroyRagdoll(*mWorld, *mRagdoll);
        mRagdoll.reset();
        mBodyIds.clear();
    }

    void RagdollHost::beginStep()
    {
        mWorld->beginStep();
    }

    void RagdollHost::endStep()
    {
        mWorld->endStep();
        if (mProgressLogging)
        {
            // The developer trace (OPENEDEN_RAGDOLL_TRACE): the step at which each actor ragdoll first sleeps ...
            for (std::size_t i = 0; i < mActorSlots.size(); ++i)
            {
                ActorSlot& slot = mActorSlots[i];
                if (!slot.mUsed)
                    continue;
                ++slot.mSteps;
                if (slot.mSettledLogged)
                    continue;
                const ActorRagdollHandle handle{ static_cast<std::uint32_t>(i), slot.mGeneration };
                if (actorStatus(handle).allAsleep)
                {
                    slot.mSettledLogged = true;
                    Log(Debug::Info) << "Actor ragdoll " << i << ": every body asleep after " << slot.mSteps
                                     << " steps (" << slot.mSteps * mStepSeconds << " s)";
                }
            }
            // ... and where each one is, about every two seconds.
            if (++mActorTraceCounter >= cProgressLogSteps)
            {
                mActorTraceCounter = 0;
                for (std::size_t i = 0; i < mActorSlots.size(); ++i)
                {
                    const ActorRagdollHandle handle{ static_cast<std::uint32_t>(i), mActorSlots[i].mGeneration };
                    if (!hasActor(handle))
                        continue;
                    const RagdollStatus current = actorStatus(handle);
                    Log(Debug::Info) << "Actor ragdoll " << i << ": lowest body " << current.lowestZ << " highest "
                                     << current.highestZ << " (units, spawn height " << mActorSlots[i].mSpawnZ
                                     << "), top speed " << current.maxSpeedMetresPerSecond << " m/s, "
                                     << (current.allAsleep ? "asleep" : "awake");
                }
            }
        }
        // Actor ragdolls that fell through the world are removed (checked a couple of times a second).
        if (++mActorStepCounter >= cActorKillCheckSteps)
        {
            mActorStepCounter = 0;
            for (ActorSlot& slot : mActorSlots)
            {
                if (!slot.mUsed)
                    continue;
                mWorld->readBodies(slot.mBodyIds, slot.mStates);
                double lowest = slot.mStates.empty() ? 0.0 : slot.mStates.front().positionM.z;
                for (const opas::physics::BodyState& state : slot.mStates)
                    lowest = std::min(lowest, static_cast<double>(state.positionM.z));
                if (fromMetres(lowest) < slot.mSpawnZ - static_cast<double>(cRagdollKillDepthMetres) * mUnitsPerMeter)
                {
                    Log(Debug::Info) << "Ragdoll: an actor's ragdoll fell " << cRagdollKillDepthMetres
                                     << " m below where it started, removed";
                    clearActorSlot(slot);
                }
            }
        }
        if (!mRagdoll.has_value())
            return;
        const RagdollStatus current = status();
        if (mProgressLogging && ++mStepsSinceSpawn % cProgressLogSteps == 0)
        {
            Log(Debug::Info) << "Ragdoll after " << mStepsSinceSpawn << " steps: lowest body " << current.lowestZ
                             << " highest " << current.highestZ << " (units, floor spawn height " << mSpawnZ
                             << "), top speed " << current.maxSpeedMetresPerSecond << " m/s, "
                             << (current.allAsleep ? "asleep" : "awake");
        }
        if (current.lowestZ < mSpawnZ - static_cast<double>(cRagdollKillDepthMetres) * mUnitsPerMeter)
        {
            Log(Debug::Info) << "Ragdoll: fell " << cRagdollKillDepthMetres << " m below where it started, removed";
            remove();
        }
    }

    void RagdollHost::summarize(const std::vector<opas::physics::BodyState>& states, RagdollStatus& result) const
    {
        double lowest = 0.0;
        double highest = 0.0;
        double fastest = 0.0;
        bool asleep = true;
        bool first = true;
        for (const opas::physics::BodyState& state : states)
        {
            const double z = state.positionM.z;
            const double speed = std::sqrt(state.linearVelocityMps.x * state.linearVelocityMps.x
                + state.linearVelocityMps.y * state.linearVelocityMps.y
                + state.linearVelocityMps.z * state.linearVelocityMps.z);
            lowest = first ? z : std::min(lowest, z);
            highest = first ? z : std::max(highest, z);
            fastest = std::max(fastest, speed);
            asleep = asleep && !state.awake;
            first = false;
        }
        result.lowestZ = fromMetres(lowest);
        result.highestZ = fromMetres(highest);
        result.maxSpeedMetresPerSecond = static_cast<float>(fastest);
        result.allAsleep = asleep;
    }

    RagdollStatus RagdollHost::actorStatus(ActorRagdollHandle handle) const
    {
        RagdollStatus result;
        if (!hasActor(handle))
            return result;
        const ActorSlot& slot = mActorSlots[handle.mIndex];
        result.present = true;
        result.bodies = slot.mBodyIds.size();
        mWorld->readBodies(slot.mBodyIds, slot.mStates);
        summarize(slot.mStates, result);
        return result;
    }

    RagdollStatus RagdollHost::status() const
    {
        RagdollStatus result;
        if (!mRagdoll.has_value() || mBodyIds.empty())
            return result;

        result.present = true;
        result.bodies = mBodyIds.size();
        mWorld->readBodies(mBodyIds, mStates);

        summarize(mStates, result);
        return result;
    }
}
