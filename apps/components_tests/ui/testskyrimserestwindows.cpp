// Task 0210: the Skyrim SE wait and sleep window (files/data/openeden/skyrimse/rest_windows.lua) over the headless UI
// backend and a fake `core.skyrimse.rest` package written in Lua.
#include "uiluafixture.hpp"

#include <string>
#include <vector>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimRestLua : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                R = require('openeden.skyrimse.rest_windows')
                state = { reason = '', rested = false, left = 0.0, maxh = 24 }
                sent = {}
                closes = 0
                rest = {
                  can_rest = function() return state.reason end,
                  max_hours = function() return state.maxh end,
                  rested = function() return state.rested end,
                  rested_hours_left = function() return state.left end,
                }
                win = R.create({ ui = ui, rest = rest,
                                 close = function() closes = closes + 1 end,
                                 send = function(t) sent[#sent + 1] = t end })
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

        /// A visible element whose text is `text` (display not none).
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

        bool click(const std::string& label)
        {
            for (const ElementId id : shown(label))
                if (backend.emit(id, EventKind::Click))
                    return run("ui.update()");
            ADD_FAILURE() << "no visible element with the text " << label;
            return false;
        }
    };

    TEST_F(SkyrimRestLua, aWaitWindowOpensWithOneHourAndAWaitButton)
    {
        ASSERT_TRUE(run("ok = win.open('wait')"));
        EXPECT_TRUE(get<bool>("ok"));
        EXPECT_EQ(documents("skyrim-rest"), 1u);
        EXPECT_EQ(get<std::string>("win.kind()"), "wait");
        EXPECT_EQ(get<int>("win.hours()"), 1);
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(shown("1 hour").size(), 1u);
        EXPECT_EQ(shown("Wait").size(), 2u); // the title and the button
        EXPECT_EQ(shown("Cancel").size(), 1u);
    }

    TEST_F(SkyrimRestLua, aSleepWindowStartsAtEightHours)
    {
        ASSERT_TRUE(run("ok = win.open('sleep')"));
        EXPECT_TRUE(get<bool>("ok"));
        EXPECT_EQ(get<int>("win.hours()"), 8);
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(shown("8 hours").size(), 1u);
        EXPECT_EQ(shown("Sleep").size(), 2u);
    }

    TEST_F(SkyrimRestLua, theHoursStayBetweenOneAndThePacksMaximum)
    {
        ASSERT_TRUE(run("state.maxh = 12; win.open('wait'); win.setHours(0)"));
        EXPECT_EQ(get<int>("win.hours()"), 1);
        ASSERT_TRUE(run("win.setHours(99)"));
        EXPECT_EQ(get<int>("win.hours()"), 12);
        ASSERT_TRUE(run("win.setHours(5.9)"));
        EXPECT_EQ(get<int>("win.hours()"), 5);
        ASSERT_TRUE(run("win.setHours('nonsense')"));
        EXPECT_EQ(get<int>("win.hours()"), 1);
    }

    TEST_F(SkyrimRestLua, thePlusAndMinusButtonsChangeTheHours)
    {
        ASSERT_TRUE(run("win.open('wait'); ui.update()"));
        ASSERT_TRUE(click("+"));
        ASSERT_TRUE(click("+"));
        EXPECT_EQ(get<int>("win.hours()"), 3);
        ASSERT_TRUE(click("-"));
        EXPECT_EQ(get<int>("win.hours()"), 2);
        EXPECT_EQ(shown("2 hours").size(), 1u);
    }

    TEST_F(SkyrimRestLua, theWaitButtonAsksTheGlobalScriptOnceAndLeaves)
    {
        ASSERT_TRUE(run("win.open('wait'); win.setHours(3); ui.update()"));
        EXPECT_EQ(get<int>("#sent"), 0);
        for (const ElementId id : withClass("button"))
            if (backend.node(id)->mText == "Wait")
                ASSERT_TRUE(backend.emit(id, EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        ASSERT_EQ(get<int>("#sent"), 1);
        EXPECT_EQ(get<int>("sent[1].hours"), 3);
        EXPECT_FALSE(get<bool>("sent[1].sleeping"));
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimRestLua, theSleepButtonSendsSleepingTrue)
    {
        ASSERT_TRUE(run("win.open('sleep'); ui.update()"));
        for (const ElementId id : withClass("button"))
            if (backend.node(id)->mText == "Sleep")
                ASSERT_TRUE(backend.emit(id, EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        ASSERT_EQ(get<int>("#sent"), 1);
        EXPECT_EQ(get<int>("sent[1].hours"), 8);
        EXPECT_TRUE(get<bool>("sent[1].sleeping"));
    }

    TEST_F(SkyrimRestLua, aRefusalOfThePackShowsItsMessageAndNoControls)
    {
        ASSERT_TRUE(run("state.reason = 'combat'; ok = win.open('wait'); ui.update()"));
        EXPECT_FALSE(get<bool>("ok"));
        EXPECT_EQ(get<std::string>("win.kind()"), "message");
        EXPECT_EQ(get<std::string>("win.message()"), "You cannot rest while you are in combat.");
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(shown("OK").size(), 1u);
        EXPECT_TRUE(shown("1 hour").empty());
        EXPECT_FALSE(get<bool>("win.confirm()"));
        EXPECT_EQ(get<int>("#sent"), 0);
    }

    TEST_F(SkyrimRestLua, eachRefusalHasItsOwnWords)
    {
        EXPECT_EQ(get<std::string>("R.refusalText('enemies')"), "You cannot rest with enemies nearby.");
        EXPECT_EQ(get<std::string>("R.refusalText('not_ready')"), "You cannot rest yet.");
        EXPECT_EQ(get<std::string>("R.refusalText('zzz')"), "You cannot rest (zzz).");
    }

    TEST_F(SkyrimRestLua, aSituationThatChangedWhileTheWindowWasOpenRefusesTheConfirm)
    {
        ASSERT_TRUE(run("win.open('wait')"));
        ASSERT_TRUE(run("state.reason = 'enemies'; ok = win.confirm()"));
        EXPECT_FALSE(get<bool>("ok"));
        EXPECT_EQ(get<int>("#sent"), 0);
        EXPECT_EQ(get<int>("closes"), 0);
        EXPECT_EQ(get<std::string>("win.message()"), "You cannot rest with enemies nearby.");
    }

    TEST_F(SkyrimRestLua, aNoticeIsAMessageWithOneOkButton)
    {
        ASSERT_TRUE(run("win.notice('This bed is not yours.'); ui.update()"));
        EXPECT_EQ(get<std::string>("win.kind()"), "message");
        EXPECT_EQ(shown("This bed is not yours.").size(), 1u);
        EXPECT_EQ(shown("OK").size(), 1u);
        EXPECT_FALSE(get<bool>("win.confirm()"));
        ASSERT_TRUE(click("OK"));
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimRestLua, theRestedStateShowsItsHoursLeft)
    {
        ASSERT_TRUE(run("state.rested = true; state.left = 6.5; win.open('wait'); ui.update()"));
        EXPECT_EQ(shown("Rested: 6.5 hours left").size(), 1u);
        ASSERT_TRUE(run("state.left = 5.0; win.update(); ui.update()"));
        EXPECT_EQ(shown("Rested: 5.0 hours left").size(), 1u);
        ASSERT_TRUE(run("state.rested = false; win.update(); ui.update()"));
        EXPECT_TRUE(shown("Rested: 5.0 hours left").empty());
    }

    TEST_F(SkyrimRestLua, aSecondOpenReplacesTheFirstAndDismissTakesTheWindowDown)
    {
        ASSERT_TRUE(run("win.open('wait'); win.open('sleep')"));
        EXPECT_EQ(documents("skyrim-rest"), 1u);
        ASSERT_TRUE(run("win.dismiss()"));
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        EXPECT_EQ(documents("skyrim-rest"), 0u);
        ASSERT_TRUE(run("win.update(); win.dismiss()")); // both safe when nothing is open
    }
}
