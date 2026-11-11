#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>

#include <components/puddle/puddlestatics.hpp>

// Task 0095, stage W-c: the filter that turns raw captured tops into static surfaces, the capture file and its folder.
// Generated scenes only (no game data).

namespace
{
    using namespace Puddle;

    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr std::uint32_t side = 16;
    constexpr float spacing = 64.f;

    struct Scene
    {
        std::vector<float> terrain = std::vector<float>(side * side, 0.f);
        std::vector<float> raw = std::vector<float>(side * side, nan);

        // A box of surface: [x0, x1) x [y0, y1) at the height z.
        void box(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, float z)
        {
            for (std::uint32_t y = y0; y < y1; ++y)
                for (std::uint32_t x = x0; x < x1; ++x)
                    raw[y * side + x] = z;
        }
        StaticFilterResult run() const { return applyStatics(terrain, raw, side, spacing); }
        bool isStatic(const StaticFilterResult& r, std::uint32_t x, std::uint32_t y) const
        {
            return r.mIsStatic[y * side + x] != 0;
        }
    };

    std::filesystem::path scratch(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / ("puddlestatics_" + name);
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        return path;
    }

    // ---- the filter -------------------------------------------------------------------------------------------

    TEST(PuddleStaticsFilter, a_flat_road_above_the_ground_is_a_static_surface)
    {
        Scene s;
        s.box(2, 6, 14, 9, 3.f); // 12 x 3 texels, 3 units above the ground
        const StaticFilterResult r = s.run();
        EXPECT_EQ(r.mAccepted, 36u);
        for (std::uint32_t y = 0; y < side; ++y)
        {
            for (std::uint32_t x = 0; x < side; ++x)
            {
                const bool road = x >= 2 && x < 14 && y >= 6 && y < 9;
                EXPECT_EQ(s.isStatic(r, x, y), road) << x << "," << y;
                EXPECT_FLOAT_EQ(r.mHeights[y * side + x], road ? 3.f : 0.f);
            }
        }
    }

    TEST(PuddleStaticsFilter, nothing_captured_gives_the_terrain_back)
    {
        Scene s;
        const StaticFilterResult r = s.run();
        EXPECT_EQ(r.mAccepted, 0u);
        EXPECT_EQ(r.mCandidates, 0u);
        EXPECT_EQ(r.mHeights, s.terrain);
    }

    TEST(PuddleStaticsFilter, a_top_at_or_barely_above_the_ground_is_the_ground)
    {
        Scene s;
        s.box(2, 2, 12, 12, 0.4f); // below the minimum rise
        EXPECT_EQ(s.run().mAccepted, 0u);
        s.box(2, 2, 12, 12, -5.f); // buried
        EXPECT_EQ(s.run().mAccepted, 0u);
        s.box(2, 2, 12, 12, staticMinRise);
        EXPECT_GT(s.run().mAccepted, 0u);
    }

    TEST(PuddleStaticsFilter, a_thin_fence_is_not_a_surface_however_long)
    {
        Scene s;
        s.box(0, 8, 16, 9, 20.f); // one texel wide and 16 long: the area alone (16) would let it through
        const StaticFilterResult r = s.run();
        EXPECT_EQ(r.mAccepted, 0u);
        EXPECT_EQ(r.mCandidates, 16u);
    }

    TEST(PuddleStaticsFilter, parts_that_touch_at_a_corner_are_one_surface)
    {
        // Two 3 x 3 squares (9 texels each, under the area minimum) that meet only diagonally are one group of 18.
        Scene s;
        s.box(2, 2, 5, 5, 10.f);
        s.box(5, 5, 8, 8, 10.f);
        const StaticFilterResult r = s.run();
        EXPECT_EQ(r.mAccepted, 18u);
        // Apart, they are clutter.
        Scene t;
        t.box(2, 2, 5, 5, 10.f);
        t.box(6, 6, 9, 9, 10.f);
        EXPECT_EQ(t.run().mAccepted, 0u);
    }

