// OpenEden (task 0095, stage W-c): the puddle map's baked tops in the GPU buffer, and the shader's height test. The
// layout is components/render/puddlemapgpu.hpp; components/puddle/puddlegpu.* fills it. Generated cells only.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <components/puddle/puddlegpu.hpp>
#include <components/render/puddlemapgpu.hpp>

namespace
{
    namespace PM = Render::PuddleMap;

    constexpr std::uint32_t side = 8;
    constexpr std::uint32_t spacing = 64;
    constexpr std::uint32_t cellSize = side * spacing;

    std::shared_ptr<Puddle::CellMap> makeCell(std::int32_t cx, std::int32_t cy, float base)
    {
        auto cell = std::make_shared<Puddle::CellMap>();
        cell->mCellX = cx;
        cell->mCellY = cy;
        cell->mSide = side;
        cell->mSpacing = static_cast<float>(spacing);
        cell->mHeightBase = base;
        const std::size_t n = side * side;
        cell->mTexelFlags.assign(n, 0);
        cell->mTop.assign(n, 0);
        cell->mCapacity.assign(n, 0);
        cell->mBasinMax.assign(n, 0);
        cell->mCatchment.assign(n, 0);
        cell->mBasinId.assign(n, Puddle::noBasin);
        return cell;
    }

    struct Fixture
    {
        std::map<std::pair<std::int32_t, std::int32_t>, std::shared_ptr<const Puddle::CellMap>> cells;
        std::vector<std::uint32_t> buffer
            = std::vector<std::uint32_t>(PM::bufferWordsWithTops(PM::windowSide, side), 0u);

        Puddle::GpuWindowParams params() const
        {
            Puddle::GpuWindowParams p;
            p.mTexelsPerCell = side;
            p.mSpacingUnits = spacing;
            p.mCellSizeUnits = cellSize;
            p.mSupply = 0.5f;
            p.mAbsorption = 0.1f;
            return p;
        }

        bool keepNoise = false;

        void pack()
        {
            Puddle::GpuWindowParams p = params();
            p.mKeepStaticNoise = keepNoise;
            Puddle::packWindow(buffer.data(), buffer.size(), p, [this](std::int32_t x, std::int32_t y) {
                const auto it = cells.find({ x, y });
                return it == cells.end() ? std::shared_ptr<const Puddle::CellMap>() : it->second;
            });
        }
    };

    float worldX(int cell, int texel)
    {
        return static_cast<float>(cell * static_cast<int>(cellSize) + texel * static_cast<int>(spacing));
    }

    // One pit texel of capacity 4 units and the pit's maximum 4 at (3, 3) of cell (0, 0), and a flat 4 x 4 block around
    // it so the bilinear blend sees the same capacity.
    void fillPit(Puddle::CellMap& c, std::uint8_t flags, std::uint16_t top)
    {
        for (std::uint32_t y = 2; y < 6; ++y)
        {
            for (std::uint32_t x = 2; x < 6; ++x)
            {
                const std::size_t i = y * side + x;
                c.mCapacity[i] = 64; // 4 units
                c.mBasinMax[i] = 64;
                c.mCatchment[i] = 60; // kappa 16
                c.mTexelFlags[i] = flags;
                c.mTop[i] = top;
            }
        }
    }

    TEST(PuddleMapTops, the_buffer_has_room_for_the_tops_after_the_texels)
    {
        const std::size_t texels = PM::bufferWords(PM::windowSide, side);
        const std::size_t cells = static_cast<std::size_t>(PM::windowSide) * PM::windowSide;
        EXPECT_EQ(PM::bufferWordsWithTops(PM::windowSide, side), texels + cells + cells * side * side);
        EXPECT_EQ(PM::bufferWordsWithTops(PM::windowSide, 128),
            PM::bufferWords(PM::windowSide, 128) + 49u + 49u * 128u * 128u);
        EXPECT_EQ(PM::wordTopsBase, 10u);
        EXPECT_EQ(PM::texelStaticBit, static_cast<std::uint32_t>(Puddle::texelStatic));
    }

