#include "opas/anim/PoseBlend.hpp"

#include "opas/anim/Interpolate.hpp"

#include <algorithm>
#include <cmath>

namespace opas::anim {
namespace {

// Below this length a summed quaternion has no direction (opposite rotations
// cancelling); the reference rotation is used instead.
constexpr double kMinQuatLength = 1e-12;

// A weight usable for blending: negative or non-finite counts as 0.
double usable(double w)
{
    return std::isfinite(w) && w > 0.0 ? w : 0.0;
}

math::Transform blendOne(const math::Transform& a, const math::Transform& b, double w)
{
    if (w <= 0.0) {
        return a;
    }
    if (w >= 1.0) {
        return b;
    }
    return {a.translation + (b.translation - a.translation) * w, slerp(a.rotation, b.rotation, w)};
}

} // namespace

void blendPoses(std::span<const math::Transform> a, std::span<const math::Transform> b,
                double weightB, std::span<math::Transform> out)
{
    if (a.size() != out.size() || b.size() != out.size()) {
        return;
    }
    const double w = std::isfinite(weightB) ? std::clamp(weightB, 0.0, 1.0) : 0.0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = blendOne(a[i], b[i], w);
    }
}

void blendPosesWeighted(std::span<const std::span<const math::Transform>> poses,
                        std::span<const double> weights, std::span<math::Transform> out)
{
    if (poses.empty() || weights.size() != poses.size()) {
        return;
    }
    for (const std::span<const math::Transform>& pose : poses) {
        if (pose.size() != out.size()) {
            return;
        }
    }
    // Weights are scaled by the largest first, so huge finite weights cannot
    // sum to infinity.
    double largest = 0.0;
    for (const double w : weights) {
        largest = std::max(largest, usable(w));
    }
    double sum = 0.0;
    std::size_t nonZero = 0;
    std::size_t first = 0; // the first pose with a non-zero weight
    for (std::size_t k = 0; k < poses.size(); ++k) {
        if (usable(weights[k]) > 0.0) {
            if (nonZero == 0) {
                first = k;
            }
            ++nonZero;
            sum += usable(weights[k]) / largest;
        }
    }
    if (nonZero <= 1) {
        // All zero: the first pose; one weight: that pose, exactly.
        std::copy(poses[first].begin(), poses[first].end(), out.begin());
        return;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        const math::Quat reference = poses[first][i].rotation;
        math::Vec3 translation;
        math::Quat rotation{0.0, 0.0, 0.0, 0.0};
        for (std::size_t k = 0; k < poses.size(); ++k) {
            const double w = usable(weights[k]) / largest / sum;
            if (w == 0.0) {
                continue;
            }
            const math::Transform& t = poses[k][i];
            translation = translation + t.translation * w;
            // Same hemisphere as the reference, so q and -q blend alike.
            const double sign = math::dot(t.rotation, reference) < 0.0 ? -w : w;
            rotation =
                math::Quat{rotation.x + sign * t.rotation.x, rotation.y + sign * t.rotation.y,
                           rotation.z + sign * t.rotation.z, rotation.w + sign * t.rotation.w};
        }
        out[i] = {translation, math::length(rotation) > kMinQuatLength
                                   ? math::normalized(rotation)
                                   : math::normalized(reference)};
    }
}

std::optional<BoneMask> makeBoneMask(const rig::Skeleton& skeleton,
                                     std::span<const std::string> bones, bool includeDescendants,
                                     double weight, std::string* error)
{
    const auto failWith = [error](std::string message) -> std::optional<BoneMask> {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return std::nullopt;
    };
    if (const std::string problem = rig::validateSkeleton(skeleton); !problem.empty()) {
        return failWith("skeleton: " + problem);
    }
    if (!(weight >= 0.0 && weight <= 1.0)) {
        return failWith("mask weight must be within [0, 1]");
    }
    std::vector<bool> chosen(skeleton.bones.size(), false);
    for (const std::string& name : bones) {
        const std::optional<std::size_t> index = rig::findBone(skeleton, name);
        if (!index) {
            return failWith("bone \"" + name + "\" is not in the skeleton");
        }
        chosen[*index] = true;
    }
    if (includeDescendants) {
        // Parents come before their children (rig::Skeleton), so one pass
        // reaches every descendant.
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
            const int parent = skeleton.bones[i].parent;
            if (parent >= 0 && chosen[static_cast<std::size_t>(parent)]) {
                chosen[i] = true;
            }
        }
    }
    BoneMask mask;
    mask.weights.resize(skeleton.bones.size(), 0.0);
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        if (chosen[i]) {
            mask.weights[i] = weight;
        }
    }
    return mask;
}

void applyMaskedLayer(std::span<const math::Transform> base, std::span<const math::Transform> layer,
                      const BoneMask& mask, std::span<math::Transform> out)
{
    if (base.size() != out.size() || layer.size() != out.size() ||
        mask.weights.size() != out.size()) {
        return;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double w = mask.weights[i];
        out[i] = blendOne(base[i], layer[i], std::isfinite(w) ? std::clamp(w, 0.0, 1.0) : 0.0);
    }
}

} // namespace opas::anim
