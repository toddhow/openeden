#ifndef OPENEDEN_COMPONENTS_NIFSCENE_SCENE_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_SCENE_HPP

// Nif::Scene: the content of a NIF file as plain arrays (task 0082, docs/design/game-packs.md section 3).
// No renderer, physics or NIF record type appears here. A consumer (the renderer, the body merger, a converter) reads
// this and decides what to keep: the scene keeps every influence the file has.
//
// Conventions, all stated by tests (apps/components_tests/nifscene):
// - Units, axes and winding are the file's. Nothing is converted to metres or flipped here.
// - Texture coordinates are as stored (V is not flipped).
// - A transform's rotation is nine floats in the order the file stores them (row-major, the NIF convention).
// - Tangents are glTF-style: xyz is the unit tangent and w is the sign such that the bitangent is
//   cross(normal, xyz) * w. The NIF stores two vectors it calls tangent and bitangent; the NIF "bitangent" is the
//   glTF tangent (research note 0044, item 5).
// - Triangle indices are 32-bit, three per triangle, into the geometry's own vertex arrays.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace NifScene
{
    using Vec2 = std::array<float, 2>;
    using Vec3 = std::array<float, 3>;
    using Vec4 = std::array<float, 4>;

    constexpr std::uint32_t sNone = 0xFFFFFFFFu;

    struct Transform
    {
        Vec3 mTranslation{ 0.f, 0.f, 0.f };
        std::array<float, 9> mRotation{ 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f };
        float mScale = 1.f;
    };

    /// A node of the scene graph: every NiAVObject of the file, in record order. A shape is a node with a geometry.
    struct Node
    {
        std::string mName;
        std::string mRecordType;
        std::uint32_t mRecordIndex = 0;
        std::uint32_t mParent = sNone; // index into Scene::mNodes
        std::uint32_t mFlags = 0;
        Transform mTransform;
        std::uint32_t mGeometry = sNone; // index into Scene::mGeometries
    };

    struct Influence
    {
        std::uint32_t mBone = 0; // index into the geometry's skin's bones
        float mWeight = 0.f;
    };

    /// One skin partition: a part of the triangle list that uses a subset of the bones.
    struct Partition
    {
        std::vector<std::uint16_t> mBones; // the partition's own bone list, as stored
        std::uint32_t mFirstTriangle = 0; // into Geometry::mTriangles, counted in triangles
        std::uint32_t mTriangleCount = 0;
        std::uint8_t mLodLevel = 0;
        bool mGlobalVB = false;
    };

    struct Skin
    {
        std::vector<std::string> mBoneNames;
        std::vector<std::uint32_t> mBoneNodes; // index into Scene::mNodes, or sNone
        Transform mSkinTransform; // the skin data's own transform, identity when the file has none
        std::vector<Transform> mBindTransforms; // per bone, as stored by the file
        std::vector<Partition> mPartitions;
        /// Partitions whose bone list is not 0, 1, 2, ... (research note 0036: the packed vertex indices are skin
        /// bone indices all the same, so this is a count and not an error).
        std::uint32_t mNonIdentityPartitionBoneMaps = 0;
        /// Influences (weight above zero) of a vertex of a partition whose bone is not in the partition's list.
        std::uint32_t mInfluencesOutsidePartitionBones = 0;
    };

    enum class GeometryKind : std::uint8_t
    {
        TriShape, // BSTriShape and its plain subclasses
        DynamicTriShape, // BSDynamicTriShape: positions from the dynamic array
        SubIndexTriShape, // BSSubIndexTriShape
    };

    struct Geometry
    {
        std::string mName;
        GeometryKind mKind = GeometryKind::TriShape;
        std::uint32_t mNode = sNone; // index into Scene::mNodes
        std::vector<Vec3> mPositions;
        std::vector<Vec3> mNormals; // empty, or one per vertex
        std::vector<Vec4> mTangents; // empty, or one per vertex: glTF-style
        std::vector<Vec2> mUV0; // empty, or one per vertex
        std::vector<Vec4> mColors; // empty, or one per vertex: RGBA in [0, 1]
        std::vector<std::uint32_t> mTriangles; // three indices per triangle
        /// Influences of vertex v are mInfluences[mInfluenceStart[v] .. mInfluenceStart[v + 1]); both are empty when
        /// the shape is not skinned. Zero weights (the packed format's padding) are not stored.
        std::vector<std::uint32_t> mInfluenceStart;
        std::vector<Influence> mInfluences;
        std::uint32_t mSkin = sNone; // index into Scene::mSkins
        /// The shape stores no vertex data of its own (Data Size 0): the vertices came from the skin partition.
        bool mVerticesFromSkinPartition = false;
        /// There was no vertex data anywhere: the arrays are empty.
        bool mNoVertexData = false;
    };

    struct Scene
    {
        std::uint32_t mVersion = 0;
        std::uint32_t mUserVersion = 0;
        std::uint32_t mBethVersion = 0;
        std::vector<Node> mNodes;
        std::vector<Geometry> mGeometries;
        std::vector<Skin> mSkins;
        std::vector<std::uint32_t> mRoots; // indices into mNodes
        /// Things a consumer may want to log: a vertex layout that disagrees with its declared size, partition bone
        /// lists that are not the identity. Deterministic, in the order found.
        std::vector<std::string> mWarnings;
    };

    /// A text form of a scene with every value, for tests and comparisons: the same scene gives the same text.
    std::string dump(const Scene& scene);
}

#endif