    TEST(PuddleMapTops, the_packer_writes_a_base_a_cell_and_a_top_word_a_texel)
    {
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        c->mTop[3 * side + 2] = 24;
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const std::uint32_t topsBase = f.buffer[PM::wordTopsBase];
        EXPECT_EQ(topsBase, PM::dataBase + 49u * side * side);
        // Window centred on (0, 0): cell (0, 0) is slot 3 + 7 * 3.
        const std::uint32_t slot = 3 + 7 * 3;
        EXPECT_EQ(f.buffer[topsBase + slot], PM::floatBits(100.f));
        EXPECT_EQ(f.buffer[topsBase + 49u + slot * side * side + 3 * side + 2], 24u);
        float z = 0.f;
        ASSERT_TRUE(PM::texelTop(f.buffer.data(), 2, 3, z));
        EXPECT_FLOAT_EQ(z, 103.f); // 100 + 24 / 8
        EXPECT_FALSE(PM::texelTop(f.buffer.data(), 2 + 4 * static_cast<int>(side), 3, z)); // cell (4, 0): not loaded
    }

    TEST(PuddleMapTops, a_buffer_without_room_for_tops_still_gets_its_texels_and_says_there_are_no_tops)
    {
        Fixture f;
        f.buffer.assign(PM::bufferWords(PM::windowSide, side), 0u);
        auto c = makeCell(0, 0, 100.f);
        fillPit(*c, 0, 0);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        EXPECT_EQ(f.buffer[PM::wordEnabled], 1u);
        EXPECT_EQ(f.buffer[PM::wordTopsBase], 0u);
        std::uint32_t word = 0;
        ASSERT_TRUE(PM::texelWord(f.buffer.data(), 3, 3, word));
        EXPECT_EQ(word & 0xFFu, 64u);
        float z = 0.f;
        EXPECT_FALSE(PM::texelTop(f.buffer.data(), 3, 3, z));
    }

    TEST(PuddleMapTops, a_cell_whose_top_channel_has_the_wrong_size_is_skipped)
    {
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        c->mTop.resize(5);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        std::uint32_t word = 0;
        EXPECT_FALSE(PM::texelWord(f.buffer.data(), 3, 3, word));
    }

    TEST(PuddleMapTops, without_a_height_test_the_shade_is_what_it_was)
    {
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        fillPit(*c, 0, 0);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const PM::Shade old = PM::shade(f.buffer.data(), worldX(0, 3), worldX(0, 3), 1.f);
        const PM::Shade now
            = PM::shadeAt(f.buffer.data(), worldX(0, 3), worldX(0, 3), 5000.f, 1.f, 0.f, PM::ShadeMode::NoHeightTest);
        EXPECT_TRUE(old.covered);
        EXPECT_TRUE(now.covered);
        EXPECT_FLOAT_EQ(old.depth, now.depth);
        EXPECT_FLOAT_EQ(old.capacity, now.capacity);
    }

    TEST(PuddleMapTops, the_terrain_is_tested_only_where_the_column_holds_a_static_surface)
    {
        Fixture f;
        auto ground = makeCell(0, 0, 100.f);
        fillPit(*ground, 0, 0);
        f.cells[{ 0, 0 }] = ground;
        f.pack();
        // A column of plain ground takes the puddle whatever the pixel's height: W-d's behaviour.
        const PM::Shade a
            = PM::shadeAt(f.buffer.data(), worldX(0, 3), worldX(0, 3), 100.f, 1.f, 1.f, PM::ShadeMode::Terrain);
        const PM::Shade b
            = PM::shadeAt(f.buffer.data(), worldX(0, 3), worldX(0, 3), 160.f, 1.f, 1.f, PM::ShadeMode::Terrain);
        EXPECT_TRUE(a.covered);
        EXPECT_TRUE(b.covered);

        Fixture g;
        auto road = makeCell(0, 0, 100.f);
        fillPit(*road, Puddle::texelStatic, 24); // a road at 103
        g.cells[{ 0, 0 }] = road;
        g.pack();
        // A terrain pixel at the ground under the road does not take the road's puddle; one at the road's height does.
        const PM::Shade under
            = PM::shadeAt(g.buffer.data(), worldX(0, 3), worldX(0, 3), 100.f, 1.f, 1.f, PM::ShadeMode::Terrain);
        const PM::Shade on
            = PM::shadeAt(g.buffer.data(), worldX(0, 3), worldX(0, 3), 103.f, 1.f, 1.f, PM::ShadeMode::Terrain);
        EXPECT_FALSE(under.covered);
        EXPECT_TRUE(under.loaded);
        EXPECT_TRUE(on.covered);
        EXPECT_GT(on.depth, 0.f);
    }

