// Task 0058: the [WorldLod] settings table of the launcher page and the pack and bake helpers. Settings defaults are
// compared with files/settings-default.cfg itself, so the page cannot drift from the game. Files are generated.

#include <components/worldlod/lodsettings.hpp>

#include "../modorganizer/tempfolder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace WorldLod;
using ModOrganizerTest::TempFolder;

namespace
{
    std::string trim(const std::string& s)
    {
        const auto b = s.find_first_not_of(" \t");
        const auto e = s.find_last_not_of(" \t");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    }

    // key to value of the [WorldLod] section of the default settings file
    std::map<std::string, std::string> defaultWorldLodSection()
    {
        std::ifstream in(std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "settings-default.cfg");
        std::map<std::string, std::string> result;
        std::string line;
        bool inSection = false;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty() && line.front() == '[')
            {
                inSection = line == "[WorldLod]";
                continue;
            }
            if (!inSection || line.empty() || line.front() == '#')
                continue;
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            result[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
        }
        return result;
    }

    const SettingSpec& spec(std::string_view key)
    {
        const SettingSpec* found = findSpec(key);
        EXPECT_NE(found, nullptr) << key;
        static const SettingSpec empty;
        return found != nullptr ? *found : empty;
    }
}

TEST(WorldLodSettings, every_key_of_the_table_exists_in_the_default_file_with_the_same_default)
{
    const auto defaults = defaultWorldLodSection();
    ASSERT_FALSE(defaults.empty());
    for (const SettingSpec& s : settingSpecs())
    {
        const auto it = defaults.find(std::string(s.mKey));
        ASSERT_NE(it, defaults.end()) << "no such key in [WorldLod]: " << s.mKey;
        // Floats and integers are compared as numbers: the file writes 1.0 where the table writes 1
        if (s.mKind == SettingKind::Float || s.mKind == SettingKind::Int)
            EXPECT_EQ(std::stod(it->second), std::stod(std::string(s.mDefault))) << s.mKey;
        else
            EXPECT_EQ(it->second, std::string(s.mDefault)) << s.mKey;
    }
}

TEST(WorldLodSettings, the_table_has_the_keys_the_task_names)
{
    for (const char* key : { "enabled", "view distance", "map view", "tier", "far levels", "object clusters",
             "object cluster effects", "object cluster scenery", "object cluster sway" })
        EXPECT_NE(findSpec(key), nullptr) << key;
    EXPECT_EQ(findSpec("terrain tile level"), nullptr); // only 1 is accepted by the game
    EXPECT_EQ(findSpec("no such key"), nullptr);
}

TEST(WorldLodSettings, keys_are_unique_and_every_default_normalizes_to_itself)
{
    std::set<std::string_view> seen;
    for (const SettingSpec& s : settingSpecs())
    {
        EXPECT_TRUE(seen.insert(s.mKey).second) << s.mKey;
        const Normalized n = normalizeSetting(s, s.mDefault);
        EXPECT_TRUE(n.mOk) << s.mKey << ": " << n.mProblem;
        EXPECT_TRUE(isDefault(s, n.mText)) << s.mKey;
    }
}

TEST(WorldLodSettings, a_row_that_needs_a_switch_names_it)
{
    EXPECT_EQ(spec("object clusters").mNeeds, "enabled");
    EXPECT_EQ(spec("object cluster effects").mNeeds, "object clusters");
    EXPECT_EQ(spec("haze").mNeeds, "map view");
    EXPECT_TRUE(spec("enabled").mNeeds.empty());
    const auto off = [](std::string_view) { return std::string("false"); };
    const auto on = [](std::string_view) { return std::string("true"); };
    EXPECT_EQ(unmetNeed(spec("haze"), off), "map view");
    EXPECT_EQ(unmetNeed(spec("haze"), on), "");
    EXPECT_EQ(unmetNeed(spec("enabled"), off), "");
}

