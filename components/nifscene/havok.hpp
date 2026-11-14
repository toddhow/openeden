#ifndef OPENEDEN_COMPONENTS_NIFSCENE_HAVOK_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_HAVOK_HPP

// Task 0144 (plan section 4.9): the collision data a Skyrim SE NIF carries in its `bhk*` blocks, as plain values.
//
// A NiAVObject points at a bhkCollisionObject, which points at a bhkRigidBody (or bhkRigidBodyT); the body points at
// a tree of shapes. This reads that tree into Havok::Collision: a list of shapes (children by index, in the order
// first met) and a list of bodies (one per node with a collision object). It has no Jolt, VSG or engine type: a
// consumer (apps/openmw/mwphysics/skyrimshape.*) turns it into physics shapes.
//
// CLEAN ROOM (root CLAUDE.md guardrails, docs/research/0144-havok-collision-provenance.md): written from the block
// layouts the repository's own record reader (components/nif/physics.*) already parses and from inspection of the
// user's own files; no Havok SDK header, source or decompiled code was read or used. Every number or rule that is not
// the file's own is listed in the provenance log with how it was confirmed; unconfirmed ones say RECALLED-VERIFY here.
//
// Units: everything here is the file's, in Havok units. kHavokToGame converts to game units (see its comment).
// Nothing here is sanitised beyond the caps: a consumer must filter non-finite numbers (the reader drops a shape that
// holds one and counts it, Report::mShapesNotFinite).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Nif
{
    struct NIFFile;
}

namespace NifScene::Havok
{
    using Vec3 = std::array<float, 3>;
    using Vec4 = std::array<float, 4>;

    constexpr std::uint32_t sNone = 0xFFFFFFFFu;

    /// Game units per Havok unit. RECALLED-VERIFY until the provenance log (item U1) says how the data settled it.
    constexpr float kHavokToGame = 69.99125f;

    enum class ShapeKind : std::uint8_t
    {
        Unsupported, ///< a block type this reader does not turn into geometry (mTypeName says which)
        Box, ///< mHalfExtents, mRadius
        Sphere, ///< mRadius
        Capsule, ///< mPoint1, mPoint2, mRadius1, mRadius2
        Cylinder, ///< mPoint1, mPoint2, mRadius1 (the cylinder's radius)
        ConvexHull, ///< mPoints (and mRadius)
        TriMesh, ///< mVertices and mTriangles
        List, ///< mChildren, all placed as they are
        Transform, ///< mChildren[0] placed by mTransform
    };

    struct Shape
    {
        ShapeKind mKind = ShapeKind::Unsupported;
        std::string mTypeName; // the NIF block type this came from
        std::uint32_t mMaterial = 0; // Havok material id as stored (not interpreted)
        float mRadius = 0.f;
        Vec3 mHalfExtents{};
        Vec3 mPoint1{};
        Vec3 mPoint2{};
        float mRadius1 = 0.f;
        float mRadius2 = 0.f;
        std::vector<Vec3> mPoints;
        std::vector<Vec3> mVertices;
        std::vector<std::array<std::uint32_t, 3>> mTriangles; // into mVertices, all in range
        std::vector<std::uint32_t> mChildren; // indices into Collision::mShapes
        /// A Transform shape's 4x4: the sixteen floats in the order the file stores them (element i = file float i).
        /// Which elements are the rotation and the translation is settled by the provenance log (item U6).
        std::array<float, 16> mTransform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    };

    /// One bhkRigidBody / bhkRigidBodyT, with the node whose collision object it is.
    struct Body
    {
        std::uint32_t mNodeRecord = sNone; // record index of the NiAVObject that owns the collision object
        std::uint32_t mShape = sNone; // index into Collision::mShapes, or sNone when the shape was not read
        bool mHasTransform = false; // a bhkRigidBodyT: mTranslation and mRotation place the shape
        Vec3 mTranslation{}; // Havok units
        Vec4 mRotation{ 0.f, 0.f, 0.f, 1.f }; // x, y, z, w
        std::uint8_t mLayer = 0; // the body's own havok filter
        std::uint8_t mFilterFlags = 0;
        std::uint16_t mGroup = 0;
        std::uint8_t mWorldObjectLayer = 0; // the bhkWorldObject's filter layer (the survey compares the two)
        std::uint8_t mMotionType = 0;
        std::uint8_t mQuality = 0;
        std::uint8_t mResponseType = 0;
        float mMass = 0.f;
        float mFriction = 0.f;
        float mRestitution = 0.f;
        std::uint16_t mCollisionFlags = 0; // the bhkCollisionObject's flags
    };

