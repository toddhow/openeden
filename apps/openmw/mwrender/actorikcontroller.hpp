#pragma once

#ifndef OPENMW_MWRENDER_ACTORIKCONTROLLER_H
#define OPENMW_MWRENDER_ACTORIKCONTROLLER_H

#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Vec3f>

#include "../mwworld/ptr.hpp"

#include "bonepose.hpp"

namespace MWAnim
{
    class Object;
}

namespace MWRender
{
    /**
     * @brief Actor Inverse Kinematics and skeletal hitbox controller for OpenEden characters.
     * Uses CreativeMotion's opas::ik::solveTwoBone for procedural foot planting
     * and knee articulation on uneven terrain, ramps, and slopes, and drives the
     * actor's mutable compound hitbox capsules from the animated/IK-solved skeleton.
     *
     * openeden-vsg (task 0011): ported from the OSG build onto BonePose; the algorithm and settings are unchanged.
     * Owned by MWMechanics::CharacterController, which calls update() after the animation pass and
     * applyPostAnimation() after MWAnim::Actor::manualAnimation, on the main thread, before the scene update skins
     * the actor. The look-at and smoother-turn settings are only read here, as on the OSG build.
     */
    class ActorIkController
    {
    public:
        /// @param object the actor's animation object; must outlive the controller (both belong to the character).
        ActorIkController(const MWWorld::Ptr& ptr, MWAnim::Object& object);
        ~ActorIkController();

        ActorIkController(const ActorIkController&) = delete;
        ActorIkController& operator=(const ActorIkController&) = delete;

        void initControllers();
        void reset();
        void update(float duration);

        /// Runs after the frame's animation and manual bone rotations, with the frame's duration in seconds.
        void applyPostAnimation(float duration);

        void updatePtr(const MWWorld::Ptr& ptr) { mPtr = ptr; }

        [[nodiscard]] bool isFootPlantingActive() const noexcept { return mFootPlantingActive; }
        [[nodiscard]] bool isLookAtActive() const noexcept { return mLookAtActive; }
        [[nodiscard]] bool isSmootherTurnsActive() const noexcept { return mSmootherTurnsActive; }

    private:
        struct BoneState
        {
            BonePose::Bone mBone;
            osg::Matrixf mBase; // animated (pre-IK) local matrix of the current frame
            osg::Matrixf mWritten; // local matrix written by the IK pass, as read back from the bone
            bool mHasWritten = false;
        };

        struct LegState
        {
            BoneState mThigh;
            BoneState mCalf;
            BoneState mFoot;
            BonePose::Bone mToe;

            // Ground probe results for the current frame, world space
            bool mGroundHit = false;
            float mGroundZ = 0.f;
            osg::Vec3f mGroundNormal{ 0.f, 0.f, 1.f };

            // Filtered state used by the solver
            bool mFilterInit = false;
            float mFilteredGroundZ = 0.f;
            osg::Vec3f mFilteredNormal{ 0.f, 0.f, 1.f };
        };

        struct ArmNodes
        {
            BonePose::Bone mUpperArm;
            BonePose::Bone mForearm;
            BonePose::Bone mHand;
        };

        struct RootState
        {
            osg::Vec3f mPosition;
            osg::Quat mAttitude;
            float mScale = 1.f;
        };

        RootState getRoot() const;
        osg::Vec3f worldPos(const BonePose::Bone& bone) const;
        void writeBone(BoneState& bone, const osg::Matrixf& local);
        void restoreBones();
        bool restoreBase(BoneState& bone);
        void probeLegGround(LegState& leg, const osg::Vec3f& actorPos, const osg::Vec3f& actorForward, float actorScale);
        void solveLeg(LegState& leg, float actorZ, float pelvisOffset, float weight, const osg::Vec3f& actorForward,
            float actorScale);
        void updateHitboxes(const RootState& root, const osg::Vec3f& actorForward, float actorScale);

        MWWorld::Ptr mPtr;
        MWAnim::Object& mObject;
        std::unique_ptr<BonePose> mPose;

        bool mInitialized = false;
        bool mFootPlantingActive = false;
        bool mLookAtActive = false;
        bool mSmootherTurnsActive = false;
        bool mHasCompoundHitboxes = false;

        BoneState mPelvis;
        LegState mLegs[2];
        ArmNodes mArms[2];
        BoneState mSpine1;
        BonePose::Bone mNeck;
        BonePose::Bone mHead;

        // Grounded/airborne blend target from update(), smoothed in the post pass
        bool mWantIk = false;
        float mIkWeight = 0.f;

        // Pelvis height filtered in world space (absorbs collision/stair height pops)
        bool mPelvisInit = false;
        float mFilteredPelvisWorldZ = 0.f;
    };
} // namespace MWRender

#endif
