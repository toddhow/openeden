// Task 0069 (stage a): the measurements that decide how stock body parts can be welded. Every case is a synthetic
// mesh (small grids), so the definitions in components/bodynif/seamanalysis.hpp are pinned by hand-computed numbers.
// No game data.
#include <gtest/gtest.h>

#include <components/bodynif/seamanalysis.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
    using namespace BodyNif;

    constexpr double tolerance = 1e-6;

    // A grid of `cells` x `cells` quads of size `size`, in the plane z = 0, from (x0, 0). Vertices are numbered row by
    // row ((cells + 1) per row). Normals (0, 0, 1), UVs (i, j) / cells, every vertex weighted 1.0 to the bone "B".
    PartMesh grid(const std::string& name, float x0, int cells = 2, float size = 0.5f)
    {
        PartMesh part;
        part.mName = name;
        part.mSkinned = true;
        part.mShapeCount = 1;
        part.mBones = { "B" };
        const int side = cells + 1;
        for (int j = 0; j < side; ++j)
        {
            for (int i = 0; i < side; ++i)
            {
                part.mPositions.push_back({ x0 + static_cast<float>(i) * size, static_cast<float>(j) * size, 0.f });
                part.mNormals.push_back({ 0.f, 0.f, 1.f });
                part.mUvs.push_back({ static_cast<float>(i) / static_cast<float>(cells),
                    static_cast<float>(j) / static_cast<float>(cells) });
                VertexSkin skin;
                skin.mCount = 1;
                skin.mBeforeCut = 1;
                skin.mInfluences[0] = { 0, 1.f };
                part.mSkin.push_back(skin);
            }
        }
        for (int j = 0; j < cells; ++j)
        {
            for (int i = 0; i < cells; ++i)
            {
                const auto a = static_cast<std::uint32_t>(j * side + i);
                const std::uint32_t b = a + 1, c = a + static_cast<std::uint32_t>(side), d = c + 1;
                for (const std::uint32_t index : { a, b, d, a, d, c })
                    part.mTriangles.push_back(index);
            }
        }
        return part;
    }

    // Vertices of a grid made by grid() in column `column` (0 is the left one).
    std::vector<std::size_t> columnOf(int cells, int column)
    {
        std::vector<std::size_t> result;
        for (int j = 0; j <= cells; ++j)
            result.push_back(static_cast<std::size_t>(j * (cells + 1) + column));
        return result;
    }

    SeamOptions narrow()
    {
        SeamOptions options;
        options.mWindow = 0.2;
        return options;
    }

    // ---- distribution --------------------------------------------------------------------------------------------

    TEST(BodyNifDistribution, NearestRankOnOneToTen)
    {
        std::vector<double> values;
        for (int i = 10; i >= 1; --i) // unsorted on purpose
            values.push_back(i);
        const Distribution result = distribution(values);
        EXPECT_EQ(result.mCount, 10u);
        EXPECT_DOUBLE_EQ(result.mMin, 1.0);
        EXPECT_DOUBLE_EQ(result.mMedian, 5.0); // rank ceil(0.5 * 10) = 5
        EXPECT_DOUBLE_EQ(result.mP95, 10.0); // rank ceil(0.95 * 10) = 10
        EXPECT_DOUBLE_EQ(result.mMax, 10.0);
    }

    TEST(BodyNifDistribution, OneValueAndNone)
    {
        const Distribution one = distribution({ 3.5 });
        EXPECT_EQ(one.mCount, 1u);
        EXPECT_DOUBLE_EQ(one.mMin, 3.5);
        EXPECT_DOUBLE_EQ(one.mMedian, 3.5);
        EXPECT_DOUBLE_EQ(one.mP95, 3.5);
        EXPECT_DOUBLE_EQ(one.mMax, 3.5);
        EXPECT_EQ(distribution({}), Distribution{});
    }

    TEST(BodyNifDistribution, TwentyValuesPutTheP95AtTheNineteenth)
    {
        std::vector<double> values;
        for (int i = 1; i <= 20; ++i)
            values.push_back(i);
        const Distribution result = distribution(values);
        EXPECT_DOUBLE_EQ(result.mMedian, 10.0); // rank 10
        EXPECT_DOUBLE_EQ(result.mP95, 19.0); // rank ceil(19) = 19
    }

    // ---- boundaries ----------------------------------------------------------------------------------------------

    TEST(BodyNifBoundary, AGridHasItsBorderOnly)
    {
        const PartMesh part = grid("A", 0.f);
        EXPECT_EQ(boundaryVertices(part), (std::vector<std::size_t>{ 0, 1, 2, 3, 5, 6, 7, 8 }));
    }

    TEST(BodyNifBoundary, ALoneTriangleIsAllBoundary)
    {
        PartMesh part;
        part.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
        part.mTriangles = { 0, 1, 2 };
        EXPECT_EQ(boundaryVertices(part), (std::vector<std::size_t>{ 0, 1, 2 }));
    }

    TEST(BodyNifBoundary, AUvSplitInsideAPartIsNotABoundary)
    {
        PartMesh part = grid("A", 0.f);
        // A second vertex at the centre's position, with another UV, used by the first four triangles.
        part.mPositions.push_back(part.mPositions[4]);
        part.mNormals.push_back(part.mNormals[4]);
        part.mUvs.push_back({ 0.9f, 0.9f });
        part.mSkin.push_back(part.mSkin[4]);
        for (std::size_t i = 0; i < 12; ++i)
        {
            if (part.mTriangles[i] == 4)
                part.mTriangles[i] = 9;
        }
        EXPECT_EQ(boundaryVertices(part), (std::vector<std::size_t>{ 0, 1, 2, 3, 5, 6, 7, 8 }));
    }

    TEST(BodyNifBoundary, AnEmptyPartHasNone)
    {
        EXPECT_TRUE(boundaryVertices(PartMesh{}).empty());
    }

    TEST(BodyNifBoundary, TwoSeparateTrianglesAreAllBoundary)
    {
        PartMesh part;
        part.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 5, 0, 0 }, { 6, 0, 0 }, { 5, 1, 0 } };
        part.mTriangles = { 0, 1, 2, 3, 4, 5 };
        EXPECT_EQ(boundaryVertices(part).size(), 6u);
    }

    // ---- part statistics -----------------------------------------------------------------------------------------

    TEST(BodyNifPartStats, CountsTheBasics)
    {
        const PartMesh part = grid("A", 0.f);
        const PartStats stats = analysePart(part);
        EXPECT_EQ(stats.mVertices, 9u);
        EXPECT_EQ(stats.mTriangles, 8u);
        EXPECT_EQ(stats.mBones, 1u);
        EXPECT_EQ(stats.mShapes, 1u);
        EXPECT_TRUE(stats.mSkinned);
        EXPECT_EQ(stats.mBoundaryVertices, 8u);
        EXPECT_EQ(stats.mInfluenceHistogram[1], 9u);
        EXPECT_EQ(stats.mInfluenceHistogram[0], 0u);
        EXPECT_EQ(stats.mOverCut, 0u);
        EXPECT_EQ(stats.mWeightSum.mCount, 9u);
        EXPECT_NEAR(stats.mWeightSum.mMin, 1.0, tolerance);
        EXPECT_NEAR(stats.mWeightSum.mMax, 1.0, tolerance);
        EXPECT_EQ(stats.mWeightSumOff, 0u);
        EXPECT_EQ(stats.mDuplicatePositions, 0u);
    }

    TEST(BodyNifPartStats, WeightsThatDoNotSumToOneAndTheCutAreCounted)
    {
        PartMesh part = grid("A", 0.f);
        part.mSkin[0].mInfluences[0].mWeight = 0.5f; // sums to 0.5 after the cut
        part.mSkin[1].mBeforeCut = 6; // had six before the cut
        part.mSkin[1].mCount = 4;
        for (int i = 0; i < 4; ++i)
            part.mSkin[1].mInfluences[i] = { 0, 0.25f };
        const PartStats stats = analysePart(part);
        EXPECT_EQ(stats.mWeightSumOff, 1u); // vertex 0 only: vertex 1 sums to 1.0
        EXPECT_EQ(stats.mOverCut, 1u);
        EXPECT_EQ(stats.mInfluenceHistogram[4], 1u);
        EXPECT_EQ(stats.mInfluenceHistogram[1], 8u);
        EXPECT_NEAR(stats.mWeightSum.mMin, 0.5, tolerance);
    }

    TEST(BodyNifPartStats, DuplicatePositionsAndSplits)
    {
        PartMesh part = grid("A", 0.f);
        // vertex 9: the same position as vertex 4 with another UV (a split); vertex 10: identical to vertex 4
        for (int k = 0; k < 2; ++k)
        {
            part.mPositions.push_back(part.mPositions[4]);
            part.mNormals.push_back(part.mNormals[4]);
            part.mUvs.push_back(k == 0 ? Vec2{ 0.9f, 0.9f } : part.mUvs[4]);
            part.mSkin.push_back(part.mSkin[4]);
        }
        const PartStats stats = analysePart(part);
        EXPECT_EQ(stats.mDuplicatePositions, 2u);
        EXPECT_EQ(stats.mDuplicateSplits, 1u);
    }

    TEST(BodyNifPartStats, ARigidPartHasNoWeights)
    {
        PartMesh part = grid("A", 0.f);
        part.mSkin.clear();
        part.mSkinned = false;
        part.mBones.clear();
        const PartStats stats = analysePart(part);
        EXPECT_FALSE(stats.mSkinned);
        EXPECT_EQ(stats.mWeightSum.mCount, 0u);
        EXPECT_EQ(stats.mWeightSumOff, 0u);
        EXPECT_EQ(stats.mBones, 0u);
    }

    TEST(BodyNifPartStats, EdgesAndBounds)
    {
        const PartStats stats = analysePart(grid("A", 0.f)); // 2 x 2 quads, 16 edges: 8 on the border, 8 inside
        EXPECT_EQ(stats.mOpenEdges, 8u);
        EXPECT_EQ(stats.mSharedEdges, 8u);
        EXPECT_EQ(stats.mOverUsedEdges, 0u);
        EXPECT_DOUBLE_EQ(stats.mBoundsMin[0], 0.0);
        EXPECT_DOUBLE_EQ(stats.mBoundsMin[1], 0.0);
        EXPECT_DOUBLE_EQ(stats.mBoundsMax[0], 1.0);
        EXPECT_DOUBLE_EQ(stats.mBoundsMax[1], 1.0);
        EXPECT_DOUBLE_EQ(stats.mBoundsMax[2], 0.0);
    }

    TEST(BodyNifPartStats, ThreeTrianglesOnOneEdgeAreOverUsed)
    {
        PartMesh part;
        part.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 } };
        part.mTriangles = { 0, 1, 2, 0, 1, 3, 0, 1, 4 }; // all three use the edge 0-1
        const PartStats stats = analysePart(part);
        EXPECT_EQ(stats.mOverUsedEdges, 1u);
        EXPECT_EQ(stats.mOpenEdges, 6u); // the other six edges are used once
        EXPECT_TRUE(boundaryVertices(part).size() == 5u);
    }

    TEST(BodyNifPartStats, AnEmptyPartHasZeroBounds)
    {
        const PartStats stats = analysePart(PartMesh{});
        EXPECT_DOUBLE_EQ(stats.mBoundsMax[0], 0.0);
        EXPECT_DOUBLE_EQ(stats.mBoundsMin[2], 0.0);
        EXPECT_EQ(stats.mOpenEdges, 0u);
    }

    // ---- merging shapes into a part ------------------------------------------------------------------------------

    TEST(BodyNifMakePart, ConcatenatesShapesAndMergesBonesInOrderOfFirstUse)
    {
        Shape first;
        first.mName = "first";
        first.mSkinned = true;
        first.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
        first.mTriangles = { 0, 1, 2 };
        first.mBones = { "X", "Y" };
        first.mSkin.resize(3);
        first.mSkin[0].mCount = 1;
        first.mSkin[0].mInfluences[0] = { 1, 1.f }; // bone "Y"
        Shape second;
        second.mName = "second";
        second.mSkinned = true;
        second.mPositions = { { 5, 0, 0 }, { 6, 0, 0 }, { 5, 1, 0 } };
        second.mTriangles = { 0, 1, 2 };
        second.mBones = { "Z", "Y" }; // "Y" is index 1 here and 1 in the merged table
        second.mSkin.resize(3);
        second.mSkin[0].mCount = 2;
        second.mSkin[0].mInfluences[0] = { 0, 0.5f }; // "Z"
        second.mSkin[0].mInfluences[1] = { 1, 0.5f }; // "Y"

        const PartMesh part = makePartMesh("both", { first, second });

        EXPECT_EQ(part.mName, "both");
        EXPECT_EQ(part.mShapeCount, 2u);
        EXPECT_TRUE(part.mSkinned);
        EXPECT_EQ(part.mPositions.size(), 6u);
        EXPECT_EQ(part.mTriangles, (std::vector<std::uint32_t>{ 0, 1, 2, 3, 4, 5 }));
        EXPECT_EQ(part.mBones, (std::vector<std::string>{ "X", "Y", "Z" }));
        ASSERT_EQ(part.mSkin.size(), 6u);
        EXPECT_EQ(part.mSkin[0].mInfluences[0].mBone, 1); // "Y"
        EXPECT_EQ(part.mSkin[3].mInfluences[0].mBone, 2); // "Z"
        EXPECT_EQ(part.mSkin[3].mInfluences[1].mBone, 1); // "Y"
    }

    TEST(BodyNifMakePart, AMixOfSkinnedAndRigidShapesIsSkinnedWithEmptyRigidInfluences)
    {
        Shape skinned;
        skinned.mSkinned = true;
        skinned.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
        skinned.mTriangles = { 0, 1, 2 };
        skinned.mBones = { "X" };
        skinned.mSkin.resize(3);
        Shape rigid;
        rigid.mPositions = { { 5, 0, 0 }, { 6, 0, 0 }, { 5, 1, 0 } };
        rigid.mTriangles = { 0, 1, 2 };

        const PartMesh part = makePartMesh("mixed", { skinned, rigid });

        EXPECT_TRUE(part.mSkinned);
        ASSERT_EQ(part.mSkin.size(), 6u);
        EXPECT_EQ(part.mSkin[4].mCount, 0);
        EXPECT_EQ(part.mPositions.size(), 6u);
    }

    // ---- seams ---------------------------------------------------------------------------------------------------

    TEST(BodyNifSeam, TwoGridsThatShareAnEdgeMeetExactly)
    {
        const PartMesh a = grid("A", 0.f);
        PartMesh b = grid("B", 1.f);
        // The grids number their UVs from 0 each, so give B's seam column the UVs of A's: one continuous surface.
        for (const std::size_t index : columnOf(2, 0))
            b.mUvs[index][0] = 1.f;

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_TRUE(report.mTouches);
        EXPECT_EQ(report.mBoundaryA, 8u);
        EXPECT_EQ(report.mBoundaryB, 8u);
        EXPECT_EQ(report.mCandidatesA, 3u);
        EXPECT_EQ(report.mCandidatesB, 3u);
        EXPECT_EQ(report.mDistance.mCount, 6u);
        EXPECT_NEAR(report.mDistance.mMax, 0.0, tolerance);
        for (std::size_t i = 0; i < report.mWithin.size(); ++i)
            EXPECT_EQ(report.mWithin[i], 6u) << "ladder step " << i;
        EXPECT_EQ(report.mMeetingPairs, 6u);
        EXPECT_EQ(report.mOverlapPairs, 0u);
        EXPECT_EQ(report.mGapPairs, 0u);
        EXPECT_NEAR(report.mSignedGap.mMin, 0.0, tolerance);
        EXPECT_NEAR(report.mSignedGap.mMax, 0.0, tolerance);
        EXPECT_NEAR(report.mWeightDisagreement.mMax, 0.0, tolerance);
        EXPECT_EQ(report.mBoneSetDiffers, 0u);
        EXPECT_EQ(report.mUvDiffers, 0u);
        EXPECT_NEAR(report.mNormalAngle.mMax, 0.0, 1e-4);
    }

    TEST(BodyNifSeam, AGapIsPositiveAlongTheSeamNormal)
    {
        const PartMesh a = grid("A", 0.f);
        const PartMesh b = grid("B", 1.05f);

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_EQ(report.mDistance.mCount, 6u);
        EXPECT_NEAR(report.mDistance.mMedian, 0.05, 1e-6);
        EXPECT_EQ(report.mGapPairs, 6u);
        EXPECT_EQ(report.mOverlapPairs, 0u);
        EXPECT_NEAR(report.mSignedGap.mMedian, 0.05, 1e-6);
        // the ladder: 0, 1e-4, 1e-3 and 1e-2 do not reach 0.05; 0.1 and 0.5 do
        EXPECT_EQ(report.mWithin[0], 0u);
        EXPECT_EQ(report.mWithin[3], 0u);
        EXPECT_EQ(report.mWithin[4], 6u);
        EXPECT_EQ(report.mWithin[5], 6u);
    }

    TEST(BodyNifSeam, AnOverlapIsNegative)
    {
        const PartMesh a = grid("A", 0.f);
        const PartMesh b = grid("B", 0.95f);

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_EQ(report.mOverlapPairs, 6u);
        EXPECT_EQ(report.mGapPairs, 0u);
        EXPECT_NEAR(report.mSignedGap.mMedian, -0.05, 1e-6);
    }

    TEST(BodyNifSeam, TheSideOfThePartsDoesNotChangeTheSignOfAGap)
    {
        // The same gap with the arguments swapped: the normal points from the first part to the second.
        const PartMesh a = grid("A", 0.f);
        const PartMesh b = grid("B", 1.05f);
        const SeamReport swapped = analyseSeam(b, a, narrow());
        EXPECT_EQ(swapped.mGapPairs, 6u);
        EXPECT_NEAR(swapped.mSignedGap.mMedian, 0.05, 1e-6);
    }

    TEST(BodyNifSeam, PartsThatDoNotTouchReportNothing)
    {
        const PartMesh a = grid("A", 0.f);
        const PartMesh b = grid("B", 10.f);

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_FALSE(report.mTouches);
        EXPECT_EQ(report.mCandidatesA, 0u);
        EXPECT_EQ(report.mCandidatesB, 0u);
        EXPECT_EQ(report.mDistance.mCount, 0u);
        EXPECT_EQ(report.mBoundaryA, 8u);
    }

    TEST(BodyNifSeam, TheWindowDecidesWhichBoundaryVerticesAreOnTheSeam)
    {
        const PartMesh a = grid("A", 0.f);
        const PartMesh b = grid("B", 1.f);
        SeamOptions wide;
        wide.mWindow = 0.6; // the border column at x = 0.5 is 0.5 from B's column at x = 1
        const SeamReport report = analyseSeam(a, b, wide);
        // A: the x = 1 column (3), the x = 0.5 vertices of the top and bottom rows (2): 5 within 0.6 of B's boundary
        EXPECT_EQ(report.mCandidatesA, 5u);
        EXPECT_EQ(report.mCandidatesB, 5u);
    }

    TEST(BodyNifSeam, DifferentUvsAtThePairsAreCounted)
    {
        const PartMesh a = grid("A", 0.f);
        PartMesh b = grid("B", 1.f);
        for (const std::size_t index : columnOf(2, 0))
            b.mUvs[index][0] += 0.1f;

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_EQ(report.mUvDiffers, 6u);
    }

    TEST(BodyNifSeam, TheNormalAngleIsMeasuredInDegrees)
    {
        const PartMesh a = grid("A", 0.f);
        PartMesh b = grid("B", 1.f);
        for (const std::size_t index : columnOf(2, 0))
            b.mNormals[index] = { 1.f, 0.f, 0.f };

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_NEAR(report.mNormalAngle.mMax, 90.0, 1e-3);
        EXPECT_NEAR(report.mNormalAngle.mMin, 90.0, 1e-3);
    }

    TEST(BodyNifSeam, AnotherBoneIsADisagreementOfOneAndADifferentBoneSet)
    {
        const PartMesh a = grid("A", 0.f);
        PartMesh b = grid("B", 1.f);
        b.mBones = { "C" };

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_NEAR(report.mWeightDisagreement.mMax, 1.0, tolerance);
        EXPECT_NEAR(report.mWeightDisagreement.mMin, 1.0, tolerance);
        EXPECT_EQ(report.mBoneSetDiffers, 6u);
    }

    TEST(BodyNifSeam, ASplitWeightIsTheLargestAbsoluteDifference)
    {
        const PartMesh a = grid("A", 0.f);
        PartMesh b = grid("B", 1.f);
        b.mBones = { "B", "C" };
        for (const std::size_t index : columnOf(2, 0))
        {
            b.mSkin[index].mCount = 2;
            b.mSkin[index].mInfluences[0] = { 0, 0.75f };
            b.mSkin[index].mInfluences[1] = { 1, 0.25f };
        }

        const SeamReport report = analyseSeam(a, b, narrow());

        // A has B:1; B's seam vertices have B:0.75, C:0.25: the largest difference is 0.25
        EXPECT_NEAR(report.mWeightDisagreement.mMax, 0.25, tolerance);
        EXPECT_EQ(report.mBoneSetDiffers, 6u);
    }

    TEST(BodyNifSeam, WeightsAreRenormalisedBeforeTheyAreCompared)
    {
        PartMesh a = grid("A", 0.f);
        const PartMesh b = grid("B", 1.f);
        for (const std::size_t index : columnOf(2, 2))
            a.mSkin[index].mInfluences[0].mWeight = 0.5f; // 0.5 renormalises to 1.0, like B's

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_NEAR(report.mWeightDisagreement.mMax, 0.0, tolerance);
    }

    TEST(BodyNifSeam, RigidPartsHaveNoDisagreementToReport)
    {
        PartMesh a = grid("A", 0.f);
        a.mSkin.clear();
        a.mSkinned = false;
        const PartMesh b = grid("B", 1.f);

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_TRUE(report.mTouches);
        EXPECT_EQ(report.mWeightDisagreement.mCount, 0u);
        EXPECT_EQ(report.mBoneSetDiffers, 0u);
    }

    TEST(BodyNifSeam, AClosedPartHasNoBoundaryButItsVerticesStillMeetTheOther)
    {
        PartMesh a; // a tetrahedron: closed, no open edge
        a.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        a.mTriangles = { 0, 1, 2, 0, 1, 3, 0, 2, 3, 1, 2, 3 };
        PartMesh b; // a triangle with one vertex where the tetrahedron has one
        b.mPositions = { { 1, 0, 0 }, { 2, 0, 0 }, { 1, 1, 0 } };
        b.mTriangles = { 0, 1, 2 };

        const SeamReport report = analyseSeam(a, b, narrow());

        EXPECT_EQ(report.mBoundaryA, 0u);
        EXPECT_FALSE(report.mTouches); // the boundary pairing finds nothing
        EXPECT_EQ(report.mAnyCandidatesA, 1u);
        EXPECT_EQ(report.mAnyCandidatesB, 1u);
        EXPECT_EQ(report.mAnyDistance.mCount, 2u);
        EXPECT_DOUBLE_EQ(report.mAnyDistance.mMax, 0.0);
        EXPECT_EQ(report.mAnyWithin[0], 2u);
        EXPECT_EQ(report.mAnyWithin[5], 2u);
    }

    TEST(BodyNifSeam, TheAllVertexPairingSeesInteriorVerticesToo)
    {
        // Two grids that share the column x = 1: the boundary pairing has 3 + 3 pairs; the all-vertex pairing has the
        // same 3 + 3 here (the other vertices are farther than the window), and never fewer.
        const PartMesh a = grid("A", 0.f);
        const PartMesh b = grid("B", 1.f);
        const SeamReport report = analyseSeam(a, b, narrow());
        EXPECT_EQ(report.mAnyCandidatesA, 3u);
        EXPECT_EQ(report.mAnyCandidatesB, 3u);
        EXPECT_GE(report.mAnyCandidatesA, report.mCandidatesA);
    }

    TEST(BodyNifSeam, TheReportIsDeterministic)
    {
        const PartMesh a = grid("A", 0.f, 4, 0.25f);
        const PartMesh b = grid("B", 1.01f, 4, 0.25f);
        EXPECT_EQ(analyseSeam(a, b), analyseSeam(a, b));
        EXPECT_EQ(analyseSeam(a, b, narrow()), analyseSeam(a, b, narrow()));
    }
}