    struct Report
    {
        std::uint32_t mCollisionObjects = 0; // nodes with any collision object
        std::uint32_t mBodies = 0; // of those, bodies read
        std::uint32_t mObjectsOfOtherKinds = 0; // phantoms, FO4 collision objects: not read
        std::uint32_t mShapesUnsupported = 0;
        std::uint32_t mShapesNotFinite = 0;
        std::uint32_t mTrianglesDropped = 0; // an index outside the vertices
        bool mOverCap = false; // a limit was hit: the collision is incomplete and must not be used
    };

    struct Limits
    {
        std::size_t mMaxShapes = 4096;
        std::size_t mMaxBodies = 512;
        std::size_t mMaxVertices = 1000000; // of all shapes together
        std::size_t mMaxTriangles = 1000000;
        std::size_t mMaxDepth = 32;
        /// Which order the two kinds of quaternion are stored in the file: x, y, z, w (true) or w, x, y, z. The
        /// survey (apps/components_tests/nifscene/testbhkreal.cpp) tries both; the values here are what it settled.
        /// RECALLED-VERIFY until the provenance log (items U4, U5) says so.
        bool mChunkQuatXyzw = true;
        bool mRigidBodyQuatXyzw = true;
    };

    struct Collision
    {
        std::vector<Shape> mShapes;
        std::vector<Body> mBodies;
        Report mReport;
        bool empty() const { return mBodies.empty(); }
    };

    /// Reads the collision of a parsed file. Never throws for odd content: a shape it cannot represent is
    /// ShapeKind::Unsupported, a limit sets Report::mOverCap.
    Collision readCollision(const Nif::NIFFile& file, const Limits& limits = {});

    // --- The pure decoders (generated-bytes tests call these directly) ---------------------------------------------

    /// One chunk of a bhkCompressedMeshShapeData, as stored.
    struct Chunk
    {
        Vec4 mTranslation{};
        std::uint16_t mTransformIndex = 0;
        std::vector<std::uint16_t> mVertices; // three per vertex, quantised
        std::vector<std::uint16_t> mIndices; // the strips' indices first, then triangle list indices
        std::vector<std::uint16_t> mStrips; // the length of each strip
    };

    struct ChunkTransform
    {
        Vec4 mTranslation{};
        Vec4 mRotation{ 0.f, 0.f, 0.f, 1.f }; // x, y, z, w
    };

    /// The size of one quantised step of a chunk vertex in Havok units. RECALLED-VERIFY (provenance item U3).
    constexpr float kChunkVertexStep = 0.001f;

    /// Appends a chunk's triangles to `mesh` (vertices decoded as translation + step * quantised, then the chunk's
    /// transform when `transform` is not null; strips unrolled with alternating winding, then the triangle list).
    /// Returns the triangles dropped for an index outside the chunk's vertices.
    std::uint32_t decodeChunk(const Chunk& chunk, const ChunkTransform* transform, std::vector<Vec3>& vertices,
        std::vector<std::array<std::uint32_t, 3>>& triangles);

    /// Whether the game lets an actor collide with this body: false for triggers, non-collidable layers and bodies
    /// with no collision response. The layer numbers are RECALLED-VERIFY until the provenance log (item U8) says how
    /// the data settled them.
    bool bodyBlocksActors(const Body& body);

    /// Rotates `v` by the unit quaternion q (x, y, z, w).
    Vec3 rotate(const Vec4& q, const Vec3& v);
}

#endif
