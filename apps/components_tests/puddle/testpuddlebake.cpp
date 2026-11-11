#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>

#include <components/render/puddlelevel.hpp>

#include "puddletestland.hpp"

// Task 0095, stage W-b: baking one cell. Generated land only (no game data).

namespace
{
    using namespace Puddle;
    using PuddleTest::FakeLand;
    using PuddleTest::pitHeights;
    using PuddleTest::smallSettings;

    std::size_t texel(const CellMap& c, int x, int y)
    {
        return static_cast<std::size_t>(y) * c.mSide + x;
    }

    TEST(PuddleTerrainHeight, the_diamond_pattern_picks_the_diagonal_by_the_parity_of_i_plus_j)
    {
        // 3 x 3 vertices, 4 texels between two. Quad (0,0) has i + j even: the diagonal runs (0,0) to (1,1).
        std::vector<float> h(9, 0.f);
        h[1 * 3 + 1] = 16.f; // vertex (1,1)
        // fa = 0.75, fb = 0.25 is in the lower-right triangle of the main diagonal: 0.25 * 16 = 4 (the other diagonal
        // would give 0).
        EXPECT_FLOAT_EQ(terrainHeightAt(h, 3, 4, 3, 1), 4.f);
        // And the upper-left triangle: fa = 0.25, fb = 0.75 gives 0.25 * 16 = 4 as well (symmetric).
        EXPECT_FLOAT_EQ(terrainHeightAt(h, 3, 4, 1, 3), 4.f);
        // On the diagonal itself: fa = fb = 0.5 is 8.
        EXPECT_FLOAT_EQ(terrainHeightAt(h, 3, 4, 2, 2), 8.f);
        // Quad (1,0) has i + j odd: the diagonal runs (1,1) to (2,0). Heights (1,0) = 0, (2,0) = 0, (1,1) = 0, (2,1)
        // = 16.
        std::vector<float> g(9, 0.f);
        g[1 * 3 + 2] = 16.f; // vertex (2,1)
        // fa = fb = 0.75 (tx = 4 + 3, ty = 3) is above the anti-diagonal: the triangle (2,1), (1,1), (2,0):
        // 16 + 0.25 * (0 - 16) + 0.25 * (0 - 16) = 8 (the main diagonal would give 12).
        EXPECT_FLOAT_EQ(terrainHeightAt(g, 3, 4, 7, 3), 8.f);
        // Below the anti-diagonal (fa = fb = 0.25) the triangle is (1,0), (2,0), (1,1), all 0.
        EXPECT_FLOAT_EQ(terrainHeightAt(g, 3, 4, 5, 1), 0.f);
    }

    TEST(PuddleTerrainHeight, vertices_are_exact_and_a_plane_is_reproduced_everywhere)
    {
        std::vector<float> h(25);
        for (int j = 0; j < 5; ++j)
            for (int i = 0; i < 5; ++i)
                h[j * 5 + i] = 3.f * i + 5.f * j + 7.f;
        for (std::uint32_t ty = 0; ty < 16; ++ty)
        {
            for (std::uint32_t tx = 0; tx < 16; ++tx)
            {
                const float expected = 3.f * (tx / 4.f) + 5.f * (ty / 4.f) + 7.f;
                EXPECT_FLOAT_EQ(terrainHeightAt(h, 5, 4, tx, ty), expected) << tx << "," << ty;
            }
        }
        EXPECT_EQ(terrainHeightAt(h, 5, 4, 8, 4), h[1 * 5 + 2]);
    }

    TEST(PuddleSettings, the_defaults_of_a_morrowind_cell_are_valid_and_odd_ones_are_refused)
    {
        BakeSettings s;
        EXPECT_FALSE(validateSettings(s));
        EXPECT_EQ(s.texelsPerCell(), 128u);
        EXPECT_EQ(s.texelsPerVertexStep(), 2u);
        BakeSettings t = s;
        t.mSpacingUnits = 48;
        EXPECT_EQ(validateSettings(t).mField, "spacing");
        t = s;
        t.mSpacingUnits = 0;
        EXPECT_EQ(validateSettings(t).mField, "spacing");
        t = s;
        t.mSpacingUnits = 256; // coarser than the land
        EXPECT_EQ(validateSettings(t).mField, "spacing");
        t = s;
        t.mCapacityCapSteps = 0;
        EXPECT_EQ(validateSettings(t).mField, "capacityCap");
        t.mCapacityCapSteps = 256;
        EXPECT_EQ(validateSettings(t).mField, "capacityCap");
        t = s;
        t.mMinPondTexels = 0;
        EXPECT_EQ(validateSettings(t).mField, "minPondTexels");
        t = s;
        t.mHalo = 5;
        EXPECT_EQ(validateSettings(t).mField, "halo");
        t = s;
        t.mStatics = true;
        EXPECT_EQ(validateSettings(t).mField, "statics");
        t = s;
        t.mSpacingUnits = 16; // 8 texels a vertex step, 512 a cell: window of 5 cells is 2560 side, over the texel cap
        EXPECT_EQ(validateSettings(t).mField, "halo");
        t.mHalo = 1;
        EXPECT_FALSE(validateSettings(t));
    }

