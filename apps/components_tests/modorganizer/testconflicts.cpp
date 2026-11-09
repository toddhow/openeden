#include <components/modorganizer/conflicts.hpp>

#include "tempfolder.hpp"

#include <components/files/collections.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include <edenarchive/writer.hpp>

#include <gtest/gtest.h>

using namespace ModOrganizer;
using ModOrganizerTest::TempFolder;

namespace
{
    std::string pathOf(const TempFolder& t, const std::string& sub)
    {
        return (t.path() / sub).generic_string();
    }

    struct Fixture
    {
        TempFolder mBase, mA, mB, mP;
        ModLibrary mLibrary;
        Profile mProfile;

        Fixture()
        {
            mBase.write("meshes/a.nif", "base");
            mBase.write("textures/t.dds", "base");
            mA.write("meshes/A.NIF", "modA"); // differs by case only
            mA.write("textures/only_a.dds", "A");
            mB.write("meshes/a.nif", "modB");
            mB.write("sound/s.wav", "B");
            mB.write("textures/t.dds", "base"); // same bytes as the base: legacy files have no stored hash
            EdenArchive::InputEntry entry;
            entry.mPath = "meshes/a.nif";
            entry.mData = { 'p', 'a', 'c', 'k' };
            EdenArchive::InputEntry second;
            second.mPath = "textures/only_p.dds";
            second.mData = { '1' };
            EdenArchive::WriteOptions options;
            options.mStoreOnly = true;
            EdenArchive::writePack({ entry, second }, mP.path() / "p.edenpak", options);
            mLibrary.mMods = { { "a", "A", ModKind::Folder, pathOf(mA, ""), "", "" },
                { "b", "B", ModKind::Folder, pathOf(mB, ""), "", "" },
                { "p", "P", ModKind::Pack, pathOf(mP, "p.edenpak"), "", "" } };
            mProfile.mMods = { { "a", true, false }, { "b", true, false }, { "p", true, false } };
        }

        Layout layout() const
        {
            LayoutOptions options;
            options.mBaseData = { pathOf(mBase, "") };
            return buildLayout(mLibrary, mProfile, options);
        }
    };

    const ConflictFile* find(const ConflictReport& r, const std::string& path)
    {
        for (const ConflictFile& f : r.mFiles)
            if (f.mPath == path)
                return &f;
        return nullptr;
    }
}

TEST(ModOrganizerConflicts, winners_and_losers_with_case_only_differences_and_a_pack)
{
    Fixture f;
    const ConflictReport r = computeConflicts(f.layout());
    EXPECT_TRUE(r.mErrors.empty());
    const ConflictFile* nif = find(r, "meshes/a.nif");
    ASSERT_NE(nif, nullptr);
    EXPECT_EQ(nif->mWinner, "mod:p");
    EXPECT_EQ(nif->mLosers, (std::vector<std::string>{ "mod:b", "mod:a", "base" }));
    EXPECT_EQ(find(r, "textures/only_a.dds"), nullptr) << "provided once, no conflict";
    EXPECT_EQ(find(r, "sound/s.wav"), nullptr);
}

TEST(ModOrganizerConflicts, identical_legacy_duplicates_are_listed_as_conflicts_until_hashed)
{
    Fixture f;
    const ConflictReport r = computeConflicts(f.layout());
    const ConflictFile* t = find(r, "textures/t.dds");
    ASSERT_NE(t, nullptr) << "legacy sources have no stored hash";
    EXPECT_EQ(t->mWinner, "mod:b");
    EXPECT_FALSE(t->mIdentical);
}

TEST(ModOrganizerConflicts, per_source_counts_in_registration_order)
{
    Fixture f;
    const ConflictReport r = computeConflicts(f.layout());
    ASSERT_EQ(r.mCounts.size(), 4u);
    EXPECT_EQ(r.mCounts[0].mSource, "base");
    EXPECT_EQ(r.mCounts[0].mOverridden, 2u);
    EXPECT_EQ(r.mCounts[1].mSource, "mod:a");
    EXPECT_EQ(r.mCounts[1].mOverridden, 1u);
    EXPECT_EQ(r.mCounts[2].mSource, "mod:b");
    EXPECT_EQ(r.mCounts[2].mOverriding, 1u);
    EXPECT_EQ(r.mCounts[2].mOverridden, 1u);
    EXPECT_EQ(r.mCounts[3].mSource, "mod:p");
    EXPECT_EQ(r.mCounts[3].mOverriding, 1u);
}

TEST(ModOrganizerConflicts, disabling_a_mod_changes_the_winner)
{
    Fixture f;
    f.mProfile.mMods[2].mEnabled = false;
    const ConflictReport r = computeConflicts(f.layout());
    EXPECT_EQ(find(r, "meshes/a.nif")->mWinner, "mod:b");
}

