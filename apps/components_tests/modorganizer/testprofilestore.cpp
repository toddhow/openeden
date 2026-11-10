// Task 0057: the launcher's profile adapter (components/modorganizer/profilestore). Every mod and every file is
// generated in a temporary folder; no game or mod content is used.

#include <components/modorganizer/profilestore.hpp>

#include "tempfolder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace ModOrganizer;
using ModOrganizerTest::TempFolder;

namespace
{
    std::string readAll(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    Store storeWithDefault(const std::string& base = "C:/Games/Morrowind/Data Files")
    {
        ConfigSnapshot snapshot;
        snapshot.mData = { base };
        snapshot.mContent = { "Morrowind.esm" };
        Store store;
        importIfEmpty(store, snapshot);
        return store;
    }
}

// ---- paths ---------------------------------------------------------------------------------------------------------

TEST(ModOrganizerStore, files_live_below_openeden_in_the_user_config_folder)
{
    const StorePaths paths = storePaths("/cfg");
    EXPECT_EQ(paths.mFolder, std::filesystem::path("/cfg") / "openeden");
    EXPECT_EQ(paths.modsFile().filename(), "mods.json");
    EXPECT_EQ(paths.profileFile("Default"), paths.profilesFolder() / "Default.json");
    EXPECT_EQ(paths.activeFile().filename(), "active-profile.txt");
    EXPECT_EQ(paths.baseDataFile().filename(), "base-data.txt");
    EXPECT_EQ(paths.appliedFile("Default"), paths.mFolder / "applied" / "Default.txt");
}

// ---- first run import ----------------------------------------------------------------------------------------------

TEST(ModOrganizerStore, import_makes_the_first_data_folder_the_locked_base_and_the_rest_mods)
{
    ConfigSnapshot snapshot;
    snapshot.mData = { "C:/Games/Morrowind/Data Files", "D:/Mods/Alpha", "D:/Mods/Beta.edenpak" };
    snapshot.mContent = { "Morrowind.esm", "Alpha.esp" };
    snapshot.mArchives = { "Morrowind.bsa" };
    snapshot.mGroundcover = { "grass.esp" };
    snapshot.mExtensions = { "compat_morrowind" };
    const ImportResult result = importFromConfig(snapshot, "Default");

    ASSERT_EQ(result.mBaseData.size(), 1u);
    EXPECT_EQ(result.mBaseData[0], "C:/Games/Morrowind/Data Files");
    ASSERT_EQ(result.mLibrary.mMods.size(), 2u);
    EXPECT_EQ(result.mLibrary.mMods[0].mName, "Alpha");
    EXPECT_EQ(result.mLibrary.mMods[0].mKind, ModKind::Folder);
    EXPECT_EQ(result.mLibrary.mMods[0].mRoot, "D:/Mods/Alpha");
    EXPECT_EQ(result.mLibrary.mMods[1].mKind, ModKind::Pack);
    EXPECT_TRUE(isValidModId(result.mLibrary.mMods[0].mId));
    ASSERT_EQ(result.mProfile.mMods.size(), 2u);
    EXPECT_EQ(result.mProfile.mMods[0].mId, result.mLibrary.mMods[0].mId);
    EXPECT_TRUE(result.mProfile.mMods[0].mEnabled);
    ASSERT_EQ(result.mProfile.mPlugins.size(), 2u);
    EXPECT_EQ(result.mProfile.mPlugins[1].mName, "Alpha.esp");
    EXPECT_TRUE(result.mProfile.mPlugins[1].mActive);
    EXPECT_EQ(result.mProfile.mArchives, snapshot.mArchives);
    EXPECT_EQ(result.mProfile.mGroundcover, snapshot.mGroundcover);
    EXPECT_EQ(result.mProfile.mExtensions, snapshot.mExtensions);
}

TEST(ModOrganizerStore, import_counts_an_equal_path_once_and_gives_unique_ids)
{
    ConfigSnapshot snapshot;
    snapshot.mData = { "C:/Base", "D:/Mods/Same", "D:/Mods/Same", "E:/More/Same" };
    const ImportResult result = importFromConfig(snapshot, "Default");
    ASSERT_EQ(result.mLibrary.mMods.size(), 2u);
    EXPECT_NE(result.mLibrary.mMods[0].mId, result.mLibrary.mMods[1].mId);
}

TEST(ModOrganizerStore, import_without_data_lines_has_no_base_and_no_mods)
{
    const ImportResult result = importFromConfig(ConfigSnapshot(), "Default");
    EXPECT_TRUE(result.mBaseData.empty());
    EXPECT_TRUE(result.mLibrary.mMods.empty());
}

TEST(ModOrganizerStore, import_if_empty_only_runs_once)
{
    Store store = storeWithDefault();
    ASSERT_EQ(store.mProfiles.size(), 1u);
    EXPECT_EQ(store.mActive, "Default");
    ConfigSnapshot other;
    other.mData = { "X:/Other", "X:/Mod" };
    EXPECT_FALSE(importIfEmpty(store, other));
    EXPECT_EQ(store.mProfiles.size(), 1u);
    EXPECT_EQ(store.mBaseData[0], "C:/Games/Morrowind/Data Files");
}

TEST(ModOrganizerStore, without_a_base_every_data_line_is_a_mod)
{
    ConfigSnapshot snapshot;
    snapshot.mData = { "D:/Mods/One", "D:/Mods/Two" };
    snapshot.mFirstDataIsBase = false;
    const ImportResult result = importFromConfig(snapshot, "Default");
    EXPECT_TRUE(result.mBaseData.empty());
    EXPECT_EQ(result.mLibrary.mMods.size(), 2u);
}

TEST(ModOrganizerStore, a_config_text_is_read_for_its_owned_keys_with_the_quoting_undone)
{
    const std::string text
        = "# comment\n"
          "encoding=win1252\n"
          "data=\"C:/Games/Morrowind/Data Files\"\n"
          "data = \"D:/Mods/A &&B\"\n"
          "data=D:/Mods/plain\n"
          "data-local=\"C:/local\"\n"
          "fallback-archive=Morrowind.bsa\n"
          "content=Morrowind.esm\r\n"
          "content=A.esp\n"
          "groundcover=grass.esp\n"
          "extension=compat_morrowind\n"
          "# data=\"C:/commented\"\n";
    const ConfigSnapshot snapshot = snapshotFromConfigText(text);
    ASSERT_EQ(snapshot.mData.size(), 3u);
    EXPECT_EQ(snapshot.mData[0], "C:/Games/Morrowind/Data Files");
    EXPECT_EQ(snapshot.mData[1], "D:/Mods/A &B");
    EXPECT_EQ(snapshot.mData[2], "D:/Mods/plain");
    EXPECT_EQ(snapshot.mContent, (std::vector<std::string>{ "Morrowind.esm", "A.esp" }));
    EXPECT_EQ(snapshot.mArchives, std::vector<std::string>{ "Morrowind.bsa" });
    EXPECT_EQ(snapshot.mGroundcover, std::vector<std::string>{ "grass.esp" });
    EXPECT_EQ(snapshot.mExtensions, std::vector<std::string>{ "compat_morrowind" });
    EXPECT_TRUE(snapshot.mFirstDataIsBase);
}

TEST(ModOrganizerStore, config_values_follow_the_engine_quoting)
{
    EXPECT_EQ(parseConfigValue("\"a&\"b\""), "a\"b");
    EXPECT_EQ(parseConfigValue("  plain text  "), "plain text");
    EXPECT_EQ(parseConfigValue("\"unterminated"), "unterminated");
    EXPECT_EQ(parseConfigValue(""), "");
    EXPECT_EQ(parseConfigValue("   "), "");
}

TEST(ModOrganizerStore, a_rendered_layout_reads_back_to_the_same_lines)
{
    Store store = storeWithDefault("C:/Games/Mor&rowind/Data \"Files\"");
    const std::vector<std::string> lines = renderConfigLines(activeLayout(store));
    std::string text;
    for (const std::string& line : lines)
        text += line + "\n";
    const ConfigSnapshot snapshot = snapshotFromConfigText(text);
    ASSERT_EQ(snapshot.mData.size(), 1u);
    EXPECT_EQ(snapshot.mData[0], "C:/Games/Mor&rowind/Data \"Files\"");
}

// ---- load and save ------------------------------------------------------------------------------------------------

TEST(ModOrganizerStore, a_missing_folder_loads_as_an_empty_store)
{
    TempFolder temp;
    const Store store = loadStore(storePaths(temp.path()));
    EXPECT_TRUE(store.mProfiles.empty());
    EXPECT_TRUE(store.mLibrary.mMods.empty());
    EXPECT_TRUE(store.mProblems.empty());
    EXPECT_TRUE(store.mActive.empty());
}

TEST(ModOrganizerStore, save_then_load_gives_the_same_store)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path());
    Store store = storeWithDefault();
    ASSERT_TRUE(createProfile(store, "Second", "Default"));
    store.mActive = "Second";
    EXPECT_TRUE(saveStore(paths, store).empty());

    const Store loaded = loadStore(paths);
    EXPECT_TRUE(loaded.mProblems.empty());
    EXPECT_EQ(loaded.mActive, "Second");
    EXPECT_EQ(loaded.mBaseData, store.mBaseData);
    ASSERT_EQ(loaded.mProfiles.size(), 2u);
    EXPECT_EQ(loaded.mProfiles[0].mName, "Default");
    EXPECT_EQ(loaded.mProfiles[1].mName, "Second");
    EXPECT_EQ(loaded.mProfiles[0], store.mProfiles[0]);
    EXPECT_EQ(loaded.mLibrary, store.mLibrary);
}

