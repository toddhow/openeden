#include "opas/arbiter/BodyPartMap.hpp"

namespace opas::arbiter {

BodyPartMap BodyPartMap::create(const rig::Skeleton& skeleton, const rig::Rig& rig)
{
    BodyPartMap map;
    map.boneParts_.resize(skeleton.bones.size(), BodyPart::Torso);
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        if (const auto p = classifyBone(skeleton.bones[i].name)) {
            map.boneParts_[i] = *p;
        } else if (skeleton.bones[i].parent >= 0 &&
                   static_cast<std::size_t>(skeleton.bones[i].parent) < i) {
            map.boneParts_[i] = map.boneParts_[static_cast<std::size_t>(skeleton.bones[i].parent)];
        } else {
            map.boneParts_[i] = BodyPart::Torso;
        }
    }

    map.bodyParts_.resize(rig.bodies.size(), BodyPart::Torso);
    for (std::size_t b = 0; b < rig.bodies.size(); ++b) {
        if (const auto boneIdx = rig::findBone(skeleton, rig.bodies[b].bone)) {
            map.bodyParts_[b] = map.boneParts_[*boneIdx];
        } else if (const auto p = classifyBone(rig.bodies[b].bone)) {
            map.bodyParts_[b] = *p;
        } else {
            map.bodyParts_[b] = BodyPart::Torso;
        }
    }

    map.jointParts_.resize(rig.joints.size(), BodyPart::Torso);
    for (std::size_t j = 0; j < rig.joints.size(); ++j) {
        if (const auto boneIdx = rig::findBone(skeleton, rig.joints[j].bone)) {
            map.jointParts_[j] = map.boneParts_[*boneIdx];
        } else if (const auto p = classifyBone(rig.joints[j].bone)) {
            map.jointParts_[j] = *p;
        } else {
            map.jointParts_[j] = BodyPart::Torso;
        }
    }

    return map;
}

BodyPart BodyPartMap::bonePart(std::size_t boneIndex) const
{
    if (boneIndex < boneParts_.size()) {
        return boneParts_[boneIndex];
    }
    return BodyPart::Torso;
}

BodyPart BodyPartMap::bodyPart(std::size_t bodyIndex) const
{
    if (bodyIndex < bodyParts_.size()) {
        return bodyParts_[bodyIndex];
    }
    return BodyPart::Torso;
}

BodyPart BodyPartMap::jointPart(std::size_t jointIndex) const
{
    if (jointIndex < jointParts_.size()) {
        return jointParts_[jointIndex];
    }
    return BodyPart::Torso;
}

} // namespace opas::arbiter