    TEST(PuddleMapTops, an_object_takes_the_map_only_at_the_height_of_the_column_and_facing_up)
    {
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        fillPit(*c, Puddle::texelStatic, 24); // 103
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const auto at = [&](float z, float nz) {
            return PM::shadeAt(f.buffer.data(), worldX(0, 3), worldX(0, 3), z, 1.f, nz, PM::ShadeMode::Object);
        };
        EXPECT_TRUE(at(103.f, 1.f).covered);
        EXPECT_TRUE(at(103.f, 0.8f).covered);
        EXPECT_FALSE(at(103.f, 0.3f).covered); // a wall
        EXPECT_FALSE(at(108.f, 1.f).covered); // a roof above it
        EXPECT_FALSE(at(90.f, 1.f).covered); // below the surface
        EXPECT_TRUE(at(103.f, 1.f).loaded);
    }

    TEST(PuddleMapTops, an_object_on_plain_ground_is_tested_against_the_ground)
    {
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        fillPit(*c, 0, 8); // the ground at 101
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const auto at = [&](float z) {
            return PM::shadeAt(f.buffer.data(), worldX(0, 3), worldX(0, 3), z, 1.f, 1.f, PM::ShadeMode::Object);
        };
        EXPECT_TRUE(at(101.f).covered);
        EXPECT_FALSE(at(120.f).covered); // a rock's top, a house's roof
    }

    TEST(PuddleMapTops, the_tolerance_is_the_documented_one)
    {
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        fillPit(*c, Puddle::texelStatic, 24);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const auto at = [&](float z) {
            return PM::shadeAt(f.buffer.data(), worldX(0, 3), worldX(0, 3), z, 1.f, 1.f, PM::ShadeMode::Object);
        };
        EXPECT_TRUE(at(103.f + PM::zTolerance - 0.01f).covered);
        EXPECT_TRUE(at(103.f - PM::zTolerance + 0.01f).covered);
        EXPECT_FALSE(at(103.f + PM::zTolerance + 0.05f).covered);
        EXPECT_FALSE(at(103.f - PM::zTolerance - 0.05f).covered);
        EXPECT_GT(PM::zTolerance, 0.5f);
        EXPECT_LT(PM::zTolerance, 4.f);
        EXPECT_GT(PM::objectMinNormalZ, 0.5f);
        EXPECT_LT(PM::objectMinNormalZ, 0.95f);
    }

    TEST(PuddleMapTops, a_sloping_surface_is_followed_between_the_texels)
    {
        // The tops rise by 4 units per texel (a plane): the blended top at half a texel is 2 units higher than the
        // texel's, and an object lying on the plane passes the test.
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        for (std::uint32_t y = 0; y < side; ++y)
        {
            for (std::uint32_t x = 0; x < side; ++x)
            {
                const std::size_t i = y * side + x;
                c->mCapacity[i] = 64;
                c->mBasinMax[i] = 64;
                c->mCatchment[i] = 60;
                c->mTexelFlags[i] = Puddle::texelStatic;
                c->mTop[i] = static_cast<std::uint16_t>(8 * 4 * x); // 4 units per texel in x
            }
        }
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const float x = worldX(0, 3) + 0.5f * spacing;
        const float planeZ = 100.f + 4.f * 3.5f;
        const PM::Shade s = PM::shadeAt(f.buffer.data(), x, worldX(0, 3), planeZ, 1.f, 1.f, PM::ShadeMode::Object);
        EXPECT_TRUE(s.covered);
        EXPECT_NEAR(s.top, planeZ, 1e-3f);
        EXPECT_FALSE(
            PM::shadeAt(f.buffer.data(), x, worldX(0, 3), planeZ + 5.f, 1.f, 1.f, PM::ShadeMode::Object).covered);
    }

    TEST(PuddleMapTops, at_the_edge_of_a_loaded_cell_the_top_is_the_loaded_taps_only)
    {
        // Half a texel past the last texel of cell (0, 0), cell (1, 0) is not loaded: only the two taps of cell (0, 0)
        // count, and the blended top is their top, not half of it.
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        for (std::uint32_t i = 0; i < side * side; ++i)
        {
            c->mCapacity[i] = 64;
            c->mBasinMax[i] = 64;
            c->mCatchment[i] = 60;
            c->mTexelFlags[i] = Puddle::texelStatic;
            c->mTop[i] = 24; // 103
        }
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const float x = worldX(0, side - 1) + 0.5f * spacing;
        const PM::Shade s = PM::shadeAt(f.buffer.data(), x, worldX(0, 3), 103.f, 1.f, 1.f, PM::ShadeMode::Object);
        EXPECT_TRUE(s.loaded);
        EXPECT_TRUE(s.covered);
        EXPECT_NEAR(s.top, 103.f, 1e-3f);
        EXPECT_NEAR(s.staticShare, 0.5f, 1e-4f);
    }