TEST(ModOrganizerStore, saving_an_unchanged_store_writes_nothing)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path());
    Store store = storeWithDefault();
    ASSERT_TRUE(saveStore(paths, store).empty());
    const auto before = std::filesystem::last_write_time(paths.profileFile("Default"));
    // A second save compares the canonical text and leaves the file alone
    const std::string text = readAll(paths.profileFile("Default"));
    ASSERT_TRUE(saveStore(paths, loadStore(paths)).empty());
    EXPECT_EQ(std::filesystem::last_write_time(paths.profileFile("Default")), before);
    EXPECT_EQ(readAll(paths.profileFile("Default")), text);
}

TEST(ModOrganizerStore, a_broken_profile_file_is_a_located_problem_and_does_not_stop_the_others)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path());
    Store store = storeWithDefault();
    ASSERT_TRUE(saveStore(paths, store).empty());
    temp.write("openeden/profiles/Broken.json", "{ this is not json");
    const Store loaded = loadStore(paths);
    ASSERT_EQ(loaded.mProblems.size(), 1u);
    EXPECT_NE(loaded.mProblems[0].find("Broken.json"), std::string::npos);
    EXPECT_EQ(loaded.mProfiles.size(), 1u);
}

TEST(ModOrganizerStore, a_newer_profile_file_is_read_only_and_never_overwritten)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path());
    Store store = storeWithDefault();
    ASSERT_TRUE(saveStore(paths, store).empty());
    const std::string future = "{\"format\": \"openeden.profile\", \"version\": 99, \"name\": \"Default\"}\n";
    temp.write("openeden/profiles/Default.json", future);

    Store loaded = loadStore(paths);
    EXPECT_TRUE(loaded.isReadOnly("Default"));
    loaded.mProfiles.clear();
    ASSERT_TRUE(loaded.mProfiles.empty());
    Store edit = storeWithDefault();
    edit.mReadOnlyProfiles = { "Default" };
    edit.mProfiles[0].mArchives.push_back("x.bsa");
    EXPECT_TRUE(saveStore(paths, edit).empty());
    EXPECT_EQ(readAll(paths.profileFile("Default")), future);
}

