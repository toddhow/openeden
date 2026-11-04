#ifndef OPENEDEN_COMPONENTS_BODYNIF_SKELETONREAD_H
#define OPENEDEN_COMPONENTS_BODYNIF_SKELETONREAD_H

// OpenEden AdvancedBodies, skeleton mapping (task 0070, stage a): reads a skeleton NIF's node tree and the skin weights
// of a body NIF into plain values, for the read-only measurement of extended skeletons (apps/edenskelstat). Pure code
// over the parsed Nif records (components/nif only): no ESM, no VFS, no scene graph, no components/body. Nothing here
// throws for a bad file; every cap is a located error.
//
// SKELETON. Every NiNode under the root (the root included) is a bone, depth first, children in file order, so a
// parent always comes before its children. Other objects are counted, never bones. Each bone keeps its rest transform
// in its parent's frame without scale: the translation is multiplied by the scales of all its ancestors (so the world
// rest positions of every bone come out right without any scale), and the rotation is the unit quaternion of
// NiTransform::mRotation (column-vector convention, v_parent = R v + t, as Nif::NiTransform::toMatrix and
// BodyNif::Matrix34 read it). The node's own scale is reported, not applied. Units are the file's.
// Note: Nif::NiNode::read replaces the transform of record 0 by the identity unless it is named "bip01"
// (components/nif/node.cpp); the root seen here is the root as the engine sees it.
//
// SKIN WEIGHTS. Three ways a Bethesda file can carry them, each read when present ("routes"):
//   SkinData    NiSkinData's per-bone lists (vertex, weight); bone i is the skin instance's bone i.
//   Partitions  NiSkinPartition: per partition, the vertex map (partition vertex to shape vertex), the weights and the
//               partition-local bone indices into the partition's bone list. A shape vertex that two partitions list
//               counts once (the first).
//   VertexData  the packed vertex data of BSTriShape (BSVertexData: four half-float weights and four bone indices per
//               vertex), from BSTriShape::mVertData or, in Skyrim SE files, NiSkinPartition::mVertexData. Read with
//               the indices as skin bone indices ("global") and, where partitions exist, as indices into each
//               partition's bone list ("local"): which one the files use is measured, not assumed.
// The masses come from the first route that holds any weight in the order SkinData, Partitions, VertexData (global,
// then local: stage a found the Skyrim SE files to store skin bone indices, research note 0036); every other route
// present is compared with it (the agreement numbers).
// Threading: pure functions. Ownership: values.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <components/nif/node.hpp>

namespace BodyNif
{
    /// An IEEE 754 binary16 value as a float (subnormals, infinities and NaN included).
    float halfToFloat(std::uint16_t bits);

    struct SkeletonNode
    {
        std::string mName;
        int mParent = -1; // index into SkeletonRead::mNodes; -1 for the root
        std::size_t mDepth = 0; // the root is 0
        /// In the parent's frame, multiplied by the product of the scales of all ancestors (file units).
        std::array<double, 3> mTranslation{};
        /// x, y, z, w; unit length.
        std::array<double, 4> mRotation{ 0, 0, 0, 1 };
        double mScale = 1; // the node's own NiTransform::mScale (not applied)
        /// The rotation matrix had orthonormal rows within 1e-3 and determinant +1 (else the quaternion is of the
        /// matrix as given, normalised, and means little).
        bool mOrthonormal = true;
    };

    struct SkeletonLimits
    {
        std::size_t mMaxNodes = 4096;
        std::size_t mMaxDepth = 128;
        std::size_t mMaxShapes = 256;
        std::size_t mMaxVertices = 1u << 20; // per shape
    };

    struct SkeletonRead
    {
        std::vector<SkeletonNode> mNodes;
        std::size_t mMaxDepth = 0;
        std::size_t mGeometry = 0; // NiGeometry and BSTriShape objects (not bones)
        std::size_t mOtherObjects = 0; // other objects that are not nodes
        std::size_t mUnnamed = 0; // nodes with an empty name (kept, with the empty name)
        std::size_t mScaled = 0; // nodes whose own scale differs from 1 by more than 1e-6
        std::size_t mNonOrthonormal = 0;
        std::size_t mRevisits = 0; // an object reached a second time (the tree is a graph): not read again
        std::vector<std::string> mErrors; // located messages (caps), in the order found
    };

    /// Reads the node tree under `root`. A node deeper than the cap is left out with its subtree (one error); reading
    /// stops at the node cap (one error).
    SkeletonRead readSkeleton(const Nif::NiAVObject& root, const SkeletonLimits& limits = {});

    enum class WeightRoute
    {
        None,
        SkinData,
        Partitions,
        VertexData,
    };

    const char* toString(WeightRoute route);

    struct ShapeWeights
    {
        std::string mName; // the shape's node name
        /// "NiGeometry+NiSkinInstance", "BSTriShape+NiSkinInstance" or "BSTriShape+BSSkin::Instance".
        std::string mKind;
        WeightRoute mRoute = WeightRoute::None;
        bool mGlobalIndices = true; // VertexData only: the indices were read as skin bone indices
        /// The shape's vertex count: the file's (NiGeometryData, BSTriShape::mNumVertices) unless that is 0, then the
        /// skin partition's packed vertex data or, without it, the largest vertex the partitions map plus one.
        std::size_t mVertices = 0;
        bool mVertexCountFromFile = true;
        std::vector<std::string> mBones; // the skin's bone names, in its order
        std::vector<double> mMass; // per bone: the route's weights summed over vertices
        double mTotal = 0; // the sum of mMass
        std::size_t mWeightedVertices = 0; // vertices with a positive weight sum
        std::size_t mUnweightedVertices = 0; // vertices with no weight at all
        std::size_t mBadSums = 0; // weighted vertices whose sum is not within 1e-2 of 1
        // In mRoute (the other routes show up in the agreement numbers):
        std::size_t mOutOfRange = 0; // vertex or bone indices out of range (ignored)
        std::size_t mNonFinite = 0; // weights not finite or negative (ignored)
        bool mHasSkinDataWeights = false;
        bool mHasPartitionWeights = false;
        bool mHasVertexData = false;
        /// The largest absolute difference of a bone's share of the total between mRoute and another route; -1 when
        /// that route is absent or is mRoute itself.
        double mPartitionAgreement = -1;
        double mVertexDataGlobalAgreement = -1;
        double mVertexDataLocalAgreement = -1;
    };

    struct SkinWeightsRead
    {
        std::vector<ShapeWeights> mShapes; // skinned shapes, in tree order
        std::size_t mUnskinnedShapes = 0;
        std::vector<std::string> mErrors;
    };

    /// Reads the skin weights of every skinned shape under `root`.
    SkinWeightsRead readSkinWeights(const Nif::NiAVObject& root, const SkeletonLimits& limits = {});
}

#endif
