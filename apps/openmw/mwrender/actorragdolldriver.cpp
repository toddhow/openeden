#include "actorragdolldriver.hpp"

#include "ragdollbonemath.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>

#include <array>

namespace MWRender
{
    namespace
    {
        // Parent first, so that a bone's parent world rotation is already the driven one when the bone is written.
        constexpr std::array<RagdollBoneMapping, 17> sMappings = { {
            { "Pelvis", "bip01 pelvis" },
            { "Spine", "bip01 spine" },
            { "Chest", "bip01 spine1" },
            { "Neck", "bip01 neck" },
            { "Head", "bip01 head" },
            { "LeftUpperArm", "bip01 l upperarm" },
            { "LeftForearm", "bip01 l forearm" },
            { "LeftHand", "bip01 l hand" },
            { "RightUpperArm", "bip01 r upperarm" },
            { "RightForearm", "bip01 r forearm" },
            { "RightHand", "bip01 r hand" },
            { "LeftThigh", "bip01 l thigh" },
            { "LeftShin", "bip01 l calf" },
            { "LeftFoot", "bip01 l foot" },
            { "RightThigh", "bip01 r thigh" },
            { "RightShin", "bip01 r calf" },
            { "RightFoot", "bip01 r foot" },
        } };

        // Fewer than this and the body would hang from a few bones: no ragdoll.
        constexpr std::size_t cMinDrivenBones = 10;

        // The model's root bone, the pelvis bone's parent in Morrowind's skeletons.
        constexpr const char* sRootBone = "bip01";

        vsg::dquat rotationOfState(const MWPhysics::RagdollBoneState& state)
        {
            return vsg::dquat(state.mRotation[0], state.mRotation[1], state.mRotation[2], state.mRotation[3]);
        }

        vsg::dvec3 positionOfState(const MWPhysics::RagdollBoneState& state)
        {
            return vsg::dvec3(state.mPosition[0], state.mPosition[1], state.mPosition[2]);
        }
    }

    std::span<const RagdollBoneMapping> ragdollBoneMappings()
    {
        return sMappings;
    }

    ActorRagdollRest::ActorRagdollRest(Anim::Bones& bones)
    {
        // Model space: no root transform, so neither the actor's yaw nor its position is in the numbers.
        const BonePose pose(bones, nullptr);
        for (std::size_t i = 0; i < sMappings.size(); ++i)
        {
            const BonePose::Bone bone = pose.find(sMappings[i].mEngineBone);
            if (!bone)
                continue;
            mRotations[i] = Misc::VsgMath::rotationOf(pose.getWorldRotation(bone));
            mPresent[i] = true;
        }
        mValid = mPresent[0]; // the pelvis is the first mapping
    }

    bool makeActorStartPose(
        Anim::Bones& bones, const Anim::Transform* root, const ActorRagdollRest& rest, MWPhysics::ActorStartPose& out)
    {
        if (!rest.isValid())
            return false;
        const BonePose modelPose(bones, nullptr);
        const BonePose worldPose(bones, root);
        const BonePose::Bone pelvis = worldPose.find(sMappings[0].mEngineBone);
        if (!pelvis)
            return false;

        MWPhysics::ActorStartPose result;
        for (std::size_t i = 0; i < sMappings.size(); ++i)
        {
            if (!rest.has(i))
                continue;
            const BonePose::Bone bone = modelPose.find(sMappings[i].mEngineBone);
            if (!bone)
                continue;
            const vsg::dquat now = Misc::VsgMath::rotationOf(modelPose.getWorldRotation(bone));
            const vsg::dquat relative = RagdollBones::restRelative(now, rest.rotation(i));
            const double rotation[4] = { relative.x, relative.y, relative.z, relative.w };
            result.add(sMappings[i].mRigBone, rotation);
        }
        const vsg::dvec3 position = Misc::VsgMath::getTrans(worldPose.getWorld(pelvis));
        result.mPelvisPosition[0] = position.x;
        result.mPelvisPosition[1] = position.y;
        result.mPelvisPosition[2] = position.z;
        out = result;
        return true;
    }

