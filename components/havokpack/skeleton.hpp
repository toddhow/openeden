#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_SKELETON_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_SKELETON_H

// Task 0119: hkaSkeleton (bone names, parents, reference pose) read from a Packfile. The member offsets are logged in
// docs/research/0054-provenance.md (F13 to F16). Formats layer, standard library only.

#include "packfile.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace HavokPack
{
    // A bone transform as Havok stores it (hkQsTransform): translation, rotation as a quaternion x y z w, scale.
    struct Transform
    {
        std::array<float, 3> mTranslation{ 0, 0, 0 };
        std::array<float, 4> mRotation{ 0, 0, 0, 1 };
        std::array<float, 3> mScale{ 1, 1, 1 };
    };

    struct Skeleton
    {
        std::string mName;
        std::vector<std::string> mBoneNames;
        // -1 for a root. A parent always comes before its child (checked by the reader).
        std::vector<std::int16_t> mParents;
        std::vector<Transform> mReferencePose;
    };

    struct SkeletonLimits
    {
        std::uint32_t mMaxBones = 4096;
    };

    // Every hkaSkeleton object of the file, in file order. Throws Error on a damaged object.
    std::vector<Skeleton> readSkeletons(const Packfile& file, const SkeletonLimits& limits = {});
}

#endif
