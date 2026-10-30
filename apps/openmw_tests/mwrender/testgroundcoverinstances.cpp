// Tests for MWRender::GroundcoverDensity and MWRender::isInGroundcoverChunk, OpenMW's DensityCalculator and
// isInChunkBorders (task 0012, V4f). Written from the comments and declarations in
// apps/openmw/mwrender/groundcoverinstances.hpp only.
//
// Conventions these tests rely on:
// - Densities used where counts must be exact (0.25, 0.5, 0.125) are exactly representable, so the running total
//   reaches exactly 1.
// - Positions are world units; a chunk bound in cell units times the cell size 8192 gives the world bound. Fractional
//   bounds used here (0.25, 0.5, 0.75) give exact world bounds.
// - "Not before minBound" means a position exactly at a fractional minBound is taken.
// - The "at least one cell wide" rule is read per chunk, by size (maxBound - minBound); a chunk one cell wide on only
//   one axis is treated as a smaller chunk, so its fractional bound on the other axis still filters.

#include "apps/openmw/mwrender/groundcoverinstances.hpp"

#include <vsg/maths/vec2.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
    using MWRender::GroundcoverDensity;
    using MWRender::isInGroundcoverChunk;

    constexpr float sCellSize = 8192.f;

    // The results of `count` calls to isInstanceEnabled.
    std::vector<bool> run(GroundcoverDensity& density, int count)
    {
        std::vector<bool> result;
        for (int i = 0; i < count; ++i)
            result.push_back(density.isInstanceEnabled());
        return result;
    }

    // Every `period`th call is kept, starting with the `period`th.
    std::vector<bool> everyNth(int period, int count)
    {
        std::vector<bool> result;
        for (int i = 1; i <= count; ++i)
            result.push_back(i % period == 0);
        return result;
    }

    // GroundcoverDensity

    TEST(MWRenderGroundcoverDensityTest, densityOneKeepsAll)
    {
        GroundcoverDensity density(1.f);
        EXPECT_EQ(run(density, 16), std::vector<bool>(16, true));
    }

    TEST(MWRenderGroundcoverDensityTest, densityAboveOneKeepsAll)
    {
        GroundcoverDensity density(2.5f);
        EXPECT_EQ(run(density, 16), std::vector<bool>(16, true));
    }

    TEST(MWRenderGroundcoverDensityTest, densityZeroKeepsNone)
    {
        GroundcoverDensity density(0.f);
        EXPECT_EQ(run(density, 32), std::vector<bool>(32, false));
    }

    TEST(MWRenderGroundcoverDensityTest, negativeDensityKeepsNone)
    {
        GroundcoverDensity density(-0.5f);
        EXPECT_EQ(run(density, 32), std::vector<bool>(32, false));
    }

    TEST(MWRenderGroundcoverDensityTest, quarterKeepsEveryFourthStartingWithTheFourth)
    {
        GroundcoverDensity density(0.25f);
        const std::vector<bool> result = run(density, 16);
        EXPECT_EQ(result, everyNth(4, 16));
        int kept = 0;
        for (bool enabled : result)
            kept += enabled ? 1 : 0;
        EXPECT_EQ(kept, 4);
    }

    TEST(MWRenderGroundcoverDensityTest, halfKeepsEverySecond)
    {
        GroundcoverDensity density(0.5f);
        EXPECT_EQ(run(density, 16), everyNth(2, 16));
    }

    TEST(MWRenderGroundcoverDensityTest, eighthKeepsEveryEighth)
    {
        GroundcoverDensity density(0.125f);
        EXPECT_EQ(run(density, 32), everyNth(8, 32));
    }

    TEST(MWRenderGroundcoverDensityTest, resetRestartsThePattern)
    {
        GroundcoverDensity density(0.25f);
        // Three calls bring the total to 0.75; without the reset the next call would be kept.
        EXPECT_EQ(run(density, 3), std::vector<bool>(3, false));
        density.reset();
        EXPECT_EQ(run(density, 8), everyNth(4, 8));
    }

    TEST(MWRenderGroundcoverDensityTest, resetOnAFreshCalculatorChangesNothing)
    {
        GroundcoverDensity density(0.5f);
        density.reset();
        EXPECT_EQ(run(density, 6), everyNth(2, 6));
    }

    TEST(MWRenderGroundcoverDensityTest, resetDoesNotAffectFullDensity)
    {
        GroundcoverDensity density(1.f);
        EXPECT_EQ(run(density, 3), std::vector<bool>(3, true));
        density.reset();
        EXPECT_EQ(run(density, 3), std::vector<bool>(3, true));
    }

    // isInGroundcoverChunk: whole-cell chunks

    TEST(MWRenderGroundcoverChunkTest, oneCellChunkTakesEverything)
    {
        const vsg::vec2 minBound(0.f, 0.f);
        const vsg::vec2 maxBound(1.f, 1.f);
        EXPECT_TRUE(isInGroundcoverChunk(100.f, 100.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(-50000.f, 50000.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(1e6f, -1e6f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(sCellSize, sCellSize, minBound, maxBound));
    }

    TEST(MWRenderGroundcoverChunkTest, largeChunkTakesEverything)
    {
        const vsg::vec2 minBound(-2.f, -2.f);
        const vsg::vec2 maxBound(2.f, 2.f);
        EXPECT_TRUE(isInGroundcoverChunk(0.f, 0.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(10.f * sCellSize, -10.f * sCellSize, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(-3.f * sCellSize, 3.f * sCellSize, minBound, maxBound));
    }

    TEST(MWRenderGroundcoverChunkTest, wholeCellChunkWithFractionalBoundsTakesEverything)
    {
        // At least one cell wide on both axes, even though the bounds are not whole numbers.
        const vsg::vec2 minBound(0.5f, 0.5f);
        const vsg::vec2 maxBound(1.5f, 1.5f);
        EXPECT_TRUE(isInGroundcoverChunk(0.f, 0.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(2.f * sCellSize, 2.f * sCellSize, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(-1e6f, 1e6f, minBound, maxBound));
    }

    // isInGroundcoverChunk: a quarter-cell chunk, min (0.5, 0.5) max (0.75, 0.75), world 4096 to 6144

    TEST(MWRenderGroundcoverChunkTest, quarterChunkTakesInside)
    {
        const vsg::vec2 minBound(0.5f, 0.5f);
        const vsg::vec2 maxBound(0.75f, 0.75f);
        EXPECT_TRUE(isInGroundcoverChunk(5000.f, 5000.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(4500.f, 6000.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(6143.f, 4097.f, minBound, maxBound));
    }

    TEST(MWRenderGroundcoverChunkTest, quarterChunkTakesExactlyAtMin)
    {
        const vsg::vec2 minBound(0.5f, 0.5f);
        const vsg::vec2 maxBound(0.75f, 0.75f);
        EXPECT_TRUE(isInGroundcoverChunk(4096.f, 4096.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(4096.f, 5000.f, minBound, maxBound));
    }

    TEST(MWRenderGroundcoverChunkTest, quarterChunkRejectsBeforeMin)
    {
        const vsg::vec2 minBound(0.5f, 0.5f);
        const vsg::vec2 maxBound(0.75f, 0.75f);
        EXPECT_FALSE(isInGroundcoverChunk(4000.f, 5000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(5000.f, 4000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(4095.f, 4095.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(0.f, 5000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(-5000.f, 5000.f, minBound, maxBound));
    }

    TEST(MWRenderGroundcoverChunkTest, quarterChunkRejectsAtMax)
    {
        const vsg::vec2 minBound(0.5f, 0.5f);
        const vsg::vec2 maxBound(0.75f, 0.75f);
        EXPECT_FALSE(isInGroundcoverChunk(6144.f, 5000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(5000.f, 6144.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(6144.f, 6144.f, minBound, maxBound));
    }

    TEST(MWRenderGroundcoverChunkTest, quarterChunkRejectsAfterMax)
    {
        const vsg::vec2 minBound(0.5f, 0.5f);
        const vsg::vec2 maxBound(0.75f, 0.75f);
        EXPECT_FALSE(isInGroundcoverChunk(7000.f, 5000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(5000.f, 7000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(2.f * sCellSize, 5000.f, minBound, maxBound));
    }

    // isInGroundcoverChunk: whole-number bounds do not filter

    TEST(MWRenderGroundcoverChunkTest, quarterChunkTakesBeyondAWholeMax)
    {
        // x from 0.75 to 1 (whole), y from 0.5 to 0.75.
        const vsg::vec2 minBound(0.75f, 0.5f);
        const vsg::vec2 maxBound(1.f, 0.75f);
        EXPECT_TRUE(isInGroundcoverChunk(7000.f, 5000.f, minBound, maxBound)); // inside
        EXPECT_TRUE(isInGroundcoverChunk(sCellSize, 5000.f, minBound, maxBound)); // at x max 1
        EXPECT_TRUE(isInGroundcoverChunk(9000.f, 5000.f, minBound, maxBound)); // past x max 1
        EXPECT_TRUE(isInGroundcoverChunk(5.f * sCellSize, 5000.f, minBound, maxBound));
        // x min is still fractional, and y still filters.
        EXPECT_FALSE(isInGroundcoverChunk(6000.f, 5000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(9000.f, 7000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(9000.f, 4000.f, minBound, maxBound));
    }

    TEST(MWRenderGroundcoverChunkTest, quarterChunkTakesBeforeAWholeMin)
    {
        // x from 1 (whole) to 1.25, y from 0.5 to 0.75.
        const vsg::vec2 minBound(1.f, 0.5f);
        const vsg::vec2 maxBound(1.25f, 0.75f);
        EXPECT_TRUE(isInGroundcoverChunk(9000.f, 5000.f, minBound, maxBound)); // inside
        EXPECT_TRUE(isInGroundcoverChunk(7000.f, 5000.f, minBound, maxBound)); // before x min 1
        EXPECT_TRUE(isInGroundcoverChunk(-3.f * sCellSize, 5000.f, minBound, maxBound));
        // x max 1.25 (10240) is fractional.
        EXPECT_FALSE(isInGroundcoverChunk(10240.f, 5000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(11000.f, 5000.f, minBound, maxBound));
        // y still filters.
        EXPECT_FALSE(isInGroundcoverChunk(7000.f, 4000.f, minBound, maxBound));
    }

    TEST(MWRenderGroundcoverChunkTest, chunkOneCellWideOnOneAxisFiltersTheOther)
    {
        // One cell wide on x (whole bounds), half a cell on y from 0.5 to 1.
        const vsg::vec2 minBound(0.f, 0.5f);
        const vsg::vec2 maxBound(1.f, 1.f);
        EXPECT_TRUE(isInGroundcoverChunk(4000.f, 5000.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(-4000.f, 9000.f, minBound, maxBound)); // x and y max are whole
        EXPECT_FALSE(isInGroundcoverChunk(4000.f, 4000.f, minBound, maxBound)); // before y min 0.5
    }

    // isInGroundcoverChunk: negative cell coordinates

    TEST(MWRenderGroundcoverChunkTest, negativeQuarterChunk)
    {
        // x and y from -0.75 to -0.5: world -6144 to -4096.
        const vsg::vec2 minBound(-0.75f, -0.75f);
        const vsg::vec2 maxBound(-0.5f, -0.5f);
        EXPECT_TRUE(isInGroundcoverChunk(-5000.f, -5000.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(-6144.f, -6144.f, minBound, maxBound)); // at min
        EXPECT_TRUE(isInGroundcoverChunk(-4097.f, -6000.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(-6500.f, -5000.f, minBound, maxBound)); // before min
        EXPECT_FALSE(isInGroundcoverChunk(-5000.f, -6500.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(-4096.f, -5000.f, minBound, maxBound)); // at max
        EXPECT_FALSE(isInGroundcoverChunk(-5000.f, -4096.f, minBound, maxBound));
        EXPECT_FALSE(isInGroundcoverChunk(-4000.f, -5000.f, minBound, maxBound)); // after max
        EXPECT_FALSE(isInGroundcoverChunk(5000.f, 5000.f, minBound, maxBound));
    }

    TEST(MWRenderGroundcoverChunkTest, negativeQuarterChunkWithWholeBounds)
    {
        // x from -1 (whole) to -0.75, y from -0.25 to 0 (whole).
        const vsg::vec2 minBound(-1.f, -0.25f);
        const vsg::vec2 maxBound(-0.75f, 0.f);
        EXPECT_TRUE(isInGroundcoverChunk(-7000.f, -1000.f, minBound, maxBound)); // inside
        EXPECT_TRUE(isInGroundcoverChunk(-9000.f, -1000.f, minBound, maxBound)); // before x min -1
        EXPECT_TRUE(isInGroundcoverChunk(-7000.f, 1000.f, minBound, maxBound)); // past y max 0
        EXPECT_FALSE(isInGroundcoverChunk(-6144.f, -1000.f, minBound, maxBound)); // at x max -0.75
        EXPECT_FALSE(isInGroundcoverChunk(-7000.f, -3000.f, minBound, maxBound)); // before y min -0.25 (-2048)
    }

    TEST(MWRenderGroundcoverChunkTest, negativeWholeCellChunkTakesEverything)
    {
        const vsg::vec2 minBound(-3.f, -1.f);
        const vsg::vec2 maxBound(-2.f, 0.f);
        EXPECT_TRUE(isInGroundcoverChunk(-20000.f, -4000.f, minBound, maxBound));
        EXPECT_TRUE(isInGroundcoverChunk(50000.f, 50000.f, minBound, maxBound));
    }

    // groundcoverCellsInRange
    //
    // Distances are kept one unit away from the exact gap to a cell, since whether a cell exactly `distance` away is
    // included is only specified for distance 0. "Sorted" is read as std::pair's ordering (x, then y).

    using Cells = std::vector<std::pair<int, int>>;

    Cells sorted(Cells cells)
    {
        std::sort(cells.begin(), cells.end());
        return cells;
    }

    bool contains(const Cells& cells, int x, int y)
    {
        return std::find(cells.begin(), cells.end(), std::make_pair(x, y)) != cells.end();
    }

    TEST(MWRenderGroundcoverCellsTest, distanceZeroGivesTheOwnCell)
    {
        EXPECT_EQ(MWRender::groundcoverCellsInRange(4096.f, 4096.f, 0.f), (Cells{ { 0, 0 } }));
        EXPECT_EQ(MWRender::groundcoverCellsInRange(3.f * 8192.f + 100.f, 8192.f + 8000.f, 0.f), (Cells{ { 3, 1 } }));
    }

    TEST(MWRenderGroundcoverCellsTest, distanceZeroOnABorderGivesBothCells)
    {
        EXPECT_EQ(MWRender::groundcoverCellsInRange(8192.f, 4096.f, 0.f), (Cells{ { 0, 0 }, { 1, 0 } }));
        EXPECT_EQ(MWRender::groundcoverCellsInRange(4096.f, 8192.f, 0.f), (Cells{ { 0, 0 }, { 0, 1 } }));
    }

    TEST(MWRenderGroundcoverCellsTest, distanceZeroOnACornerGivesFourCells)
    {
        EXPECT_EQ(
            MWRender::groundcoverCellsInRange(8192.f, 8192.f, 0.f), (Cells{ { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } }));
        EXPECT_EQ(
            MWRender::groundcoverCellsInRange(0.f, 0.f, 0.f), (Cells{ { -1, -1 }, { -1, 0 }, { 0, -1 }, { 0, 0 } }));
    }

    TEST(MWRenderGroundcoverCellsTest, justUnderTheGapExcludesTheNeighbours)
    {
        // From the centre of cell (0, 0) each straight neighbour is 4096 away.
        EXPECT_EQ(MWRender::groundcoverCellsInRange(4096.f, 4096.f, 4095.f), (Cells{ { 0, 0 } }));
    }

    TEST(MWRenderGroundcoverCellsTest, justOverTheGapIncludesTheStraightNeighbours)
    {
        // The diagonal neighbours are 4096 * sqrt(2), about 5793, away.
        EXPECT_EQ(MWRender::groundcoverCellsInRange(4096.f, 4096.f, 4097.f),
            sorted(Cells{ { -1, 0 }, { 0, -1 }, { 0, 0 }, { 0, 1 }, { 1, 0 } }));
    }

    TEST(MWRenderGroundcoverCellsTest, gapToOneNeighbourOnly)
    {
        // At x 8000 the cell (1, 0) is 192 away; (-1, 0) is 8000 away, (0, +-1) 4096 away.
        const Cells under = MWRender::groundcoverCellsInRange(8000.f, 4096.f, 191.f);
        EXPECT_EQ(under, (Cells{ { 0, 0 } }));
        const Cells over = MWRender::groundcoverCellsInRange(8000.f, 4096.f, 193.f);
        EXPECT_EQ(over, (Cells{ { 0, 0 }, { 1, 0 } }));
    }

    TEST(MWRenderGroundcoverCellsTest, largeDistanceIsRoundNotSquare)
    {
        // From the centre of (0, 0) with 12289: the cells two away straight are 12288 away (in), (+-1, +-1) about
        // 5793 (in), (+-1, +-2) and (+-2, +-1) about 12953 (out), (+-2, +-2) about 17378 (out). 13 cells, not 25.
        const Cells cells = MWRender::groundcoverCellsInRange(4096.f, 4096.f, 12289.f);
        const Cells expected = sorted(Cells{ { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -2, 0 }, { 2, 0 },
            { 0, -2 }, { 0, 2 }, { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } });
        EXPECT_EQ(cells, expected);
        EXPECT_TRUE(contains(cells, 2, 0));
        EXPECT_FALSE(contains(cells, 2, 1));
        EXPECT_FALSE(contains(cells, 2, 2));
    }

    TEST(MWRenderGroundcoverCellsTest, resultIsSorted)
    {
        const float positions[][2] = { { 4096.f, 4096.f }, { -20000.f, 30000.f }, { 8192.f, -8192.f } };
        for (const auto& p : positions)
        {
            const Cells cells = MWRender::groundcoverCellsInRange(p[0], p[1], 30000.f);
            EXPECT_TRUE(std::is_sorted(cells.begin(), cells.end())) << p[0] << ", " << p[1];
            EXPECT_EQ(std::adjacent_find(cells.begin(), cells.end()), cells.end()) << "duplicates at " << p[0];
        }
    }

    TEST(MWRenderGroundcoverCellsTest, negativeCoordinates)
    {
        // (-4096, -4096) is the centre of cell (-1, -1); (-1, -1) world is in it too, not in (0, 0).
        EXPECT_EQ(MWRender::groundcoverCellsInRange(-4096.f, -4096.f, 0.f), (Cells{ { -1, -1 } }));
        EXPECT_EQ(MWRender::groundcoverCellsInRange(-1.f, -1.f, 0.f), (Cells{ { -1, -1 } }));
        EXPECT_EQ(MWRender::groundcoverCellsInRange(-4096.f, -4096.f, 4095.f), (Cells{ { -1, -1 } }));
        EXPECT_EQ(MWRender::groundcoverCellsInRange(-4096.f, -4096.f, 4097.f),
            sorted(Cells{ { -2, -1 }, { -1, -2 }, { -1, -1 }, { -1, 0 }, { 0, -1 } }));
        // On the border at x -8192: cells -2 and -1.
        EXPECT_EQ(MWRender::groundcoverCellsInRange(-8192.f, -4096.f, 0.f), (Cells{ { -2, -1 }, { -1, -1 } }));
    }

    TEST(MWRenderGroundcoverCellsTest, ownCellAlwaysIncluded)
    {
        const float positions[][2]
            = { { 4096.f, 4096.f }, { 1.f, 8191.f }, { -1.f, 1.f }, { -40000.f, -40000.f }, { 81920.f, -81919.f } };
        const float distances[] = { 0.f, 1.f, 4095.f, 20000.f };
        for (const auto& p : positions)
        {
            const int cellX = static_cast<int>(std::floor(p[0] / 8192.f));
            const int cellY = static_cast<int>(std::floor(p[1] / 8192.f));
            for (float d : distances)
                EXPECT_TRUE(contains(MWRender::groundcoverCellsInRange(p[0], p[1], d), cellX, cellY))
                    << p[0] << ", " << p[1] << " distance " << d;
        }
    }
}
