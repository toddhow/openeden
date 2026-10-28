#pragma once

// Skeleton: bone names, parents and rest pose. A rig (Rig.hpp) adds bodies,
// masses and joint limits on top, and is loaded from a rig file (RigFile.hpp).

#include "opas/math/Transform.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opas::rig {

struct Bone {
    std::string name;
    int parent = -1;           // index into Skeleton::bones; -1 for a root
    math::Transform localRest; // rest pose relative to the parent (or model root)

    friend bool operator==(const Bone&, const Bone&) = default;
};

// Bones are ordered so every parent comes before its children.
struct Skeleton {
    std::vector<Bone> bones;

    friend bool operator==(const Skeleton&, const Skeleton&) = default;
};

// Empty string if valid; otherwise a message naming the bone and field, e.g.
// "bone 3 (\"LeftForearm\"): parent must be < 3".
std::string validateSkeleton(const Skeleton& skeleton);

std::optional<std::size_t> findBone(const Skeleton& skeleton, std::string_view name);

// World (model-space) rest transform of every bone, same order as bones.
// The skeleton must be valid.
std::vector<math::Transform> computeWorldRest(const Skeleton& skeleton);

// World transform of every bone for a posed skeleton: localPose holds one
// transform per bone (same order, relative to the parent, like localRest) and
// root places the whole model in the world. The skeleton must be valid and
// localPose.size() == bones.size().
std::vector<math::Transform> computeWorldPose(const Skeleton& skeleton,
                                              std::span<const math::Transform> localPose,
                                              const math::Transform& root);

// As computeWorldPose, but writes into `out` (out.size() == bones.size())
// instead of allocating, for per-step use. Writes nothing if localPose or out
// has fewer entries than bones.
void computeWorldPoseInto(const Skeleton& skeleton, std::span<const math::Transform> localPose,
                          const math::Transform& root, std::span<math::Transform> out);

} // namespace opas::rig