    ActorRagdollDriver::ActorRagdollDriver(
        Anim::Bones& bones, const Anim::Transform* root, std::unique_ptr<RagdollSource> source)
        : mPose(std::make_unique<BonePose>(bones, root))
        , mSource(std::move(source))
    {
        if (mSource == nullptr)
        {
            mReason = "no ragdoll source";
            return;
        }
        const std::span<const std::string> names = mSource->boneNames();
        mStates.assign(names.size(), MWPhysics::RagdollBoneState{});
        if (!mSource->read(mStates))
        {
            mReason = "the ragdoll is already gone";
            return;
        }

        // A rig bone without a body (the embedded humanoid has none for the neck) or an engine bone the actor lacks is
        // skipped: that bone keeps the animation's local rotation under its driven parent. The pelvis is required.
        mDriven.reserve(sMappings.size());
        for (const RagdollBoneMapping& mapping : sMappings)
        {
            const bool isPelvis = mDriven.empty();
            std::size_t state = names.size();
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (names[i] == mapping.mRigBone)
                {
                    state = i;
                    break;
                }
            }
            if (state == names.size())
            {
                if (isPelvis)
                {
                    mReason = std::string("the ragdoll has no body for the rig bone ") + mapping.mRigBone;
                    return;
                }
                continue;
            }
            Driven driven;
            driven.mBone = mPose->find(mapping.mEngineBone);
            if (!driven.mBone)
            {
                if (isPelvis)
                {
                    mReason = std::string("the actor has no bone ") + mapping.mEngineBone;
                    return;
                }
                continue;
            }
            driven.mState = state;
            // The spawn frame: the actor's bone and the ragdoll's bone as they are now.
            const vsg::dquat actorRotation = Misc::VsgMath::rotationOf(mPose->getWorldRotation(driven.mBone));
            driven.mDelta = RagdollBones::bindDelta(rotationOfState(mStates[state]), actorRotation);
            mDriven.push_back(std::move(driven));
        }
        if (mDriven.size() < cMinDrivenBones)
        {
            mReason = "only " + std::to_string(mDriven.size()) + " bones can be driven";
            return;
        }

        // Only the pelvis translates; it needs its parent to turn a world position into a local one.
        mPelvisDriven = 0;
        mPelvisParent = mPose->find(sRootBone);
        if (mPelvisParent)
        {
            mActorPelvisAtSpawn = Misc::VsgMath::getTrans(mPose->getWorld(mDriven[mPelvisDriven].mBone));
            mRagdollPelvisAtSpawn = positionOfState(mStates[mDriven[mPelvisDriven].mState]);
            mHasPelvisTranslation = true;
        }
        mValid = true;
    }

    ActorRagdollDriver::~ActorRagdollDriver() = default;

    bool ActorRagdollDriver::apply()
    {
        if (!mValid || !mSource->read(mStates))
            return false;

        for (const Driven& driven : mDriven)
        {
            const vsg::dquat world
                = RagdollBones::drivenWorldRotation(rotationOfState(mStates[driven.mState]), driven.mDelta);
            const vsg::dquat parent = Misc::VsgMath::rotationOf(mPose->getParentWorldRotation(driven.mBone));
            mPose->setLocalRotation(driven.mBone, RagdollBones::localFromWorld(parent, world));
        }

        if (mHasPelvisTranslation)
        {
            const Driven& pelvis = mDriven[mPelvisDriven];
            const vsg::dvec3 wanted = RagdollBones::drivenPelvisPosition(
                mActorPelvisAtSpawn, mRagdollPelvisAtSpawn, positionOfState(mStates[pelvis.mState]));
            const vsg::mat4 parentWorld = mPose->getWorld(mPelvisParent);
            vsg::mat4 inverseParent;
            if (Misc::VsgMath::invert(parentWorld, inverseParent))
            {
                const vsg::vec4 local = inverseParent
                    * vsg::vec4(
                        static_cast<float>(wanted.x), static_cast<float>(wanted.y), static_cast<float>(wanted.z), 1.0f);
                mPose->setLocalTranslation(pelvis.mBone, vsg::vec3(local.x, local.y, local.z));
            }
        }
        return true;
    }
}
