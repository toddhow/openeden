#ifndef OPENEDEN_COMPONENTS_NIFSCENE_SKELETON_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_SKELETON_HPP

// Task 0121 (neutral world stage N5-2): a skeleton NIF as a list of bone nodes with parents and local transforms, for
// the engine's animation system to build its Anim::Transform hierarchy from. Pure: no VSG or engine type. Geometry
// nodes are left out (a skeleton file has none; a body file's shapes are drawn by skinned.hpp). Hidden-flag nodes are
// KEPT: a bone that a file hides is still a bone that skin weights name.
//
// Order: parents before children (depth first from the roots, children in record order), so a consumer builds the
// hierarchy in one pass. A node whose parent index is outside the scene is a root; a parent cycle is cut and the nodes
// on it that no root reaches are left out and counted. Non-finite transforms become the identity (counted).

#include <cstdint>
#include <string>
#include <vector>

#include "scene.hpp"

namespace NifScene::Skeleton
{
    struct Limits
    {
        std::size_t mMaxNodes = 4096;
        std::size_t mMaxNameBytes = 256; // a longer name is cut
    };

    struct BoneNode
    {
        std::string mName;
        std::uint32_t mParent = sNone; // index into Plan::mNodes (always lower than this node's own index), or sNone
        Transform mLocal;
    };

    struct Plan
    {
        std::vector<BoneNode> mNodes;
        std::uint32_t mSkippedGeometry = 0;
        std::uint32_t mUnreachable = 0; // on a cycle, or past the node cap
        std::uint32_t mNonFiniteTransforms = 0;
        std::uint32_t mDuplicateNames = 0; // names (case-insensitive) seen before: only the first is findable
    };

    [[nodiscard]] Plan plan(const Scene& scene, const Limits& limits = {});

    /// The index of the node named `name` (case-insensitive), or sNone.
    [[nodiscard]] std::uint32_t find(const Plan& plan, const std::string& name);
}

#endif
