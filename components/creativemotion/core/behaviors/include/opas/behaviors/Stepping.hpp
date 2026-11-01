#pragma once

// Balance by stepping (M5c, replaced by task 0019, docs/tasks/0019): which
// bones make the two legs and the trunk, and the formulas the step uses. The
// phase machine that runs it lives in BalanceDriver.
//
// After Coros, Beaudoin and van de Panne 2010 ("Generalized Biped Walking
// Control"): the swing foot goes where an inverted pendulum comes to rest
// (Sec. 3.2), placed by two-bone IK, and a virtual force on the centre of
// mass brakes the body through joint torques on the stance leg and trunk
// (Sec. 3.3). Every torque is a joint couple, +tau on one body and -tau on
// the other, so nothing pushes the body from outside.
//
// Ownership: a plain value, owned by BalanceDriver. It holds indices into the
// rig, precomputed body sets and the tuning from the profile; nothing
// allocates after create().

#include "opas/ik/TwoBoneIk.hpp"
#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/rig/Profile.hpp"
#include "opas/rig/Rig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opas::behaviors
{

    // Distance from a body's centre to each of the two points a joint couple's
    // impulses are applied at. The torque does not depend on it; 0.1 m keeps the
    // impulses small (the M5c trunk assist used the same construction).
    inline constexpr double kCoupleArmM = 0.1;

    // Coros Sec. 3.2: the step from the centre of mass's ground point that
    // brings an inverted pendulum of height heightM to rest over the new foot:
    // per horizontal axis d = v * sqrt(h/g + v^2 / (4 g^2)), the offset's
    // length then capped at maxM. z is 0; the input's z is ignored.
    math::Vec3 ipmStepOffset(math::Vec3 comVelocityMps, double heightM, double gravityMps2, double maxM);

    // Coros Sec. 3.3: the torque at a joint that applies forceN to the centre of
    // mass through it, tau = massFraction * (distalCentreM - jointM) x forceN,
    // where the distal part is the bodies on the far side of the joint and
    // massFraction their share of the total mass.
    math::Vec3 virtualForceTorque(math::Vec3 distalCentreM, math::Vec3 jointM, math::Vec3 forceN, double massFraction);

    // One impulse of a couple: applied at pointM.
    struct CoupleImpulse
    {
        math::Vec3 impulseNs;
        math::Vec3 pointM;
    };

    // The two opposite impulses, at kCoupleArmM either side of bodyCentreM, whose
    // net force is zero and whose net torque about bodyCentreM is
    // angularImpulseNms. A joint torque tau over dt is this with +tau*dt on one
    // body of the joint and -tau*dt on the other. A zero angular impulse gives
    // two zero impulses.
    std::array<CoupleImpulse, 2> jointCoupleImpulses(math::Vec3 bodyCentreM, math::Vec3 angularImpulseNms);

    class Stepping
    {
    public:
        // One leg: skeleton bone, rig body and rig joint indices from the hip
        // down, and how its knee bends for the IK.
        struct Leg
        {
            std::size_t hipBone = 0, kneeBone = 0, ankleBone = 0;
            std::size_t thighBody = 0, shinBody = 0, footBody = 0;
            std::size_t hipJoint = 0, kneeJoint = 0, ankleJoint = 0;
            // The knee bends about its joint's twist axis (in the shin bone's
            // frame), towards the larger side of the joint's twist range.
            math::Vec3 kneeAxis;
            double kneeSign = -1.0;
            // For ik::solveTwoBone in model space: the side the knee goes to
            // when it bends, and the bend range from the knee's twist range.
            math::Vec3 kneePoleModel;
            ik::HingeLimits kneeLimits;
            // Thigh + shin length in the rig's rest pose.
            double lengthM = 0.0;
        };

        // The rig and profile the ragdoll was built from (validated). Fails, with
        // a message, without `stepping`, `balance` or `feet`, unless the feet
        // are two legs of exactly three joints (hip, knee, ankle) whose hips hang
        // from the root body's bone, or if a trunk bone has no joint or lies in
        // a leg.
        static std::optional<Stepping> create(
            const rig::Rig& rig, const rig::Profile& profile, std::string* error = nullptr);

        [[nodiscard]] const rig::SteppingTuning& tuning() const { return tuning_; }
        [[nodiscard]] const std::array<Leg, 2>& legs() const { return legs_; }
        // Rig joint indices of velocityTuning.trunkBones, in profile order.
        [[nodiscard]] std::span<const std::size_t> trunkJoints() const { return trunkJoints_; }
        // The root body (the pelvis the hips hang from) and its bone's rest
        // rotation in model space.
        [[nodiscard]] std::size_t rootBody() const { return rootBody_; }
        [[nodiscard]] math::Quat rootRestModel() const { return rootRestModel_; }
        // The bodies on the head side of a joint: for a leg joint every body
        // not below it, for a trunk joint the bodies above it. Indexed by rig
        // body, 1 for a distal body; empty for any other joint.
        [[nodiscard]] std::span<const std::uint8_t> distalBodies(std::size_t rigJoint) const;
        // Body masses from the rig, by rig body, and their sum.
        [[nodiscard]] double bodyMassKg(std::size_t rigBody) const { return massesKg_[rigBody]; }
        [[nodiscard]] double totalMassKg() const { return totalMassKg_; }

    private:
        Stepping() = default;

        rig::SteppingTuning tuning_;
        std::array<Leg, 2> legs_{};
        std::vector<std::size_t> trunkJoints_;
        std::size_t rootBody_ = 0;
        math::Quat rootRestModel_;
        std::size_t bodyCount_ = 0;
        std::vector<std::uint8_t> distal_; // rig joints x rig bodies, for leg and trunk joints
        std::vector<std::uint8_t> hasDistal_; // per rig joint
        std::vector<double> massesKg_;
        double totalMassKg_ = 0.0;
    };

} // namespace opas::behaviors
