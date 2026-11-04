#ifndef OPENEDEN_COMPONENTS_BODY_BODYMERGE_H
#define OPENEDEN_COMPONENTS_BODY_BODYMERGE_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage b): the merger as a pure library.
//
// Input: a list of PartMesh (plain arrays: positions in part space plus the part-to-bind placement, normals, UVs,
// triangles, influences by bone NAME or one rigid attach bone, a material key, a slot number, a mirror flag) and a
// list of seam declarations (which slots may weld, and how close vertices must be). Output: one MergedBody (arrays,
// one canonical bone list, region and submesh ranges, weld groups) or located errors.
//
// Game-agnostic: nothing here names Morrowind, an ESM record, a NIF or a skeleton file. A game's adapter fills the
// parts (Morrowind: components/bodynif plus apps/openmw/mwrender/bodylegacy.*; a seamless game such as Skyrim: body,
// hands and feet with exact coincident seams, a tolerance near zero, through the same code). The weld map can be
// computed alone (computeWeldMap), for a game whose parts already meet exactly and are not merged.
//
// What the merge does, in this order (the numbers are named constants below or options):
//   1. Parts are put in a canonical order (slot, material key, name, sizes, content): the result does not depend on
//      the order the caller passes them in.
//   2. Each part is placed: a mirror flag scales x by -1 in part space first, then the 3 x 4 placement matrix is
//      applied. Normals follow (cofactor matrix, sign of the determinant), and the winding flips when the mirror and
//      the placement's determinant do not cancel.
//   3. Vertices of two parts that a declared seam allows, closer than the seam's tolerance, form weld sets
//      (union-find; the smallest vertex id is the root). Two vertices of ONE part are never paired directly, but a
//      vertex of another part can join them; a member that is then further from the root than the tolerance stays
//      out of the set (unwelded), so no vertex ever moves further than the tolerance. A set's vertices all take the
//      position of the set's lowest member. Inside a set, vertices whose UVs are equal and whose normals differ by
//      less than a named angle merge into one output vertex; the others stay separate
//      (a UV or normal crease). A set that leaves two or more output vertices is a WELD GROUP, listed in the output,
//      so that a later morph moves its members identically.
//   4. Influences: a rigid part gives weight 1.0 to its attach bone. A vertex in a weld set gets the equal-weight
//      mean of the weights of all members, bone by bone. Then: duplicate bones add, zero weights drop, the four
//      largest are kept (ties by canonical bone index), and the four are renormalised to sum 1. Bones are written as
//      indices into one canonical list: the names of MergeOptions::mBoneOrder first, in that order, then the other
//      used names sorted by byte value. Only bones some vertex still uses are listed.
//   5. Triangles are ordered by (slot, material key, canonical part order, source order); a triangle whose vertices
//      collapse into fewer than three distinct output vertices is dropped and counted. A region is one contiguous
//      triangle range per slot (matches the body package's regionMap); a submesh is one range per (slot, material).
//
// Hostile input: every count is capped (MergeLimits), every index and number is checked, nothing throws; a bad part
// gives a located message ("part 'Chest' (slot 3): triangle 12: index 99 out of range (8 vertices)") and no body.
//
// Determinism: no clock, no randomness, no threads; the only unordered-looking step (the spatial lookup) is a sorted
// vector; all orders are total. Same input and options give identical bytes on one machine (floats are computed in
// double and rounded once). Threading: pure functions; one body per call.
// Ownership: all results are values.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Body
{
    using Vec2 = std::array<float, 2>;
    using Vec3 = std::array<float, 3>;
    /// A 3 x 4 matrix, row-major, [rotation and scale | translation]: v' = M3 * v + t.
    using Mat34 = std::array<double, 12>;

    inline constexpr Mat34 identityPlacement = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

    /// The merged body keeps at most this many influences per vertex (the engine's skin does the same).
    inline constexpr std::size_t maxMergedInfluences = 4;
    /// An input vertex may name more, up to this many (a modder's mesh); the merger cuts them to four.
    inline constexpr std::size_t maxInputInfluences = 8;

    /// Two UV coordinates are the same when every component differs by at most this. UVs are stored as float, so two
    /// vertices authored as one differ by about 1e-7 at most; 1e-5 is 100 times that and far below a texel of any
    /// texture (1/4096 = 2.4e-4). A starting value.
    inline constexpr double uvMergeTolerance = 1e-5;
    /// Two normals merge into one vertex when the angle between them is at most this (radians, 5 degrees). Stage a
    /// (research note 0033) measured normals up to 27 degrees apart at exact seams: those are creases or authored
    /// splits and stay separate. A starting value, to be tuned with the data of stage c.
    inline constexpr double normalMergeAngle = 0.0872664626;
    /// Coordinates beyond this (in bind-space units) are refused: it keeps the spatial grid inside 64-bit cells
    /// even for a tolerance of 1e-6.
    inline constexpr double maxCoordinate = 1.0e7;
    /// The finest grid cell of the spatial lookup (a tolerance below it still compares exactly).
    inline constexpr double minGridCell = 1.0e-6;
    /// A seam tolerance above this (bind-space units) is refused: it would make every vertex a neighbour of every
    /// other and the weld quadratic. A Morrowind body is about 128 units tall; a tolerance of 100 is far beyond any
    /// seam.
    inline constexpr double maxSeamTolerance = 100.0;
    /// A placement entry (rotation, scale or translation) beyond this magnitude is refused. A Morrowind placement has
    /// entries of at most a few hundred; 1e6 keeps every product and sum of the placement far below double overflow and
    /// the placed vertices are checked against maxCoordinate separately.
    inline constexpr double maxPlacementEntry = 1.0e6;

    struct PartInfluence
    {
        std::uint32_t mBone = 0; // index into PartMesh::mBones
        float mWeight = 0.f;
    };

    struct PartVertexSkin
    {
        std::uint8_t mCount = 0; // 0 to maxInputInfluences
        std::array<PartInfluence, maxInputInfluences> mInfluences{};
    };

    /// One mesh of one body part: a game adapter's view of a part, after reading it. A part with several materials
    /// is given as several PartMesh with the same slot.
    struct PartMesh
    {
        std::string mName; // for messages and as a tie-break of the canonical order
        int mSlot = 0; // the body region this part is (>= 0); a region is the contiguous triangles of one slot
        std::string mMaterial; // material key (texture, alpha flags ...): one submesh per (slot, material)
        std::vector<Vec3> mPositions; // part space
        std::vector<Vec3> mNormals; // empty or one per vertex
        std::vector<Vec2> mUvs; // empty or one per vertex
        std::vector<std::uint32_t> mTriangles; // three indices per triangle
        /// Skinned: bone names, and one PartVertexSkin per vertex (indices into mBones). Rigid: both empty and
        /// mRigidBone names the bone that carries the whole part with weight 1.
        std::vector<std::string> mBones;
        std::vector<PartVertexSkin> mSkin;
        std::string mRigidBone;
        /// Part space to bind space. Applied after the mirror.
        Mat34 mPlacement = identityPlacement;
        /// Scale x by -1 in part space before the placement (a left part that shares a file with its right twin).
        bool mMirror = false;
    };

    /// Two slots whose parts may weld where their vertices are closer than the tolerance (bind-space units). The
    /// same slot twice means two parts of one slot (a part split by material).
    struct SeamDeclaration
    {
        int mSlotA = 0;
        int mSlotB = 0;
        double mTolerance = 0.01; // 0 means exactly equal positions
    };

    struct MergeOptions
    {
        std::vector<std::string> mBoneOrder; // the canonical order of known bone names (see the header comment)
        double mUvTolerance = uvMergeTolerance;
        double mNormalAngle = normalMergeAngle;
    };

    struct MergeLimits
    {
        std::size_t mMaxParts = 64;
        std::size_t mMaxVerticesPerPart = 200000;
        std::size_t mMaxTrianglesPerPart = 400000;
        std::size_t mMaxTotalVertices = 500000;
        std::size_t mMaxTotalTriangles = 1000000;
        std::size_t mMaxBonesPerPart = 1024;
        std::size_t mMaxSeams = 256;
        std::size_t mMaxPairsPerSeam = 2000000; // vertex pairs one seam may join before it is refused
        /// Vertex pairs one seam may COMPARE (not only join) before it is refused: with a tolerance of 0 and many
        /// coincident vertices the lookup would otherwise be quadratic. 50 million is a few seconds of work.
        std::size_t mMaxCandidateChecks = 50000000;
        std::size_t mMaxNameBytes = 128;
        std::size_t mMaxErrors = 16;
    };

    /// A member of a weld set: the INPUT part index (as passed in) and the vertex index in that part.
    struct WeldMember
    {
        std::uint32_t mPart = 0;
        std::uint32_t mVertex = 0;
        friend bool operator==(const WeldMember&, const WeldMember&) = default;
    };

    /// The weld map: every set of two or more vertices that a declared seam joins, without merging anything.
    /// Members are in canonical order (canonical part rank, then vertex index); sets in order of their first member.
    struct WeldMap
    {
        std::vector<std::vector<WeldMember>> mSets;
        /// The position every member of a set takes: the position of its first member (placed, in bind space).
        std::vector<Vec3> mPositions;
        /// Pairs found per declared seam, in the order of the seam list.
        std::vector<std::size_t> mPairsPerSeam;
        std::vector<std::string> mErrors; // located messages; empty when the map is valid
        friend bool operator==(const WeldMap&, const WeldMap&) = default;
    };

    struct RegionRange
    {
        int mSlot = 0;
        std::uint32_t mFirstTriangle = 0;
        std::uint32_t mTriangleCount = 0;
        friend bool operator==(const RegionRange&, const RegionRange&) = default;
    };

    /// One draw: the triangles of one slot with one material.
    struct Submesh
    {
        std::uint32_t mMaterial = 0; // index into MergedBody::mMaterials
        int mSlot = 0;
        std::uint32_t mFirstTriangle = 0;
        std::uint32_t mTriangleCount = 0;
        friend bool operator==(const Submesh&, const Submesh&) = default;
    };

    struct MergeReport
    {
        std::size_t mParts = 0;
        std::size_t mInputVertices = 0;
        std::size_t mOutputVertices = 0;
        std::size_t mInputTriangles = 0;
        std::size_t mOutputTriangles = 0;
        std::size_t mCollapsedVertices = 0; // input vertices that share an output vertex with an earlier one
        std::size_t mWeldSets = 0;
        std::size_t mWeldGroups = 0; // sets that kept two or more output vertices
        std::size_t mDroppedTriangles = 0; // collapsed by a weld
        /// The largest absolute difference, over bones, between the weights of two members of one weld set before
        /// they were averaged. The bound of the skinning change a weld makes (research note 0033: stock seams have 0).
        double mMaxWeightDisagreement = 0;
        /// The largest angle (radians) between two normals that merged into one vertex.
        double mMaxMergedNormalAngle = 0;
        friend bool operator==(const MergeReport&, const MergeReport&) = default;
    };

    struct MergedBody
    {
        std::vector<Vec3> mPositions;
        std::vector<Vec3> mNormals; // empty when the parts had none
        std::vector<Vec2> mUvs; // empty when the parts had none
        std::vector<std::string> mBones; // the canonical list of used bones
        std::vector<std::array<std::uint16_t, maxMergedInfluences>> mBoneIndices; // unused entries 0 with weight 0
        std::vector<std::array<float, maxMergedInfluences>> mBoneWeights; // largest first, sum 1
        std::vector<std::uint32_t> mTriangles; // three indices per triangle
        std::vector<std::string> mMaterials; // sorted by byte value
        std::vector<RegionRange> mRegions; // ordered by slot
        std::vector<Submesh> mSubmeshes; // ordered by (slot, material)
        std::vector<std::vector<std::uint32_t>> mWeldGroups; // output vertex indices, ascending; groups by first
        /// For every input part (as passed in) and every vertex: its output vertex.
        std::vector<std::vector<std::uint32_t>> mVertexMap;
        /// The canonical order: mPartOrder[rank] is the input index of the part with that rank.
        std::vector<std::size_t> mPartOrder;
        MergeReport mReport;
        friend bool operator==(const MergedBody&, const MergedBody&) = default;
    };

    struct MergeResult
    {
        MergedBody mBody;
        std::vector<std::string> mErrors; // empty when mBody is valid
        bool ok() const { return mErrors.empty(); }
    };

    /// The weld sets alone (no merging). Errors as in mergeParts (the same checks of the parts and seams).
    WeldMap computeWeldMap(
        const std::vector<PartMesh>& parts, const std::vector<SeamDeclaration>& seams, const MergeLimits& limits = {});

    /// The merge. A failed check leaves mBody empty and fills mErrors (at most MergeLimits::mMaxErrors).
    MergeResult mergeParts(const std::vector<PartMesh>& parts, const std::vector<SeamDeclaration>& seams,
        const MergeOptions& options = {}, const MergeLimits& limits = {});
}

#endif
