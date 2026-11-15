// Task 0173 (revision, section 8): the launcher's game choice (components/modorganizer/games.hpp) and the game field of
// profiles. Written from the spec and the headers' comments, before the code. Every plugin and folder is generated in a
// temporary folder; no game or mod content is used.
#include <components/modorganizer/configfile.hpp>
#include <components/modorganizer/games.hpp>
#include <components/modorganizer/layout.hpp>
#include <components/modorganizer/model.hpp>
#include <components/modorganizer/profilestore.hpp>

#include "../contentsource/pluginbuilder.hpp"
#include "tempfolder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace ModOrganizer;
using ModOrganizerTest::TempFolder;

namespace
{
    namespace fs = std::filesystem;

    openeden::extension::ExtensionManifest manifest(
        const std::string& id, const std::string& name, const std::string& version, const std::string& kind)
    {
        openeden::extension::ExtensionManifest m;
        m.id = id;
        m.name = name;
        m.extensionVersion = version;
        m.kind = kind;
        return m;
    }

    Profile profileOf(const std::string& name, const std::string& game)
    {
        Profile profile;
        profile.mName = name;
        profile.mGame = game;
        return profile;
    }

    Store storeWithTwoGames()
    {
        Store store;
        store.mProfiles = { profileOf("Default", ""), profileOf("Skyrim", "skyrimse") };
        store.mActive = "Default";
        store.mBaseData = { "C:/Games/Morrowind/Data Files" };
        store.mGameBaseData["skyrimse"] = { "D:/Skyrim/Data" };
        return store;
    }

    std::string readAll(const fs::path& file)
    {
        return TempFolder::read(file);
    }

    GamePlugin plugin(
        const std::string& name, bool master = false, bool light = false, std::vector<std::string> masters = {})
    {
        GamePlugin result;
        result.mName = name;
        result.mMaster = master;
        result.mLight = light;
        result.mMasters = std::move(masters);
        return result;
    }

    std::vector<std::string> names(const std::vector<PluginEntry>& plugins)
    {
        std::vector<std::string> result;
        for (const PluginEntry& entry : plugins)
        {
            result.push_back(entry.mName);
        }
        return result;
    }

    std::vector<std::string> names(const std::vector<GamePlugin>& plugins)
    {
        std::vector<std::string> result;
        for (const GamePlugin& entry : plugins)
        {
            result.push_back(entry.mName);
        }
        return result;
    }
}

// ---- listGames -----------------------------------------------------------------------------------------------------

TEST(ModOrganizerGames, with_no_manifests_only_the_built_in_morrowind_entry_is_listed)
{
    const std::vector<GameEntry> games = listGames({});
    ASSERT_EQ(games.size(), 1u);
    EXPECT_EQ(games[0].mId, "morrowind");
    EXPECT_EQ(games[0].mName, "Morrowind");
    EXPECT_TRUE(games[0].mBuiltIn);
    EXPECT_TRUE(games[0].mVersion.empty());
}

TEST(ModOrganizerGames, game_packs_follow_morrowind_sorted_by_id_and_other_kinds_are_left_out)
{
    const std::vector<GameEntry> games = listGames({
        manifest("skyrimse", "Skyrim Special Edition", "0.1.0", "game"),
        manifest("alpha", "Alpha extension", "1.0.0", "feature"),
        manifest("fallout4", "Fallout 4", "0.2.0", "game"),
        manifest("compat_skyrimse", "Skyrim compatibility", "0.3.0", "compatibility"),
    });
    ASSERT_EQ(games.size(), 3u);
    EXPECT_EQ(games[0].mId, "morrowind");
    EXPECT_EQ(games[1].mId, "fallout4");
    EXPECT_EQ(games[1].mName, "Fallout 4");
    EXPECT_EQ(games[1].mVersion, "0.2.0");
    EXPECT_FALSE(games[1].mBuiltIn);
    EXPECT_EQ(games[2].mId, "skyrimse");
    EXPECT_EQ(games[2].mName, "Skyrim Special Edition");
    EXPECT_EQ(games[2].mVersion, "0.1.0");
    EXPECT_FALSE(games[2].mBuiltIn);
}

