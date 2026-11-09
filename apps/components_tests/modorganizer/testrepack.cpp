#include <components/modorganizer/conflicts.hpp>
#include <components/modorganizer/repack.hpp>

#include "tempfolder.hpp"

#include <edenarchive/hash.hpp>
#include <edenarchive/writer.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

using namespace ModOrganizer;
using ModOrganizerTest::TempFolder;

namespace
{
    // The mod: loose files, a plugin and an archive at the top level that stay on disk.
    void makeMod(const TempFolder& t)
    {
        t.write("mod.esp", "plugin bytes");
        t.write("art.bsa", "archive bytes");
        t.write("meshes/a.nif", "mesh a");
        t.write("Textures/B.DDS", std::string(5000, 'b'));
        t.write("sound/deep/c.wav", "wave");
        t.write("meshes/inner.esp", "not a top level plugin");
    }

    // Packs the stage folder in the test itself (the real flow runs edenpak as a child process).
    bool packStage(const std::filesystem::path& stage, const std::filesystem::path& out)
    {
        std::vector<EdenArchive::InputEntry> entries;
        for (const auto& item : std::filesystem::recursive_directory_iterator(stage))
        {
            if (!item.is_regular_file())
                continue;
            EdenArchive::InputEntry e;
            std::string rel = std::filesystem::relative(item.path(), stage).generic_string();
            std::transform(rel.begin(), rel.end(), rel.begin(), [](unsigned char c) { return std::tolower(c); });
            e.mPath = rel;
            e.mFile = item.path();
            e.mCompression = EdenArchive::Compression::Store;
            entries.push_back(std::move(e));
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return *a.mPath < *b.mPath; });
        EdenArchive::WriteOptions options;
        options.mStoreOnly = true;
        return EdenArchive::writePack(entries, out, options).ok();
    }
}

TEST(ModOrganizerRepack, what_stays_on_disk)
{
    EXPECT_TRUE(staysOnDisk("mod.esp"));
    EXPECT_TRUE(staysOnDisk("Mod.ESM"));
    EXPECT_TRUE(staysOnDisk("x.omwscripts"));
    EXPECT_TRUE(staysOnDisk("art.bsa"));
    EXPECT_TRUE(staysOnDisk("art.BA2"));
    EXPECT_FALSE(staysOnDisk("meshes/inner.esp"));
    EXPECT_FALSE(staysOnDisk("textures/a.dds"));
}

TEST(ModOrganizerRepack, dry_run_counts_splits_and_estimates)
{
    TempFolder mod, target;
    makeMod(mod);
    const DryRun d = planRepack(mod.path(), target.path());
    EXPECT_EQ(d.mPackFileCount, 4u);
    EXPECT_EQ(d.mPackSourceBytes, 6u + 5000u + 4u + 22u);
    EXPECT_EQ(d.mStayOnDisk, (std::vector<std::string>{ "art.bsa", "mod.esp" }));
    EXPECT_TRUE(d.canRepack());
    EXPECT_GE(d.mEstimatedPackBytes, 4u * sPackBlockBytes) << "every file costs at least a block";
    EXPECT_GT(d.mFreeBytes, 0u);
    EXPECT_TRUE(d.mFits);
}

TEST(ModOrganizerRepack, many_tiny_files_are_flagged_as_probably_larger)
{
    TempFolder mod, target;
    for (int i = 0; i < 50; ++i)
        mod.write("meshes/t" + std::to_string(i) + ".nif", "tiny");
    const DryRun d = planRepack(mod.path(), target.path());
    EXPECT_TRUE(d.mProbablyLarger);
}

TEST(ModOrganizerRepack, names_that_collide_after_lower_casing_block_the_repack)
{
    TempFolder mod, target;
    mod.write("meshes/A.nif", "1");
    mod.write("meshes/a.nif", "2");
    std::error_code ec;
    if (std::filesystem::equivalent(mod.path() / "meshes" / "A.nif", mod.path() / "meshes" / "a.nif", ec))
        GTEST_SKIP() << "case-insensitive file system cannot hold both";
    const DryRun d = planRepack(mod.path(), target.path());
    ASSERT_EQ(d.mCollisions.size(), 1u);
    EXPECT_EQ(d.mCollisions[0].mNormalized, "meshes/a.nif");
    EXPECT_EQ(d.mCollisions[0].mOriginals.size(), 2u);
    EXPECT_FALSE(d.canRepack());
}

