#pragma once

// The profile: how a rig's body behaves (motor gains, surface materials).
// One rig can have several profiles. File format: docs/formats/profile.md.
//
// Field meanings match opas::physics::JointMotor and BodyDesc; M3 applies them.

#include "opas/rig/Rig.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opas::rig
{

    // Angular spring towards a joint's target: torque = stiffness * angle error -
    // damping * relative angular velocity, clamped to maxTorqueNm.
    struct MotorGains
    {
        double stiffnessNmPerRad = 0.0;
        double dampingNmsPerRad = 0.0;
        double maxTorqueNm = 0.0;
        friend bool operator==(const MotorGains&, const MotorGains&) = default;
    };

    struct Material
    {
        double friction = 0.0;
        double restitution = 0.0;
        double linearDamping = 0.0;
        double angularDamping = 0.0;
        friend bool operator==(const Material&, const Material&) = default;
    };

    // Version 2 (M5b, docs/tasks/0017): the lean-balance tuning. Leg joints are
    // the joints on the chain from each foot body up to, but not including, the
    // root body.
    struct BalanceTuning
    {
        MotorGains legMotor; // every leg joint while balance runs
        double ankleGainRadPerM = 0.0; // ankle lean per metre of capture-point offset
        double hipGainRadPerM = 0.0; // hip lean per metre of the same offset
        double maxLeanRad = 0.0; // the largest ankle or hip lean, in (0, 0.5]
        friend bool operator==(const BalanceTuning&, const BalanceTuning&) = default;
    };

    // Task 0019: braking while stepping, by a virtual force on the centre of mass
    // applied as joint torques through the stance leg and these trunk bones
    // (Coros, Beaudoin and van de Panne 2010, Sec. 3.3).
    struct VelocityTuning
    {
        double gainNsPerM = 0.0; // k_V in F = k_V (0 - v)
        double maxTorqueNm = 0.0; // cap on each joint torque stepping applies
        double afterTouchdownS = 0.0; // keeps braking this long after a landing
        std::vector<std::string> trunkBones; // bones with joints, none in a leg
        double ankleOffTiltRad = 0.0; // stance ankle takes no torque past this foot tilt
        friend bool operator==(const VelocityTuning&, const VelocityTuning&) = default;
    };

    // Version 2 (M5c, replaced by task 0019): balance by stepping, with the
    // swing foot placed by an inverted pendulum model and two-bone IK. Needs
    // `balance`.
    struct SteppingTuning
    {
        double armTimeS = 0.0; // capture point inside this long arms stepping
        double stepTimeS = 0.0; // one swing
        int maxSteps = 0; // steps in a row before giving up
        double liftM = 0.0; // peak swing-foot height above its start
        double stanceHalfWidthM = 0.0; // the feet stay at least twice this apart across the body
        double maxStepLegFraction = 0.0; // step length cap, as a fraction of leg length
        VelocityTuning velocityTuning;
        bool swingGravityCompensation = false; // gravity torques on the swing hip and knee
        friend bool operator==(const SteppingTuning&, const SteppingTuning&) = default;
    };

    // Version 2 (M5d): hit reaction. A hit lowers joint stiffness near the
    // struck body and lets it ramp back. Needs `balance`.
    struct HitReactionTuning
    {
        double dropFraction = 0.0; // stiffness drop at the struck body's joints, in (0, 1]
        double spread = 0.0; // factor per body step away, in [0, 1]
        double recoverTimeS = 0.0; // time constant of the ramp back
        int reachJoints = 0; // farthest body step that still gets a drop
        friend bool operator==(const HitReactionTuning&, const HitReactionTuning&) = default;
    };

    // Version 2 (M5d): the arbiter's state machine thresholds. Needs `balance`.
    struct ArbiterTuning
    {
        double blendTimeS = 0.0; // gains and targets blend this long on an owner change
        double fallTiltRad = 0.0; // pelvis tilt that starts Falling
        double standingSpeedMps = 0.0; // horizontal centre-of-mass speed that counts as Standing
        double standingTimeS = 0.0; // slow this long before Leaning becomes Standing
        double restSpeedMps = 0.0; // every body slower than this ...
        double restTimeS = 0.0; // ... for this long gives Fallen
        friend bool operator==(const ArbiterTuning&, const ArbiterTuning&) = default;
    };

    // Task 0018 stage P3: how a dead (limp) ragdoll is simulated, cheaper than a
    // living one. Applied by motor::makeLimp.
    struct DeathTuning
    {
        int jointPositionSteps = 0; // Jolt position iterations per joint, in [1, 255]
        double linearDampingPerS = 0.0; // body damping, >= 0
        double angularDampingPerS = 0.0;
        friend bool operator==(const DeathTuning&, const DeathTuning&) = default;
    };

    struct JointOverride
    {
        std::string bone; // a bone with a joint in the rig
        MotorGains motor;
        friend bool operator==(const JointOverride&, const JointOverride&) = default;
    };

    struct BodyOverride
    {
        std::string bone; // a bone with a body in the rig
        Material material;
        friend bool operator==(const BodyOverride&, const BodyOverride&) = default;
    };

    // Defaults cover every joint and body; an override replaces the whole motor
    // or material of one bone (no field-by-field merging).
    struct Profile
    {
        std::string rig; // the rig name this profile was tuned for
        MotorGains defaultMotor;
        Material defaultMaterial;
        std::vector<JointOverride> joints;
        std::vector<BodyOverride> bodies;
        // Version 2 (M5): bones whose bodies are feet, for the balance sensors
        // (foot contact and support area). Empty in version 1 files.
        std::vector<std::string> feet;
        // Version 2 (M5b): balance tuning; needs feet.
        std::optional<BalanceTuning> balance;
        // Version 2 (M5c): stepping tuning; needs balance.
        std::optional<SteppingTuning> stepping;
        // Version 2 (M5d): hit reaction and the arbiter; both need balance.
        std::optional<HitReactionTuning> hitReaction;
        std::optional<ArbiterTuning> arbiter;
        // Version 2 (task 0018): limp ragdolls after death; independent of the rest.
        std::optional<DeathTuning> death;
        friend bool operator==(const Profile&, const Profile&) = default;
    };

    // Empty string if the profile is valid on its own and for `rig` (every rule
    // in docs/formats/profile.md); otherwise "<JSON pointer>: <message>". The
    // loader checks only the rules that need no rig, so call this once the rig
    // is known.
    std::string validateProfile(const Profile& profile, const Rig& rig);

    // The gains and material that apply to a bone: its override, else the default.
    MotorGains motorFor(const Profile& profile, std::string_view bone);
    Material materialFor(const Profile& profile, std::string_view bone);

} // namespace opas::rig
