#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include <components/puddle/puddlejob.hpp>
#include <components/puddle/puddlepack.hpp>

#include "puddletestland.hpp"

// Task 0095, stage W-b, fixes of the fresh review of 2adb11d952: a damaged pack is repaired and not reported up to
// date, ponds under a clamped height are dropped, the reader is canonical in meaning, one bad height skips one cell,
// the statistics are optional, a ranged bake over other settings says so. Generated land only.

namespace
{
    using namespace Puddle;
    using PuddleTest::FakeLand;
    using PuddleTest::pitHeights;
    using PuddleTest::smallSettings;

    std::filesystem::path scratch(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / ("puddlereview_" + name);
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
        return path;
    }

    FakeLand::HeightFn world()
    {
        return [](int vx, int vy) {
            const int cx = vx >= 0 ? vx / 4 : -((-vx + 3) / 4);
            if (cx == 1)
                return 108.f;
            return pitHeights(cx * 4 + 2, 2, 108.f, 8.f)(vx, vy);
        };
    }

    JobConfig configFor(const std::filesystem::path& folder)
    {
        JobConfig c;
        c.mSettings = smallSettings();
        c.mFolder = folder;
        c.mLandCells = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 }, { 0, 2 } };
        c.mStoreOnly = true;
        return c;
    }

    bool runJob(BakeJob& job)
    {
        for (int i = 0; i < 100000; ++i)
            if (job.step(std::chrono::milliseconds(50)))
                return true;
        return false;
    }

    std::size_t texel(const CellMap& c, int x, int y)
    {
        return static_cast<std::size_t>(y) * c.mSide + x;
    }

    // Is the pack at `path` whole: it opens and every cell reads?
    bool packIsIntact(const std::filesystem::path& path, std::size_t expectedCells)
    {
        Error error;
        const auto pack = PuddlePack::open(path, error);
        if (!pack || pack->cells().size() != expectedCells)
            return false;
        for (const auto& cell : pack->cells())
        {
            CellMap map;
            if (pack->readCell(cell.first, cell.second, map))
                return false;
        }
        return true;
    }

    // ---- 1. a damaged pack is repaired, never reported up to date ----

    TEST(PuddleReview, a_damaged_pack_is_repaired_and_not_reported_up_to_date)
    {
        int damagedCases = 0;
        int openButDamaged = 0; // the old code's blind spot: the pack opens, one cell does not read
        for (int step = 1; step < 20; ++step)
        {
            const double where = step / 20.0;
            const std::filesystem::path folder = scratch("damaged");
            FakeLand land(world());
            {
                BakeJob first(land, configFor(folder));
                ASSERT_TRUE(runJob(first));
                ASSERT_FALSE(first.result().mError);
            }
            const std::filesystem::path pack = folder / "puddles.edenpak";
            {
                std::fstream file(pack, std::ios::binary | std::ios::in | std::ios::out);
                const auto size = static_cast<std::streamoff>(std::filesystem::file_size(pack));
                file.seekg(static_cast<std::streamoff>(static_cast<double>(size) * where));
                char c = 0;
                file.get(c);
                file.seekp(static_cast<std::streamoff>(static_cast<double>(size) * where));
                file.put(static_cast<char>(c ^ 0x55));
            }
            if (packIsIntact(pack, 9))
                continue; // the flipped byte was padding: nothing to repair
            ++damagedCases;
            {
                Error opened;
                if (PuddlePack::open(pack, opened))
                    ++openButDamaged;
            }
            FakeLand again(world());
            BakeJob second(again, configFor(folder));
            ASSERT_TRUE(runJob(second));
            ASSERT_FALSE(second.result().mError) << second.result().mError.describe();
            EXPECT_FALSE(second.result().mUpToDate) << "at " << where;
            EXPECT_TRUE(packIsIntact(pack, 9)) << "at " << where;
        }
        EXPECT_GT(damagedCases, 0);
        EXPECT_GT(openButDamaged, 0);
    }

    // ---- 2. ponds under a clamped height are dropped ----

    TEST(PuddleReview, a_pond_whose_top_is_clamped_at_the_sixteen_bit_limit_has_no_capacity)
    {
        // One low vertex pulls the cell's base to 0; the pit at (3,3) sits at about 9,000 units, above the 8,191.875
        // that 16 bits hold over the base, so its top is clamped and the level test at run time could not use it.
        FakeLand land(
            [](int vx, int vy) { return (vx == 0 && vy == 0) ? 0.f : pitHeights(3, 3, 9000.f, 8.f)(vx, vy); });
        const BakeResult r = bakeCell(land, 0, 0, smallSettings());
        ASSERT_FALSE(r.mError);
        const CellMap& c = r.mCell;
        std::size_t clamped = 0;
        std::size_t wetClamped = 0;
        for (std::size_t i = 0; i < c.mTop.size(); ++i)
        {
            if (c.mTop[i] != 65535)
                continue;
            ++clamped;
            if (!c.noPits() && c.mCapacity[i] != 0)
                ++wetClamped;
        }
        EXPECT_GT(clamped, 0u);
        EXPECT_EQ(wetClamped, 0u);
        EXPECT_TRUE(r.mStats.mHeightsClamped);
    }

    // ---- 3. the reader is canonical in meaning ----

    CellMap sampleCell()
    {
        CellMap c;
        c.mCellX = 1;
        c.mCellY = 2;
        c.mSide = 8;
        c.mSpacing = 64.f;
        c.mHeightBase = 10.f;
        const std::size_t n = 64;
        c.mTexelFlags.assign(n, 0);
        c.mTop.assign(n, 100);
        c.mCapacity.assign(n, 0);
        c.mBasinMax.assign(n, 0);
        c.mCatchment.assign(n, 0);
        c.mBasinId.assign(n, noBasin);
        for (const std::size_t i : { 10u, 11u, 18u })
        {
            c.mBasinId[i] = 0;
            c.mCapacity[i] = 30;
            c.mBasinMax[i] = 60;
            c.mCatchment[i] = 33;
        }
        c.mBasinId[40] = 1;
        c.mCapacity[40] = 20;
        c.mBasinMax[40] = 20;
        c.mCatchment[40] = 100;
        c.mBasins = { BasinInfo{ 17, 60, 33 }, BasinInfo{ 16, 20, 100 } };
        return c;
    }

    std::vector<std::uint8_t> bytesOf(const CellMap& cell)
    {
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(writeCell(cell, bytes));
        return bytes;
    }

    TEST(PuddleReview, the_reader_checks_the_basin_table_against_the_texels)
    {
        const std::vector<std::uint8_t> good = bytesOf(sampleCell());
        const std::size_t n = 64;
        const std::size_t capacity = 108;
        const std::size_t basinMax = capacity + n;
        const std::size_t catchment = capacity + 2 * n;
        const std::size_t flags = capacity + 3 * n;
        const std::size_t table = flags + n + 2 * n + 2 * n;
        CellMap out;
        ASSERT_FALSE(parseCell(good, out));
        auto with = [&](std::size_t offset, std::uint8_t value) {
            std::vector<std::uint8_t> bytes = good;
            bytes[offset] = value;
            return parseCell(bytes, out).mField;
        };
        EXPECT_EQ(with(table + 4, 59), "basinTable"); // the texels' pit maximum is 60
        EXPECT_EQ(with(table + 5, 34), "basinTable"); // the texels' catchment code is 33
        EXPECT_EQ(with(table + 0, 2), "basinTable"); // 3 texels of the pit are in this cell, its area says 2
        EXPECT_EQ(with(catchment + 18, 34), "catchment"); // one texel of a pit says another catchment
        EXPECT_EQ(with(basinMax + 18, 61), "basinMax"); // one texel says another pit maximum
        EXPECT_EQ(with(flags + 5, 0x0B), "texelFlags"); // a sink has no direction
        EXPECT_EQ(with(flags + 5, 0x10), "texelFlags"); // a static surface in a cell that has none
    }

    TEST(PuddleReview, a_cell_that_captured_statics_may_mark_a_static_texel)
    {
        CellMap c = sampleCell();
        c.mFlags = flagStatics;
        c.mTexelFlags[5] = texelStatic;
        std::vector<std::uint8_t> bytes;
        ASSERT_FALSE(writeCell(c, bytes));
        CellMap back;
        EXPECT_FALSE(parseCell(bytes, back));
        EXPECT_EQ(back, c);
    }

    TEST(PuddleReview, the_writer_refuses_a_cell_whose_table_disagrees_with_its_texels)
    {
        std::vector<std::uint8_t> out;
        CellMap c = sampleCell();
        c.mBasins[1].mCatchmentCode = 7;
        EXPECT_EQ(writeCell(c, out).mField, "basinTable");
        c = sampleCell();
        c.mCatchment[11] = 1;
        EXPECT_EQ(writeCell(c, out).mField, "catchment");
    }

    // ---- 4. one bad height skips one cell ----

    TEST(PuddleReview, a_non_finite_or_huge_height_skips_that_cell_and_the_bake_goes_on)
    {
        for (const float bad :
            { std::numeric_limits<float>::quiet_NaN(), 1.0e30f, -std::numeric_limits<float>::infinity() })
        {
            const std::filesystem::path folder = scratch("badheight");
            FakeLand land([bad](int vx, int vy) { return (vx == 10 && vy == 2) ? bad : world()(vx, vy); });
            BakeJob job(land, configFor(folder));
            ASSERT_TRUE(runJob(job));
            const JobResult& r = job.result();
            ASSERT_FALSE(r.mError) << r.mError.describe();
            EXPECT_EQ(r.mCellsInPack, 8u); // cell (2,0) is skipped
            EXPECT_TRUE(packIsIntact(folder / "puddles.edenpak", 8));
            Error error;
            const auto pack = PuddlePack::open(folder / "puddles.edenpak", error);
            ASSERT_TRUE(pack);
            EXPECT_FALSE(pack->contains(2, 0));
            EXPECT_TRUE(pack->contains(1, 0));
            EXPECT_TRUE(pack->contains(3, 0));
        }
    }

    TEST(PuddleReview, bakeCell_names_the_bad_height_in_a_located_error)
    {
        FakeLand land([](int vx, int vy) {
            return (vx == 10 && vy == 2) ? std::numeric_limits<float>::quiet_NaN() : world()(vx, vy);
        });
        const BakeResult r = bakeCell(land, 2, 0, smallSettings());
        ASSERT_TRUE(r.mError);
        EXPECT_EQ(r.mError.mField, "heights");
        EXPECT_NE(r.mError.mMessage.find("(2, 0)"), std::string::npos);
        EXPECT_NE(r.mError.mMessage.find("(2, 2)"), std::string::npos); // local vertex 10 - 8 = 2, row 2
    }

    TEST(PuddleReview, a_bad_cell_next_to_a_good_one_counts_as_no_land_for_the_good_one)
    {
        FakeLand bad([](int vx, int vy) {
            return (vx == 10 && vy == 2) ? std::numeric_limits<float>::quiet_NaN() : world()(vx, vy);
        });
        FakeLand holes(world(), [](int x, int y) { return !(x == 2 && y == 0); });
        const BakeResult a = bakeCell(bad, 1, 0, smallSettings());
        const BakeResult b = bakeCell(holes, 1, 0, smallSettings());
        ASSERT_FALSE(a.mError);
        ASSERT_FALSE(b.mError);
        EXPECT_EQ(a.mCell, b.mCell);
    }

    // ---- 7. the unbounded catchment is a statistic ----

    TEST(PuddleReview, the_unbounded_catchment_is_only_worked_out_when_asked_for)
    {
        FakeLand land(pitHeights(2, 2, 108.f, 8.f));
        const BakeResult plain = bakeCell(land, 0, 0, smallSettings());
        BakeOptions statistics;
        statistics.mStatistics = true;
        const BakeResult measured = bakeCell(land, 0, 0, smallSettings(), statistics);
        ASSERT_FALSE(plain.mError);
        ASSERT_FALSE(measured.mError);
        EXPECT_FALSE(plain.mStats.mKappasBounded.empty());
        EXPECT_TRUE(plain.mStats.mKappasUnbounded.empty());
        EXPECT_EQ(measured.mStats.mKappasUnbounded.size(), measured.mStats.mKappasBounded.size());
        // The pack does not depend on it.
        EXPECT_EQ(plain.mCell, measured.mCell);
    }

    // ---- 8. a pack made with other settings is said to be ignored ----

    TEST(PuddleReview, a_ranged_bake_over_a_pack_of_other_settings_says_so_and_keeps_only_the_range)
    {
        const std::filesystem::path folder = scratch("othersettings");
        FakeLand land(world());
        {
            BakeJob first(land, configFor(folder));
            ASSERT_TRUE(runJob(first));
            ASSERT_FALSE(first.result().mError);
            EXPECT_FALSE(first.result().mOldPackOtherSettings);
        }
        {
            // The same settings again: nothing to warn about.
            BakeJob same(land, configFor(folder));
            ASSERT_TRUE(runJob(same));
            EXPECT_FALSE(same.result().mOldPackOtherSettings);
            EXPECT_TRUE(same.result().mUpToDate);
        }
        JobConfig other = configFor(folder);
        other.mSettings.mMinPondTexels = 7;
        other.mRange = std::array<std::int32_t, 4>{ 0, 0, 1, 0 };
        BakeJob job(land, other);
        ASSERT_TRUE(runJob(job));
        ASSERT_FALSE(job.result().mError);
        EXPECT_TRUE(job.result().mOldPackOtherSettings);
        EXPECT_EQ(job.result().mCellsInPack, 2u); // only the range: the 9-cell pack was replaced by 2 cells
        EXPECT_EQ(job.result().mFromPack, 0u);
    }

    TEST(PuddleReview, a_pack_file_that_cannot_be_opened_is_replaced_and_said_so)
    {
        const std::filesystem::path folder = scratch("garbage");
        {
            std::ofstream file(folder / "puddles.edenpak", std::ios::binary);
            file << "this is not a pack";
        }
        FakeLand land(world());
        BakeJob job(land, configFor(folder));
        ASSERT_TRUE(runJob(job));
        ASSERT_FALSE(job.result().mError);
        EXPECT_TRUE(job.result().mOldPackUnreadable);
        EXPECT_TRUE(packIsIntact(folder / "puddles.edenpak", 9));
    }

    // ---- 5. the pack is written from files: the job holds no cell maps ----

    TEST(PuddleReview, a_rebake_after_a_change_keeps_the_cells_that_still_hold_and_writes_a_whole_pack)
    {
        const std::filesystem::path folder = scratch("streaming");
        FakeLand land(world());
        {
            BakeJob first(land, configFor(folder));
            ASSERT_TRUE(runJob(first));
        }
        // A change at vertex (14, 1) (cell (3,0)): the four cells whose window holds it, (2,0), (3,0), (2,1) and
        // (3,1), are rebaked, the other five come from the pack that is replaced; the new pack is whole.
        FakeLand changed([](int vx, int vy) { return (vx == 14 && vy == 1) ? 140.f : world()(vx, vy); });
        BakeJob second(changed, configFor(folder));
        ASSERT_TRUE(runJob(second));
        ASSERT_FALSE(second.result().mError);
        EXPECT_FALSE(second.result().mUpToDate);
        EXPECT_EQ(second.result().mBaked, 4u);
        EXPECT_EQ(second.result().mFromPack, 5u);
        EXPECT_EQ(second.result().mReadBack, 9u);
        EXPECT_TRUE(packIsIntact(folder / "puddles.edenpak", 9));
        EXPECT_FALSE(std::filesystem::exists(folder / "work"));
    }
}
