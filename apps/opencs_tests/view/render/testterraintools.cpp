#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include <components/esm3/loadland.hpp>

#include <apps/opencs/view/render/terraintoolgeometry.hpp>

// Written from the OSG editor's brushdraw.cpp, terrainselection.cpp and TerrainShapeMode::applyTerrainEditChanges.
namespace
{
    namespace Tool = CSVRender::TerrainToolGeometry;

    float flat(float, float)
    {
        return 100.f;
    }

    TEST(TerrainToolGeometryTest, landSizeFactorIsOneVertexOrOneTexel)
    {
        EXPECT_FLOAT_EQ(Tool::landSizeFactor(false), 8192.f / 64.f);
        EXPECT_FLOAT_EQ(Tool::landSizeFactor(true), 8192.f / 16.f);
    }

    TEST(TerrainToolGeometryTest, shapeBrushSnapsToTheNearestVertex)
    {
        const vsg::dvec3 snapped = Tool::snapPoint(vsg::dvec3(130, 250, 7), false);
        EXPECT_DOUBLE_EQ(snapped.x, 128.0);
        EXPECT_DOUBLE_EQ(snapped.y, 256.0);
        EXPECT_DOUBLE_EQ(snapped.z, 7.0);
    }

    TEST(TerrainToolGeometryTest, textureBrushSnapsToTheMiddleOfATexel)
    {
        const vsg::dvec3 first = Tool::snapPoint(vsg::dvec3(300, 300, 0), true);
        const vsg::dvec3 next = Tool::snapPoint(vsg::dvec3(800, 300, 0), true);
        EXPECT_NEAR(next.x - first.x, 512.0, 1e-3);
        EXPECT_NEAR(std::fmod(first.x, 512.0), std::fmod(next.x, 512.0), 1e-3);
    }

    TEST(TerrainToolGeometryTest, pointBrushIsACrossOfTwoLinesAboveTheGround)
    {
        const auto lines = Tool::pointLines(vsg::dvec3(0, 0, 0), flat);
        ASSERT_EQ(lines.size(), 4u);
        EXPECT_FLOAT_EQ(lines[0].x, -8.f);
        EXPECT_FLOAT_EQ(lines[1].x, 8.f);
        for (const auto& vertex : lines)
            EXPECT_FLOAT_EQ(vertex.z, 101.f);
    }

    TEST(TerrainToolGeometryTest, squareBrushHasEightVerticesAtHalfAVertexEach)
    {
        const float factor = Tool::landSizeFactor(false);
        const float radius = factor * 3 / 2;
        const auto lines = Tool::squareLines(radius, vsg::dvec3(1000, 2000, 0), factor, flat);
        // A diameter of 3 vertices is 6 half steps, 8 vertices each.
        EXPECT_EQ(lines.size(), 6u * 8u);
        for (const auto& vertex : lines)
        {
            const bool onX = std::abs(std::abs(vertex.x - 1000.f) - radius) < 1e-2f;
            const bool onY = std::abs(std::abs(vertex.y - 2000.f) - radius) < 1e-2f;
            EXPECT_TRUE(onX || onY);
            EXPECT_FLOAT_EQ(vertex.z, 101.f);
        }
    }

    TEST(TerrainToolGeometryTest, squareBrushLimitsItsResolutionForPerformance)
    {
        const float factor = Tool::landSizeFactor(false);
        const auto lines = Tool::squareLines(factor * 100, vsg::dvec3(0, 0, 0), factor, flat);
        EXPECT_EQ(lines.size(), 128u * 8u);
    }

    TEST(TerrainToolGeometryTest, circleBrushLiesOnItsCircle)
    {
        const auto lines = Tool::circleLines(200.f, vsg::dvec3(10, 20, 0), flat);
        EXPECT_EQ(lines.size(), 128u * 2u);
        for (const auto& vertex : lines)
        {
            EXPECT_NEAR(std::hypot(vertex.x - 10.f, vertex.y - 20.f), 200.f, 1e-2f);
            EXPECT_FLOAT_EQ(vertex.z, 101.f);
        }
    }

    TEST(TerrainToolGeometryTest, shapeSelectionDrawsACrossAtEachVertex)
    {
        const auto height = [](int x, int y) { return x + y; };
        const auto lines = Tool::shapeSelectionLines({ { 2, 3 } }, height);
        ASSERT_EQ(lines.size(), 8u);
        EXPECT_FLOAT_EQ(lines[0].x, 256.f);
        EXPECT_FLOAT_EQ(lines[0].y, 384.f);
        EXPECT_FLOAT_EQ(lines[0].z, 5.f);
        EXPECT_FLOAT_EQ(lines[1].y, 256.f);
        EXPECT_FLOAT_EQ(lines[1].z, 4.f);
    }

    TEST(TerrainToolGeometryTest, textureSelectionDrawsOnlyTheOutside)
    {
        const auto height = [](int, int) { return 0; };
        // Four vertex steps a side: a single texel has 4 sides of 4 lines.
        EXPECT_EQ(Tool::textureSelectionLines({ { 0, 0 } }, height).size(), 4u * 4u * 2u);
        // Two side by side share a side, which neither draws.
        EXPECT_EQ(Tool::textureSelectionLines({ { 0, 0 }, { 1, 0 } }, height).size(), 6u * 4u * 2u);
    }

    TEST(TerrainToolGeometryTest, bilinearHeightMeetsTheCorners)
    {
        EXPECT_FLOAT_EQ(Tool::bilinear(0.f, 0.f, 1.f, 2.f, 3.f, 4.f), 1.f);
        EXPECT_FLOAT_EQ(Tool::bilinear(1.f, 0.f, 1.f, 2.f, 3.f, 4.f), 2.f);
        EXPECT_FLOAT_EQ(Tool::bilinear(0.f, 1.f, 1.f, 2.f, 3.f, 4.f), 3.f);
        EXPECT_FLOAT_EQ(Tool::bilinear(0.5f, 0.5f, 1.f, 2.f, 3.f, 4.f), 2.5f);
    }

    TEST(TerrainToolGeometryTest, landNormalIsUpOnFlatGround)
    {
        const vsg::vec3 normal = Tool::landNormal(0.f, 0.f);
        EXPECT_FLOAT_EQ(normal.x, 0.f);
        EXPECT_FLOAT_EQ(normal.y, 0.f);
        EXPECT_FLOAT_EQ(normal.z, 127.f);
    }

    TEST(TerrainToolGeometryTest, landNormalLeansAwayFromARise)
    {
        // A rise of 128 per vertex along x: 45 degrees, leaning towards -x.
        const vsg::vec3 normal = Tool::landNormal(128.f, 0.f);
        EXPECT_NEAR(normal.x, -127.f / std::sqrt(2.f), 1e-3f);
        EXPECT_NEAR(normal.z, 127.f / std::sqrt(2.f), 1e-3f);
        EXPECT_FLOAT_EQ(normal.y, 0.f);
    }
}