TEST(ModOrganizerGames, a_second_manifest_with_a_listed_id_and_one_named_morrowind_are_ignored)
{
    const std::vector<GameEntry> games = listGames({
        manifest("skyrimse", "First", "1.0.0", "game"),
        manifest("skyrimse", "Second", "2.0.0", "game"),
        manifest("morrowind", "Another Morrowind", "9.9.9", "game"),
    });
    ASSERT_EQ(games.size(), 2u);
    EXPECT_EQ(games[0].mId, "morrowind");
    EXPECT_EQ(games[0].mName, "Morrowind");
    EXPECT_TRUE(games[0].mBuiltIn);
    EXPECT_EQ(games[1].mId, "skyrimse");
    EXPECT_EQ(games[1].mName, "First");
}

// ---- small helpers -------------------------------------------------------------------------------------------------

TEST(ModOrganizerGames, empty_and_morrowind_both_mean_morrowind)
{
    EXPECT_TRUE(isMorrowind(""));
    EXPECT_TRUE(isMorrowind("morrowind"));
    EXPECT_FALSE(isMorrowind("skyrimse"));
}

TEST(ModOrganizerGames, the_profile_game_id_is_empty_for_morrowind_and_the_id_otherwise)
{
    EXPECT_EQ(profileGameId("morrowind"), "");
    EXPECT_EQ(profileGameId(""), "");
    EXPECT_EQ(profileGameId("skyrimse"), "skyrimse");
}

TEST(ModOrganizerGames, the_expected_header_game_of_a_pack_id)
{
    EXPECT_EQ(expectedHeaderGame("skyrimse"), Content::GameGuess::SkyrimSE);
    EXPECT_EQ(expectedHeaderGame("fallout4"), Content::GameGuess::Fallout4);
    EXPECT_EQ(expectedHeaderGame("falloutnv"), Content::GameGuess::FalloutNV);
    EXPECT_EQ(expectedHeaderGame("morrowind"), Content::GameGuess::Morrowind);
    EXPECT_EQ(expectedHeaderGame("somethingelse"), Content::GameGuess::Unknown);
}

// ---- profiles of a game, selecting a game --------------------------------------------------------------------------

TEST(ModOrganizerGames, the_profiles_of_a_game_are_those_made_for_it_in_the_stores_order)
{
    Store store;
    store.mProfiles = { profileOf("A", ""), profileOf("B", "skyrimse"), profileOf("C", ""), profileOf("D", "skyrimse"),
        profileOf("E", "fallout4") };
    EXPECT_EQ(profilesOfGame(store, "skyrimse"), (std::vector<std::string>{ "B", "D" }));
    EXPECT_EQ(profilesOfGame(store, "morrowind"), (std::vector<std::string>{ "A", "C" }));
    EXPECT_EQ(profilesOfGame(store, ""), (std::vector<std::string>{ "A", "C" }));
    EXPECT_EQ(profilesOfGame(store, "fallout4"), (std::vector<std::string>{ "E" }));
    EXPECT_TRUE(profilesOfGame(store, "falloutnv").empty());
}

TEST(ModOrganizerGames, selecting_a_game_with_no_remembered_profile_activates_its_first_profile)
{
    Store store;
    store.mProfiles = { profileOf("A", ""), profileOf("B", "skyrimse"), profileOf("C", "skyrimse") };
    store.mActive = "A";
    ASSERT_TRUE(selectGame(store, "skyrimse"));
    EXPECT_EQ(store.mActive, "B");
    // The profile that was active is now the last one of its own game (Morrowind's key is the empty id)
    const auto it = store.mLastProfileOfGame.find("");
    ASSERT_NE(it, store.mLastProfileOfGame.end());
    EXPECT_EQ(it->second, "A");
}

TEST(ModOrganizerGames, selecting_a_game_goes_back_to_the_profile_last_active_for_it)
{
    Store store;
    store.mProfiles = { profileOf("A", ""), profileOf("B", "skyrimse"), profileOf("C", "skyrimse") };
    store.mActive = "C";
    ASSERT_TRUE(selectGame(store, "morrowind"));
    EXPECT_EQ(store.mActive, "A");
    EXPECT_EQ(store.mLastProfileOfGame["skyrimse"], "C");
    ASSERT_TRUE(selectGame(store, "skyrimse"));
    EXPECT_EQ(store.mActive, "C");
}

