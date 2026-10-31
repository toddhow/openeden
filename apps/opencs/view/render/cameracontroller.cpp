#include "cameracontroller.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include <QWidget>

#include <optional>

#include <vsg/maths/transform.h>

#include <components/misc/constants.hpp>

#include "../../model/prefs/shortcut.hpp"

#include "scenehost.hpp"

namespace
{
    const double sPi = std::acos(-1.0);
}

namespace CSVRender
{

    /*
    Camera Controller
    */

    const vsg::dvec3 CameraController::WorldUp = vsg::dvec3(0, 0, 1);

    const vsg::dvec3 CameraController::LocalUp = vsg::dvec3(0, 1, 0);
    const vsg::dvec3 CameraController::LocalLeft = vsg::dvec3(1, 0, 0);
    const vsg::dvec3 CameraController::LocalForward = vsg::dvec3(0, 0, 1);

    CameraController::CameraController(QObject* parent)
        : QObject(parent)
        , mActive(false)
        , mInverted(false)
        , mCameraSensitivity(1 / 650.f)
        , mSecondaryMoveMult(50)
        , mWheelMoveMult(8)
        , mCamera(nullptr)
    {
    }

    bool CameraController::isActive() const
    {
        return mActive;
    }

    SceneHost* CameraController::getCamera() const
    {
        return mCamera;
    }

    double CameraController::getCameraSensitivity() const
    {
        return mCameraSensitivity;
    }

    bool CameraController::getInverted() const
    {
        return mInverted;
    }

    double CameraController::getSecondaryMovementMultiplier() const
    {
        return mSecondaryMoveMult;
    }

    double CameraController::getWheelMovementMultiplier() const
    {
        return mWheelMoveMult;
    }

    void CameraController::setCamera(SceneHost* camera)
    {
        bool wasActive = mActive;

        mCamera = camera;
        mActive = (mCamera != nullptr);

        if (mActive != wasActive)
        {
            for (std::vector<CSMPrefs::Shortcut*>::iterator it = mShortcuts.begin(); it != mShortcuts.end(); ++it)
            {
                CSMPrefs::Shortcut* shortcut = *it;
                shortcut->enable(mActive);
            }
        }
    }

    void CameraController::setCameraSensitivity(double value)
    {
        mCameraSensitivity = value;
    }

    void CameraController::setInverted(bool value)
    {
        mInverted = value;
    }

    void CameraController::setSecondaryMovementMultiplier(double value)
    {
        mSecondaryMoveMult = value;
    }

    void CameraController::setWheelMovementMultiplier(double value)
    {
        mWheelMoveMult = value;
    }

    void CameraController::addShortcut(CSMPrefs::Shortcut* shortcut)
    {
        mShortcuts.push_back(shortcut);
    }

    vsg::dmat4 CameraController::getView() const
    {
        return mCamera->getViewMatrix();
    }

    void CameraController::setView(const vsg::dmat4& view)
    {
        mCamera->setViewMatrix(view);
    }

    CameraPose::LookAt CameraController::getLookAt(double distance) const
    {
        return CameraPose::lookAtFrom(getView(), distance);
    }

    void CameraController::setLookAt(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up)
    {
        setView(CameraPose::viewFrom(eye, center, up));
    }

    /*
    Free Camera Controller
    */

