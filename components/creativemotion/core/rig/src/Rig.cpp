#include "opas/rig/Rig.hpp"

#include "Problem.hpp"

#include <cmath>
#include <numbers>
#include <set>

namespace opas::rig {
namespace {

using detail::Problem;

// How far an authored rotation or axis may be from unit length, and two axes
// from perpendicular. Text round-off stays far below it; real mistakes do not.
constexpr double kUnitTolerance = 1e-6;

bool finite(math::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Written as !(... <= tol) so NaN fails too.
bool unitQuat(const math::Quat& q)
{
    return std::abs(math::length(q) - 1.0) <= kUnitTolerance;
}

bool unitVec(math::Vec3 v)
{
    return finite(v) && std::abs(math::length(v) - 1.0) <= kUnitTolerance;
}

bool positive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

bool inRange(double v, double lo, double hi)
{
    return v >= lo && v <= hi; // false for NaN
}

std::string at(std::string_view array, std::size_t index, std::string_view field = {})
{
    std::string pointer = "/" + std::string(array) + "/" + std::to_string(index);
    if (!field.empty()) {
        pointer += "/" + std::string(field);
    }
    return pointer;
}

std::string inQuotes(std::string_view name)
{
    return "\"" + std::string(name) + "\"";
}

std::optional<Problem> findBoneProblem(const Skeleton& skeleton)
{
    std::set<std::string> seen;
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Bone& bone = skeleton.bones[i];
        if (bone.name.empty()) {
            return Problem{at("bones", i, "name"), "must not be empty"};
        }
        if (!detail::isValidName(bone.name)) {
            return Problem{at("bones", i, "name"), detail::kInvalidNameMessage};
        }
        if (!seen.insert(bone.name).second) {
            return Problem{at("bones", i, "name"), "duplicate bone " + inQuotes(bone.name)};
        }
        if (bone.parent < -1 || (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) >= i)) {
            return Problem{at("bones", i, "parent"), "must name a bone listed before this one"};
        }
        if (!finite(bone.localRest.translation)) {
            return Problem{at("bones", i, "translationM"), "must be finite"};
        }
        if (!unitQuat(bone.localRest.rotation)) {
            return Problem{at("bones", i, "rotation"), "must be a unit quaternion"};
        }
    }
    return std::nullopt;
}

std::optional<Problem> findShapeProblem(const Shape& shape, const std::string& pointer)
{
    if (const auto* capsule = std::get_if<CapsuleShape>(&shape)) {
        if (!positive(capsule->halfHeightM)) {
            return Problem{pointer + "/halfHeightM", "must be > 0"};
        }
        if (!positive(capsule->radiusM)) {
            return Problem{pointer + "/radiusM", "must be > 0"};
        }
    } else if (const auto* box = std::get_if<BoxShape>(&shape)) {
        const math::Vec3 e = box->halfExtentsM;
        if (!positive(e.x) || !positive(e.y) || !positive(e.z)) {
            return Problem{pointer + "/halfExtentsM", "each half extent must be > 0"};
        }
    } else if (const auto* sphere = std::get_if<SphereShape>(&shape);
               sphere != nullptr && !positive(sphere->radiusM)) {
        return Problem{pointer + "/radiusM", "must be > 0"};
    }
    return std::nullopt;
}

std::optional<Problem> findBodyProblem(const Rig& rig)
{
    std::set<std::string> seen;
    for (std::size_t i = 0; i < rig.bodies.size(); ++i) {
        const Body& body = rig.bodies[i];
        if (!findBone(rig.skeleton, body.bone)) {
            return Problem{at("bodies", i, "bone"), "unknown bone " + inQuotes(body.bone)};
        }
        if (!seen.insert(body.bone).second) {
            return Problem{at("bodies", i, "bone"),
                           "bone " + inQuotes(body.bone) + " already has a body"};
        }
        if (auto problem = findShapeProblem(body.shape, at("bodies", i, "shape"))) {
            return problem;
        }
        if (!finite(body.local.translation)) {
            return Problem{at("bodies", i, "translationM"), "must be finite"};
        }
        if (!unitQuat(body.local.rotation)) {
            return Problem{at("bodies", i, "rotation"), "must be a unit quaternion"};
        }
        if (!positive(body.massKg)) {
            return Problem{at("bodies", i, "massKg"), "must be > 0"};
        }
    }
    return std::nullopt;
}

// A cone's swing axis: stored as min = -half, max = +half, with the half-angle
// in [0, pi]. Problems point at the half-cone key a file would use.
std::optional<Problem> findConeProblem(std::size_t j, std::string_view key, double minRad,
                                       double maxRad)
{
    if (!inRange(maxRad, 0.0, std::numbers::pi)) {
        return Problem{at("joints", j, key), "must be in [0, pi]"};
    }
    if (!(minRad == -maxRad)) {
        return Problem{at("joints", j, key),
                       "a cone's swing range must be symmetric (min == -max)"};
    }
    return std::nullopt;
}

// A pyramid's swing axis (version 2): its own min and max, which must contain
// 0 so the rest pose is inside the limits.
std::optional<Problem> findPyramidProblem(std::size_t j, const std::string& name, double minRad,
                                          double maxRad)
{
    const double pi = std::numbers::pi;
    const std::string minKey = name + "MinRad";
    const std::string maxKey = name + "MaxRad";
    if (!inRange(minRad, -pi, pi)) {
        return Problem{at("joints", j, minKey), "must be in [-pi, pi]"};
    }
    if (!inRange(maxRad, -pi, pi)) {
        return Problem{at("joints", j, maxKey), "must be in [-pi, pi]"};
    }
    if (minRad > maxRad) {
        return Problem{at("joints", j, minKey), "must be <= " + maxKey};
    }
    if (minRad > 0.0) {
        return Problem{at("joints", j, minKey),
                       "must be <= 0 (the rest pose must be inside the range)"};
    }
    if (maxRad < 0.0) {
        return Problem{at("joints", j, maxKey),
                       "must be >= 0 (the rest pose must be inside the range)"};
    }
    return std::nullopt;
}

// Checked in the order the keys appear in a file of that shape.
std::optional<Problem> findSwingProblem(std::size_t j, const Joint& joint)
{
    if (joint.swing == SwingShape::Cone) {
        if (auto problem = findConeProblem(j, "normalHalfConeRad", joint.normalSwingMinRad,
                                           joint.normalSwingMaxRad)) {
            return problem;
        }
        return findConeProblem(j, "planeHalfConeRad", joint.planeSwingMinRad,
                               joint.planeSwingMaxRad);
    }
    if (joint.swing == SwingShape::Pyramid) {
        if (auto problem = findPyramidProblem(j, "planeSwing", joint.planeSwingMinRad,
                                              joint.planeSwingMaxRad)) {
            return problem;
        }
        return findPyramidProblem(j, "normalSwing", joint.normalSwingMinRad,
                                  joint.normalSwingMaxRad);
    }
    return Problem{at("joints", j, "swing"), "must be \"cone\" or \"pyramid\""};
}

std::optional<Problem> findJointProblem(const Rig& rig)
{
    const double pi = std::numbers::pi;
    std::set<std::string> seen;
    for (std::size_t j = 0; j < rig.joints.size(); ++j) {
        const Joint& joint = rig.joints[j];
        if (!findBone(rig.skeleton, joint.bone)) {
            return Problem{at("joints", j, "bone"), "unknown bone " + inQuotes(joint.bone)};
        }
        if (!seen.insert(joint.bone).second) {
            return Problem{at("joints", j, "bone"),
                           "bone " + inQuotes(joint.bone) + " already has a joint"};
        }
        const std::optional<std::size_t> body = findBody(rig, joint.bone);
        if (!body) {
            return Problem{at("joints", j, "bone"), "bone " + inQuotes(joint.bone) +
                                                        " has no body (a joint connects bodies)"};
        }
        if (!parentBody(rig, *body)) {
            return Problem{at("joints", j, "bone"),
                           "bone " + inQuotes(joint.bone) +
                               " has the root body, which has no parent body to join"};
        }
        if (!unitVec(joint.twistAxis)) {
            return Problem{at("joints", j, "twistAxis"), "must be a unit vector"};
        }
        if (!unitVec(joint.planeAxis)) {
            return Problem{at("joints", j, "planeAxis"), "must be a unit vector"};
        }
        if (!(std::abs(math::dot(joint.twistAxis, joint.planeAxis)) <= kUnitTolerance)) {
            return Problem{at("joints", j, "planeAxis"), "must be perpendicular to twistAxis"};
        }
        if (auto problem = findSwingProblem(j, joint)) {
            return problem;
        }
        if (!inRange(joint.twistMinRad, -pi, pi)) {
            return Problem{at("joints", j, "twistMinRad"), "must be in [-pi, pi]"};
        }
        if (!inRange(joint.twistMaxRad, -pi, pi)) {
            return Problem{at("joints", j, "twistMaxRad"), "must be in [-pi, pi]"};
        }
        if (joint.twistMinRad > joint.twistMaxRad) {
            return Problem{at("joints", j, "twistMinRad"), "must be <= twistMaxRad"};
        }
    }
    return std::nullopt;
}

// One root body; every other body has a joint.
std::optional<Problem> findStructureProblem(const Rig& rig)
{
    std::optional<std::size_t> root;
    for (std::size_t i = 0; i < rig.bodies.size(); ++i) {
        if (parentBody(rig, i)) {
            continue;
        }
        if (root) {
            return Problem{at("bodies", i, "bone"),
                           "no ancestor of " + inQuotes(rig.bodies[i].bone) +
                               " has a body, so it would be a second root body (the root is on " +
                               inQuotes(rig.bodies[*root].bone) + ")"};
        }
        root = i;
    }
    for (std::size_t i = 0; i < rig.bodies.size(); ++i) {
        if (i != root && !findJoint(rig, rig.bodies[i].bone)) {
            return Problem{at("bodies", i), "the body on " + inQuotes(rig.bodies[i].bone) +
                                                " needs a joint (only the root body has none)"};
        }
    }
    return std::nullopt;
}

std::optional<Problem> findHelperProblem(const Rig& rig)
{
    std::set<std::string> seenDriven;

    // Pass 1: validate each helper rule individually and collect driven bones.
    for (std::size_t i = 0; i < rig.helpers.size(); ++i) {
        const std::string ptr = at("helpers", i);
        const auto& helper = rig.helpers[i];

        if (const auto* twist = std::get_if<TwistRule>(&helper)) {
            if (!findBone(rig.skeleton, twist->bone)) {
                return Problem{ptr + "/bone", "unknown bone " + inQuotes(twist->bone)};
            }
            if (!findBone(rig.skeleton, twist->driver)) {
                return Problem{ptr + "/driver", "unknown bone " + inQuotes(twist->driver)};
            }
            if (twist->bone == twist->driver) {
                return Problem{ptr + "/driver", "must not be the driven bone " + inQuotes(twist->bone)};
            }
            if (!std::isfinite(twist->fraction)) {
                return Problem{ptr + "/fraction", "must be finite"};
            }
            if (!unitVec(twist->twistAxis)) {
                return Problem{ptr + "/twistAxis", "must be a unit vector"};
            }
            if (!seenDriven.insert(twist->bone).second) {
                return Problem{ptr + "/bone", "bone " + inQuotes(twist->bone) + " already has a helper rule"};
            }
        } else if (const auto* swing = std::get_if<SwingRule>(&helper)) {
            if (!findBone(rig.skeleton, swing->bone)) {
                return Problem{ptr + "/bone", "unknown bone " + inQuotes(swing->bone)};
            }
            if (!findBone(rig.skeleton, swing->driver)) {
                return Problem{ptr + "/driver", "unknown bone " + inQuotes(swing->driver)};
            }
            if (swing->bone == swing->driver) {
                return Problem{ptr + "/driver", "must not be the driven bone " + inQuotes(swing->bone)};
            }
            if (!std::isfinite(swing->fraction)) {
                return Problem{ptr + "/fraction", "must be finite"};
            }
            if (!unitVec(swing->driverAxis)) {
                return Problem{ptr + "/driverAxis", "must be a unit vector"};
            }
            if (!unitVec(swing->drivenAxis)) {
                return Problem{ptr + "/drivenAxis", "must be a unit vector"};
            }
            if (!std::isfinite(swing->maxAngleRad) || swing->maxAngleRad <= 0.0) {
                return Problem{ptr + "/maxAngleRad", "must be > 0"};
            }
            if (swing->maxAngleRad > std::numbers::pi) {
                return Problem{ptr + "/maxAngleRad", "must be in (0, pi]"};
            }
            if (!seenDriven.insert(swing->bone).second) {
                return Problem{ptr + "/bone", "bone " + inQuotes(swing->bone) + " already has a helper rule"};
            }
        } else if (const auto* chain = std::get_if<ChainRule>(&helper)) {
            if (!findBone(rig.skeleton, chain->startBone)) {
                return Problem{ptr + "/startBone", "unknown bone " + inQuotes(chain->startBone)};
            }
            if (!findBone(rig.skeleton, chain->endBone)) {
                return Problem{ptr + "/endBone", "unknown bone " + inQuotes(chain->endBone)};
            }
            if (chain->startBone == chain->endBone) {
                return Problem{ptr + "/endBone", "must not be startBone " + inQuotes(chain->startBone)};
            }
            if (chain->intermediates.empty()) {
                return Problem{ptr + "/intermediates", "needs at least one intermediate bone"};
            }
            std::set<std::string> localIntermediates;
            for (std::size_t k = 0; k < chain->intermediates.size(); ++k) {
                const auto& inter = chain->intermediates[k];
                const std::string interPtr = ptr + "/intermediates/" + std::to_string(k);
                if (!findBone(rig.skeleton, inter.bone)) {
                    return Problem{interPtr + "/bone", "unknown bone " + inQuotes(inter.bone)};
                }
                if (inter.bone == chain->startBone) {
                    return Problem{interPtr + "/bone", "must not be startBone " + inQuotes(chain->startBone)};
                }
                if (inter.bone == chain->endBone) {
                    return Problem{interPtr + "/bone", "must not be endBone " + inQuotes(chain->endBone)};
                }
                if (!localIntermediates.insert(inter.bone).second) {
                    return Problem{interPtr + "/bone", "duplicate intermediate bone " + inQuotes(inter.bone)};
                }
                if (!std::isfinite(inter.weight) || inter.weight <= 0.0 || inter.weight >= 1.0) {
                    return Problem{interPtr + "/weight", "must be in (0, 1)"};
                }
                if (!seenDriven.insert(inter.bone).second) {
                    return Problem{interPtr + "/bone", "bone " + inQuotes(inter.bone) + " already has a helper rule"};
                }
            }
        } else if (const auto* breathing = std::get_if<BreathingRule>(&helper)) {
            if (!findBone(rig.skeleton, breathing->bone)) {
                return Problem{ptr + "/bone", "unknown bone " + inQuotes(breathing->bone)};
            }
            if (!std::isfinite(breathing->rateHz) || breathing->rateHz <= 0.0) {
                return Problem{ptr + "/rateHz", "must be > 0"};
            }
            if (!std::isfinite(breathing->expansionM)) {
                return Problem{ptr + "/expansionM", "must be finite"};
            }
            if (!std::isfinite(breathing->pitchRad)) {
                return Problem{ptr + "/pitchRad", "must be finite"};
            }
            if (!seenDriven.insert(breathing->bone).second) {
                return Problem{ptr + "/bone", "bone " + inQuotes(breathing->bone) + " already has a helper rule"};
            }
        }
    }

    // Pass 2: ensure no driver is driven by any helper rule (prevents cycles and chaining).
    for (std::size_t i = 0; i < rig.helpers.size(); ++i) {
        const std::string ptr = at("helpers", i);
        const auto& helper = rig.helpers[i];

        if (const auto* twist = std::get_if<TwistRule>(&helper)) {
            if (seenDriven.contains(twist->driver)) {
                return Problem{ptr + "/driver", "driver bone " + inQuotes(twist->driver) + " is driven by another helper rule"};
            }
        } else if (const auto* swing = std::get_if<SwingRule>(&helper)) {
            if (seenDriven.contains(swing->driver)) {
                return Problem{ptr + "/driver", "driver bone " + inQuotes(swing->driver) + " is driven by another helper rule"};
            }
        } else if (const auto* chain = std::get_if<ChainRule>(&helper)) {
            if (seenDriven.contains(chain->startBone)) {
                return Problem{ptr + "/startBone", "driver bone " + inQuotes(chain->startBone) + " is driven by another helper rule"};
            }
            if (seenDriven.contains(chain->endBone)) {
                return Problem{ptr + "/endBone", "driver bone " + inQuotes(chain->endBone) + " is driven by another helper rule"};
            }
        }
    }

    return std::nullopt;
}

} // namespace

