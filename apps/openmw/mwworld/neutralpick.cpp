// Task 0233: see neutralpick.hpp.
#include "neutralpick.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MWWorld
{
    namespace
    {
        bool finite(const vsg::dvec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        double dot(const vsg::dvec3& a, const vsg::dvec3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        vsg::dvec3 cross(const vsg::dvec3& a, const vsg::dvec3& b)
        {
            return vsg::dvec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        }

        /// The active rotation of `v` by the unit quaternion (x, y, z, w).
        vsg::dvec3 rotate(double qx, double qy, double qz, double qw, const vsg::dvec3& v)
        {
            const vsg::dvec3 u(qx, qy, qz);
            const vsg::dvec3 t = cross(u, v) * 2.0;
            return v + t * qw + cross(u, t);
        }
    }

    std::optional<NeutralBoxHit> rayOrientedBox(const vsg::dvec3& origin, const vsg::dvec3& direction,
        double maxDistance, const vsg::dvec3& boxMin, const vsg::dvec3& boxMax, const vsg::dvec3& position,
        const vsg::dquat& rotation, double scale)
    {
        if (!finite(origin) || !finite(direction) || !finite(boxMin) || !finite(boxMax) || !finite(position)
            || !std::isfinite(maxDistance) || !std::isfinite(scale) || !(scale > 0.0) || !std::isfinite(rotation.x)
            || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) || !std::isfinite(rotation.w))
            return std::nullopt;
        if (boxMin.x > boxMax.x || boxMin.y > boxMax.y || boxMin.z > boxMax.z)
            return std::nullopt;
        const double length = std::sqrt(dot(direction, direction));
        const double turn = std::sqrt(
            rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
        if (!(length > 1e-12) || !(turn > 1e-12))
            return std::nullopt;
        // The ray in the box's own frame (the parameter t stays the world distance: the scale is uniform).
        const double qx = -rotation.x / turn, qy = -rotation.y / turn, qz = -rotation.z / turn, qw = rotation.w / turn;
        const vsg::dvec3 unit = direction * (1.0 / length);
        const vsg::dvec3 o = rotate(qx, qy, qz, qw, origin - position) * (1.0 / scale);
        const vsg::dvec3 d = rotate(qx, qy, qz, qw, unit) * (1.0 / scale);

        double entry = 0.0;
        double exit = std::numeric_limits<double>::infinity();
        const double start[3] = { o.x, o.y, o.z };
        const double step[3] = { d.x, d.y, d.z };
        const double low[3] = { boxMin.x, boxMin.y, boxMin.z };
        const double high[3] = { boxMax.x, boxMax.y, boxMax.z };
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(step[axis]) < 1e-12)
            {
                if (start[axis] < low[axis] || start[axis] > high[axis])
                    return std::nullopt; // parallel to the slab and outside it
                continue;
            }
            double t1 = (low[axis] - start[axis]) / step[axis];
            double t2 = (high[axis] - start[axis]) / step[axis];
            if (t1 > t2)
                std::swap(t1, t2);
            entry = std::max(entry, t1);
            exit = std::min(exit, t2);
        }
        if (exit < entry || entry > maxDistance)
            return std::nullopt;
        return NeutralBoxHit{ entry, exit };
    }

    bool boxBeatsRenderHit(const NeutralBoxHit& box, double renderDistance, bool renderHitCanBeActivated)
    {
        if (!std::isfinite(renderDistance) || renderDistance < 0.0)
            return true;
        return renderHitCanBeActivated ? renderDistance > box.mExit : renderDistance > box.mEntry;
    }
}
