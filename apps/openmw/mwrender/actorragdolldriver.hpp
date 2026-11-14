#ifndef OPENEDEN_MWRENDER_ACTORRAGDOLLDRIVER_H
#define OPENEDEN_MWRENDER_ACTORRAGDOLLDRIVER_H

#include <components/misc/vsgmath.hpp>

#include "../mwphysics/ragdolltypes.hpp"
#include "bonepose.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Anim
{
    class Bones;
    class Transform;
}

namespace MWRender
{
    /// Where the driver reads the ragdoll from. The game's implementation asks MWPhysics::PhysicsSystem; tests give a
    /// fake. Main thread only.
    class RagdollSource
    {
    public:
        virtual ~RagdollSource() = default;

        /// The rig bone each state belongs to, in the order read() fills them. Constant for the source's life.
        virtual std::span<const std::string> boneNames() const = 0;

        /// Fills out (at least boneNames().size() entries) with the bones' world rotation and origin in game units.
        /// False once the ragdoll is gone; the driver then stops.
        virtual bool read(std::span<MWPhysics::RagdollBoneState> out) const = 0;
    };

    /// One line of the humanoid skeleton profile (components/creativemotion/data/profiles/
    /// humanoid-to-eden.skeletonprofile.json): the CreativeMotion rig bone and the Morrowind bone that follows it
    /// (Anim::Bones-normalised names).
    struct RagdollBoneMapping
    {
        const char* mRigBone;
        const char* mEngineBone;
    };
    std::span<const RagdollBoneMapping> ragdollBoneMappings();

    /// The actor's bones in their REST pose (the pose the model has before any animation runs), model space, for the 17
    /// mapped bones. Read once when the actor's pose controllers are made (CharacterController::createPoseControllers),
    /// so that a death can tell the pose the actor was in from the way its bone axes differ from the rig's.
    /// Ownership: plain values, no pointers into the bones. Main thread.
    class ActorRagdollRest
    {
    public:
        /// Reads the mapped bones' model space rotations now. isValid() is false when the pelvis bone is missing.
        explicit ActorRagdollRest(Anim::Bones& bones);

        bool isValid() const { return mValid; }
        /// Index = the mapping's index (ragdollBoneMappings()).
        bool has(std::size_t mapping) const { return mPresent[mapping]; }
        const vsg::dquat& rotation(std::size_t mapping) const { return mRotations[mapping]; }

    private:
        std::array<vsg::dquat, 17> mRotations;
        std::array<bool, 17> mPresent{};
        bool mValid = false;
    };

    /// The pose a ragdoll for this actor should start in: the actor's mapped bones now, against their rest. False when
    /// the rest is invalid or the actor lacks its pelvis; then `out` is untouched. Per death, not per frame
    /// (BonePose::find allocates).
    bool makeActorStartPose(
        Anim::Bones& bones, const Anim::Transform* root, const ActorRagdollRest& rest, MWPhysics::ActorStartPose& out);

    /**
     * @brief Draws a dead NPC's skeleton from its CreativeMotion ragdoll (task 0106).
     *
     * Each frame, after the animation, the manual bone rotations and the IK controller, apply() writes the world
     * rotation of every mapped bone (parent first) as ragdoll bone rotation times a fixed offset captured on the
     * spawn frame, so that the first frame draws the pose the animation had. The pelvis bone's origin follows the
     * ragdoll's pelvis. Bones not in the mapping keep what the animation gave them. Bones are written only through
     * BonePose (task 0011).
     *
     * Ownership: the driver owns its BonePose and its RagdollSource. The Anim::Bones and the root transform must
     * outlive it (they belong to the actor's MWAnim::Object; the CharacterController drops the driver whenever it
     * drops its bone controllers). Main thread only. apply() allocates nothing.
     */
    class ActorRagdollDriver
    {
    public:
        /// Reads the actor's current bones and the ragdoll's first state to capture the offsets. Invalid (isValid()
        /// false, reason()) when the source lacks a mapped rig bone, or the actor lacks a mapped bone.
        ActorRagdollDriver(Anim::Bones& bones, const Anim::Transform* root, std::unique_ptr<RagdollSource> source);
        ~ActorRagdollDriver();

        ActorRagdollDriver(const ActorRagdollDriver&) = delete;
        ActorRagdollDriver& operator=(const ActorRagdollDriver&) = delete;

        bool isValid() const { return mValid; }
        const std::string& reason() const { return mReason; }

        /// Number of bones the driver writes (the mapping's size when valid).
        std::size_t drivenBoneCount() const { return mDriven.size(); }

        /// Writes the bones. False when the ragdoll is gone (the bones keep the last pose written).
        bool apply();

    private:
        struct Driven
        {
            BonePose::Bone mBone;
            std::size_t mState = 0; // index into mStates
            vsg::dquat mDelta;
        };

        std::unique_ptr<BonePose> mPose;
        std::unique_ptr<RagdollSource> mSource;
        std::vector<Driven> mDriven;
        std::vector<MWPhysics::RagdollBoneState> mStates;
        BonePose::Bone mPelvisParent;
        std::size_t mPelvisDriven = 0; // index into mDriven
        vsg::dvec3 mActorPelvisAtSpawn;
        vsg::dvec3 mRagdollPelvisAtSpawn;
        bool mValid = false;
        bool mHasPelvisTranslation = false;
        std::string mReason;
    };
}

#endif