TEST(ModOrganizerGames, a_remembered_profile_that_is_gone_gives_way_to_the_games_first_profile)
{
    Store store;
    store.mProfiles = { profileOf("A", ""), profileOf("B", "skyrimse"), profileOf("C", "skyrimse") };
    store.mActive = "A";
    store.mLastProfileOfGame["skyrimse"] = "Deleted";
    ASSERT_TRUE(selectGame(store, "skyrimse"));
    EXPECT_EQ(store.mActive, "B");
}

TEST(ModOrganizerGames, selecting_a_game_that_has_no_profile_changes_nothing)
{
    Store store;
    store.mProfiles = { profileOf("A", ""), profileOf("B", "skyrimse") };
    store.mActive = "A";
    const std::map<std::string, std::string> before = store.mLastProfileOfGame;
    EXPECT_FALSE(selectGame(store, "fallout4"));
    EXPECT_EQ(store.mActive, "A");
    EXPECT_EQ(store.mLastProfileOfGame, before);
}

// ---- a game's first profile ----------------------------------------------------------------------------------------

TEST(ModOrganizerGames, the_first_profile_lists_masters_then_light_plugins_then_the_rest_masters_first)
{
    const std::vector<GamePlugin> plugins = {
        plugin("Update.esm", true, false, { "Skyrim.esm" }),
        plugin("Skyrim.esm", true),
        plugin("Dawnguard.esm", true, false, { "Skyrim.esm" }),
        plugin("ccTest.esl", false, true, { "Skyrim.esm" }),
        plugin("Zed.esp"),
        plugin("alpha.esp"),
        plugin("Mid.esl", false, true),
    };
    const ImportResult result = buildFirstGameProfile("skyrimse", "D:/Skyrim/Data", plugins, {});

    EXPECT_EQ(result.mProfile.mName, "Default");
    EXPECT_EQ(result.mProfile.mGame, "skyrimse");
    EXPECT_EQ(result.mBaseData, (std::vector<std::string>{ "D:/Skyrim/Data" }));
    EXPECT_TRUE(result.mLibrary.mMods.empty());
    EXPECT_TRUE(result.mProfile.mMods.empty());
    // Groups by name, then the stable masters-first sort moves Skyrim.esm in front of Dawnguard.esm
    EXPECT_EQ(names(result.mProfile.mPlugins),
        (std::vector<std::string>{
            "Skyrim.esm", "Dawnguard.esm", "Update.esm", "ccTest.esl", "Mid.esl", "alpha.esp", "Zed.esp" }));
    for (const PluginEntry& entry : result.mProfile.mPlugins)
    {
        EXPECT_TRUE(entry.mActive) << entry.mName;
    }
}

TEST(ModOrganizerGames, masters_that_form_a_cycle_keep_the_grouped_order)
{
    const std::vector<GamePlugin> plugins = {
        plugin("B.esm", true, false, { "A.esm" }),
        plugin("A.esm", true, false, { "B.esm" }),
    };
    const ImportResult result = buildFirstGameProfile("skyrimse", "D:/Data", plugins, {});
    EXPECT_EQ(names(result.mProfile.mPlugins), (std::vector<std::string>{ "A.esm", "B.esm" }));
}

TEST(ModOrganizerGames, the_first_profile_takes_each_plugins_archives_once_in_the_plugin_order)
{
    const std::vector<GamePlugin> plugins = {
        plugin("Update.esm", true, false, { "Skyrim.esm" }),
        plugin("Skyrim.esm", true),
        plugin("Dawnguard.esm", true, false, { "Skyrim.esm" }),
        plugin("Sky.esp"),
    };
    const std::vector<std::string> archives = { "Skyrim - Textures0.bsa", "Update.bsa", "Other.bsa",
        "Skyrim - Meshes0.bsa", "Dawnguard.bsa", "Skyrim.bsa" };
    const ImportResult result = buildFirstGameProfile("skyrimse", "D:/Data", plugins, archives);
    // Skyrim.esm, Dawnguard.esm, Update.esm, Sky.esp: Sky.esp's prefix matches Skyrim's archives again, which count
    // once
    EXPECT_EQ(result.mProfile.mArchives,
        (std::vector<std::string>{
            "Skyrim - Meshes0.bsa", "Skyrim - Textures0.bsa", "Skyrim.bsa", "Dawnguard.bsa", "Update.bsa" }));
}

