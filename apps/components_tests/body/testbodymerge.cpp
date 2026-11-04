// OpenEden AdvancedBodies, merged legacy body (task 0069, stage b): tests of the merger (components/body/bodymerge.*).
// Written from the spec before the merge code, on SYNTHETIC meshes only: no game data is read here.

#include <gtest/gtest.h>

#include <components/body/bodymerge.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace
{
    using namespace Body;

    constexpr float pi = 3.14159265358979f;

    /// A flat grid of cols x rows cells in the xy plane at z = 0: normals (0, 0, 1), UVs equal to 0.1 * position,
    /// counter-clockwise triangles seen from +z. Rigid on `bone`.
    PartMesh makeGrid(const std::string& name, int slot, const std::string& material, float x0, float y0, int cols,
        int rows, float cell, const std::string& bone)
    {
        PartMesh part;
        part.mName = name;
        part.mSlot = slot;
        part.mMaterial = material;
        part.mRigidBone = bone;
        for (int r = 0; r <= rows; ++r)
        {
            for (int c = 0; c <= cols; ++c)
            {
                const float x = x0 + static_cast<float>(c) * cell;
                const float y = y0 + static_cast<float>(r) * cell;
                part.mPositions.push_back({ x, y, 0.f });
                part.mNormals.push_back({ 0.f, 0.f, 1.f });
                part.mUvs.push_back({ x * 0.1f, y * 0.1f });
            }
        }
        const auto index = [cols](int c, int r) { return static_cast<std::uint32_t>(r * (cols + 1) + c); };
        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                part.mTriangles.insert(part.mTriangles.end(), { index(c, r), index(c + 1, r), index(c + 1, r + 1) });
                part.mTriangles.insert(part.mTriangles.end(), { index(c, r), index(c + 1, r + 1), index(c, r + 1) });
            }
        }
        return part;
    }

    PartMesh makeQuad(const std::string& name, int slot, float x0, const std::string& bone)
    {
        return makeGrid(name, slot, "mat", x0, 0.f, 1, 1, 1.f, bone);
    }

    PartMesh makeTriangle(const std::string& name, int slot, Vec3 a, Vec3 b, Vec3 c, const std::string& bone)
    {
        PartMesh part;
        part.mName = name;
        part.mSlot = slot;
        part.mMaterial = "mat";
        part.mRigidBone = bone;
        part.mPositions = { a, b, c };
        part.mTriangles = { 0, 1, 2 };
        return part;
    }

    void setSkin(PartMesh& part, const std::vector<std::string>& bones, std::size_t vertex,
        const std::vector<std::pair<std::uint32_t, float>>& influences)
    {
        part.mRigidBone.clear();
        part.mBones = bones;
        if (part.mSkin.size() != part.mPositions.size())
            part.mSkin.assign(part.mPositions.size(), PartVertexSkin{});
        PartVertexSkin& skin = part.mSkin[vertex];
        skin.mCount = static_cast<std::uint8_t>(influences.size());
        for (std::size_t i = 0; i < influences.size(); ++i)
            skin.mInfluences[i] = { influences[i].first, influences[i].second };
    }

    float weightOf(const MergedBody& body, std::size_t vertex, const std::string& bone)
    {
        float sum = 0.f;
        for (std::size_t i = 0; i < maxMergedInfluences; ++i)
        {
            if (body.mBoneWeights[vertex][i] > 0.f && body.mBones[body.mBoneIndices[vertex][i]] == bone)
                sum += body.mBoneWeights[vertex][i];
        }
        return sum;
    }

    bool hasMessage(const MergeResult& result, const std::string& text)
    {
        for (const std::string& message : result.mErrors)
        {
            if (message.find(text) != std::string::npos)
                return true;
        }
        return false;
    }

    /// The part of a MergedBody that does not depend on how the caller numbered the parts.
    MergedBody withoutCallerNumbering(MergedBody body)
    {
        body.mVertexMap.clear();
        body.mPartOrder.clear();
        return body;
    }

    std::vector<SeamDeclaration> oneSeam(int a, int b, double tolerance = 0.01)
    {
        return { SeamDeclaration{ a, b, tolerance } };
    }

    std::array<std::uint32_t, 3> triangleAt(const MergedBody& body, std::size_t triangle)
    {
        return { body.mTriangles[triangle * 3], body.mTriangles[triangle * 3 + 1], body.mTriangles[triangle * 3 + 2] };
    }

    Vec3 applyMat(const Mat34& m, const Vec3& v)
    {
        return { static_cast<float>(m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3]),
            static_cast<float>(m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7]),
            static_cast<float>(m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11]) };
    }
}

// ----------------------------------------------------------------------------------------------------------------
// Welding
// ----------------------------------------------------------------------------------------------------------------

TEST(BodyMergeWeld, two_quads_sharing_an_edge_have_one_welded_edge_and_no_duplicate_vertices)
{
    const std::vector<PartMesh> parts = { makeQuad("A", 1, 0.f, "b"), makeQuad("B", 2, 1.f, "b") };
    const MergeResult result = mergeParts(parts, oneSeam(1, 2));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    EXPECT_EQ(body.mPositions.size(), 6u);
    EXPECT_EQ(body.mTriangles.size(), 12u);
    EXPECT_TRUE(body.mWeldGroups.empty());
    EXPECT_EQ(body.mReport.mCollapsedVertices, 2u);
    EXPECT_EQ(body.mReport.mWeldSets, 2u);
    EXPECT_EQ(body.mReport.mWeldGroups, 0u);
    // The documented order: output vertices follow the lowest member of each vertex (A: 0 to 3, B's own: 4 and 5).
    ASSERT_EQ(body.mVertexMap.size(), 2u);
    EXPECT_EQ(body.mVertexMap[0], (std::vector<std::uint32_t>{ 0, 1, 2, 3 }));
    EXPECT_EQ(body.mVertexMap[1], (std::vector<std::uint32_t>{ 1, 4, 3, 5 }));
}

TEST(BodyMergeWeld, a_uv_split_seam_keeps_two_vertices_and_one_weld_group_per_pair)
{
    PartMesh a = makeQuad("A", 1, 0.f, "b");
    PartMesh b = makeQuad("B", 2, 1.f, "b");
    for (Vec2& uv : b.mUvs)
        uv[0] += 0.5f; // B's UVs are a different island: the seam is a UV split
    const MergeResult result = mergeParts({ a, b }, oneSeam(1, 2));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    EXPECT_EQ(body.mPositions.size(), 8u);
    ASSERT_EQ(body.mWeldGroups.size(), 2u);
    EXPECT_EQ(body.mWeldGroups[0], (std::vector<std::uint32_t>{ 1, 4 }));
    EXPECT_EQ(body.mWeldGroups[1], (std::vector<std::uint32_t>{ 3, 6 }));
    EXPECT_EQ(body.mReport.mWeldGroups, 2u);
    EXPECT_EQ(body.mReport.mCollapsedVertices, 0u);
    // The members of a group coincide exactly.
    EXPECT_EQ(body.mPositions[1], body.mPositions[4]);
    EXPECT_EQ(body.mPositions[3], body.mPositions[6]);
    // Their UVs are untouched.
    EXPECT_NE(body.mUvs[1], body.mUvs[4]);
}

TEST(BodyMergeWeld, members_of_a_set_take_the_position_of_the_lowest_member)
{
    PartMesh a = makeQuad("A", 1, 0.f, "b");
    PartMesh b = makeQuad("B", 2, 1.004f, "b"); // 0.004 off the seam: inside the tolerance of 0.01
    for (Vec2& uv : b.mUvs)
        uv[0] += 0.5f;
    const MergeResult result = mergeParts({ a, b }, oneSeam(1, 2));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    ASSERT_EQ(body.mWeldGroups.size(), 2u);
    EXPECT_EQ(body.mPositions[4][0], 1.f); // B's seam vertex snapped to A's
    EXPECT_FLOAT_EQ(body.mPositions[5][0], 2.004f); // B's other vertices stay
}