    TEST(PuddleStaticsFilter, a_column_of_tiny_objects_is_clutter)
    {
        Scene s;
        s.box(3, 3, 5, 5, 30.f); // 2 x 2: wide enough but only 4 texels
        s.box(9, 9, 11, 11, 30.f);
        EXPECT_EQ(s.run().mAccepted, 0u);
    }

    TEST(PuddleStaticsFilter, the_smallest_accepted_surface_is_the_minimum_area)
    {
        Scene s;
        s.box(2, 2, 6, 6, 10.f); // 4 x 4 = 16 texels
        EXPECT_EQ(s.run().mAccepted, staticMinAreaTexels);
        Scene t;
        t.box(2, 2, 6, 5, 10.f); // 12 texels
        EXPECT_EQ(t.run().mAccepted, 0u);
    }

    TEST(PuddleStaticsFilter, a_steep_roof_is_not_a_surface_and_a_gentle_slope_is)
    {
        Scene steep;
        for (std::uint32_t y = 2; y < 12; ++y)
            for (std::uint32_t x = 2; x < 12; ++x)
                steep.raw[y * side + x] = 5.f + static_cast<float>(x) * spacing; // 45 degrees along x
        EXPECT_EQ(steep.run().mAccepted, 0u);

        Scene gentle;
        for (std::uint32_t y = 2; y < 12; ++y)
            for (std::uint32_t x = 2; x < 12; ++x)
                gentle.raw[y * side + x] = 5.f + static_cast<float>(x) * 0.2f * spacing; // about 11 degrees
        EXPECT_EQ(gentle.run().mAccepted, 100u);
    }

    TEST(PuddleStaticsFilter, the_slope_limit_is_the_documented_one)
    {
        // The step between neighbours may be staticSlopeLimit * spacing; a hair more is too steep.
        const float exact = staticSlopeLimit * spacing;
        Scene a;
        for (std::uint32_t y = 2; y < 12; ++y)
            for (std::uint32_t x = 2; x < 12; ++x)
                a.raw[y * side + x] = 5.f + static_cast<float>(x) * exact;
        EXPECT_EQ(a.run().mAccepted, 100u);
        Scene b;
        for (std::uint32_t y = 2; y < 12; ++y)
            for (std::uint32_t x = 2; x < 12; ++x)
                b.raw[y * side + x] = 5.f + static_cast<float>(x) * (exact + 0.5f);
        EXPECT_EQ(b.run().mAccepted, 0u);
    }

    TEST(PuddleStaticsFilter, a_deck_next_to_the_ground_keeps_its_edge_texels)
    {
        // The deck's neighbours at the edge are ground (not candidates), so the cliff there is no slope.
        Scene s;
        s.box(4, 4, 12, 12, 40.f);
        const StaticFilterResult r = s.run();
        EXPECT_EQ(r.mAccepted, 64u);
        EXPECT_TRUE(s.isStatic(r, 4, 4));
        EXPECT_TRUE(s.isStatic(r, 11, 11));
    }

    TEST(PuddleStaticsFilter, two_floors_at_different_heights_next_to_each_other_lose_their_shared_edge)
    {
        // A step of 40 units between two raised surfaces is steeper than the limit on both sides of the step, so the
        // two texels at it drop out; each floor keeps the rest.
        Scene s;
        s.box(2, 4, 8, 12, 40.f);
        s.box(8, 4, 14, 12, 80.f);
        const StaticFilterResult r = s.run();
        EXPECT_FALSE(s.isStatic(r, 7, 6));
        EXPECT_FALSE(s.isStatic(r, 8, 6));
        EXPECT_TRUE(s.isStatic(r, 5, 6));
        EXPECT_TRUE(s.isStatic(r, 11, 6));
        EXPECT_FLOAT_EQ(r.mHeights[6 * side + 5], 40.f);
        EXPECT_FLOAT_EQ(r.mHeights[6 * side + 11], 80.f);
    }