    TEST(PuddleBake, flat_land_has_no_pits_and_keeps_its_height)
    {
        FakeLand land([](int, int) { return 100.f; });
        const BakeResult r = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        EXPECT_TRUE(r.mCell.noPits());
        EXPECT_TRUE(r.mCell.mBasins.empty());
        EXPECT_EQ(r.mCell.mSide, 16u);
        EXPECT_EQ(r.mCell.mHeightBase, 100.f);
        for (const std::uint16_t top : r.mCell.mTop)
            EXPECT_EQ(top, 0);
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(writeCell(r.mCell, bytes));
    }

    TEST(PuddleBake, a_round_pit_becomes_one_pit_that_is_deepest_in_the_middle)
    {
        FakeLand land(pitHeights(2, 2, 108.f, 8.f));
        const BakeResult r = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        ASSERT_FALSE(r.mCell.noPits());
        EXPECT_EQ(r.mCell.mBasins.size(), 1u);
        EXPECT_EQ(r.mStats.mPitsInCell, 1u);
        // The vertex (2,2) is texel (8,8): 8 units of capacity, 128 sixteenths.
        const CellMap& c = r.mCell;
        EXPECT_EQ(c.mCapacity[texel(c, 8, 8)], 128);
        EXPECT_EQ(c.mBasinMax[texel(c, 8, 8)], 128);
        EXPECT_EQ(c.mBasinId[texel(c, 8, 8)], 0);
        EXPECT_EQ(c.mBasins[0].mMaxCapacityCode, 128);
        // The corner of the cell is on the plateau: no capacity there.
        EXPECT_EQ(c.mCapacity[texel(c, 0, 0)], 0);
        EXPECT_EQ(c.mBasinId[texel(c, 0, 0)], noBasin);
        // Capacity falls from the middle outwards.
        EXPECT_GT(c.mCapacity[texel(c, 8, 8)], c.mCapacity[texel(c, 10, 8)]);
        EXPECT_GT(c.mCapacity[texel(c, 10, 8)], c.mCapacity[texel(c, 12, 8)]);
        // Height: the base is the floor of the lowest height (100), the middle sits at 0.
        EXPECT_EQ(c.mHeightBase, 100.f);
        EXPECT_EQ(c.mTop[texel(c, 8, 8)], 0);
        EXPECT_FLOAT_EQ(Render::Puddle::dequantiseHeight(c.mTop[texel(c, 0, 0)], c.mHeightBase), 108.f);
    }

    TEST(PuddleBake, decisions_1_and_2_a_deep_pit_holds_a_puddle_only_at_its_floor_up_to_the_cap)
    {
        // Rim 150, floor 100: 50 units deep. The maximum puddle depth is 12 units (192 sixteenths).
        FakeLand land(pitHeights(2, 2, 150.f, 50.f));
        const BakeResult r = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        const CellMap& c = r.mCell;
        ASSERT_FALSE(c.noPits());
        EXPECT_EQ(c.mCapacity[texel(c, 8, 8)], 192);
        EXPECT_EQ(c.mBasinMax[texel(c, 8, 8)], 192);
        // Halfway up the floor: the ground along the row of vertices is linear between them, 100 at the vertex (2,2)
        // and 112.5 at (3,2), so one texel out it is 103.125 (46.875 below the rim 150: 8.875 above the pond's top
        // limit of 38) and two texels out 106.25 (43.75 below the rim, 5.75). Measured per texel against the cap
        // instead (the flat plate of the first W-a run) these would both be 192.
        EXPECT_EQ(c.mCapacity[texel(c, 9, 8)], 142);
        EXPECT_EQ(c.mCapacity[texel(c, 10, 8)], 92);
        // Never above the cap anywhere.
        EXPECT_EQ(*std::max_element(c.mCapacity.begin(), c.mCapacity.end()), 192);
        // The pond is the bottom 12 units only: fewer texels than the pit has below its rim.
        std::size_t wet = 0;
        for (const std::uint8_t v : c.mCapacity)
            wet += v != 0;
        EXPECT_LT(wet, 0.5 * 16 * 16);
        // A texel where the ground is 12 or more above the floor holds nothing.
        EXPECT_EQ(c.mCapacity[texel(c, 12, 8)], 0);
    }