TEST(BodyMergeWeld, a_normal_crease_stays_separate_and_a_small_difference_merges)
{
    PartMesh a = makeQuad("A", 1, 0.f, "b");
    PartMesh b = makeQuad("B", 2, 1.f, "b");
    for (Vec3& n : b.mNormals)
        n = { 0.f, 1.f, 0.f }; // 90 degrees from A's
    MergeResult crease = mergeParts({ a, b }, oneSeam(1, 2));
    ASSERT_TRUE(crease.ok()) << crease.mErrors.front();
    EXPECT_EQ(crease.mBody.mWeldGroups.size(), 2u);
    EXPECT_EQ(crease.mBody.mPositions.size(), 8u);

    const float one = pi / 180.f;
    for (Vec3& n : b.mNormals)
        n = { std::sin(one), 0.f, std::cos(one) }; // one degree from A's
    MergeResult smooth = mergeParts({ a, b }, oneSeam(1, 2));
    ASSERT_TRUE(smooth.ok()) << smooth.mErrors.front();
    EXPECT_TRUE(smooth.mBody.mWeldGroups.empty());
    EXPECT_EQ(smooth.mBody.mPositions.size(), 6u);
    EXPECT_NEAR(smooth.mBody.mReport.mMaxMergedNormalAngle, one, 1e-5);
    // The merged vertex's normal is the normalised mean, half a degree from either.
    const Vec3 n = smooth.mBody.mNormals[1];
    EXPECT_NEAR(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 1.0, 1e-5);
    EXPECT_NEAR(std::acos(n[2]), 0.5f * one, 1e-4);
}

TEST(BodyMergeWeld, the_tolerance_is_inclusive_inside_and_exclusive_outside)
{
    const auto weldedAt = [](float gap, double tolerance) {
        const std::vector<PartMesh> parts = { makeQuad("A", 1, 0.f, "b"), makeQuad("B", 2, 1.f + gap, "b") };
        const MergeResult result = mergeParts(parts, oneSeam(1, 2, tolerance));
        EXPECT_TRUE(result.ok());
        return result.ok() ? result.mBody.mReport.mWeldSets : 99u;
    };
    EXPECT_EQ(weldedAt(0.0099f, 0.01), 2u); // just inside
    EXPECT_EQ(weldedAt(0.0101f, 0.01), 0u); // just outside
    EXPECT_EQ(weldedAt(0.f, 0.0), 2u); // exact equality, tolerance 0
    EXPECT_EQ(weldedAt(1e-6f, 0.0), 0u); // a near miss at tolerance 0
}

TEST(BodyMergeWeld, overlapping_shells_without_a_declared_seam_are_not_welded)
{
    // Two quads lying on top of each other: every vertex of A has a twin in B. No seam is declared between slots
    // 1 and 2 (only a seam of other slots), so nothing welds.
    const std::vector<PartMesh> parts
        = { makeQuad("A", 1, 0.f, "b"), makeQuad("B", 2, 0.f, "b"), makeQuad("C", 3, 5.f, "b") };
    const MergeResult result = mergeParts(parts, oneSeam(2, 3));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    EXPECT_EQ(result.mBody.mPositions.size(), 12u);
    EXPECT_EQ(result.mBody.mReport.mWeldSets, 0u);
}

TEST(BodyMergeWeld, overlapping_shells_with_a_declared_seam_but_too_far_apart_are_not_welded)
{
    // The overlapping joints of Morrowind's body: the parts interpenetrate, the nearest vertices are far apart in
    // comparison with the tolerance.
    PartMesh a = makeQuad("A", 1, 0.f, "b");
    PartMesh b = makeQuad("B", 2, 0.f, "b");
    for (Vec3& p : b.mPositions)
        p[2] += 0.3f;
    const MergeResult result = mergeParts({ a, b }, oneSeam(1, 2));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    EXPECT_EQ(result.mBody.mPositions.size(), 8u);
    EXPECT_EQ(result.mBody.mReport.mWeldSets, 0u);
}

