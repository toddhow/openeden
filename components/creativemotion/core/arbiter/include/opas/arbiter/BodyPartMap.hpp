#pragma once

// Maps rig bodies, joints, and skeleton bones to body parts (Stage C1c,
// docs/tasks/0024-c1-arbiter-per-part-strictness.md).
//
// Used by behaviors and runtime systems to query which BodyPart a given
// body, joint, or bone belongs to.

#include "opas/arbiter/Arbiter.hpp"
#include "opas/rig/Rig.hpp"
#include "opas/rig/Skeleton.hpp"

#include <cstddef>
#include <vector>

namespace opas::arbiter {

class BodyPartMap {
public:
    BodyPartMap() = default;

    [[nodiscard]] static BodyPartMap create(const rig::Skeleton& skeleton, const rig::Rig& rig);

    [[nodiscard]] BodyPart bonePart(std::size_t boneIndex) const;
    [[nodiscard]] BodyPart bodyPart(std::size_t bodyIndex) const;
    [[nodiscard]] BodyPart jointPart(std::size_t jointIndex) const;

    [[nodiscard]] std::size_t boneCount() const noexcept { return boneParts_.size(); }
    [[nodiscard]] std::size_t bodyCount() const noexcept { return bodyParts_.size(); }
    [[nodiscard]] std::size_t jointCount() const noexcept { return jointParts_.size(); }

private:
    std::vector<BodyPart> boneParts_;
    std::vector<BodyPart> bodyParts_;
    std::vector<BodyPart> jointParts_;
};

} // namespace opas::arbiter
