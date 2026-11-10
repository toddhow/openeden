// Task 0097 stage C: the REQUIRES= tag of .omwscripts and the refusal of a script whose requirement is not met.
//
// In plain words: a mod's script line can say which script namespace of a game pack it needs, for example
// "GLOBAL REQUIRES=core.skyrimse.actor>=1.2: scripts/x.lua". The parser records that; when the configuration is built
// the core asks a function whether each requirement is met, and a script whose requirement is not met is left out as if
// its mod were not installed, with one log line saying why. Scripts without the tag are never asked about.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <components/lua/configuration.hpp>
#include <components/testing/expecterror.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using testing::ElementsAre;
    using testing::Pair;

    LuaUtil::ScriptsConfiguration::RequirementCheck recordingCheck(
        std::vector<std::string>& asked, const std::string& answer)
    {
        return [&asked, answer](const ESM::LuaScriptCfg::Requirement& r) {
            asked.push_back(r.mPackage + ">=" + std::to_string(r.mMajor) + "." + std::to_string(r.mMinor));
            return answer;
        };
    }

    TEST(LuaConfigRequires, a_requires_tag_is_parsed_with_package_major_and_minor)
    {
        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg, "GLOBAL REQUIRES=core.skyrimse.actor>=1.2: my_mod/x.lua\n");
        ASSERT_EQ(cfg.mScripts.size(), 1u);
        EXPECT_EQ(cfg.mScripts[0].mFlags, ESM::LuaScriptCfg::sGlobal);
        ASSERT_EQ(cfg.mScripts[0].mRequires.size(), 1u);
        EXPECT_EQ(cfg.mScripts[0].mRequires[0].mPackage, "core.skyrimse.actor");
        EXPECT_EQ(cfg.mScripts[0].mRequires[0].mMajor, 1u);
        EXPECT_EQ(cfg.mScripts[0].mRequires[0].mMinor, 2u);
    }

    TEST(LuaConfigRequires, several_tags_and_commas_work_and_the_tag_can_come_first)
    {
        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg,
            "REQUIRES=core.a.b>=1.0, REQUIRES=core.c.d>=12.345 PLAYER : m/y.lua\n"
            "NPC,REQUIRES=core.a.b>=2.0:m/z.lua\n");
        ASSERT_EQ(cfg.mScripts.size(), 2u);
        ASSERT_EQ(cfg.mScripts[0].mRequires.size(), 2u);
        EXPECT_EQ(cfg.mScripts[0].mRequires[1].mPackage, "core.c.d");
        EXPECT_EQ(cfg.mScripts[0].mRequires[1].mMajor, 12u);
        EXPECT_EQ(cfg.mScripts[0].mRequires[1].mMinor, 345u);
        EXPECT_EQ(cfg.mScripts[0].mFlags, ESM::LuaScriptCfg::sPlayer);
        ASSERT_EQ(cfg.mScripts[1].mRequires.size(), 1u);
        EXPECT_EQ(cfg.mScripts[1].mTypes.size(), 1u);
    }

    TEST(LuaConfigRequires, a_line_without_the_tag_parses_exactly_as_before)
    {
        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg, "GLOBAL: my_mod/a.lua\nCUSTOM, NPC: my_mod/b.lua\n");
        ASSERT_EQ(cfg.mScripts.size(), 2u);
        EXPECT_TRUE(cfg.mScripts[0].mRequires.empty());
        EXPECT_TRUE(cfg.mScripts[1].mRequires.empty());
        EXPECT_EQ(LuaUtil::scriptCfgToString(cfg.mScripts[0]), "GLOBAL : my_mod/a.lua");
        EXPECT_EQ(LuaUtil::scriptCfgToString(cfg.mScripts[1]), "CUSTOM NPC : my_mod/b.lua");
    }

    TEST(LuaConfigRequires, a_malformed_tag_is_an_error_that_names_the_line)
    {
        const char* bad[] = {
            "GLOBAL REQUIRES=core.x.y>=1: s.lua", // no minor
            "GLOBAL REQUIRES=core.x.y>1.2: s.lua", // not >=
            "GLOBAL REQUIRES=>=1.2: s.lua", // no package
            "GLOBAL REQUIRES=core.x.y>=a.b: s.lua", // not numbers
            "GLOBAL REQUIRES=core.x.y>=1.2.3: s.lua", // too many parts
            "GLOBAL REQUIRES=: s.lua", // empty
            "GLOBAL REQUIRES=Core.X.Y>=1.2: s.lua", // capital letters
            "GLOBAL REQUIRES=core.x.y>=1.99999999999: s.lua", // too big
            "GLOBAL REQUIRES core.x.y>=1.2: s.lua", // no equals sign: an unknown tag
        };
        for (const char* line : bad)
        {
            ESM::LuaScriptsCfg cfg;
            try
            {
                LuaUtil::parseOMWScripts(cfg, line);
                ADD_FAILURE() << "accepted: " << line;
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("REQUIRES"), std::string::npos) << line << " -> " << e.what();
            }
        }
        ESM::LuaScriptsCfg cfg;
        try
        {
            LuaUtil::parseOMWScripts(cfg, "GLOBAL REQUIRES=core.x.y>=1: s.lua");
        }
        catch (const std::runtime_error& e)
        {
            EXPECT_NE(std::string(e.what()).find("GLOBAL REQUIRES=core.x.y>=1: s.lua"), std::string::npos);
        }
    }

    TEST(LuaConfigRequires, the_text_form_of_a_script_shows_its_requirements)
    {
        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg, "GLOBAL REQUIRES=core.a.b>=1.2: m/x.lua\n");
        EXPECT_NE(LuaUtil::scriptCfgToString(cfg.mScripts[0]).find("REQUIRES=core.a.b>=1.2"), std::string::npos);
    }

    TEST(LuaConfigRequires, a_met_requirement_keeps_the_script_and_the_check_is_asked_once_per_requirement)
    {
        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg, "GLOBAL REQUIRES=core.a.b>=1.2, REQUIRES=core.c.d>=3.0: m/x.lua\n");
        std::vector<std::string> asked;
        LuaUtil::ScriptsConfiguration conf;
        conf.init(std::move(cfg), false, recordingCheck(asked, ""));
        ASSERT_EQ(conf.size(), 1u);
        EXPECT_THAT(asked, ElementsAre("core.a.b>=1.2", "core.c.d>=3.0"));
        EXPECT_TRUE(conf.findId(VFS::Path::NormalizedView("m/x.lua")).has_value());
    }

    TEST(LuaConfigRequires, an_unmet_requirement_drops_only_that_script)
    {
        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg,
            "GLOBAL REQUIRES=core.a.b>=9.0: m/needs.lua\n"
            "GLOBAL: m/plain.lua\n"
            "PLAYER: m/player.lua\n");
        std::vector<std::string> asked;
        LuaUtil::ScriptsConfiguration conf;
        conf.init(std::move(cfg), false, recordingCheck(asked, "the session has core.a.b 1.2"));
        ASSERT_EQ(conf.size(), 2u);
        EXPECT_FALSE(conf.findId(VFS::Path::NormalizedView("m/needs.lua")).has_value());
        EXPECT_TRUE(conf.findId(VFS::Path::NormalizedView("m/plain.lua")).has_value());
        EXPECT_EQ(conf.getGlobalConf().size(), 1u);
        EXPECT_EQ(conf.getPlayerConf().size(), 1u);
        EXPECT_THAT(asked, ElementsAre("core.a.b>=9.0")); // scripts without a tag are never asked about
    }

    TEST(LuaConfigRequires, with_no_check_function_a_script_with_a_requirement_is_refused)
    {
        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg, "GLOBAL REQUIRES=core.a.b>=1.0: m/needs.lua\nGLOBAL: m/plain.lua\n");
        LuaUtil::ScriptsConfiguration conf;
        conf.init(std::move(cfg), false);
        ASSERT_EQ(conf.size(), 1u);
        EXPECT_TRUE(conf.findId(VFS::Path::NormalizedView("m/plain.lua")).has_value());
    }

    TEST(LuaConfigRequires, a_merge_unites_the_requirements_and_one_unmet_one_drops_the_script)
    {
        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg, "CUSTOM REQUIRES=core.a.b>=1.0: m/x.lua\n");
        LuaUtil::parseOMWScripts(cfg, "CUSTOM REQUIRES=core.c.d>=1.0, REQUIRES=core.a.b>=1.0: m/x.lua\n");
        cfg.mScripts[1].mFlags |= ESM::LuaScriptCfg::sMerge;

        std::vector<std::string> asked;
        LuaUtil::ScriptsConfiguration conf;
        auto check = [&asked](const ESM::LuaScriptCfg::Requirement& r) {
            asked.push_back(r.mPackage);
            return r.mPackage == "core.c.d" ? std::string("missing") : std::string();
        };
        conf.init(ESM::LuaScriptsCfg(cfg), false, check);
        EXPECT_EQ(conf.size(), 0u);
        // The union has each requirement once
        EXPECT_THAT(asked, ::testing::UnorderedElementsAre("core.a.b", "core.c.d"));

        asked.clear();
        LuaUtil::ScriptsConfiguration all;
        all.init(ESM::LuaScriptsCfg(cfg), false, recordingCheck(asked, ""));
        ASSERT_EQ(all.size(), 1u);
        EXPECT_EQ(all[0].mRequires.size(), 2u);
    }

    TEST(LuaConfigRequires, a_saved_script_of_a_refused_script_finds_no_mapping_after_a_reload)
    {
        ESM::LuaScriptsCfg first;
        LuaUtil::parseOMWScripts(first, "GLOBAL REQUIRES=core.a.b>=1.0: m/needs.lua\nGLOBAL: m/plain.lua\n");
        LuaUtil::ScriptsConfiguration conf;
        conf.init(
            ESM::LuaScriptsCfg(first), false, [](const ESM::LuaScriptCfg::Requirement&) { return std::string(); });
        // Both are known the first time (ids 0 and 1)
        ASSERT_EQ(conf.size(), 2u);

        // Now the session cannot satisfy the requirement (another game pack): a reload with remapping
        conf.init(ESM::LuaScriptsCfg(first), true,
            [](const ESM::LuaScriptCfg::Requirement&) { return std::string("no game pack"); });
        ASSERT_EQ(conf.size(), 1u);
        EXPECT_FALSE(conf.mapId(0).has_value()); // the refused script's saved data finds nothing
        ASSERT_TRUE(conf.mapId(1).has_value()); // the plain one moved from id 1 to id 0
        EXPECT_EQ(*conf.mapId(1), 0);
    }
}
