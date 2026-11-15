// Task 0173, fresh review fixes: a game switch is no hand edit, a game= line is imported, renaming or deleting a
// profile keeps the last profile of each game. Written before the fixes and run red against them (stage log,
// section 9.1).
#include <components/modorganizer/configfile.hpp>
#include <components/modorganizer/games.hpp>
#include <components/modorganizer/profilestore.hpp>

#include "tempfolder.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace ModOrganizer;
using ModOrganizerTest::TempFolder;

namespace
{
    namespace fs = std::filesystem;

    Profile profileOf(const std::string& name, const std::string& game, const std::string& plugin)
    {
        Profile profile;
        profile.mName = name;
        profile.mGame = game;
        profile.mPlugins = { { plugin, true } };
        return profile;
    }

    Store twoGames()
    {
        Store store;
        store.mProfiles = { profileOf("Default", "", "Morrowind.esm"), profileOf("Skyrim", "skyrimse", "Skyrim.esm") };
        store.mActive = "Default";
        store.mBaseData = { "C:/Games/Morrowind/Data Files" };
        store.mGameBaseData["skyrimse"] = { "D:/Skyrim/Data" };
        return store;
    }

    ApplyOptions noGameRunning()
    {
        ApplyOptions options;
        options.mGameRunning = [] { return false; };
        return options;
    }
}

TEST(ModOrganizerGamesReview, the_lines_a_profile_wrote_match_its_last_apply)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const fs::path cfg = temp.write("cfg/openmw.cfg", "encoding=win1252\n");
    Store store = twoGames();
    ASSERT_TRUE(applyActiveProfile(paths, store, cfg, noGameRunning(), false).mOk);
    EXPECT_TRUE(configMatchesLastApply(paths, "Default", cfg));
}

TEST(ModOrganizerGamesReview, a_hand_edit_after_the_last_apply_does_not_match)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const fs::path cfg = temp.write("cfg/openmw.cfg", "encoding=win1252\n");
    Store store = twoGames();
    ASSERT_TRUE(applyActiveProfile(paths, store, cfg, noGameRunning(), false).mOk);
    temp.write("cfg/openmw.cfg", "encoding=win1252\ncontent=Handmade.esp\n");
    EXPECT_FALSE(configMatchesLastApply(paths, "Default", cfg));
}

TEST(ModOrganizerGamesReview, with_no_record_of_an_apply_nothing_counts_as_a_hand_edit)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const fs::path cfg = temp.write("cfg/openmw.cfg", "encoding=win1252\ncontent=Anything.esp\n");
    EXPECT_TRUE(configMatchesLastApply(paths, "Default", cfg));
}

TEST(ModOrganizerGamesReview, a_round_trip_through_another_game_is_no_hand_edit)
{
    // Morrowind applied, then Skyrim applied over it, then back: the lines in the file are Skyrim's last apply, so the
    // switch back can be applied without asking (finding 1 of the review)
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const fs::path cfg = temp.write("cfg/openmw.cfg", "encoding=win1252\n");
    Store store = twoGames();
    ASSERT_TRUE(applyActiveProfile(paths, store, cfg, noGameRunning(), false).mOk);
    ASSERT_TRUE(selectGame(store, "skyrimse"));
    ASSERT_TRUE(
        applyActiveProfile(paths, store, cfg, noGameRunning(), configMatchesLastApply(paths, "Default", cfg)).mOk);
    ASSERT_TRUE(selectGame(store, "morrowind"));
    EXPECT_TRUE(configMatchesLastApply(paths, "Skyrim", cfg));
    const ApplyOutcome back
        = applyActiveProfile(paths, store, cfg, noGameRunning(), configMatchesLastApply(paths, "Skyrim", cfg));
    EXPECT_TRUE(back.mOk) << back.mError;
    EXPECT_FALSE(back.mHandEdited);
}

TEST(ModOrganizerGamesReview, the_game_line_is_read_into_the_snapshot)
{
    const ConfigSnapshot snapshot
        = snapshotFromConfigText("data=\"D:/Skyrim/Data\"\ngame=skyrimse\ncontent=Skyrim.esm\n");
    EXPECT_EQ(snapshot.mGame, "skyrimse");
    EXPECT_TRUE(snapshotFromConfigText("content=Morrowind.esm\n").mGame.empty());
}

TEST(ModOrganizerGamesReview, an_imported_game_line_becomes_the_profiles_game)
{
    ConfigSnapshot snapshot = snapshotFromConfigText("data=\"D:/Skyrim/Data\"\ngame=skyrimse\ncontent=Skyrim.esm\n");
    const ImportResult imported = importFromConfig(snapshot, "Default");
    EXPECT_EQ(imported.mProfile.mGame, "skyrimse");
    // and a Morrowind import stays a Morrowind profile
    EXPECT_TRUE(importFromConfig(snapshotFromConfigText("content=Morrowind.esm\n"), "Default").mProfile.mGame.empty());
    // "morrowind" is Morrowind, which a profile stores as no game
    EXPECT_TRUE(importFromConfig(snapshotFromConfigText("game=morrowind\n"), "Default").mProfile.mGame.empty());
}

TEST(ModOrganizerGamesReview, renaming_a_profile_keeps_it_the_last_profile_of_its_game)
{
    Store store = twoGames();
    store.mLastProfileOfGame["skyrimse"] = "Skyrim";
    ASSERT_TRUE(renameProfile(store, "Skyrim", "Skyrim Main"));
    EXPECT_EQ(store.mLastProfileOfGame["skyrimse"], "Skyrim Main");
}

TEST(ModOrganizerGamesReview, deleting_the_active_profile_prefers_another_of_the_same_game)
{
    Store store = twoGames();
    store.mProfiles.push_back(profileOf("Skyrim Two", "skyrimse", "Update.esm"));
    store.mActive = "Skyrim";
    store.mLastProfileOfGame["skyrimse"] = "Skyrim";
    ASSERT_TRUE(deleteProfile(store, "Skyrim"));
    EXPECT_EQ(store.mActive, "Skyrim Two");
    EXPECT_EQ(
        store.mLastProfileOfGame.count("skyrimse") == 0 || store.mLastProfileOfGame["skyrimse"] != "Skyrim", true);
}
