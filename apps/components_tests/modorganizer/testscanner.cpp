#include <components/modorganizer/scanner.hpp>

#include "tempfolder.hpp"

#include <gtest/gtest.h>

using namespace ModOrganizer;
using ModOrganizerTest::TempFolder;

TEST(ModOrganizerScanner, finds_plugins_archives_and_packs_at_the_top_level_only_and_sorted)
{
    TempFolder t;
    t.write("Zeta.ESP");
    t.write("alpha.esm");
    t.write("beta.omwaddon");
    t.write("scripts.omwscripts");
    t.write("game.omwgame");
    t.write("art.BSA");
    t.write("art2.ba2");
    t.write("p.edenpak");
    t.write("meshes/deep/inner.esp"); // not a plugin the engine would find
    t.write("textures/a.dds", "12345");
    const ScanResult r = scanDataRoot(t.path());
    EXPECT_EQ(r.mPlugins,
        (std::vector<std::string>{ "alpha.esm", "beta.omwaddon", "game.omwgame", "scripts.omwscripts", "Zeta.ESP" }));
    EXPECT_EQ(r.mArchives, (std::vector<std::string>{ "art.BSA", "art2.ba2" }));
    EXPECT_EQ(r.mPacks, (std::vector<std::string>{ "p.edenpak" }));
    EXPECT_EQ(r.mFileCount, 10u);
    EXPECT_EQ(r.mTotalBytes, 14u);
    EXPECT_FALSE(r.mTruncated);
    for (const ScanProblem& p : r.mProblems)
        ADD_FAILURE() << p.mPath << ": " << p.mMessage;
}

TEST(ModOrganizerScanner, a_missing_root_is_reported)
{
    TempFolder t;
    EXPECT_TRUE(scanDataRoot(t.path() / "nope").mRootMissing);
}

TEST(ModOrganizerScanner, names_that_are_not_safe_to_extract_are_problems_not_crashes)
{
    TempFolder t;
    t.write("textures/ok.dds");
    // Windows cannot create these names at all; the check is the same function the VFS tests cover.
    std::error_code ec;
    std::filesystem::create_directories(t.path() / "meshes");
    std::ofstream(t.path() / "meshes" / "aux.nif") << "x";
    if (!std::filesystem::exists(t.path() / "meshes" / "aux.nif"))
        GTEST_SKIP() << "this file system cannot hold a Windows device name";
    const ScanResult r = scanDataRoot(t.path());
    bool sawAux = false;
    for (const ScanProblem& p : r.mProblems)
        sawAux = sawAux || p.mPath == "meshes/aux.nif";
    EXPECT_TRUE(sawAux);
}

TEST(ModOrganizerScanner, limits_truncate_instead_of_running_on)
{
    TempFolder t;
    for (int i = 0; i < 20; ++i)
        t.write("f" + std::to_string(i) + ".txt");
    ScanLimits limits;
    limits.mMaxFiles = 5;
    const ScanResult r = scanDataRoot(t.path(), limits);
    EXPECT_TRUE(r.mTruncated);
    EXPECT_LE(r.mFileCount, 5u);
}

TEST(ModOrganizerScanner, depth_limit_is_a_problem)
{
    TempFolder t;
    t.write("a/b/c/d/e.txt");
    ScanLimits limits;
    limits.mMaxDepth = 2;
    const ScanResult r = scanDataRoot(t.path(), limits);
    EXPECT_FALSE(r.mProblems.empty());
}

TEST(ModOrganizerScanner, problems_are_capped_and_counted)
{
    TempFolder t;
    for (int i = 0; i < 10; ++i)
        t.write("x/d" + std::to_string(i) + "/e/f.txt");
    ScanLimits limits;
    limits.mMaxDepth = 2;
    limits.mMaxProblems = 3;
    const ScanResult r = scanDataRoot(t.path(), limits);
    EXPECT_EQ(r.mProblems.size(), 3u);
    EXPECT_EQ(r.mProblems.size() + r.mProblemsNotListed, 10u);
}

TEST(ModOrganizerScanner, a_symbolic_link_is_not_followed)
{
    TempFolder t;
    TempFolder outside;
    outside.write("secret/a.esp");
    std::error_code ec;
    std::filesystem::create_directory_symlink(outside.path(), t.path() / "link", ec);
    if (ec)
        GTEST_SKIP() << "symlinks not available: " << ec.message();
    t.write("real.esp");
    const ScanResult r = scanDataRoot(t.path());
    EXPECT_EQ(r.mPlugins, (std::vector<std::string>{ "real.esp" }));
    EXPECT_EQ(r.mFileCount, 1u);
    EXPECT_FALSE(r.mProblems.empty()) << "the link is named";
}

TEST(ModOrganizerScanner, scanning_changes_no_file)
{
    TempFolder t;
    const auto file = t.write("a.esp", "content");
    const auto before = std::filesystem::last_write_time(file);
    scanDataRoot(t.path());
    EXPECT_EQ(std::filesystem::last_write_time(file), before);
    EXPECT_EQ(TempFolder::read(file), "content");
}

TEST(ModOrganizerScanner, detect_data_path)
{
    {
        TempFolder t;
        t.write("meshes/a.nif");
        EXPECT_EQ(detectDataPath(t.path()), "");
    }
    {
        TempFolder t;
        t.write("mod.esp");
        EXPECT_EQ(detectDataPath(t.path()), "");
    }
    {
        TempFolder t;
        t.write("Data Files/meshes/a.nif");
        t.write("readme.txt");
        EXPECT_EQ(detectDataPath(t.path()), "Data Files");
    }
    {
        TempFolder t;
        t.write("wrapper/inner/textures/a.dds");
        EXPECT_EQ(detectDataPath(t.path()), "wrapper/inner");
    }
    {
        TempFolder t;
        t.write("readme.txt");
        EXPECT_EQ(detectDataPath(t.path()), "");
    }
    {
        TempFolder t;
        t.write("b/meshes/a.nif");
        t.write("a/meshes/a.nif");
        EXPECT_EQ(detectDataPath(t.path()), "a") << "ties go to the first lower-case name";
    }
}

TEST(ModOrganizerScanner, eden_pack_magic)
{
    TempFolder t;
    const auto good = t.write("g.edenpak", std::string("EDENPAK") + '\x01' + "rest");
    const auto bad = t.write("b.edenpak", "not a pack");
    const auto tiny = t.write("t.edenpak", "EDEN");
    EXPECT_TRUE(looksLikeEdenPack(good));
    EXPECT_FALSE(looksLikeEdenPack(bad));
    EXPECT_FALSE(looksLikeEdenPack(tiny));
    EXPECT_FALSE(looksLikeEdenPack(t.path()));
    EXPECT_FALSE(looksLikeEdenPack(t.path() / "missing"));
}