TEST(BodyMergeWeld, a_chain_of_close_vertices_is_one_set)
{
    // q is within 0.01 of p0 and of p1, which are 0.009 apart (inside the tolerance too): one set through q. With the
    // two 0.012 apart the far one stays out: see BodyMergeReview.a_weld_set_never_reaches_further_than_the_tolerance.
    const PartMesh x = makeTriangle("X", 1, { 0.f, 0.f, 0.f }, { 0.009f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, "b");
    const PartMesh y = makeTriangle("Y", 2, { 0.006f, 0.f, 0.f }, { 3.f, 0.f, 0.f }, { 3.f, 1.f, 0.f }, "b");
    const MergeResult result = mergeParts({ x, y }, oneSeam(1, 2));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    EXPECT_EQ(result.mBody.mReport.mWeldSets, 1u);
    // Three input vertices in one set with equal UV and normal collapse into one.
    EXPECT_EQ(result.mBody.mReport.mCollapsedVertices, 2u);
}

TEST(BodyMergeWeld, two_parts_of_one_slot_weld_through_a_seam_of_that_slot_with_itself)
{
    PartMesh a = makeQuad("A", 3, 0.f, "b");
    PartMesh b = makeQuad("B", 3, 1.f, "b");
    b.mMaterial = "other";
    EXPECT_EQ(mergeParts({ a, b }, {}).mBody.mPositions.size(), 8u);
    const MergeResult welded = mergeParts({ a, b }, oneSeam(3, 3));
    ASSERT_TRUE(welded.ok()) << welded.mErrors.front();
    EXPECT_EQ(welded.mBody.mPositions.size(), 6u);
}

TEST(BodyMergeWeld, a_triangle_whose_corners_collapse_is_dropped_and_counted)
{
    const PartMesh x = makeTriangle("X", 1, { 0.f, 0.f, 0.f }, { 0.001f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, "b");
    const PartMesh y = makeTriangle("Y", 2, { 0.0005f, 0.f, 0.f }, { 3.f, 0.f, 0.f }, { 3.f, 1.f, 0.f }, "b");
    const MergeResult result = mergeParts({ x, y }, oneSeam(1, 2));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    EXPECT_EQ(result.mBody.mReport.mDroppedTriangles, 1u);
    EXPECT_EQ(result.mBody.mTriangles.size(), 3u); // only Y's triangle is left
    EXPECT_EQ(result.mBody.mReport.mInputTriangles, 2u);
    EXPECT_EQ(result.mBody.mReport.mOutputTriangles, 1u);
}

TEST(BodyMergeWeld, the_weld_map_alone_lists_the_sets_without_merging)
{
    const std::vector<PartMesh> parts = { makeQuad("A", 1, 0.f, "b"), makeQuad("B", 2, 1.f, "b") };
    const WeldMap map = computeWeldMap(parts, oneSeam(1, 2));
    ASSERT_TRUE(map.mErrors.empty()) << map.mErrors.front();
    ASSERT_EQ(map.mSets.size(), 2u);
    EXPECT_EQ(map.mSets[0], (std::vector<WeldMember>{ { 0, 1 }, { 1, 0 } }));
    EXPECT_EQ(map.mSets[1], (std::vector<WeldMember>{ { 0, 3 }, { 1, 2 } }));
    ASSERT_EQ(map.mPositions.size(), 2u);
    EXPECT_EQ(map.mPositions[0], (Vec3{ 1.f, 0.f, 0.f }));
    EXPECT_EQ(map.mPositions[1], (Vec3{ 1.f, 1.f, 0.f }));
    ASSERT_EQ(map.mPairsPerSeam.size(), 1u);
    EXPECT_EQ(map.mPairsPerSeam[0], 2u);
}

TEST(BodyMergeWeld, the_weld_map_does_not_depend_on_the_order_of_the_parts)
{
    const std::vector<PartMesh> parts
        = { makeQuad("A", 1, 0.f, "b"), makeQuad("B", 2, 1.f, "b"), makeQuad("C", 3, 2.f, "b") };
    const std::vector<SeamDeclaration> seams = { { 1, 2, 0.01 }, { 2, 3, 0.01 } };
    const WeldMap forward = computeWeldMap(parts, seams);
    const std::vector<PartMesh> reversed(parts.rbegin(), parts.rend());
    const WeldMap backward = computeWeldMap(reversed, seams);
    ASSERT_EQ(forward.mSets.size(), backward.mSets.size());
    ASSERT_FALSE(forward.mSets.empty());
    // Same sets once the part numbers are mapped back (reversed: input 2 is A, 1 is B, 0 is C).
    for (std::size_t i = 0; i < forward.mSets.size(); ++i)
    {
        std::vector<WeldMember> mapped;
        for (const WeldMember& m : backward.mSets[i])
            mapped.push_back({ static_cast<std::uint32_t>(2 - m.mPart), m.mVertex });
        EXPECT_EQ(forward.mSets[i], mapped);
    }
    EXPECT_EQ(forward.mPositions, backward.mPositions);
}

// The Skyrim-like case: body, hands and feet meet in exactly coincident rows of vertices. The same code, tolerance 0.
TEST(BodyMergeSeamless, body_hands_and_feet_with_exact_seams_merge_at_tolerance_zero)
{
    const PartMesh body = makeGrid("body", 0, "skin", 0.f, 0.f, 4, 4, 1.f, "pelvis");
    const PartMesh hands = makeGrid("hands", 1, "skin", 4.f, 0.f, 2, 4, 1.f, "hand");
    const PartMesh feet = makeGrid("feet", 2, "skin", 0.f, -2.f, 4, 2, 1.f, "foot");
    const std::vector<SeamDeclaration> seams = { { 0, 1, 0.0 }, { 0, 2, 0.0 } };
    const MergeResult result = mergeParts({ body, hands, feet }, seams);
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& merged = result.mBody;
    EXPECT_EQ(merged.mReport.mInputVertices, 25u + 15u + 15u);
    EXPECT_EQ(merged.mPositions.size(), 45u);
    EXPECT_EQ(merged.mReport.mCollapsedVertices, 10u);
    EXPECT_TRUE(merged.mWeldGroups.empty());
    EXPECT_EQ(merged.mReport.mDroppedTriangles, 0u);
    EXPECT_EQ(merged.mTriangles.size(), (4u * 4u + 2u * 4u + 4u * 2u) * 6u);
    ASSERT_EQ(merged.mRegions.size(), 3u);
    // The shared corner (4, 0) is one vertex for all three parts.
    EXPECT_EQ(merged.mVertexMap[0][4], merged.mVertexMap[1][0]);
    EXPECT_EQ(merged.mVertexMap[0][4], merged.mVertexMap[2][14]);
}

TEST(BodyMergeSeamless, a_seamless_pair_with_different_bone_weights_gets_the_mean_at_the_seam_only)
{
    const PartMesh body = makeQuad("body", 0, 0.f, "spine");
    const PartMesh hands = makeQuad("hands", 1, 1.f, "hand");
    const MergeResult result = mergeParts({ body, hands }, oneSeam(0, 1, 0.0));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& merged = result.mBody;
    ASSERT_EQ(merged.mPositions.size(), 6u);
    EXPECT_FLOAT_EQ(weightOf(merged, 0, "spine"), 1.f); // away from the seam
    EXPECT_FLOAT_EQ(weightOf(merged, 5, "hand"), 1.f);
    EXPECT_FLOAT_EQ(weightOf(merged, 1, "spine"), 0.5f); // on the seam
    EXPECT_FLOAT_EQ(weightOf(merged, 1, "hand"), 0.5f);
}

// ----------------------------------------------------------------------------------------------------------------
// Placement, mirror
// ----------------------------------------------------------------------------------------------------------------

TEST(BodyMergePlacement, a_mirrored_part_flips_x_the_winding_and_the_normals)
{
    PartMesh part = makeTriangle("L", 1, { 1.f, 0.f, 0.f }, { 2.f, 0.f, 0.f }, { 1.f, 1.f, 0.f }, "b");
    part.mNormals = { { 0.6f, 0.f, 0.8f }, { 0.6f, 0.f, 0.8f }, { 0.6f, 0.f, 0.8f } };
    part.mMirror = true;
    const MergeResult result = mergeParts({ part }, {});
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    EXPECT_EQ(body.mPositions[0], (Vec3{ -1.f, 0.f, 0.f }));
    EXPECT_EQ(body.mPositions[1], (Vec3{ -2.f, 0.f, 0.f }));
    EXPECT_EQ(body.mPositions[2], (Vec3{ -1.f, 1.f, 0.f }));
    EXPECT_FLOAT_EQ(body.mNormals[0][0], -0.6f);
    EXPECT_FLOAT_EQ(body.mNormals[0][2], 0.8f);
    EXPECT_EQ(triangleAt(body, 0), (std::array<std::uint32_t, 3>{ 0, 2, 1 })); // the winding is flipped
}

TEST(BodyMergePlacement, a_mirror_and_a_mirroring_placement_cancel)
{
    PartMesh part = makeTriangle("L", 1, { 1.f, 0.f, 0.f }, { 2.f, 0.f, 0.f }, { 1.f, 1.f, 0.f }, "b");
    part.mNormals = { { 0.6f, 0.f, 0.8f }, { 0.6f, 0.f, 0.8f }, { 0.6f, 0.f, 0.8f } };
    part.mMirror = true;
    part.mPlacement = { -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }; // the placement mirrors x again
    const MergeResult result = mergeParts({ part }, {});
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    EXPECT_EQ(result.mBody.mPositions[0], (Vec3{ 1.f, 0.f, 0.f }));
    EXPECT_EQ(triangleAt(result.mBody, 0), (std::array<std::uint32_t, 3>{ 0, 1, 2 }));
    EXPECT_FLOAT_EQ(result.mBody.mNormals[0][0], 0.6f);
}

TEST(BodyMergePlacement, the_placement_moves_positions_and_turns_normals)
{
    PartMesh part = makeTriangle("P", 1, { 1.f, 0.f, 0.f }, { 2.f, 0.f, 0.f }, { 1.f, 1.f, 0.f }, "b");
    part.mNormals = { { 1.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 1.f, 0.f, 0.f } };
    part.mPlacement = { 0, -1, 0, 5, 1, 0, 0, 0, 0, 0, 1, 0 }; // a quarter turn about z, then 5 along x
    const MergeResult result = mergeParts({ part }, {});
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    EXPECT_EQ(body.mPositions[0], (Vec3{ 5.f, 1.f, 0.f }));
    EXPECT_EQ(body.mPositions[1], (Vec3{ 5.f, 2.f, 0.f }));
    EXPECT_NEAR(body.mNormals[0][0], 0.f, 1e-6);
    EXPECT_NEAR(body.mNormals[0][1], 1.f, 1e-6);
    EXPECT_EQ(triangleAt(body, 0), (std::array<std::uint32_t, 3>{ 0, 1, 2 })); // a rotation keeps the winding
}

TEST(BodyMergePlacement, a_non_uniform_scale_keeps_normals_perpendicular)
{
    PartMesh part = makeTriangle("S", 1, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 1.f }, { 0.f, 1.f, 0.f }, "b");
    // The surface x - z = 0 ... use the plane through (0,0,0), (1,0,1) and (0,1,0): normal (-1, 0, 1)/sqrt(2).
    part.mNormals.assign(3, Vec3{ -0.70710678f, 0.f, 0.70710678f });
    part.mPlacement = { 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }; // stretch x by 2
    const MergeResult result = mergeParts({ part }, {});
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    // The stretched edge (2, 0, 1) must be perpendicular to the new normal.
    const Vec3 e = { body.mPositions[1][0] - body.mPositions[0][0], body.mPositions[1][1] - body.mPositions[0][1],
        body.mPositions[1][2] - body.mPositions[0][2] };
    const Vec3 n = body.mNormals[0];
    EXPECT_NEAR(e[0] * n[0] + e[1] * n[1] + e[2] * n[2], 0.0, 1e-5);
    EXPECT_NEAR(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 1.0, 1e-5);
}

// ----------------------------------------------------------------------------------------------------------------
// Influences
// ----------------------------------------------------------------------------------------------------------------

TEST(BodyMergeWeights, a_rigid_part_gets_weight_one_on_its_bone_exactly)
{
    const MergeResult result = mergeParts({ makeQuad("A", 1, 0.f, "forearm") }, {});
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    ASSERT_EQ(result.mBody.mBones, (std::vector<std::string>{ "forearm" }));
    for (std::size_t v = 0; v < result.mBody.mPositions.size(); ++v)
    {
        EXPECT_EQ(result.mBody.mBoneWeights[v][0], 1.0f);
        EXPECT_EQ(result.mBody.mBoneIndices[v][0], 0u);
        for (std::size_t i = 1; i < maxMergedInfluences; ++i)
            EXPECT_EQ(result.mBody.mBoneWeights[v][i], 0.f);
    }
}

TEST(BodyMergeWeights, more_than_four_influences_are_cut_with_ties_by_canonical_bone_order_and_renormalised)
{
    PartMesh part = makeTriangle("S", 1, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, "");
    const std::vector<std::string> bones = { "b0", "b1", "b2", "b3", "b4", "b5" };
    // Weights 0.1 0.3 0.2 0.2 0.1 0.1: the four largest are b1, b2, b3 (a tie of b2 and b3) and one of the three 0.1.
    setSkin(part, bones, 0, { { 0, 0.1f }, { 1, 0.3f }, { 2, 0.2f }, { 3, 0.2f }, { 4, 0.1f }, { 5, 0.1f } });
    setSkin(part, bones, 1, { { 0, 1.f } });
    setSkin(part, bones, 2, { { 0, 1.f } });
    MergeOptions options;
    options.mBoneOrder = bones;
    const MergeResult result = mergeParts({ part }, {}, options);
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    EXPECT_FLOAT_EQ(weightOf(body, 0, "b1"), 0.375f);
    EXPECT_FLOAT_EQ(weightOf(body, 0, "b2"), 0.25f);
    EXPECT_FLOAT_EQ(weightOf(body, 0, "b3"), 0.25f);
    EXPECT_FLOAT_EQ(weightOf(body, 0, "b0"), 0.125f); // the lowest canonical index of the three-way tie
    EXPECT_EQ(weightOf(body, 0, "b4"), 0.f);
    EXPECT_EQ(weightOf(body, 0, "b5"), 0.f);
    // Largest first, ties by bone index.
    EXPECT_GE(body.mBoneWeights[0][0], body.mBoneWeights[0][1]);
    float sum = 0.f;
    for (float w : body.mBoneWeights[0])
        sum += w;
    EXPECT_NEAR(sum, 1.f, 1e-6);
    EXPECT_EQ(body.mBones, (std::vector<std::string>{ "b0", "b1", "b2", "b3" })); // only used bones are listed
}

TEST(BodyMergeWeights, a_bone_named_twice_adds_and_zero_weights_drop)
{
    PartMesh part = makeTriangle("S", 1, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, "");
    const std::vector<std::string> bones = { "x", "y" };
    setSkin(part, bones, 0, { { 0, 0.25f }, { 1, 0.f }, { 0, 0.25f } }); // x twice, y with weight 0
    setSkin(part, bones, 1, { { 1, 2.f } }); // not normalised in the input: renormalised
    setSkin(part, bones, 2, { { 0, 1.f } });
    const MergeResult result = mergeParts({ part }, {});
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    EXPECT_FLOAT_EQ(weightOf(result.mBody, 0, "x"), 1.f);
    EXPECT_EQ(weightOf(result.mBody, 0, "y"), 0.f);
    EXPECT_FLOAT_EQ(weightOf(result.mBody, 1, "y"), 1.f);
}

TEST(BodyMergeWeights, a_weld_set_gets_the_equal_weight_mean_bone_by_bone)
{
    // A's seam vertex: x 1.0. B's seam vertex: y 0.5, z 0.5. The mean: x 0.5, y 0.25, z 0.25.
    PartMesh a = makeQuad("A", 1, 0.f, "");
    PartMesh b = makeQuad("B", 2, 1.f, "");
    for (std::size_t v = 0; v < 4; ++v)
    {
        setSkin(a, { "x" }, v, { { 0, 1.f } });
        setSkin(b, { "y", "z" }, v, { { 0, 0.5f }, { 1, 0.5f } });
    }
    const MergeResult result = mergeParts({ a, b }, oneSeam(1, 2));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    EXPECT_FLOAT_EQ(weightOf(body, 1, "x"), 0.5f);
    EXPECT_FLOAT_EQ(weightOf(body, 1, "y"), 0.25f);
    EXPECT_FLOAT_EQ(weightOf(body, 1, "z"), 0.25f);
    EXPECT_FLOAT_EQ(weightOf(body, 0, "x"), 1.f); // away from the seam
    EXPECT_FLOAT_EQ(weightOf(body, 5, "y"), 0.5f);
    EXPECT_NEAR(body.mReport.mMaxWeightDisagreement, 1.0, 1e-6); // x: 1 against 0
}

TEST(BodyMergeWeights, seam_vertices_of_a_uv_split_share_the_mean_too)
{
    PartMesh a = makeQuad("A", 1, 0.f, "armL");
    PartMesh b = makeQuad("B", 2, 1.f, "handL");
    for (Vec2& uv : b.mUvs)
        uv[0] += 0.5f;
    const MergeResult result = mergeParts({ a, b }, oneSeam(1, 2));
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    ASSERT_EQ(result.mBody.mWeldGroups.size(), 2u);
    for (const auto& group : result.mBody.mWeldGroups)
    {
        for (std::uint32_t v : group)
        {
            EXPECT_FLOAT_EQ(weightOf(result.mBody, v, "armL"), 0.5f);
            EXPECT_FLOAT_EQ(weightOf(result.mBody, v, "handL"), 0.5f);
        }
    }
}

TEST(BodyMergeWeights, the_bone_list_starts_with_the_known_order_then_sorts_the_rest_by_name)
{
    PartMesh a = makeQuad("A", 1, 0.f, "zeta");
    PartMesh b = makeQuad("B", 2, 5.f, "alpha");
    PartMesh c = makeQuad("C", 3, 10.f, "root");
    PartMesh d = makeQuad("D", 4, 15.f, "beta");
    MergeOptions options;
    options.mBoneOrder = { "root", "unused", "zeta" };
    const MergeResult result = mergeParts({ a, b, c, d }, {}, options);
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    EXPECT_EQ(result.mBody.mBones, (std::vector<std::string>{ "root", "zeta", "alpha", "beta" }));
}

// ----------------------------------------------------------------------------------------------------------------
// Order and determinism
// ----------------------------------------------------------------------------------------------------------------

TEST(BodyMergeOrder, regions_submeshes_and_materials_have_the_documented_order)
{
    PartMesh slot5 = makeQuad("five", 5, 0.f, "b");
    PartMesh slot2a = makeQuad("two a", 2, 3.f, "b");
    PartMesh slot2b = makeQuad("two b", 2, 6.f, "b");
    slot2a.mMaterial = "zz";
    slot2b.mMaterial = "aa";
    const MergeResult result = mergeParts({ slot5, slot2a, slot2b }, {});
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    EXPECT_EQ(body.mMaterials, (std::vector<std::string>{ "aa", "mat", "zz" }));
    ASSERT_EQ(body.mRegions.size(), 2u);
    EXPECT_EQ(body.mRegions[0], (RegionRange{ 2, 0, 4 })); // slot 2: two quads, two triangles each
    EXPECT_EQ(body.mRegions[1], (RegionRange{ 5, 4, 2 }));
    ASSERT_EQ(body.mSubmeshes.size(), 3u);
    EXPECT_EQ(body.mSubmeshes[0], (Submesh{ 0, 2, 0, 2 })); // slot 2, material "aa"
    EXPECT_EQ(body.mSubmeshes[1], (Submesh{ 2, 2, 2, 2 })); // slot 2, material "zz"
    EXPECT_EQ(body.mSubmeshes[2], (Submesh{ 1, 5, 4, 2 })); // slot 5, material "mat"
    EXPECT_EQ(body.mReport.mParts, 3u);
    // The first part in canonical order is "two b" (slot 2, material "aa"): input index 2.
    EXPECT_EQ(body.mPartOrder, (std::vector<std::size_t>{ 2, 1, 0 }));
}

TEST(BodyMergeOrder, the_result_does_not_depend_on_the_order_the_parts_are_passed_in)
{
    std::vector<PartMesh> parts = { makeGrid("chest", 3, "skin", 0.f, 0.f, 2, 2, 1.f, "chest"),
        makeGrid("arm", 4, "skin", 2.f, 0.f, 2, 2, 1.f, "arm"),
        makeGrid("neck", 1, "skin", 0.f, 2.f, 2, 1, 1.f, "neck"),
        makeGrid("arm", 4, "skin", 2.f, 0.f, 2, 2, 1.f, "arm2") }; // two parts alike in slot, material and name
    parts[3].mPositions[0][2] = 0.5f; // but different in content: the content breaks the tie
    const std::vector<SeamDeclaration> seams = { { 3, 4, 0.01 }, { 1, 3, 0.01 } };
    std::vector<std::size_t> order(parts.size());
    std::iota(order.begin(), order.end(), 0u);
    const MergedBody reference = withoutCallerNumbering(mergeParts(parts, seams).mBody);
    ASSERT_FALSE(reference.mPositions.empty());
    int permutations = 0;
    do
    {
        std::vector<PartMesh> shuffled;
        for (std::size_t i : order)
            shuffled.push_back(parts[i]);
        const MergeResult result = mergeParts(shuffled, seams);
        ASSERT_TRUE(result.ok()) << result.mErrors.front();
        EXPECT_TRUE(withoutCallerNumbering(result.mBody) == reference) << "permutation " << permutations;
        // The caller's numbering is mapped consistently: every vertex of every input part has an output vertex.
        ASSERT_EQ(result.mBody.mVertexMap.size(), shuffled.size());
        for (std::size_t p = 0; p < shuffled.size(); ++p)
        {
            ASSERT_EQ(result.mBody.mVertexMap[p].size(), shuffled[p].mPositions.size());
            for (std::uint32_t out : result.mBody.mVertexMap[p])
                EXPECT_LT(out, result.mBody.mPositions.size());
        }
        ++permutations;
    } while (std::next_permutation(order.begin(), order.end()));
    EXPECT_EQ(permutations, 24);
}

TEST(BodyMergeOrder, the_same_input_gives_the_same_bytes_twice)
{
    const std::vector<PartMesh> parts = { makeQuad("A", 1, 0.f, "b"), makeQuad("B", 2, 1.f, "c") };
    const MergedBody first = mergeParts(parts, oneSeam(1, 2)).mBody;
    const MergedBody second = mergeParts(parts, oneSeam(1, 2)).mBody;
    EXPECT_TRUE(first == second);
    ASSERT_FALSE(first.mPositions.empty());
    EXPECT_EQ(0,
        std::memcmp(first.mBoneWeights.data(), second.mBoneWeights.data(),
            first.mBoneWeights.size() * sizeof(first.mBoneWeights[0])));
}

// ----------------------------------------------------------------------------------------------------------------
// Hostile input
// ----------------------------------------------------------------------------------------------------------------

TEST(BodyMergeHostile, a_bad_part_is_refused_with_its_name_and_the_index_and_never_crashes)
{
    const auto expectRefused = [](const std::function<void(PartMesh&)>& spoil, const std::string& text) {
        PartMesh bad = makeQuad("Bad", 1, 0.f, "b");
        spoil(bad);
        const MergeResult result = mergeParts({ makeQuad("Good", 2, 5.f, "b"), bad }, oneSeam(1, 2));
        EXPECT_FALSE(result.ok()) << text;
        EXPECT_TRUE(result.mBody.mPositions.empty()) << text;
        EXPECT_TRUE(hasMessage(result, "part 'Bad'")) << text;
        EXPECT_TRUE(hasMessage(result, text))
            << text << " in: " << (result.mErrors.empty() ? "no message" : result.mErrors.front());
        const WeldMap map = computeWeldMap({ makeQuad("Good", 2, 5.f, "b"), bad }, oneSeam(1, 2));
        EXPECT_FALSE(map.mErrors.empty()) << text;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    expectRefused([&](PartMesh& p) { p.mPositions[2][1] = nan; }, "vertex 2: position is not finite");
    expectRefused([&](PartMesh& p) { p.mPositions[3][0] = inf; }, "vertex 3: position is not finite");
    expectRefused([&](PartMesh& p) { p.mNormals[1][2] = nan; }, "vertex 1: normal is not finite");
    expectRefused([&](PartMesh& p) { p.mUvs[0][0] = inf; }, "vertex 0: uv is not finite");
    expectRefused([&](PartMesh& p) { p.mTriangles[4] = 99; }, "triangle 1: index 99 out of range (4 vertices)");
    expectRefused([&](PartMesh& p) { p.mTriangles.pop_back(); }, "triangle list size 5 is not a multiple of 3");
    expectRefused([&](PartMesh& p) { p.mTriangles[1] = p.mTriangles[0]; }, "triangle 0: repeated vertex index");
    expectRefused([&](PartMesh& p) { p.mTriangles.clear(); }, "has no triangles");
    expectRefused([&](PartMesh& p) { p.mNormals.pop_back(); }, "normals are given for 3 vertices, expected 4");
    expectRefused([&](PartMesh& p) { p.mUvs.pop_back(); }, "uvs are given for 3 vertices, expected 4");
    expectRefused([&](PartMesh& p) { p.mRigidBone.clear(); }, "needs either a skin or a rigid bone");
    expectRefused([&](PartMesh& p) { p.mPlacement[5] = nan; }, "placement is not finite");
    expectRefused([&](PartMesh& p) { p.mPlacement = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 }; }, "placement is singular");
    expectRefused([&](PartMesh& p) { p.mPositions[0][0] = 2.0e7f; }, "coordinate");
    expectRefused([&](PartMesh& p) { p.mSlot = -1; }, "slot is negative");
    expectRefused(
        [&](PartMesh& p) {
            p.mName = "Bad";
            p.mRigidBone = std::string(500, 'x');
        },
        "name is longer");
}

TEST(BodyMergeHostile, a_bad_skin_is_refused)
{
    const auto expectRefused = [](const std::function<void(PartMesh&)>& spoil, const std::string& text) {
        PartMesh bad = makeTriangle("Bad", 1, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, "");
        for (std::size_t v = 0; v < 3; ++v)
            setSkin(bad, { "x", "y" }, v, { { 0, 0.5f }, { 1, 0.5f } });
        spoil(bad);
        const MergeResult result = mergeParts({ bad }, {});
        EXPECT_FALSE(result.ok()) << text;
        EXPECT_TRUE(hasMessage(result, "part 'Bad'")) << text;
        EXPECT_TRUE(hasMessage(result, text)) << text;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    expectRefused([&](PartMesh& p) { p.mSkin[1].mInfluences[0].mWeight = -0.1f; }, "vertex 1: influence 0: weight");
    expectRefused([&](PartMesh& p) { p.mSkin[2].mInfluences[1].mWeight = nan; }, "vertex 2: influence 1: weight");
    expectRefused([&](PartMesh& p) { p.mSkin[0].mInfluences[1].mBone = 7; },
        "vertex 0: influence 1: bone index 7 out of range (2 bones)");
    expectRefused([&](PartMesh& p) { p.mSkin[0].mCount = 9; }, "vertex 0: more than 8 influences");
    expectRefused([&](PartMesh& p) { p.mSkin[1].mCount = 0; }, "vertex 1: no influence with a positive weight");
    expectRefused(
        [&](PartMesh& p) {
            p.mSkin[1].mInfluences[0].mWeight = 0.f;
            p.mSkin[1].mInfluences[1].mWeight = 0.f;
        },
        "vertex 1: no influence with a positive weight");
    expectRefused([&](PartMesh& p) { p.mSkin.pop_back(); }, "skin has 2 entries, expected 3 vertices");
    expectRefused([&](PartMesh& p) { p.mRigidBone = "x"; }, "has both a skin and a rigid bone");
    expectRefused([&](PartMesh& p) { p.mBones.assign(2000, "x"); }, "too many bones");
}

TEST(BodyMergeHostile, bad_seams_and_mixed_attributes_are_refused)
{
    const std::vector<PartMesh> parts = { makeQuad("A", 1, 0.f, "b"), makeQuad("B", 2, 1.f, "b") };
    EXPECT_TRUE(hasMessage(mergeParts(parts, oneSeam(1, 2, -0.1)), "seam 0: tolerance"));
    EXPECT_TRUE(
        hasMessage(mergeParts(parts, oneSeam(1, 2, std::numeric_limits<double>::quiet_NaN())), "seam 0: tolerance"));
    EXPECT_TRUE(hasMessage(mergeParts(parts, oneSeam(1, 9)), "seam 0: slot 9 has no part"));
    EXPECT_TRUE(hasMessage(mergeParts(parts, { { 1, 2, 0.01 }, { 7, 2, 0.01 } }), "seam 1: slot 7 has no part"));

    std::vector<PartMesh> mixed = parts;
    mixed[1].mNormals.clear();
    EXPECT_TRUE(hasMessage(mergeParts(mixed, {}), "part 'B' has no normals"));
    mixed = parts;
    mixed[0].mUvs.clear();
    EXPECT_TRUE(hasMessage(mergeParts(mixed, {}), "part 'A' has no uvs"));
}

TEST(BodyMergeHostile, the_limits_are_enforced_and_the_error_list_is_capped)
{
    const std::vector<PartMesh> parts = { makeQuad("A", 1, 0.f, "b"), makeQuad("B", 2, 1.f, "b") };
    MergeLimits limits;
    limits.mMaxParts = 1;
    EXPECT_TRUE(hasMessage(mergeParts(parts, {}, {}, limits), "too many parts (2, limit 1)"));
    limits = {};
    limits.mMaxVerticesPerPart = 3;
    EXPECT_TRUE(hasMessage(mergeParts(parts, {}, {}, limits), "part 'A' (slot 1): too many vertices (4, limit 3)"));
    limits = {};
    limits.mMaxTotalVertices = 6;
    EXPECT_TRUE(hasMessage(mergeParts(parts, {}, {}, limits), "too many vertices in all parts (8, limit 6)"));
    limits = {};
    limits.mMaxTrianglesPerPart = 1;
    EXPECT_TRUE(hasMessage(mergeParts(parts, {}, {}, limits), "too many triangles (2, limit 1)"));
    limits = {};
    limits.mMaxSeams = 0;
    EXPECT_TRUE(hasMessage(mergeParts(parts, oneSeam(1, 2), {}, limits), "too many seams (1, limit 0)"));

    std::vector<PartMesh> many;
    for (int i = 0; i < 40; ++i)
    {
        PartMesh bad = makeQuad("bad" + std::to_string(i), i, static_cast<float>(i) * 2.f, "b");
        bad.mPositions[0][0] = std::numeric_limits<float>::quiet_NaN();
        many.push_back(bad);
    }
    limits = {};
    limits.mMaxErrors = 3;
    const MergeResult capped = mergeParts(many, {}, {}, limits);
    EXPECT_FALSE(capped.ok());
    EXPECT_LE(capped.mErrors.size(), 3u);
}

TEST(BodyMergeHostile, an_empty_input_is_refused_and_an_empty_seam_list_just_concatenates)
{
    EXPECT_TRUE(hasMessage(mergeParts({}, {}), "no parts"));
    const MergeResult result = mergeParts({ makeQuad("A", 1, 0.f, "b"), makeQuad("B", 2, 1.f, "b") }, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.mBody.mPositions.size(), 8u);
    EXPECT_EQ(result.mBody.mReport.mWeldSets, 0u);
}

// ----------------------------------------------------------------------------------------------------------------
// Properties on random meshes
// ----------------------------------------------------------------------------------------------------------------

namespace
{
    struct Bones
    {
        std::vector<std::string> mNames;
        std::vector<Mat34> mDelta; // bind space to posed space, per bone
    };

    Bones randomPose(std::mt19937& rng, int count)
    {
        std::uniform_real_distribution<double> angle(-0.6, 0.6);
        std::uniform_real_distribution<double> shift(-1.5, 1.5);
        Bones bones;
        for (int i = 0; i < count; ++i)
        {
            bones.mNames.push_back("bone" + std::to_string(i));
            const double a = angle(rng);
            const double c = std::cos(a);
            const double s = std::sin(a);
            // A rotation about z by a, then a shift.
            bones.mDelta.push_back({ c, -s, 0, shift(rng), s, c, 0, shift(rng), 0, 0, 1, shift(rng) });
        }
        return bones;
    }

    /// The weights of a vertex are a smooth function of its position, so two parts that meet exactly agree. With
    /// `partBias` they disagree by about that much.
    PartVertexSkin skinAt(const Vec3& p, int boneCount, int partNumber, float partBias)
    {
        std::vector<std::pair<float, std::uint32_t>> raw;
        for (int b = 0; b < boneCount; ++b)
        {
            const float w = 0.2f + 0.8f * std::abs(std::sin(0.7f * p[0] + 0.45f * p[1] + 1.3f * static_cast<float>(b)))
                + partBias * static_cast<float>(((partNumber + b) % 3) == 0 ? 1 : 0);
            raw.emplace_back(w, static_cast<std::uint32_t>(b));
        }
        std::stable_sort(raw.begin(), raw.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
        PartVertexSkin skin;
        skin.mCount = 4;
        float sum = 0.f;
        for (int i = 0; i < 4; ++i)
            sum += raw[static_cast<std::size_t>(i)].first;
        for (int i = 0; i < 4; ++i)
        {
            skin.mInfluences[static_cast<std::size_t>(i)]
                = { raw[static_cast<std::size_t>(i)].second, raw[static_cast<std::size_t>(i)].first / sum };
        }
        return skin;
    }

    std::vector<PartMesh> randomBody(int boneCount, float partBias)
    {
        std::vector<PartMesh> parts;
        for (int i = 0; i < 4; ++i)
        {
            PartMesh part
                = makeGrid("patch" + std::to_string(i), i, "skin", static_cast<float>(i) * 6.f, 0.f, 6, 6, 1.f, "");
            part.mRigidBone.clear();
            for (int b = 0; b < boneCount; ++b)
                part.mBones.push_back("bone" + std::to_string(b));
            part.mSkin.reserve(part.mPositions.size());
            for (const Vec3& p : part.mPositions)
                part.mSkin.push_back(skinAt(p, boneCount, i, partBias));
            parts.push_back(part);
        }
        return parts;
    }

    Vec3 skinned(const Bones& bones, const std::vector<std::pair<std::string, float>>& weights, const Vec3& p)
    {
        double x = 0, y = 0, z = 0;
        for (const auto& [name, w] : weights)
        {
            const std::size_t b = static_cast<std::size_t>(std::stoi(name.substr(4)));
            const Vec3 q = applyMat(bones.mDelta[b], p);
            x += static_cast<double>(w) * q[0];
            y += static_cast<double>(w) * q[1];
            z += static_cast<double>(w) * q[2];
        }
        return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
    }

    std::vector<std::pair<std::string, float>> sourceWeights(const PartMesh& part, std::size_t vertex)
    {
        std::vector<std::pair<std::string, float>> weights;
        const PartVertexSkin& skin = part.mSkin[vertex];
        for (std::size_t i = 0; i < skin.mCount; ++i)
            weights.emplace_back(part.mBones[skin.mInfluences[i].mBone], skin.mInfluences[i].mWeight);
        return weights;
    }

    std::vector<std::pair<std::string, float>> mergedWeights(const MergedBody& body, std::size_t vertex)
    {
        std::vector<std::pair<std::string, float>> weights;
        for (std::size_t i = 0; i < maxMergedInfluences; ++i)
        {
            if (body.mBoneWeights[vertex][i] > 0.f)
                weights.emplace_back(body.mBones[body.mBoneIndices[vertex][i]], body.mBoneWeights[vertex][i]);
        }
        return weights;
    }

    double distanceBetween(const Vec3& a, const Vec3& b)
    {
        return std::sqrt(static_cast<double>(
            (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])));
    }
}

TEST(BodyMergeProperty, skinning_the_merged_body_equals_skinning_each_part_when_seam_weights_agree)
{
    std::mt19937 rng(20261103);
    const std::vector<SeamDeclaration> seams = { { 0, 1, 0.0 }, { 1, 2, 0.0 }, { 2, 3, 0.0 } };
    for (int trial = 0; trial < 8; ++trial)
    {
        const std::vector<PartMesh> parts = randomBody(5, 0.f);
        const MergeResult result = mergeParts(parts, seams);
        ASSERT_TRUE(result.ok()) << result.mErrors.front();
        const Bones bones = randomPose(rng, 5);
        double worst = 0;
        for (std::size_t p = 0; p < parts.size(); ++p)
        {
            for (std::size_t v = 0; v < parts[p].mPositions.size(); ++v)
            {
                const std::uint32_t out = result.mBody.mVertexMap[p][v];
                const Vec3 expected = skinned(bones, sourceWeights(parts[p], v), parts[p].mPositions[v]);
                const Vec3 actual = skinned(bones, mergedWeights(result.mBody, out), result.mBody.mPositions[out]);
                worst = std::max(worst, distanceBetween(expected, actual));
            }
        }
        EXPECT_LT(worst, 1e-4) << "trial " << trial;
        EXPECT_EQ(result.mBody.mReport.mMaxWeightDisagreement, 0.0);
    }
}

TEST(BodyMergeProperty, where_seam_weights_disagree_the_difference_is_bounded_by_the_disagreement)
{
    std::mt19937 rng(7);
    const std::vector<SeamDeclaration> seams = { { 0, 1, 0.0 }, { 1, 2, 0.0 }, { 2, 3, 0.0 } };
    const std::vector<PartMesh> parts = randomBody(5, 0.35f);
    const MergeResult result = mergeParts(parts, seams);
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    EXPECT_GT(result.mBody.mReport.mMaxWeightDisagreement, 0.0);
    const Bones bones = randomPose(rng, 5);
    std::size_t seamVertices = 0;
    for (std::size_t p = 0; p < parts.size(); ++p)
    {
        for (std::size_t v = 0; v < parts[p].mPositions.size(); ++v)
        {
            const std::uint32_t out = result.mBody.mVertexMap[p][v];
            const auto source = sourceWeights(parts[p], v);
            const auto merged = mergedWeights(result.mBody, out);
            const Vec3 expected = skinned(bones, source, parts[p].mPositions[v]);
            const Vec3 actual = skinned(bones, merged, result.mBody.mPositions[out]);
            // |sum (wm - ws) D p| <= sum |wm - ws| max |D p - p|, because both weight sets sum to 1.
            std::map<std::string, float> delta;
            for (const auto& [name, w] : merged)
                delta[name] += w;
            for (const auto& [name, w] : source)
                delta[name] -= w;
            double sumDelta = 0;
            for (const auto& entry : delta)
                sumDelta += std::abs(entry.second);
            double maxMove = 0;
            for (const Mat34& m : bones.mDelta)
                maxMove = std::max(
                    maxMove, distanceBetween(applyMat(m, result.mBody.mPositions[out]), result.mBody.mPositions[out]));
            EXPECT_LE(distanceBetween(expected, actual), sumDelta * maxMove + 1e-4);
            if (sumDelta > 1e-6)
                ++seamVertices;
        }
    }
    EXPECT_GT(seamVertices, 0u); // the test saw seams that changed
}

TEST(BodyMergeProperty, no_vertex_is_lost_no_triangle_changes_and_every_weight_set_sums_to_one)
{
    const std::vector<SeamDeclaration> seams = { { 0, 1, 0.0 }, { 1, 2, 0.0 }, { 2, 3, 0.0 } };
    const std::vector<PartMesh> parts = randomBody(6, 0.2f);
    const MergeResult result = mergeParts(parts, seams);
    ASSERT_TRUE(result.ok()) << result.mErrors.front();
    const MergedBody& body = result.mBody;
    std::size_t inputVertices = 0;
    std::size_t inputTriangles = 0;
    for (const PartMesh& part : parts)
    {
        inputVertices += part.mPositions.size();
        inputTriangles += part.mTriangles.size() / 3;
    }
    EXPECT_EQ(body.mReport.mInputVertices, inputVertices);
    EXPECT_EQ(body.mPositions.size() + body.mReport.mCollapsedVertices, inputVertices);
    EXPECT_EQ(body.mTriangles.size() / 3 + body.mReport.mDroppedTriangles, inputTriangles);
    // Every source triangle appears with its corners mapped: the triangles of a part, in source order, are one
    // contiguous run in the output (parts here have distinct slots).
    for (std::size_t p = 0; p < parts.size(); ++p)
    {
        const RegionRange& region = body.mRegions[p];
        ASSERT_EQ(region.mSlot, static_cast<int>(p));
        ASSERT_EQ(region.mTriangleCount, parts[p].mTriangles.size() / 3);
        for (std::size_t t = 0; t < region.mTriangleCount; ++t)
        {
            for (std::size_t k = 0; k < 3; ++k)
            {
                EXPECT_EQ(body.mTriangles[(region.mFirstTriangle + t) * 3 + k],
                    body.mVertexMap[p][parts[p].mTriangles[t * 3 + k]]);
            }
        }
    }
    for (std::size_t v = 0; v < body.mPositions.size(); ++v)
    {
        float sum = 0.f;
        for (float w : body.mBoneWeights[v])
        {
            EXPECT_GE(w, 0.f);
            sum += w;
        }
        EXPECT_NEAR(sum, 1.f, 1e-6) << "vertex " << v;
    }
    for (const auto& indices : body.mBoneIndices)
    {
        for (std::uint16_t i : indices)
            EXPECT_LT(i, body.mBones.size());
    }
}

// ----------------------------------------------------------------------------------------------------------------
// Review fixes of stage b (task 0069): order independence for skin and negative zero, placement and coordinate
// range, transitive welds, the work budget of the lookup.
// ----------------------------------------------------------------------------------------------------------------

TEST(BodyMergeReview, two_parts_that_differ_only_in_skin_come_out_the_same_whatever_the_order)
{
    PartMesh a = makeTriangle("shape", 3, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, "x");
    PartMesh b = a;
    const std::vector<std::string> bones = { "boneA", "boneB" };
    for (std::size_t v = 0; v < 3; ++v)
    {
        setSkin(a, bones, v, { { 0, 0.75f }, { 1, 0.25f } });
        setSkin(b, bones, v, { { 0, 0.25f }, { 1, 0.75f } });
    }
    const MergeResult forward = mergeParts({ a, b }, {});
    const MergeResult backward = mergeParts({ b, a }, {});
    ASSERT_TRUE(forward.ok());
    ASSERT_TRUE(backward.ok());
    EXPECT_TRUE(withoutCallerNumbering(forward.mBody) == withoutCallerNumbering(backward.mBody));
}

TEST(BodyMergeReview, negative_zero_and_positive_zero_do_not_make_the_result_depend_on_the_order)
{
    PartMesh a = makeTriangle("shape", 3, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, "x");
    a.mUvs = { { 0.f, 0.f }, { 1.f, 0.f }, { 0.f, 1.f } };
    a.mNormals = { { 0.f, 0.f, 1.f }, { 0.f, 0.f, 1.f }, { 0.f, 0.f, 1.f } };
    PartMesh b = a;
    b.mUvs[0] = { -0.f, -0.f };
    b.mNormals[0] = { -0.f, -0.f, 1.f };
    const MergeResult forward = mergeParts({ a, b }, {});
    const MergeResult backward = mergeParts({ b, a }, {});
    ASSERT_TRUE(forward.ok());
    ASSERT_TRUE(backward.ok());
    EXPECT_TRUE(withoutCallerNumbering(forward.mBody) == withoutCallerNumbering(backward.mBody));
    // And the bytes carry no negative zero at all.
    for (const MergeResult* result : { &forward, &backward })
    {
        for (const Vec2& uv : result->mBody.mUvs)
        {
            EXPECT_FALSE(std::signbit(uv[0]) && uv[0] == 0.f);
            EXPECT_FALSE(std::signbit(uv[1]) && uv[1] == 0.f);
        }
        for (const Vec3& n : result->mBody.mNormals)
        {
            for (const float c : n)
                EXPECT_FALSE(std::signbit(c) && c == 0.f);
        }
    }
}

TEST(BodyMergeReview, a_huge_or_absurd_placement_is_refused_before_any_cast)
{
    const auto refused = [](const Mat34& placement, const char* expect) {
        PartMesh part = makeTriangle("shape", 3, { 1.f, 2.f, 3.f }, { 2.f, 2.f, 3.f }, { 1.f, 3.f, 3.f }, "x");
        part.mPlacement = placement;
        const MergeResult result = mergeParts({ part }, {});
        EXPECT_FALSE(result.ok()) << expect;
        if (!result.ok())
        {
            EXPECT_TRUE(hasMessage(result, "placement")) << expect << ": " << result.mErrors.front();
        }
        EXPECT_TRUE(result.mBody.mPositions.empty());
    };
    Mat34 translation = identityPlacement;
    translation[3] = 1.0e30;
    refused(translation, "translation 1e30");
    Mat34 scale = identityPlacement;
    scale[0] = 1.0e30;
    refused(scale, "scale 1e30");
    Mat34 beyondLimit = identityPlacement;
    beyondLimit[7] = 2.0 * maxCoordinate; // finite, but the placed vertices would be beyond maxCoordinate
    refused(beyondLimit, "translation twice the coordinate limit");
    Mat34 grows = identityPlacement;
    grows[0] = grows[5] = grows[10] = maxPlacementEntry; // a vertex at (1,2,3) lands at 3e6: fine; at 1e7 it would not
    PartMesh edge = makeTriangle("far", 3, { 9.0e6f, 0.f, 0.f }, { 9.0e6f, 1.f, 0.f }, { 9.0e6f, 0.f, 1.f }, "x");
    edge.mPlacement = grows;
    const MergeResult tooFar = mergeParts({ edge }, {});
    EXPECT_FALSE(tooFar.ok());
    EXPECT_TRUE(hasMessage(tooFar, "placed"));
}

TEST(BodyMergeReview, a_tiny_tolerance_at_large_coordinates_welds_exact_coincidence_and_never_overflows_the_grid)
{
    PartMesh a = makeTriangle("a", 1, { 9.0e6f, 0.f, 0.f }, { 9.0e6f, 1.f, 0.f }, { 9.0e6f, 0.f, 1.f }, "x");
    PartMesh b = makeTriangle("b", 2, { 9.0e6f, 0.f, 0.f }, { 9.0e6f, -1.f, 0.f }, { 9.0e6f, 0.f, -1.f }, "y");
    for (const double tolerance : { 0.0, 1.0e-9, 1.0e-6 })
    {
        const MergeResult result = mergeParts({ a, b }, oneSeam(1, 2, tolerance));
        ASSERT_TRUE(result.ok()) << result.mErrors.front();
        EXPECT_EQ(result.mBody.mReport.mWeldSets, 1u) << "tolerance " << tolerance;
    }
}

TEST(BodyMergeReview, a_weld_set_never_reaches_further_than_the_tolerance_from_its_first_member)
{
    // a1 at 0, b at 0.006 and a2 at 0.012: b is within 0.01 of both, a1 and a2 are not within 0.01 of each other.
    // The old union-find chained all three into one set and moved a2 by 0.012, more than the tolerance.
    PartMesh a = makeTriangle("a", 1, { 0.f, 0.f, 0.f }, { 0.012f, 0.f, 0.f }, { 0.f, 5.f, 0.f }, "x");
    PartMesh b = makeTriangle("b", 2, { 0.006f, 0.f, 0.f }, { 0.006f, 9.f, 0.f }, { 3.f, 9.f, 0.f }, "y");
    const WeldMap map = computeWeldMap({ a, b }, oneSeam(1, 2, 0.01));
    ASSERT_TRUE(map.mErrors.empty());
    ASSERT_EQ(map.mSets.size(), 1u);
    EXPECT_EQ(map.mSets[0].size(), 2u); // a's first vertex and b's first vertex
    for (const WeldMember& member : map.mSets[0])
        EXPECT_FALSE(member.mPart == 0 && member.mVertex == 1) << "the far end of the chain was welded";

    const MergeResult merged = mergeParts({ a, b }, oneSeam(1, 2, 0.01));
    ASSERT_TRUE(merged.ok());
    // No vertex of a stays more than the tolerance from where it was.
    for (std::size_t v = 0; v < a.mPositions.size(); ++v)
    {
        const Vec3& out = merged.mBody.mPositions[merged.mBody.mVertexMap[0][v]];
        const double dx = out[0] - a.mPositions[v][0];
        const double dy = out[1] - a.mPositions[v][1];
        const double dz = out[2] - a.mPositions[v][2];
        EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz), 0.01 + 1e-6) << "vertex " << v;
    }
}