namespace detail {

std::optional<Problem> findRigProblem(const Rig& rig)
{
    if (rig.name.empty()) {
        return Problem{"/name", "must not be empty"};
    }
    if (!isValidName(rig.name)) {
        return Problem{"/name", kInvalidNameMessage};
    }
    if (rig.skeleton.bones.empty()) {
        return Problem{"/bones", "needs at least one bone"};
    }
    if (auto problem = findBoneProblem(rig.skeleton)) {
        return problem;
    }
    if (rig.bodies.empty()) {
        return Problem{"/bodies", "needs at least one body"};
    }
    if (auto problem = findBodyProblem(rig)) {
        return problem;
    }
    if (auto problem = findJointProblem(rig)) {
        return problem;
    }
    if (auto problem = findStructureProblem(rig)) {
        return problem;
    }
    return findHelperProblem(rig);
}

} // namespace detail

std::string validateRig(const Rig& rig)
{
    return detail::toString(detail::findRigProblem(rig));
}

std::optional<std::size_t> findBody(const Rig& rig, std::string_view bone)
{
    for (std::size_t i = 0; i < rig.bodies.size(); ++i) {
        if (rig.bodies[i].bone == bone) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> findJoint(const Rig& rig, std::string_view bone)
{
    for (std::size_t i = 0; i < rig.joints.size(); ++i) {
        if (rig.joints[i].bone == bone) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> parentBody(const Rig& rig, std::size_t bodyIndex)
{
    const std::optional<std::size_t> bone = findBone(rig.skeleton, rig.bodies[bodyIndex].bone);
    if (!bone) {
        return std::nullopt;
    }
    // Parents come before children, so this walk ends.
    int ancestor = rig.skeleton.bones[*bone].parent;
    while (ancestor >= 0) {
        const auto index = static_cast<std::size_t>(ancestor);
        if (const auto body = findBody(rig, rig.skeleton.bones[index].name)) {
            return body;
        }
        ancestor = rig.skeleton.bones[index].parent;
    }
    return std::nullopt;
}

} // namespace opas::rig
