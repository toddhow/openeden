// Capsule fit: docs/tasks/0009-a5-body-surfaces-fingers.md, A5a rules 1-4.

#include "opas/markers/CapsuleFit.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace opas::markers {
namespace {

bool isFinite(math::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

CapsuleFitResult fail(std::string message)
{
    CapsuleFitResult result;
    result.error = std::move(message);
    return result;
}

CapsuleFitResult fitUnchecked(const rig::Skeleton& skeleton, const BodyMapping& mapping,
                              std::span<const SurfacePoint> points,
                              const CapsuleFitSettings& settings)
{
    if (!(settings.percentile > 0.0) || !(settings.percentile <= 1.0)) {
        return fail("settings: percentile must be in (0, 1]");
    }
    if (settings.minPoints < 1) {
        return fail("settings: minPoints must be >= 1");
    }
    if (const std::string error = rig::validateSkeleton(skeleton); !error.empty()) {
        return fail("skeleton: " + error);
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const int bone = points[i].bone;
        if (!isFinite(points[i].positionM) || bone < 0 ||
            static_cast<std::size_t>(bone) >= skeleton.bones.size()) {
            return fail("point " + std::to_string(i) +
                        ": position must be finite and bone a valid index");
        }
    }
    const std::vector<math::Transform> world = rig::computeWorldRest(skeleton);

    CapsuleFitResult result;
    BodyMapping fitted = mapping;
    std::vector<double> distances;
    for (CapsuleRule& rule : fitted.capsules) {
        const auto a = rig::findBone(skeleton, rule.bone);
        const auto b = rig::findBone(skeleton, rule.endBone);
        if (!a || !b) {
            return fail("capsule \"" + rule.region + "\": bone \"" +
                        (a ? rule.endBone : rule.bone) + "\" is not in the skeleton");
        }
        const math::Vec3 start = world[*a].translation;
        const math::Vec3 axis = world[*b].translation - start;
        const double length2 = math::lengthSquared(axis);
        if (!(length2 > 0.0)) {
            return fail("capsule \"" + rule.region + "\": bone and endBone are at the same place");
        }
        // Rule 2: points of this bone that project onto the segment.
        distances.clear();
        for (const SurfacePoint& p : points) {
            if (static_cast<std::size_t>(p.bone) != *a) {
                continue;
            }
            const double t = math::dot(p.positionM - start, axis) / length2;
            if (t < 0.0 || t > 1.0) {
                continue;
            }
            distances.push_back(math::distance(p.positionM, start + t * axis));
        }
        result.pointsUsed.push_back(distances.size());
        if (distances.size() < settings.minPoints) {
            result.fitted.push_back(false); // keeps the rule's own ratio
            continue;
        }
        // Nearest-rank percentile: the smallest distance with at least
        // `percentile` of the points at or below it.
        const auto n = static_cast<double>(distances.size());
        const auto rank = static_cast<std::size_t>(std::ceil(settings.percentile * n));
        const std::size_t k = std::max<std::size_t>(rank, 1) - 1;
        std::nth_element(distances.begin(), distances.begin() + static_cast<std::ptrdiff_t>(k),
                         distances.end());
        const double radius = distances[k];
        if (radius > 0.0) {
            rule.radiusRatio = radius / std::sqrt(length2);
            result.fitted.push_back(true);
        } else {
            result.fitted.push_back(false); // all points on the axis: keep the ratio
        }
    }
    result.value = std::move(fitted);
    return result;
}

} // namespace

CapsuleFitResult fitCapsules(const rig::Skeleton& skeleton, const BodyMapping& mapping,
                             std::span<const SurfacePoint> points,
                             const CapsuleFitSettings& settings)
{
    try {
        return fitUnchecked(skeleton, mapping, points, settings);
    } catch (const std::exception& e) {
        // Only allocation can throw here; the contract is "never throws".
        return fail(std::string("internal error: ") + e.what());
    }
}

} // namespace opas::markers