TEST(ModOrganizerStore, a_newer_mods_file_is_read_only)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path());
    const std::string future = "{\"format\": \"openeden.mods\", \"version\": 99, \"mods\": []}\n";
    temp.write("openeden/mods.json", future);
    Store loaded = loadStore(paths);
    EXPECT_TRUE(loaded.mLibraryReadOnly);
    Store edit;
    edit.mLibraryReadOnly = true;
    EXPECT_TRUE(saveStore(paths, edit).empty());
    EXPECT_EQ(readAll(paths.modsFile()), future);
}

TEST(ModOrganizerStore, an_active_name_that_does_not_exist_falls_back_to_the_first_profile)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path());
    Store store = storeWithDefault();
    ASSERT_TRUE(saveStore(paths, store).empty());
    temp.write("openeden/active-profile.txt", "Gone\n");
    EXPECT_EQ(loadStore(paths).mActive, "Default");
}

// ---- adding and removing mods ---------------------------------------------------------------------------------------

TEST(ModOrganizerStore, adding_a_folder_registers_it_in_place_and_lists_its_plugins)
{
    TempFolder temp;
    temp.write("mods/Alpha/Alpha.esp");
    temp.write("mods/Alpha/meshes/a.nif");
    Store store = storeWithDefault();

    const EditResult result = addFolderMod(store, temp.path() / "mods" / "Alpha");
    ASSERT_TRUE(result.mOk) << result.mError;
    ASSERT_EQ(store.mLibrary.mMods.size(), 1u);
    EXPECT_EQ(store.mLibrary.mMods[0].mId, result.mId);
    EXPECT_EQ(store.mLibrary.mMods[0].mName, "Alpha");
    EXPECT_EQ(store.mLibrary.mMods[0].mDataPath, "");
    EXPECT_EQ(store.mLibrary.mMods[0].mKind, ModKind::Folder);
    const Profile& profile = *store.active();
    ASSERT_EQ(profile.mMods.size(), 1u);
    EXPECT_TRUE(profile.mMods[0].mEnabled);
    // Morrowind.esm from the import, then the new plugin, inactive until the player switches it on
    ASSERT_EQ(profile.mPlugins.size(), 2u);
    EXPECT_EQ(profile.mPlugins[1].mName, "Alpha.esp");
    EXPECT_FALSE(profile.mPlugins[1].mActive);
    // Nothing was written into the mod
    EXPECT_TRUE(std::filesystem::exists(temp.path() / "mods/Alpha/Alpha.esp"));
}

