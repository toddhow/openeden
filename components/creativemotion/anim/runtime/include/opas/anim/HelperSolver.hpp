#pragma once

// Driven helper bones solver (Milestone C6, task 0021).
// Evaluates secondary bones (roll twist, clavicle swing elevation, spine/neck chain
// interpolation, and chest breathing) from base animated bones. Zero heap allocation on hot path.

#include "opas/anim/SwingTwist.hpp"
#include "opas/math/Transform.hpp"
#include "opas/rig/Rig.hpp"

#include <span>
#include <vector>

namespace opas::anim {

class HelperSolver {
public:
    // Resolves helper bone rules against skeleton bone names.
    // If rig has no helpers, hasHelpers() is false.
    explicit HelperSolver(const rig::Rig& rig);

    bool hasHelpers() const noexcept;

    // Evaluates driven helper bones in place. Zero heap allocations.
    // If hasHelpers() is false or localPose.size() < skeleton.bones.size(), returns immediately.
    void apply(std::span<math::Transform> localPose, double timeS = 0.0, double exertion = 0.0) const;

private:
    struct ResolvedTwist {
        std::size_t driver = 0;
        std::size_t driven = 0;
        double fraction = 0.0;
        math::Vec3 twistAxis;
        math::Quat driverRest;
        math::Quat drivenRest;
    };

    struct ResolvedSwing {
        std::size_t driver = 0;
        std::size_t driven = 0;
        double fraction = 0.0;
        math::Vec3 driverAxis;
        math::Vec3 drivenAxis;
        double maxAngleRad = 0.0;
        math::Quat driverRest;
        math::Quat drivenRest;
    };

    struct ResolvedIntermediate {
        std::size_t bone = 0;
        double weight = 0.0;
    };

    struct ResolvedChain {
        std::size_t startBone = 0;
        std::size_t endBone = 0;
        std::vector<ResolvedIntermediate> intermediates;
    };

    struct ResolvedBreathing {
        std::size_t bone = 0;
        double rateHz = 0.0;
        double expansionM = 0.0;
        double pitchRad = 0.0;
        math::Transform rest;
    };

    bool hasHelpers_ = false;
    std::size_t boneCount_ = 0;
    std::vector<ResolvedTwist> twistRules_;
    std::vector<ResolvedSwing> swingRules_;
    std::vector<ResolvedChain> chainRules_;
    std::vector<ResolvedBreathing> breathingRules_;
};

} // namespace opas::anim