    FreeCameraController::FreeCameraController(QWidget* widget)
        : CameraController(widget)
        , mLockUpright(false)
        , mModified(false)
        , mNaviPrimary(false)
        , mNaviSecondary(false)
        , mFast(false)
        , mFastAlternate(false)
        , mLeft(false)
        , mRight(false)
        , mForward(false)
        , mBackward(false)
        , mRollLeft(false)
        , mRollRight(false)
        , mUp(LocalUp)
        , mLinSpeed(1000)
        , mRotSpeed(sPi / 2)
        , mSpeedMult(8)
    {
        CSMPrefs::Shortcut* naviPrimaryShortcut = new CSMPrefs::Shortcut("scene-navi-primary", widget);
        naviPrimaryShortcut->enable(false);
        connect(naviPrimaryShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this,
            &FreeCameraController::naviPrimary);

        addShortcut(naviPrimaryShortcut);

        CSMPrefs::Shortcut* naviSecondaryShortcut = new CSMPrefs::Shortcut("scene-navi-secondary", widget);
        naviSecondaryShortcut->enable(false);
        connect(naviSecondaryShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this,
            &FreeCameraController::naviSecondary);

        addShortcut(naviSecondaryShortcut);

        CSMPrefs::Shortcut* forwardShortcut
            = new CSMPrefs::Shortcut("free-forward", "scene-speed-modifier", CSMPrefs::Shortcut::SM_Detach, widget);
        forwardShortcut->enable(false);
        connect(forwardShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &FreeCameraController::forward);
        connect(forwardShortcut, qOverload<bool>(&CSMPrefs::Shortcut::secondary), this,
            &FreeCameraController::alternateFast);

        addShortcut(forwardShortcut);

        CSMPrefs::Shortcut* leftShortcut = new CSMPrefs::Shortcut("free-left", widget);
        leftShortcut->enable(false);
        connect(leftShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &FreeCameraController::left);

        addShortcut(leftShortcut);

        CSMPrefs::Shortcut* backShortcut = new CSMPrefs::Shortcut("free-backward", widget);
        backShortcut->enable(false);
        connect(backShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &FreeCameraController::backward);

        addShortcut(backShortcut);

        CSMPrefs::Shortcut* rightShortcut = new CSMPrefs::Shortcut("free-right", widget);
        rightShortcut->enable(false);
        connect(rightShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &FreeCameraController::right);

        addShortcut(rightShortcut);

        CSMPrefs::Shortcut* rollLeftShortcut = new CSMPrefs::Shortcut("free-roll-left", widget);
        rollLeftShortcut->enable(false);
        connect(
            rollLeftShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &FreeCameraController::rollLeft);

        addShortcut(rollLeftShortcut);

        CSMPrefs::Shortcut* rollRightShortcut = new CSMPrefs::Shortcut("free-roll-right", widget);
        rollRightShortcut->enable(false);
        connect(
            rollRightShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &FreeCameraController::rollRight);

        addShortcut(rollRightShortcut);

        CSMPrefs::Shortcut* speedModeShortcut = new CSMPrefs::Shortcut("free-speed-mode", widget);
        speedModeShortcut->enable(false);
        connect(
            speedModeShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this, &FreeCameraController::swapSpeedMode);

        addShortcut(speedModeShortcut);
    }

    double FreeCameraController::getLinearSpeed() const
    {
        return mLinSpeed;
    }

    double FreeCameraController::getRotationalSpeed() const
    {
        return mRotSpeed;
    }

    double FreeCameraController::getSpeedMultiplier() const
    {
        return mSpeedMult;
    }

    void FreeCameraController::setLinearSpeed(double value)
    {
        mLinSpeed = value;
    }

    void FreeCameraController::setRotationalSpeed(double value)
    {
        mRotSpeed = value;
    }

    void FreeCameraController::setSpeedMultiplier(double value)
    {
        mSpeedMult = value;
    }

    void FreeCameraController::fixUpAxis(const vsg::dvec3& up)
    {
        mLockUpright = true;
        mUp = up;
        mModified = true;
    }

    void FreeCameraController::unfixUpAxis()
    {
        mLockUpright = false;
    }

    void FreeCameraController::handleMouseMoveEvent(int x, int y)
    {
        if (!isActive())
            return;

        if (mNaviPrimary)
        {
            double scalar = getCameraSensitivity() * (getInverted() ? -1.0 : 1.0);
            yaw(x * scalar);
            pitch(y * scalar);
        }
        else if (mNaviSecondary)
        {
            vsg::dvec3 movement;
            movement += LocalLeft * -x * getSecondaryMovementMultiplier();
            movement += LocalUp * y * getSecondaryMovementMultiplier();

            translate(movement);
        }
    }

    void FreeCameraController::handleMouseScrollEvent(int x)
    {
        if (!isActive())
            return;

        translate(LocalForward * x * ((mFast ^ mFastAlternate) ? getWheelMovementMultiplier() : 1));
    }

