#include <components/modorganizer/layout.hpp>

#include <gtest/gtest.h>

using namespace ModOrganizer;

namespace
{
    ModLibrary library()
    {
        ModLibrary l;
        l.mMods.push_back({ "a", "A", ModKind::Folder, "C:/Mods/A", "", "" });
        l.mMods.push_back({ "b", "B", ModKind::Folder, "C:/Mods/B", "Data Files", "" });
        l.mMods.push_back({ "p", "P", ModKind::Pack, "C:/Mods/p.edenpak", "", "" });
        return l;
    }
}

TEST(ModOrganizerLayout, golden_lines_for_a_generated_profile)
{
    Profile p;
    p.mName = "Main";
    p.mMods = { { "a", true, false }, { "b", false, false }, { "p", true, false } };
    p.mPlugins = { { "one.esm", true }, { "two.esp", false }, { "three.esp", true } };
    p.mArchives = { "x.bsa" };
    p.mGroundcover = { "grass.esp" };
    p.mExtensions = { "compat_morrowind" };
    LayoutOptions options;
    options.mBaseData = { "C:/Morrowind/Data Files" };

    const Layout layout = buildLayout(library(), p, options);
    EXPECT_TRUE(layout.mProblems.empty());
    const std::vector<std::string> expected = {
        "fallback-archive=x.bsa",
        "data=\"C:/Morrowind/Data Files\"",
        "data=\"C:/Mods/A\"",
        "data=\"C:/Mods/p.edenpak\"",
        "groundcover=grass.esp",
        "content=one.esm",
        "content=three.esp",
        "extension=compat_morrowind",
    };
    EXPECT_EQ(renderConfigLines(layout), expected);
}

TEST(ModOrganizerLayout, data_path_is_joined_and_disabled_mods_are_left_out)
{
    Profile p;
    p.mMods = { { "b", true, false } };
    const Layout layout = buildLayout(library(), p, LayoutOptions());
    ASSERT_EQ(layout.mData.size(), 1u);
    EXPECT_EQ(layout.mData[0].mPath, "C:/Mods/B/Data Files");
    EXPECT_EQ(layout.mData[0].mModId, "b");
}

TEST(ModOrganizerLayout, quoting_follows_the_config_file_rules)
{
    EXPECT_EQ(quoteConfigPath("C:/a b"), "\"C:/a b\"");
    EXPECT_EQ(quoteConfigPath("a\"b"), "\"a&\"b\"");
    EXPECT_EQ(quoteConfigPath("a&b"), "\"a&&b\"");
    EXPECT_EQ(quoteConfigPath("&\""), "\"&&&\"\"");
}

TEST(ModOrganizerLayout, an_unknown_mod_is_a_problem_and_contributes_nothing)
{
    Profile p;
    p.mMods = { { "ghost", true, false }, { "a", true, false } };
    const Layout layout = buildLayout(library(), p, LayoutOptions());
    ASSERT_EQ(layout.mProblems.size(), 1u);
    EXPECT_NE(layout.mProblems[0].find("ghost"), std::string::npos);
    EXPECT_EQ(layout.mData.size(), 1u);
}

TEST(ModOrganizerLayout, a_repacked_mod_is_a_shim_and_a_pack_at_the_old_position)
{
    Profile p;
    p.mMods = { { "a", true, false }, { "b", true, true }, { "p", true, false } };
    LayoutOptions options;
    options.mRepacks["b"] = { "D:/cache/b-shim", "D:/cache/b-key.edenpak" };
    const Layout layout = buildLayout(library(), p, options);
    ASSERT_EQ(layout.mData.size(), 4u);
    EXPECT_EQ(layout.mData[0].mModId, "a");
    EXPECT_EQ(layout.mData[1], (DataEntry{ "D:/cache/b-shim", "b", false, true }));
    EXPECT_EQ(layout.mData[2], (DataEntry{ "D:/cache/b-key.edenpak", "b", true, false }));
    EXPECT_EQ(layout.mData[3].mModId, "p");
    const auto lines = renderConfigLines(layout);
    for (const std::string& line : lines)
    {
        EXPECT_NE(line.rfind("fallback-archive", 0), 0u) << "a pack never goes to fallback-archive";
    }
}

TEST(ModOrganizerLayout, use_repack_without_a_repack_falls_back_to_the_original_and_says_so)
{
    Profile p;
    p.mMods = { { "a", true, true } };
    const Layout layout = buildLayout(library(), p, LayoutOptions());
    ASSERT_EQ(layout.mData.size(), 1u);
    EXPECT_FALSE(layout.mData[0].mIsPack);
    EXPECT_EQ(layout.mProblems.size(), 1u);
}

TEST(ModOrganizerLayout, same_profile_same_lines)
{
    Profile p;
    p.mMods = { { "a", true, false }, { "b", true, false } };
    p.mPlugins = { { "z.esp", true }, { "a.esp", true } };
    EXPECT_EQ(renderConfigLines(buildLayout(library(), p, LayoutOptions())),
        renderConfigLines(buildLayout(library(), p, LayoutOptions())));
    const auto lines = renderConfigLines(buildLayout(library(), p, LayoutOptions()));
    EXPECT_EQ(lines.back(), "content=a.esp") << "plugin order is the profile's, not sorted";
}

TEST(ModOrganizerLayout, a_value_with_a_newline_cannot_inject_a_line)
{
    Profile p;
    p.mPlugins = { { "x.esp\nextension=evil", true } };
    p.mExtensions = { "a\r\nb" };
    const Layout layout = buildLayout(library(), p, LayoutOptions());
    for (const std::string& line : renderConfigLines(layout))
    {
        EXPECT_EQ(line.find_first_of("\r\n"), std::string::npos);
    }
    EXPECT_FALSE(layout.mProblems.empty());
}
