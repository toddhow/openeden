#ifndef OPENEDEN_RENDER_POINTSHADOWFACES_H
#define OPENEDEN_RENDER_POINTSHADOWFACES_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <vsg/maths/vec3.h>

#include "pointshadowmath.hpp"

namespace Render::PointShadow
{
    /*
     * Which of a light's six shadow faces are drawn (task 0362, item 1; follow-up of task 0355 stage g). A game's
     * hemisphere light only lights the half of space in front of its axis and a spot light only the cone round it,
     * so the faces that lie wholly outside that region are never drawn and never sampled: the shader reads the
     * light's face mask (the z of its entry in the shadow data buffer) and treats a face that is not in it as "no
     * light there", which is what the light's own cone or half space gives anyway.
     *
     * A face covers the directions whose largest component is its axis (the wedge the shader chooses the face by).
     * A face is in the mask when the region reaches its wedge. Pure maths, no clock, no allocation.
     */

    /// What a light's shadow covers (the axis is the light's local +X, as for a spot light: stage g).
    enum class Shape : std::uint8_t
    {
        Omni = 0, ///< all six faces
        Hemisphere = 1, ///< the half space in front of the axis
        Spot = 2, ///< the cone round the axis
    };

    constexpr std::uint8_t allFacesMask = 0x3F;

    /// Extra angle added to a spot's half angle so the filter taps of the shadow lookup near the cone's edge find a
    /// face that is drawn (radians).
    constexpr double spotFaceMarginRadians = 0.12;

    /// The cosine of the smallest angle between the unit vector `axis` and any direction of the face's wedge (1 when
    /// the axis lies in the wedge; 0 or less when the wedge is 90 degrees or more away).
    inline double wedgeCosine(const vsg::dvec3& axis, int face)
    {
        const FaceAxes f = faceAxes(face);
        const double dF = vsg::dot(axis, f.forward);
        const double d1 = vsg::dot(axis, f.right);
        const double d2 = vsg::dot(axis, f.up);
        if (dF >= std::fabs(d1) && dF >= std::fabs(d2))
            return 1.0;
        // The wedge is the cone spanned by four edge rays, F +- P1 +- P2, in cyclic order.
        const double inv = 1.0 / std::sqrt(3.0);
        const double sign1[4] = { 1.0, 1.0, -1.0, -1.0 };
        const double sign2[4] = { 1.0, -1.0, -1.0, 1.0 };
        double dots[4];
        for (int i = 0; i < 4; ++i)
            dots[i] = (dF + sign1[i] * d1 + sign2[i] * d2) * inv;
        double best = 0.0;
        for (int i = 0; i < 4; ++i)
            best = std::max(best, dots[i]);
        // A face of the cone: project onto the plane of two neighbouring edge rays, if the projection lies between.
        for (int i = 0; i < 4; ++i)
        {
            const int j = (i + 1) % 4;
            const double g = (1.0 + sign1[i] * sign1[j] * 1.0 + sign2[i] * sign2[j] * 1.0) * inv * inv; // e_i . e_j
            const double det = 1.0 - g * g;
            if (!(det > 1e-12))
                continue;
            const double c1 = (dots[i] - g * dots[j]) / det;
            const double c2 = (dots[j] - g * dots[i]) / det;
            if (c1 >= 0.0 && c2 >= 0.0)
                best = std::max(best, std::sqrt(std::max(0.0, c1 * dots[i] + c2 * dots[j])));
        }
        return std::min(best, 1.0);
    }

    /// Bit f of the result is set when face f is drawn for a light of this shape. `axis` need not be a unit vector;
    /// an unusable axis (zero, not finite) or a spot without a usable cone gives all six faces. `coneCos` is the cosine
    /// of a spot's half angle.
    inline std::uint8_t faceMask(Shape shape, const vsg::dvec3& axis, double coneCos)
    {
        if (shape == Shape::Omni)
            return allFacesMask;
        const double length = std::sqrt(vsg::dot(axis, axis));
        if (!std::isfinite(length) || !(length > 1e-9))
            return allFacesMask;
        double cosLimit = 0.0; // a hemisphere: the faces the half space reaches
        if (shape == Shape::Spot)
        {
            if (!(coneCos > 0.0) || !(coneCos < 1.0))
                return allFacesMask;
            const double theta = std::acos(coneCos) + spotFaceMarginRadians;
            constexpr double halfPi = 1.57079632679489661923;
            cosLimit = theta >= halfPi ? 0.0 : std::cos(theta);
        }
        const vsg::dvec3 unit = axis / length;
        std::uint8_t mask = 0;
        for (int f = 0; f < facesPerLight; ++f)
        {
            const double best = wedgeCosine(unit, f);
            if (best > 0.0 && best >= cosLimit - 1e-12)
                mask = static_cast<std::uint8_t>(mask | (1u << f));
        }
        return mask == 0 ? allFacesMask : mask;
    }

    inline int faceCount(std::uint8_t mask)
    {
        int n = 0;
        for (int f = 0; f < facesPerLight; ++f)
            n += (mask >> f) & 1;
        return n;
    }
}

#endif