    void FreeCameraController::update(double dt)
    {
        if (!isActive())
            return;

        double linDist = mLinSpeed * dt;
        double rotDist = mRotSpeed * dt;

        if (mFast ^ mFastAlternate)
            linDist *= mSpeedMult;

        if (mLeft)
            translate(LocalLeft * linDist);
        if (mRight)
            translate(LocalLeft * -linDist);
        if (mForward)
            translate(LocalForward * linDist);
        if (mBackward)
            translate(LocalForward * -linDist);

        if (!mLockUpright)
        {
            if (mRollLeft)
                roll(-rotDist);
            if (mRollRight)
                roll(rotDist);
        }
        else if (mModified)
        {
            stabilize();
            mModified = false;
        }

        // Normalize the matrix to counter drift
        setView(CameraPose::orthoNormal(getView()));
    }

    void FreeCameraController::yaw(double value)
    {
        setView(CameraPose::rotatedInEye(getView(), value, LocalUp));
        mModified = true;
    }

    void FreeCameraController::pitch(double value)
    {
        const double constraint = sPi / 2 - 0.1;

        if (mLockUpright)
        {
            const auto [eye, center, up] = getLookAt();

            const vsg::dvec3 forward = center - eye;
            const vsg::dvec3 left = vsg::cross(up, forward);

            // Clamped: an up vector a rounding error over length one has no acos.
            double pitchAngle = std::acos(std::clamp(vsg::dot(up, mUp), -1.0, 1.0));
            if (vsg::dot(vsg::cross(mUp, up), left) < 0)
                pitchAngle *= -1;

            if (std::abs(pitchAngle + value) > constraint)
                value = (pitchAngle > 0 ? 1 : -1) * constraint - pitchAngle;
        }

        setView(CameraPose::rotatedInEye(getView(), value, LocalLeft));
        mModified = true;
    }

    void FreeCameraController::roll(double value)
    {
        setView(CameraPose::rotatedInEye(getView(), value, LocalForward));
        mModified = true;
    }

    void FreeCameraController::translate(const vsg::dvec3& offset)
    {
        setView(CameraPose::translatedInEye(getView(), offset));
        mModified = true;
    }

    void FreeCameraController::stabilize()
    {
        const auto [eye, center, up] = getLookAt();
        setLookAt(eye, center, mUp);
    }

    void FreeCameraController::naviPrimary(bool active)
    {
        mNaviPrimary = active;
    }

    void FreeCameraController::naviSecondary(bool active)
    {
        mNaviSecondary = active;
    }

    void FreeCameraController::forward(bool active)
    {
        mForward = active;
    }

    void FreeCameraController::left(bool active)
    {
        mLeft = active;
    }

    void FreeCameraController::backward(bool active)
    {
        mBackward = active;
    }

    void FreeCameraController::right(bool active)
    {
        mRight = active;
    }

    void FreeCameraController::rollLeft(bool active)
    {
        mRollLeft = active;
    }

    void FreeCameraController::rollRight(bool active)
    {
        mRollRight = active;
    }

    void FreeCameraController::alternateFast(bool active)
    {
        mFastAlternate = active;
    }

    void FreeCameraController::swapSpeedMode()
    {
        mFast = !mFast;
    }

    /*
    Orbit Camera Controller
    */