TEST(WorldLodSettings, bools_are_true_or_false_only)
{
    EXPECT_EQ(normalizeSetting(spec("enabled"), "true").mText, "true");
    EXPECT_EQ(normalizeSetting(spec("enabled"), "false").mText, "false");
    EXPECT_EQ(normalizeSetting(spec("enabled"), "TRUE").mText, "true");
    EXPECT_FALSE(normalizeSetting(spec("enabled"), "yes").mOk);
    EXPECT_FALSE(normalizeSetting(spec("enabled"), "").mOk);
}

TEST(WorldLodSettings, floats_are_cut_to_the_engine_ranges_and_written_short)
{
    const SettingSpec& fade = spec("terrain fade time"); // 0 to 60
    EXPECT_EQ(normalizeSetting(fade, "0.4").mText, "0.4");
    Normalized n = normalizeSetting(fade, "100");
    EXPECT_TRUE(n.mOk);
    EXPECT_TRUE(n.mChanged);
    EXPECT_EQ(n.mText, "60");
    n = normalizeSetting(fade, "-3");
    EXPECT_EQ(n.mText, "0");
    EXPECT_EQ(normalizeSetting(fade, " 2.50 ").mText, "2.5");
    EXPECT_FALSE(normalizeSetting(fade, "soon").mOk);
    EXPECT_FALSE(normalizeSetting(fade, "nan").mOk);
    EXPECT_FALSE(normalizeSetting(fade, "inf").mOk);
    EXPECT_FALSE(normalizeSetting(fade, "").mOk);

    const SettingSpec& threshold = spec("terrain tile threshold"); // 0.01 to 1000000
    EXPECT_EQ(normalizeSetting(threshold, "0").mText, "0.01");
    EXPECT_EQ(normalizeSetting(threshold, "5000000").mText, "1000000");
    const SettingSpec& lean = spec("object cluster sway lean"); // 0 to 0.1
    EXPECT_EQ(normalizeSetting(lean, "0.5").mText, "0.1");
    const SettingSpec& wind = spec("object cluster sway wind"); // 0 to 4
    EXPECT_EQ(normalizeSetting(wind, "9").mText, "4");
}

TEST(WorldLodSettings, integers_are_cut_and_must_be_whole)
{
    const SettingSpec& levels = spec("far levels"); // 0 to 6
    EXPECT_EQ(normalizeSetting(levels, "3").mText, "3");
    EXPECT_EQ(normalizeSetting(levels, "9").mText, "6");
    EXPECT_EQ(normalizeSetting(levels, "-1").mText, "0");
    EXPECT_FALSE(normalizeSetting(levels, "2.5").mOk);
    EXPECT_FALSE(normalizeSetting(levels, "two").mOk);
    EXPECT_EQ(normalizeSetting(spec("far texture max size"), "99999").mText, "8192");
    EXPECT_EQ(normalizeSetting(spec("far memory budget"), "70000").mText, "65536");
}

TEST(WorldLodSettings, the_tier_is_one_of_its_choices)
{
    const SettingSpec& tier = spec("tier");
    for (const char* ok : { "auto", "minimal", "low", "medium", "high" })
        EXPECT_TRUE(normalizeSetting(tier, ok).mOk) << ok;
    EXPECT_EQ(normalizeSetting(tier, "HIGH").mText, "high");
    EXPECT_FALSE(normalizeSetting(tier, "ultra").mOk);
}

TEST(WorldLodSettings, the_view_distance_is_auto_or_a_positive_number)
{
    const SettingSpec& view = spec("view distance");
    EXPECT_EQ(normalizeSetting(view, "auto").mText, "auto");
    EXPECT_EQ(normalizeSetting(view, "AUTO").mText, "auto");
    EXPECT_EQ(normalizeSetting(view, "40000").mText, "40000");
    EXPECT_EQ(normalizeSetting(view, "12345.5").mText, "12345.5");
    EXPECT_FALSE(normalizeSetting(view, "far").mOk);
    EXPECT_FALSE(normalizeSetting(view, "0").mOk);
    EXPECT_FALSE(normalizeSetting(view, "-5").mOk);
    // The engine cuts a number above 10000000 to it, with a warning
    EXPECT_EQ(normalizeSetting(view, "99999999").mText, "10000000");
}

