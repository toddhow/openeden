// Finger curl to contact: docs/tasks/0009-a5-body-surfaces-fingers.md, A5c rule 2.

#include "opas/ik/FingerCurl.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>

namespace opas::ik {
namespace {

// Upper bound on scan steps, so a tiny target radius cannot stall a frame.
constexpr double kMaxScanSteps = 4096.0;

bool isFinite(math::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double distanceToCapsule(math::Vec3 p, const CapsuleShape& c)
{
    const math::Vec3 ab = c.end - c.start;
    const double len2 = math::lengthSquared(ab);
    const double t = len2 > 0.0 ? std::clamp(math::dot(p - c.start, ab) / len2, 0.0, 1.0) : 0.0;
    return math::distance(p, c.start + t * ab) - c.radius;
}

// Joint positions (and the tip) after curling by t; fills deltas if given.
math::Vec3 curledTip(std::span<const FingerJoint> joints, double t, std::vector<math::Quat>* deltas)
{
    std::vector<math::Vec3> points;
    std::vector<math::Vec3> axes;
    points.reserve(joints.size() + 1);
    axes.reserve(joints.size());
    for (const FingerJoint& j : joints) {
        points.push_back(j.position);
        axes.push_back(math::normalized(j.hingeAxis));
    }
    const math::Vec3 last = points.back();
    points.push_back(last + (last - points[points.size() - 2])); // the tip
    math::Quat total;                                            // identity
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const math::Quat turn = math::fromAxisAngle(axes[i], t * joints[i].maxCurlRad);
        // Everything after joint i turns about it, including later hinge axes.
        for (std::size_t k = i + 1; k < points.size(); ++k) {
            points[k] = points[i] + math::rotate(turn, points[k] - points[i]);
        }
        for (std::size_t k = i + 1; k < axes.size(); ++k) {
            axes[k] = math::rotate(turn, axes[k]);
        }
        total = math::normalized(turn * total);
        if (deltas != nullptr) {
            deltas->push_back(total);
        }
    }
    return points.back();
}

FingerCurlResult solveUnchecked(std::span<const FingerJoint> joints, const CapsuleShape& target,
                                int scanSteps, double tolerance)
{
    FingerCurlResult result;
    if (joints.size() < 2 || scanSteps < 1 || !(tolerance > 0.0) || !(target.radius > 0.0) ||
        !isFinite(target.start) || !isFinite(target.end) || !std::isfinite(target.radius)) {
        return result;
    }
    for (const FingerJoint& j : joints) {
        if (!isFinite(j.position) || !isFinite(j.hingeAxis) || !(math::length(j.hingeAxis) > 0.0) ||
            !(j.maxCurlRad >= 0.0) || !(j.maxCurlRad <= std::numbers::pi)) {
            return result;
        }
    }
    result.valid = true;
    const auto gap = [&](double t) {
        return distanceToCapsule(curledTip(joints, t, nullptr), target);
    };

    double t = 1.0;
    if (gap(0.0) <= 0.0) {
        t = 0.0;
        result.touched = true;
    } else {
        // Scan for the first step that touches, then bisect inside it. The tip
        // must not move more than half the target's radius per step, or a thin
        // target could be stepped over: the tip's speed per unit t is at most
        // the sum over joints of (curl limit x length from that joint to the tip).
        double tipTravel = 0.0;
        double toTip =
            2.0 * math::distance(joints.back().position, joints[joints.size() - 2].position);
        for (std::size_t i = joints.size() - 1; i-- > 0;) {
            tipTravel += joints[i + 1].maxCurlRad * toTip;
            toTip += math::distance(joints[i + 1].position, joints[i].position);
        }
        tipTravel += joints[0].maxCurlRad * toTip;
        const double needed = std::ceil(tipTravel / (0.5 * target.radius));
        const int steps = std::max(scanSteps, static_cast<int>(std::min(needed, kMaxScanSteps)));
        double before = 0.0;
        for (int s = 1; s <= steps; ++s) {
            const double at = static_cast<double>(s) / steps;
            if (gap(at) <= 0.0) {
                double lo = before;
                double hi = at;
                while (hi - lo > tolerance) {
                    const double mid = 0.5 * (lo + hi);
                    (gap(mid) <= 0.0 ? hi : lo) = mid;
                }
                t = hi;
                result.touched = true;
                break;
            }
            before = at;
        }
    }
    result.t = t;
    result.tip = curledTip(joints, t, &result.jointDeltas);
    return result;
}

} // namespace

FingerCurlResult solveFingerCurl(std::span<const FingerJoint> joints, const CapsuleShape& target,
                                 int scanSteps, double tolerance)
{
    try {
        return solveUnchecked(joints, target, scanSteps, tolerance);
    } catch (const std::exception&) {
        return {}; // only allocation can throw; the contract is "never throws"
    }
}

} // namespace opas::ik