    OrbitCameraController::OrbitCameraController(QWidget* widget)
        : CameraController(widget)
        , mInitialized(false)
        , mNaviPrimary(false)
        , mNaviSecondary(false)
        , mFast(false)
        , mFastAlternate(false)
        , mLeft(false)
        , mRight(false)
        , mUp(false)
        , mDown(false)
        , mRollLeft(false)
        , mRollRight(false)
        , mPickingMask(~0u)
        , mCenter(0, 0, 0)
        , mDistance(0)
        , mOrbitSpeed(sPi / 4)
        , mOrbitSpeedMult(4)
        , mConstRoll(false)
    {
        CSMPrefs::Shortcut* naviPrimaryShortcut = new CSMPrefs::Shortcut("scene-navi-primary", widget);
        naviPrimaryShortcut->enable(false);
        connect(naviPrimaryShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this,
            &OrbitCameraController::naviPrimary);

        addShortcut(naviPrimaryShortcut);

        CSMPrefs::Shortcut* naviSecondaryShortcut = new CSMPrefs::Shortcut("scene-navi-secondary", widget);
        naviSecondaryShortcut->enable(false);
        connect(naviSecondaryShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this,
            &OrbitCameraController::naviSecondary);

        addShortcut(naviSecondaryShortcut);

        CSMPrefs::Shortcut* upShortcut
            = new CSMPrefs::Shortcut("orbit-up", "scene-speed-modifier", CSMPrefs::Shortcut::SM_Detach, widget);
        upShortcut->enable(false);
        connect(upShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &OrbitCameraController::up);
        connect(
            upShortcut, qOverload<bool>(&CSMPrefs::Shortcut::secondary), this, &OrbitCameraController::alternateFast);

        addShortcut(upShortcut);

        CSMPrefs::Shortcut* leftShortcut = new CSMPrefs::Shortcut("orbit-left", widget);
        leftShortcut->enable(false);
        connect(leftShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &OrbitCameraController::left);

        addShortcut(leftShortcut);

        CSMPrefs::Shortcut* downShortcut = new CSMPrefs::Shortcut("orbit-down", widget);
        downShortcut->enable(false);
        connect(downShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &OrbitCameraController::down);

        addShortcut(downShortcut);

        CSMPrefs::Shortcut* rightShortcut = new CSMPrefs::Shortcut("orbit-right", widget);
        rightShortcut->enable(false);
        connect(rightShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &OrbitCameraController::right);

        addShortcut(rightShortcut);

        CSMPrefs::Shortcut* rollLeftShortcut = new CSMPrefs::Shortcut("orbit-roll-left", widget);
        rollLeftShortcut->enable(false);
        connect(
            rollLeftShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &OrbitCameraController::rollLeft);

        addShortcut(rollLeftShortcut);

        CSMPrefs::Shortcut* rollRightShortcut = new CSMPrefs::Shortcut("orbit-roll-right", widget);
        rollRightShortcut->enable(false);
        connect(rollRightShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this,
            &OrbitCameraController::rollRight);

        addShortcut(rollRightShortcut);

        CSMPrefs::Shortcut* speedModeShortcut = new CSMPrefs::Shortcut("orbit-speed-mode", widget);
        speedModeShortcut->enable(false);
        connect(speedModeShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this,
            &OrbitCameraController::swapSpeedMode);

        addShortcut(speedModeShortcut);
    }

    vsg::dvec3 OrbitCameraController::getCenter() const
    {
        return mCenter;
    }

    double OrbitCameraController::getOrbitSpeed() const
    {
        return mOrbitSpeed;
    }

    double OrbitCameraController::getOrbitSpeedMultiplier() const
    {
        return mOrbitSpeedMult;
    }

    unsigned int OrbitCameraController::getPickingMask() const
    {
        return mPickingMask;
    }

    void OrbitCameraController::setCenter(const vsg::dvec3& value)
    {
        if (!isActive())
            return;

        const auto [eye, center, up] = getLookAt();

        mCenter = value;
        mDistance = vsg::length(eye - mCenter);

        setLookAt(eye, mCenter, up);

        mInitialized = true;
    }

    void OrbitCameraController::setOrbitSpeed(double value)
    {
        mOrbitSpeed = value;
    }

    void OrbitCameraController::setOrbitSpeedMultiplier(double value)
    {
        mOrbitSpeedMult = value;
    }

    void OrbitCameraController::setPickingMask(unsigned int value)
    {
        mPickingMask = value;
    }

    void OrbitCameraController::handleMouseMoveEvent(int x, int y)
    {
        if (!isActive())
            return;

        if (!mInitialized)
            initialize();

        if (mNaviPrimary)
        {
            double scalar = getCameraSensitivity() * (getInverted() ? -1.0 : 1.0);
            rotateHorizontal(x * scalar);
            rotateVertical(-y * scalar);
        }
        else if (mNaviSecondary)
        {
            vsg::dvec3 movement;
            movement += LocalLeft * x * getSecondaryMovementMultiplier();
            movement += LocalUp * -y * getSecondaryMovementMultiplier();

            translate(movement);
        }
    }

