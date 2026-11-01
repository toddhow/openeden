#include "apps/opencs/model/mesh/meshoverlay.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <vector>

namespace CSMMesh
{
    namespace
    {
        using Col = std::array<float, 4>;
        using P3 = std::array<float, 3>;

        const Col kRed{ 1.f, 0.3f, 0.3f, 1.f };
        const Col kGreen{ 0.3f, 1.f, 0.3f, 1.f };
        const Col kGrey{ 0.5f, 0.5f, 0.5f, 0.6f };

        Aabb makeBox(const std::array<double, 3>& lo, const std::array<double, 3>& hi, bool valid = true)
        {
            Aabb box;
            box.min = lo;
            box.max = hi;
            box.valid = valid;
            return box;
        }

        bool sameColour(const Col& a, const Col& b)
        {
            for (int i = 0; i < 4; ++i)
                if (std::abs(a[i] - b[i]) > 1e-6f)
                    return false;
            return true;
        }

        // Axis a line runs along (0, 1, 2) or -1 if it is not axis aligned or has no length.
        int axisOf(const Line& line)
        {
            int axis = -1;
            for (int i = 0; i < 3; ++i)
                if (std::abs(line.a[i] - line.b[i]) > 1e-6f)
                {
                    if (axis != -1)
                        return -1;
                    axis = i;
                }
            return axis;
        }
    }

    TEST(CSMMeshOverlayTest, boxLinesGivesTwelveEdges)
    {
        const Col colour{ 0.1f, 0.2f, 0.3f, 0.4f };
        const auto lines = boxLines(makeBox({ -1, -2, -3 }, { 1, 2, 3 }), colour);
        ASSERT_EQ(lines.size(), 12u);
        int perAxis[3] = { 0, 0, 0 };
        for (const Line& line : lines)
        {
            EXPECT_TRUE(sameColour(line.colour, colour));
            const int axis = axisOf(line);
            ASSERT_GE(axis, 0);
            ++perAxis[axis];
            EXPECT_NEAR(std::abs(line.a[axis] - line.b[axis]), 2.f * (axis + 1), 1e-5f);
            for (int i = 0; i < 3; ++i)
            {
                EXPECT_NEAR(std::abs(line.a[i]), i + 1, 1e-5f);
                EXPECT_NEAR(std::abs(line.b[i]), i + 1, 1e-5f);
            }
        }
        EXPECT_EQ(perAxis[0], 4);
        EXPECT_EQ(perAxis[1], 4);
        EXPECT_EQ(perAxis[2], 4);
    }

    TEST(CSMMeshOverlayTest, boxLinesEdgesAreDistinct)
    {
        const auto lines = boxLines(makeBox({ 0, 0, 0 }, { 1, 1, 1 }), kRed);
        std::set<std::array<float, 6>> edges;
        for (const Line& line : lines)
        {
            std::array<float, 6> e{ line.a[0], line.a[1], line.a[2], line.b[0], line.b[1], line.b[2] };
            std::array<float, 6> r{ line.b[0], line.b[1], line.b[2], line.a[0], line.a[1], line.a[2] };
            edges.insert(std::min(e, r));
        }
        EXPECT_EQ(edges.size(), 12u);
    }

    TEST(CSMMeshOverlayTest, boxLinesNoneForInvalidBox)
    {
        EXPECT_TRUE(boxLines(makeBox({ 0, 0, 0 }, { 1, 1, 1 }, false), kRed).empty());
        EXPECT_TRUE(boxLines(Aabb{}, kRed).empty());
    }

    TEST(CSMMeshOverlayTest, gridSpacingPicksOneTwoFiveSteps)
    {
        EXPECT_DOUBLE_EQ(gridSpacing(4.0), 1.0); // 4/4 = 1
        EXPECT_DOUBLE_EQ(gridSpacing(3.0), 1.0); // 0.75 -> 1
        EXPECT_DOUBLE_EQ(gridSpacing(4.1), 2.0); // 1.025 -> 2
        EXPECT_DOUBLE_EQ(gridSpacing(8.0), 2.0);
        EXPECT_DOUBLE_EQ(gridSpacing(9.0), 5.0); // 2.25 -> 5
        EXPECT_DOUBLE_EQ(gridSpacing(20.0), 5.0);
        EXPECT_DOUBLE_EQ(gridSpacing(21.0), 10.0); // 5.25 -> 10
        EXPECT_DOUBLE_EQ(gridSpacing(40.0), 10.0);
        EXPECT_DOUBLE_EQ(gridSpacing(41.0), 20.0);
        EXPECT_DOUBLE_EQ(gridSpacing(100.0), 50.0); // 25 -> 50
        EXPECT_DOUBLE_EQ(gridSpacing(1000.0), 500.0); // 250 -> 500
        EXPECT_DOUBLE_EQ(gridSpacing(4000.0), 1000.0);
    }

    TEST(CSMMeshOverlayTest, gridSpacingSmallRadiiUseNegativePowers)
    {
        EXPECT_NEAR(gridSpacing(2.0), 0.5, 1e-12); // 0.5
        EXPECT_NEAR(gridSpacing(1.0), 0.5, 1e-12); // 0.25 -> 0.5
        EXPECT_NEAR(gridSpacing(0.4), 0.1, 1e-12); // 0.1
        EXPECT_NEAR(gridSpacing(0.44), 0.2, 1e-12); // 0.11 -> 0.2
    }

