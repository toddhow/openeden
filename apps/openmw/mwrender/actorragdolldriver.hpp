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

    /// Task 0174: the bones of another skeleton. The mapping lists the 17 rig bones in the order of
    /// ragdollBoneMappings() (the pelvis first, parents before children); an entry whose mEngineBone is empty has no
    /// bone in the skeleton and is skipped, like a bone the actor lacks. mRootBone names the pelvis bone's parent,
    /// which turns the pelvis' world position into a local one (null: the pelvis does not translate). The strings and
    /// the span must outlive everything built from the set.
    struct RagdollBoneSet
    {
        std::span<const RagdollBoneMapping> mMappings;
        const char* mRootBone = nullptr;
        /// Task 0174. False (Morrowind): the pelvis bone itself takes the translation, which carries the whole body
        /// because every other bone hangs below it. True (Skyrim: the spine is the pelvis' SIBLING, both under the
        /// "COM" bone): the pelvis' parent takes it, placed so that the pelvis origin lands where the ragdoll's pelvis
        /// is, and the spine goes with it.
        bool mTranslateRootBone = false;
    };
    /// Morrowind's bones (ragdollBoneMappings(), root "bip01").
    RagdollBoneSet morrowindRagdollBones();

    /// The actor's bones in their REST pose (the pose the model has before any animation runs), model space, for the 17
    /// mapped bones. Read once when the actor's pose controllers are made (CharacterController::createPoseControllers),
    /// so that a death can tell the pose the actor was in from the way its bone axes differ from the rig's.
    /// Ownership: plain values, no pointers into the bones. Main thread.
    class ActorRagdollRest
    {
    public:
        /// Reads the mapped bones' model space rotations now. isValid() is false when the pelvis bone is missing.
        explicit ActorRagdollRest(Anim::Bones& bones);
        /// Task 0174: the same for another skeleton's bone set.
        ActorRagdollRest(Anim::Bones& bones, const RagdollBoneSet& set);

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
    bool makeActorStartPose(Anim::Bones& bones, const Anim::Transform* root, const ActorRagdollRest& rest,
        const RagdollBoneSet& set, MWPhysics::ActorStartPose& out);

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
        /// Task 0174: another skeleton's bones (the set must outlive the driver).
        ActorRagdollDriver(Anim::Bones& bones, const Anim::Transform* root, std::unique_ptr<RagdollSource> source,
            const RagdollBoneSet& set);
        /// Task 0263: with absolute set, the states ARE the world rotations of the driven bones and the pelvis' world
        /// position (what drivenPose() gave), not a ragdoll's: no offsets are captured, so the corpse is drawn in
        /// exactly that pose.
        ActorRagdollDriver(Anim::Bones& bones, const Anim::Transform* root, std::unique_ptr<RagdollSource> source,
            const RagdollBoneSet& set, bool absolute);
        ~ActorRagdollDriver();

        ActorRagdollDriver(const ActorRagdollDriver&) = delete;
        ActorRagdollDriver& operator=(const ActorRagdollDriver&) = delete;

        bool isValid() const { return mValid; }
        const std::string& reason() const { return mReason; }

        /// Number of bones the driver writes (the mapping's size when valid).
        std::size_t drivenBoneCount() const { return mDriven.size(); }

        /// Writes the bones. False when the ragdoll is gone (the bones keep the last pose written).
        bool apply();

        /// Task 0263. The pose the last apply() wrote, in the layout of the source's states: each driven bone's state
        /// carries the bone's world rotation as drawn, the pelvis' state also its world position as drawn. Bones with
        /// no body keep the source's own state. False before the first apply() or when `out` is too small. Same
        /// thread as apply(). No allocation.
        bool drivenPose(std::span<MWPhysics::RagdollBoneState> out) const;

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
        std::vector<MWPhysics::RagdollBoneState> mLastDrawn; // task 0263: what apply() last wrote, see drivenPose
        bool mHasDrawn = false;
        bool mAbsolute = false;
        BonePose::Bone mPelvisParent;
        BonePose::Bone mPelvisGrandParent; // only with mTranslateRootBone; empty: the model root
        bool mTranslateRootBone = false;
        std::size_t mPelvisDriven = 0; // index into mDriven
        vsg::dvec3 mActorPelvisAtSpawn;
        vsg::dvec3 mRagdollPelvisAtSpawn;
        bool mValid = false;
        bool mHasPelvisTranslation = false;
        std::string mReason;
    };
}

#endif
