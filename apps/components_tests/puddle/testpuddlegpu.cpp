// OpenEden (task 0095, stage W-d): tests of components/puddle/puddlegpu.*, which packs the window of loaded puddle
// cells into the storage buffer the shader reads (the layout is components/render/puddlemapgpu.hpp). Written from the
// spec before the code; generated cells only.

#include <gtest/gtest.h>

#include <algorithm>
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

    std::shared_ptr<Puddle::CellMap> makeCell(std::int32_t cx, std::int32_t cy, bool pits)
    {
        auto cell = std::make_shared<Puddle::CellMap>();
        cell->mCellX = cx;
        cell->mCellY = cy;
        cell->mSide = side;
        cell->mSpacing = static_cast<float>(spacing);
        const std::size_t n = side * side;
        cell->mTexelFlags.assign(n, 0);
        cell->mTop.assign(n, 0);
        if (pits)
        {
            cell->mCapacity.assign(n, 0);
            cell->mBasinMax.assign(n, 0);
            cell->mCatchment.assign(n, 0);
            cell->mBasinId.assign(n, Puddle::noBasin);
        }
        else
            cell->mFlags = Puddle::flagNoPits;
        return cell;
    }

    struct Fixture
    {
        std::map<std::pair<std::int32_t, std::int32_t>, std::shared_ptr<const Puddle::CellMap>> cells;
        std::vector<std::uint32_t> buffer
            = std::vector<std::uint32_t>(PM::bufferWords(PM::windowSide, side), 0xDEADBEEFu);

        Puddle::GpuWindowParams params() const
        {
            Puddle::GpuWindowParams p;
            p.mCentreX = 0;
            p.mCentreY = 0;
            p.mTexelsPerCell = side;
            p.mSpacingUnits = spacing;
            p.mCellSizeUnits = cellSize;
            p.mSupply = 0.5f;
            p.mAbsorption = 0.1f;
            p.mDebugMode = 0;
            return p;
        }

        Puddle::CellFinder finder() const
        {
            return [this](std::int32_t x, std::int32_t y) -> std::shared_ptr<const Puddle::CellMap> {
                const auto it = cells.find({ x, y });
                return it == cells.end() ? nullptr : it->second;
            };
        }
    };

    TEST(PuddleGpu, the_texels_that_fit_are_those_of_the_fixed_buffer)
    {
        EXPECT_TRUE(Puddle::gpuTexelsFit(8));
        EXPECT_TRUE(Puddle::gpuTexelsFit(128));
        EXPECT_FALSE(Puddle::gpuTexelsFit(129));
        EXPECT_FALSE(Puddle::gpuTexelsFit(256));
        EXPECT_FALSE(Puddle::gpuTexelsFit(0));
    }

    TEST(PuddleGpu, the_header_says_where_the_window_is_and_what_the_settings_are)
    {
        Fixture f;
        auto p = f.params();
        p.mCentreX = 5;
        p.mCentreY = -7;
        p.mDebugMode = 2;
        const Puddle::GpuPackResult r = Puddle::packWindow(f.buffer.data(), f.buffer.size(), p, f.finder());
        EXPECT_EQ(r.mPacked, 0u);
        EXPECT_EQ(f.buffer[PM::wordEnabled], 1u);
        EXPECT_EQ(f.buffer[PM::wordDebug], 2u);
        // The window reaches three cells either side of the centre.
        EXPECT_EQ(static_cast<std::int32_t>(f.buffer[PM::wordOriginX]), 2);
        EXPECT_EQ(static_cast<std::int32_t>(f.buffer[PM::wordOriginY]), -10);
        EXPECT_EQ(f.buffer[PM::wordWindowSide], PM::windowSide);
        EXPECT_EQ(f.buffer[PM::wordTexelsPerCell], side);
        EXPECT_EQ(f.buffer[PM::wordSpacing], spacing);
        EXPECT_EQ(f.buffer[PM::wordCellSize], cellSize);
        EXPECT_EQ(PM::bitsFloat(f.buffer[PM::wordSupply]), 0.5f);
        EXPECT_EQ(PM::bitsFloat(f.buffer[PM::wordAbsorption]), 0.1f);
    }

    TEST(PuddleGpu, the_buffer_is_cleared_before_it_is_filled)
    {
        Fixture f; // starts full of 0xDEADBEEF
        Puddle::packWindow(f.buffer.data(), f.buffer.size(), f.params(), f.finder());
        for (std::uint32_t slot = 0; slot < PM::maxWindowSide * PM::maxWindowSide; ++slot)
            EXPECT_EQ(f.buffer[PM::tableBase + slot], 0u);
        for (std::size_t i = 10; i < PM::tableBase; ++i)
            EXPECT_EQ(f.buffer[i], 0u);
        for (std::size_t i = PM::dataBase; i < f.buffer.size(); i += 997)
            EXPECT_EQ(f.buffer[i], 0u);
    }

    TEST(PuddleGpu, a_loaded_cell_gets_a_slot_and_its_texels_are_copied)
    {
        Fixture f;
        auto cell = makeCell(1, -1, true);
        cell->mCapacity[3 * side + 2] = 17;
        cell->mBasinMax[3 * side + 2] = 99;
        cell->mCatchment[3 * side + 2] = 45;
        cell->mTexelFlags[3 * side + 2] = 0x0B;
        f.cells[{ 1, -1 }] = cell;
        const Puddle::GpuPackResult r = Puddle::packWindow(f.buffer.data(), f.buffer.size(), f.params(), f.finder());
        EXPECT_EQ(r.mPacked, 1u);
        EXPECT_EQ(r.mSkipped, 0u);
        std::uint32_t word = 0;
        // Global texel of cell (1, -1), local (2, 3): (8 + 2, -8 + 3).
        ASSERT_TRUE(PM::texelWord(f.buffer.data(), 10, -5, word));
        EXPECT_EQ(word, PM::packTexel(17, 99, 45, 0x0B));
        ASSERT_TRUE(PM::texelWord(f.buffer.data(), 9, -5, word));
        EXPECT_EQ(word, 0u);
        // Every other cell is absent.
        EXPECT_FALSE(PM::texelWord(f.buffer.data(), 0, 0, word));
        EXPECT_FALSE(PM::texelWord(f.buffer.data(), 16, -5, word));
    }

    TEST(PuddleGpu, cells_at_the_corners_of_the_window_are_found_and_those_beyond_it_are_ignored)
    {
        Fixture f;
        for (const auto& c :
            std::vector<std::pair<int, int>>{ { -3, -3 }, { 3, -3 }, { -3, 3 }, { 3, 3 }, { 4, 0 }, { 0, -4 } })
            f.cells[c] = makeCell(c.first, c.second, true);
        const Puddle::GpuPackResult r = Puddle::packWindow(f.buffer.data(), f.buffer.size(), f.params(), f.finder());
        EXPECT_EQ(r.mPacked, 4u); // (4, 0) and (0, -4) are outside the 7 x 7 window
        std::uint32_t word = 0;
        EXPECT_TRUE(PM::texelWord(f.buffer.data(), -3 * static_cast<int>(side), -3 * static_cast<int>(side), word));
        EXPECT_TRUE(PM::texelWord(f.buffer.data(), 3 * static_cast<int>(side), 3 * static_cast<int>(side), word));
        EXPECT_FALSE(PM::texelWord(f.buffer.data(), 4 * static_cast<int>(side), 0, word));
    }

    TEST(PuddleGpu, a_cell_without_pits_is_present_and_empty)
    {
        Fixture f;
        f.cells[{ 0, 0 }] = makeCell(0, 0, false);
        const Puddle::GpuPackResult r = Puddle::packWindow(f.buffer.data(), f.buffer.size(), f.params(), f.finder());
        EXPECT_EQ(r.mPacked, 1u);
        std::uint32_t word = 1234;
        ASSERT_TRUE(PM::texelWord(f.buffer.data(), 3, 3, word));
        EXPECT_EQ(word, 0u);
        EXPECT_TRUE(PM::shade(f.buffer.data(), 100.f, 100.f, 1.f).covered);
    }

    TEST(PuddleGpu, a_cell_of_another_size_is_skipped_and_counted)
    {
        Fixture f;
        auto cell = makeCell(0, 0, true);
        cell->mSide = 16;
        cell->mCapacity.assign(16 * 16, 0);
        cell->mBasinMax.assign(16 * 16, 0);
        cell->mCatchment.assign(16 * 16, 0);
        cell->mTexelFlags.assign(16 * 16, 0);
        f.cells[{ 0, 0 }] = cell;
        auto bad = makeCell(1, 0, true);
        bad->mCapacity.resize(5); // channels that do not match the side
        f.cells[{ 1, 0 }] = bad;
        const Puddle::GpuPackResult r = Puddle::packWindow(f.buffer.data(), f.buffer.size(), f.params(), f.finder());
        EXPECT_EQ(r.mPacked, 0u);
        EXPECT_EQ(r.mSkipped, 2u);
        std::uint32_t word = 0;
        EXPECT_FALSE(PM::texelWord(f.buffer.data(), 0, 0, word));
        EXPECT_FALSE(PM::shade(f.buffer.data(), 10.f, 10.f, 1.f).covered);
    }

    TEST(PuddleGpu, a_buffer_too_small_for_the_window_is_left_off)
    {
        Fixture f;
        f.cells[{ 0, 0 }] = makeCell(0, 0, true);
        std::vector<std::uint32_t> small(PM::dataBase + 10, 7u);
        const Puddle::GpuPackResult r = Puddle::packWindow(small.data(), small.size(), f.params(), f.finder());
        EXPECT_EQ(r.mPacked, 0u);
        EXPECT_EQ(small[PM::wordEnabled], 0u);
    }

    TEST(PuddleGpu, off_is_a_zeroed_header)
    {
        std::vector<std::uint32_t> b(PM::dataBase + 4, 0xFFFFFFFFu);
        Puddle::packOff(b.data(), b.size());
        for (std::size_t i = 0; i < PM::dataBase; ++i)
            EXPECT_EQ(b[i], 0u);
        EXPECT_FALSE(PM::shade(b.data(), 0.f, 0.f, 1.f).covered);
    }

    TEST(PuddleGpu, the_shader_maths_over_a_packed_window_gives_the_hand_values)
    {
        // A pit of capacity 12 and kappa 16 in cell (0, 0): at wetness 1 the level is 0.4 * 16 = 6.4 units.
        Fixture f;
        auto cell = makeCell(0, 0, true);
        cell->mCapacity[4 * side + 4] = 192;
        cell->mBasinMax[4 * side + 4] = 192;
        cell->mCatchment[4 * side + 4] = 60;
        f.cells[{ 0, 0 }] = cell;
        Puddle::packWindow(f.buffer.data(), f.buffer.size(), f.params(), f.finder());
        const PM::Shade s = PM::shade(f.buffer.data(), 4.f * spacing, 4.f * spacing, 1.f);
        EXPECT_TRUE(s.covered);
        EXPECT_NEAR(s.depth, 6.4f, 1e-3f);
        // The same texel in a window centred on the cell next door.
        auto p = f.params();
        p.mCentreX = 2;
        Puddle::packWindow(f.buffer.data(), f.buffer.size(), p, f.finder());
        EXPECT_NEAR(PM::shade(f.buffer.data(), 4.f * spacing, 4.f * spacing, 1.f).depth, 6.4f, 1e-3f);
        p.mCentreX = 4; // cell (0, 0) is now 4 cells west of the centre: outside
        Puddle::packWindow(f.buffer.data(), f.buffer.size(), p, f.finder());
        EXPECT_FALSE(PM::shade(f.buffer.data(), 4.f * spacing, 4.f * spacing, 1.f).covered);
    }

    TEST(PuddleGpu, packing_twice_gives_the_same_words)
    {
        Fixture f;
        auto cell = makeCell(-2, 1, true);
        for (std::size_t i = 0; i < side * side; i += 3)
        {
            cell->mCapacity[i] = static_cast<std::uint8_t>(i);
            cell->mBasinMax[i] = static_cast<std::uint8_t>(i + 1);
            cell->mCatchment[i] = static_cast<std::uint8_t>(i + 2);
        }
        f.cells[{ -2, 1 }] = cell;
        f.cells[{ 0, 0 }] = makeCell(0, 0, false);
        Puddle::packWindow(f.buffer.data(), f.buffer.size(), f.params(), f.finder());
        const std::vector<std::uint32_t> first = f.buffer;
        std::fill(f.buffer.begin(), f.buffer.end(), 0x12345678u);
        Puddle::packWindow(f.buffer.data(), f.buffer.size(), f.params(), f.finder());
        EXPECT_EQ(first, f.buffer);
    }
}