TEST(ModOrganizerGames, the_first_profile_has_the_given_name_and_morrowind_has_no_game_id)
{
    const ImportResult result
        = buildFirstGameProfile("morrowind", "C:/Morrowind/Data Files", { plugin("Morrowind.esm", true) }, {}, "Mine");
    EXPECT_EQ(result.mProfile.mName, "Mine");
    EXPECT_EQ(result.mProfile.mGame, "");
    EXPECT_EQ(result.mBaseData, (std::vector<std::string>{ "C:/Morrowind/Data Files" }));
}

// ---- scanning a Data folder ----------------------------------------------------------------------------------------

TEST(ModOrganizerGames, scanning_keeps_the_plugins_of_the_expected_game_and_reads_their_header_facts)
{
    using namespace ContentTests;
    TempFolder temp;
    temp.write("Data/Skyrim.esm", tes4Header(1.7f, 1, "a", "d", 0, {}).str());
    temp.write("Data/Update.esm", tes4Header(1.7f, 1, "a", "d", 0, { { "Skyrim.esm", 0 } }).str());
    temp.write("Data/Light.esl", tes4Header(1.7f, 0x200, "a", "d", 0, {}).str());
    temp.write("Data/Lite.esl", tes4Header(1.7f, 0, "a", "d", 0, {}).str());
    temp.write("Data/Fake.esm", tes4Header(1.7f, 0, "a", "d", 0, {}).str());
    temp.write("Data/Alpha.esp", tes4Header(1.7f, 0, "a", "d", 0, {}).str());
    temp.write("Data/Zed.ESP", tes4Header(1.7f, 0, "a", "d", 0, {}).str());
    temp.write("Data/Morrowind.esm", tes3Header(1.3f, 1, "a", "d", 0, {}).str());
    temp.write("Data/Beta.esp", "this is not a plugin");
    temp.write("Data/notes.txt", "x");
    temp.write("Data/sub/Inner.esp", tes4Header(1.7f, 0, "a", "d", 0, {}).str());

    std::vector<std::string> problems;
    const std::vector<GamePlugin> found = scanGamePlugins(temp.path() / "Data", Content::GameGuess::SkyrimSE, problems);

    EXPECT_EQ(names(found),
        (std::vector<std::string>{
            "Alpha.esp", "Fake.esm", "Light.esl", "Lite.esl", "Skyrim.esm", "Update.esm", "Zed.ESP" }));
    const auto byName = [&](const std::string& name) -> const GamePlugin* {
        for (const GamePlugin& entry : found)
        {
            if (entry.mName == name)
                return &entry;
        }
        return nullptr;
    };
    ASSERT_NE(byName("Skyrim.esm"), nullptr);
    EXPECT_TRUE(byName("Skyrim.esm")->mMaster);
    EXPECT_FALSE(byName("Skyrim.esm")->mLight);
    EXPECT_TRUE(byName("Skyrim.esm")->mMasters.empty());
    ASSERT_NE(byName("Update.esm"), nullptr);
    EXPECT_EQ(byName("Update.esm")->mMasters, (std::vector<std::string>{ "Skyrim.esm" }));
    ASSERT_NE(byName("Light.esl"), nullptr);
    EXPECT_TRUE(byName("Light.esl")->mLight);
    EXPECT_FALSE(byName("Light.esl")->mMaster);
    ASSERT_NE(byName("Lite.esl"), nullptr);
    EXPECT_TRUE(byName("Lite.esl")->mLight); // the .esl name counts without the flag
    ASSERT_NE(byName("Fake.esm"), nullptr);
    EXPECT_TRUE(byName("Fake.esm")->mMaster); // the .esm name counts without the flag
    ASSERT_NE(byName("Alpha.esp"), nullptr);
    EXPECT_FALSE(byName("Alpha.esp")->mMaster);
    EXPECT_FALSE(byName("Alpha.esp")->mLight);

    // The unreadable file is named
    const bool namedBeta = std::any_of(problems.begin(), problems.end(),
        [](const std::string& problem) { return problem.find("Beta.esp") != std::string::npos; });
    EXPECT_TRUE(namedBeta);
}

