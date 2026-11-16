#include "actorragdolldriver.hpp"

#include "ragdollbonemath.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/vsgutil/name.hpp>

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

    RagdollBoneSet morrowindRagdollBones()
    {
        return RagdollBoneSet{ std::span<const RagdollBoneMapping>(sMappings), sRootBone };
    }

    ActorRagdollRest::ActorRagdollRest(Anim::Bones& bones)
        : ActorRagdollRest(bones, morrowindRagdollBones())
    {
    }

    ActorRagdollRest::ActorRagdollRest(Anim::Bones& bones, const RagdollBoneSet& set)
    {
        // Model space: no root transform, so neither the actor's yaw nor its position is in the numbers.
        const BonePose pose(bones, nullptr);
        for (std::size_t i = 0; i < set.mMappings.size() && i < mPresent.size(); ++i)
        {
            if (set.mMappings[i].mEngineBone == nullptr || set.mMappings[i].mEngineBone[0] == '\0')
                continue;
            const BonePose::Bone bone = pose.find(set.mMappings[i].mEngineBone);
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
        return makeActorStartPose(bones, root, rest, morrowindRagdollBones(), out);
    }

    bool makeActorStartPose(Anim::Bones& bones, const Anim::Transform* root, const ActorRagdollRest& rest,
        const RagdollBoneSet& set, MWPhysics::ActorStartPose& out)
    {
        if (!rest.isValid() || set.mMappings.empty() || set.mMappings[0].mEngineBone == nullptr)
            return false;
        const BonePose modelPose(bones, nullptr);
        const BonePose worldPose(bones, root);
        const BonePose::Bone pelvis = worldPose.find(set.mMappings[0].mEngineBone);
        if (!pelvis)
            return false;

        const std::span<const RagdollBoneMapping> mappings = set.mMappings;
        MWPhysics::ActorStartPose result;
        for (std::size_t i = 0; i < mappings.size(); ++i)
        {
            if (!rest.has(i))
                continue;
            const BonePose::Bone bone = modelPose.find(mappings[i].mEngineBone);
            if (!bone)
                continue;
            const vsg::dquat now = Misc::VsgMath::rotationOf(modelPose.getWorldRotation(bone));
            const vsg::dquat relative = RagdollBones::restRelative(now, rest.rotation(i));
            const double rotation[4] = { relative.x, relative.y, relative.z, relative.w };
            result.add(mappings[i].mRigBone, rotation);
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
        : ActorRagdollDriver(bones, root, std::move(source), morrowindRagdollBones())
    {
    }

    ActorRagdollDriver::ActorRagdollDriver(Anim::Bones& bones, const Anim::Transform* root,
        std::unique_ptr<RagdollSource> source, const RagdollBoneSet& set)
        : ActorRagdollDriver(bones, root, std::move(source), set, false)
    {
    }

    ActorRagdollDriver::ActorRagdollDriver(Anim::Bones& bones, const Anim::Transform* root,
        std::unique_ptr<RagdollSource> source, const RagdollBoneSet& set, bool absolute)
        : mPose(std::make_unique<BonePose>(bones, root))
        , mSource(std::move(source))
        , mAbsolute(absolute)
    {
        if (mSource == nullptr)
        {
            mReason = "no ragdoll source";
            return;
        }
        const std::span<const std::string> names = mSource->boneNames();
        mStates.assign(names.size(), MWPhysics::RagdollBoneState{});
        mLastDrawn.assign(names.size(), MWPhysics::RagdollBoneState{});
        if (!mSource->read(mStates))
        {
            mReason = "the ragdoll is already gone";
            return;
        }

        // A rig bone without a body (the embedded humanoid has none for the neck) or an engine bone the actor lacks is
        // skipped: that bone keeps the animation's local rotation under its driven parent. The pelvis is required.
        mDriven.reserve(set.mMappings.size());
        for (const RagdollBoneMapping& mapping : set.mMappings)
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
            if (mapping.mEngineBone != nullptr && mapping.mEngineBone[0] != '\0')
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
            if (mAbsolute)
                driven.mDelta = vsg::dquat(0.0, 0.0, 0.0, 1.0);
            else
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
        if (set.mRootBone != nullptr && set.mRootBone[0] != '\0')
            mPelvisParent = mPose->find(set.mRootBone);
        if (mPelvisParent && set.mTranslateRootBone && mPelvisParent.mPath.size() >= 2)
        {
            // Task 0174: the bone above the root bone, found by name, turns a world position into the root bone's
            // local translation. A root bone with no bone above it is under the model root (getRoot()).
            mPelvisGrandParent = mPose->find(vsgUtil::getName(*mPelvisParent.mPath[mPelvisParent.mPath.size() - 2]));
        }
        mTranslateRootBone = set.mTranslateRootBone;
        if (mPelvisParent)
        {
            mActorPelvisAtSpawn = Misc::VsgMath::getTrans(mPose->getWorld(mDriven[mPelvisDriven].mBone));
            mRagdollPelvisAtSpawn = positionOfState(mStates[mDriven[mPelvisDriven].mState]);
            if (mAbsolute)
            {
                // wanted = actor + (state - ragdoll): with both zero it is the state's own position
                mActorPelvisAtSpawn = vsg::dvec3(0.0, 0.0, 0.0);
                mRagdollPelvisAtSpawn = vsg::dvec3(0.0, 0.0, 0.0);
            }
            mHasPelvisTranslation = true;
        }
        mValid = true;
    }

    ActorRagdollDriver::~ActorRagdollDriver() = default;

    bool ActorRagdollDriver::apply()
    {
        if (!mValid || !mSource->read(mStates))
            return false;

        mLastDrawn = mStates;
        mHasDrawn = true;
        for (const Driven& driven : mDriven)
        {
            const vsg::dquat world
                = RagdollBones::drivenWorldRotation(rotationOfState(mStates[driven.mState]), driven.mDelta);
            MWPhysics::RagdollBoneState& drawn = mLastDrawn[driven.mState];
            drawn.mRotation[0] = world.x;
            drawn.mRotation[1] = world.y;
            drawn.mRotation[2] = world.z;
            drawn.mRotation[3] = world.w;
            const vsg::dquat parent = Misc::VsgMath::rotationOf(mPose->getParentWorldRotation(driven.mBone));
            mPose->setLocalRotation(driven.mBone, RagdollBones::localFromWorld(parent, world));
        }

        if (mHasPelvisTranslation)
        {
            const Driven& pelvis = mDriven[mPelvisDriven];
            const vsg::dvec3 wanted = RagdollBones::drivenPelvisPosition(
                mActorPelvisAtSpawn, mRagdollPelvisAtSpawn, positionOfState(mStates[pelvis.mState]));
            MWPhysics::RagdollBoneState& drawnPelvis = mLastDrawn[pelvis.mState];
            drawnPelvis.mPosition[0] = wanted.x;
            drawnPelvis.mPosition[1] = wanted.y;
            drawnPelvis.mPosition[2] = wanted.z;
            if (mTranslateRootBone)
            {
                // The pelvis origin is parent * (local translation), and the parent's rotation is not driven: put the
                // parent where that product lands on `wanted`.
                const vsg::vec3 offset = mPose->getLocalTranslation(pelvis.mBone);
                const vsg::vec4 rotated
                    = mPose->getWorldRotation(mPelvisParent) * vsg::vec4(offset.x, offset.y, offset.z, 0.0f);
                const vsg::vec4 parentAt(static_cast<float>(wanted.x) - rotated.x,
                    static_cast<float>(wanted.y) - rotated.y, static_cast<float>(wanted.z) - rotated.z, 1.0f);
                const vsg::mat4 aboveWorld
                    = mPelvisGrandParent ? mPose->getWorld(mPelvisGrandParent) : mPose->getRoot();
                vsg::mat4 inverseAbove;
                if (Misc::VsgMath::invert(aboveWorld, inverseAbove))
                {
                    const vsg::vec4 local = inverseAbove * parentAt;
                    mPose->setLocalTranslation(mPelvisParent, vsg::vec3(local.x, local.y, local.z));
                }
                return true;
            }
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

    bool ActorRagdollDriver::drivenPose(std::span<MWPhysics::RagdollBoneState> out) const
    {
        if (!mHasDrawn || out.size() < mLastDrawn.size())
            return false;
        for (std::size_t i = 0; i < mLastDrawn.size(); ++i)
            out[i] = mLastDrawn[i];
        return true;
    }
}