TEST(ModOrganizerRepack, fingerprint_depends_on_content_not_on_time_or_order)
{
    TempFolder one, two;
    makeMod(one);
    // The same files created in another order, later.
    two.write("sound/deep/c.wav", "wave");
    two.write("meshes/inner.esp", "not a top level plugin");
    two.write("Textures/B.DDS", std::string(5000, 'b'));
    two.write("meshes/a.nif", "mesh a");
    two.write("art.bsa", "archive bytes");
    two.write("mod.esp", "plugin bytes");
    const Fingerprint a = computeFingerprint(one.path());
    const Fingerprint b = computeFingerprint(two.path());
    ASSERT_TRUE(a.mOk) << a.mError;
    ASSERT_TRUE(b.mOk);
    EXPECT_EQ(a.mHex.size(), 64u);
    EXPECT_EQ(a.mHex, b.mHex);
    EXPECT_EQ(a.mFiles, 4u);
    two.write("meshes/a.nif", "mesh A");
    EXPECT_NE(computeFingerprint(two.path()).mHex, a.mHex);
}

TEST(ModOrganizerRepack, the_plugin_and_archive_do_not_change_the_fingerprint_but_a_case_rename_does_not_either)
{
    TempFolder one, two;
    makeMod(one);
    makeMod(two);
    two.write("mod.esp", "a different plugin");
    EXPECT_EQ(computeFingerprint(one.path()).mHex, computeFingerprint(two.path()).mHex)
        << "files that stay on disk are not in the pack";
}

TEST(ModOrganizerRepack, a_cancelled_fingerprint_is_not_ok)
{
    TempFolder mod;
    makeMod(mod);
    std::atomic<bool> cancel{ true };
    const Fingerprint f = computeFingerprint(mod.path(), &cancel);
    EXPECT_FALSE(f.mOk);
    EXPECT_FALSE(f.mError.empty());
}

TEST(ModOrganizerRepack, key_depends_on_every_input)
{
    PackerSettings s;
    const std::string base = repackKey(std::string(64, 'a'), s);
    EXPECT_EQ(base.size(), 32u);
    EXPECT_EQ(base, repackKey(std::string(64, 'a'), s));
    EXPECT_NE(base, repackKey(std::string(64, 'b'), s));
    PackerSettings t = s;
    t.mVersion = "2";
    EXPECT_NE(base, repackKey(std::string(64, 'a'), t));
    t = s;
    t.mMode = "optimise";
    EXPECT_NE(base, repackKey(std::string(64, 'a'), t));
    t = s;
    t.mChunkSize = 4096;
    EXPECT_NE(base, repackKey(std::string(64, 'a'), t));
    t = s;
    t.mLevel = 5;
    EXPECT_NE(base, repackKey(std::string(64, 'a'), t));
    EXPECT_EQ(packFileName("mod-a", base), "mod-a-" + base + ".edenpak");
}

TEST(ModOrganizerRepack, packer_arguments)
{
    PackerSettings s;
    const auto args = edenpackArguments(s, "in", "out.edenpak");
    EXPECT_EQ(args, (std::vector<std::string>{ "--store", "--normalize-names", "in", "out.edenpak" }));
}

TEST(ModOrganizerRepack, stage_and_shim_split_the_files_and_leave_the_originals_alone)
{
    TempFolder mod, work;
    makeMod(mod);
    const auto before = std::filesystem::last_write_time(mod.path() / "mod.esp");
    const StageResult stage = buildPackStage(mod.path(), work.path() / "stage");
    const StageResult shim = buildShim(mod.path(), work.path() / "shim");
    ASSERT_TRUE(stage.mOk) << stage.mError;
    ASSERT_TRUE(shim.mOk) << shim.mError;
    EXPECT_EQ(stage.mFiles, 4u);
    EXPECT_EQ(shim.mFiles, 2u);
    EXPECT_TRUE(std::filesystem::exists(work.path() / "stage" / "meshes" / "a.nif"));
    EXPECT_FALSE(std::filesystem::exists(work.path() / "stage" / "mod.esp"));
    EXPECT_TRUE(std::filesystem::exists(work.path() / "shim" / "mod.esp"));
    EXPECT_FALSE(std::filesystem::exists(work.path() / "shim" / "meshes"));
    EXPECT_EQ(TempFolder::read(work.path() / "shim" / "mod.esp"), "plugin bytes");
    EXPECT_EQ(std::filesystem::last_write_time(mod.path() / "mod.esp"), before);
    // A second try into a non-empty folder is refused.
    EXPECT_FALSE(buildShim(mod.path(), work.path() / "shim").mOk);
}