    TEST(PuddleStaticsFilter, a_bridge_deck_is_the_surface_and_the_ditch_under_it_keeps_its_terrain_height)
    {
        Scene s;
        for (std::uint32_t y = 0; y < side; ++y)
            for (std::uint32_t x = 6; x < 10; ++x)
                s.terrain[y * side + x] = -30.f; // a ditch along y, 4 texels wide
        s.box(0, 6, 16, 10, 8.f); // the deck across it, 4 texels wide
        const StaticFilterResult r = s.run();
        EXPECT_TRUE(s.isStatic(r, 8, 8));
        EXPECT_FLOAT_EQ(r.mHeights[8 * side + 8], 8.f);
        // Beside the deck the ditch is still the terrain.
        EXPECT_FALSE(s.isStatic(r, 8, 2));
        EXPECT_FLOAT_EQ(r.mHeights[2 * side + 8], -30.f);
    }

    TEST(PuddleStaticsFilter, a_non_finite_or_huge_top_is_not_a_candidate)
    {
        Scene s;
        s.box(2, 2, 12, 12, std::numeric_limits<float>::infinity());
        EXPECT_EQ(s.run().mCandidates, 0u);
        s.box(2, 2, 12, 12, 2.0e7f);
        EXPECT_EQ(s.run().mCandidates, 0u);
        s.box(2, 2, 12, 12, -std::numeric_limits<float>::infinity());
        EXPECT_EQ(s.run().mCandidates, 0u);
    }

    TEST(PuddleStaticsFilter, wrong_sizes_give_the_terrain_back_untouched)
    {
        Scene s;
        s.box(2, 2, 12, 12, 10.f);
        std::vector<float> shortRaw(side * side - 1, 10.f);
        const StaticFilterResult r = applyStatics(s.terrain, shortRaw, side, spacing);
        EXPECT_EQ(r.mAccepted, 0u);
        EXPECT_EQ(r.mHeights, s.terrain);
    }

    TEST(PuddleStaticsFilter, the_answer_does_not_depend_on_anything_but_the_arrays)
    {
        std::mt19937 rng(1234);
        for (int round = 0; round < 40; ++round)
        {
            Scene s;
            for (std::size_t i = 0; i < s.terrain.size(); ++i)
            {
                s.terrain[i] = static_cast<float>(rng() % 20) - 10.f;
                if (rng() % 3 != 0)
                    s.raw[i] = s.terrain[i] + static_cast<float>(rng() % 10) * 0.4f - 0.5f;
            }
            const StaticFilterResult a = s.run();
            const StaticFilterResult b = s.run();
            EXPECT_EQ(a.mHeights, b.mHeights);
            EXPECT_EQ(a.mIsStatic, b.mIsStatic);
        }
    }

    TEST(PuddleStaticsFilter, accepted_surfaces_obey_the_rules_on_random_scenes)
    {
        std::mt19937 rng(99);
        for (int round = 0; round < 60; ++round)
        {
            Scene s;
            for (std::size_t i = 0; i < s.terrain.size(); ++i)
                s.terrain[i] = static_cast<float>(rng() % 5);
            const int boxes = 1 + static_cast<int>(rng() % 5);
            for (int b = 0; b < boxes; ++b)
            {
                const std::uint32_t x0 = rng() % side;
                const std::uint32_t y0 = rng() % side;
                s.box(x0, y0, std::min(side, x0 + 1 + rng() % 8), std::min(side, y0 + 1 + rng() % 8),
                    static_cast<float>(rng() % 100) * 0.5f);
            }
            const StaticFilterResult r = s.run();
            std::uint32_t counted = 0;
            for (std::uint32_t y = 0; y < side; ++y)
            {
                for (std::uint32_t x = 0; x < side; ++x)
                {
                    const std::size_t i = y * side + x;
                    // Never lower than the ground; raised exactly where accepted.
                    EXPECT_GE(r.mHeights[i], s.terrain[i]);
                    if (!r.mIsStatic[i])
                    {
                        EXPECT_FLOAT_EQ(r.mHeights[i], s.terrain[i]);
                        continue;
                    }
                    ++counted;
                    EXPECT_FLOAT_EQ(r.mHeights[i], s.raw[i]);
                    EXPECT_GE(s.raw[i], s.terrain[i] + staticMinRise);
                    // Every accepted texel lies in a 2 x 2 block of accepted texels.
                    bool inBlock = false;
                    for (int oy = -1; oy <= 0 && !inBlock; ++oy)
                    {
                        for (int ox = -1; ox <= 0; ++ox)
                        {
                            const int bx = static_cast<int>(x) + ox;
                            const int by = static_cast<int>(y) + oy;
                            if (bx < 0 || by < 0 || bx + 1 >= static_cast<int>(side)
                                || by + 1 >= static_cast<int>(side))
                                continue;
                            const auto at = [&](int ax, int ay) { return r.mIsStatic[ay * side + ax] != 0; };
                            if (at(bx, by) && at(bx + 1, by) && at(bx, by + 1) && at(bx + 1, by + 1))
                            {
                                inBlock = true;
                                break;
                            }
                        }
                    }
                    EXPECT_TRUE(inBlock) << round << ": " << x << "," << y;
                }
            }
            EXPECT_EQ(counted, r.mAccepted);
            EXPECT_LE(r.mAccepted, r.mCandidates);
            if (r.mAccepted > 0)
                EXPECT_GE(r.mAccepted, staticMinAreaTexels);
        }
    }

