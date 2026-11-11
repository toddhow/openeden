#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>

#include <components/puddle/puddlebake.hpp>
#include <components/puddle/puddlejob.hpp>
#include <components/puddle/puddlemap.hpp>
#include <components/puddle/puddlestatics.hpp>
#include <components/render/puddlelevel.hpp>

#include "puddletestland.hpp"

// Task 0095, stage W-c: baking with static surfaces. Generated land and a generated source of captures (no game data).

namespace
{
    using namespace Puddle;
    using PuddleTest::FakeLand;
    using PuddleTest::pitHeights;
    using PuddleTest::smallSettings;

    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr std::uint32_t side = 16;
    using Cell = std::pair<std::int32_t, std::int32_t>;

    BakeSettings staticSettings()
    {
        BakeSettings s = smallSettings();
        s.mStatics = true;
        return s;
    }

    std::size_t texel(const CellMap& c, int x, int y)
    {
        return static_cast<std::size_t>(y) * c.mSide + x;
    }

    /// Generated captures: per cell a raw top for each texel, NaN by default; a digest per cell derived from its place
    /// unless set. A cell can be made to have no digest or no capture.
    class FakeStatics final : public StaticSurfaceSource
    {
    public:
        bool refsDigest(std::int32_t cx, std::int32_t cy, Digest& digest) override
        {
            ++mDigestReads;
            if (mNoDigest.count({ cx, cy }))
                return false;
            const auto found = mDigests.find({ cx, cy });
            if (found != mDigests.end())
            {
                digest = found->second;
                return true;
            }
            digest = {};
            digest[0] = static_cast<std::uint8_t>(cx);
            digest[1] = static_cast<std::uint8_t>(cy);
            digest[2] = 0x5A;
            return true;
        }

        bool cellTops(std::int32_t cx, std::int32_t cy, std::vector<float>& tops) override
        {
            ++mTopReads;
            if (mNoCapture.count({ cx, cy }))
                return false;
            const auto found = mTops.find({ cx, cy });
            if (found != mTops.end())
                tops = found->second;
            else
                tops.assign(side * side, nan);
            return true;
        }

        std::vector<float>& tops(std::int32_t cx, std::int32_t cy)
        {
            auto it = mTops.find({ cx, cy });
            if (it == mTops.end())
                it = mTops.emplace(Cell{ cx, cy }, std::vector<float>(side * side, nan)).first;
            return it->second;
        }

        void box(std::int32_t cx, std::int32_t cy, int x0, int y0, int x1, int y1, float z)
        {
            auto& t = tops(cx, cy);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    t[static_cast<std::size_t>(y) * side + x] = z;
        }

        std::map<Cell, Digest> mDigests;
        std::map<Cell, std::vector<float>> mTops;
        std::set<Cell> mNoDigest;
        std::set<Cell> mNoCapture;
        std::size_t mDigestReads = 0;
        std::size_t mTopReads = 0;
    };

    FakeLand flat(float z = 100.f)
    {
        return FakeLand([z](int, int) { return z; });
    }

    // A road along x through the middle of cell (0, 0), four texels wide, 3 units above the plateau, with a dip of 2 x
    // 2 texels that is one unit lower.
    void roadWithDip(FakeStatics& s)
    {
        s.box(0, 0, 0, 6, 16, 10, 103.f);
        s.box(0, 0, 6, 7, 8, 9, 102.f);
    }

    TEST(PuddleStaticsSettings, statics_are_valid_settings_now_and_a_parameter_of_the_bake)
    {
        BakeSettings s = smallSettings();
        s.mStatics = true;
        EXPECT_FALSE(validateSettings(s));
        EXPECT_NE(parameterDigest(s), parameterDigest(smallSettings()));
    }

    TEST(PuddleStaticsBake, statics_off_ignores_a_source_and_gives_the_same_cell_and_digest)
    {
        FakeLand land(pitHeights(2, 2, 108.f, 8.f));
        FakeStatics source;
        roadWithDip(source);
        const BakeResult plain = bakeCell(land, 0, 0, smallSettings());
        const BakeResult with = bakeCell(land, 0, 0, smallSettings(), {}, &source);
        ASSERT_FALSE(plain.mError);
        ASSERT_FALSE(with.mError);
        EXPECT_TRUE(plain.mCell == with.mCell);
        EXPECT_EQ(plain.mCell.mFlags & flagStatics, 0u);
        EXPECT_EQ(inputDigest(land, 0, 0, smallSettings()), inputDigest(land, 0, 0, smallSettings(), &source));
        EXPECT_EQ(source.mDigestReads, 0u);
        EXPECT_EQ(source.mTopReads, 0u);
    }

