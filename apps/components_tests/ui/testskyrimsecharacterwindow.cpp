// Task 0225: the Skyrim SE character creation window (files/data/openeden/skyrimse/character_window.lua) over the headless
// UI backend and a fake `core.skyrimse.character` package written in Lua.
#include "uiluafixture.hpp"

#include <string>
#include <vector>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimCharacterLua : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.character_window')
                state = {
                  revision = 1, ready = true,
                  races = {
                    { name = 'Argonian', desc = 'Lizards.', skills = 'Lockpicking +10', abilities = 2 },
                    { name = 'Nord', desc = 'Hardy and tough.', skills = 'Two-Handed +10, Block +5', abilities = 1 },
                  },
                  race = 1, female = false, name = '',
                  parts = {
                    hair = { n = 3, i = 1, name = 'HairA' },
                    beard = { n = 2, i = 0, name = 'None' },
                    eyes = { n = 2, i = 0, name = 'EyesA' },
                  },
                }
                chr = {
                  ready = function() return state.ready end,
                  revision = function() return state.revision end,
                  race_count = function() return #state.races end,
                  race_name = function(i) return state.races[i + 1].name end,
                  race_desc = function(i) return state.races[i + 1].desc end,
                  race_skills = function(i) return state.races[i + 1].skills end,
                  race_abilities = function(i) return state.races[i + 1].abilities end,
                  race_index = function() return state.race end,
                  female = function() return state.female end,
                  name = function() return state.name end,
                  part_count = function(p) return state.parts[p].n end,
                  part_name = function(p) return state.parts[p].name end,
                  part_index = function(p) return state.parts[p].i end,
                }
                sent = {}
                closes = 0
                win = W.create({ ui = ui, chr = chr,
                                 send = function(t) sent[#sent + 1] = t end,
                                 close = function() closes = closes + 1 end })
            )"));
        }

        std::size_t documents(const std::string& name) const
        {
            std::size_t n = 0;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument && node.mName == name)
                    ++n;
            return n;
        }

        /// A visible element whose text is `text`.
        std::vector<ElementId> shown(const std::string& text) const
        {
            std::vector<ElementId> out;
            for (const auto& [id, node] : backend.nodes())
            {
                if (node.mText != text)
                    continue;
                const auto it = node.mProperties.find("display");
                if (it == node.mProperties.end() || it->second != "none")
                    out.push_back(id);
            }
            return out;
        }

        bool anyTextContains(const std::string& part) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText.find(part) != std::string::npos)
                    return true;
            return false;
        }

        bool clickClass(const std::string& cls)
        {
            const auto ids = withClass(cls);
            if (ids.size() != 1)
            {
                ADD_FAILURE() << "expected one element of class " << cls << ", found " << ids.size();
                return false;
            }
            if (!backend.emit(ids[0], EventKind::Click))
                return false;
            return run("ui.update()");
        }
    };

    TEST_F(SkyrimCharacterLua, theWindowOpensOnceWithTheCurrentRaceItsTextAndItsSkills)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_TRUE(get<bool>("win.isOpen()"));
        EXPECT_EQ(documents("skyrim-character"), 1u);
        EXPECT_EQ(shown("Nord").size(), 1u);
        EXPECT_TRUE(anyTextContains("Hardy and tough."));
        EXPECT_TRUE(anyTextContains("Two-Handed +10, Block +5"));
        EXPECT_TRUE(anyTextContains("Create"));
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_EQ(documents("skyrim-character"), 1u) << "a second open rebuilds, it does not add a window";
    }

    TEST_F(SkyrimCharacterLua, theRaceButtonsAskForTheNextAndThePreviousRaceAndWrapRound)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        ASSERT_TRUE(clickClass("chr-race-next"));
        ASSERT_EQ(get<int>("#sent"), 1);
        EXPECT_EQ(get<std::string>("sent[1].kind"), "race");
        EXPECT_EQ(get<int>("sent[1].index"), 0) << "after the Nord (the last) comes the first: wrapped";
        ASSERT_TRUE(clickClass("chr-race-prev"));
        EXPECT_EQ(get<int>("sent[2].index"), 0) << "before the Nord comes the Argonian";
    }

    TEST_F(SkyrimCharacterLua, theSexButtonsAskForTheSex)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        ASSERT_TRUE(clickClass("chr-female"));
        ASSERT_EQ(get<int>("#sent"), 1);
        EXPECT_EQ(get<std::string>("sent[1].kind"), "female");
        EXPECT_TRUE(get<bool>("sent[1].value"));
        ASSERT_TRUE(clickClass("chr-male"));
        EXPECT_FALSE(get<bool>("sent[2].value"));
    }

    TEST_F(SkyrimCharacterLua, eachPartHasPreviousAndNextButtonsThatSendAStep)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        for (const char* part : { "hair", "beard", "eyes" })
        {
            ASSERT_TRUE(clickClass(std::string("chr-") + part + "-next")) << part;
            ASSERT_TRUE(clickClass(std::string("chr-") + part + "-prev")) << part;
        }
        ASSERT_EQ(get<int>("#sent"), 6);
        EXPECT_EQ(get<std::string>("sent[1].kind"), "part");
        EXPECT_EQ(get<std::string>("sent[1].part"), "hair");
        EXPECT_EQ(get<int>("sent[1].delta"), 1);
        EXPECT_EQ(get<int>("sent[2].delta"), -1);
        EXPECT_EQ(get<std::string>("sent[3].part"), "beard");
        EXPECT_EQ(get<std::string>("sent[5].part"), "eyes");
    }

    TEST_F(SkyrimCharacterLua, aPartRowShowsItsCurrentNameAndPlaceInTheList)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_TRUE(anyTextContains("HairA"));
        EXPECT_TRUE(anyTextContains("2 of 3")) << "index 1 of 3 is the second";
        EXPECT_TRUE(anyTextContains("None")) << "the beard";
    }

    TEST_F(SkyrimCharacterLua, theWindowFollowsTheRevisionOfThePack)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        ASSERT_TRUE(run(R"(
            state.race = 0; state.revision = 2; state.female = true
            state.parts.hair = { n = 4, i = 2, name = 'HairZ' }
            win.update(); ui.update()
        )"));
        EXPECT_EQ(shown("Argonian").size(), 1u);
        EXPECT_TRUE(shown("Nord").empty());
        EXPECT_TRUE(anyTextContains("Lizards."));
        EXPECT_TRUE(anyTextContains("HairZ"));
        EXPECT_TRUE(anyTextContains("3 of 4"));
    }

    TEST_F(SkyrimCharacterLua, anIdleFrameReadsNothingElse)
    {
        ASSERT_TRUE(run(R"(
            win.open(); ui.update()
            reads = 0
            for _, k in ipairs({ 'race_count', 'race_name', 'race_desc', 'race_skills', 'part_name', 'part_count' }) do
              local f = chr[k]
              chr[k] = function(...) reads = reads + 1; return f(...) end
            end
            for i = 1, 20 do win.update(); ui.update() end
        )"));
        EXPECT_EQ(get<int>("reads"), 0) << "no change of revision: no re-read";
    }

    TEST_F(SkyrimCharacterLua, typingANameSendsItAndDoneSendsTheNameThenFinishAndLeaves)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const auto inputs = withClass("input");
        ASSERT_EQ(inputs.size(), 1u);
        ASSERT_TRUE(backend.emit(inputs[0], EventKind::Change, "Ria"));
        ASSERT_TRUE(run("ui.update()"));
        ASSERT_GE(get<int>("#sent"), 1);
        EXPECT_EQ(get<std::string>("sent[#sent].kind"), "name");
        EXPECT_EQ(get<std::string>("sent[#sent].text"), "Ria");
        ASSERT_TRUE(run("sent = {}"));
        ASSERT_TRUE(clickClass("chr-done"));
        ASSERT_EQ(get<int>("#sent"), 2);
        EXPECT_EQ(get<std::string>("sent[1].kind"), "name");
        EXPECT_EQ(get<std::string>("sent[1].text"), "Ria");
        EXPECT_EQ(get<std::string>("sent[2].kind"), "finish");
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimCharacterLua, doneTwiceFinishesOnce)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        ASSERT_TRUE(clickClass("chr-done"));
        ASSERT_TRUE(run("win.update(); ui.update()"));
        const auto done = withClass("chr-done");
        for (const ElementId id : done)
            (void)backend.emit(id, EventKind::Click);
        ASSERT_TRUE(run("ui.update()"));
        int finishes = 0;
        ASSERT_TRUE(run("n = 0; for _, t in ipairs(sent) do if t.kind == 'finish' then n = n + 1 end end"));
        finishes = get<int>("n");
        EXPECT_EQ(finishes, 1);
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimCharacterLua, dismissTakesTheWindowDownAndSafeWhenNothingIsOpen)
    {
        ASSERT_TRUE(run("win.dismiss()"));
        ASSERT_TRUE(run("win.open(); ui.update(); win.dismiss(); ui.update()"));
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        EXPECT_EQ(documents("skyrim-character"), 0u);
    }
}