    // ---- the capture file -------------------------------------------------------------------------------------

    StaticCapture sampleCapture()
    {
        StaticCapture c;
        c.mCellX = -3;
        c.mCellY = 12;
        c.mSide = 8;
        c.mSpacing = 64.f;
        for (std::size_t i = 0; i < c.mRefsDigest.size(); ++i)
            c.mRefsDigest[i] = static_cast<std::uint8_t>(i * 7 + 1);
        c.mTops.assign(64, nan);
        c.mTops[3] = 12.5f;
        c.mTops[40] = -200.25f;
        c.mTops[63] = 0.f;
        return c;
    }

    TEST(PuddleStaticCapture, writes_the_documented_layout)
    {
        const StaticCapture c = sampleCapture();
        std::vector<std::uint8_t> bytes;
        ASSERT_FALSE(writeCapture(c, bytes));
        ASSERT_EQ(bytes.size(), capturePayloadSize(8));
        EXPECT_EQ(capturePayloadSize(8), 64u + 4u * 64u);
        EXPECT_EQ(std::memcmp(bytes.data(), "EDENPST\0", 8), 0);
        EXPECT_EQ(bytes[8], 1); // layout 1
        EXPECT_EQ(bytes[12], 0xFD); // cell x -3, little endian
        EXPECT_EQ(bytes[13], 0xFF);
        EXPECT_EQ(bytes[16], 12);
        EXPECT_EQ(bytes[20], 8);
        EXPECT_EQ(bytes[32], 1); // the digest starts at 32
        EXPECT_EQ(bytes[33], 8);
        // The first top is NaN, written as the canonical one; the fourth is 12.5f = 0x41480000.
        EXPECT_EQ(bytes[64], 0x00);
        EXPECT_EQ(bytes[65], 0x00);
        EXPECT_EQ(bytes[66], 0xC0);
        EXPECT_EQ(bytes[67], 0x7F);
        EXPECT_EQ(bytes[64 + 12 + 2], 0x48);
        EXPECT_EQ(bytes[64 + 12 + 3], 0x41);
    }

    TEST(PuddleStaticCapture, round_trips_byte_for_byte_and_value_for_value)
    {
        const StaticCapture c = sampleCapture();
        std::vector<std::uint8_t> bytes;
        ASSERT_FALSE(writeCapture(c, bytes));
        StaticCapture back;
        ASSERT_FALSE(parseCapture(bytes.data(), bytes.size(), back));
        EXPECT_TRUE(back == c);
        std::vector<std::uint8_t> again;
        ASSERT_FALSE(writeCapture(back, again));
        EXPECT_EQ(again, bytes);
    }