TEST(WorldLodSettings, the_sway_word_list_is_tidied_and_must_not_be_empty_or_hold_line_breaks)
{
    const SettingSpec& models = spec("object cluster sway models");
    EXPECT_EQ(normalizeSetting(models, "tree,  bush ,reed").mText, "tree, bush, reed");
    EXPECT_FALSE(normalizeSetting(models, "").mOk);
    EXPECT_FALSE(normalizeSetting(models, " , ,").mOk);
    EXPECT_FALSE(normalizeSetting(models, "tree\nbush").mOk);
    EXPECT_FALSE(normalizeSetting(models, std::string(2000, 'a')).mOk);
}

TEST(WorldLodSettings, is_default_compares_the_normalized_value)
{
    EXPECT_TRUE(isDefault(spec("terrain tile threshold"), "8"));
    EXPECT_TRUE(isDefault(spec("terrain tile threshold"), "8.0"));
    EXPECT_FALSE(isDefault(spec("terrain tile threshold"), "9"));
    EXPECT_TRUE(isDefault(spec("object cluster sway wind"), "1"));
}

// ---- packs and bake commands ------------------------------------------------------------------------------------

TEST(WorldLodPacks, the_folder_is_openeden_lod_below_the_user_data)
{
    EXPECT_EQ(lodFolder("/data"), std::filesystem::path("/data") / "openeden" / "lod");
}

TEST(WorldLodPacks, nothing_baked_describes_two_missing_packs)
{
    TempFolder temp;
    const auto packs = describePacks(temp.path());
    ASSERT_EQ(packs.size(), 2u);
    EXPECT_EQ(packs[0].mKind, PackKind::TerrainTiles);
    EXPECT_EQ(packs[0].mFileName, "terrain.edenpak");
    EXPECT_FALSE(packs[0].mExists);
    EXPECT_EQ(packs[1].mKind, PackKind::ObjectClusters);
    EXPECT_EQ(packs[1].mFileName, "objects.edenpak");
    EXPECT_FALSE(packs[1].mExists);
}

TEST(WorldLodPacks, a_baked_pack_reports_its_size_and_time)
{
    TempFolder temp;
    temp.write("openeden/lod/terrain.edenpak", std::string(1234, 'x'));
    const auto packs = describePacks(temp.path());
    ASSERT_EQ(packs.size(), 2u);
    EXPECT_TRUE(packs[0].mExists);
    EXPECT_EQ(packs[0].mBytes, 1234u);
    EXPECT_GT(packs[0].mModifiedSeconds, 1000000000);
    EXPECT_FALSE(packs[1].mExists);
}

TEST(WorldLodPacks, a_folder_with_a_pack_name_is_not_a_pack)
{
    TempFolder temp;
    temp.write("openeden/lod/objects.edenpak/inner.txt");
    const auto packs = describePacks(temp.path());
    EXPECT_FALSE(packs[1].mExists);
}

TEST(WorldLodBake, the_arguments_are_the_flags_that_exist_and_nothing_else)
{
    EXPECT_EQ(bakeArguments(PackKind::TerrainTiles), std::vector<std::string>({ "--bake-lod-terrain" }));
    EXPECT_EQ(bakeArguments(PackKind::ObjectClusters), std::vector<std::string>({ "--bake-lod-objects" }));
}

TEST(WorldLodSummary, the_summary_follows_the_order_off_running_status_file_packs)
{
    PackInfo none[2];
    none[0].mKind = PackKind::TerrainTiles;
    none[1].mKind = PackKind::ObjectClusters;
    std::vector<PackInfo> packs(none, none + 2);

    EXPECT_EQ(summarize(false, true, true, packs), Summary::Off);
    EXPECT_EQ(summarize(true, true, true, packs), Summary::RunningHere);
    EXPECT_EQ(summarize(true, false, true, packs), Summary::FromStatusFile);
    EXPECT_EQ(summarize(true, false, false, packs), Summary::NotBaked);
    packs[0].mExists = true;
    EXPECT_EQ(summarize(true, false, false, packs), Summary::PartlyBaked);
    packs[1].mExists = true;
    EXPECT_EQ(summarize(true, false, false, packs), Summary::Baked);
    EXPECT_EQ(summarize(true, false, false, {}), Summary::NotBaked);
}
