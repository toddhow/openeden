#ifndef CSM_MESH_FRAMEMATH_H
#define CSM_MESH_FRAMEMATH_H

#include <array>

namespace CSMMesh
{
    /// An axis-aligned box in doubles. Default: invalid (nothing in it yet).
    struct Aabb
    {
        std::array<double, 3> min{};
        std::array<double, 3> max{};
        bool valid = false;
    };

    void expand(Aabb& box, const std::array<double, 3>& point);
    /// An invalid box is ignored.
    Aabb merged(const Aabb& a, const Aabb& b);
    /// The bounds of the eight transformed corners. \param columnMajor a 4x4 matrix, translation in elements 12..14.
    Aabb transformed(const Aabb& box, const std::array<double, 16>& columnMajor);
    std::array<double, 3> centre(const Aabb& box);
    /// Half the diagonal; 0 for an invalid box.
    double radius(const Aabb& box);

    struct FramePose
    {
        std::array<double, 3> eye;
        std::array<double, 3> centre;
        std::array<double, 3> up;
    };

    /// The "frame object" camera (task 0043): looks at the box's centre along \a viewDirection from far enough that
    /// the box's bounding sphere fits the narrower field of view, with \a margin to spare. Z is up. An invalid or
    /// zero-radius box counts as radius 1 unit. \throws std::invalid_argument for a zero \a viewDirection.
    FramePose frameCamera(const Aabb& box, double fovYDegrees, double aspect,
        const std::array<double, 3>& viewDirection = { 0.5, -1.0, 0.35 }, double margin = 1.15);
}

#endif
