#include "opas/rig/Skeleton.hpp"

#include <cmath>
#include <set>

namespace opas::rig {
namespace {

// How far a rest rotation may be from unit length. Authored data passes
// through text, so allow round-off but catch real mistakes.
constexpr double kUnitTolerance = 1e-6;

std::string describe(std::size_t index, const Bone& bone)
{
    return "bone " + std::to_string(index) + " (\"" + bone.name + "\")";
}

} // namespace

std::string validateSkeleton(const Skeleton& skeleton)
{
    std::set<std::string> seen;
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Bone& bone = skeleton.bones[i];
        if (bone.name.empty()) {
            return describe(i, bone) + ": name must not be empty";
        }
        if (!seen.insert(bone.name).second) {
            return describe(i, bone) + ": name is used by an earlier bone";
        }
        if (bone.parent < -1 || (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) >= i)) {
            return describe(i, bone) + ": parent must be -1 or the index of an earlier bone, got " +
                   std::to_string(bone.parent);
        }
        const math::Vec3 t = bone.localRest.translation;
        if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z)) {
            return describe(i, bone) + ": localRest.translation must be finite";
        }
        // Written as !(... <= tol) so a NaN rotation is rejected too.
        if (!(std::abs(math::length(bone.localRest.rotation) - 1.0) <= kUnitTolerance)) {
            return describe(i, bone) + ": localRest.rotation must be a unit quaternion";
        }
    }
    return {};
}

std::optional<std::size_t> findBone(const Skeleton& skeleton, std::string_view name)
{
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        if (skeleton.bones[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<math::Transform> computeWorldRest(const Skeleton& skeleton)
{
    std::vector<math::Transform> world;
    world.reserve(skeleton.bones.size());
    for (const Bone& bone : skeleton.bones) {
        // Parents come first, so world[parent] is already computed.
        world.push_back(bone.parent < 0
                            ? bone.localRest
                            : world[static_cast<std::size_t>(bone.parent)] * bone.localRest);
    }
    return world;
}

std::vector<math::Transform> computeWorldPose(const Skeleton& skeleton,
                                              std::span<const math::Transform> localPose,
                                              const math::Transform& root)
{
    std::vector<math::Transform> world(skeleton.bones.size());
    computeWorldPoseInto(skeleton, localPose, root, world);
    return world;
}

void computeWorldPoseInto(const Skeleton& skeleton, std::span<const math::Transform> localPose,
                          const math::Transform& root, std::span<math::Transform> out)
{
    if (localPose.size() < skeleton.bones.size() || out.size() < skeleton.bones.size()) {
        return; // precondition broken: write nothing rather than read or write out of bounds
    }
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const int parent = skeleton.bones[i].parent;
        // Parents come first, so out[parent] is already computed; root bones
        // hang off the model root transform.
        out[i] =
            parent < 0 ? root * localPose[i] : out[static_cast<std::size_t>(parent)] * localPose[i];
    }
}

} // namespace opas::rig
