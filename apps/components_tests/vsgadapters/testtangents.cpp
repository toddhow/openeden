// Tests for vsgAdapters::generateTangents (task 0012, Step 1 (V4h), "Tangents"), the port of
// OSG 3.6.5's osgUtil::TangentSpaceGenerator::compute and the end of generate.
//
// Written from the spec alone, with expected values derived by hand from OSG 3.6.5's
// TangentSpaceGenerator.cpp. Assumptions and ambiguities:
// - Namespace vsgAdapters; signature generateTangents(vertices, normals, texCoords,
//   indexLists, strip) with indexLists a std::vector<std::vector<uint16_t>>.
// - "No normals" means an empty normals vector. "Mismatched sizes" therefore means a
//   non-empty normals vector whose size differs from vertices, or texCoords whose size
//   differs from vertices; both give an empty result.
// - The result has one tangent per vertex (vertices.size()), also when no index list
//   forms a triangle (every vertex is then (0, 0, 0, 1)).
// - With per-vertex normals the winding of a triangle does not change the result (the
//   per-axis increments are ratios of the same cross product, which only changes sign).
//   Without normals the face normal flips with the winding, and so does w; the odd-window
//   swap of strips is tested that way.
// - Unnormalised face normals weight triangles by area in OSG; the tests avoid depending on
//   that weighting (single triangles or equal-area faces when normals are empty).
// - Only out-of-range indices that are part of a triangle are tested; whether an
//   out-of-range index in an ignored trailing partial triple fails is not pinned.