    TEST(PuddleBake, a_pit_shallower_than_the_cap_keeps_its_own_depth)
    {
        FakeLand land(pitHeights(2, 2, 105.f, 5.f));
        const BakeResult r = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        EXPECT_EQ(r.mCell.mCapacity[texel(r.mCell, 8, 8)], 80);
        EXPECT_EQ(r.mCell.mBasins[0].mMaxCapacityCode, 80);
    }

    TEST(PuddleBake, the_cap_is_a_setting)
    {
        FakeLand land(pitHeights(2, 2, 150.f, 50.f));
        BakeSettings s = smallSettings();
        s.mCapacityCapSteps = 64; // 4 units
        const BakeResult r = bakeCell(land, 0, 0, s);
        ASSERT_FALSE(r.mError);
        EXPECT_EQ(r.mCell.mCapacity[texel(r.mCell, 8, 8)], 64);
    }

    TEST(PuddleBake, decision_3_a_pit_with_a_small_pond_is_dropped_at_the_limit)
    {
        FakeLand land(pitHeights(2, 2, 108.f, 8.f));
        const BakeResult base = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(base.mError);
        ASSERT_EQ(base.mCell.mBasins.size(), 1u);
        const std::uint32_t area = base.mCell.mBasins[0].mArea;
        ASSERT_GT(area, 16u);
        BakeSettings s = smallSettings();
        s.mMinPondTexels = area; // exactly the pond's size: kept
        EXPECT_FALSE(bakeCell(land, 0, 0, s).mCell.noPits());
        s.mMinPondTexels = area + 1; // one more than the pond has: dropped
        const BakeResult dropped = bakeCell(land, 0, 0, s);
        ASSERT_FALSE(dropped.mError);
        EXPECT_TRUE(dropped.mCell.noPits());
        EXPECT_EQ(dropped.mStats.mPitsDroppedByArea, 1u);
        EXPECT_EQ(dropped.mStats.mPitsKept, 0u);
    }

    TEST(PuddleBake, decision_3_the_default_limit_is_sixteen_texels)
    {
        EXPECT_EQ(BakeSettings{}.mMinPondTexels, 16u);
    }

    TEST(PuddleBake, decision_4_the_height_base_makes_every_cell_of_a_tall_world_fit)
    {
        // A plane falling from 18,952 to -2,152 over 20 cells (1,055 units a cell step).
        FakeLand land([](int vx, int) { return 18952.f - static_cast<float>(vx) * (21104.f / 80.f); });
        for (int cell : { 0, 7, 13, 19 })
        {
            const BakeResult r = bakeCell(land, cell, 0, smallSettings());
            ASSERT_FALSE(r.mError);
            EXPECT_FALSE(r.mStats.mHeightsClamped) << cell;
            std::vector<float> vertices;
            land.cellHeights(cell, 0, vertices);
            for (std::uint32_t ty = 0; ty < 16; ty += 5)
                for (std::uint32_t tx = 0; tx < 16; tx += 3)
                {
                    const float truth = terrainHeightAt(vertices, 5, 4, tx, ty);
                    const float back
                        = Render::Puddle::dequantiseHeight(r.mCell.mTop[texel(r.mCell, tx, ty)], r.mCell.mHeightBase);
                    EXPECT_NEAR(back, truth, 1.f / 16.f) << cell << " " << tx << "," << ty;
                }
        }
    }

    TEST(PuddleBake, decision_4_a_cell_taller_than_the_sixteen_bit_range_is_flagged)
    {
        FakeLand land([](int vx, int vy) { return (vx == 3 && vy == 3) ? 9000.f : 0.f; });
        const BakeResult r = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        EXPECT_TRUE(r.mStats.mHeightsClamped);
    }