    std::string hexOf(const Digest& digest)
    {
        static const char* digits = "0123456789abcdef";
        std::string text;
        for (const std::uint8_t b : digest)
        {
            text += digits[b >> 4];
            text += digits[b & 15];
        }
        return text;
    }

    TEST(PuddleStaticsBake, with_statics_off_the_digests_are_those_of_stage_w_b_byte_for_byte)
    {
        // Recorded from the code of stage W-b (the commit before W-c), so that "statics off gives a byte-identical
        // pack" is a fact about the digests and not only about two runs of the new code agreeing with each other.
        FakeLand flatLand = flat();
        FakeLand pitLand(pitHeights(2, 2, 108.f, 8.f));
        EXPECT_EQ(hexOf(parameterDigest(smallSettings())),
            "b996e215b121a624f9191fc42188ae87062225832efe6f429ea54395846edbc3");
        EXPECT_EQ(hexOf(inputDigest(flatLand, 0, 0, smallSettings())),
            "d9ba6d8173c60920f6570da564302158b23e1d6f68a8d77b85fb5ff9b5cf6d18");
        EXPECT_EQ(hexOf(inputDigest(pitLand, 0, 0, smallSettings())),
            "8694091fbea5768bd15a2efebb267bcda0179828e337837485b8c147359c86ef");
        const BakeResult r = bakeCell(pitLand, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        EXPECT_EQ(hexOf(r.mCell.mInputDigest), "8694091fbea5768bd15a2efebb267bcda0179828e337837485b8c147359c86ef");
        EXPECT_EQ(hexOf(r.mCell.mParameterDigest), "b996e215b121a624f9191fc42188ae87062225832efe6f429ea54395846edbc3");
    }

    TEST(PuddleStaticsBake, statics_on_without_a_source_is_an_error)
    {
        FakeLand land = flat();
        const BakeResult r = bakeCell(land, 0, 0, staticSettings());
        EXPECT_EQ(r.mError.mField, "statics");
    }

    TEST(PuddleStaticsBake, a_road_is_a_static_surface_at_its_own_height_and_the_cell_says_it_captured_statics)
    {
        FakeLand land = flat();
        FakeStatics source;
        roadWithDip(source);
        const BakeResult r = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        ASSERT_FALSE(r.mError) << r.mError.describe();
        const CellMap& c = r.mCell;
        EXPECT_NE(c.mFlags & flagStatics, 0u);
        std::uint32_t statics = 0;
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const bool road = y >= 6 && y < 10;
                const bool flagged = (c.mTexelFlags[texel(c, x, y)] & texelStatic) != 0;
                EXPECT_EQ(flagged, road) << x << "," << y;
                statics += flagged ? 1 : 0;
                const float expected = road ? ((x >= 6 && x < 8 && y >= 7 && y < 9) ? 102.f : 103.f) : 100.f;
                EXPECT_FLOAT_EQ(Render::Puddle::dequantiseHeight(c.mTop[texel(c, x, y)], c.mHeightBase), expected)
                    << x << "," << y;
            }
        }
        EXPECT_EQ(statics, 64u);
        EXPECT_EQ(r.mStats.mStaticTexels, 64u);
        EXPECT_GE(r.mStats.mStaticCandidates, 64u);
    }

    TEST(PuddleStaticsBake, the_dip_in_a_road_holds_water_and_the_rest_of_the_road_drains)
    {
        FakeLand land = flat();
        FakeStatics source;
        roadWithDip(source);
        const BakeResult r = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        ASSERT_FALSE(r.mError) << r.mError.describe();
        const CellMap& c = r.mCell;
        ASSERT_FALSE(c.noPits());
        EXPECT_EQ(c.mBasins.size(), 1u);
        // One unit of capacity, 16 sixteenths, in the four dip texels.
        for (int y = 7; y < 9; ++y)
            for (int x = 6; x < 8; ++x)
            {
                EXPECT_EQ(c.mCapacity[texel(c, x, y)], 16) << x << "," << y;
                EXPECT_EQ(c.mBasinMax[texel(c, x, y)], 16);
                EXPECT_NE(c.mTexelFlags[texel(c, x, y)] & texelStatic, 0);
            }
        EXPECT_EQ(c.mCapacity[texel(c, 12, 7)], 0); // the road beside the dip
        EXPECT_EQ(c.mCapacity[texel(c, 3, 3)], 0); // the ground beside the road
        EXPECT_EQ(r.mStats.mWetTexels, 4u);
    }

    TEST(PuddleStaticsBake, a_road_with_no_dip_holds_no_water_and_is_still_flagged)
    {
        FakeLand land = flat();
        FakeStatics source;
        source.box(0, 0, 0, 6, 16, 10, 103.f);
        const BakeResult r = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        ASSERT_FALSE(r.mError) << r.mError.describe();
        EXPECT_TRUE(r.mCell.noPits());
        EXPECT_NE(r.mCell.mFlags & flagStatics, 0u);
        EXPECT_NE(r.mCell.mTexelFlags[texel(r.mCell, 5, 7)] & texelStatic, 0);
    }

    TEST(PuddleStaticsBake, without_statics_the_same_land_shows_no_road)
    {
        FakeLand land = flat();
        const BakeResult r = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        EXPECT_TRUE(r.mCell.noPits());
        EXPECT_EQ(r.mCell.mFlags & flagStatics, 0u);
    }

    TEST(PuddleStaticsBake, a_deck_over_the_middle_of_a_pit_takes_the_columns_and_the_ground_under_it_has_no_capacity)
    {
        FakeLand land(pitHeights(2, 2, 108.f, 8.f));
        FakeStatics source;
        source.box(0, 0, 4, 4, 12, 12, 120.f);
        const BakeResult plain = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(plain.mError);
        EXPECT_EQ(plain.mCell.mCapacity[texel(plain.mCell, 8, 8)], 128);

        const BakeResult r = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        ASSERT_FALSE(r.mError) << r.mError.describe();
        const CellMap& c = r.mCell;
        EXPECT_EQ(c.mCapacity[texel(c, 8, 8)], 0);
        EXPECT_NE(c.mTexelFlags[texel(c, 8, 8)] & texelStatic, 0);
        EXPECT_FLOAT_EQ(Render::Puddle::dequantiseHeight(c.mTop[texel(c, 8, 8)], c.mHeightBase), 120.f);
        // The ring of the pit round the deck still holds water, and it is ground.
        EXPECT_GT(c.mCapacity[texel(c, 8, 3)], 0);
        EXPECT_EQ(c.mTexelFlags[texel(c, 8, 3)] & texelStatic, 0);
        // The deck is the surface a pixel at its height would find; the ground under it is not offered.
        EXPECT_NE(c.mHeightBase, 120.f);
    }

    TEST(PuddleStaticsBake, a_fence_and_clutter_across_a_pit_change_nothing)
    {
        FakeLand land(pitHeights(2, 2, 108.f, 8.f));
        FakeStatics source;
        source.box(0, 0, 8, 2, 9, 14, 130.f); // a fence one texel wide
        source.box(0, 0, 5, 5, 7, 7, 130.f); // a 2 x 2 crate
        const BakeResult plain = bakeCell(land, 0, 0, smallSettings());
        const BakeResult r = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        ASSERT_FALSE(plain.mError);
        ASSERT_FALSE(r.mError) << r.mError.describe();
        EXPECT_EQ(r.mCell.mCapacity, plain.mCell.mCapacity);
        EXPECT_EQ(r.mCell.mTop, plain.mCell.mTop);
        EXPECT_EQ(r.mStats.mStaticTexels, 0u);
    }

    TEST(PuddleStaticsBake, a_dock_over_the_sea_is_dry_land_with_water_only_in_its_dips)
    {
        // The whole cell is sea floor well under the water level; a dock deck 2 units above the sea, with a dip.
        FakeLand land([](int, int) { return -50.f; });
        FakeStatics source;
        source.box(0, 0, 2, 4, 14, 12, 2.f);
        source.box(0, 0, 6, 7, 8, 9, 1.f);
        const BakeResult r = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        ASSERT_FALSE(r.mError) << r.mError.describe();
        const CellMap& c = r.mCell;
        EXPECT_NE(c.mTexelFlags[texel(c, 10, 8)] & texelStatic, 0);
        EXPECT_EQ(c.mTexelFlags[texel(c, 10, 8)] & texelSink, 0); // not a sink: it is above the water
        EXPECT_NE(c.mTexelFlags[texel(c, 1, 1)] & texelSink, 0); // the sea is
        EXPECT_EQ(c.mCapacity[texel(c, 10, 8)], 0); // the deck drains over its edge
        EXPECT_EQ(c.mCapacity[texel(c, 6, 7)], 16); // the dip holds one unit
    }

    TEST(PuddleStaticsBake, a_submerged_static_stays_a_sink)
    {
        FakeLand land([](int, int) { return -50.f; });
        FakeStatics source;
        source.box(0, 0, 2, 4, 14, 12, -10.f); // above the sea floor, below the water level (-1)
        const BakeResult r = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        ASSERT_FALSE(r.mError) << r.mError.describe();
        EXPECT_NE(r.mCell.mTexelFlags[texel(r.mCell, 8, 8)] & texelSink, 0);
        EXPECT_TRUE(r.mCell.noPits());
    }

    // ---- the input digest and the errors ----------------------------------------------------------------------

    TEST(PuddleStaticsDigest, follows_the_references_of_every_cell_of_the_window_and_nothing_beyond)
    {
        FakeLand land = flat();
        FakeStatics source;
        const BakeSettings s = staticSettings();
        const Digest base = inputDigest(land, 0, 0, s, &source);

        FakeStatics inside;
        inside.mDigests[{ 1, 0 }] = Digest{ 9 };
        EXPECT_NE(inputDigest(land, 0, 0, s, &inside), base); // inside the halo of 1

        FakeStatics outside;
        outside.mDigests[{ 3, 0 }] = Digest{ 9 };
        EXPECT_EQ(inputDigest(land, 0, 0, s, &outside), base); // outside it

        FakeStatics centre;
        centre.mDigests[{ 0, 0 }] = Digest{ 9 };
        EXPECT_NE(inputDigest(land, 0, 0, s, &centre), base);
    }

    TEST(PuddleStaticsDigest, does_not_depend_on_the_captured_tops_only_on_the_references)
    {
        FakeLand land = flat();
        FakeStatics a;
        FakeStatics b;
        roadWithDip(b);
        const BakeSettings s = staticSettings();
        EXPECT_EQ(inputDigest(land, 0, 0, s, &a), inputDigest(land, 0, 0, s, &b));
        EXPECT_EQ(b.mTopReads, 0u);
    }

    TEST(PuddleStaticsDigest, differs_with_statics_on_and_off_and_when_a_digest_cannot_be_made)
    {
        FakeLand land = flat();
        FakeStatics source;
        const Digest off = inputDigest(land, 0, 0, smallSettings(), &source);
        const Digest on = inputDigest(land, 0, 0, staticSettings(), &source);
        EXPECT_NE(off, on);
        FakeStatics broken;
        broken.mNoDigest.insert({ 1, 0 });
        EXPECT_NE(inputDigest(land, 0, 0, staticSettings(), &broken), on);
    }

    TEST(PuddleStaticsDigest, a_cell_without_land_adds_nothing)
    {
        FakeLand land([](int, int) { return 100.f; }, [](int x, int) { return x != 1; });
        FakeStatics a;
        FakeStatics b;
        b.mDigests[{ 1, 0 }] = Digest{ 9 }; // the cell has no land: its references are not read
        EXPECT_EQ(inputDigest(land, 0, 0, staticSettings(), &a), inputDigest(land, 0, 0, staticSettings(), &b));
    }

    TEST(PuddleStaticsBake, the_result_carries_the_digest_of_the_inputs)
    {
        FakeLand land = flat();
        FakeStatics source;
        roadWithDip(source);
        const BakeResult r = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        ASSERT_FALSE(r.mError);
        EXPECT_EQ(r.mCell.mInputDigest, inputDigest(land, 0, 0, staticSettings(), &source));
        EXPECT_EQ(r.mCell.mParameterDigest, parameterDigest(staticSettings()));
    }

    TEST(PuddleStaticsBake, a_land_cell_of_the_window_without_a_capture_is_an_error_naming_it)
    {
        FakeLand land = flat();
        FakeStatics source;
        source.mNoCapture.insert({ 1, 0 });
        const BakeResult r = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        EXPECT_EQ(r.mError.mField, "statics");
        EXPECT_NE(r.mError.mMessage.find("(1, 0)"), std::string::npos) << r.mError.mMessage;
    }

    TEST(PuddleStaticsBake, a_cell_without_land_needs_no_capture)
    {
        FakeLand land([](int, int) { return 100.f; }, [](int x, int) { return x != 1; });
        FakeStatics source;
        source.mNoCapture.insert({ 1, 0 });
        EXPECT_FALSE(bakeCell(land, 0, 0, staticSettings(), {}, &source).mError);
    }

    TEST(PuddleStaticsBake, references_that_cannot_be_read_are_an_error)
    {
        FakeLand land = flat();
        FakeStatics source;
        source.mNoDigest.insert({ 0, 1 });
        EXPECT_EQ(bakeCell(land, 0, 0, staticSettings(), {}, &source).mError.mField, "statics");
    }

    TEST(PuddleStaticsBake, a_capture_of_the_wrong_size_is_an_error)
    {
        FakeLand land = flat();
        FakeStatics source;
        source.mTops[{ 0, 0 }] = std::vector<float>(10, nan);
        EXPECT_EQ(bakeCell(land, 0, 0, staticSettings(), {}, &source).mError.mField, "statics");
    }

    TEST(PuddleStaticsBake, the_result_depends_on_the_window_only_not_on_what_lies_beyond_or_the_order)
    {
        FakeLand land = flat();
        FakeStatics a;
        roadWithDip(a);
        FakeStatics b;
        roadWithDip(b);
        b.box(5, 0, 2, 2, 14, 14, 140.f); // far outside the halo of cell (0, 0)
        const BakeResult first = bakeCell(land, 0, 0, staticSettings(), {}, &a);
        const BakeResult second = bakeCell(land, 0, 0, staticSettings(), {}, &b);
        ASSERT_FALSE(first.mError);
        ASSERT_FALSE(second.mError);
        EXPECT_TRUE(first.mCell == second.mCell);
        // Another cell baked in between changes nothing.
        bakeCell(land, 1, 0, staticSettings(), {}, &a);
        EXPECT_TRUE(bakeCell(land, 0, 0, staticSettings(), {}, &a).mCell == first.mCell);
    }

    TEST(PuddleStaticsBake, a_road_that_continues_into_the_next_cell_is_one_surface)
    {
        // The halo makes a road that crosses the cell border a single surface: neither half is below the area minimum.
        FakeLand land = flat();
        FakeStatics source;
        source.box(0, 0, 14, 6, 16, 10, 103.f); // 2 x 4 texels at the east end of cell (0, 0): 8 texels
        source.box(1, 0, 0, 6, 2, 10, 103.f); // 2 x 4 at the west end of cell (1, 0)
        const BakeResult left = bakeCell(land, 0, 0, staticSettings(), {}, &source);
        ASSERT_FALSE(left.mError) << left.mError.describe();
        EXPECT_EQ(left.mStats.mStaticTexels, 8u);
        // Alone, the 8 texels are clutter.
        FakeStatics alone;
        alone.box(0, 0, 14, 6, 16, 10, 103.f);
        EXPECT_EQ(bakeCell(land, 0, 0, staticSettings(), {}, &alone).mStats.mStaticTexels, 0u);
    }

    // ---- the job ----------------------------------------------------------------------------------------------

    std::filesystem::path scratch(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / ("puddlestaticsjob_" + name);
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
        return path;
    }

    std::vector<std::uint8_t> fileBytes(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    bool runJob(BakeJob& job)
    {
        for (int i = 0; i < 100000; ++i)
            if (job.step(std::chrono::milliseconds(50)))
                return true;
        return false;
    }

    JobConfig jobConfig(const std::filesystem::path& folder)
    {
        JobConfig c;
        c.mSettings = staticSettings();
        c.mFolder = folder;
        c.mLandCells = { { 0, 0 }, { 1, 0 }, { 2, 0 } };
        c.mStoreOnly = true;
        return c;
    }

    TEST(PuddleStaticsJob, a_statics_job_without_a_source_is_an_error)
    {
        FakeLand land = flat();
        BakeJob job(land, jobConfig(scratch("nosource")));
        EXPECT_EQ(job.result().mError.mField, "statics");
    }

    TEST(PuddleStaticsJob, bakes_a_pack_whose_cells_say_they_captured_statics_and_is_deterministic)
    {
        const std::filesystem::path a = scratch("a");
        const std::filesystem::path b = scratch("b");
        FakeLand landA = flat();
        FakeLand landB = flat();
        FakeStatics sourceA;
        FakeStatics sourceB;
        roadWithDip(sourceA);
        roadWithDip(sourceB);
        BakeJob jobA(landA, jobConfig(a), &sourceA);
        JobConfig reversed = jobConfig(b);
        std::reverse(reversed.mLandCells.begin(), reversed.mLandCells.end());
        BakeJob jobB(landB, reversed, &sourceB);
        ASSERT_TRUE(runJob(jobA));
        ASSERT_TRUE(runJob(jobB));
        ASSERT_FALSE(jobA.result().mError) << jobA.result().mError.describe();
        ASSERT_FALSE(jobB.result().mError);
        EXPECT_EQ(jobA.result().mBaked, 3u);
        EXPECT_EQ(jobA.result().mStaticTexels, 64u);
        EXPECT_EQ(fileBytes(a / "puddles.edenpak"), fileBytes(b / "puddles.edenpak"));
        Error error;
        const auto pack = PuddlePack::open(a / "puddles.edenpak", error);
        ASSERT_TRUE(pack) << error.describe();
        CellMap cell;
        ASSERT_FALSE(pack->readCell(0, 0, cell));
        EXPECT_NE(cell.mFlags & flagStatics, 0u);
        ASSERT_FALSE(pack->readCell(2, 0, cell));
        EXPECT_NE(cell.mFlags & flagStatics, 0u); // captured, found none
    }

    TEST(PuddleStaticsJob, a_second_run_is_up_to_date_and_a_changed_reference_rebakes_only_the_cells_that_see_it)
    {
        const std::filesystem::path folder = scratch("again");
        FakeLand land = flat();
        FakeStatics source;
        roadWithDip(source);
        {
            BakeJob first(land, jobConfig(folder), &source);
            ASSERT_TRUE(runJob(first));
            ASSERT_FALSE(first.result().mError);
        }
        {
            BakeJob second(land, jobConfig(folder), &source);
            ASSERT_TRUE(runJob(second));
            EXPECT_TRUE(second.result().mUpToDate);
            EXPECT_EQ(second.result().mBaked, 0u);
        }
        // A reference of cell (2, 0) changes: cells (1, 0) and (2, 0) have it in their window (halo 1), cell (0, 0)
        // does not.
        source.mDigests[{ 2, 0 }] = Digest{ 77 };
        BakeJob third(land, jobConfig(folder), &source);
        ASSERT_TRUE(runJob(third));
        ASSERT_FALSE(third.result().mError);
        EXPECT_FALSE(third.result().mUpToDate);
        EXPECT_EQ(third.result().mBaked, 2u);
        EXPECT_EQ(third.result().mFromPack, 1u);
    }

    TEST(PuddleStaticsJob, a_cell_with_no_capture_is_skipped_and_counted_and_the_others_are_baked)
    {
        const std::filesystem::path folder = scratch("missing");
        FakeLand land = flat();
        FakeStatics source;
        source.mNoCapture.insert({ 2, 0 });
        BakeJob job(land, jobConfig(folder), &source);
        ASSERT_TRUE(runJob(job));
        ASSERT_FALSE(job.result().mError) << job.result().mError.describe();
        // Cells (1, 0) and (2, 0) have (2, 0) in their window; only (0, 0) can be baked.
        EXPECT_EQ(job.result().mBaked, 1u);
        EXPECT_EQ(job.result().mBadCells, 2u);
        EXPECT_NE(job.result().mFirstBadCell.find("(2, 0)"), std::string::npos);
    }

    TEST(PuddleStaticsJob, a_statics_off_pack_is_not_reused_by_a_statics_on_job_and_the_reverse)
    {
        const std::filesystem::path folder = scratch("switch");
        FakeLand land = flat();
        FakeStatics source;
        JobConfig off = jobConfig(folder);
        off.mSettings.mStatics = false;
        {
            BakeJob job(land, off, &source);
            ASSERT_TRUE(runJob(job));
            ASSERT_FALSE(job.result().mError);
        }
        BakeJob on(land, jobConfig(folder), &source);
        ASSERT_TRUE(runJob(on));
        EXPECT_TRUE(on.result().mOldPackOtherSettings);
        EXPECT_EQ(on.result().mBaked, 3u);
    }
}