    TEST(CSMMeshOverlayTest, gridSpacingNeverBelowOneThousandth)
    {
        EXPECT_NEAR(gridSpacing(0.004), 0.001, 1e-12);
        EXPECT_NEAR(gridSpacing(0.0001), 0.001, 1e-12);
        EXPECT_NEAR(gridSpacing(1e-9), 0.001, 1e-12);
    }

    TEST(CSMMeshOverlayTest, gridSpacingIsOneForNonPositiveRadius)
    {
        EXPECT_DOUBLE_EQ(gridSpacing(0.0), 1.0);
        EXPECT_DOUBLE_EQ(gridSpacing(-5.0), 1.0);
    }

    TEST(CSMMeshOverlayTest, gridLinesCountAndExtent)
    {
        const auto lines = gridLines(2.0, 3);
        ASSERT_EQ(lines.size(), 14u);
        int alongX = 0, alongY = 0;
        std::set<float> xOffsets, yOffsets;
        for (const Line& line : lines)
        {
            EXPECT_FLOAT_EQ(line.a[2], 0.f);
            EXPECT_FLOAT_EQ(line.b[2], 0.f);
            const int axis = axisOf(line);
            ASSERT_TRUE(axis == 0 || axis == 1);
            const float lo = std::min(line.a[axis], line.b[axis]);
            const float hi = std::max(line.a[axis], line.b[axis]);
            EXPECT_FLOAT_EQ(lo, -6.f);
            EXPECT_FLOAT_EQ(hi, 6.f);
            if (axis == 0)
            {
                ++alongX;
                yOffsets.insert(line.a[1]);
            }
            else
            {
                ++alongY;
                xOffsets.insert(line.a[0]);
            }
        }
        EXPECT_EQ(alongX, 7);
        EXPECT_EQ(alongY, 7);
        const std::set<float> expected{ -6.f, -4.f, -2.f, 0.f, 2.f, 4.f, 6.f };
        EXPECT_EQ(xOffsets, expected);
        EXPECT_EQ(yOffsets, expected);
    }

    TEST(CSMMeshOverlayTest, gridLinesColours)
    {
        const auto lines = gridLines(1.0, 2);
        int red = 0, green = 0, grey = 0;
        for (const Line& line : lines)
        {
            const int axis = axisOf(line);
            ASSERT_GE(axis, 0);
            if (sameColour(line.colour, kRed))
            {
                ++red;
                EXPECT_EQ(axis, 0);
                EXPECT_FLOAT_EQ(line.a[1], 0.f);
            }
            else if (sameColour(line.colour, kGreen))
            {
                ++green;
                EXPECT_EQ(axis, 1);
                EXPECT_FLOAT_EQ(line.a[0], 0.f);
            }
            else
            {
                ++grey;
                EXPECT_TRUE(sameColour(line.colour, kGrey));
            }
        }
        EXPECT_EQ(red, 1);
        EXPECT_EQ(green, 1);
        EXPECT_EQ(grey, 8);
    }

    TEST(CSMMeshOverlayTest, gridLinesHalfCountOne)
    {
        EXPECT_EQ(gridLines(1.0, 1).size(), 6u);
    }

    TEST(CSMMeshOverlayTest, gridLinesNoneForBadArguments)
    {
        EXPECT_TRUE(gridLines(1.0, 0).empty());
        EXPECT_TRUE(gridLines(1.0, -3).empty());
        EXPECT_TRUE(gridLines(0.0, 4).empty());
        EXPECT_TRUE(gridLines(-1.0, 4).empty());
    }

    TEST(CSMMeshOverlayTest, axesLinesThreeColouredLines)
    {
        const P3 origin{ 1.f, 2.f, 3.f };
        const auto lines = axesLines(origin, 5.f);
        ASSERT_EQ(lines.size(), 3u);
        const Col colours[3] = { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 } };
        int found = 0;
        for (const Line& line : lines)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (!sameColour(line.colour, colours[axis]))
                    continue;
                ++found;
                EXPECT_EQ(axisOf(line), axis);
                for (int i = 0; i < 3; ++i)
                {
                    EXPECT_FLOAT_EQ(line.a[i], origin[i]);
                    EXPECT_FLOAT_EQ(line.b[i], origin[i] + (i == axis ? 5.f : 0.f));
                }
            }
        }
        EXPECT_EQ(found, 3);
    }

    TEST(CSMMeshOverlayTest, normalLinesOnePerNormal)
    {
        IndexedMesh mesh;
        mesh.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
        mesh.normals = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 } };
        const Col colour{ 0.2f, 0.4f, 0.6f, 0.8f };
        const auto lines = normalLines(mesh, 2.f, colour);
        ASSERT_EQ(lines.size(), 3u);
        for (size_t i = 0; i < 3; ++i)
        {
            EXPECT_TRUE(sameColour(lines[i].colour, colour));
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_FLOAT_EQ(lines[i].a[k], mesh.positions[i][k]);
                EXPECT_FLOAT_EQ(lines[i].b[k], mesh.positions[i][k] + mesh.normals[i][k] * 2.f);
            }
        }
    }

    TEST(CSMMeshOverlayTest, normalLinesNoneWithoutNormals)
    {
        IndexedMesh mesh;
        mesh.positions = { { 0, 0, 0 }, { 1, 0, 0 } };
        EXPECT_TRUE(normalLines(mesh, 1.f, kRed).empty());
        EXPECT_TRUE(normalLines(IndexedMesh{}, 1.f, kRed).empty());
    }
}
