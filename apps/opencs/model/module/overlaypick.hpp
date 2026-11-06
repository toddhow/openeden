#ifndef CSM_MODULE_OVERLAYPICK_H
#define CSM_MODULE_OVERLAYPICK_H

#include <cstddef>
#include <optional>
#include <vector>

#include <openeden/editor_module/types.hpp>

namespace CSMModule
{
    /// Picking of overlay data and the maths under it (task 0091 stage c). Pure: no Qt, no vsg, no window. Every
    /// length is in one unit of the caller's choice (Studio uses meters, converting the camera ray once at the
    /// boundary), and a pixel tolerance becomes a slope: how far from the ray an item may be per unit of distance
    /// along the ray, so that a handle stays as easy to hit at any distance.

    struct PickRay
    {
        openeden::editor::Vec3 mOrigin;
        openeden::editor::Vec3 mDirection; ///< Need not be a unit vector; must not be zero.
    };

    struct PickItem
    {
        enum class Kind
        {
            Point, ///< mA; a handle.
            Segment, ///< mA to mB; one overlay line.
            Triangle ///< mA, mB, mC; hit exactly (both sides), no tolerance.
        };
        Kind mKind = Kind::Point;
        openeden::editor::Vec3 mA;
        openeden::editor::Vec3 mB;
        openeden::editor::Vec3 mC;
    };

    struct PickHit
    {
        std::size_t mIndex = 0; ///< Index into the items.
        double mDistance
            = 0; ///< Distance along the ray (in units of its direction's length: the direction is normalised).
        double mMiss = 0; ///< How far the item is from the ray there (0 for a triangle).
    };

    /// The tolerance slope of \a pixels on a perspective view with the vertical field of view \a fovYRadians and a
    /// viewport \a viewportHeightPixels high: pixels * 2 * tan(fov / 2) / height. 0 for a non-positive height or an
    /// invalid field of view.
    double pickSlope(double pixels, double fovYRadians, double viewportHeightPixels);

    /// Closest approach of the ray (t >= 0) to a point: the distance along the ray in \a t and the distance between
    /// them returned. A point behind the origin gives t = 0 (and the distance to the origin).
    double rayPointMiss(const PickRay& ray, const openeden::editor::Vec3& point, double& t);

    /// The same for a segment (its ends clamp). A segment parallel to the ray is measured at the ray's origin side.
    double raySegmentMiss(
        const PickRay& ray, const openeden::editor::Vec3& a, const openeden::editor::Vec3& b, double& t);

    /// Distance along the ray (normalised direction) to the triangle, or nothing. Both sides hit; a hit at t <= 0 or a
    /// ray parallel to the triangle's plane misses.
    std::optional<double> rayTriangleDistance(const PickRay& ray, const openeden::editor::Vec3& a,
        const openeden::editor::Vec3& b, const openeden::editor::Vec3& c);

    /// The nearest item the ray hits: a point or segment within slope * distance, a triangle exactly. The nearest is
    /// the smallest distance along the ray; items whose distances differ by at most 1e-9 (relative) are a tie and the
    /// lower index wins, so the result never depends on anything but the items and their order. Items with a
    /// non-finite coordinate and a ray with a zero or non-finite direction give nothing.
    std::optional<PickHit> pickNearest(const PickRay& ray, const std::vector<PickItem>& items, double slope);

    /// Where the ray meets the plane through \a pointOnPlane with \a normal, or nothing when it is parallel (or
    /// the intersection is behind the origin).
    std::optional<openeden::editor::Vec3> rayPlaneHit(
        const PickRay& ray, const openeden::editor::Vec3& pointOnPlane, const openeden::editor::Vec3& normal);
}

#endif