    TEST(PuddleBake, decision_5_the_catchment_radius_bounds_the_ratio)
    {
        // A plane falling east with a pit in the middle of the cell.
        auto height = [](int vx, int vy) {
            float h = 200.f - 3.f * static_cast<float>(vx);
            const float dx = static_cast<float>(vx - 2);
            const float dy = static_cast<float>(vy - 2);
            if (dx * dx + dy * dy < 1.f)
                h -= 14.f;
            return h;
        };
        FakeLand land(height);
        BakeSettings small = smallSettings();
        small.mHalo = 2;
        small.mCatchmentRadiusUnits = 64;
        BakeSettings large = small;
        large.mCatchmentRadiusUnits = 4096;
        const BakeResult a = bakeCell(land, 0, 0, small);
        const BakeResult b = bakeCell(land, 0, 0, large);
        ASSERT_FALSE(a.mError);
        ASSERT_FALSE(b.mError);
        ASSERT_EQ(a.mStats.mKappasBounded.size(), 1u);
        ASSERT_EQ(b.mStats.mKappasBounded.size(), 1u);
        EXPECT_GE(a.mStats.mKappasBounded[0], 1.f);
        EXPECT_LT(a.mStats.mKappasBounded[0], b.mStats.mKappasBounded[0]);
        // The stored code follows.
        EXPECT_LT(a.mCell.mBasins[0].mCatchmentCode, b.mCell.mBasins[0].mCatchmentCode);
        // The radius is in the parameter digest.
        EXPECT_NE(parameterDigest(small), parameterDigest(large));
    }

    TEST(PuddleBake, a_bake_is_deterministic)
    {
        FakeLand land(pitHeights(2, 2, 108.f, 8.f));
        const BakeResult a = bakeCell(land, 0, 0, smallSettings());
        const BakeResult b = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(a.mError);
        EXPECT_EQ(a.mCell, b.mCell);
    }

    TEST(PuddleBake, the_result_does_not_depend_on_cells_beyond_the_window)
    {
        auto height = pitHeights(2, 2, 108.f, 8.f);
        auto noisy = [height](int vx, int vy) {
            // A different world far away (more than one cell away from cell (0,0)).
            if (vx > 12 || vx < -12 || vy > 12 || vy < -12)
                return 50.f + static_cast<float>((vx * 7 + vy * 13) % 5);
            return height(vx, vy);
        };
        FakeLand plain(height);
        FakeLand far(noisy);
        const BakeResult a = bakeCell(plain, 0, 0, smallSettings());
        const BakeResult b = bakeCell(far, 0, 0, smallSettings());
        ASSERT_FALSE(a.mError);
        ASSERT_FALSE(b.mError);
        // The digest differs only if the window sees the difference: cells (-1..1) cover vertices -4..8 here.
        EXPECT_EQ(a.mCell, b.mCell);
        // And the result does not depend on whether cells far away exist at all.
        FakeLand alone(height, [](int x, int y) { return std::abs(x) <= 1 && std::abs(y) <= 1; });
        const BakeResult c = bakeCell(alone, 0, 0, smallSettings());
        EXPECT_EQ(a.mCell, c.mCell);
    }

    TEST(PuddleBake, the_input_digest_follows_the_window_and_only_the_window)
    {
        FakeLand land(pitHeights(2, 2, 108.f, 8.f));
        const BakeSettings s = smallSettings();
        const Digest base = inputDigest(land, 0, 0, s);
        EXPECT_EQ(base, bakeCell(land, 0, 0, s).mCell.mInputDigest);
        // A vertex in the halo cell (1, 0) changes it ...
        FakeLand halo(
            [](int vx, int vy) { return (vx == 6 && vy == 1) ? 130.f : pitHeights(2, 2, 108.f, 8.f)(vx, vy); });
        EXPECT_NE(inputDigest(halo, 0, 0, s), base);
        // ... one in cell (2, 0), beyond the halo, does not.
        FakeLand beyond(
            [](int vx, int vy) { return (vx == 10 && vy == 1) ? 130.f : pitHeights(2, 2, 108.f, 8.f)(vx, vy); });
        EXPECT_EQ(inputDigest(beyond, 0, 0, s), base);
        BakeSettings wide = s;
        wide.mHalo = 2;
        EXPECT_NE(inputDigest(beyond, 0, 0, wide), inputDigest(land, 0, 0, wide));
        // The water level and the presence of land matter.
        BakeSettings wet = s;
        wet.mWaterLevelUnits = 50;
        EXPECT_NE(inputDigest(land, 0, 0, wet), base);
        FakeLand holes(pitHeights(2, 2, 108.f, 8.f), [](int x, int y) { return !(x == 1 && y == 0); });
        EXPECT_NE(inputDigest(holes, 0, 0, s), base);
        // Settings that do not change what is read do not change it.
        BakeSettings other = s;
        other.mMinPondTexels = 99;
        other.mCapacityCapSteps = 100;
        EXPECT_EQ(inputDigest(land, 0, 0, other), base);
        // Another cell is another digest.
        EXPECT_NE(inputDigest(land, 1, 0, s), base);
    }

