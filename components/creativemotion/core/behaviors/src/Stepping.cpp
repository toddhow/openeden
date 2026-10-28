#include "opas/behaviors/Stepping.hpp"

#include "opas/rig/Skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace opas::behaviors {
namespace {

// A leg from the foot up to, but not including, the root body: ankle, knee,
// hip (docs/tasks/0017, Stage M5c interface).
constexpr std::size_t kLegJoints = 3;

// The IK never straightens a knee fully: at zero bend the knee direction is
// undefined and the knee can lock. 0.05 rad is the bend the M5c IK prototype
// used (docs/research/0001).
constexpr double kMinKneeBendRad = 0.05;

// Walks up from `body` through parent bodies; true if it reaches `top`.
bool inSubtree(const rig::Rig& rig, std::size_t body, std::size_t top)
{
    std::optional<std::size_t> b = body;
    while (b) {
        if (*b == top) {
            return true;
        }
        b = rig::parentBody(rig, *b);
    }
    return false;
}

} // namespace

math::Vec3 ipmStepOffset(math::Vec3 comVelocityMps, double heightM, double gravityMps2, double maxM)
{
    // Coros Sec. 3.2, constant leg length (after Pratt and Tedrake 2006).
    const auto distance = [heightM, gravityMps2](double v) {
        return v * std::sqrt(heightM / gravityMps2 + v * v / (4.0 * gravityMps2 * gravityMps2));
    };
    math::Vec3 offset{distance(comVelocityMps.x), distance(comVelocityMps.y), 0.0};
    const double length = math::length(offset);
    if (length > maxM && length > 0.0) {
        offset = offset * (maxM / length);
    }
    return offset;
}

math::Vec3 virtualForceTorque(math::Vec3 distalCentreM, math::Vec3 jointM, math::Vec3 forceN,
                              double massFraction)
{
    return math::cross(distalCentreM - jointM, forceN) * massFraction;
}

std::array<CoupleImpulse, 2> jointCoupleImpulses(math::Vec3 bodyCentreM,
                                                 math::Vec3 angularImpulseNms)
{
    const double magnitude = math::length(angularImpulseNms);
    if (!(magnitude > 0.0)) {
        return {CoupleImpulse{{}, bodyCentreM}, CoupleImpulse{{}, bodyCentreM}};
    }
    // +F at c + a e and -F at c - a e, with e a unit vector perpendicular to
    // L, give 2a e x F = L when F = (L x e) / (2a). e is built from whichever
    // world axis is far from parallel to L.
    const math::Vec3 u = angularImpulseNms * (1.0 / magnitude);
    const math::Vec3 helper =
        std::abs(u.z) < 0.9 ? math::Vec3{0.0, 0.0, 1.0} : math::Vec3{1.0, 0.0, 0.0};
    const math::Vec3 e = math::normalized(math::cross(u, helper));
    const math::Vec3 force = math::cross(angularImpulseNms, e) * (1.0 / (2.0 * kCoupleArmM));
    return {CoupleImpulse{force, bodyCentreM + e * kCoupleArmM},
            CoupleImpulse{force * -1.0, bodyCentreM - e * kCoupleArmM}};
}

std::span<const std::uint8_t> Stepping::distalBodies(std::size_t rigJoint) const
{
    if (rigJoint >= hasDistal_.size() || hasDistal_[rigJoint] == 0) {
        return {};
    }
    return {distal_.data() + rigJoint * bodyCount_, bodyCount_};
}