    TEST(PuddleStaticCapture, every_truncation_and_every_extension_is_refused)
    {
        const StaticCapture c = sampleCapture();
        std::vector<std::uint8_t> bytes;
        ASSERT_FALSE(writeCapture(c, bytes));
        StaticCapture sink;
        for (std::size_t n = 0; n < bytes.size(); ++n)
            EXPECT_TRUE(parseCapture(bytes.data(), n, sink)) << n;
        std::vector<std::uint8_t> longer = bytes;
        longer.push_back(0);
        EXPECT_TRUE(parseCapture(longer.data(), longer.size(), sink));
    }

    TEST(PuddleStaticCapture, the_header_fields_are_checked)
    {
        std::vector<std::uint8_t> good;
        ASSERT_FALSE(writeCapture(sampleCapture(), good));
        StaticCapture sink;
        auto bad = [&](std::size_t offset, std::uint8_t value) {
            std::vector<std::uint8_t> b = good;
            b[offset] = value;
            return parseCapture(b.data(), b.size(), sink);
        };
        EXPECT_EQ(bad(0, 'X').mField, "magic");
        EXPECT_EQ(bad(8, 2).mField, "layout");
        EXPECT_EQ(bad(20, 7).mField, "side");
        EXPECT_EQ(bad(28, 1).mField, "reserved");
        // A spacing of 0 or a NaN.
        std::vector<std::uint8_t> b = good;
        b[24] = b[25] = b[26] = b[27] = 0;
        EXPECT_EQ(parseCapture(b.data(), b.size(), sink).mField, "spacing");
        // A giant side is refused before the size is compared with the allocation it would ask for.
        b = good;
        b[20] = 0xFF;
        b[21] = 0xFF;
        b[22] = 0xFF;
        b[23] = 0x7F;
        EXPECT_EQ(parseCapture(b.data(), b.size(), sink).mField, "side");
    }

    TEST(PuddleStaticCapture, only_the_canonical_nan_and_finite_values_are_allowed)
    {
        std::vector<std::uint8_t> good;
        ASSERT_FALSE(writeCapture(sampleCapture(), good));
        StaticCapture sink;
        std::vector<std::uint8_t> b = good;
        b[64] = 0x01; // a NaN with another payload
        EXPECT_EQ(parseCapture(b.data(), b.size(), sink).mField, "tops");
        b = good;
        b[64] = 0;
        b[65] = 0;
        b[66] = 0x80;
        b[67] = 0x7F; // +infinity
        EXPECT_EQ(parseCapture(b.data(), b.size(), sink).mField, "tops");
        b = good;
        b[64 + 4] = 0x00;
        b[64 + 5] = 0x00;
        b[64 + 6] = 0x80;
        b[64 + 7] = 0x4B; // 16777216: beyond the limit of 1e7
        EXPECT_EQ(parseCapture(b.data(), b.size(), sink).mField, "tops");
    }

    TEST(PuddleStaticCapture, the_writer_refuses_what_the_reader_would)
    {
        std::vector<std::uint8_t> out;
        StaticCapture c = sampleCapture();
        c.mTops[5] = std::numeric_limits<float>::infinity();
        EXPECT_EQ(writeCapture(c, out).mField, "tops");
        c = sampleCapture();
        c.mTops.pop_back();
        EXPECT_EQ(writeCapture(c, out).mField, "tops");
        c = sampleCapture();
        c.mSide = 4;
        EXPECT_EQ(writeCapture(c, out).mField, "side");
        c = sampleCapture();
        c.mSpacing = 0.f;
        EXPECT_EQ(writeCapture(c, out).mField, "spacing");
        c = sampleCapture();
        c.mSpacing = nan;
        EXPECT_EQ(writeCapture(c, out).mField, "spacing");
    }

