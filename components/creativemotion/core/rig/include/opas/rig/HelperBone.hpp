#pragma once

// Driven helper bones (Stage C6a, docs/tasks/0021-c6-helper-bones.md).
// Helper rules allow the runtime to procedurally drive secondary bones (roll,
// clavicle/shoulder swing, intermediate spine/neck chains, chest breathing)
// from animated base bones. File format: docs/formats/rig.md.

#include "opas/math/Vec3.hpp"

#include <string>
#include <variant>
#include <vector>

namespace opas::rig
{

    // Twist helper bone: distributes roll/twist rotation about `twistAxis` from
    // `driver` onto `bone` by `fraction`.
    struct TwistRule
    {
        std::string bone;
        std::string driver;
        double fraction = 0.0;
        math::Vec3 twistAxis;
        friend bool operator==(const TwistRule&, const TwistRule&) = default;
    };

    // Swing / elevation helper bone: rotates `bone` about `drivenAxis` proportionally
    // to `driver`'s swing deflection away from `driverAxis`, clamped to `maxAngleRad`.
    struct SwingRule
    {
        std::string bone;
        std::string driver;
        double fraction = 0.0;
        math::Vec3 driverAxis;
        math::Vec3 drivenAxis;
        double maxAngleRad = 0.0;
        friend bool operator==(const SwingRule&, const SwingRule&) = default;
    };

    // Intermediate chain bone with interpolation weight along the chain.
    struct ChainIntermediate
    {
        std::string bone;
        double weight = 0.0;
        friend bool operator==(const ChainIntermediate&, const ChainIntermediate&) = default;
    };

    // Interpolated chain helper: slerps intermediate bones between `startBone` and `endBone`.
    struct ChainRule
    {
        std::string startBone;
        std::string endBone;
        std::vector<ChainIntermediate> intermediates;
        friend bool operator==(const ChainRule&, const ChainRule&) = default;
    };

    // Breathing helper bone: periodic chest expansion and pitch oscillator.
    struct BreathingRule
    {
        std::string bone;
        double rateHz = 0.0;
        double expansionM = 0.0;
        double pitchRad = 0.0;
        friend bool operator==(const BreathingRule&, const BreathingRule&) = default;
    };

    using HelperRule = std::variant<TwistRule, SwingRule, ChainRule, BreathingRule>;

} // namespace opas::rig
