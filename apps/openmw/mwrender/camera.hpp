#ifndef GAME_MWRENDER_CAMERA_H
#define GAME_MWRENDER_CAMERA_H

#include <optional>
#include <string>

#include <vsg/core/ref_ptr.h>

#include <components/misc/vsgmath.hpp>

#include "../mwworld/ptr.hpp"

namespace vsg
{
    class LookAt;
}
namespace Anim
{
    class Transform;
}
namespace MWRender
{
    class Player;

    /// \brief Camera control
    class Camera
    {
    public:
        enum class Mode : int
        {
            Static = 0,
            FirstPerson = 1,
            ThirdPerson = 2,
            Vanity = 3,
            Preview = 4
        };

        Camera();
        ~Camera();

        /// Attach camera to object
        void attachTo(const MWWorld::Ptr& ptr) { mTrackingPtr = ptr; }
        MWWorld::Ptr getTrackingPtr() const { return mTrackingPtr; }

        void setFocalPointTransitionSpeed(float v) { mFocalPointTransitionSpeedCoef = v; }
        float getFocalPointTransitionSpeed() const { return mFocalPointTransitionSpeedCoef; }
        void setFocalPointTargetOffset(const vsg::dvec2& v);
        vsg::dvec2 getFocalPointTargetOffset() const { return mFocalPointTargetOffset; }
        void instantTransition();
        void showCrosshair(bool v) { mShowCrosshair = v; }

        void updateCamera(vsg::LookAt& lookAt);

        /// Reset to defaults
        void reset() { setMode(Mode::FirstPerson); }

        void rotateCameraToTrackingPtr();

        float getPitch() const { return mPitch; }
        float getYaw() const { return mYaw; }
        float getRoll() const { return mRoll; }

        void setPitch(float angle, bool force = false);
        void setYaw(float angle, bool force = false);
        void setRoll(float angle) { mRoll = angle; }

        float getExtraPitch() const { return mExtraPitch; }
        float getExtraYaw() const { return mExtraYaw; }
        float getExtraRoll() const { return mExtraRoll; }
        void setExtraPitch(float angle) { mExtraPitch = angle; }
        void setExtraYaw(float angle) { mExtraYaw = angle; }
        void setExtraRoll(float angle) { mExtraRoll = angle; }

        vsg::dquat getOrient() const;

        /// @param Force view mode switch, even if currently not allowed by the animation.
        void toggleViewMode(bool force = false);
        bool toggleVanityMode(bool enable);

        void applyDeferredPreviewRotationToPlayer(float dt);
        void disableDeferredPreviewRotation() { mDeferredRotationDisabled = true; }

        /// \brief Lowers the camera for sneak.
        void setSneakOffset(float offset);

        void processViewChange();

        void update(float duration, bool paused = false);

        float getCameraDistance() const { return mCameraDistance; }
        void setPreferredCameraDistance(float v) { mPreferredCameraDistance = v; }

        void setAnimation(Player* anim);

        vsg::dvec3 getTrackedPosition() const { return mTrackedPosition; }
        const vsg::dvec3& getPosition() const { return mPosition; }
        void setStaticPosition(const vsg::dvec3& pos);

        bool isVanityOrPreviewModeEnabled() const { return mMode == Mode::Vanity || mMode == Mode::Preview; }
        Mode getMode() const { return mMode; }
        std::optional<Mode> getQueuedMode() const { return mQueuedMode; }
        void setMode(Mode mode, bool force = true);

        void allowCharacterDeferredRotation(bool v) { mDeferredRotationAllowed = v; }
        void calculateDeferredRotation();
        void setFirstPersonOffset(const vsg::vec3& v) { mFirstPersonOffset = v; }
        vsg::vec3 getFirstPersonOffset() const { return mFirstPersonOffset; }

        int getCollisionType() const { return mCollisionType; }
        void setCollisionType(int collisionType) { mCollisionType = collisionType; }

        const vsg::mat4& getViewMatrix() const { return mViewMatrix; }

    private:
        MWWorld::Ptr mTrackingPtr;
        std::vector<vsg::ref_ptr<const Anim::Transform>> mTrackingPath;
        vsg::dvec3 mTrackedPosition;
        float mHeightScale;
        int mCollisionType;

        Player* mAnimation;

        // Always 'true' if mMode == `FirstPerson`. Also it is 'true' in `Vanity` or `Preview` modes if
        // the camera should return to `FirstPerson` view after it.
        bool mFirstPersonView;

        Mode mMode;
        std::optional<Mode> mQueuedMode;
        bool mVanityAllowed;
        bool mDeferredRotationAllowed;

        bool mProcessViewChange;

        float mHeight;
        float mPitch, mYaw, mRoll;
        float mExtraPitch = 0, mExtraYaw = 0, mExtraRoll = 0;
        bool mLockPitch = false, mLockYaw = false;
        vsg::dvec3 mPosition;
        vsg::mat4 mViewMatrix;

        float mCameraDistance, mPreferredCameraDistance;

        vsg::vec3 mFirstPersonOffset{ 0, 0, 0 };

        vsg::dvec2 mFocalPointCurrentOffset;
        vsg::dvec2 mFocalPointTargetOffset;
        float mFocalPointTransitionSpeedCoef;
        bool mSkipFocalPointTransition;

        // This fields are used to make focal point transition smooth if previous transition was not finished.
        float mPreviousTransitionInfluence;
        vsg::dvec2 mFocalPointTransitionSpeed;
        vsg::dvec2 mPreviousTransitionSpeed;
        vsg::dvec2 mPreviousExtraOffset;

        bool mShowCrosshair;

        vsg::dvec3 calculateTrackedPosition() const;
        vsg::dvec3 calculateFirstPersonPosition(const vsg::dvec3& trackedPosition) const;
        vsg::dvec3 getFocalPointOffset() const;
        void updateFocalPointOffset(float duration);
        void updatePosition();

        // Used to rotate player to the direction of view after exiting preview or vanity mode.
        vsg::vec3 mDeferredRotation;
        bool mDeferredRotationDisabled;
    };
}

#endif
