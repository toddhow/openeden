#pragma once

#ifndef OPENMW_MWRENDER_ACTORIKCONTROLLER_H
#define OPENMW_MWRENDER_ACTORIKCONTROLLER_H

#include "../mwworld/ptr.hpp"

#include <components/inertialmotion/footlock.hpp>
#include <components/inertialmotion/leansmoother.hpp>
#include <components/misc/vsgmath.hpp>

#include "bonepose.hpp"
#include "footikmath.hpp"

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
            FootIk::BoneBase mBase; // what the IK pass wrote and the animated values it undoes
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
            vsg::vec3 mGroundNormal{ 0.f, 0.f, 1.f };

            // Filtered state used by the solver
            bool mFilterInit = false;
            float mFilteredGroundZ = 0.f;
            vsg::vec3 mFilteredNormal{ 0.f, 0.f, 1.f };

            // Foot lock (task 0108): holds this foot while the actor stands still. mLastShown is where the ankle
            // was drawn last frame (after the solve), the position a hold starts at.
            InertialMotion::FootLock mLock;
            vsg::vec2 mLastShown{ 0.f, 0.f };
        };

        struct ArmNodes
        {
            BonePose::Bone mUpperArm;
            BonePose::Bone mForearm;
            BonePose::Bone mHand;
        };

        struct RootState
        {
            vsg::vec3 mPosition;
            vsg::dquat mAttitude;
            float mScale = 1.f;
        };

        RootState getRoot() const;
        void updateBodySpeed(const RootState& root, float dt);
        vsg::vec3 worldPos(const BonePose::Bone& bone) const;
        void restoreBones();
        bool restoreBase(BoneState& bone);
        void probeLegGround(LegState& leg, const vsg::vec3& actorPos, const vsg::vec3& actorForward, float actorScale);
        void solveLeg(LegState& leg, float actorZ, float pelvisOffset, float weight, const vsg::vec3& actorForward,
            float actorScale);
        void updateHitboxes(const RootState& root, const vsg::vec3& actorForward, float actorScale);

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
        // The procedural lean angles, eased toward their targets so the spine never steps (owned by this controller)
        InertialMotion::LeanSmoother mLeanPitch;
        InertialMotion::LeanSmoother mLeanBank;
        BonePose::Bone mNeck;
        BonePose::Bone mHead;

        // Grounded/airborne blend target from update(), smoothed in the post pass
        bool mWantIk = false;
        float mIkWeight = 0.f;

        // Frame time and horizontal ground speed of the actor for the foot lock (set in applyPostAnimation)
        float mFrameDt = 0.f;
        float mBodySpeed = 0.f;
        bool mHasLastRoot = false;
        vsg::vec2 mLastRootXY{ 0.f, 0.f };

        // Pelvis height filtered in world space (absorbs collision/stair height pops)
        bool mPelvisInit = false;
        float mFilteredPelvisWorldZ = 0.f;
    };
} // namespace MWRender

#endif