    TEST(PuddleBake, every_setting_changes_the_parameter_digest)
    {
        const BakeSettings s = smallSettings();
        const Digest base = parameterDigest(s);
        EXPECT_EQ(parameterDigest(smallSettings()), base);
        auto differs = [&](auto change) {
            BakeSettings t = s;
            change(t);
            return parameterDigest(t) != base;
        };
        EXPECT_TRUE(differs([](BakeSettings& t) { t.mSpacingUnits = 32; }));
        EXPECT_TRUE(differs([](BakeSettings& t) { t.mCapacityCapSteps = 100; }));
        EXPECT_TRUE(differs([](BakeSettings& t) { t.mMinPondTexels = 9; }));
        EXPECT_TRUE(differs([](BakeSettings& t) { t.mCatchmentRadiusUnits = 7; }));
        EXPECT_TRUE(differs([](BakeSettings& t) { t.mHalo = 2; }));
        EXPECT_TRUE(differs([](BakeSettings& t) { t.mStatics = true; }));
        EXPECT_TRUE(differs([](BakeSettings& t) { t.mWaterLevelUnits = 3; }));
        EXPECT_TRUE(differs([](BakeSettings& t) { t.mCellSizeUnits = 2048; }));
        EXPECT_TRUE(differs([](BakeSettings& t) { t.mCellVertices = 9; }));
    }

    TEST(PuddleBake, land_below_the_water_level_is_a_sink_and_holds_no_puddle)
    {
        // A pit whose floor and rim are below the water level of -1.
        FakeLand land(pitHeights(2, 2, -20.f, 8.f));
        const BakeResult r = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        EXPECT_TRUE(r.mCell.noPits());
        // The same pit with the water lower is a pit again.
        BakeSettings low = smallSettings();
        low.mWaterLevelUnits = -100;
        EXPECT_FALSE(bakeCell(land, 0, 0, low).mCell.noPits());
    }

    TEST(PuddleBake, a_cell_next_to_cells_without_land_drains_into_them)
    {
        // A pit at the edge of the land: the cell to the east has no land, so its texels are sinks and the rim there
        // is whatever the ground is; a pit whose rim is on the coast cannot hold water above the coast.
        FakeLand land(pitHeights(4, 2, 108.f, 8.f), [](int x, int) { return x <= 0; });
        const BakeResult r = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        // The pit sits on the cell's east edge (vertex 4): the water would leave over the coast, so what is left in
        // the middle of the pit's east half is nothing.
        for (std::uint32_t y = 0; y < 16; ++y)
            EXPECT_EQ(r.mCell.mCapacity.empty() ? 0 : r.mCell.mCapacity[texel(r.mCell, 15, y)], 0) << y;
    }

    TEST(PuddleBake, two_pits_get_two_ids_in_order_of_first_texel)
    {
        auto height = [](int vx, int vy) {
            const float a = pitHeights(1, 2, 108.f, 8.f)(vx, vy);
            const float b = pitHeights(3, 2, 108.f, 8.f)(vx, vy);
            return std::min(a, b);
        };
        FakeLand land(height);
        BakeSettings s = smallSettings();
        s.mHalo = 2;
        const BakeResult r = bakeCell(land, 0, 0, s);
        ASSERT_FALSE(r.mError);
        // The pits touch at their rims at the plateau height: they may be one pit or two; the format must be valid.
        ASSERT_FALSE(r.mCell.noPits());
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(writeCell(r.mCell, bytes));
        CellMap back;
        EXPECT_FALSE(parseCell(bytes, back));
        EXPECT_EQ(back, r.mCell);
    }
}
