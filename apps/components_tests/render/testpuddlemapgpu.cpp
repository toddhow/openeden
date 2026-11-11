// OpenEden (task 0095, stage W-d): tests of components/render/puddlemapgpu.hpp, the C++ twin of the shader that reads
// the puddle map from a storage buffer (files/shaders/lib/material/puddlemap.glsl and .slang): the buffer's layout, the
// texel lookup across cells, the bilinear capacity and the water level, written from the spec before the code. Hand
// values come from research note 0049 section 5.2 (level = min(Cmax, Wf * kappa), depth = clamp(level - (Cmax - C),
// 0, C)).

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include <components/render/puddlelevel.hpp>
#include <components/render/puddlemapgpu.hpp>

namespace
{
    namespace PM = Render::PuddleMap;

    // A window of 7 x 7 cells with origin (-3, -3) and 8 texels a cell side, 64 units apart (a cell is 512 units here,
    // so the tests are small; the shader reads the cell size from the header). Cells are added by `addCell`.
    constexpr std::uint32_t side = 8;
    constexpr std::uint32_t spacing = 64;
    constexpr std::uint32_t cellSize = side * spacing;

    struct Buffer
    {
        std::vector<std::uint32_t> words;
        std::int32_t originX = -3;
        std::int32_t originY = -3;
        std::uint32_t nextCell = 0;

        Buffer()
            : words(PM::bufferWords(PM::windowSide, side), 0u)
        {
            words[PM::wordEnabled] = 1;
            words[PM::wordDebug] = 0;
            words[PM::wordOriginX] = static_cast<std::uint32_t>(originX);
            words[PM::wordOriginY] = static_cast<std::uint32_t>(originY);
            words[PM::wordWindowSide] = PM::windowSide;
            words[PM::wordTexelsPerCell] = side;
            words[PM::wordSpacing] = spacing;
            words[PM::wordCellSize] = cellSize;
            words[PM::wordSupply] = PM::floatBits(0.5f);
            words[PM::wordAbsorption] = PM::floatBits(0.1f);
        }

        // Makes the cell present (all texels empty) and returns the index of its first texel word.
        std::uint32_t addCell(std::int32_t cx, std::int32_t cy)
        {
            const std::uint32_t slot
                = static_cast<std::uint32_t>(cx - originX) + PM::windowSide * static_cast<std::uint32_t>(cy - originY);
            const std::uint32_t order = nextCell++;
            const std::uint32_t at = PM::dataBase + order * side * side;
            words[PM::tableBase + slot] = at;
            return at;
        }

        void put(std::uint32_t cellStart, std::uint32_t tx, std::uint32_t ty, std::uint8_t cap, std::uint8_t max,
            std::uint8_t catchment)
        {
            words[cellStart + ty * side + tx] = PM::packTexel(cap, max, catchment, 0);
        }
    };

    // World position of texel (tx, ty) of cell (cx, cy).
    float worldX(std::int32_t cx, std::uint32_t tx)
    {
        return static_cast<float>(cx) * cellSize + static_cast<float>(tx * spacing);
    }
    float worldY(std::int32_t cy, std::uint32_t ty)
    {
        return static_cast<float>(cy) * cellSize + static_cast<float>(ty * spacing);
    }

    TEST(RenderPuddleMapGpu, the_layout_is_pinned)
    {
        EXPECT_EQ(PM::wordEnabled, 0u);
        EXPECT_EQ(PM::wordDebug, 1u);
        EXPECT_EQ(PM::wordOriginX, 2u);
        EXPECT_EQ(PM::wordOriginY, 3u);
        EXPECT_EQ(PM::wordWindowSide, 4u);
        EXPECT_EQ(PM::wordTexelsPerCell, 5u);
        EXPECT_EQ(PM::wordSpacing, 6u);
        EXPECT_EQ(PM::wordCellSize, 7u);
        EXPECT_EQ(PM::wordSupply, 8u);
        EXPECT_EQ(PM::wordAbsorption, 9u);
        EXPECT_EQ(PM::tableBase, 16u);
        EXPECT_EQ(PM::dataBase, 128u);
        EXPECT_EQ(PM::windowSide, 7u);
        // The table has room for the largest window (81 slots) before the data starts.
        EXPECT_LE(PM::tableBase + PM::maxWindowSide * PM::maxWindowSide, PM::dataBase);
        EXPECT_EQ(PM::bufferWords(7, 128), 128u + 49u * 128u * 128u);
        EXPECT_EQ(PM::bufferWords(7, 8), 128u + 49u * 64u);
    }