std::optional<Stepping> Stepping::create(const rig::Rig& rig, const rig::Profile& profile,
                                         std::string* error)
{
    const auto failWith = [error](std::string message) -> std::optional<Stepping> {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return std::nullopt;
    };
    if (!profile.stepping) {
        return failWith("the profile has no stepping tuning (profile version 2, \"stepping\")");
    }
    if (!profile.balance) {
        return failWith("stepping needs balance (profile version 2, \"balance\")");
    }
    if (profile.feet.size() != 2) {
        return failWith("stepping needs exactly two feet; the profile names " +
                        std::to_string(profile.feet.size()));
    }

    Stepping stepping;
    stepping.tuning_ = *profile.stepping;
    stepping.bodyCount_ = rig.bodies.size();
    stepping.distal_.assign(rig.joints.size() * rig.bodies.size(), 0);
    stepping.hasDistal_.assign(rig.joints.size(), 0);
    for (const rig::Body& body : rig.bodies) {
        stepping.massesKg_.push_back(body.massKg);
        stepping.totalMassKg_ += body.massKg;
    }
    const std::vector<math::Transform> worldRest = rig::computeWorldRest(rig.skeleton);
    for (std::size_t f = 0; f < 2; ++f) {
        const std::string& foot = profile.feet[f];
        const std::optional<std::size_t> footBody = rig::findBody(rig, foot);
        if (!footBody) {
            return failWith("foot \"" + foot + "\" has no body in the rig");
        }
        // Walk from the foot body to the root body, collecting bodies.
        std::vector<std::size_t> chain; // foot, shin, thigh, ...
        std::size_t body = *footBody;
        std::optional<std::size_t> parent = rig::parentBody(rig, body);
        while (parent) {
            chain.push_back(body);
            body = *parent;
            parent = rig::parentBody(rig, body);
        }
        if (chain.size() != kLegJoints) {
            return failWith("foot \"" + foot + "\" has " + std::to_string(chain.size()) +
                            " joints up to the root body; stepping needs a hip, knee and ankle");
        }
        Leg& leg = stepping.legs_[f];
        leg.footBody = chain[0];
        leg.shinBody = chain[1];
        leg.thighBody = chain[2];
        stepping.rootBody_ = body;
        const auto boneOf = [&rig](std::size_t b) {
            return rig::findBone(rig.skeleton, rig.bodies[b].bone);
        };
        const auto ankle = boneOf(leg.footBody);
        const auto knee = boneOf(leg.shinBody);
        const auto hip = boneOf(leg.thighBody);
        const auto root = boneOf(body);
        if (!ankle || !knee || !hip || !root) {
            return failWith("a leg bone of foot \"" + foot + "\" is not in the skeleton");
        }
        // The legs are set relative to the root body's bone, so the thigh
        // must hang from it directly.
        if (rig.skeleton.bones[*hip].parent != static_cast<int>(*root)) {
            return failWith("the hip of foot \"" + foot +
                            "\" does not hang from the root body's bone");
        }
        leg.ankleBone = *ankle;
        leg.kneeBone = *knee;
        leg.hipBone = *hip;
        const auto hipJoint = rig::findJoint(rig, rig.bodies[leg.thighBody].bone);
        const auto kneeJoint = rig::findJoint(rig, rig.bodies[leg.shinBody].bone);
        const auto ankleJoint = rig::findJoint(rig, rig.bodies[leg.footBody].bone);
        if (!hipJoint || !kneeJoint || !ankleJoint) {
            return failWith("a leg joint of foot \"" + foot + "\" is missing");
        }
        leg.hipJoint = *hipJoint;
        leg.kneeJoint = *kneeJoint;
        leg.ankleJoint = *ankleJoint;

        const rig::Joint& joint = rig.joints[*kneeJoint];
        leg.kneeAxis = math::normalized(joint.twistAxis);
        leg.kneeSign = std::abs(joint.twistMinRad) > std::abs(joint.twistMaxRad) ? -1.0 : 1.0;
        // Bending turns the shin by kneeSign about the knee axis (in model
        // space here), which moves the ankle along a x d; the knee goes the
        // other way, to the pole side.
        const math::Vec3 hipM = worldRest[*hip].translation;
        const math::Vec3 kneeM = worldRest[*knee].translation;
        const math::Vec3 ankleM = worldRest[*ankle].translation;
        const math::Vec3 axisM =
            math::rotate(worldRest[*knee].rotation, leg.kneeAxis) * leg.kneeSign;
        leg.kneePoleModel = math::normalized(math::cross(axisM, ankleM - kneeM)) * -1.0;
        const double maxBend = std::max(std::abs(joint.twistMinRad), std::abs(joint.twistMaxRad));
        leg.kneeLimits = ik::HingeLimits{std::min(kMinKneeBendRad, maxBend), maxBend};
        leg.lengthM = math::length(kneeM - hipM) + math::length(ankleM - kneeM);
        stepping.rootRestModel_ = worldRest[*root].rotation;

        // Head side of each leg joint: every body not below it.
        for (const std::size_t j : {leg.hipJoint, leg.kneeJoint, leg.ankleJoint}) {
            const std::optional<std::size_t> child = rig::findBody(rig, rig.joints[j].bone);
            stepping.hasDistal_[j] = 1;
            for (std::size_t b = 0; b < rig.bodies.size(); ++b) {
                stepping.distal_[j * rig.bodies.size() + b] = inSubtree(rig, b, *child) ? 0 : 1;
            }
        }
    }
    for (const std::string& bone : stepping.tuning_.velocityTuning.trunkBones) {
        const std::optional<std::size_t> joint = rig::findJoint(rig, bone);
        const std::optional<std::size_t> child = rig::findBody(rig, bone);
        if (!joint || !child) {
            return failWith("trunk bone \"" + bone + "\" has no joint in the rig");
        }
        for (const Leg& leg : stepping.legs_) {
            if (*child == leg.thighBody || *child == leg.shinBody || *child == leg.footBody) {
                return failWith("trunk bone \"" + bone + "\" is part of a leg");
            }
        }
        stepping.trunkJoints_.push_back(*joint);
        // Distal side of a trunk joint: the bodies above it.
        stepping.hasDistal_[*joint] = 1;
        for (std::size_t b = 0; b < rig.bodies.size(); ++b) {
            stepping.distal_[*joint * rig.bodies.size() + b] = inSubtree(rig, b, *child) ? 1 : 0;
        }
    }
    return stepping;
}

} // namespace opas::behaviors
