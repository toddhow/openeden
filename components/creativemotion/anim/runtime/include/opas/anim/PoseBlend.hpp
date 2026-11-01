#pragma once

// Blending sampled poses and body-part masks (task 0020, stage C3c). A pose is
// one local transform per target bone, as ClipBinding::sample fills it.
// Nothing here allocates except makeBoneMask.

#include "opas/math/Transform.hpp"
#include "opas/rig/Skeleton.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opas::anim
{

    // out = a blended toward b by weightB, clamped to [0, 1] (a non-finite weight
    // counts as 0): translations
    // linearly, rotations by slerp (shortest path). Weight 0 gives a exactly and
    // weight 1 gives b exactly. out may be a or b. Does nothing if a, b or out
    // differ in size.
    void blendPoses(std::span<const math::Transform> a, std::span<const math::Transform> b, double weightB,
        std::span<math::Transform> out);

    // Weighted blend of several poses. Weights are normalized to sum to one; a
    // negative or non-finite weight counts as 0; if all are 0, out = poses[0].
    // Translations: the weighted mean. Rotations: the weighted sum of the
    // quaternions, each first flipped into the hemisphere of the first pose's
    // rotation with a non-zero weight (shortest path), then normalized. A single
    // non-zero weight gives that pose exactly. Does nothing if poses is empty,
    // weights has a different count, or any pose differs in size from out.
    void blendPosesWeighted(std::span<const std::span<const math::Transform>> poses, std::span<const double> weights,
        std::span<math::Transform> out);

    // A weight per target bone, in [0, 1]: how much a layer replaces the base
    // pose there. Ownership: a plain value.
    struct BoneMask
    {
        std::vector<double> weights;
        friend bool operator==(const BoneMask&, const BoneMask&) = default;
    };

    // A mask over `skeleton`: each named bone gets `weight`, and so do all its
    // descendants when includeDescendants is true; every other bone gets 0. Fails
    // (with a message) if the skeleton is invalid, a name is not a bone of it, or
    // weight is outside [0, 1]. An empty name list gives an all-zero mask.
    std::optional<BoneMask> makeBoneMask(const rig::Skeleton& skeleton, std::span<const std::string> bones,
        bool includeDescendants = true, double weight = 1.0, std::string* error = nullptr);

    // Per bone, out[i] = blendPoses of base[i] toward layer[i] by mask.weights[i]:
    // bones weighted 0 keep the base exactly, bones weighted 1 take the layer
    // exactly. out may be base or layer. Does nothing if any size differs.
    void applyMaskedLayer(std::span<const math::Transform> base, std::span<const math::Transform> layer,
        const BoneMask& mask, std::span<math::Transform> out);

} // namespace opas::anim
