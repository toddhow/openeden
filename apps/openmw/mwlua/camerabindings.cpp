#include "camerabindings.hpp"

#include <components/lua/luastate.hpp>
#include <components/lua/utilpackage.hpp>
#include <components/misc/finitevalues.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/settings/values.hpp>

#include <vsg/maths/transform.h>

#include "../mwrender/camera.hpp"
#include "../mwrender/rendermanager.hpp"

namespace MWLua
{
    using CameraMode = MWRender::Camera::Mode;

    sol::table initCameraPackage(sol::state_view lua, MWRender::RenderManager* renderingManager)
    {
        using Misc::FiniteFloat;

        MWRender::Camera* camera = renderingManager->getCamera();

        sol::table api(lua, sol::create);
        api["MODE"] = LuaUtil::makeStrictReadOnly(
            lua.create_table_with("Static", CameraMode::Static, "FirstPerson", CameraMode::FirstPerson, "ThirdPerson",
                CameraMode::ThirdPerson, "Vanity", CameraMode::Vanity, "Preview", CameraMode::Preview));

        api["getMode"] = [camera]() -> int { return static_cast<int>(camera->getMode()); };
        api["getQueuedMode"] = [camera]() -> sol::optional<int> {
            std::optional<CameraMode> mode = camera->getQueuedMode();
            if (mode)
                return static_cast<int>(*mode);
            else
                return sol::nullopt;
        };
        api["setMode"] = [camera](int mode, sol::optional<bool> force) {
            camera->setMode(static_cast<CameraMode>(mode), force ? *force : false);
        };

        api["allowCharacterDeferredRotation"] = [camera](bool v) { camera->allowCharacterDeferredRotation(v); };
        api["showCrosshair"] = [camera](bool v) { camera->showCrosshair(v); };

        api["getTrackedPosition"] = [camera]() -> vsg::vec3 { return vsg::vec3(camera->getTrackedPosition()); };
        api["getPosition"] = [camera]() -> vsg::vec3 { return vsg::vec3(camera->getPosition()); };

        // All angles are negated in order to make camera rotation consistent with objects rotation.
        // TODO: Fix the inconsistency of rotation direction in camera.cpp.
        api["getPitch"] = [camera]() { return -camera->getPitch(); };
        api["getYaw"] = [camera]() { return -camera->getYaw(); };
        api["getRoll"] = [camera]() { return -camera->getRoll(); };

        api["setStaticPosition"] = [camera](const vsg::vec3& pos) { camera->setStaticPosition(vsg::dvec3(pos)); };
        api["setPitch"] = [camera](const FiniteFloat v) {
            camera->setPitch(-v, true);
            if (camera->getMode() == CameraMode::ThirdPerson)
                camera->calculateDeferredRotation();
        };
        api["setYaw"] = [camera](const FiniteFloat v) {
            camera->setYaw(-v, true);
            if (camera->getMode() == CameraMode::ThirdPerson)
                camera->calculateDeferredRotation();
        };
        api["setRoll"] = [camera](const FiniteFloat v) { camera->setRoll(-v); };
        api["setExtraPitch"] = [camera](const FiniteFloat v) { camera->setExtraPitch(-v); };
        api["setExtraYaw"] = [camera](const FiniteFloat v) { camera->setExtraYaw(-v); };
        api["setExtraRoll"] = [camera](const FiniteFloat v) { camera->setExtraRoll(-v); };
        api["getExtraPitch"] = [camera]() { return -camera->getExtraPitch(); };
        api["getExtraYaw"] = [camera]() { return -camera->getExtraYaw(); };
        api["getExtraRoll"] = [camera]() { return -camera->getExtraRoll(); };

        api["getThirdPersonDistance"] = [camera]() { return camera->getCameraDistance(); };
        api["setPreferredThirdPersonDistance"]
            = [camera](const FiniteFloat v) { camera->setPreferredCameraDistance(v); };

        api["getFirstPersonOffset"] = [camera]() { return camera->getFirstPersonOffset(); };
        api["setFirstPersonOffset"] = [camera](const vsg::vec3& v) { camera->setFirstPersonOffset(v); };

        api["getFocalPreferredOffset"]
            = [camera]() -> vsg::vec2 { return vsg::vec2(camera->getFocalPointTargetOffset()); };
        api["setFocalPreferredOffset"]
            = [camera](const vsg::vec2& v) { camera->setFocalPointTargetOffset(vsg::dvec2(v)); };
        api["getFocalTransitionSpeed"] = [camera]() { return camera->getFocalPointTransitionSpeed(); };
        api["setFocalTransitionSpeed"] = [camera](const FiniteFloat v) { camera->setFocalPointTransitionSpeed(v); };
        api["instantTransition"] = [camera]() { camera->instantTransition(); };

        api["getCollisionType"] = [camera]() { return camera->getCollisionType(); };
        api["setCollisionType"] = [camera](int collisionType) { camera->setCollisionType(collisionType); };

        api["getBaseFieldOfView"] = [] { return Misc::VsgMath::inDegrees(Settings::camera().mFieldOfView); };
        api["getFieldOfView"]
            = [renderingManager]() { return Misc::VsgMath::inDegrees(renderingManager->getFieldOfView()); };
        api["setFieldOfView"] = [renderingManager](const FiniteFloat v) {
            renderingManager->setFieldOfView(Misc::VsgMath::radiansToDegrees(v));
        };

        api["getBaseViewDistance"] = [] { return Settings::camera().mViewingDistance.get(); };
        api["getViewDistance"] = [renderingManager]() { return renderingManager->getViewDistance(); };
        api["setViewDistance"]
            = [renderingManager](const FiniteFloat d) { renderingManager->setViewDistance(d, true); };

        api["getViewTransform"] = [camera]() { return LuaUtil::TransformM{ camera->getViewMatrix() }; };

        api["viewportToWorldVector"] = [camera, renderingManager](vsg::vec2 pos) -> vsg::vec3 {
            const double width = Settings::video().mResolutionX;
            const double height = Settings::video().mResolutionY;
            double aspect = (height == 0.0) ? 1.0 : width / height;
            double fovTan = std::tan(Misc::VsgMath::inDegrees(renderingManager->getFieldOfView()) / 2);
            vsg::mat4 invertedViewMatrix;
            Misc::VsgMath::invert(camera->getViewMatrix(), invertedViewMatrix);
            float x = static_cast<float>((pos.x * 2 - 1) * aspect * fovTan);
            float y = static_cast<float>((1 - pos.y * 2) * fovTan);
            return invertedViewMatrix * vsg::vec3(x, y, -1) - vsg::vec3(camera->getPosition());
        };

        api["worldToViewportVector"] = [camera, renderingManager](vsg::vec3 pos) {
            const int width = Settings::video().mResolutionX;
            const int height = Settings::video().mResolutionY;

            vsg::dmat4 windowMatrix
                = vsg::scale(0.5 * width, 0.5 * height, 0.5) * vsg::translate(1.0, 1.0, 1.0);
            const vsg::mat4& viewMatrix = camera->getViewMatrix();
            const vsg::mat4 projectionMatrix(renderingManager->getProjectionMatrix());
            vsg::vec3 vpCoords = vsg::mat4(windowMatrix) * (projectionMatrix * viewMatrix) * pos;

            // Move 0,0 to top left to match viewportToWorldVector
            // TODO: Do we still need to flip with Vulkan?
            vpCoords.y = height - vpCoords.y;

            // Set the z component to be distance from camera, in world space units
            vpCoords.z = vsg::length(pos - vsg::vec3(camera->getPosition()));

            return vpCoords;
        };

        return LuaUtil::makeReadOnly(api);
    }

}
