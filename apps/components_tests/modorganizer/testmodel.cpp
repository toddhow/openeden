#include <components/modorganizer/model.hpp>

#include <gtest/gtest.h>

using namespace ModOrganizer;

namespace
{
    Profile sampleProfile()
    {
        Profile p;
        p.mName = "Main";
        p.mMods = { { "a", true, false }, { "b-2", false, true } };
        p.mPlugins = { { "foo.esp", true }, { "Bar.ESM", false } };
        p.mArchives = { "x.bsa" };
        p.mGroundcover = { "grass.esp" };
        p.mExtensions = { "compat_morrowind" };
        return p;
    }

    ModLibrary sampleLibrary()
    {
        ModLibrary l;
        l.mMods.push_back({ "a", "Mod A", ModKind::Folder, "C:/Mods/A", "Data Files", "a note" });
        l.mMods.push_back({ "b-2", "Ünïcode B", ModKind::Pack, "C:/Mods/b.edenpak", "", "" });
        return l;
    }
}

TEST(ModOrganizerModel, golden_text_of_a_small_profile)
{
    Profile p;
    p.mName = "Main";
    p.mMods = { { "a", true, false } };
    p.mPlugins = { { "foo.esp", true } };
    const char* expected
        = "{\n"
          "  \"archives\": [],\n"
          "  \"extensions\": [],\n"
          "  \"format\": \"openeden.profile\",\n"
          "  \"groundcover\": [],\n"
          "  \"mods\": [\n"
          "    {\n"
          "      \"enabled\": true,\n"
          "      \"id\": \"a\",\n"
          "      \"useRepack\": false\n"
          "    }\n"
          "  ],\n"
          "  \"name\": \"Main\",\n"
          "  \"plugins\": [\n"
          "    {\n"
          "      \"active\": true,\n"
          "      \"name\": \"foo.esp\"\n"
          "    }\n"
          "  ],\n"
          "  \"version\": 1\n"
          "}\n";
    EXPECT_EQ(writeProfile(p), expected);
}

TEST(ModOrganizerModel, profile_round_trips_byte_for_byte)
{
    const std::string text = writeProfile(sampleProfile());
    LoadError error;
    const auto read = readProfile(text, "p.json", error);
    ASSERT_TRUE(read.has_value()) << error.format();
    EXPECT_EQ(*read, sampleProfile());
    EXPECT_EQ(writeProfile(*read), text);
}

TEST(ModOrganizerModel, library_round_trips_byte_for_byte_with_unicode)
{
    const std::string text = writeModLibrary(sampleLibrary());
    EXPECT_NE(text.find("Ünïcode B"), std::string::npos) << "UTF-8 is written as it is";
    LoadError error;
    const auto read = readModLibrary(text, "mods.json", error);
    ASSERT_TRUE(read.has_value()) << error.format();
    EXPECT_EQ(*read, sampleLibrary());
    EXPECT_EQ(writeModLibrary(*read), text);
}

TEST(ModOrganizerModel, a_pack_is_written_with_kind_pack)
{
    EXPECT_NE(writeModLibrary(sampleLibrary()).find("\"kind\": \"pack\""), std::string::npos);
}

TEST(ModOrganizerModel, a_higher_version_is_reported_as_newer_not_as_a_syntax_error)
{
    std::string text = writeProfile(sampleProfile());
    text.replace(text.find("\"version\": 1"), 12, "\"version\": 2");
    LoadError error;
    EXPECT_FALSE(readProfile(text, "p.json", error).has_value());
    EXPECT_TRUE(error.mNewerVersion);
}

TEST(ModOrganizerModel, wrong_format_string_is_refused_with_a_located_error)
{
    std::string text = writeProfile(sampleProfile());
    text.replace(text.find("openeden.profile"), 16, "openeden.profilX");
    LoadError error;
    EXPECT_FALSE(readProfile(text, "p.json", error).has_value());
    EXPECT_EQ(error.mPointer, "/format");
    EXPECT_NE(error.format().find("p.json"), std::string::npos);
}

TEST(ModOrganizerModel, hostile_text_is_refused_without_throwing)
{
    LoadError error;
    EXPECT_FALSE(readProfile("", "p", error).has_value());
    EXPECT_FALSE(readProfile("{", "p", error).has_value());
    EXPECT_FALSE(readProfile("[]", "p", error).has_value());
    EXPECT_FALSE(readProfile("null", "p", error).has_value());
    EXPECT_FALSE(readProfile(std::string(10, '\0'), "p", error).has_value());
    EXPECT_FALSE(readProfile(std::string(Limits::sMaxFileBytes + 1, ' '), "p", error).has_value());
    EXPECT_FALSE(readProfile(std::string(100000, '['), "p", error).has_value()) << "deep nesting";
    EXPECT_FALSE(readModLibrary("{\"format\":\"openeden.mods\",\"version\":1,\"mods\":[1]}", "m", error).has_value());
}