TEST(ModOrganizerGames, scanning_for_morrowind_keeps_only_the_tes3_plugin)
{
    using namespace ContentTests;
    TempFolder temp;
    temp.write("Data/Skyrim.esm", tes4Header(1.7f, 1, "a", "d", 0, {}).str());
    temp.write("Data/Morrowind.esm", tes3Header(1.3f, 1, "a", "d", 0, {}).str());
    std::vector<std::string> problems;
    const std::vector<GamePlugin> found
        = scanGamePlugins(temp.path() / "Data", Content::GameGuess::Morrowind, problems);
    EXPECT_EQ(names(found), (std::vector<std::string>{ "Morrowind.esm" }));
}

TEST(ModOrganizerGames, scanning_a_missing_folder_gives_nothing)
{
    TempFolder temp;
    std::vector<std::string> problems;
    EXPECT_TRUE(scanGamePlugins(temp.path() / "nowhere", Content::GameGuess::SkyrimSE, problems).empty());
}

TEST(ModOrganizerGames, listing_archives_finds_bsa_and_ba2_files_directly_in_the_folder_by_name)
{
    TempFolder temp;
    temp.write("Data/Zed.bsa");
    temp.write("Data/Beta.BA2");
    temp.write("Data/Alpha.bsa");
    temp.write("Data/readme.txt");
    temp.write("Data/Alpha.bsa.bak");
    temp.write("Data/sub/Inner.bsa");
    EXPECT_EQ(listArchives(temp.path() / "Data"), (std::vector<std::string>{ "Alpha.bsa", "Beta.BA2", "Zed.bsa" }));
    EXPECT_TRUE(listArchives(temp.path() / "nowhere").empty());
}

// ---- the options handed to the programs the launcher starts --------------------------------------------------------

TEST(ModOrganizerGames, only_the_config_options_are_forwarded_in_their_order)
{
    const std::vector<std::string> arguments
        = { "--config", "C:/cfg", "--verbose", "--replace", "config", "--user-data", "C:/ud", "--resources", "C:/res" };
    EXPECT_EQ(forwardedConfigArguments(arguments),
        (std::vector<std::string>{
            "--config", "C:/cfg", "--replace", "config", "--user-data", "C:/ud", "--resources", "C:/res" }));
}

TEST(ModOrganizerGames, the_equals_form_is_forwarded_too)
{
    const std::vector<std::string> result
        = forwardedConfigArguments({ "--verbose", "--user-data=C:/ud", "--other", "--config=C:/cfg" });
    std::string joined;
    for (const std::string& argument : result)
    {
        joined += argument + " ";
    }
    EXPECT_NE(joined.find("--user-data"), std::string::npos) << joined;
    EXPECT_NE(joined.find("C:/ud"), std::string::npos) << joined;
    EXPECT_NE(joined.find("--config"), std::string::npos) << joined;
    EXPECT_NE(joined.find("C:/cfg"), std::string::npos) << joined;
    EXPECT_EQ(joined.find("--verbose"), std::string::npos) << joined;
    EXPECT_EQ(joined.find("--other"), std::string::npos) << joined;
    EXPECT_LT(joined.find("--user-data"), joined.find("--config")) << joined;
}

TEST(ModOrganizerGames, no_arguments_forward_nothing)
{
    EXPECT_TRUE(forwardedConfigArguments({}).empty());
}

// ---- the profile file and the store files --------------------------------------------------------------------------

TEST(ModOrganizerGames, a_profiles_game_is_written_only_when_not_empty_and_read_back)
{
    Profile skyrim = profileOf("Skyrim", "skyrimse");
    const std::string text = writeProfile(skyrim);
    EXPECT_NE(text.find("\"game\""), std::string::npos) << text;
    LoadError error;
    const std::optional<Profile> back = readProfile(text, "Skyrim.json", error);
    ASSERT_TRUE(back.has_value()) << error.format();
    EXPECT_EQ(back->mGame, "skyrimse");
    EXPECT_EQ(*back, skyrim);
    EXPECT_EQ(writeProfile(*back), text);

    const std::string morrowind = writeProfile(profileOf("Default", ""));
    EXPECT_EQ(morrowind.find("game"), std::string::npos) << morrowind;
    const std::optional<Profile> morrowindBack = readProfile(morrowind, "Default.json", error);
    ASSERT_TRUE(morrowindBack.has_value()) << error.format();
    EXPECT_EQ(morrowindBack->mGame, "");
}

