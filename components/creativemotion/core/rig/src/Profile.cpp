#include "opas/rig/Profile.hpp"

#include "Problem.hpp"

#include <cmath>
#include <limits>
#include <set>

namespace opas::rig {
namespace {

using detail::Problem;

bool nonNegative(double v)
{
    return std::isfinite(v) && v >= 0.0;
}

std::string inQuotes(std::string_view name)
{
    return "\"" + std::string(name) + "\"";
}

std::optional<Problem> findMotorProblem(const MotorGains& motor, const std::string& pointer)
{
    if (!nonNegative(motor.stiffnessNmPerRad)) {
        return Problem{pointer + "/stiffnessNmPerRad", "must be >= 0"};
    }
    if (!nonNegative(motor.dampingNmsPerRad)) {
        return Problem{pointer + "/dampingNmsPerRad", "must be >= 0"};
    }
    if (!(std::isfinite(motor.maxTorqueNm) && motor.maxTorqueNm > 0.0)) {
        return Problem{pointer + "/maxTorqueNm", "must be > 0"};
    }
    return std::nullopt;
}

std::optional<Problem> findMaterialProblem(const Material& material, const std::string& pointer)
{
    if (!nonNegative(material.friction)) {
        return Problem{pointer + "/friction", "must be >= 0"};
    }
    if (!nonNegative(material.restitution)) {
        return Problem{pointer + "/restitution", "must be >= 0"};
    }
    if (!nonNegative(material.linearDamping)) {
        return Problem{pointer + "/linearDamping", "must be >= 0"};
    }
    if (!nonNegative(material.angularDamping)) {
        return Problem{pointer + "/angularDamping", "must be >= 0"};
    }
    return std::nullopt;
}

// The largest lean balance may ask for (docs/tasks/0017, M5b): half a radian,
// about 29 degrees, beyond which leaning is no longer a small correction.
constexpr double kMaxLeanLimitRad = 0.5;

std::optional<Problem> findBalanceProblem(const BalanceTuning& balance, bool noFeet)
{
    if (noFeet) {
        return Problem{"/balance", "needs feet (balance uses the foot sensors)"};
    }
    if (auto problem = findMotorProblem(balance.legMotor, "/balance/legMotor")) {
        return problem;
    }
    if (!nonNegative(balance.ankleGainRadPerM)) {
        return Problem{"/balance/ankleGainRadPerM", "must be >= 0"};
    }
    if (!nonNegative(balance.hipGainRadPerM)) {
        return Problem{"/balance/hipGainRadPerM", "must be >= 0"};
    }
    if (!(std::isfinite(balance.maxLeanRad) && balance.maxLeanRad > 0.0 &&
          balance.maxLeanRad <= kMaxLeanLimitRad)) {
        return Problem{"/balance/maxLeanRad", "must be in (0, 0.5]"};
    }
    return std::nullopt;
}

// Ranges of the stepping keys (docs/tasks/0017 Stage M5c interface, replaced
// by docs/tasks/0019).
constexpr double kMaxArmTimeS = 2.0;
constexpr double kMaxStepTimeS = 1.0;
constexpr int kMaxSteps = 8;
constexpr double kMaxLiftM = 0.3;
constexpr double kMaxStanceHalfWidthM = 0.3;
constexpr double kMaxAnkleOffTiltRad = 1.5;

bool inRange(double v, double min, double max)
{
    return std::isfinite(v) && v >= min && v <= max;
}

bool inOpenClosed(double v, double min, double max)
{
    return std::isfinite(v) && v > min && v <= max;
}

std::optional<Problem> findVelocityTuningProblem(const VelocityTuning& v)
{
    const std::string at = "/stepping/velocityTuning";
    if (!(std::isfinite(v.gainNsPerM) && v.gainNsPerM > 0.0)) {
        return Problem{at + "/gainNsPerM", "must be > 0"};
    }
    if (!(std::isfinite(v.maxTorqueNm) && v.maxTorqueNm > 0.0)) {
        return Problem{at + "/maxTorqueNm", "must be > 0"};
    }
    if (!nonNegative(v.afterTouchdownS)) {
        return Problem{at + "/afterTouchdownS", "must be >= 0"};
    }
    std::set<std::string> seen;
    for (std::size_t i = 0; i < v.trunkBones.size(); ++i) {
        const std::string pointer = at + "/trunkBones/" + std::to_string(i);
        const std::string& bone = v.trunkBones[i];
        if (bone.empty()) {
            return Problem{pointer, "must not be empty"};
        }
        if (!detail::isValidName(bone)) {
            return Problem{pointer, detail::kInvalidNameMessage};
        }
        if (!seen.insert(bone).second) {
            return Problem{pointer, "duplicate bone " + inQuotes(bone)};
        }
    }
    if (!inOpenClosed(v.ankleOffTiltRad, 0.0, kMaxAnkleOffTiltRad)) {
        return Problem{at + "/ankleOffTiltRad", "must be in (0, 1.5]"};
    }
    return std::nullopt;
}

std::optional<Problem> findSteppingProblem(const SteppingTuning& s, bool noBalance)
{
    const std::string at = "/stepping";
    if (noBalance) {
        return Problem{at, "needs balance (stepping runs inside balance)"};
    }
    if (!inOpenClosed(s.armTimeS, 0.0, kMaxArmTimeS)) {
        return Problem{at + "/armTimeS", "must be in (0, 2]"};
    }
    if (!inOpenClosed(s.stepTimeS, 0.0, kMaxStepTimeS)) {
        return Problem{at + "/stepTimeS", "must be in (0, 1]"};
    }
    if (s.maxSteps < 1 || s.maxSteps > kMaxSteps) {
        return Problem{at + "/maxSteps", "must be in [1, 8]"};
    }
    if (!inOpenClosed(s.liftM, 0.0, kMaxLiftM)) {
        return Problem{at + "/liftM", "must be in (0, 0.3]"};
    }
    if (!inRange(s.stanceHalfWidthM, 0.0, kMaxStanceHalfWidthM)) {
        return Problem{at + "/stanceHalfWidthM", "must be in [0, 0.3]"};
    }
    if (!inOpenClosed(s.maxStepLegFraction, 0.0, 1.0)) {
        return Problem{at + "/maxStepLegFraction", "must be in (0, 1]"};
    }
    return findVelocityTuningProblem(s.velocityTuning);
}

// Ranges of the hit reaction and arbiter keys (docs/tasks/0017, Stage M5d
// interface).
constexpr double kMaxRecoverTimeS = 10.0;
constexpr int kMaxReachJoints = 4;
constexpr double kMaxBlendTimeS = 2.0;
constexpr double kMaxFallTiltRad = 1.5;
constexpr double kMaxStandingTimeS = 2.0;

bool positive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

std::optional<Problem> findHitReactionProblem(const HitReactionTuning& h, bool noBalance)
{
    const std::string at = "/hitReaction";
    if (noBalance) {
        return Problem{at, "needs balance (hit reaction runs inside balance)"};
    }
    if (!inOpenClosed(h.dropFraction, 0.0, 1.0)) {
        return Problem{at + "/dropFraction", "must be in (0, 1]"};
    }
    if (!inRange(h.spread, 0.0, 1.0)) {
        return Problem{at + "/spread", "must be in [0, 1]"};
    }
    if (!inOpenClosed(h.recoverTimeS, 0.0, kMaxRecoverTimeS)) {
        return Problem{at + "/recoverTimeS", "must be in (0, 10]"};
    }
    if (h.reachJoints < 0 || h.reachJoints > kMaxReachJoints) {
        return Problem{at + "/reachJoints", "must be in [0, 4]"};
    }
    return std::nullopt;
}

std::optional<Problem> findArbiterProblem(const ArbiterTuning& a, bool noBalance)
{
    const std::string at = "/arbiter";
    if (noBalance) {
        return Problem{at, "needs balance (the arbiter runs inside balance)"};
    }
    if (!inOpenClosed(a.blendTimeS, 0.0, kMaxBlendTimeS)) {
        return Problem{at + "/blendTimeS", "must be in (0, 2]"};
    }
    if (!inOpenClosed(a.fallTiltRad, 0.0, kMaxFallTiltRad)) {
        return Problem{at + "/fallTiltRad", "must be in (0, 1.5]"};
    }
    if (!positive(a.standingSpeedMps)) {
        return Problem{at + "/standingSpeedMps", "must be > 0"};
    }
    if (!inOpenClosed(a.standingTimeS, 0.0, kMaxStandingTimeS)) {
        return Problem{at + "/standingTimeS", "must be in (0, 2]"};
    }
    if (!positive(a.restSpeedMps)) {
        return Problem{at + "/restSpeedMps", "must be > 0"};
    }
    if (!positive(a.restTimeS)) {
        return Problem{at + "/restTimeS", "must be > 0"};
    }
    return std::nullopt;
}

// Jolt stores a constraint's position steps in 8 bits
// (Constraint::SetNumPositionStepsOverride asserts below 256).
constexpr int kMaxJointPositionSteps = 255;

std::optional<Problem> findDeathProblem(const DeathTuning& d)
{
    const std::string at = "/death";
    if (d.jointPositionSteps < 1 || d.jointPositionSteps > kMaxJointPositionSteps) {
        return Problem{at + "/jointPositionSteps", "must be in [1, 255]"};
    }
    // The physics interface takes damping as float, so a larger value would
    // only fail later, at the death, far from this file and line.
    const auto fitsFloat = [](double v) {
        return v >= 0.0 && v <= static_cast<double>(std::numeric_limits<float>::max());
    };
    if (!fitsFloat(d.linearDampingPerS)) {
        return Problem{at + "/linearDampingPerS", "must be >= 0 and fit a float"};
    }
    if (!fitsFloat(d.angularDampingPerS)) {
        return Problem{at + "/angularDampingPerS", "must be >= 0 and fit a float"};
    }
    return std::nullopt;
}

// Checks each override: a non-empty bone, used once, with valid values.
template <typename Override, typename CheckValue>
std::optional<Problem> findOverrideProblem(const std::vector<Override>& overrides,
                                           std::string_view array, std::string_view valueKey,
                                           CheckValue checkValue)
{
    std::set<std::string> seen;
    for (std::size_t i = 0; i < overrides.size(); ++i) {
        const std::string pointer = "/" + std::string(array) + "/" + std::to_string(i);
        const std::string& bone = overrides[i].bone;
        if (bone.empty()) {
            return Problem{pointer + "/bone", "must not be empty"};
        }
        if (!detail::isValidName(bone)) {
            return Problem{pointer + "/bone", detail::kInvalidNameMessage};
        }
        if (!seen.insert(bone).second) {
            return Problem{pointer + "/bone", "duplicate override for bone " + inQuotes(bone)};
        }
        if (auto problem = checkValue(overrides[i], pointer + "/" + std::string(valueKey))) {
            return problem;
        }
    }
    return std::nullopt;
}

} // namespace

namespace detail {

std::optional<Problem> findProfileProblem(const Profile& profile)
{
    if (profile.rig.empty()) {
        return Problem{"/rig", "must not be empty"};
    }
    if (!isValidName(profile.rig)) {
        return Problem{"/rig", kInvalidNameMessage};
    }
    if (auto problem = findMotorProblem(profile.defaultMotor, "/defaults/motor")) {
        return problem;
    }
    if (auto problem = findMaterialProblem(profile.defaultMaterial, "/defaults/material")) {
        return problem;
    }
    if (auto problem = findOverrideProblem(profile.joints, "joints", "motor",
                                           [](const JointOverride& o, const std::string& p) {
                                               return findMotorProblem(o.motor, p);
                                           })) {
        return problem;
    }
    if (auto problem = findOverrideProblem(profile.bodies, "bodies", "material",
                                           [](const BodyOverride& o, const std::string& p) {
                                               return findMaterialProblem(o.material, p);
                                           })) {
        return problem;
    }
    std::set<std::string> feet;
    for (std::size_t i = 0; i < profile.feet.size(); ++i) {
        const std::string pointer = "/feet/" + std::to_string(i);
        const std::string& foot = profile.feet[i];
        if (foot.empty()) {
            return Problem{pointer, "must not be empty"};
        }
        if (!isValidName(foot)) {
            return Problem{pointer, kInvalidNameMessage};
        }
        if (!feet.insert(foot).second) {
            return Problem{pointer, "duplicate foot " + inQuotes(foot)};
        }
    }
    if (profile.balance) {
        if (auto problem = findBalanceProblem(*profile.balance, profile.feet.empty())) {
            return problem;
        }
    }
    if (profile.stepping) {
        if (auto problem = findSteppingProblem(*profile.stepping, !profile.balance)) {
            return problem;
        }
    }
    if (profile.hitReaction) {
        if (auto problem = findHitReactionProblem(*profile.hitReaction, !profile.balance)) {
            return problem;
        }
    }
    if (profile.arbiter) {
        if (auto problem = findArbiterProblem(*profile.arbiter, !profile.balance)) {
            return problem;
        }
    }
    if (profile.death) {
        if (auto problem = findDeathProblem(*profile.death)) {
            return problem;
        }
    }
    return std::nullopt;
}

std::optional<Problem> findProfileRigProblem(const Profile& profile, const Rig& rig)
{
    if (profile.rig != rig.name) {
        return Problem{"/rig", "the profile is for rig " + inQuotes(profile.rig) + ", not " +
                                   inQuotes(rig.name)};
    }
    for (std::size_t i = 0; i < profile.joints.size(); ++i) {
        if (!findJoint(rig, profile.joints[i].bone)) {
            return Problem{"/joints/" + std::to_string(i) + "/bone",
                           "rig " + inQuotes(rig.name) + " has no joint on bone " +
                               inQuotes(profile.joints[i].bone)};
        }
    }
    for (std::size_t i = 0; i < profile.bodies.size(); ++i) {
        if (!findBody(rig, profile.bodies[i].bone)) {
            return Problem{"/bodies/" + std::to_string(i) + "/bone",
                           "rig " + inQuotes(rig.name) + " has no body on bone " +
                               inQuotes(profile.bodies[i].bone)};
        }
    }
    for (std::size_t i = 0; i < profile.feet.size(); ++i) {
        if (!findBody(rig, profile.feet[i])) {
            return Problem{"/feet/" + std::to_string(i), "rig " + inQuotes(rig.name) +
                                                             " has no body on bone " +
                                                             inQuotes(profile.feet[i])};
        }
    }
    if (profile.stepping) {
        // Leg bodies: each foot and its parents up to, not including, the root.
        std::set<std::size_t> legBodies;
        for (const std::string& foot : profile.feet) {
            std::optional<std::size_t> b = findBody(rig, foot);
            while (b && parentBody(rig, *b)) {
                legBodies.insert(*b);
                b = parentBody(rig, *b);
            }
        }
        const std::vector<std::string>& bones = profile.stepping->velocityTuning.trunkBones;
        for (std::size_t i = 0; i < bones.size(); ++i) {
            const std::string pointer = "/stepping/velocityTuning/trunkBones/" + std::to_string(i);
            const std::optional<std::size_t> body = findBody(rig, bones[i]);
            if (!body || !findJoint(rig, bones[i])) {
                return Problem{pointer, "rig " + inQuotes(rig.name) + " has no joint on bone " +
                                            inQuotes(bones[i])};
            }
            if (legBodies.count(*body) != 0) {
                return Problem{pointer, "bone " + inQuotes(bones[i]) + " is part of a leg"};
            }
        }
    }
    return std::nullopt;
}

} // namespace detail

std::string validateProfile(const Profile& profile, const Rig& rig)
{
    if (const auto problem = detail::findProfileProblem(profile)) {
        return detail::toString(problem);
    }
    return detail::toString(detail::findProfileRigProblem(profile, rig));
}

MotorGains motorFor(const Profile& profile, std::string_view bone)
{
    for (const JointOverride& o : profile.joints) {
        if (o.bone == bone) {
            return o.motor;
        }
    }
    return profile.defaultMotor;
}

Material materialFor(const Profile& profile, std::string_view bone)
{
    for (const BodyOverride& o : profile.bodies) {
        if (o.bone == bone) {
            return o.material;
        }
    }
    return profile.defaultMaterial;
}

} // namespace opas::rig
