#ifndef OPENEDEN_COMPONENTS_NIFSCENE_SKINNED_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_SKINNED_HPP

// Task 0121 (neutral world stage N5-2): the skinned half of drawing a Skyrim SE model. flatten.hpp leaves skinned
// shapes out; this file keeps ONLY them, in a form a skinned draw needs, with no Vulkan, VSG or engine type:
//  - vertices stay in the shape's own space (no node transform baked: the bind transforms take them to bone space),
//  - each vertex has at most four influences, largest weight first, renormalised to sum 1, indices into the shape's
//    own bone list (the skin's), non-finite weights and indices outside the list dropped and counted,
//  - the material is reduced to the same MaterialKey as the static path (diffuse texture, alpha, double sided ...).
// Hostile input: counts are capped, nothing is indexed outside its array, nothing throws.
// Bone transforms: Bone::mInverseBind is the file's per-bone skin transform (skin space to bone space, scene.hpp
// Skin::mBindTransforms), the matrix the engine's Anim::Skin calls invBindMatrix.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "flatten.hpp"
#include "scene.hpp"

namespace NifScene::Skinned
{
    struct Limits
    {
        std::size_t mMaxNodes = 100000;
        std::size_t mMaxShapes = 512;
        std::size_t mMaxVertices = 2000000;
        std::size_t mMaxTriangles = 2000000;
        std::size_t mMaxBonesPerShape = 256;
        double mMaxCoordinate = 1.0e7;
        std::size_t mMaxPathLength = 260;
    };

    struct Bone
    {
        std::string mName;
        Transform mInverseBind;
    };

    struct Shape
    {
        Flat::MaterialKey mKey;
        std::string mName;
        std::vector<std::array<float, 3>> mPositions; // the shape's own space
        std::vector<std::array<float, 3>> mNormals; // one per vertex, unit length
        std::vector<std::array<float, 2>> mUV; // empty, or one per vertex (the shader's offset and scale baked in)
        std::vector<std::uint32_t> mIndices; // three per triangle, all below mPositions.size()
        std::vector<Bone> mBones;
        std::vector<std::array<std::uint32_t, 4>> mBoneIndices; // per vertex, into mBones; weight 0 slots hold 0
        std::vector<std::array<float, 4>> mBoneWeights; // per vertex, sum 1 (or 1 on bone 0 for a vertex with none)
        bool mHasSkinTransform = false; // the skin's own transform is not the identity
        Transform mSkinTransform;
    };

    struct Report
    {
        std::uint32_t mGeometries = 0;
        std::uint32_t mDrawn = 0;
        std::uint32_t mSkippedUnskinned = 0; // the static path draws these
        std::uint32_t mSkippedHidden = 0;
        std::uint32_t mSkippedNoMaterial = 0;
        std::uint32_t mSkippedEffect = 0;
        std::uint32_t mSkippedNoGeometry = 0;
        std::uint32_t mSkippedOverCap = 0;
        std::uint32_t mBadVertices = 0;
        std::uint32_t mDroppedTriangles = 0;
        std::uint32_t mBadInfluences = 0; // an index outside the bone list, or a weight that is not a finite number
        std::uint32_t mVerticesWithoutInfluence = 0; // given weight 1 on bone 0
        std::uint32_t mVertices = 0;
        std::uint32_t mTriangles = 0;
    };

    struct SkinnedModel
    {
        std::vector<Shape> mShapes;
        Report mReport;
    };

    /// Never throws for any scene; the result obeys the caps in `limits`.
    [[nodiscard]] SkinnedModel flattenSkinned(const Scene& scene, const Limits& limits = {});
}

#endif