#include <components/vsgadapters/tangents.hpp>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    using Lists = std::vector<std::vector<uint16_t>>;

    constexpr float tolerance = 1e-5f;
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);

    void expectTangent(const vsg::vec4& actual, float x, float y, float z, float w)
    {
        EXPECT_NEAR(actual.x, x, tolerance);
        EXPECT_NEAR(actual.y, y, tolerance);
        EXPECT_NEAR(actual.z, z, tolerance);
        EXPECT_NEAR(actual.w, w, tolerance);
    }

    std::vector<vsg::vec3> upNormals(size_t count)
    {
        return std::vector<vsg::vec3>(count, vsg::vec3(0.0f, 0.0f, 1.0f));
    }

    // Unit quad in the XY plane: 0 (0,0), 1 (1,0), 2 (1,1), 3 (0,1).
    std::vector<vsg::vec3> quad()
    {
        return { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
    }

    std::vector<vsg::vec2> uvXY(const std::vector<vsg::vec3>& vertices, float scale = 1.0f)
    {
        std::vector<vsg::vec2> uvs;
        for (const auto& v : vertices)
            uvs.emplace_back(v.x * scale, v.y * scale);
        return uvs;
    }

    const Lists quadTriangles{ { 0, 1, 2, 0, 2, 3 } };

    // ---- Basic orientation ---------------------------------------------------------------------

    TEST(VsgAdaptersTangentsTest, uvAlongXYGivesTangentX)
    {
        const auto vertices = quad();
        const auto result
            = vsgAdapters::generateTangents(vertices, upNormals(4), uvXY(vertices), quadTriangles, false);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, 1, 0, 0, 1);
    }

    TEST(VsgAdaptersTangentsTest, resultIsNormalisedWhateverTheUvScale)
    {
        const auto vertices = quad();
        const auto result
            = vsgAdapters::generateTangents(vertices, upNormals(4), uvXY(vertices, 3.0f), quadTriangles, false);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, 1, 0, 0, 1);
    }

    TEST(VsgAdaptersTangentsTest, mirroredUFlipsTangentAndHandedness)
    {
        // u = -x, v = y: T = (-1, 0, 0), B = (0, 1, 0), (T x B).N < 0.
        const auto vertices = quad();
        std::vector<vsg::vec2> uvs;
        for (const auto& v : vertices)
            uvs.emplace_back(-v.x, v.y);
        const auto result = vsgAdapters::generateTangents(vertices, upNormals(4), uvs, quadTriangles, false);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, -1, 0, 0, -1);
    }

    TEST(VsgAdaptersTangentsTest, uvRotatedNinetyDegrees)
    {
        // u = y, v = -x: T = (0, 1, 0), B = (-1, 0, 0), (T x B) = +Z.
        const auto vertices = quad();
        std::vector<vsg::vec2> uvs;
        for (const auto& v : vertices)
            uvs.emplace_back(v.y, -v.x);
        const auto result = vsgAdapters::generateTangents(vertices, upNormals(4), uvs, quadTriangles, false);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, 0, 1, 0, 1);
    }

    TEST(VsgAdaptersTangentsTest, tangentIsProjectedOntoThePlaneOfTheVertexNormal)
    {
        // N = (1, 0, 1)/sqrt2: (N x T) x N = T - (T.N)N = (0.5, 0, -0.5); B stays (0, 1, 0).
        const auto vertices = quad();
        const std::vector<vsg::vec3> normals(4, vsg::vec3(invSqrt2, 0.0f, invSqrt2));
        const auto result = vsgAdapters::generateTangents(vertices, normals, uvXY(vertices), quadTriangles, false);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, invSqrt2, 0, -invSqrt2, 1);
    }

    // ---- No normals: the face normal -----------------------------------------------------------

    TEST(VsgAdaptersTangentsTest, noNormalsUsesTheFaceNormal)
    {
        // Counter-clockwise seen from +Z: face normal +Z, so w = +1.
        const auto vertices = quad();
        const auto result = vsgAdapters::generateTangents(vertices, {}, uvXY(vertices), quadTriangles, false);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, 1, 0, 0, 1);
    }

    TEST(VsgAdaptersTangentsTest, noNormalsWithClockwiseWindingFlipsHandedness)
    {
        // Clockwise seen from +Z: face normal -Z. T and B are unchanged, (T x B).N < 0.
        const auto vertices = quad();
        const Lists clockwise{ { 0, 2, 1, 0, 3, 2 } };
        const auto result = vsgAdapters::generateTangents(vertices, {}, uvXY(vertices), clockwise, false);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, 1, 0, 0, -1);
    }

    TEST(VsgAdaptersTangentsTest, windingDoesNotMatterWithNormals)
    {
        const auto vertices = quad();
        const Lists clockwise{ { 0, 2, 1, 0, 3, 2 } };
        const auto result = vsgAdapters::generateTangents(vertices, upNormals(4), uvXY(vertices), clockwise, false);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, 1, 0, 0, 1);
    }

    // ---- Accumulation over shared vertices and lists -------------------------------------------

    TEST(VsgAdaptersTangentsTest, sharedVertexAveragesAcrossTrianglesInDifferentLists)
    {
        // Triangle A (0, 1, 2) with uv = xy: T = +X, B = +Y.
        // Triangle B (0, 3, 4) with u = y, v = -x: T = +Y, B = -X.
        const std::vector<vsg::vec3> vertices{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 2, 0, 0 } };
        const std::vector<vsg::vec2> uvs{ { 0, 0 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -2 } };
        const auto result
            = vsgAdapters::generateTangents(vertices, upNormals(5), uvs, Lists{ { 0, 1, 2 }, { 0, 3, 4 } }, false);
        ASSERT_EQ(result.size(), 5u);
        expectTangent(result[0], invSqrt2, invSqrt2, 0, 1);
        expectTangent(result[1], 1, 0, 0, 1);
        expectTangent(result[2], 1, 0, 0, 1);
        expectTangent(result[3], 0, 1, 0, 1);
        expectTangent(result[4], 0, 1, 0, 1);
    }

    TEST(VsgAdaptersTangentsTest, sameTrianglesInOneListOrTwoGiveTheSameResult)
    {
        const std::vector<vsg::vec3> vertices{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 2, 0, 0 } };
        const std::vector<vsg::vec2> uvs{ { 0, 0 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -2 } };
        const auto one = vsgAdapters::generateTangents(vertices, upNormals(5), uvs, Lists{ { 0, 1, 2, 0, 3, 4 } }, false);
        const auto two
            = vsgAdapters::generateTangents(vertices, upNormals(5), uvs, Lists{ { 0, 1, 2 }, { 0, 3, 4 } }, false);
        ASSERT_EQ(one.size(), 5u);
        ASSERT_EQ(two.size(), 5u);
        for (size_t i = 0; i < 5; ++i)
            expectTangent(two[i], one[i].x, one[i].y, one[i].z, one[i].w);
    }

    TEST(VsgAdaptersTangentsTest, vertexInNoTriangleIsZeroWithPositiveW)
    {
        auto vertices = quad();
        vertices.emplace_back(5.0f, 5.0f, 5.0f);
        const auto result = vsgAdapters::generateTangents(
            vertices, upNormals(5), uvXY(vertices), Lists{ { 0, 1, 2 } }, false);
        ASSERT_EQ(result.size(), 5u);
        expectTangent(result[0], 1, 0, 0, 1);
        expectTangent(result[1], 1, 0, 0, 1);
        expectTangent(result[2], 1, 0, 0, 1);
        expectTangent(result[3], 0, 0, 0, 1);
        expectTangent(result[4], 0, 0, 0, 1);
    }

    // ---- Triangle lists ------------------------------------------------------------------------

    TEST(VsgAdaptersTangentsTest, trailingPartialTripleIsIgnored)
    {
        const auto vertices = quad();
        const auto result
            = vsgAdapters::generateTangents(vertices, upNormals(4), uvXY(vertices), Lists{ { 0, 1, 2, 3 } }, false);
        ASSERT_EQ(result.size(), 4u);
        expectTangent(result[0], 1, 0, 0, 1);
        expectTangent(result[1], 1, 0, 0, 1);
        expectTangent(result[2], 1, 0, 0, 1);
        expectTangent(result[3], 0, 0, 0, 1);

        const auto two
            = vsgAdapters::generateTangents(vertices, upNormals(4), uvXY(vertices), Lists{ { 0, 1, 2, 3, 0 } }, false);
        ASSERT_EQ(two.size(), 4u);
        expectTangent(two[3], 0, 0, 0, 1);
    }

    TEST(VsgAdaptersTangentsTest, noTrianglesGivesZeroTangents)
    {
        const auto vertices = quad();
        for (const Lists& lists : { Lists{}, Lists{ {} }, Lists{ { 0, 1 } } })
        {
            const auto result = vsgAdapters::generateTangents(vertices, upNormals(4), uvXY(vertices), lists, false);
            ASSERT_EQ(result.size(), 4u);
            for (const auto& t : result)
                expectTangent(t, 0, 0, 0, 1);
        }
    }

    // ---- Strips --------------------------------------------------------------------------------

    // Strip quad: 0 (0,0), 1 (1,0), 2 (0,1), 3 (1,1); windows (0,1,2) and (2,1,3), both
    // counter-clockwise seen from +Z.
    std::vector<vsg::vec3> stripQuad()
    {
        return { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 } };
    }

    TEST(VsgAdaptersTangentsTest, stripCoversEveryWindow)
    {
        // Vertex 3 is only in the second window.
        const auto vertices = stripQuad();
        const auto result
            = vsgAdapters::generateTangents(vertices, upNormals(4), uvXY(vertices), Lists{ { 0, 1, 2, 3 } }, true);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, 1, 0, 0, 1);
    }

    TEST(VsgAdaptersTangentsTest, stripOddWindowSwapMattersWithoutNormals)
    {
        // Window 1 is (2, 1, 3): face normal +Z. Unswapped (1, 2, 3) would give -Z, so
        // vertex 3 (only in window 1) would get w = -1.
        const auto vertices = stripQuad();
        const auto result = vsgAdapters::generateTangents(vertices, {}, uvXY(vertices), Lists{ { 0, 1, 2, 3 } }, true);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, 1, 0, 0, 1);
    }

    TEST(VsgAdaptersTangentsTest, stripMatchesTheEquivalentTriangleList)
    {
        // Non-planar, irregular geometry without normals, so face normals and their winding
        // matter. Strip (0,1,2,3,4) = triangles (0,1,2), (2,1,3), (2,3,4).
        const std::vector<vsg::vec3> vertices{
            { 0, 0, 0 }, { 1, 0, 0.3f }, { 0.2f, 1, 0.1f }, { 1.1f, 1.2f, -0.2f }, { 0.3f, 2, 0.4f } };
        const std::vector<vsg::vec2> uvs{ { 0, 0 }, { 1, 0.1f }, { 0.1f, 1 }, { 1.2f, 0.9f }, { 0.2f, 2.1f } };
        const auto strip = vsgAdapters::generateTangents(vertices, {}, uvs, Lists{ { 0, 1, 2, 3, 4 } }, true);
        const auto list
            = vsgAdapters::generateTangents(vertices, {}, uvs, Lists{ { 0, 1, 2, 2, 1, 3, 2, 3, 4 } }, false);
        ASSERT_EQ(strip.size(), 5u);
        ASSERT_EQ(list.size(), 5u);
        for (size_t i = 0; i < 5; ++i)
        {
            const float length = std::sqrt(strip[i].x * strip[i].x + strip[i].y * strip[i].y + strip[i].z * strip[i].z);
            EXPECT_NEAR(length, 1.0f, tolerance);
            expectTangent(strip[i], list[i].x, list[i].y, list[i].z, list[i].w);
        }
    }

    TEST(VsgAdaptersTangentsTest, eachListIsASeparateStrip)
    {
        // Strip A (0,1,2,3) with uv = xy; strip B (4,5,6) with u = -x. Joined into one strip,
        // the bridging windows (2,3,4) and (3,4,5) would change vertices 2 to 5.
        auto vertices = stripQuad();
        vertices.insert(vertices.end(), { { 2, 0, 0 }, { 3, 0, 0 }, { 2, 1, 0 } });
        const std::vector<vsg::vec2> uvs{ { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { -2, 0 }, { -3, 0 }, { -2, 1 } };
        const auto result
            = vsgAdapters::generateTangents(vertices, upNormals(7), uvs, Lists{ { 0, 1, 2, 3 }, { 4, 5, 6 } }, true);
        ASSERT_EQ(result.size(), 7u);
        for (size_t i = 0; i < 4; ++i)
            expectTangent(result[i], 1, 0, 0, 1);
        for (size_t i = 4; i < 7; ++i)
            expectTangent(result[i], -1, 0, 0, -1);
    }

    TEST(VsgAdaptersTangentsTest, stripShorterThanThreeIndicesHasNoTriangles)
    {
        const auto vertices = stripQuad();
        const auto result = vsgAdapters::generateTangents(
            vertices, upNormals(4), uvXY(vertices), Lists{ { 0, 1 }, {}, { 3 } }, true);
        ASSERT_EQ(result.size(), 4u);
        for (const auto& t : result)
            expectTangent(t, 0, 0, 0, 1);
    }

    // ---- Degenerate uv -------------------------------------------------------------------------

    TEST(VsgAdaptersTangentsTest, triangleWithDegenerateUvAddsNoTangent)
    {
        // Collinear uvs: the uv determinant (the cross product's x) is 0 for every axis.
        const std::vector<vsg::vec3> vertices{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
        const std::vector<vsg::vec2> uvs{ { 0, 0 }, { 1, 1 }, { 2, 2 } };
        const auto result = vsgAdapters::generateTangents(vertices, upNormals(3), uvs, Lists{ { 0, 1, 2 } }, false);
        ASSERT_EQ(result.size(), 3u);
        for (const auto& t : result)
            expectTangent(t, 0, 0, 0, 1);
    }

    TEST(VsgAdaptersTangentsTest, degenerateUvTriangleDoesNotDisturbASharedVertex)
    {
        // Triangle (0,1,2) is regular; triangle (0,3,4) has all uvs equal to vertex 0's.
        const std::vector<vsg::vec3> vertices{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 0, -1, 0 } };
        const std::vector<vsg::vec2> uvs{ { 0, 0 }, { 1, 0 }, { 0, 1 }, { 0, 0 }, { 0, 0 } };
        const auto result
            = vsgAdapters::generateTangents(vertices, upNormals(5), uvs, Lists{ { 0, 1, 2, 0, 3, 4 } }, false);
        ASSERT_EQ(result.size(), 5u);
        expectTangent(result[0], 1, 0, 0, 1);
        expectTangent(result[1], 1, 0, 0, 1);
        expectTangent(result[2], 1, 0, 0, 1);
        expectTangent(result[3], 0, 0, 0, 1);
        expectTangent(result[4], 0, 0, 0, 1);
    }

    // ---- Invalid input -------------------------------------------------------------------------

    TEST(VsgAdaptersTangentsTest, mismatchedNormalCountGivesEmpty)
    {
        const auto vertices = quad();
        EXPECT_TRUE(vsgAdapters::generateTangents(vertices, upNormals(3), uvXY(vertices), quadTriangles, false).empty());
        EXPECT_TRUE(vsgAdapters::generateTangents(vertices, upNormals(5), uvXY(vertices), quadTriangles, false).empty());
    }

    TEST(VsgAdaptersTangentsTest, mismatchedTexCoordCountGivesEmpty)
    {
        const auto vertices = quad();
        auto uvs = uvXY(vertices);
        uvs.pop_back();
        EXPECT_TRUE(vsgAdapters::generateTangents(vertices, upNormals(4), uvs, quadTriangles, false).empty());
        EXPECT_TRUE(vsgAdapters::generateTangents(vertices, upNormals(4), {}, quadTriangles, false).empty());
    }

    TEST(VsgAdaptersTangentsTest, indexOutOfRangeGivesEmpty)
    {
        const auto vertices = quad();
        const auto uvs = uvXY(vertices);
        EXPECT_TRUE(
            vsgAdapters::generateTangents(vertices, upNormals(4), uvs, Lists{ { 0, 1, 4 } }, false).empty());
        EXPECT_TRUE(
            vsgAdapters::generateTangents(vertices, upNormals(4), uvs, Lists{ { 0, 1, 2 }, { 0, 2, 9 } }, false)
                .empty());
        EXPECT_TRUE(
            vsgAdapters::generateTangents(vertices, upNormals(4), uvs, Lists{ { 0, 1, 2, 4 } }, true).empty());
        EXPECT_TRUE(vsgAdapters::generateTangents(vertices, {}, uvs, Lists{ { 0, 65535, 2 } }, false).empty());
    }

    TEST(VsgAdaptersTangentsTest, emptyInputGivesEmpty)
    {
        EXPECT_TRUE(vsgAdapters::generateTangents({}, {}, {}, Lists{}, false).empty());
        EXPECT_TRUE(vsgAdapters::generateTangents({}, {}, {}, Lists{}, true).empty());
    }
}
