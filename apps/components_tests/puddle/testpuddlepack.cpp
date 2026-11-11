#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <edenarchive/source.hpp>

#include <components/puddle/puddlejob.hpp>
#include <components/puddle/puddlepack.hpp>
#include <components/puddle/puddlepartial.hpp>
#include <components/puddle/puddlewindow.hpp>

#include "puddletestland.hpp"

// Task 0095, stage W-b: the manifest, the pack, the work folder, the job (digests, resume, up to date) and the run-time
// window. Generated land only.

namespace
{
    using namespace Puddle;
    using PuddleTest::FakeLand;
    using PuddleTest::pitHeights;
    using PuddleTest::smallSettings;

    std::filesystem::path scratch(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / ("puddletest_" + name);
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
        return path;
    }

    // Three cells with a pit each (and a flat one without), so that packs hold different things.
    FakeLand::HeightFn world()
    {
        return [](int vx, int vy) {
            const int cx = vx >= 0 ? vx / 4 : -((-vx + 3) / 4);
            if (cx == 1)
                return 108.f; // flat cell, at the height of the plateau round the pits
            return pitHeights(cx * 4 + 2, 2, 108.f, 8.f)(vx, vy);
        };
    }

    std::vector<std::pair<std::int32_t, std::int32_t>> cellsOfWorld()
    {
        return { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } };
    }

    JobConfig configFor(const std::filesystem::path& folder)
    {
        JobConfig c;
        c.mSettings = smallSettings();
        c.mFolder = folder;
        c.mLandCells = cellsOfWorld();
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

    std::vector<std::uint8_t> fileBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    // ---- the manifest ----

    TEST(PuddleManifest, it_round_trips_byte_for_byte_and_is_one_canonical_line)
    {
        Manifest m;
        m.mSettings = smallSettings();
        m.mSettings.mWaterLevelUnits = -7;
        const std::string text = writeManifestJson(m);
        EXPECT_EQ(text.find('\n'), std::string::npos);
        EXPECT_EQ(text.find(' '), std::string::npos);
        Manifest back;
        ASSERT_FALSE(parseManifestJson(text, back));
        EXPECT_EQ(back, m);
        EXPECT_EQ(writeManifestJson(back), text);
        EXPECT_NE(text.find("\"waterLevelUnits\":-7"), std::string::npos);
    }

    TEST(PuddleManifest, anything_but_the_canonical_text_is_refused_with_a_place)
    {
        Manifest m;
        const std::string good = writeManifestJson(m);
        Manifest out;
        EXPECT_TRUE(parseManifestJson(good + "\n", out));
        EXPECT_TRUE(parseManifestJson(" " + good, out));
        EXPECT_TRUE(parseManifestJson("", out));
        EXPECT_TRUE(parseManifestJson(good.substr(0, good.size() - 1), out));
        std::string spaced = good;
        spaced.insert(spaced.find(',') + 1, " ");
        EXPECT_TRUE(parseManifestJson(spaced, out));
        std::string leadingZero = good;
        leadingZero.replace(leadingZero.find("\"halo\":2"), 8, "\"halo\":02");
        EXPECT_TRUE(parseManifestJson(leadingZero, out));
        std::string plus = good;
        plus.replace(plus.find("\"halo\":2"), 8, "\"halo\":+2");
        EXPECT_TRUE(parseManifestJson(plus, out));
        std::string reordered = good;
        const std::size_t a = reordered.find("\"bakerVersion\"");
        reordered.replace(a, 14, "\"bakerVersiom\"");
        const Error error = parseManifestJson(reordered, out);
        ASSERT_TRUE(error);
        EXPECT_EQ(error.mField, "manifest");
        EXPECT_GT(error.mOffset, 0u);
        std::string bigFlag = good;
        bigFlag.replace(bigFlag.find("\"statics\":0"), 11, "\"statics\":2");
        EXPECT_TRUE(parseManifestJson(bigFlag, out));
        std::string overflow = good;
        overflow.replace(overflow.find("\"halo\":2"), 8, "\"halo\":99999999999");
        EXPECT_TRUE(parseManifestJson(overflow, out));
    }

    // ---- the pack ----

    std::vector<PackedCell> bakedCells(std::vector<std::pair<std::int32_t, std::int32_t>> cells)
    {
        FakeLand land(world());
        std::vector<PackedCell> out;
        for (const auto& c : cells)
        {
            BakeResult r = bakeCell(land, c.first, c.second, smallSettings());
            EXPECT_FALSE(r.mError);
            out.push_back(PackedCell{ c.first, c.second, std::move(r.mCell) });
        }
        return out;
    }

    TEST(PuddlePack, a_pack_reads_back_and_does_not_depend_on_the_order_of_the_cells)
    {
        Manifest m;
        m.mSettings = smallSettings();
        std::vector<PackedCell> cells = bakedCells(cellsOfWorld());
        PackOptions options;
        options.mStoreOnly = true;
        options.mGeneration = 3;
        std::vector<std::uint8_t> a;
        ASSERT_FALSE(buildPuddlePack(m, cells, options, a));
        std::reverse(cells.begin(), cells.end());
        std::vector<std::uint8_t> b;
        ASSERT_FALSE(buildPuddlePack(m, cells, options, b));
        EXPECT_EQ(a, b);

        Error error;
        auto source = EdenArchive::makeMemorySource(a);
        const std::unique_ptr<PuddlePack> pack = PuddlePack::open(source, error);
        ASSERT_TRUE(pack) << error.describe();
        EXPECT_EQ(pack->generation(), 3u);
        EXPECT_EQ(pack->manifest(), m);
        ASSERT_EQ(pack->cells().size(), 4u);
        EXPECT_EQ(pack->cells()[0], (std::pair<std::int32_t, std::int32_t>{ 0, 0 }));
        EXPECT_TRUE(pack->contains(2, 0));
        EXPECT_FALSE(pack->contains(9, 9));
        for (const PackedCell& cell : cells)
        {
            CellMap back;
            ASSERT_FALSE(pack->readCell(cell.mCellX, cell.mCellY, back));
            EXPECT_EQ(back, cell.mMap);
            EXPECT_EQ(pack->inputDigest(cell.mCellX, cell.mCellY), std::optional<Digest>(cell.mMap.mInputDigest));
        }
        CellMap missing;
        EXPECT_TRUE(pack->readCell(9, 9, missing));
    }

    TEST(PuddlePack, a_duplicate_cell_or_a_wrong_cell_is_refused_by_the_writer)
    {
        Manifest m;
        m.mSettings = smallSettings();
        std::vector<PackedCell> cells = bakedCells({ { 0, 0 } });
        std::vector<std::uint8_t> out;
        PackOptions options;
        std::vector<PackedCell> twice = { cells[0], cells[0] };
        EXPECT_TRUE(buildPuddlePack(m, twice, options, out));
        PackedCell wrong = cells[0];
        wrong.mCellX = 5;
        EXPECT_TRUE(buildPuddlePack(m, { wrong }, options, out));
    }

    TEST(PuddlePack, a_pack_with_another_layout_or_no_manifest_is_refused)
    {
        Manifest m;
        m.mSettings = smallSettings();
        m.mLayoutVersion = 2;
        std::vector<std::uint8_t> bytes;
        ASSERT_FALSE(buildPuddlePack(m, bakedCells({ { 0, 0 } }), PackOptions{}, bytes));
        Error error;
        EXPECT_FALSE(PuddlePack::open(EdenArchive::makeMemorySource(bytes), error));
        EXPECT_TRUE(error);
        EXPECT_FALSE(PuddlePack::open(EdenArchive::makeMemorySource(std::vector<std::uint8_t>(10, 0)), error));
    }

    TEST(PuddlePack, a_damaged_pack_never_crashes_the_reader)
    {
        Manifest m;
        m.mSettings = smallSettings();
        PackOptions options;
        options.mStoreOnly = true;
        std::vector<std::uint8_t> good;
        ASSERT_FALSE(buildPuddlePack(m, bakedCells(cellsOfWorld()), options, good));
        std::uint64_t state = 88172645463325252ull;
        auto next = [&state]() {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        };
        for (int round = 0; round < 1500; ++round)
        {
            std::vector<std::uint8_t> bytes = good;
            for (int f = 0; f < 3; ++f)
                bytes[next() % bytes.size()] ^= static_cast<std::uint8_t>(1u << (next() % 8));
            if (round % 5 == 0)
                bytes.resize(next() % bytes.size());
            Error error;
            const std::unique_ptr<PuddlePack> pack = PuddlePack::open(EdenArchive::makeMemorySource(bytes), error);
            if (!pack)
                continue;
            for (const auto& cell : pack->cells())
            {
                CellMap map;
                (void)pack->readCell(cell.first, cell.second, map);
            }
        }
        SUCCEED();
    }

    TEST(PuddlePack, the_content_digest_follows_the_set_the_manifest_and_every_input)
    {
        Manifest m;
        m.mSettings = smallSettings();
        std::vector<std::pair<std::uint64_t, Digest>> inputs;
        for (int i = 0; i < 4; ++i)
        {
            std::uint64_t key = 0;
            packCellKey(i, 0, key);
            Digest d{};
            d[0] = static_cast<std::uint8_t>(i + 1);
            inputs.emplace_back(key, d);
        }
        const Digest base = contentDigest(m, inputs);
        auto reversed = inputs;
        std::reverse(reversed.begin(), reversed.end());
        EXPECT_EQ(contentDigest(m, reversed), base);
        auto changed = inputs;
        changed[2].second[5] = 9;
        EXPECT_NE(contentDigest(m, changed), base);
        auto fewer = inputs;
        fewer.pop_back();
        EXPECT_NE(contentDigest(m, fewer), base);
        Manifest other = m;
        other.mSettings.mHalo = 2;
        EXPECT_NE(contentDigest(other, inputs), base);
    }

    // ---- the work folder ----

    TEST(PuddlePartial, a_stored_cell_is_loaded_only_for_the_same_digests)
    {
        const PartialStore store(scratch("partial") / "work");
        CellMap cell = bakedCells({ { 0, 0 } })[0].mMap;
        ASSERT_FALSE(store.store(cell));
        EXPECT_EQ(store.load(0, 0, cell.mParameterDigest, cell.mInputDigest), std::optional<CellMap>(cell));
        Digest other = cell.mInputDigest;
        other[1] ^= 1;
        EXPECT_FALSE(store.load(0, 0, cell.mParameterDigest, other));
        other = cell.mParameterDigest;
        other[1] ^= 1;
        EXPECT_FALSE(store.load(0, 0, other, cell.mInputDigest));
        EXPECT_FALSE(store.load(1, 0, cell.mParameterDigest, cell.mInputDigest));
        store.clear();
        EXPECT_FALSE(store.load(0, 0, cell.mParameterDigest, cell.mInputDigest));
    }

    TEST(PuddlePartial, a_damaged_file_is_ignored)
    {
        const std::filesystem::path folder = scratch("partialbad") / "work";
        const PartialStore store(folder);
        CellMap cell = bakedCells({ { 0, 0 } })[0].mMap;
        ASSERT_FALSE(store.store(cell));
        {
            std::ofstream file(folder / "0_0.cell", std::ios::binary | std::ios::trunc);
            file << "not a cell";
        }
        EXPECT_FALSE(store.load(0, 0, cell.mParameterDigest, cell.mInputDigest));
    }

    // ---- the job ----

    TEST(PuddleJob, a_full_bake_writes_a_pack_that_holds_every_cell)
    {
        const std::filesystem::path folder = scratch("job1");
        FakeLand land(world());
        BakeJob job(land, configFor(folder));
        ASSERT_TRUE(runJob(job));
        const JobResult& r = job.result();
        ASSERT_FALSE(r.mError) << r.mError.describe();
        EXPECT_FALSE(r.mUpToDate);
        EXPECT_EQ(r.mBaked, 4u);
        EXPECT_EQ(r.mCellsInPack, 4u);
        EXPECT_EQ(r.mReadBack, 4u);
        EXPECT_EQ(r.mGeneration, 1u);
        EXPECT_GT(r.mPackBytes, 0u);
        EXPECT_GE(r.mPitsInCells, 3u);
        Error error;
        const auto pack = PuddlePack::open(folder / "puddles.edenpak", error);
        ASSERT_TRUE(pack) << error.describe();
        EXPECT_EQ(pack->cells().size(), 4u);
        EXPECT_FALSE(std::filesystem::exists(folder / "work"));
    }

    TEST(PuddleJob, two_bakes_are_byte_identical_whatever_order_the_cells_come_in)
    {
        const std::filesystem::path a = scratch("job2a");
        const std::filesystem::path b = scratch("job2b");
        FakeLand landA(world());
        FakeLand landB(world());
        BakeJob jobA(landA, configFor(a));
        JobConfig reversed = configFor(b);
        std::reverse(reversed.mLandCells.begin(), reversed.mLandCells.end());
        BakeJob jobB(landB, reversed);
        ASSERT_TRUE(runJob(jobA));
        ASSERT_TRUE(runJob(jobB));
        ASSERT_FALSE(jobA.result().mError);
        ASSERT_FALSE(jobB.result().mError);
        EXPECT_EQ(fileBytes(a / "puddles.edenpak"), fileBytes(b / "puddles.edenpak"));
        // And a compressed pack is the same on the same machine.
        const std::filesystem::path c = scratch("job2c");
        const std::filesystem::path d = scratch("job2d");
        JobConfig compressed = configFor(c);
        compressed.mStoreOnly = false;
        FakeLand landC(world());
        BakeJob jobC(landC, compressed);
        compressed.mFolder = d;
        FakeLand landD(world());
        BakeJob jobD(landD, compressed);
        ASSERT_TRUE(runJob(jobC));
        ASSERT_TRUE(runJob(jobD));
        EXPECT_EQ(fileBytes(c / "puddles.edenpak"), fileBytes(d / "puddles.edenpak"));
    }

    TEST(PuddleJob, a_second_run_is_up_to_date_and_writes_nothing)
    {
        const std::filesystem::path folder = scratch("job3");
        FakeLand land(world());
        {
            BakeJob first(land, configFor(folder));
            ASSERT_TRUE(runJob(first));
            ASSERT_FALSE(first.result().mError);
        }
        const std::vector<std::uint8_t> before = fileBytes(folder / "puddles.edenpak");
        const auto stamp = std::filesystem::last_write_time(folder / "puddles.edenpak");
        BakeJob second(land, configFor(folder));
        ASSERT_TRUE(runJob(second));
        const JobResult& r = second.result();
        ASSERT_FALSE(r.mError);
        EXPECT_TRUE(r.mUpToDate);
        EXPECT_EQ(r.mBaked, 0u);
        EXPECT_EQ(r.mFromPack, 4u);
        EXPECT_EQ(r.mGeneration, 1u);
        EXPECT_EQ(fileBytes(folder / "puddles.edenpak"), before);
        EXPECT_EQ(std::filesystem::last_write_time(folder / "puddles.edenpak"), stamp);
    }

    TEST(PuddleJob, changed_land_rebakes_only_the_cells_whose_window_changed)
    {
        const std::filesystem::path folder = scratch("job4");
        FakeLand land(world());
        {
            BakeJob first(land, configFor(folder));
            ASSERT_TRUE(runJob(first));
        }
        // A change at vertex (14, 1), inside cell 3 (vertices 12 to 16): cells 2, 3 (and 4, which is not in the world)
        // see it with halo 1; cell 0 and 1 do not.
        FakeLand changed([](int vx, int vy) { return (vx == 14 && vy == 1) ? 140.f : world()(vx, vy); });
        BakeJob second(changed, configFor(folder));
        ASSERT_TRUE(runJob(second));
        const JobResult& r = second.result();
        ASSERT_FALSE(r.mError);
        EXPECT_FALSE(r.mUpToDate);
        EXPECT_EQ(r.mBaked, 2u);
        EXPECT_EQ(r.mFromPack, 2u);
        EXPECT_EQ(r.mGeneration, 2u);
    }

    TEST(PuddleJob, other_settings_ignore_the_old_pack)
    {
        const std::filesystem::path folder = scratch("job5");
        FakeLand land(world());
        {
            BakeJob first(land, configFor(folder));
            ASSERT_TRUE(runJob(first));
        }
        JobConfig other = configFor(folder);
        other.mSettings.mMinPondTexels = 5;
        BakeJob second(land, other);
        ASSERT_TRUE(runJob(second));
        EXPECT_EQ(second.result().mBaked, 4u);
        EXPECT_EQ(second.result().mFromPack, 0u);
    }

    TEST(PuddleJob, a_killed_and_resumed_bake_equals_an_uninterrupted_one)
    {
        const std::filesystem::path whole = scratch("job6whole");
        const std::filesystem::path resumed = scratch("job6resumed");
        FakeLand landA(world());
        BakeJob full(landA, configFor(whole));
        ASSERT_TRUE(runJob(full));

        FakeLand landB(world());
        {
            // Run until two cells are baked, then drop the job (a kill).
            BakeJob partway(landB, configFor(resumed));
            while (partway.result().mBaked < 2 && !partway.result().mFinished)
                partway.step(std::chrono::milliseconds(0));
            ASSERT_EQ(partway.result().mBaked, 2u);
            ASSERT_FALSE(partway.result().mFinished);
        }
        EXPECT_TRUE(std::filesystem::exists(resumed / "work"));
        EXPECT_FALSE(std::filesystem::exists(resumed / "puddles.edenpak"));
        FakeLand landC(world());
        BakeJob second(landC, configFor(resumed));
        ASSERT_TRUE(runJob(second));
        ASSERT_FALSE(second.result().mError);
        EXPECT_EQ(second.result().mFromPartial, 2u);
        EXPECT_EQ(second.result().mBaked, 2u);
        EXPECT_EQ(fileBytes(whole / "puddles.edenpak"), fileBytes(resumed / "puddles.edenpak"));
        EXPECT_FALSE(std::filesystem::exists(resumed / "work"));
    }

    TEST(PuddleJob, a_range_bakes_those_cells_and_keeps_valid_ones_from_the_pack_on_disk)
    {
        const std::filesystem::path folder = scratch("job7");
        FakeLand land(world());
        {
            JobConfig range = configFor(folder);
            range.mRange = std::array<std::int32_t, 4>{ 0, 0, 1, 0 };
            BakeJob job(land, range);
            ASSERT_TRUE(runJob(job));
            ASSERT_FALSE(job.result().mError);
            EXPECT_EQ(job.result().mBaked, 2u);
            EXPECT_EQ(job.result().mSkipped, 2u);
            EXPECT_EQ(job.result().mCellsInPack, 2u);
        }
        {
            JobConfig range = configFor(folder);
            range.mRange = std::array<std::int32_t, 4>{ 2, 0, 3, 0 };
            BakeJob job(land, range);
            ASSERT_TRUE(runJob(job));
            ASSERT_FALSE(job.result().mError);
            EXPECT_EQ(job.result().mBaked, 2u);
            EXPECT_EQ(job.result().mFromPack, 2u);
            EXPECT_EQ(job.result().mCellsInPack, 4u);
        }
        // The result equals a full bake.
        const std::filesystem::path full = scratch("job7full");
        FakeLand landFull(world());
        BakeJob job(landFull, configFor(full));
        ASSERT_TRUE(runJob(job));
        Error error;
        const auto a = PuddlePack::open(folder / "puddles.edenpak", error);
        const auto b = PuddlePack::open(full / "puddles.edenpak", error);
        ASSERT_TRUE(a && b);
        EXPECT_EQ(a->sourceDigest(), b->sourceDigest());
        for (const auto& cell : b->cells())
        {
            CellMap x;
            CellMap y;
            ASSERT_FALSE(a->readCell(cell.first, cell.second, x));
            ASSERT_FALSE(b->readCell(cell.first, cell.second, y));
            EXPECT_EQ(x, y);
        }
    }

    TEST(PuddleJob, bad_settings_and_no_land_are_errors_not_crashes)
    {
        FakeLand land(world());
        JobConfig bad = configFor(scratch("job8"));
        bad.mSettings.mSpacingUnits = 48;
        BakeJob a(land, bad);
        EXPECT_TRUE(a.result().mFinished);
        EXPECT_EQ(a.result().mError.mField, "spacing");
        EXPECT_TRUE(a.step(std::chrono::milliseconds(1)));
        JobConfig none = configFor(scratch("job9"));
        none.mLandCells.clear();
        BakeJob b(land, none);
        EXPECT_TRUE(b.result().mFinished);
        EXPECT_EQ(b.result().mError.mField, "cells");
    }

    TEST(PuddleJob, the_digest_pass_reads_each_cell_a_bounded_number_of_times)
    {
        const std::filesystem::path folder = scratch("job10");
        FakeLand land(world());
        BakeJob job(land, configFor(folder));
        ASSERT_TRUE(runJob(job));
        // Four cells with halo 1: the cache keeps neighbours, so far fewer than 4 * 9 + 4 * 9 reads happen.
        EXPECT_LT(land.mReads, 40u);
    }

    // ---- the window ----

    TEST(PuddleWindow, it_loads_the_cells_round_the_centre_and_drops_the_others)
    {
        const std::filesystem::path folder = scratch("win1");
        FakeLand land(world());
        BakeJob job(land, configFor(folder));
        ASSERT_TRUE(runJob(job));
        Error error;
        std::shared_ptr<const PuddlePack> pack = PuddlePack::open(folder / "puddles.edenpak", error);
        ASSERT_TRUE(pack);
        std::vector<std::string> warnings;
        FakeLand now(world());
        PuddleWindow window(
            pack, [&](std::int32_t x, std::int32_t y) { return inputDigest(now, x, y, smallSettings()); },
            [&](const std::string& text) { warnings.push_back(text); }, 1);
        window.update(0, 0);
        EXPECT_EQ(window.loadedCount(), 2u); // cells (0,0) and (1,0); the others are not in the pack
        ASSERT_TRUE(window.find(0, 0));
        EXPECT_EQ(window.find(0, 0)->mCellX, 0);
        EXPECT_TRUE(window.find(1, 0));
        EXPECT_FALSE(window.find(3, 0));
        window.update(3, 0);
        EXPECT_FALSE(window.find(0, 0));
        EXPECT_TRUE(window.find(3, 0));
        EXPECT_TRUE(window.find(2, 0));
        EXPECT_TRUE(warnings.empty());
    }

    TEST(PuddleWindow, stale_or_foreign_cells_are_ignored_with_one_warning)
    {
        const std::filesystem::path folder = scratch("win2");
        FakeLand land(world());
        BakeJob job(land, configFor(folder));
        ASSERT_TRUE(runJob(job));
        Error error;
        std::shared_ptr<const PuddlePack> pack = PuddlePack::open(folder / "puddles.edenpak", error);
        ASSERT_TRUE(pack);
        std::vector<std::string> warnings;
        // The game now has other land at vertex (6, 1) (cell 1): cells 0, 1 and 2 see it.
        FakeLand changed([](int vx, int vy) { return (vx == 6 && vy == 1) ? 140.f : world()(vx, vy); });
        PuddleWindow window(
            pack, [&](std::int32_t x, std::int32_t y) { return inputDigest(changed, x, y, smallSettings()); },
            [&](const std::string& text) { warnings.push_back(text); }, 3);
        window.update(1, 0);
        EXPECT_EQ(window.loadedCount(), 1u); // only cell 3 is still valid
        EXPECT_TRUE(window.find(3, 0));
        EXPECT_FALSE(window.find(0, 0));
        EXPECT_EQ(window.ignoredCount(), 3u);
        ASSERT_EQ(warnings.size(), 1u);
        EXPECT_NE(warnings[0].find("other land"), std::string::npos);
        window.update(1, 0);
        EXPECT_EQ(warnings.size(), 1u);
    }

    TEST(PuddleWindow, a_cell_that_cannot_be_checked_is_not_used)
    {
        const std::filesystem::path folder = scratch("win3");
        FakeLand land(world());
        BakeJob job(land, configFor(folder));
        ASSERT_TRUE(runJob(job));
        Error error;
        std::shared_ptr<const PuddlePack> pack = PuddlePack::open(folder / "puddles.edenpak", error);
        ASSERT_TRUE(pack);
        int warned = 0;
        PuddleWindow window(
            pack, [](std::int32_t, std::int32_t) { return std::nullopt; }, [&](const std::string&) { ++warned; }, 3);
        window.update(1, 0);
        EXPECT_EQ(window.loadedCount(), 0u);
        EXPECT_EQ(warned, 1);
    }
}
