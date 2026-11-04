#ifndef OPENEDEN_COMPONENTS_BODYNIF_SEAMANALYSIS_H
#define OPENEDEN_COMPONENTS_BODYNIF_SEAMANALYSIS_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage a): the measurements that decide whether and how
// stock body parts can be welded into one mesh. Pure maths over plain arrays; the numbers it returns are the
// "go/no-go" table of research note 0033. No game data is read here: the engine adapter feeds it parts.
//
// Definitions (the tests pin each one):
//   - A part's boundary vertices are the vertices on an open edge, found after welding the part's own vertices that
//     share a position exactly (a UV or normal split inside a part is not a boundary).
//   - A seam is a pair of parts. A boundary vertex of A is a candidate when its nearest boundary vertex of B is
//     closer than SeamOptions::mWindow; the same for B against A. The pairs are (vertex, nearest partner).
//   - The seam normal is the unit vector from the centre (mean vertex position) of A to the centre of B. (A ring
//     seam of a limb is perpendicular to the line between the two parts' centres, which is all this measure needs;
//     the raw 3D distance of a pair does not depend on it.) The signed gap of a pair is (position in B - position
//     in A) . normal: negative means the parts overlap, positive that there is a gap, within +-mCoincident that
//     they meet.
//   - Weight disagreement of a pair is the largest absolute difference, over bones, of the two vertices'
//     renormalised influence weights (a bone one vertex lacks counts as weight 0).
//
// Determinism: every list is processed in index order and every statistic uses nearest-rank percentiles; no clock,
// no randomness. Threading: pure functions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "skinnedshape.hpp"

namespace BodyNif
{
    /// All shapes of one part, concatenated: one vertex list, one triangle list, one bone-name table.
    struct PartMesh
    {
        std::string mName;
        std::vector<Vec3> mPositions;
        std::vector<Vec3> mNormals; // empty or one per vertex
        std::vector<Vec2> mUvs; // empty or one per vertex
        std::vector<std::uint32_t> mTriangles;
        std::vector<std::string> mBones;
        std::vector<VertexSkin> mSkin; // empty (rigid) or one per vertex, bone indices into mBones
        std::size_t mShapeCount = 0;
        bool mSkinned = false;
    };

    /// Concatenates the shapes (in the given order) into one part. Bone names are merged into one table in order of
    /// first use.
    PartMesh makePartMesh(const std::string& name, const std::vector<Shape>& shapes);

    /// Nearest-rank statistics of a list of numbers (all zero for an empty list). mP95 is the value at rank
    /// ceil(0.95 n) of the sorted list, mMedian the one at rank ceil(0.5 n).
    struct Distribution
    {
        std::size_t mCount = 0;
        double mMin = 0;
        double mMedian = 0;
        double mP95 = 0;
        double mMax = 0;
        friend bool operator==(const Distribution&, const Distribution&) = default;
    };

    Distribution distribution(std::vector<double> values);

    /// Indices (ascending) of the vertices on an open edge. Vertices at the same position (to the bit) are one for
    /// the purpose of finding edges; an edge is open when the welded vertices' triangles use it once.
    std::vector<std::size_t> boundaryVertices(const PartMesh& part);

    struct PartStats
    {
        std::size_t mVertices = 0;
        std::size_t mTriangles = 0;
        std::size_t mBones = 0;
        std::size_t mShapes = 0;
        bool mSkinned = false;
        std::size_t mBoundaryVertices = 0;
        /// Vertices with 0, 1, 2, 3, 4 influences after the cut.
        std::array<std::size_t, 5> mInfluenceHistogram{};
        /// Vertices that had more than 4 influences before the cut.
        std::size_t mOverCut = 0;
        /// Sum of the kept weights per skinned vertex.
        Distribution mWeightSum;
        /// Skinned vertices whose kept weights do not sum to 1 within 1e-3 (the validator's tolerance).
        std::size_t mWeightSumOff = 0;
        /// Vertices whose position equals an earlier vertex's, to the bit.
        std::size_t mDuplicatePositions = 0;
        /// ... of which the earlier vertex has another UV or normal (a split, not an error).
        std::size_t mDuplicateSplits = 0;
        /// Edges of the welded mesh used by one triangle (open), two (shared) and more than two (over-used).
        std::size_t mOpenEdges = 0;
        std::size_t mSharedEdges = 0;
        std::size_t mOverUsedEdges = 0;
        /// The axis-aligned bounds of the positions (zero when the part has no vertices).
        std::array<double, 3> mBoundsMin{};
        std::array<double, 3> mBoundsMax{};
    };

    PartStats analysePart(const PartMesh& part);

    struct SeamOptions
    {
        /// Boundary vertices farther than this from every boundary vertex of the other part are not on the seam.
        double mWindow = 2.0;
        /// The tolerance ladder for the counts of pairs: a pair is counted for every entry its distance is <=.
        std::array<double, 6> mLadder = { 0.0, 1e-4, 1e-3, 1e-2, 0.1, 0.5 };
        /// UVs and normals are compared for pairs closer than this.
        double mPairDistance = 1e-2;
        /// UV coordinates closer than this are equal.
        double mUvEqual = 1e-3;
        /// A signed gap within +- this is "meets".
        double mCoincident = 1e-3;
    };

    struct SeamReport
    {
        std::size_t mBoundaryA = 0;
        std::size_t mBoundaryB = 0;
        std::size_t mCandidatesA = 0; // boundary vertices of A with a partner within the window
        std::size_t mCandidatesB = 0;
        /// Distance of every pair (candidates of A first, then of B).
        Distribution mDistance;
        /// Pairs with a distance <= SeamOptions::mLadder[i].
        std::array<std::size_t, 6> mWithin{};
        Distribution mSignedGap;
        std::size_t mOverlapPairs = 0; // signed gap < -mCoincident
        std::size_t mGapPairs = 0; // signed gap > +mCoincident
        std::size_t mMeetingPairs = 0;
        /// Over pairs closer than mPairDistance.
        Distribution mWeightDisagreement;
        std::size_t mBoneSetDiffers = 0; // pairs (closer than mPairDistance) whose bone-name sets differ
        std::size_t mUvDiffers = 0; // pairs (closer than mPairDistance), both parts having UVs
        Distribution mNormalAngle; // degrees, pairs (closer than mPairDistance), both parts having normals
        /// The same pairing over ALL vertices of both parts, not only the boundary ones (a closed part, capped at its
        /// ends, has no boundary): a vertex of one part and its nearest vertex of the other, when within the window.
        std::size_t mAnyCandidatesA = 0;
        std::size_t mAnyCandidatesB = 0;
        Distribution mAnyDistance;
        std::array<std::size_t, 6> mAnyWithin{};
        /// False when no pair exists (a seam that does not touch).
        bool mTouches = false;
        friend bool operator==(const SeamReport&, const SeamReport&) = default;
    };

    SeamReport analyseSeam(const PartMesh& a, const PartMesh& b, const SeamOptions& options = {});
}

#endif