    // A static column with no baked pit: the bake cannot see the micro relief of paving, so with the option the noise
    // puddles stay on objects there.
    struct StaticNoiseFixture : Fixture
    {
        explicit StaticNoiseFixture(bool keep)
        {
            auto c = makeCell(0, 0, 100.f);
            for (std::uint32_t i = 0; i < side * side; ++i)
            {
                c->mTexelFlags[i] = Puddle::texelStatic;
                c->mTop[i] = 24; // 103, no capacity: a flat road
            }
            // And a pit on a static surface in the corner, and a plain ground pit next to it.
            for (std::uint32_t y = 0; y < 2; ++y)
            {
                for (std::uint32_t x = 0; x < 2; ++x)
                {
                    const std::size_t i = y * side + x;
                    c->mCapacity[i] = 64;
                    c->mBasinMax[i] = 64;
                    c->mCatchment[i] = 60;
                }
            }
            auto g = makeCell(1, 0, 100.f);
            for (std::uint32_t i = 0; i < side * side; ++i)
            {
                g->mTop[i] = 8; // plain ground at 101
            }
            cells[{ 0, 0 }] = c;
            cells[{ 1, 0 }] = g;
            keepNoise = keep;
            pack();
        }
        PM::Shade object(float x, float y, float z) const
        {
            return PM::shadeAt(buffer.data(), x, y, z, 1.f, 1.f, PM::ShadeMode::Object);
        }
    };

    TEST(PuddleMapTops, with_the_option_a_static_column_without_a_pit_keeps_the_noise_puddles_on_objects)
    {
        StaticNoiseFixture f(true);
        const PM::Shade road = f.object(worldX(0, 5), worldX(0, 4), 103.f);
        EXPECT_TRUE(road.loaded);
        EXPECT_FALSE(road.covered);
        EXPECT_NEAR(road.staticShare, 1.f, 1e-5f);
        // The pit on the static surface is the map's.
        const PM::Shade pit = f.object(worldX(0, 0), worldX(0, 0), 103.f);
        EXPECT_TRUE(pit.covered);
        EXPECT_GT(pit.depth, 0.f);
        // Plain ground at its own height is the map's too: dry there, as in W-d.
        const PM::Shade ground = f.object(worldX(1, 4), worldX(0, 4), 101.f);
        EXPECT_TRUE(ground.covered);
        EXPECT_FLOAT_EQ(ground.depth, 0.f);
    }

    TEST(PuddleMapTops, the_option_needs_a_mostly_static_column)
    {
        // Three quarters of the way from the road's last texel to the ground cell's first: the static taps weigh 0.25.
        StaticNoiseFixture f(true);
        const float x = worldX(0, side - 1) + 0.75f * spacing;
        const PM::Shade edge = f.object(x, worldX(0, 4), 101.5f);
        EXPECT_NEAR(edge.staticShare, 0.25f, 1e-4f);
        EXPECT_TRUE(edge.covered);
        // And one quarter of the way: the static taps weigh 0.75, and the road keeps the noise.
        const float y = worldX(0, side - 1) + 0.25f * spacing;
        const PM::Shade inside = f.object(y, worldX(0, 4), 102.5f);
        EXPECT_NEAR(inside.staticShare, 0.75f, 1e-4f);
        EXPECT_FALSE(inside.covered);
    }

    TEST(PuddleMapTops, without_the_option_a_flat_road_is_dry_under_the_map)
    {
        StaticNoiseFixture f(false);
        const PM::Shade road = f.object(worldX(0, 5), worldX(0, 4), 103.f);
        EXPECT_TRUE(road.covered);
        EXPECT_FLOAT_EQ(road.depth, 0.f);
    }