TEST(ModOrganizerRepack, a_good_pack_verifies_and_a_flipped_byte_does_not)
{
    TempFolder mod, work;
    makeMod(mod);
    ASSERT_TRUE(buildPackStage(mod.path(), work.path() / "stage").mOk);
    ASSERT_TRUE(packStage(work.path() / "stage", work.path() / "m.edenpak"));
    const VerifyResult good = verifyPack(work.path() / "m.edenpak", mod.path());
    EXPECT_TRUE(good.mOk) << good.mError;
    EXPECT_EQ(good.mEntries, 4u);

    // Flip one byte inside the 5000 'b' bytes of the stored texture.
    std::string bytes = TempFolder::read(work.path() / "m.edenpak");
    const std::size_t at = bytes.find(std::string(100, 'b'));
    ASSERT_NE(at, std::string::npos) << "stored mode keeps the bytes readable";
    bytes[at + 50] = 'X';
    work.write("bad.edenpak", bytes);
    const VerifyResult bad = verifyPack(work.path() / "bad.edenpak", mod.path());
    EXPECT_FALSE(bad.mOk);
    EXPECT_FALSE(bad.mError.empty());
}

TEST(ModOrganizerRepack, a_changed_source_after_packing_fails_verification_and_names_the_file)
{
    TempFolder mod, work;
    makeMod(mod);
    ASSERT_TRUE(buildPackStage(mod.path(), work.path() / "stage").mOk);
    ASSERT_TRUE(packStage(work.path() / "stage", work.path() / "m.edenpak"));
    mod.write("meshes/a.nif", "mesh Z");
    const VerifyResult r = verifyPack(work.path() / "m.edenpak", mod.path());
    EXPECT_FALSE(r.mOk);
    EXPECT_NE(r.mError.find("meshes/a.nif"), std::string::npos);
}

TEST(ModOrganizerRepack, an_extra_or_missing_entry_fails)
{
    TempFolder mod, work;
    makeMod(mod);
    ASSERT_TRUE(buildPackStage(mod.path(), work.path() / "stage").mOk);
    std::filesystem::remove(work.path() / "stage" / "sound" / "deep" / "c.wav");
    ASSERT_TRUE(packStage(work.path() / "stage", work.path() / "m.edenpak"));
    EXPECT_FALSE(verifyPack(work.path() / "m.edenpak", mod.path()).mOk) << "source file with no entry";
    ASSERT_TRUE(buildPackStage(mod.path(), work.path() / "stage2").mOk);
    work.write("stage2/extra.txt", "x");
    ASSERT_TRUE(packStage(work.path() / "stage2", work.path() / "m2.edenpak"));
    EXPECT_FALSE(verifyPack(work.path() / "m2.edenpak", mod.path()).mOk) << "entry with no source file";
}

TEST(ModOrganizerRepack, a_file_that_is_not_a_pack_fails_verification)
{
    TempFolder mod, work;
    makeMod(mod);
    EXPECT_FALSE(verifyPack(work.write("x.edenpak", "junk"), mod.path()).mOk);
    EXPECT_FALSE(verifyPack(work.path() / "missing.edenpak", mod.path()).mOk);
}

TEST(ModOrganizerRepack, a_full_repack_and_swap_cycle_leaves_every_original_byte_and_time_alone)
{
    TempFolder mod, work;
    makeMod(mod);
    std::vector<std::pair<std::filesystem::path, std::pair<std::string, std::filesystem::file_time_type>>> before;
    for (const auto& item : std::filesystem::recursive_directory_iterator(mod.path()))
        if (item.is_regular_file())
            before.push_back({ item.path(), { TempFolder::read(item.path()), item.last_write_time() } });

    ASSERT_TRUE(computeFingerprint(mod.path()).mOk);
    ASSERT_TRUE(buildPackStage(mod.path(), work.path() / "stage").mOk);
    ASSERT_TRUE(buildShim(mod.path(), work.path() / "shim").mOk);
    ASSERT_TRUE(packStage(work.path() / "stage", work.path() / "m.edenpak"));
    ASSERT_TRUE(verifyPack(work.path() / "m.edenpak", mod.path()).mOk);

    for (const auto& [path, state] : before)
    {
        EXPECT_EQ(TempFolder::read(path), state.first);
        EXPECT_EQ(std::filesystem::last_write_time(path), state.second);
    }
}

