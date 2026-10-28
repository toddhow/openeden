#pragma once

#ifndef OPENMW_MWRENDER_ACTORIKCONTROLLER_H
#define OPENMW_MWRENDER_ACTORIKCONTROLLER_H

#include <osg/Matrix>
#include <osg/Quat>
#include <osg/Vec3f>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

namespace osg
{
    class Callback;
    class MatrixTransform;
    class Node;
}

namespace SceneUtil
{
    class PositionAttitudeTransform;
}

namespace MWRender
{
    class Animation;

    /**
     * @brief Actor Inverse Kinematics and skeletal hitbox controller for OpenEden characters.
     * Uses CreativeMotion's opas::ik::solveTwoBone for procedural foot planting
     * and knee articulation on uneven terrain, ramps, and slopes, and drives the
     * actor's mutable compound hitbox capsules from the animated/IK-solved skeleton.
     */
    class ActorIkController
    {
    public:
        explicit ActorIkController(Animation& animation);
        ~ActorIkController();

        ActorIkController(const ActorIkController&) = delete;
        ActorIkController& operator=(const ActorIkController&) = delete;

        void initControllers();
        void reset();
        void update(float duration);

        /// Called from the post-animation update callback on the object root.
        void applyPostAnimation(double simulationTime);

        [[nodiscard]] bool isFootPlantingActive() const noexcept { return mFootPlantingActive; }
        [[nodiscard]] bool isLookAtActive() const noexcept { return mLookAtActive; }
        [[nodiscard]] bool isSmootherTurnsActive() const noexcept { return mSmootherTurnsActive; }

    private:
        struct BoneState
        {
            osg::ref_ptr<osg::MatrixTransform> mNode;
            osg::Matrix mBase; // animated (pre-IK) local matrix of the current frame
            osg::Matrix mWritten; // local matrix written by the IK pass
            bool mHasWritten = false;
        };

        struct LegState
        {
            BoneState mThigh;
            BoneState mCalf;
            BoneState mFoot;
            osg::ref_ptr<osg::MatrixTransform> mToe;

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
            osg::ref_ptr<osg::MatrixTransform> mUpperArm;
            osg::ref_ptr<osg::MatrixTransform> mForearm;
            osg::ref_ptr<osg::MatrixTransform> mHand;
        };

        class PostAnimationCallback;

        void attachPostCallback();
        void detachPostCallback();
        void restoreBones();
        bool restoreBase(BoneState& bone);
        void probeLegGround(LegState& leg, const osg::Vec3f& actorPos, const osg::Vec3f& actorForward, float actorScale);
        void solveLeg(LegState& leg, float actorZ, float pelvisOffset, float weight, const osg::Vec3f& actorForward,
            float actorScale);
        void updateHitboxes(const SceneUtil::PositionAttitudeTransform* baseNode, const osg::Vec3f& actorForward,
            float actorScale);

        Animation& mAnimation;

        bool mInitialized = false;
        bool mFootPlantingActive = false;
        bool mLookAtActive = false;
        bool mSmootherTurnsActive = false;
        bool mHasCompoundHitboxes = false;

        osg::ref_ptr<PostAnimationCallback> mPostCallback;
        osg::observer_ptr<osg::Node> mAttachedTo;

        BoneState mPelvis;
        LegState mLegs[2];
        ArmNodes mArms[2];
        BoneState mSpine1;
        osg::ref_ptr<osg::MatrixTransform> mNeck;
        osg::ref_ptr<osg::MatrixTransform> mHead;

        // Grounded/airborne blend target from update(), smoothed in the post pass
        bool mWantIk = false;
        float mIkWeight = 0.f;

        // Pelvis height filtered in world space (absorbs collision/stair height pops)
        bool mPelvisInit = false;
        float mFilteredPelvisWorldZ = 0.f;

        double mLastSimTime = -1.0;
    };
} // namespace MWRender

#endif