    TEST(PuddleMapTops, the_option_is_a_header_word_the_packer_writes_and_the_terrain_ignores)
    {
        EXPECT_EQ(PM::wordOptions, 11u);
        EXPECT_EQ(PM::optionKeepStaticNoise, 1u);
        StaticNoiseFixture on(true);
        EXPECT_EQ(on.buffer[PM::wordOptions], 1u);
        StaticNoiseFixture off(false);
        EXPECT_EQ(off.buffer[PM::wordOptions], 0u);
        // A terrain pixel at the road's height is not an object: the option does not change it.
        const PM::Shade t
            = PM::shadeAt(on.buffer.data(), worldX(0, 5), worldX(0, 4), 103.f, 1.f, 1.f, PM::ShadeMode::Terrain);
        EXPECT_TRUE(t.covered);
    }

    TEST(PuddleMapTops, the_static_share_is_the_weight_of_the_static_texels)
    {
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        for (std::uint32_t x = 2; x < 6; ++x)
        {
            for (std::uint32_t y = 2; y < 6; ++y)
            {
                const std::size_t i = y * side + x;
                c->mCapacity[i] = 64;
                c->mBasinMax[i] = 64;
                c->mCatchment[i] = 60;
                c->mTexelFlags[i] = x >= 4 ? Puddle::texelStatic : 0;
            }
        }
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const PM::Shade left
            = PM::shadeAt(f.buffer.data(), worldX(0, 2), worldX(0, 3), 100.f, 1.f, 1.f, PM::ShadeMode::NoHeightTest);
        const PM::Shade mid = PM::shadeAt(
            f.buffer.data(), worldX(0, 3) + 0.5f * spacing, worldX(0, 3), 100.f, 1.f, 1.f, PM::ShadeMode::NoHeightTest);
        const PM::Shade right
            = PM::shadeAt(f.buffer.data(), worldX(0, 4), worldX(0, 3), 100.f, 1.f, 1.f, PM::ShadeMode::NoHeightTest);
        EXPECT_NEAR(left.staticShare, 0.f, 1e-5f);
        EXPECT_NEAR(mid.staticShare, 0.5f, 1e-4f);
        EXPECT_NEAR(right.staticShare, 1.f, 1e-5f);
    }

    TEST(PuddleMapTops, a_map_without_tops_cannot_test_the_height_and_covers_the_cell)
    {
        Fixture f;
        f.buffer.assign(PM::bufferWords(PM::windowSide, side), 0u);
        auto c = makeCell(0, 0, 100.f);
        fillPit(*c, Puddle::texelStatic, 24);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        EXPECT_TRUE(
            PM::shadeAt(f.buffer.data(), worldX(0, 3), worldX(0, 3), 9999.f, 1.f, 1.f, PM::ShadeMode::Object).covered);
    }

    TEST(PuddleMapTops, a_cell_that_is_not_loaded_is_neither_loaded_nor_covered)
    {
        Fixture f;
        f.pack();
        const PM::Shade s = PM::shadeAt(f.buffer.data(), 1e5f, 1e5f, 0.f, 1.f, 1.f, PM::ShadeMode::Object);
        EXPECT_FALSE(s.loaded);
        EXPECT_FALSE(s.covered);
    }

    TEST(PuddleMapTops, the_statics_debug_view_is_named_and_marks_static_columns)
    {
        std::uint32_t mode = 0;
        ASSERT_TRUE(PM::parseDebugMode("statics", mode));
        EXPECT_EQ(mode, static_cast<std::uint32_t>(PM::DebugStatics));
        EXPECT_FALSE(PM::parseDebugMode("static", mode));
        Fixture f;
        auto c = makeCell(0, 0, 100.f);
        fillPit(*c, Puddle::texelStatic, 24);
        f.cells[{ 0, 0 }] = c;
        auto d = makeCell(1, 0, 100.f);
        fillPit(*d, 0, 0);
        f.cells[{ 1, 0 }] = d;
        f.pack();
        const PM::Shade stat
            = PM::shadeAt(f.buffer.data(), worldX(0, 3), worldX(0, 3), 103.f, 0.f, 1.f, PM::ShadeMode::NoHeightTest);
        const PM::Shade plain
            = PM::shadeAt(f.buffer.data(), worldX(1, 3), worldX(0, 3), 100.f, 0.f, 1.f, PM::ShadeMode::NoHeightTest);
        const PM::Rgb a = PM::debugColour(PM::DebugStatics, stat);
        const PM::Rgb b = PM::debugColour(PM::DebugStatics, plain);
        EXPECT_GT(a.r, b.r);
        EXPECT_GT(a.b, b.b);
        EXPECT_GT(a.r, 0.5f);
    }
}
