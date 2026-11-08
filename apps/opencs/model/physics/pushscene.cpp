#include "pushscene.hpp"

#include <components/debug/debuglog.hpp>

#include <opas/math/Quat.hpp>
#include <opas/math/Transform.hpp>

#include <cmath>
#include <exception>
#include <utility>

namespace CSMPhysics
{
    namespace
    {
        bool finite(const opas::math::Vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        opas::physics::Vec3 toPhysics(const opas::math::Vec3& v)
        {
            return opas::physics::Vec3{ static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
        }

        void setError(std::string* error, std::string message)
        {
            if (error != nullptr)
                *error = std::move(message);
        }
    }

    PushScene::PushScene(
        StudioWorld& world, const opas::rig::Rig& rig, const opas::rig::Profile& profile, const Placement& placement)
        : mWorld(world)
        , mRig(rig)
        , mProfile(profile)
        , mPlacement(placement)
    {
        mBoneNames.reserve(mRig.bodies.size());
        for (const opas::rig::Body& body : mRig.bodies)
            mBoneNames.push_back(body.bone);
    }

    std::unique_ptr<PushScene> PushScene::create(StudioWorld& world, const opas::rig::Rig& rig,
        const opas::rig::Profile& profile, const Placement& placement, std::string* error)
    {
        if (!finite(placement.rootPositionM) || !std::isfinite(placement.yawRadians))
        {
            setError(error, "the placement is not finite");
            return nullptr;
        }
        if (std::string problem = opas::rig::validateRig(rig); !problem.empty())
        {
            setError(error, "rig: " + problem);
            return nullptr;
        }
        if (std::string problem = opas::rig::validateProfile(profile, rig); !problem.empty())
        {
            setError(error, "profile: " + problem);
            return nullptr;
        }

        std::unique_ptr<PushScene> scene(new PushScene(world, rig, profile, placement));
        if (!scene->build(error))
            return nullptr;
        // Only now: a scene that failed to build never reaches the world's hook list.
        world.addPreStepHook(scene.get());
        return scene;
    }

    PushScene::~PushScene()
    {
        mWorld.removePreStepHook(this);
        removeRagdoll();
    }

    bool PushScene::present() const
    {
        if (!mRagdoll.has_value() || mRagdoll->bodies.empty())
            return false;
        opas::physics::BodyState state;
        return mWorld.cmWorld().readBody(mRagdoll->bodies.front().id, state);
    }

    double PushScene::boneMassKg(std::string_view bone) const
    {
        const std::optional<std::size_t> index = findBone(bone);
        return index.has_value() ? mRig.bodies[*index].massKg : 0.0;
    }

    bool PushScene::boneState(std::string_view bone, opas::physics::BodyState& out) const
    {
        const std::optional<std::size_t> index = findBone(bone);
        if (!index.has_value() || !mRagdoll.has_value())
            return false;
        return mWorld.cmWorld().readBody(mRagdoll->bodies[*index].id, out);
    }

    bool PushScene::push(std::string_view bone, const opas::math::Vec3& impulseNs, const opas::math::Vec3& worldPointM)
    {
        const std::optional<std::size_t> index = findBone(bone);
        if (!index.has_value() || !present() || mPendingCount >= sMaxPending)
            return false;
        if (!finite(impulseNs) || !finite(worldPointM))
            return false;
        const double length
            = std::sqrt(impulseNs.x * impulseNs.x + impulseNs.y * impulseNs.y + impulseNs.z * impulseNs.z);
        if (length > sMaxImpulseNs)
            return false;
        mPending[mPendingCount++] = Pending{ *index, impulseNs, worldPointM };
        return true;
    }

    void PushScene::applyPending()
    {
        const std::size_t count = mPendingCount;
        mPendingCount = 0;
        if (!mRagdoll.has_value())
            return;
        opas::physics::PhysicsWorld& cm = mWorld.cmWorld();
        for (std::size_t i = 0; i < count; ++i)
        {
            const Pending& push = mPending[i];
            if (!cm.applyImpulse(
                    mRagdoll->bodies[push.body].id, toPhysics(push.impulseNs), toPhysics(push.worldPointM)))
                continue; // a stale handle (the world was cleared): nothing to push
            mLastApplied = AppliedPush{ push.body, push.impulseNs, push.worldPointM, mWorld.stepIndex() };
        }
    }

    void PushScene::beforeStep()
    {
        applyPending();
    }

    bool PushScene::reset(std::string* error)
    {
        removeRagdoll();
        mPendingCount = 0;
        mLastApplied.reset();
        return build(error);
    }

    std::optional<std::size_t> PushScene::findBone(std::string_view bone) const
    {
        for (std::size_t i = 0; i < mBoneNames.size(); ++i)
        {
            if (mBoneNames[i] == bone)
                return i;
        }
        return std::nullopt;
    }

    bool PushScene::build(std::string* error)
    {
        opas::math::Transform rootWorld;
        rootWorld.translation = mPlacement.rootPositionM;
        rootWorld.rotation = opas::math::fromAxisAngle(opas::math::Vec3{ 0.0, 0.0, 1.0 }, mPlacement.yawRadians);

        opas::physics::PhysicsWorld& cm = mWorld.cmWorld();
        opas::motor::BuildResult built = opas::motor::buildRagdoll(cm, mRig, mProfile, rootWorld);
        if (!built.ok())
        {
            setError(error, "ragdoll: " + built.message);
            return false;
        }
        mRagdoll.emplace(std::move(*built.ragdoll));
        // Limp, as the game's debug ragdoll: no driver holds the standing pose, so it falls and settles.
        if (!opas::motor::makeLimp(cm, *mRagdoll, mProfile))
            Log(Debug::Warning) << "Push sandbox: some of the ragdoll's limp settings could not be applied";
        return true;
    }

    void PushScene::removeRagdoll()
    {
        if (!mRagdoll.has_value())
            return;
        // False when the world was cleared (the handles are stale); the bodies are gone then.
        opas::motor::destroyRagdoll(mWorld.cmWorld(), *mRagdoll);
        mRagdoll.reset();
    }
}