TEST(ModOrganizerStore, a_mod_that_wraps_its_data_in_a_folder_gets_a_data_path)
{
    TempFolder temp;
    temp.write("mods/Wrapped/readme.txt");
    temp.write("mods/Wrapped/Data Files/Wrapped.esp");
    Store store = storeWithDefault();
    const EditResult result = addFolderMod(store, temp.path() / "mods" / "Wrapped");
    ASSERT_TRUE(result.mOk) << result.mError;
    EXPECT_EQ(store.mLibrary.mMods[0].mDataPath, "Data Files");
    EXPECT_EQ(store.active()->mPlugins.back().mName, "Wrapped.esp");
}

TEST(ModOrganizerStore, adding_the_same_folder_twice_is_refused)
{
    TempFolder temp;
    temp.write("mods/Alpha/Alpha.esp");
    Store store = storeWithDefault();
    ASSERT_TRUE(addFolderMod(store, temp.path() / "mods" / "Alpha").mOk);
    const EditResult second = addFolderMod(store, temp.path() / "mods" / "Alpha");
    EXPECT_FALSE(second.mOk);
    EXPECT_FALSE(second.mError.empty());
    EXPECT_EQ(store.mLibrary.mMods.size(), 1u);
}

TEST(ModOrganizerStore, two_mods_with_the_same_name_get_different_ids)
{
    TempFolder temp;
    temp.write("a/Same/x.txt");
    temp.write("b/Same/y.txt");
    Store store = storeWithDefault();
    const EditResult first = addFolderMod(store, temp.path() / "a" / "Same");
    const EditResult second = addFolderMod(store, temp.path() / "b" / "Same");
    ASSERT_TRUE(first.mOk);
    ASSERT_TRUE(second.mOk);
    EXPECT_NE(first.mId, second.mId);
}

TEST(ModOrganizerStore, a_missing_folder_or_a_file_is_not_a_mod_folder)
{
    TempFolder temp;
    temp.write("file.txt");
    Store store = storeWithDefault();
    EXPECT_FALSE(addFolderMod(store, temp.path() / "nope").mOk);
    EXPECT_FALSE(addFolderMod(store, temp.path() / "file.txt").mOk);
    EXPECT_TRUE(store.mLibrary.mMods.empty());
}

TEST(ModOrganizerStore, adding_a_pack_checks_the_magic_bytes)
{
    TempFolder temp;
    temp.write("packs/good.edenpak", "EDENPAK01 and then some bytes");
    temp.write("packs/bad.edenpak", "not a pack at all");
    Store store = storeWithDefault();
    const EditResult good = addPackMod(store, temp.path() / "packs" / "good.edenpak");
    ASSERT_TRUE(good.mOk) << good.mError;
    EXPECT_EQ(store.mLibrary.mMods[0].mKind, ModKind::Pack);
    EXPECT_EQ(store.mLibrary.mMods[0].mName, "good");
    EXPECT_FALSE(addPackMod(store, temp.path() / "packs" / "bad.edenpak").mOk);
    EXPECT_EQ(store.mLibrary.mMods.size(), 1u);
}

TEST(ModOrganizerStore, removing_a_mod_leaves_the_files_and_drops_it_from_every_profile)
{
    TempFolder temp;
    temp.write("mods/Alpha/Alpha.esp");
    Store store = storeWithDefault();
    const EditResult added = addFolderMod(store, temp.path() / "mods" / "Alpha");
    ASSERT_TRUE(added.mOk);
    ASSERT_TRUE(createProfile(store, "Other", "Default"));
    EXPECT_TRUE(removeMod(store, added.mId));
    EXPECT_TRUE(store.mLibrary.mMods.empty());
    for (const Profile& profile : store.mProfiles)
        EXPECT_TRUE(profile.mMods.empty());
    EXPECT_TRUE(std::filesystem::exists(temp.path() / "mods/Alpha/Alpha.esp"));
    EXPECT_FALSE(removeMod(store, added.mId));
}

