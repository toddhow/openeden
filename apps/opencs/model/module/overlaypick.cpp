#include "overlaypick.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace CSMModule
{
    using openeden::editor::Vec3;

    namespace
    {
        // Distances that differ by at most this fraction are a tie (decided by the item index).
        constexpr double cTieRelative = 1.0e-9;
        constexpr double cParallelEpsilon = 1.0e-12;

        Vec3 sub(const Vec3& a, const Vec3& b)
        {
            return { a.x - b.x, a.y - b.y, a.z - b.z };
        }
        Vec3 add(const Vec3& a, const Vec3& b)
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }
        Vec3 scale(const Vec3& a, double s)
        {
            return { a.x * s, a.y * s, a.z * s };
        }
        double dot(const Vec3& a, const Vec3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
        Vec3 cross(const Vec3& a, const Vec3& b)
        {
            return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        }
        double length(const Vec3& a)
        {
            return std::sqrt(dot(a, a));
        }
        bool finite(const Vec3& a)
        {
            return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
        }

        /// The ray with a unit direction, or nothing for a zero or non-finite one.
        std::optional<PickRay> normalised(const PickRay& ray)
        {
            if (!finite(ray.mOrigin) || !finite(ray.mDirection))
                return std::nullopt;
            const double len = length(ray.mDirection);
            if (!(len > 0.0) || !std::isfinite(len))
                return std::nullopt;
            return PickRay{ ray.mOrigin, scale(ray.mDirection, 1.0 / len) };
        }

        double pointMiss(const PickRay& unit, const Vec3& p, double& t)
        {
            const Vec3 v = sub(p, unit.mOrigin);
            t = std::max(0.0, dot(v, unit.mDirection));
            return length(sub(v, scale(unit.mDirection, t)));
        }

        double segmentMiss(const PickRay& unit, const Vec3& a, const Vec3& b, double& t)
        {
            // Closest points between the ray o + t d (t >= 0) and the segment a + s (b - a), s in [0, 1].
            const Vec3 e = sub(b, a);
            const double ee = dot(e, e);
            if (!(ee > cParallelEpsilon * cParallelEpsilon))
                return pointMiss(unit, a, t);
            const Vec3 w = sub(unit.mOrigin, a);
            const double de = dot(unit.mDirection, e);
            const double dw = dot(unit.mDirection, w);
            const double ew = dot(e, w);
            const double denominator = ee - de * de; // d is a unit vector
            // The optimum lies at one of these parameters of the segment: both free (the unconstrained solution), the
            // ray clamped at its origin (the origin's projection onto the segment), or an end. Take the best.
            double candidates[4] = { 0.0, 1.0, std::clamp(ew / ee, 0.0, 1.0), 0.0 };
            if (denominator > cParallelEpsilon * ee)
                candidates[3] = std::clamp((ew - de * dw) / denominator, 0.0, 1.0);
            double bestMiss = std::numeric_limits<double>::infinity();
            double bestT = 0.0;
            for (const double s : candidates)
            {
                double candidateT = 0.0;
                const double miss = pointMiss(unit, add(a, scale(e, s)), candidateT);
                if (miss < bestMiss)
                {
                    bestMiss = miss;
                    bestT = candidateT;
                }
            }
            t = bestT;
            return bestMiss;
        }
    }

    double pickSlope(double pixels, double fovYRadians, double viewportHeightPixels)
    {
        const double pi = 3.14159265358979323846;
        if (!(viewportHeightPixels > 0.0) || !(fovYRadians > 0.0) || !(fovYRadians < pi) || !(pixels >= 0.0))
            return 0.0;
        return pixels * 2.0 * std::tan(fovYRadians / 2.0) / viewportHeightPixels;
    }

    double rayPointMiss(const PickRay& ray, const Vec3& point, double& t)
    {
        const auto unit = normalised(ray);
        if (!unit || !finite(point))
        {
            t = 0.0;
            return std::numeric_limits<double>::infinity();
        }
        return pointMiss(*unit, point, t);
    }

    double raySegmentMiss(const PickRay& ray, const Vec3& a, const Vec3& b, double& t)
    {
        const auto unit = normalised(ray);
        if (!unit || !finite(a) || !finite(b))
        {
            t = 0.0;
            return std::numeric_limits<double>::infinity();
        }
        return segmentMiss(*unit, a, b, t);
    }

    std::optional<double> rayTriangleDistance(const PickRay& ray, const Vec3& a, const Vec3& b, const Vec3& c)
    {
        const auto unit = normalised(ray);
        if (!unit || !finite(a) || !finite(b) || !finite(c))
            return std::nullopt;
        // Moller-Trumbore, both sides.
        const Vec3 e1 = sub(b, a);
        const Vec3 e2 = sub(c, a);
        const Vec3 p = cross(unit->mDirection, e2);
        const double determinant = dot(e1, p);
        if (std::abs(determinant) < cParallelEpsilon)
            return std::nullopt;
        const double inverse = 1.0 / determinant;
        const Vec3 tv = sub(unit->mOrigin, a);
        const double u = dot(tv, p) * inverse;
        if (u < 0.0 || u > 1.0)
            return std::nullopt;
        const Vec3 q = cross(tv, e1);
        const double v = dot(unit->mDirection, q) * inverse;
        if (v < 0.0 || u + v > 1.0)
            return std::nullopt;
        const double t = dot(e2, q) * inverse;
        if (!(t > 0.0))
            return std::nullopt;
        return t;
    }

    std::optional<PickHit> pickNearest(const PickRay& ray, const std::vector<PickItem>& items, double slope)
    {
        const auto unit = normalised(ray);
        if (!unit || !(slope >= 0.0))
            return std::nullopt;

        std::optional<PickHit> best;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const PickItem& item = items[i];
            double t = 0.0;
            double miss = 0.0;
            bool hit = false;
            switch (item.mKind)
            {
                case PickItem::Kind::Point:
                    if (finite(item.mA))
                    {
                        miss = pointMiss(*unit, item.mA, t);
                        hit = t > 0.0 && miss <= slope * t;
                    }
                    break;
                case PickItem::Kind::Segment:
                    if (finite(item.mA) && finite(item.mB))
                    {
                        miss = segmentMiss(*unit, item.mA, item.mB, t);
                        hit = t > 0.0 && miss <= slope * t;
                    }
                    break;
                case PickItem::Kind::Triangle:
                    if (const auto distance = rayTriangleDistance(*unit, item.mA, item.mB, item.mC))
                    {
                        t = *distance;
                        miss = 0.0;
                        hit = true;
                    }
                    break;
            }
            if (!hit)
                continue;
            // Strictly nearer by more than the tie margin replaces; a tie keeps the earlier (lower) index.
            if (!best || t < best->mDistance - cTieRelative * std::max(1.0, best->mDistance))
                best = PickHit{ i, t, miss };
        }
        return best;
    }

    std::optional<Vec3> rayPlaneHit(const PickRay& ray, const Vec3& pointOnPlane, const Vec3& normal)
    {
        const auto unit = normalised(ray);
        if (!unit || !finite(pointOnPlane) || !finite(normal))
            return std::nullopt;
        const double denominator = dot(normal, unit->mDirection);
        if (std::abs(denominator) < cParallelEpsilon)
            return std::nullopt;
        const double t = dot(sub(pointOnPlane, unit->mOrigin), normal) / denominator;
        if (!(t > 0.0))
            return std::nullopt;
        return add(unit->mOrigin, scale(unit->mDirection, t));
    }
}
