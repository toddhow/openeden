#include "opas/markers/BodyMeasurements.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace opas::markers {
namespace {

// World rest position of a named bone, or nullopt if there is no such bone.
std::optional<math::Vec3> bonePosition(const rig::Skeleton& skeleton,
                                       const std::vector<math::Transform>& world,
                                       const std::string& name)
{
    const std::optional<std::size_t> index = rig::findBone(skeleton, name);
    if (!index) {
        return std::nullopt;
    }
    return world[*index].translation;
}

} // namespace

MeasureResult measureBody(const rig::Skeleton& skeleton, const BodyMapping& mapping)
{
    MeasureResult result;
    if (const std::string error = rig::validateSkeleton(skeleton); !error.empty()) {
        result.error = "invalid skeleton: " + error;
        return result;
    }
    const std::vector<math::Transform> world = rig::computeWorldRest(skeleton);
    BodyMeasurements body;
    // Names must be unique: markers look regions up by name.
    std::set<std::string> limbNames;
    std::set<std::string> regions;

    for (const LimbDefinition& limb : mapping.limbs) {
        if (!limbNames.insert(limb.name).second) {
            result.error = "limb \"" + limb.name + "\": name is used by an earlier limb";
            return result;
        }
        if (limb.bones.size() < 2) {
            result.error = "limb \"" + limb.name + "\": needs at least 2 bones";
            return result;
        }
        double lengthM = 0.0;
        std::optional<math::Vec3> previous;
        for (const std::string& boneName : limb.bones) {
            const std::optional<math::Vec3> position = bonePosition(skeleton, world, boneName);
            if (!position) {
                result.error = "limb \"" + limb.name + "\": no bone named \"" + boneName + "\"";
                return result;
            }
            if (previous) {
                lengthM += math::distance(*previous, *position);
            }
            previous = position;
        }
        body.limbs.push_back(LimbLength{limb.name, lengthM});
    }

    for (const CapsuleRule& rule : mapping.capsules) {
        if (!regions.insert(rule.region).second) {
            result.error = "capsule \"" + rule.region + "\": region is used by an earlier capsule";
            return result;
        }
        // Written so NaN is rejected too.
        if (!(rule.radiusRatio > 0.0) || !std::isfinite(rule.radiusRatio)) {
            result.error = "capsule \"" + rule.region + "\": radiusRatio must be > 0";
            return result;
        }
        const std::optional<math::Vec3> start = bonePosition(skeleton, world, rule.bone);
        const std::optional<math::Vec3> end = bonePosition(skeleton, world, rule.endBone);
        if (!start || !end) {
            const std::string& missing = start ? rule.endBone : rule.bone;
            result.error = "capsule \"" + rule.region + "\": no bone named \"" + missing + "\"";
            return result;
        }
        body.capsules.push_back(SurfaceCapsule{rule.region, *start, *end,
                                               rule.radiusRatio * math::distance(*start, *end)});
    }

    if (!world.empty()) {
        const auto [lowest, highest] = std::minmax_element(
            world.begin(), world.end(), [](const math::Transform& l, const math::Transform& r) {
                return l.translation.z < r.translation.z;
            });
        body.heightM = highest->translation.z - lowest->translation.z;
    }
    result.value = std::move(body);
    return result;
}

} // namespace opas::markers