// ---- reordering ---------------------------------------------------------------------------------------------------

TEST(ModOrganizerStore, moving_a_mod_changes_the_priority_order_and_refuses_out_of_range)
{
    Profile profile;
    profile.mMods = { { "a", true, false }, { "b", true, false }, { "c", true, false } };
    EXPECT_TRUE(moveMod(profile, 0, 2));
    EXPECT_EQ(profile.mMods[0].mId, "b");
    EXPECT_EQ(profile.mMods[2].mId, "a");
    EXPECT_TRUE(moveMod(profile, 2, 1));
    EXPECT_EQ(profile.mMods[1].mId, "a");
    EXPECT_FALSE(moveMod(profile, 0, 3));
    EXPECT_FALSE(moveMod(profile, 3, 0));
    EXPECT_TRUE(moveMod(profile, 1, 1)); // no change is fine
    EXPECT_EQ(profile.mMods[1].mId, "a");
}

TEST(ModOrganizerStore, moving_a_plugin_changes_the_load_order)
{
    Profile profile;
    profile.mPlugins = { { "a.esp", true }, { "b.esp", true }, { "c.esp", false } };
    EXPECT_TRUE(movePlugin(profile, 2, 0));
    EXPECT_EQ(profile.mPlugins[0].mName, "c.esp");
    EXPECT_FALSE(profile.mPlugins[0].mActive);
    EXPECT_FALSE(movePlugin(profile, 5, 0));
}

TEST(ModOrganizerStore, enable_flags_change_by_id_or_name_and_unknown_ones_are_refused)
{
    Profile profile;
    profile.mMods = { { "a", true, false } };
    profile.mPlugins = { { "A.esp", true } };
    EXPECT_TRUE(setModEnabled(profile, "a", false));
    EXPECT_FALSE(profile.mMods[0].mEnabled);
    EXPECT_FALSE(setModEnabled(profile, "zzz", true));
    EXPECT_TRUE(setModUseRepack(profile, "a", true));
    EXPECT_TRUE(profile.mMods[0].mUseRepack);
    EXPECT_TRUE(setPluginActive(profile, "a.esp", false)); // names compare ignoring ASCII case
    EXPECT_FALSE(profile.mPlugins[0].mActive);
    EXPECT_FALSE(setPluginActive(profile, "nope.esp", true));
}

// ---- plugin inventory ----------------------------------------------------------------------------------------------

TEST(ModOrganizerStore, the_inventory_lists_plugins_of_enabled_mods_and_the_later_mod_wins_a_name)
{
    TempFolder temp;
    temp.write("m/One/Shared.esp");
    temp.write("m/One/OnlyOne.esp");
    temp.write("m/Two/shared.esp");
    temp.write("m/Off/Hidden.esp");
    Store store = storeWithDefault();
    const auto one = addFolderMod(store, temp.path() / "m" / "One");
    const auto two = addFolderMod(store, temp.path() / "m" / "Two");
    const auto off = addFolderMod(store, temp.path() / "m" / "Off");
    ASSERT_TRUE(one.mOk && two.mOk && off.mOk);
    ASSERT_TRUE(setModEnabled(*store.active(), off.mId, false));

    const PluginInventory inventory = inventoryPlugins(store.mLibrary, *store.active());
    EXPECT_EQ(inventory.mProvider.at("shared.esp"), two.mId);
    EXPECT_EQ(inventory.mProvider.at("onlyone.esp"), one.mId);
    EXPECT_EQ(inventory.mProvider.count("hidden.esp"), 0u);
}

TEST(ModOrganizerStore, dropping_missing_plugins_keeps_the_games_own_and_the_provided_ones)
{
    PluginInventory inventory;
    inventory.mProvider["there.esp"] = "m";
    Profile profile;
    profile.mPlugins = { { "Morrowind.esm", true }, { "There.esp", true }, { "Gone.esp", true } };
    const auto dropped = dropMissingPlugins(profile, inventory, { "morrowind.esm" });
    ASSERT_EQ(dropped.size(), 1u);
    EXPECT_EQ(dropped[0], "Gone.esp");
    ASSERT_EQ(profile.mPlugins.size(), 2u);
    EXPECT_EQ(profile.mPlugins[0].mName, "Morrowind.esm");
}

