// Task 0185: the Skyrim SE quest journal window of files/data/openeden/skyrimse/journal_windows.lua over the headless
// UI backend and a fake `core.skyrimse.journal` package written in Lua. Written before the module and run red against a
// stub module first.
#include "uiluafixture.hpp"

#include <algorithm>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimJournalWindow : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.journal_windows')
                revision = 1
                data = {
                  running = { 'a0000001', 'a0000002' },
                  completed = { 'b0000001' },
                  q = {
                    a0000001 = { name = 'Unbound', status = 'running', entry = 'Find the gate.',
                                 log = { 'Escape.', 'Find the gate.' },
                                 obj = { 'Run to the keep', 'Hide' }, done = { true, false } },
                    a0000002 = { name = 'Bleak Falls', status = 'running', entry = 'Climb.', log = { 'Climb.' },
                                 obj = {}, done = {} },
                    b0000001 = { name = 'Old Deed', status = 'completed', entry = 'It is over.',
                                 log = { 'It is over.' }, obj = {}, done = {} },
                  },
                }
                jr = {
                  revision = function() return revision end,
                  quests = function(completed) return completed and data.completed or data.running end,
                  name = function(id) return data.q[id] and data.q[id].name or '' end,
                  entry = function(id) return data.q[id] and data.q[id].entry or '' end,
                  status = function(id) return data.q[id] and data.q[id].status or '' end,
                  log = function(id) return data.q[id] and data.q[id].log or {} end,
                  objectives = function(id) return data.q[id] and data.q[id].obj or {} end,
                  objective_done = function(id) return data.q[id] and data.q[id].done or {} end,
                }
                closes = 0
                win = W.create({ ui = ui, jr = jr, close = function() closes = closes + 1 end })
            )"));
        }

        std::vector<ElementId> buttonsWith(const std::string& prefix) const
        {
            std::vector<ElementId> out;
            for (ElementId id : withClass("button"))
                if (backend.node(id)->mText.find(prefix) != std::string::npos)
                    out.push_back(id);
            return out;
        }

        bool textExists(const std::string& text) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText == text)
                    return true;
            return false;
        }

        std::size_t documents() const
        {
            std::size_t n = 0;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument && node.mName == "skyrim-journal")
                    ++n;
            return n;
        }

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }
    };

    TEST_F(SkyrimJournalWindow, openingBuildsOneDocumentWithTheRunningQuestsAndTheFirstOneSelected)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_EQ(documents(), 1u);
        EXPECT_TRUE(get<bool>("win.isOpen()"));
        EXPECT_TRUE(textExists("Journal"));
        EXPECT_EQ(buttonsWith("Quests (2)").size(), 1u);
        EXPECT_EQ(buttonsWith("Completed (1)").size(), 1u);
        EXPECT_EQ(buttonsWith("> Unbound").size(), 1u);
        EXPECT_EQ(buttonsWith("Bleak Falls").size(), 1u);
        EXPECT_EQ(buttonsWith("Old Deed").size(), 0u);
        EXPECT_EQ(get<std::string>("win.selected()"), "a0000001");
        EXPECT_TRUE(textExists("Unbound"));
        EXPECT_TRUE(textExists("Find the gate."));
        EXPECT_TRUE(textExists("[x] Run to the keep"));
        EXPECT_TRUE(textExists("[ ] Hide"));
        EXPECT_TRUE(textExists("Escape."));
    }

    TEST_F(SkyrimJournalWindow, clickingAnotherQuestSelectsItAndShowsItsDetails)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const auto other = buttonsWith("Bleak Falls");
        ASSERT_EQ(other.size(), 1u);
        click(other.front());
        EXPECT_EQ(get<std::string>("win.selected()"), "a0000002");
        EXPECT_TRUE(textExists("Climb."));
        EXPECT_FALSE(textExists("Find the gate."));
        EXPECT_FALSE(textExists("[x] Run to the keep"));
        EXPECT_EQ(buttonsWith("> Bleak Falls").size(), 1u);
        EXPECT_EQ(buttonsWith("> Unbound").size(), 0u);
    }

    TEST_F(SkyrimJournalWindow, theCompletedTabListsTheCompletedQuestsSeparately)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        click(buttonsWith("Completed (1)").front());
        EXPECT_EQ(get<std::string>("win.tab()"), "completed");
        EXPECT_EQ(buttonsWith("> Old Deed").size(), 1u);
        EXPECT_EQ(buttonsWith("Unbound").size(), 0u);
        EXPECT_TRUE(textExists("It is over."));
        EXPECT_TRUE(textExists("completed"));
        click(buttonsWith("Quests (2)").front());
        EXPECT_EQ(get<std::string>("win.tab()"), "running");
        EXPECT_EQ(buttonsWith("> Unbound").size(), 1u);
    }

    TEST_F(SkyrimJournalWindow, anEmptyListSaysSo)
    {
        ASSERT_TRUE(run("data.completed = {}; revision = 2; win.open(); ui.update()"));
        click(buttonsWith("Completed (0)").front());
        EXPECT_TRUE(textExists("No completed quests."));
        EXPECT_EQ(get<std::string>("tostring(win.selected())"), "nil");
    }

    TEST_F(SkyrimJournalWindow, theWindowFollowsThePacksRevision)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        ASSERT_TRUE(run(R"(
            data.running = { 'a0000001', 'a0000002', 'a0000003' }
            data.q.a0000003 = { name = 'Dragon Rising', status = 'running', entry = 'Go.', log = { 'Go.' },
                                obj = {}, done = {} }
            data.q.a0000001.entry = 'Open the gate.'
            data.q.a0000001.log = { 'Escape.', 'Find the gate.', 'Open the gate.' }
            revision = 2
            win.update(); ui.update()
        )"));
        EXPECT_EQ(buttonsWith("Quests (3)").size(), 1u);
        EXPECT_EQ(buttonsWith("Dragon Rising").size(), 1u);
        EXPECT_TRUE(textExists("Open the gate."));
    }

    TEST_F(SkyrimJournalWindow, aSelectedQuestThatVanishesMovesTheSelectionToTheFirst)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        click(buttonsWith("Bleak Falls").front());
        ASSERT_TRUE(run("data.running = { 'a0000001' }; revision = 2; win.update(); ui.update()"));
        EXPECT_EQ(get<std::string>("win.selected()"), "a0000001");
    }

    TEST_F(SkyrimJournalWindow, aLongEntryAndALongEarlierLineStayOneNodeEachAndWrapOnTheirOwn)
    {
        // Task 0188 (toddhow's approval of 2026-11-15): the bitmap font wraps text now, so the window no longer breaks
        // lines by hand: one node per entry, whole.
        ASSERT_TRUE(run(R"(
            local long = 'I have awoken in the Temple of Dibella in Markarth and Sam and the staff he promised me have '
              .. 'disappeared, so now I must find them before the trail goes cold in the mountains.'
            data.q.a0000001.entry = long
            data.q.a0000001.log = { long .. ' Earlier', long }
            revision = 2
            win.open(); ui.update()
        )"));
        std::size_t entryNodes = 0, wholeNodes = 0;
        for (const auto& [id, node] : backend.nodes())
        {
            if (node.mClass.find("quest-entry") != std::string::npos && !node.mText.empty())
            {
                ++entryNodes;
                EXPECT_EQ(node.mText.rfind("I have awoken", 0), 0u);
                EXPECT_NE(node.mText.find("mountains."), std::string::npos);
                EXPECT_GT(node.mText.size(), 100u);
            }
            if (node.mClass.find("quest-line") != std::string::npos
                && node.mText.find("mountains. Earlier") != std::string::npos)
                ++wholeNodes;
        }
        EXPECT_EQ(entryNodes, 1u);
        EXPECT_EQ(wholeNodes, 1u);
    }

    TEST_F(SkyrimJournalWindow, aFrameWithNothingChangedMakesNoProtocolCalls)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const std::size_t calls = backend.calls().size();
        for (int i = 0; i < 20; ++i)
            ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_EQ(backend.calls().size(), calls);
    }

    TEST_F(SkyrimJournalWindow, closeCallsItsHookAndDismissTakesTheDocumentDown)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        click(buttonsWith("Close").front());
        EXPECT_EQ(get<int>("closes"), 1);
        ASSERT_TRUE(run("win.dismiss(); ui.update()"));
        EXPECT_EQ(documents(), 0u);
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        ASSERT_TRUE(run("win.dismiss(); ui.update()")); // again: harmless
    }
}
