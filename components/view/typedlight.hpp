#ifndef OPENEDEN_VIEW_TYPEDLIGHT_H
#define OPENEDEN_VIEW_TYPEDLIGHT_H

#include <cmath>
#include <cstdint>

#include <vsg/lighting/PointLight.h>
#include <vsg/maths/vec3.h>

#include <components/render/pointshadowselect.hpp>

namespace View
{
    /// cos of the half angle of a spot light whose full opening angle is `fovDegrees`; 0 (no cone) when the angle is
    /// not a number or not within 1 to 170 degrees.
    inline float spotConeCos(double fovDegrees)
    {
        if (!std::isfinite(fovDegrees) || fovDegrees < 1.0 || fovDegrees > 170.0)
            return 0.f;
        constexpr double degreesToRadians = 3.14159265358979323846 / 180.0;
        return static_cast<float>(std::cos(0.5 * fovDegrees * degreesToRadians));
    }

    /// A point light that also carries what a game's light record says about its kind (task 0355 stage g): a spot cone
    /// (axis in the light's own space, cos of the half angle) and whether it may take a point light shadow slot.
    /// Only a game pack's lights are made as this class (MWAnim::addLight); every Morrowind light stays a plain
    /// vsg::PointLight, which CollectLights treats as before. Owned by whatever owns the node (a vsg::ref_ptr).
    class TypedPointLight : public vsg::PointLight
    {
    public:
        Render::PointShadow::Rule shadowRule = Render::PointShadow::Rule::Unspecified;
        /// 0 when the light is not a spot.
        float spotCosHalf = 0.f;
        /// Task 0362: a shadowed light that lights only the half space in front of `spotAxis` (not a spot).
        bool shadowHemisphere = false;
        vsg::vec3 spotAxis{ 1.f, 0.f, 0.f };
    };
}

#endif