TEST(ModOrganizerModel, duplicate_keys_unknown_keys_and_floats_are_refused)
{
    LoadError error;
    EXPECT_FALSE(
        readModLibrary("{\"format\":\"openeden.mods\",\"version\":1,\"mods\":[],\"mods\":[]}", "m", error).has_value());
    EXPECT_FALSE(
        readModLibrary("{\"format\":\"openeden.mods\",\"version\":1,\"mods\":[],\"extra\":1}", "m", error).has_value());
    EXPECT_EQ(error.mPointer, "/extra");
    EXPECT_FALSE(readModLibrary("{\"format\":\"openeden.mods\",\"version\":1.5,\"mods\":[]}", "m", error).has_value());
}

TEST(ModOrganizerModel, a_mod_with_a_bad_id_or_data_path_is_refused)
{
    const auto lib = [](const std::string& id, const std::string& dataPath) {
        return "{\"format\":\"openeden.mods\",\"version\":1,\"mods\":[{\"id\":\"" + id
            + "\",\"name\":\"n\",\"kind\":\"folder\",\"root\":\"C:/m\",\"dataPath\":\"" + dataPath
            + "\",\"notes\":\"\"}]}";
    };
    LoadError error;
    EXPECT_TRUE(readModLibrary(lib("ok", "Data Files"), "m", error).has_value()) << error.format();
    EXPECT_FALSE(readModLibrary(lib("Bad Id", ""), "m", error).has_value());
    EXPECT_EQ(error.mPointer, "/mods/0/id");
    EXPECT_FALSE(readModLibrary(lib("ok", "../up"), "m", error).has_value());
    EXPECT_EQ(error.mPointer, "/mods/0/dataPath");
    EXPECT_FALSE(readModLibrary(lib("ok", "/abs"), "m", error).has_value());
    EXPECT_FALSE(readModLibrary(lib("ok", "a\\\\b"), "m", error).has_value());
    EXPECT_FALSE(readModLibrary(lib("ok", "con"), "m", error).has_value()) << "windows device name";
}

TEST(ModOrganizerModel, duplicate_mod_ids_are_refused)
{
    ModLibrary l = sampleLibrary();
    l.mMods[1].mId = "a";
    LoadError error;
    EXPECT_FALSE(readModLibrary(writeModLibrary(l), "m", error).has_value());
    EXPECT_EQ(error.mPointer, "/mods/1/id");
}

TEST(ModOrganizerModel, a_profile_naming_a_mod_twice_or_a_plugin_twice_is_refused)
{
    Profile p = sampleProfile();
    p.mMods.push_back({ "a", true, false });
    LoadError error;
    EXPECT_FALSE(readProfile(writeProfile(p), "p", error).has_value());
    p = sampleProfile();
    p.mPlugins.push_back({ "FOO.esp", true });
    EXPECT_FALSE(readProfile(writeProfile(p), "p", error).has_value()) << "plugin names compare case-insensitively";
}

TEST(ModOrganizerModel, control_characters_in_text_are_refused)
{
    Profile p = sampleProfile();
    p.mName = "bad\nname";
    LoadError error;
    EXPECT_FALSE(readProfile(writeProfile(p), "p", error).has_value());
}

TEST(ModOrganizerModel, counts_are_capped)
{
    Profile p = sampleProfile();
    for (std::size_t i = 0; i <= Limits::sMaxPlugins; ++i)
        p.mPlugins.push_back({ "p" + std::to_string(i) + ".esp", true });
    LoadError error;
    EXPECT_FALSE(readProfile(writeProfile(p), "p", error).has_value());
}

TEST(ModOrganizerModel, ids_and_profile_names)
{
    EXPECT_TRUE(isValidModId("a"));
    EXPECT_TRUE(isValidModId("my-mod_1.2"));
    EXPECT_FALSE(isValidModId(""));
    EXPECT_FALSE(isValidModId("-x"));
    EXPECT_FALSE(isValidModId("A"));
    EXPECT_FALSE(isValidModId(std::string(65, 'a')));
    EXPECT_TRUE(isValidProfileName("My profile 1"));
    EXPECT_FALSE(isValidProfileName(""));
    EXPECT_FALSE(isValidProfileName("a/b"));
    EXPECT_FALSE(isValidProfileName("a\\b"));
    EXPECT_FALSE(isValidProfileName("nul"));
    EXPECT_FALSE(isValidProfileName("name."));
    EXPECT_FALSE(isValidProfileName(std::string(65, 'a')));
}

TEST(ModOrganizerModel, make_mod_id_slugs_and_avoids_taken_ids)
{
    EXPECT_EQ(makeModId("Better Bodies 2.0!", {}), "better-bodies-2.0");
    EXPECT_EQ(makeModId("Better Bodies 2.0!", { "better-bodies-2.0" }), "better-bodies-2.0-2");
    EXPECT_EQ(makeModId("Better Bodies 2.0!", { "better-bodies-2.0", "better-bodies-2.0-2" }), "better-bodies-2.0-3");
    EXPECT_TRUE(isValidModId(makeModId("###", {})));
    EXPECT_TRUE(isValidModId(makeModId(std::string(500, 'x'), {})));
}

TEST(ModOrganizerModel, library_find)
{
    const ModLibrary l = sampleLibrary();
    ASSERT_NE(l.find("a"), nullptr);
    EXPECT_EQ(l.find("a")->mName, "Mod A");
    EXPECT_EQ(l.find("zz"), nullptr);
}