TEST(BodyMergeReview, many_coincident_vertices_at_tolerance_zero_hit_the_work_budget_not_a_quadratic_scan)
{
    const auto cloud = [](const std::string& name, int slot, std::size_t count) {
        PartMesh part;
        part.mName = name;
        part.mSlot = slot;
        part.mMaterial = "mat";
        part.mRigidBone = "x";
        for (std::size_t i = 0; i < count; ++i)
            part.mPositions.push_back({ 1.f, 1.f, 1.f });
        for (std::size_t i = 0; i + 2 < count; i += 3)
            part.mTriangles.insert(part.mTriangles.end(),
                { static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i + 1),
                    static_cast<std::uint32_t>(i + 2) });
        return part;
    };
    MergeLimits limits;
    limits.mMaxCandidateChecks = 100000;
    limits.mMaxPairsPerSeam = 1000000000; // the pair cap alone would not stop it: the work budget must
    const MergeResult result = mergeParts({ cloud("a", 1, 3000), cloud("b", 2, 3000) }, oneSeam(1, 2, 0.0), {}, limits);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(hasMessage(result, "seam 0")) << (result.ok() ? "no error" : result.mErrors.front());
    EXPECT_TRUE(hasMessage(result, "comparisons"));
    // A small cloud inside the budget still welds.
    const MergeResult small = mergeParts({ cloud("a", 1, 30), cloud("b", 2, 30) }, oneSeam(1, 2, 0.0), {}, limits);
    EXPECT_TRUE(small.ok());
}
