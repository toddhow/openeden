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
        constexpr std::uint32_t cMaxRagdollBodies = 64;
        constexpr std::uint32_t cMaxRagdollJoints = 64;

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

        CMPhysics::HostedWorldConfig config;
        config.unitsPerMeter = unitsPerMeter;
        config.stepSeconds = stepSeconds;
        config.movingLayer = Layers::RAGDOLL;
        // The game's Jolt gravity points up (+Z; actors fall by their own code), so ragdolls pull the other way.
        config.gravityFactor = -1.0f;
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

        // The one place game units become meters for the spawn pose (the adapter converts everything it is given
        // after this, in its own file).
        const auto toMetres = [this](double units) { return units / static_cast<double>(mUnitsPerMeter); };
        opas::math::Transform rootWorld;
        rootWorld.translation = opas::math::Vec3{ toMetres(feetUnits.GetX()), toMetres(feetUnits.GetY()),
            toMetres(feetUnits.GetZ()) };
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

    RagdollStatus RagdollHost::status() const
    {
        RagdollStatus result;
        if (!mRagdoll.has_value() || mBodyIds.empty())
            return result;

        result.present = true;
        result.bodies = mBodyIds.size();
        mWorld->readBodies(mBodyIds, mStates);

        double lowest = 0.0;
        double highest = 0.0;
        double fastest = 0.0;
        bool asleep = true;
        bool first = true;
        for (const opas::physics::BodyState& state : mStates)
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
        result.lowestZ = lowest * mUnitsPerMeter;
        result.highestZ = highest * mUnitsPerMeter;
        result.maxSpeedMetresPerSecond = static_cast<float>(fastest);
        result.allAsleep = asleep;
        return result;
    }
}