    TEST(RenderPuddleMapGpu, a_texel_word_holds_the_four_channels_in_four_bytes)
    {
        EXPECT_EQ(PM::packTexel(0x12, 0x34, 0x56, 0x78), 0x78563412u);
        EXPECT_EQ(PM::packTexel(255, 0, 0, 0), 0x000000FFu);
    }

    TEST(RenderPuddleMapGpu, floats_travel_as_their_bits)
    {
        for (float v : { 0.f, 0.5f, 1.f, -2.25f, 123456.789f })
            EXPECT_EQ(PM::bitsFloat(PM::floatBits(v)), v);
        EXPECT_EQ(PM::floatBits(1.f), 0x3F800000u);
    }

    TEST(RenderPuddleMapGpu, a_texel_is_found_by_its_global_coordinates)
    {
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 3, 5, 10, 20, 30);
        std::uint32_t word = 0;
        ASSERT_TRUE(PM::texelWord(b.words.data(), 3, 5, word));
        EXPECT_EQ(word, PM::packTexel(10, 20, 30, 0));
        ASSERT_TRUE(PM::texelWord(b.words.data(), 0, 0, word));
        EXPECT_EQ(word, 0u);
    }

    TEST(RenderPuddleMapGpu, a_texel_of_a_cell_with_negative_coordinates_is_found)
    {
        Buffer b;
        const std::uint32_t a = b.addCell(-1, -2);
        b.put(a, 7, 0, 1, 2, 3);
        std::uint32_t word = 0;
        // Cell (-1, -2) starts at global texel (-8, -16).
        ASSERT_TRUE(PM::texelWord(b.words.data(), -1, -16, word));
        EXPECT_EQ(word, PM::packTexel(1, 2, 3, 0));
        EXPECT_FALSE(PM::texelWord(b.words.data(), -9, -16, word)); // cell (-2, -2): not present
        EXPECT_FALSE(PM::texelWord(b.words.data(), 0, -16, word)); // cell (0, -2): not present
    }

    TEST(RenderPuddleMapGpu, a_cell_outside_the_window_is_not_present)
    {
        Buffer b;
        b.addCell(0, 0);
        std::uint32_t word = 99;
        EXPECT_FALSE(PM::texelWord(b.words.data(), 8 * 4, 0, word)); // cell (4, 0): past the window's east edge
        EXPECT_FALSE(PM::texelWord(b.words.data(), -8 * 4, 0, word)); // cell (-4, 0): before the origin
    }

    TEST(RenderPuddleMapGpu, a_pit_floor_wet_enough_is_under_water)
    {
        // Pit of capacity 12 units (code 192), kappa 16 (log2 = 4, code 60). Wetness 1: supply 0.5, absorbed 0.1,
        // free 0.4, level 0.4 * 16 = 6.4 units; the floor texel (capacity 12 = pit maximum) has depth 6.4.
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 4, 4, 192, 192, 60);
        const PM::Shade s = PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 1.f);
        EXPECT_TRUE(s.covered);
        EXPECT_FLOAT_EQ(s.capacity, 12.f);
        EXPECT_FLOAT_EQ(s.basinMax, 12.f);
        EXPECT_NEAR(s.kappa, 16.f, 1e-3f);
        EXPECT_NEAR(s.level, 6.4f, 1e-3f);
        EXPECT_NEAR(s.depth, 6.4f, 1e-3f);
        EXPECT_FLOAT_EQ(s.coverage, 1.f);
        EXPECT_EQ(s.cellX, 0);
        EXPECT_EQ(s.cellY, 0);
    }

    TEST(RenderPuddleMapGpu, a_shore_texel_stays_dry_until_the_level_reaches_it)
    {
        // The same pit; a texel whose ground is 8 units above the floor has capacity 12 - 8 = 4 (code 64).
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 4, 4, 192, 192, 60);
        b.put(a, 5, 4, 64, 192, 60);
        const PM::Shade dry = PM::shade(b.words.data(), worldX(0, 5), worldY(0, 4), 1.f);
        EXPECT_NEAR(dry.level, 6.4f, 1e-3f);
        EXPECT_FLOAT_EQ(dry.depth, 0.f); // the water is 6.4 above the floor, the ground here is 8 above it
        EXPECT_FLOAT_EQ(dry.coverage, 0.f);
        // More rain: a pit with kappa 64 (code 90) has level 25.6, capped at the pit maximum 12; depth 12 - 8 = 4.
        b.put(a, 4, 4, 192, 192, 90);
        b.put(a, 5, 4, 64, 192, 90);
        const PM::Shade wet = PM::shade(b.words.data(), worldX(0, 5), worldY(0, 4), 1.f);
        EXPECT_NEAR(wet.level, 12.f, 1e-3f);
        EXPECT_NEAR(wet.depth, 4.f, 1e-3f);
        EXPECT_FLOAT_EQ(wet.coverage, 1.f);
    }

    TEST(RenderPuddleMapGpu, between_two_texels_the_capacity_is_blended)
    {
        // Capacity 8 next to capacity 0: halfway the blended capacity is 4, so the depth cannot exceed 4.
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 2, 3, 128, 128, 90);
        const PM::Shade mid = PM::shade(b.words.data(), worldX(0, 2) + spacing / 2.f, worldY(0, 3), 1.f);
        EXPECT_NEAR(mid.capacity, 4.f, 1e-4f);
        EXPECT_NEAR(mid.basinMax, 8.f, 1e-4f); // the pit comes from the texel that has one
        EXPECT_LE(mid.depth, 4.f + 1e-4f);
        EXPECT_GT(mid.depth, 0.f);
        // Past the pit texel the blend reaches zero.
        const PM::Shade out = PM::shade(b.words.data(), worldX(0, 4), worldY(0, 3), 1.f);
        EXPECT_FLOAT_EQ(out.capacity, 0.f);
        EXPECT_FLOAT_EQ(out.depth, 0.f);
    }

    TEST(RenderPuddleMapGpu, the_blend_reaches_over_a_cell_border)
    {
        // The pit texel is the last of cell (0, 0); its neighbour to the east is in cell (1, 0), which is present and
        // empty: halfway between them the capacity is half of the pit's.
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.addCell(1, 0);
        b.put(a, side - 1, 2, 128, 128, 90);
        const float x = worldX(0, side - 1) + spacing / 2.f;
        const PM::Shade s = PM::shade(b.words.data(), x, worldY(0, 2), 1.f);
        EXPECT_NEAR(s.capacity, 4.f, 1e-4f);
        EXPECT_TRUE(s.covered);
        // The same position when the neighbour cell is absent: the missing cell counts as no capacity.
        Buffer c;
        const std::uint32_t d = c.addCell(0, 0);
        c.put(d, side - 1, 2, 128, 128, 90);
        EXPECT_NEAR(PM::shade(c.words.data(), x, worldY(0, 2), 1.f).capacity, 4.f, 1e-4f);
    }

    TEST(RenderPuddleMapGpu, a_position_in_an_absent_cell_is_not_covered)
    {
        Buffer b;
        b.addCell(0, 0);
        const PM::Shade s = PM::shade(b.words.data(), worldX(2, 1), worldY(0, 1), 1.f);
        EXPECT_FALSE(s.covered);
        EXPECT_FLOAT_EQ(s.depth, 0.f);
        EXPECT_FLOAT_EQ(s.coverage, 0.f);
        // A position outside the window altogether.
        EXPECT_FALSE(PM::shade(b.words.data(), 1e6f, -1e6f, 1.f).covered);
    }

    TEST(RenderPuddleMapGpu, a_buffer_marked_off_covers_nothing)
    {
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 4, 4, 192, 192, 60);
        b.words[PM::wordEnabled] = 0;
        EXPECT_FALSE(PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 1.f).covered);
    }

    TEST(RenderPuddleMapGpu, dry_ground_reports_the_capacity_and_no_water)
    {
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 4, 4, 192, 192, 60);
        const PM::Shade s = PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 0.f);
        EXPECT_TRUE(s.covered);
        EXPECT_FLOAT_EQ(s.capacity, 12.f);
        EXPECT_FLOAT_EQ(s.level, 0.f);
        EXPECT_FLOAT_EQ(s.depth, 0.f);
    }

    TEST(RenderPuddleMapGpu, the_ground_soaks_up_the_first_of_the_rain)
    {
        // Wetness 0.2: supply 0.1, exactly what the ground absorbs: no free water.
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 4, 4, 192, 192, 90);
        EXPECT_FLOAT_EQ(PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 0.2f).depth, 0.f);
        EXPECT_GT(PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 0.3f).depth, 0.f);
    }

    TEST(RenderPuddleMapGpu, the_depth_never_falls_as_the_rain_grows_and_never_passes_the_capacity)
    {
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        for (std::uint32_t tx = 0; tx < side; ++tx)
            b.put(a, tx, 2, static_cast<std::uint8_t>(tx * 24), 192, 40);
        for (std::uint32_t tx = 0; tx < side; ++tx)
        {
            float last = 0.f;
            for (float wet = 0.f; wet <= 1.0001f; wet += 0.05f)
            {
                const PM::Shade s = PM::shade(b.words.data(), worldX(0, tx), worldY(0, 2), wet);
                EXPECT_GE(s.depth, last - 1e-5f);
                EXPECT_LE(s.depth, s.capacity + 1e-5f);
                last = s.depth;
            }
        }
    }

    TEST(RenderPuddleMapGpu, ripples_are_gated_by_depth)
    {
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 4, 4, 192, 192, 90); // kappa 64
        // Wetness 1: free water 0.4, level 12 (full): far past the ripple limit.
        EXPECT_FLOAT_EQ(PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 1.f).rippleGate, 1.f);
        // Wetness 0.205: supply 0.1025, free 0.0025, level 0.16 units: a thin film, below the limit.
        const PM::Shade shallow = PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 0.205f);
        EXPECT_GT(shallow.depth, 0.f);
        EXPECT_LT(shallow.depth, PM::rippleDepthFrom);
        EXPECT_FLOAT_EQ(shallow.rippleGate, 0.f);
        // Between the two limits the gate is between 0 and 1.
        const PM::Shade mid = PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 0.22f);
        EXPECT_GT(mid.depth, PM::rippleDepthFrom);
        EXPECT_LT(mid.depth, PM::rippleDepthTo);
        EXPECT_GT(mid.rippleGate, 0.f);
        EXPECT_LT(mid.rippleGate, 1.f);
    }

    TEST(RenderPuddleMapGpu, the_debug_cell_colour_depends_on_the_cell_and_is_grey_where_nothing_covers)
    {
        Buffer b;
        b.addCell(0, 0);
        b.addCell(1, 0);
        const PM::Shade a = PM::shade(b.words.data(), worldX(0, 1), worldY(0, 1), 0.f);
        const PM::Shade c = PM::shade(b.words.data(), worldX(1, 1), worldY(0, 1), 0.f);
        const PM::Rgb ca = PM::debugColour(PM::DebugCell, a);
        const PM::Rgb cc = PM::debugColour(PM::DebugCell, c);
        EXPECT_TRUE(ca.r != cc.r || ca.g != cc.g || ca.b != cc.b);
        EXPECT_EQ(ca.r, PM::debugColour(PM::DebugCell, a).r); // deterministic
        const PM::Shade none = PM::shade(b.words.data(), worldX(2, 1), worldY(0, 1), 0.f);
        const PM::Rgb grey = PM::debugColour(PM::DebugCell, none);
        EXPECT_FLOAT_EQ(grey.r, grey.g);
        EXPECT_FLOAT_EQ(grey.g, grey.b);
        EXPECT_GT(grey.r, 0.f);
    }

    TEST(RenderPuddleMapGpu, the_debug_capacity_colour_grows_with_the_capacity)
    {
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 1, 1, 32, 192, 30);
        b.put(a, 2, 1, 160, 192, 30);
        const PM::Rgb low
            = PM::debugColour(PM::DebugCapacity, PM::shade(b.words.data(), worldX(0, 1), worldY(0, 1), 0.f));
        const PM::Rgb high
            = PM::debugColour(PM::DebugCapacity, PM::shade(b.words.data(), worldX(0, 2), worldY(0, 1), 0.f));
        EXPECT_LT(low.r, high.r);
        const PM::Rgb none
            = PM::debugColour(PM::DebugCapacity, PM::shade(b.words.data(), worldX(0, 5), worldY(0, 5), 0.f));
        EXPECT_FLOAT_EQ(none.r, 0.f);
    }

    TEST(RenderPuddleMapGpu, the_debug_level_colour_shows_the_depth)
    {
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 4, 4, 192, 192, 90);
        const PM::Rgb wet = PM::debugColour(PM::DebugLevel, PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 1.f));
        const PM::Rgb dry = PM::debugColour(PM::DebugLevel, PM::shade(b.words.data(), worldX(0, 4), worldY(0, 4), 0.f));
        EXPECT_GT(wet.g, dry.g);
    }

    TEST(RenderPuddleMapGpu, the_debug_setting_names_the_views)
    {
        std::uint32_t mode = 99;
        ASSERT_TRUE(PM::parseDebugMode("off", mode));
        EXPECT_EQ(mode, static_cast<std::uint32_t>(PM::DebugOff));
        ASSERT_TRUE(PM::parseDebugMode("capacity", mode));
        EXPECT_EQ(mode, static_cast<std::uint32_t>(PM::DebugCapacity));
        ASSERT_TRUE(PM::parseDebugMode("level", mode));
        EXPECT_EQ(mode, static_cast<std::uint32_t>(PM::DebugLevel));
        ASSERT_TRUE(PM::parseDebugMode("cell", mode));
        EXPECT_EQ(mode, static_cast<std::uint32_t>(PM::DebugCell));
        mode = 7;
        EXPECT_FALSE(PM::parseDebugMode("Capacity", mode)); // names are exact
        EXPECT_FALSE(PM::parseDebugMode("", mode));
        EXPECT_FALSE(PM::parseDebugMode("basins", mode));
        EXPECT_EQ(mode, 7u); // untouched on failure
    }

    TEST(RenderPuddleMapGpu, the_depth_tint_goes_from_one_to_the_deep_value)
    {
        EXPECT_FLOAT_EQ(PM::depthTint(0.f), 1.f);
        EXPECT_FLOAT_EQ(PM::depthTint(PM::deepDarkeningDepth), PM::deepDarkening);
        EXPECT_FLOAT_EQ(PM::depthTint(100.f), PM::deepDarkening);
        float last = 1.f;
        for (float d = 0.f; d <= 4.f; d += 0.25f)
        {
            EXPECT_LE(PM::depthTint(d), last + 1e-6f);
            last = PM::depthTint(d);
        }
    }

    TEST(RenderPuddleMapGpu, the_blend_weights_follow_the_fraction_in_both_directions)
    {
        // Sampling at (2.25, 3.75) texels: the four taps are (2,3), (3,3), (2,4), (3,4) with the weights
        // 0.75 * 0.25, 0.25 * 0.25, 0.75 * 0.75 and 0.25 * 0.75.
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 3, 3, 128, 128, 90); // capacity 8 in the tap with weight 0.0625
        const float x = worldX(0, 2) + 0.25f * spacing;
        const float y = worldY(0, 3) + 0.75f * spacing;
        EXPECT_NEAR(PM::shade(b.words.data(), x, y, 1.f).capacity, 0.5f, 1e-4f);
        b.put(a, 3, 3, 0, 0, 0);
        b.put(a, 2, 4, 128, 128, 90); // the tap with weight 0.5625
        EXPECT_NEAR(PM::shade(b.words.data(), x, y, 1.f).capacity, 4.5f, 1e-4f);
        b.put(a, 2, 4, 0, 0, 0);
        b.put(a, 3, 4, 128, 128, 90); // weight 0.1875
        EXPECT_NEAR(PM::shade(b.words.data(), x, y, 1.f).capacity, 1.5f, 1e-4f);
        b.put(a, 3, 4, 0, 0, 0);
        b.put(a, 2, 3, 128, 128, 90); // weight 0.1875
        EXPECT_NEAR(PM::shade(b.words.data(), x, y, 1.f).capacity, 1.5f, 1e-4f);
    }

    TEST(RenderPuddleMapGpu, the_pit_is_that_of_the_tap_that_adds_most_to_the_blend)
    {
        // A deep pit (capacity 12, kappa 2) in a tap with weight 0.25 and a shallower one (capacity 8, kappa 16) in the
        // tap with weight 0.75: 0.25 * 12 = 3 against 0.75 * 8 = 6, so the second one is the pit of the pixel.
        Buffer b;
        const std::uint32_t a = b.addCell(0, 0);
        b.put(a, 2, 3, 192, 192, 15);
        b.put(a, 3, 3, 128, 128, 60);
        const PM::Shade s = PM::shade(b.words.data(), worldX(0, 2) + 0.75f * spacing, worldY(0, 3), 1.f);
        EXPECT_NEAR(s.capacity, 0.25f * 12.f + 0.75f * 8.f, 1e-4f);
        EXPECT_NEAR(s.basinMax, 8.f, 1e-4f);
        EXPECT_NEAR(s.kappa, 16.f, 1e-3f);
    }
}