    TEST(PuddleStaticCapture, whatever_parses_writes_back_the_same_bytes)
    {
        std::vector<std::uint8_t> good;
        ASSERT_FALSE(writeCapture(sampleCapture(), good));
        std::mt19937 rng(7);
        int accepted = 0;
        for (int round = 0; round < 6000; ++round)
        {
            std::vector<std::uint8_t> b = good;
            const int edits = 1 + static_cast<int>(rng() % 3);
            for (int e = 0; e < edits; ++e)
                b[rng() % b.size()] = static_cast<std::uint8_t>(rng());
            StaticCapture parsed;
            if (parseCapture(b.data(), b.size(), parsed))
                continue;
            ++accepted;
            std::vector<std::uint8_t> again;
            ASSERT_FALSE(writeCapture(parsed, again));
            EXPECT_EQ(again, b);
        }
        EXPECT_GT(accepted, 0);
    }

    // ---- the folder -------------------------------------------------------------------------------------------

    TEST(PuddleCaptureStore, stores_and_loads_a_capture_for_the_same_references)
    {
        const CaptureStore store(scratch("store1") / "statics");
        const StaticCapture c = sampleCapture();
        ASSERT_FALSE(store.store(c));
        const auto loaded = store.load(c.mCellX, c.mCellY, c.mSide, c.mSpacing, c.mRefsDigest);
        ASSERT_TRUE(loaded.has_value());
        ASSERT_EQ(loaded->size(), c.mTops.size());
        EXPECT_FLOAT_EQ((*loaded)[3], 12.5f);
        EXPECT_TRUE(std::isnan((*loaded)[0]));
    }

    TEST(PuddleCaptureStore, a_capture_for_other_references_other_sides_or_other_cells_is_not_current)
    {
        const CaptureStore store(scratch("store2") / "statics");
        const StaticCapture c = sampleCapture();
        ASSERT_FALSE(store.store(c));
        Digest other = c.mRefsDigest;
        other[0] ^= 1;
        EXPECT_FALSE(store.load(c.mCellX, c.mCellY, c.mSide, c.mSpacing, other));
        EXPECT_FALSE(store.load(c.mCellX, c.mCellY, 16, c.mSpacing, c.mRefsDigest));
        EXPECT_FALSE(store.load(c.mCellX, c.mCellY, c.mSide, 32.f, c.mRefsDigest));
        EXPECT_FALSE(store.load(c.mCellX + 1, c.mCellY, c.mSide, c.mSpacing, c.mRefsDigest));
    }

    TEST(PuddleCaptureStore, a_damaged_or_foreign_file_is_not_a_capture)
    {
        const CaptureStore store(scratch("store3") / "statics");
        const StaticCapture c = sampleCapture();
        ASSERT_FALSE(store.store(c));
        std::filesystem::resize_file(store.pathOf(c.mCellX, c.mCellY), 100);
        EXPECT_FALSE(store.load(c.mCellX, c.mCellY, c.mSide, c.mSpacing, c.mRefsDigest));
        // A file the right size with wrong content.
        {
            std::ofstream f(store.pathOf(c.mCellX, c.mCellY), std::ios::binary | std::ios::trunc);
            std::string junk(static_cast<std::size_t>(capturePayloadSize(8)), 'x');
            f << junk;
        }
        EXPECT_FALSE(store.load(c.mCellX, c.mCellY, c.mSide, c.mSpacing, c.mRefsDigest));
        EXPECT_FALSE(store.load(5, 5, c.mSide, c.mSpacing, c.mRefsDigest)); // no file at all
    }

    TEST(PuddleCaptureStore, a_store_replaces_the_old_file_and_leaves_no_temporary)
    {
        const CaptureStore store(scratch("store4") / "statics");
        StaticCapture c = sampleCapture();
        ASSERT_FALSE(store.store(c));
        c.mTops[3] = 99.f;
        ASSERT_FALSE(store.store(c));
        const auto loaded = store.load(c.mCellX, c.mCellY, c.mSide, c.mSpacing, c.mRefsDigest);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_FLOAT_EQ((*loaded)[3], 99.f);
        std::size_t files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(store.folder()))
        {
            (void)entry;
            ++files;
        }
        EXPECT_EQ(files, 1u);
    }
}