TEST(ModOrganizerGames, the_store_files_of_a_game_have_their_own_names)
{
    const StorePaths paths = storePaths("/cfg");
    EXPECT_EQ(paths.gameBaseDataFile("skyrimse"), paths.mFolder / "base-data-skyrimse.txt");
    EXPECT_EQ(paths.lastProfilesFile(), paths.mFolder / "last-profiles.txt");
}

TEST(ModOrganizerGames, a_store_with_two_games_round_trips_through_the_files)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    Store store = storeWithTwoGames();
    store.mActive = "Skyrim";
    store.mLastProfileOfGame["skyrimse"] = "Skyrim";
    ASSERT_TRUE(saveStore(paths, store).empty());

    EXPECT_TRUE(fs::exists(paths.gameBaseDataFile("skyrimse")));
    EXPECT_TRUE(fs::exists(paths.lastProfilesFile()));

    const Store back = loadStore(paths);
    EXPECT_TRUE(back.mProblems.empty());
    EXPECT_EQ(back.mProfiles, store.mProfiles);
    EXPECT_EQ(back.mActive, "Skyrim");
    EXPECT_EQ(back.mBaseData, store.mBaseData);
    ASSERT_EQ(back.mGameBaseData.count("skyrimse"), 1u);
    EXPECT_EQ(back.mGameBaseData.at("skyrimse"), (std::vector<std::string>{ "D:/Skyrim/Data" }));
    ASSERT_EQ(back.mLastProfileOfGame.count("skyrimse"), 1u);
    EXPECT_EQ(back.mLastProfileOfGame.at("skyrimse"), "Skyrim");
}

TEST(ModOrganizerGames, a_morrowind_only_store_writes_no_game_files)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    Store store;
    store.mProfiles = { profileOf("Default", "") };
    store.mActive = "Default";
    store.mBaseData = { "C:/Games/Morrowind/Data Files" };
    store.mLastProfileOfGame[""] = "Default";
    store.mGameBaseData["skyrimse"] = {}; // a game with no base data gets no file
    ASSERT_TRUE(saveStore(paths, store).empty());

    EXPECT_FALSE(fs::exists(paths.lastProfilesFile()));
    EXPECT_FALSE(fs::exists(paths.gameBaseDataFile("skyrimse")));
    for (const fs::directory_entry& entry : fs::directory_iterator(paths.mFolder))
    {
        EXPECT_NE(entry.path().filename().string().rfind("base-data-", 0), 0u) << entry.path();
    }
    EXPECT_TRUE(fs::exists(paths.baseDataFile()));
}

// ---- the lines of a profile ----------------------------------------------------------------------------------------

TEST(ModOrganizerGames, the_game_line_comes_first_and_only_when_the_layout_has_a_game)
{
    Layout layout;
    layout.mData.push_back({ "D:/Skyrim/Data", "", false, false });
    layout.mContent = { "Skyrim.esm" };
    const std::vector<std::string> without = renderConfigLines(layout);
    for (const std::string& line : without)
    {
        EXPECT_NE(line.rfind("game", 0), 0u) << line;
    }

    layout.mGame = "skyrimse";
    const std::vector<std::string> with = renderConfigLines(layout);
    ASSERT_EQ(with.size(), without.size() + 1);
    EXPECT_EQ(with[0], "game=skyrimse");
    EXPECT_EQ(std::vector<std::string>(with.begin() + 1, with.end()), without);
}

TEST(ModOrganizerGames, the_layout_carries_the_profiles_game)
{
    ModLibrary library;
    EXPECT_EQ(buildLayout(library, profileOf("Skyrim", "skyrimse"), LayoutOptions()).mGame, "skyrimse");
    EXPECT_EQ(buildLayout(library, profileOf("Default", ""), LayoutOptions()).mGame, "");
}

TEST(ModOrganizerGames, a_game_line_is_an_owned_line)
{
    EXPECT_TRUE(isOwnedLine("game=skyrimse"));
    EXPECT_TRUE(isOwnedLine("  game = skyrimse"));
    EXPECT_FALSE(isOwnedLine("gamefoo=1"));
    EXPECT_FALSE(isOwnedLine("# game=skyrimse"));
}