TEST(ModOrganizerStore, new_plugins_are_appended_inactive_in_name_order_and_only_once)
{
    PluginInventory inventory;
    inventory.mProvider = { { "b.esp", "m" }, { "a.esp", "m" }, { "known.esp", "m" } };
    inventory.mSpelling = { { "b.esp", "B.esp" }, { "a.esp", "A.esp" }, { "known.esp", "Known.esp" } };
    Profile profile;
    profile.mPlugins = { { "KNOWN.esp", true } };
    EXPECT_EQ(appendNewPlugins(profile, inventory), 2u);
    ASSERT_EQ(profile.mPlugins.size(), 3u);
    EXPECT_EQ(profile.mPlugins[1].mName, "A.esp");
    EXPECT_FALSE(profile.mPlugins[1].mActive);
    EXPECT_EQ(profile.mPlugins[2].mName, "B.esp");
    EXPECT_EQ(appendNewPlugins(profile, inventory), 0u);
}

TEST(ModOrganizerStore, the_plugin_report_finds_each_file_and_reads_its_masters)
{
    TempFolder temp;
    temp.write("base/Morrowind.esm");
    temp.write("m/One/One.esp");
    temp.write("m/Two/one.esp");
    Store store = storeWithDefault(Files::pathToUnicodeString(temp.path() / "base"));
    ASSERT_TRUE(addFolderMod(store, temp.path() / "m" / "One").mOk);
    const auto two = addFolderMod(store, temp.path() / "m" / "Two");
    ASSERT_TRUE(two.mOk);
    store.active()->mPlugins.push_back({ "Missing.esp", true });

    std::vector<std::string> asked;
    const MasterReader reader = [&asked](const std::filesystem::path& file, std::string&) {
        asked.push_back(Files::pathToUnicodeString(file.filename()));
        return std::vector<std::string>{ "Morrowind.esm" };
    };
    const PluginReport report = buildPluginReport(store, reader);
    ASSERT_EQ(report.mRows.size(), 3u);
    EXPECT_EQ(report.mRows[0].mName, "Morrowind.esm");
    EXPECT_TRUE(report.mRows[0].mFromBase);
    EXPECT_TRUE(report.mRows[0].mProviderModId.empty());
    // The later mod wins the shared name, with its own spelling of the file
    EXPECT_EQ(report.mRows[1].mProviderModId, two.mId);
    EXPECT_EQ(Files::pathToUnicodeString(report.mRows[1].mFile.filename()), "one.esp");
    EXPECT_EQ(report.mRows[1].mMasters, std::vector<std::string>{ "Morrowind.esm" });
    EXPECT_TRUE(report.mRows[2].mFile.empty());
    EXPECT_FALSE(report.mRows[2].mError.empty());
    EXPECT_EQ(asked.size(), 2u); // the missing file is not opened
    ASSERT_EQ(report.mBasePlugins.size(), 1u);
    EXPECT_EQ(report.mBasePlugins[0], "Morrowind.esm");
}

TEST(ModOrganizerStore, a_master_reader_error_is_kept_on_its_row_and_no_profile_gives_an_empty_report)
{
    TempFolder temp;
    temp.write("base/Morrowind.esm");
    Store store = storeWithDefault(Files::pathToUnicodeString(temp.path() / "base"));
    const MasterReader failing = [](const std::filesystem::path&, std::string& error) {
        error = "bad header";
        return std::vector<std::string>();
    };
    const PluginReport report = buildPluginReport(store, failing);
    ASSERT_EQ(report.mRows.size(), 1u);
    EXPECT_EQ(report.mRows[0].mError, "bad header");
    EXPECT_TRUE(buildPluginReport(Store(), failing).mRows.empty());
}

// ---- profiles -----------------------------------------------------------------------------------------------------

TEST(ModOrganizerStore, creating_copies_and_refuses_bad_or_used_names)
{
    Store store = storeWithDefault();
    store.mProfiles[0].mArchives = { "x.bsa" };
    EXPECT_TRUE(createProfile(store, "Copy", "Default"));
    EXPECT_EQ(store.findProfile("Copy")->mArchives, store.findProfile("Default")->mArchives);
    EXPECT_EQ(store.findProfile("Copy")->mName, "Copy");
    EXPECT_FALSE(createProfile(store, "Copy"));
    EXPECT_FALSE(createProfile(store, ""));
    EXPECT_FALSE(createProfile(store, "a/b"));
    EXPECT_TRUE(createProfile(store, "Empty"));
    EXPECT_TRUE(store.findProfile("Empty")->mMods.empty());
}

TEST(ModOrganizerStore, profiles_stay_sorted_by_name)
{
    Store store = storeWithDefault();
    ASSERT_TRUE(createProfile(store, "Zed"));
    ASSERT_TRUE(createProfile(store, "Alpha"));
    ASSERT_EQ(store.mProfiles.size(), 3u);
    EXPECT_EQ(store.mProfiles[0].mName, "Alpha");
    EXPECT_EQ(store.mProfiles[2].mName, "Zed");
}

