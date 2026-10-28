#include "opas/ik/TwoBoneIk.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace opas::ik {
namespace {

using math::Vec3;

// Below this relative size a direction is treated as "no direction" (a pole
// parallel to the target line, a straight chain, a target on the root).
constexpr double kDegenerate = 1e-9;

// Distance from root to end when the middle joint bends by bendRad, from the
// law of cosines (bend 0 = straight = a + b).
double reach(double a, double b, double bendRad)
{
    return std::sqrt(std::max(0.0, a * a + b * b + 2.0 * a * b * std::cos(bendRad)));
}

bool isFinite(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool validLimits(const HingeLimits& limits)
{
    // Written so NaN fails every comparison and is rejected.
    return limits.minBendRad >= 0.0 && limits.maxBendRad <= std::numbers::pi &&
           limits.minBendRad <= limits.maxBendRad;
}

// Unit direction from the root towards the target; if the target is on the
// root, the current end direction, then the current upper bone direction.
Vec3 aimDirection(const TwoBoneChain& chain, Vec3 toTarget, double scale)
{
    if (math::length(toTarget) > kDegenerate * scale) {
        return math::normalized(toTarget);
    }
    const Vec3 toEnd = chain.end - chain.root;
    if (math::length(toEnd) > kDegenerate * scale) {
        return math::normalized(toEnd);
    }
    return math::normalized(chain.mid - chain.root);
}

// Part of v perpendicular to unit vector `axis`.
Vec3 perpendicularPart(Vec3 v, Vec3 axis)
{
    return v - math::dot(v, axis) * axis;
}

// Unit vector perpendicular to `dir` pointing to the side the mid joint goes:
// the pole's side, else the current mid joint's side, else a fixed axis.
Vec3 bendSide(const TwoBoneChain& chain, Vec3 dir, Vec3 pole, double upperLength)
{
    const Vec3 fromPole = perpendicularPart(pole, dir);
    if (math::length(fromPole) > kDegenerate * std::max(1.0, math::length(pole))) {
        return math::normalized(fromPole);
    }
    const Vec3 fromChain = perpendicularPart(chain.mid - chain.root, dir);
    if (math::length(fromChain) > kDegenerate * upperLength) {
        return math::normalized(fromChain);
    }
    // Same deterministic choice as math::fromTo: the world axis least aligned
    // with dir keeps the cross product well conditioned.
    const Vec3 helper = std::abs(dir.x) < 0.9 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
    return math::normalized(math::cross(dir, helper));
}

} // namespace

TwoBoneResult solveTwoBone(const TwoBoneChain& chain, Vec3 target, Vec3 poleDirection,
                           const HingeLimits& limits)
{
    TwoBoneResult result;
    result.mid = chain.mid;
    result.end = chain.end;

    // Reject non-finite input, and input so large that squared lengths
    // overflow (the distances below would then be infinite): either would
    // silently produce NaN or collapsed positions.
    if (!isFinite(chain.root) || !isFinite(chain.mid) || !isFinite(chain.end) ||
        !isFinite(target) || !isFinite(poleDirection) ||
        !std::isfinite(math::length(target - chain.root)) ||
        !std::isfinite(math::length(poleDirection))) {
        return result;
    }
    const double a = math::distance(chain.root, chain.mid);
    const double b = math::distance(chain.mid, chain.end);
    if (!(a > 0.0) || !(b > 0.0) || !std::isfinite(a + b) || !validLimits(limits)) {
        return result;
    }
    result.valid = true;

    // Less bend reaches further, so the limits give a reach range.
    const double maxReach = reach(a, b, limits.minBendRad);
    const double minReach = reach(a, b, limits.maxBendRad);

    const Vec3 toTarget = target - chain.root;
    const double targetDistance = math::length(toTarget);
    const Vec3 dir = aimDirection(chain, toTarget, a + b);
    result.reached = targetDistance >= minReach && targetDistance <= maxReach;
    const double d = std::clamp(targetDistance, minReach, maxReach);

    // Bend from the law of cosines, kept inside the limits against round-off.
    const double cosBend = std::clamp((d * d - a * a - b * b) / (2.0 * a * b), -1.0, 1.0);
    result.bendRad = std::clamp(std::acos(cosBend), limits.minBendRad, limits.maxBendRad);

    // Angle at the root between the aim direction and the upper bone, from
    // the solved bend: in the bend plane the end sits at
    // (a + b cos(bend), b sin(bend)) relative to the upper bone. atan2 keeps
    // this exact for a straight arm (bend 0), where computing sin from a cos
    // close to 1 would lose about 1e-8 rad. With the end on the root (d == 0)
    // this gives 90 degrees, putting the mid joint squarely on the bend side.
    const double rootAngle =
        std::atan2(b * std::sin(result.bendRad), a + b * std::cos(result.bendRad));

    const Vec3 side = bendSide(chain, dir, poleDirection, a);
    result.mid = chain.root + a * (std::cos(rootAngle) * dir + std::sin(rootAngle) * side);
    result.end = chain.root + d * dir;

    result.rootRotation = math::fromTo(chain.mid - chain.root, result.mid - chain.root);
    result.midRotation = math::fromTo(chain.end - chain.mid, result.end - result.mid);
    return result;
}

} // namespace opas::ik
