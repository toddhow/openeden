#pragma once

// Additive style layers, posture modifiers, and pose composition (milestone C5, task 0023).
// Evaluates in-place on local poses with pre-resolved bone indices and zero heap allocations.

#include "opas/math/Transform.hpp"
#include "opas/rig/Skeleton.hpp"
#include "opas/style/IdleGenerator.hpp"
#include "opas/style/StyleProfile.hpp"

#include <span>

namespace opas::style
{

    struct ResolvedBoneIndices
    {
        int pelvis = -1;
        int spine = -1;
        int chest = -1;
        int neck = -1;
        int head = -1;
        int leftShoulder = -1;
        int rightShoulder = -1;
        int leftUpperArm = -1;
        int leftForearm = -1;
        int rightUpperArm = -1;
        int rightForearm = -1;
        int leftThigh = -1;
        int leftShin = -1;
        int rightThigh = -1;
        int rightShin = -1;
    };

    class StyleModifier
    {
    public:
        explicit StyleModifier(const rig::Skeleton& skeleton);

        [[nodiscard]] const ResolvedBoneIndices& bones() const { return bones_; }

        // Evaluates procedural posture, idle life, and additive layers directly into localPose.
        // Zero heap allocations on hot path.
        void apply(std::span<math::Transform> localPose, const StyleProfile& profile, double timeS) const;

    private:
        ResolvedBoneIndices bones_;
    };

} // namespace opas::style