TEST(ModOrganizerStore, deleting_keeps_at_least_one_profile_and_moves_the_active_one)
{
    Store store = storeWithDefault();
    EXPECT_FALSE(deleteProfile(store, "Default")); // the last one
    ASSERT_TRUE(createProfile(store, "Other"));
    store.mActive = "Default";
    EXPECT_TRUE(deleteProfile(store, "Default"));
    EXPECT_EQ(store.mActive, "Other");
    EXPECT_FALSE(deleteProfile(store, "Gone"));
}

TEST(ModOrganizerStore, a_read_only_profile_cannot_be_deleted_or_renamed)
{
    Store store = storeWithDefault();
    ASSERT_TRUE(createProfile(store, "Other"));
    store.mReadOnlyProfiles = { "Other" };
    EXPECT_FALSE(deleteProfile(store, "Other"));
    EXPECT_FALSE(renameProfile(store, "Other", "New"));
}

TEST(ModOrganizerStore, renaming_updates_the_active_name_and_refuses_a_used_name)
{
    Store store = storeWithDefault();
    ASSERT_TRUE(createProfile(store, "Other"));
    EXPECT_TRUE(renameProfile(store, "Default", "Main"));
    EXPECT_EQ(store.mActive, "Main");
    EXPECT_EQ(store.findProfile("Main")->mName, "Main");
    EXPECT_FALSE(renameProfile(store, "Main", "Other"));
    EXPECT_FALSE(renameProfile(store, "Main", "bad/name"));
}

TEST(ModOrganizerStore, deleting_and_renaming_remove_the_old_profile_file_on_save)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path());
    Store store = storeWithDefault();
    ASSERT_TRUE(createProfile(store, "Other"));
    ASSERT_TRUE(saveStore(paths, store).empty());
    ASSERT_TRUE(std::filesystem::exists(paths.profileFile("Other")));
    ASSERT_TRUE(renameProfile(store, "Other", "Renamed"));
    ASSERT_TRUE(saveStore(paths, store).empty());
    EXPECT_FALSE(std::filesystem::exists(paths.profileFile("Other")));
    EXPECT_TRUE(std::filesystem::exists(paths.profileFile("Renamed")));
    ASSERT_TRUE(deleteProfile(store, "Renamed"));
    ASSERT_TRUE(saveStore(paths, store).empty());
    EXPECT_FALSE(std::filesystem::exists(paths.profileFile("Renamed")));
}

// ---- applying to openmw.cfg ----------------------------------------------------------------------------------------

TEST(ModOrganizerStore, the_layout_starts_with_the_base_and_follows_the_profile_order)
{
    TempFolder temp;
    temp.write("m/A/a.esp");
    temp.write("m/B/b.esp");
    Store store = storeWithDefault();
    const auto a = addFolderMod(store, temp.path() / "m" / "A");
    const auto b = addFolderMod(store, temp.path() / "m" / "B");
    ASSERT_TRUE(a.mOk && b.mOk);
    ASSERT_TRUE(moveMod(*store.active(), 0, 1));

    const Layout layout = activeLayout(store);
    ASSERT_EQ(layout.mData.size(), 3u);
    EXPECT_EQ(layout.mData[0].mPath, "C:/Games/Morrowind/Data Files");
    EXPECT_EQ(layout.mData[1].mModId, b.mId);
    EXPECT_EQ(layout.mData[2].mModId, a.mId);
}

TEST(ModOrganizerStore, applying_writes_the_owned_lines_keeps_the_rest_and_records_what_it_wrote)
{
    TempFolder temp;
    temp.write("m/A/a.esp");
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const auto cfg = temp.write("cfg/openmw.cfg", "# mine\nencoding=win1252\ndata=\"C:/old\"\ncontent=Old.esm\n");
    Store store = storeWithDefault();
    ASSERT_TRUE(addFolderMod(store, temp.path() / "m" / "A").mOk);
    ASSERT_TRUE(setPluginActive(*store.active(), "a.esp", true));

    ApplyOptions options;
    options.mGameRunning = [] { return false; };
    const ApplyOutcome outcome = applyActiveProfile(paths, store, cfg, options, false);
    ASSERT_TRUE(outcome.mOk) << outcome.mError;
    EXPECT_TRUE(outcome.mWritten);
    EXPECT_FALSE(outcome.mHandEdited);
    const std::string text = readAll(cfg);
    EXPECT_NE(text.find("# mine"), std::string::npos);
    EXPECT_NE(text.find("encoding=win1252"), std::string::npos);
    EXPECT_NE(text.find("content=Morrowind.esm"), std::string::npos);
    EXPECT_NE(text.find("content=a.esp"), std::string::npos);
    EXPECT_EQ(text.find("C:/old"), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(outcome.mBackup));
    EXPECT_TRUE(std::filesystem::exists(paths.appliedFile("Default")));
}