TEST(ModOrganizerConflicts, same_profile_gives_the_same_report_every_time)
{
    Fixture f;
    const ConflictReport a = computeConflicts(f.layout());
    const ConflictReport b = computeConflicts(f.layout());
    ASSERT_EQ(a.mFiles.size(), b.mFiles.size());
    for (std::size_t i = 0; i < a.mFiles.size(); ++i)
    {
        EXPECT_EQ(a.mFiles[i].mPath, b.mFiles[i].mPath);
        EXPECT_EQ(a.mFiles[i].mWinner, b.mFiles[i].mWinner);
        EXPECT_EQ(a.mFiles[i].mLosers, b.mFiles[i].mLosers);
    }
}

TEST(ModOrganizerConflicts, the_winner_equals_what_the_real_registration_gives)
{
    Fixture f;
    const Layout layout = f.layout();
    const ConflictReport report = computeConflicts(layout);

    // The real call sequence of the engine, from the same data lines.
    Files::PathContainer dirs;
    for (const DataEntry& entry : layout.mData)
        dirs.emplace_back(std::filesystem::u8path(entry.mPath));
    Files::Collections collections(dirs);
    VFS::Manager vfs;
    VFS::registerArchives(&vfs, collections, layout.mArchives, true, nullptr);
    const std::vector<VFS::Override> overrides = vfs.getOverrides();
    ASSERT_EQ(overrides.size(), report.mFiles.size());
    for (std::size_t i = 0; i < overrides.size(); ++i)
    {
        EXPECT_EQ(overrides[i].mPath.value(), report.mFiles[i].mPath);
        const std::string winnerDir = vfs.getArchiveDescription(overrides[i].mWinner.mArchive);
        const DataEntry& entry = layout.mData[overrides[i].mWinner.mArchive];
        EXPECT_NE(winnerDir.find(std::filesystem::u8path(entry.mPath).filename().string()), std::string::npos);
        EXPECT_EQ(report.mFiles[i].mWinner, entry.mModId.empty() ? std::string("base") : "mod:" + entry.mModId);
        EXPECT_EQ(report.mFiles[i].mLosers.size(), overrides[i].mLosers.size());
    }
}

TEST(ModOrganizerConflicts, source_mod_id_mapping)
{
    EXPECT_EQ(sourceModId("mod:abc"), "abc");
    EXPECT_EQ(sourceModId("shim:abc"), "abc");
    EXPECT_EQ(sourceModId("base"), "");
    EXPECT_EQ(sourceModId("archive:x.bsa"), "");
}

TEST(ModOrganizerConflicts, a_source_that_cannot_be_opened_is_an_error_not_a_crash)
{
    Fixture f;
    f.mLibrary.mMods[0].mRoot = pathOf(f.mA, "missing-folder");
    const ConflictReport r = computeConflicts(f.layout());
    EXPECT_FALSE(r.mErrors.empty());
    EXPECT_NE(find(r, "meshes/a.nif"), nullptr);
}

TEST(ModOrganizerConflicts, a_pack_that_is_not_a_pack_is_an_error)
{
    Fixture f;
    f.mP.write("p.edenpak", "garbage");
    const ConflictReport r = computeConflicts(f.layout());
    EXPECT_FALSE(r.mErrors.empty());
}

TEST(ModOrganizerConflicts, differing_winners_between_two_layouts)
{
    Fixture f;
    const Layout loose = f.layout();
    Layout other = f.layout();
    EXPECT_TRUE(differentWinners(loose, other).empty());
    std::swap(other.mData[2], other.mData[3]); // b and p change places
    const std::vector<std::string> diff = differentWinners(loose, other);
    EXPECT_NE(std::find(diff.begin(), diff.end(), "meshes/a.nif"), diff.end());
}

TEST(ModOrganizerConflicts, a_pack_of_a_mod_replacing_its_folder_changes_no_winner)
{
    Fixture f;
    // Mod "a" repacked: a pack with the same two files at a's position.
    TempFolder cache;
    EdenArchive::InputEntry nif;
    nif.mPath = "meshes/a.nif";
    nif.mData = { 'm', 'o', 'd', 'A' };
    EdenArchive::InputEntry tex;
    tex.mPath = "textures/only_a.dds";
    tex.mData = { 'A' };
    EdenArchive::WriteOptions options;
    options.mStoreOnly = true;
    ASSERT_EQ(EdenArchive::writePack({ nif, tex }, cache.path() / "a.edenpak", options).mClass,
        EdenArchive::ErrorClass::None);
    f.mProfile.mMods[0].mUseRepack = true;
    LayoutOptions lo;
    lo.mBaseData = { pathOf(f.mBase, "") };
    lo.mRepacks["a"] = { "", pathOf(cache, "a.edenpak") };
    const Layout original = f.layout();
    const Layout repacked = buildLayout(f.mLibrary, f.mProfile, lo);
    EXPECT_TRUE(differentWinners(original, repacked).empty());
}