TEST(ModOrganizerGames, rewriting_replaces_or_removes_an_old_game_line)
{
    const RewriteResult replaced = rewriteOwnedLines(
        "encoding=win1252\ngame=fallout4\ncontent=Old.esm\n", { "game=skyrimse", "content=New.esm" });
    EXPECT_NE(replaced.mText.find("game=skyrimse"), std::string::npos);
    EXPECT_EQ(replaced.mText.find("fallout4"), std::string::npos);
    EXPECT_EQ(replaced.mText.find("Old.esm"), std::string::npos);
    EXPECT_NE(replaced.mText.find("encoding=win1252"), std::string::npos);

    const RewriteResult removed = rewriteOwnedLines("game=skyrimse\ncontent=Old.esm\n", { "content=New.esm" });
    EXPECT_EQ(removed.mText.find("game"), std::string::npos);
    EXPECT_NE(removed.mText.find("content=New.esm"), std::string::npos);
}

TEST(ModOrganizerGames, the_layout_options_use_the_active_profiles_games_base_data)
{
    Store store = storeWithTwoGames();
    store.mActive = "Default";
    EXPECT_EQ(layoutOptions(store).mBaseData, (std::vector<std::string>{ "C:/Games/Morrowind/Data Files" }));
    store.mActive = "Skyrim";
    EXPECT_EQ(layoutOptions(store).mBaseData, (std::vector<std::string>{ "D:/Skyrim/Data" }));

    const Layout layout = activeLayout(store);
    EXPECT_EQ(layout.mGame, "skyrimse");
    ASSERT_FALSE(layout.mData.empty());
    EXPECT_EQ(layout.mData[0].mPath, "D:/Skyrim/Data");
}

TEST(ModOrganizerGames, a_non_morrowind_game_without_base_data_does_not_borrow_morrowinds)
{
    Store store = storeWithTwoGames();
    store.mGameBaseData.clear();
    store.mActive = "Skyrim";
    EXPECT_TRUE(layoutOptions(store).mBaseData.empty());
}

TEST(ModOrganizerGames, applying_a_skyrim_profile_writes_the_game_line_and_a_morrowind_profile_removes_it)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const fs::path cfg = temp.write("cfg/openmw.cfg", "encoding=win1252\n");
    Store store = storeWithTwoGames();
    store.mProfiles[1].mPlugins = { { "Skyrim.esm", true } };
    store.mProfiles[0].mPlugins = { { "Morrowind.esm", true } };

    ApplyOptions options;
    options.mGameRunning = [] { return false; };

    store.mActive = "Skyrim";
    const ApplyOutcome sky = applyActiveProfile(paths, store, cfg, options, true);
    ASSERT_TRUE(sky.mOk) << sky.mError;
    ASSERT_FALSE(sky.mLines.empty());
    EXPECT_EQ(sky.mLines[0], "game=skyrimse");
    std::string text = readAll(cfg);
    EXPECT_NE(text.find("game=skyrimse"), std::string::npos) << text;
    EXPECT_NE(text.find("D:/Skyrim/Data"), std::string::npos) << text;
    EXPECT_NE(text.find("content=Skyrim.esm"), std::string::npos) << text;
    EXPECT_EQ(text.find("C:/Games/Morrowind"), std::string::npos) << text;
    EXPECT_NE(text.find("encoding=win1252"), std::string::npos) << text;

    store.mActive = "Default";
    const ApplyOutcome morrowind = applyActiveProfile(paths, store, cfg, options, true);
    ASSERT_TRUE(morrowind.mOk) << morrowind.mError;
    text = readAll(cfg);
    EXPECT_EQ(text.find("game"), std::string::npos) << text;
    EXPECT_NE(text.find("content=Morrowind.esm"), std::string::npos) << text;
    EXPECT_EQ(text.find("Skyrim"), std::string::npos) << text;
    EXPECT_NE(text.find("encoding=win1252"), std::string::npos) << text;
}

TEST(ModOrganizerGames, a_morrowind_profile_renders_the_same_lines_as_before_with_no_game_line)
{
    Store store;
    store.mProfiles = { profileOf("Default", "") };
    store.mProfiles[0].mPlugins = { { "Morrowind.esm", true } };
    store.mActive = "Default";
    store.mBaseData = { "C:/Games/Morrowind/Data Files" };
    const std::vector<std::string> lines = renderConfigLines(activeLayout(store));
    ASSERT_FALSE(lines.empty());
    for (const std::string& line : lines)
    {
        EXPECT_NE(line.rfind("game", 0), 0u) << line;
    }
}
