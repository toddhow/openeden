#ifndef OPENCS_VIEW_CAMERACONTROLLER_H
#define OPENCS_VIEW_CAMERACONTROLLER_H

#include <vector>

#include <QObject>

#include <vsg/maths/vec3.h>

#include "camerapose.hpp"

namespace CSMPrefs
{
    class Shortcut;
}

namespace CSVRender
{
    class SceneHost;

    class CameraController : public QObject
    {
        Q_OBJECT

    public:
        static const vsg::dvec3 WorldUp;

        static const vsg::dvec3 LocalUp;
        static const vsg::dvec3 LocalLeft;
        static const vsg::dvec3 LocalForward;

        CameraController(QObject* parent);
        ~CameraController() override = default;

        bool isActive() const;

        SceneHost* getCamera() const;
        double getCameraSensitivity() const;
        bool getInverted() const;
        double getSecondaryMovementMultiplier() const;
        double getWheelMovementMultiplier() const;

        void setCamera(SceneHost*);
        void setCameraSensitivity(double value);
        void setInverted(bool value);
        void setSecondaryMovementMultiplier(double value);
        void setWheelMovementMultiplier(double value);

        virtual void handleMouseMoveEvent(int x, int y) = 0;
        virtual void handleMouseScrollEvent(int x) = 0;

        virtual void update(double dt) = 0;

    protected:
        void addShortcut(CSMPrefs::Shortcut* shortcut);

        vsg::dmat4 getView() const;
        void setView(const vsg::dmat4& view);
        CameraPose::LookAt getLookAt(double distance = 1.0) const;
        void setLookAt(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up);

    private:
        bool mActive, mInverted;
        double mCameraSensitivity;
        double mSecondaryMoveMult;
        double mWheelMoveMult;

        SceneHost* mCamera;

        std::vector<CSMPrefs::Shortcut*> mShortcuts;
    };

    class FreeCameraController : public CameraController
    {
        Q_OBJECT

    public:
        FreeCameraController(QWidget* parent);

        double getLinearSpeed() const;
        double getRotationalSpeed() const;
        double getSpeedMultiplier() const;

        void setLinearSpeed(double value);
        void setRotationalSpeed(double value);
        void setSpeedMultiplier(double value);

        void fixUpAxis(const vsg::dvec3& up);
        void unfixUpAxis();

        void handleMouseMoveEvent(int x, int y) override;
        void handleMouseScrollEvent(int x) override;

        void update(double dt) override;

    private:
        void yaw(double value);
        void pitch(double value);
        void roll(double value);
        void translate(const vsg::dvec3& offset);

        void stabilize();

        bool mLockUpright, mModified;
        bool mNaviPrimary, mNaviSecondary;
        bool mFast, mFastAlternate;
        bool mLeft, mRight, mForward, mBackward, mRollLeft, mRollRight;
        vsg::dvec3 mUp;

        double mLinSpeed;
        double mRotSpeed;
        double mSpeedMult;

    private slots:

        void naviPrimary(bool active);
        void naviSecondary(bool active);
        void forward(bool active);
        void left(bool active);
        void backward(bool active);
        void right(bool active);
        void rollLeft(bool active);
        void rollRight(bool active);
        void alternateFast(bool active);
        void swapSpeedMode();
    };

    class OrbitCameraController : public CameraController
    {
        Q_OBJECT

    public:
        OrbitCameraController(QWidget* parent);

        vsg::dvec3 getCenter() const;
        double getOrbitSpeed() const;
        double getOrbitSpeedMultiplier() const;
        unsigned int getPickingMask() const;

        void setCenter(const vsg::dvec3& center);
        void setOrbitSpeed(double value);
        void setOrbitSpeedMultiplier(double value);
        void setPickingMask(unsigned int value);

        void handleMouseMoveEvent(int x, int y) override;
        void handleMouseScrollEvent(int x) override;

        void update(double dt) override;

        /// \brief Flag controller to be re-initialized.
        void reset();

        void setConstRoll(bool enable);

        /// The distance from the centre, for checks.
        double getDistance() const { return mDistance; }

    private:
        void initialize();

        void rotateHorizontal(double value);
        void rotateVertical(double value);
        void roll(double value);
        void translate(const vsg::dvec3& offset);
        void zoom(double value);

        bool mInitialized;
        bool mNaviPrimary, mNaviSecondary;
        bool mFast, mFastAlternate;
        bool mLeft, mRight, mUp, mDown, mRollLeft, mRollRight;
        unsigned int mPickingMask;
        vsg::dvec3 mCenter;
        double mDistance;

        double mOrbitSpeed;
        double mOrbitSpeedMult;

        bool mConstRoll;

    private slots:

        void naviPrimary(bool active);
        void naviSecondary(bool active);
        void up(bool active);
        void left(bool active);
        void down(bool active);
        void right(bool active);
        void rollLeft(bool active);
        void rollRight(bool active);
        void alternateFast(bool active);
        void swapSpeedMode();
    };
}

#endif