TEST(ModOrganizerStore, applying_twice_in_a_row_writes_nothing_the_second_time)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const auto cfg = temp.write("cfg/openmw.cfg", "encoding=win1252\n");
    Store store = storeWithDefault();
    ApplyOptions options;
    options.mGameRunning = [] { return false; };
    ASSERT_TRUE(applyActiveProfile(paths, store, cfg, options, false).mWritten);
    const ApplyOutcome second = applyActiveProfile(paths, store, cfg, options, false);
    EXPECT_TRUE(second.mOk);
    EXPECT_FALSE(second.mWritten);
}

TEST(ModOrganizerStore, a_hand_edit_since_the_last_apply_is_refused_until_accepted)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const auto cfg = temp.write("cfg/openmw.cfg", "encoding=win1252\n");
    Store store = storeWithDefault();
    ApplyOptions options;
    options.mGameRunning = [] { return false; };
    ASSERT_TRUE(applyActiveProfile(paths, store, cfg, options, false).mOk);

    // Someone adds a plugin by hand
    {
        std::ofstream out(cfg, std::ios::app | std::ios::binary);
        out << "content=ByHand.esp\n";
    }
    store.active()->mArchives.push_back("New.bsa");
    const std::string before = readAll(cfg);
    const ApplyOutcome refused = applyActiveProfile(paths, store, cfg, options, false);
    EXPECT_FALSE(refused.mOk);
    EXPECT_TRUE(refused.mHandEdited);
    EXPECT_EQ(readAll(cfg), before);

    const ApplyOutcome accepted = applyActiveProfile(paths, store, cfg, options, true);
    EXPECT_TRUE(accepted.mOk);
    EXPECT_TRUE(accepted.mWritten);
    EXPECT_EQ(readAll(cfg).find("ByHand.esp"), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(accepted.mBackup));
    // The backup still has the hand edit
    EXPECT_NE(readAll(accepted.mBackup).find("ByHand.esp"), std::string::npos);
}

TEST(ModOrganizerStore, applying_is_refused_while_the_game_runs_and_for_the_global_file)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const auto cfg = temp.write("cfg/openmw.cfg", "encoding=win1252\n");
    Store store = storeWithDefault();
    ApplyOptions running;
    running.mGameRunning = [] { return true; };
    const ApplyOutcome refused = applyActiveProfile(paths, store, cfg, running, false);
    EXPECT_FALSE(refused.mOk);
    EXPECT_FALSE(refused.mError.empty());
    EXPECT_EQ(readAll(cfg), "encoding=win1252\n");

    ApplyOptions global;
    global.mGlobalConfig = cfg;
    global.mGameRunning = [] { return false; };
    EXPECT_FALSE(applyActiveProfile(paths, store, cfg, global, false).mOk);
}

TEST(ModOrganizerStore, with_no_active_profile_applying_does_nothing)
{
    TempFolder temp;
    const StorePaths paths = storePaths(temp.path() / "cfg");
    const auto cfg = temp.write("cfg/openmw.cfg", "encoding=win1252\n");
    Store store;
    ApplyOptions options;
    options.mGameRunning = [] { return false; };
    const ApplyOutcome outcome = applyActiveProfile(paths, store, cfg, options, false);
    EXPECT_FALSE(outcome.mOk);
    EXPECT_EQ(readAll(cfg), "encoding=win1252\n");
}

TEST(ModOrganizerStore, a_repack_asked_for_but_missing_is_reported_as_a_problem)
{
    Store store = storeWithDefault();
    Mod mod;
    mod.mId = "m";
    mod.mName = "M";
    mod.mRoot = "D:/Mods/M";
    store.mLibrary.mMods.push_back(mod);
    store.active()->mMods.push_back({ "m", true, true });
    const Layout layout = activeLayout(store);
    EXPECT_FALSE(layout.mProblems.empty());
    store.mRepacks["m"] = { "D:/cache/shim", "D:/cache/m.edenpak" };
    const Layout repacked = activeLayout(store);
    EXPECT_TRUE(repacked.mProblems.empty());
    ASSERT_EQ(repacked.mData.size(), 3u);
    EXPECT_TRUE(repacked.mData[2].mIsPack);
}
