#include "framemath.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CSMMesh
{
    namespace
    {
        constexpr double sPi = 3.14159265358979323846;
    }

    void expand(Aabb& box, const std::array<double, 3>& point)
    {
        if (!box.valid)
        {
            box.min = point;
            box.max = point;
            box.valid = true;
            return;
        }
        for (int i = 0; i < 3; ++i)
        {
            box.min[i] = std::min(box.min[i], point[i]);
            box.max[i] = std::max(box.max[i], point[i]);
        }
    }

    Aabb merged(const Aabb& a, const Aabb& b)
    {
        if (!a.valid)
            return b;
        if (!b.valid)
            return a;
        Aabb result = a;
        expand(result, b.min);
        expand(result, b.max);
        return result;
    }

    Aabb transformed(const Aabb& box, const std::array<double, 16>& m)
    {
        if (!box.valid)
            return Aabb{};
        Aabb result;
        for (int corner = 0; corner < 8; ++corner)
        {
            const double x = (corner & 1) ? box.max[0] : box.min[0];
            const double y = (corner & 2) ? box.max[1] : box.min[1];
            const double z = (corner & 4) ? box.max[2] : box.min[2];
            expand(result,
                { m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13],
                    m[2] * x + m[6] * y + m[10] * z + m[14] });
        }
        return result;
    }

    std::array<double, 3> centre(const Aabb& box)
    {
        return { (box.min[0] + box.max[0]) * 0.5, (box.min[1] + box.max[1]) * 0.5, (box.min[2] + box.max[2]) * 0.5 };
    }

    double radius(const Aabb& box)
    {
        if (!box.valid)
            return 0.0;
        const double dx = box.max[0] - box.min[0];
        const double dy = box.max[1] - box.min[1];
        const double dz = box.max[2] - box.min[2];
        return 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    FramePose frameCamera(
        const Aabb& box, double fovYDegrees, double aspect, const std::array<double, 3>& viewDirection, double margin)
    {
        const double length = std::sqrt(
            viewDirection[0] * viewDirection[0] + viewDirection[1] * viewDirection[1]
            + viewDirection[2] * viewDirection[2]);
        if (length == 0.0)
            throw std::invalid_argument("the view direction of frameCamera must not be zero");

        double r = radius(box);
        if (r <= 0.0)
            r = 1.0;
        const double fovY = fovYDegrees * sPi / 180.0;
        const double fovX = 2.0 * std::atan(std::tan(fovY / 2.0) * aspect);
        const double distance = margin * r / std::sin(std::min(fovY, fovX) / 2.0);

        FramePose pose;
        pose.centre = box.valid ? CSMMesh::centre(box) : std::array<double, 3>{ 0.0, 0.0, 0.0 };
        for (int i = 0; i < 3; ++i)
            pose.eye[i] = pose.centre[i] - viewDirection[i] / length * distance;
        pose.up = { 0.0, 0.0, 1.0 };
        return pose;
    }
}
