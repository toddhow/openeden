#ifndef OPENMW_COMPONENTS_LOD_CLUSTERBUILD_H
#define OPENMW_COMPONENTS_LOD_CLUSTERBUILD_H

// OpenEden world LOD (task 0059, stage S4a). Pure code that turns the far-visible statics of one tile into an
// ObjectCluster: classify each object, transform it into tile space, simplify it on its own with meshoptimizer, and
// merge the pieces into one mesh per material group. No GPU, no window, no game data, no clock.
//
// Determinism: the output is a function of the set of objects, not of the order they are given in, and not of the
// thread that runs it. (Byte equality across machines or compilers is not promised; meshoptimizer's result is a
// function of its input on one build.)

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "loderror.hpp"
#include "objectcluster.hpp"

namespace Lod
{
    /// What kind of record an object comes from. Only statics enter a cluster.
    enum class ObjectKind : std::uint8_t
    {
        Static,
        Activator,
        Door,
        Other,
    };

    /// What the classifier needs to know about an object.
    struct ObjectTraits
    {
        ObjectKind mKind = ObjectKind::Static;
        float mRadius = 0; // bounding sphere radius in world units, the reference's scale included
        std::uint32_t mTriangleCount = 0; // of the full-detail model
        bool mAnimated = false; // controllers, or any animation
        bool mSkinned = false;
        bool mHasParticles = false;
        bool mAlphaBlended = false; // sorted transparency (alpha test is allowed)
    };

    /// One level's parameters.
    struct ClusterLevelParams
    {
        std::uint32_t mLevel = 1;
        float mError = 64; // world units: the largest simplification error allowed
        float mDropFactor = 1; // an object with radius < mDropFactor * mError is left out
    };

    /// The simplification error of each cluster level, in world units (task 0066 stage d). Levels 1 to 3 are the values
    /// of task 0059, derived from the tile selection's pixel-error formula (720p, 60 degrees, 8 px: a level's error is
    /// about 1.28 percent of the distance where the level starts); level 4 continues the growth of about three times a
    /// level from note 0029. 0 for a level outside 1 to maxClusterLevel.
    float clusterLevelError(std::uint32_t level) noexcept;

    enum class ExcludeReason : std::uint8_t
    {
        None = 0, // included
        NotStatic,
        Empty, // no triangles
        Animated,
        Skinned,
        Particles,
        AlphaBlended,
        TooSmall,
        Count,
    };

    /// The first reason that applies, in the order of the enumeration above (NotStatic first, TooSmall last); None
    /// when the object is included. TooSmall is radius < mDropFactor * mError (strictly less).
    ExcludeReason classifyObject(const ObjectTraits& traits, const ClusterLevelParams& level) noexcept;

    struct SourceVertex
    {
        float mPosition[3] = { 0, 0, 0 };
        float mNormal[3] = { 0, 0, 1 };
        float mUv[2] = { 0, 0 };
        std::uint8_t mColor[4] = { 255, 255, 255, 255 };
    };

    /// One drawable part of a model with one material, in model space.
    struct SourcePiece
    {
        std::string mTexture;
        std::uint32_t mFlags = 0; // clusterFlagAlphaTest, clusterFlagTwoSided
        float mAlphaCutoff = 0;
        /// A time-driven effect of this piece (task 0096 stage b). Pieces with different effects never share a group.
        ClusterEffect mEffect;
        std::vector<SourceVertex> mVertices;
        std::vector<std::uint32_t> mIndices; // triangle list
    };

    struct SourceObject
    {
        ClusterRef mRef;
        std::int32_t mCellX = 0, mCellY = 0; // the cell the object stands in
        ObjectTraits mTraits;
        /// Model space to world space, row-major 3 x 4 (rows of [m00 m01 m02 tx]). Normals follow with the cofactor
        /// matrix; a negative determinant flips the winding.
        std::array<double, 12> mTransform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        std::vector<SourcePiece> mPieces;
        /// Task 0096 stage c: above 0 (at most maxClusterSwayLean) the object's pieces without an effect sway: they
        /// get the sway effect with this lean, and each vertex's colour alpha byte becomes its sway weight
        /// (swayEncodeWeight; the object's z range after the transform gives the base and the height). The model's
        /// own vertex alpha is lost.
        float mSwayLean = 0;
    };

    struct ClusterBuildInput
    {
        ClusterLevelParams mLevel;
        double mOriginX = 0, mOriginY = 0; // south-west corner of the tile, world units
        double mSide = 0; // tile side in world units
        double mCellSize = 8192; // world units
        std::int32_t mOriginCellX = 0, mOriginCellY = 0; // south-west cell of the tile
        std::vector<SourceObject> mObjects;
    };

    struct ClusterBuildStats
    {
        std::uint32_t mObjectsSeen = 0;
        std::uint32_t mObjectsIncluded = 0; // with at least one triangle left after simplification
        std::array<std::uint32_t, static_cast<std::size_t>(ExcludeReason::Count)> mExcluded{}; // by reason
        std::uint32_t mTrianglesBefore = 0; // of the included objects
        std::uint32_t mTrianglesAfter = 0;
        float mMaxSimplifyError = 0; // world units
        float mMaxDroppedRadius = 0; // of the objects excluded as TooSmall
        /// Pieces whose simplification error the simplifier reported above 1.01 times the level's error (the
        /// error counted in mMaxSimplifyError and the cluster is capped at the level's error: one stubborn piece
        /// must not make a whole tile look coarse).
        std::uint32_t mErrorOutliers = 0;
    };

    /// Builds the cluster. An empty result (no groups) is not an error: `cluster` is then default-constructed and the
    /// caller writes nothing. Errors (located by field): a level outside 1 to 3, non-positive side, error or cell
    /// size, two objects with the same reference, an object whose cell is outside the tile, a piece with an index
    /// outside its vertices or a number of indices that is not a multiple of 3, a non-finite number, more cells than
    /// maxClusterCells. The result passes validateObjectCluster.
    LodError buildObjectCluster(
        const ClusterBuildInput& input, ObjectCluster& cluster, ClusterBuildStats* stats = nullptr);
}

#endif