    void OrbitCameraController::handleMouseScrollEvent(int x)
    {
        if (!isActive())
            return;

        zoom(-x * ((mFast ^ mFastAlternate) ? getWheelMovementMultiplier() : 1));
    }

    void OrbitCameraController::update(double dt)
    {
        if (!isActive())
            return;

        if (!mInitialized)
            initialize();

        double rotDist = mOrbitSpeed * dt;

        if (mFast ^ mFastAlternate)
            rotDist *= mOrbitSpeedMult;

        if (mLeft)
            rotateHorizontal(-rotDist);
        if (mRight)
            rotateHorizontal(rotDist);
        if (mUp)
            rotateVertical(rotDist);
        if (mDown)
            rotateVertical(-rotDist);

        if (mRollLeft)
            roll(-rotDist);
        if (mRollRight)
            roll(rotDist);

        // Normalize the matrix to counter drift
        setView(CameraPose::orthoNormal(getView()));
    }

    void OrbitCameraController::reset()
    {
        mInitialized = false;
    }

    void OrbitCameraController::initialize()
    {
        const int defaultStartDistance = 10000.f;

        const auto [eye, center, up] = getLookAt(defaultStartDistance);

        // Try to intelligently pick focus object
        if (const std::optional<vsg::dvec3> hit = getCamera()->pickNearest(0.5, 0.5, mPickingMask))
        {
            mCenter = *hit;
            mDistance = vsg::length(eye - mCenter);
        }
        else
        {
            mCenter = center;
            mDistance = defaultStartDistance;
        }

        mInitialized = true;
    }

    void OrbitCameraController::setConstRoll(bool enabled)
    {
        mConstRoll = enabled;
    }

    void OrbitCameraController::rotateHorizontal(double value)
    {
        auto [eye, center, up] = getLookAt();
        const vsg::dvec3 axis = mConstRoll ? WorldUp : up;

        const vsg::dvec3 newOffset = CameraPose::rotated(eye - mCenter, value, axis);

        if (mConstRoll)
            up = CameraPose::rotated(up, value, axis);

        setLookAt(mCenter + newOffset, mCenter, up);
    }

    void OrbitCameraController::rotateVertical(double value)
    {
        auto [eye, center, up] = getLookAt();

        const vsg::dvec3 axis = vsg::cross(up, center - eye);

        const vsg::dvec3 newOffset = CameraPose::rotated(eye - mCenter, value, axis);

        if (mConstRoll)
            up = CameraPose::rotated(up, value, axis);

        setLookAt(mCenter + newOffset, mCenter, up);
    }

    void OrbitCameraController::roll(double value)
    {
        setView(CameraPose::rotatedInEye(getView(), value, LocalForward));
    }

    void OrbitCameraController::translate(const vsg::dvec3& offset)
    {
        auto [eye, center, up] = getLookAt();

        const vsg::dvec3 newOffset = CameraPose::eyeToWorldDirection(getView(), offset);
        mCenter += newOffset;
        eye += newOffset;

        setLookAt(eye, mCenter, up);
    }

    void OrbitCameraController::zoom(double value)
    {
        mDistance = std::max(10., mDistance + value);

        const auto [eye, center, up] = getLookAt(1.0);

        const vsg::dvec3 offset = (eye - center) * mDistance;

        setLookAt(mCenter + offset, mCenter, up);
    }

    void OrbitCameraController::naviPrimary(bool active)
    {
        mNaviPrimary = active;
    }

    void OrbitCameraController::naviSecondary(bool active)
    {
        mNaviSecondary = active;
    }

    void OrbitCameraController::up(bool active)
    {
        mUp = active;
    }

    void OrbitCameraController::left(bool active)
    {
        mLeft = active;
    }

    void OrbitCameraController::down(bool active)
    {
        mDown = active;
    }

    void OrbitCameraController::right(bool active)
    {
        mRight = active;
    }

    void OrbitCameraController::rollLeft(bool active)
    {
        if (isActive())
            mRollLeft = active;
    }

    void OrbitCameraController::rollRight(bool active)
    {
        mRollRight = active;
    }

    void OrbitCameraController::alternateFast(bool active)
    {
        mFastAlternate = active;
    }

    void OrbitCameraController::swapSpeedMode()
    {
        mFast = !mFast;
    }
}