TEST(ModOrganizerRepack, the_swap_proof_with_a_real_pack_and_shim_changes_no_winner)
{
    TempFolder base, other, mod, work;
    base.write("meshes/a.nif", "base");
    other.write("meshes/a.nif", "other");
    other.write("meshes/o.nif", "other only");
    makeMod(mod);
    mod.write("meshes/a.nif", "mod");
    ASSERT_TRUE(buildPackStage(mod.path(), work.path() / "stage").mOk);
    ASSERT_TRUE(buildShim(mod.path(), work.path() / "shim").mOk);
    ASSERT_TRUE(packStage(work.path() / "stage", work.path() / "m.edenpak"));

    ModLibrary lib;
    lib.mMods = { { "o1", "O", ModKind::Folder, other.path().generic_string(), "", "" },
        { "m", "M", ModKind::Folder, mod.path().generic_string(), "", "" },
        { "o2", "O2", ModKind::Folder, other.path().generic_string(), "", "" } };
    Profile profile;
    profile.mMods = { { "o1", true, false }, { "m", true, false }, { "o2", true, false } };
    LayoutOptions options;
    options.mBaseData = { base.path().generic_string() };
    const Layout original = buildLayout(lib, profile, options);
    profile.mMods[1].mUseRepack = true;
    options.mRepacks["m"] = { (work.path() / "shim").generic_string(), (work.path() / "m.edenpak").generic_string() };
    const Layout repacked = buildLayout(lib, profile, options);
    EXPECT_TRUE(differentWinners(original, repacked).empty());
    ASSERT_TRUE(repacked.mProblems.empty());

    // Plugins are still found on disk through the shim, and the assets are served from the pack.
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::u8path(repacked.mData[2].mPath) / "mod.esp"));
    const ConflictReport report = computeConflicts(repacked);
    EXPECT_TRUE(report.mErrors.empty());
}

TEST(ModOrganizerRepack, the_cache_panel_lists_marks_stale_and_clears_only_inside_a_valid_folder)
{
    TempFolder root;
    const auto cache = root.path() / "openeden" / "repack";
    root.write("openeden/repack/a-111.edenpak", std::string(100, 'x'));
    root.write("openeden/repack/a-222.edenpak", std::string(50, 'x'));
    root.write("openeden/repack/shim-a/mod.esp", "pl");
    EXPECT_TRUE(isRepackCacheFolder(cache));
    EXPECT_FALSE(isRepackCacheFolder(root.path()));
    EXPECT_FALSE(isRepackCacheFolder(root.path() / "openeden"));
    EXPECT_FALSE(isRepackCacheFolder(std::filesystem::path("C:/Users/x/Documents")));

    const CacheUse use = scanCache(cache, { "a-222.edenpak" });
    ASSERT_EQ(use.mItems.size(), 3u);
    EXPECT_EQ(use.mTotalBytes, 152u);
    EXPECT_EQ(use.mStaleBytes, 100u);
    std::vector<CacheItem> stale;
    for (const CacheItem& item : use.mItems)
        if (item.mStale)
            stale.push_back(item);
    ASSERT_EQ(stale.size(), 1u);
    EXPECT_EQ(clearCache(cache, stale), "");
    EXPECT_FALSE(std::filesystem::exists(cache / "a-111.edenpak"));
    EXPECT_TRUE(std::filesystem::exists(cache / "a-222.edenpak"));

    // Never outside the cache, never in an invalid folder.
    EXPECT_NE(clearCache(root.path(), { CacheItem{ cache / "a-222.edenpak", 50, false } }), "");
    EXPECT_TRUE(std::filesystem::exists(cache / "a-222.edenpak"));
    TempFolder elsewhere;
    const auto victim = elsewhere.write("precious.txt", "keep");
    EXPECT_NE(clearCache(cache, { CacheItem{ victim, 4, false } }), "");
    EXPECT_NE(clearCache(cache, { CacheItem{ cache / ".." / ".." / ".." / "x", 4, false } }), "");
    EXPECT_TRUE(std::filesystem::exists(victim));
}
