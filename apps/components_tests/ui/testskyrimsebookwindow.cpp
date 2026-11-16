// Task 0203: the book window (files/data/openeden/skyrimse/book_window.lua) and the Read button the inventory window
// gets for books (inventory_windows.lua, env.read), over the headless UI backend and fake Lua packages. Written AFTER
// the modules; a one-line mutation check is in the task's stage log.
#include "uiluafixture.hpp"

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimBookWindow : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.book_window')
                books = {
                  ['00000a01'] = { title = 'A Plain Book', kind = 'book', teaches = '', pages = { 'Page one', 'Page two', 'Page three' } },
                  ['00000a02'] = { title = 'Twin Secrets', kind = 'skill', teaches = 'Enchanting', pages = { 'Secrets' } },
                  ['00000a03'] = { title = 'Tome of Thunderbolt', kind = 'spell', teaches = 'Thunderbolt', pages = { 'Thud' } },
                  ['00000a04'] = { title = 'Empty', kind = 'book', teaches = '', pages = {} },
                }
                results = {}
                lb = {
                  book_known = function(id) return books[id] ~= nil end,
                  book_title = function(id) return books[id].title end,
                  book_kind = function(id) return books[id].kind end,
                  book_teaches = function(id) return books[id].teaches end,
                  book_pages = function(id) return books[id].pages end,
                  book_result = function(id) return results[id] or '' end,
                }
                closes = 0
                win = W.create{ ui = ui, lb = lb, close = function() closes = closes + 1 end }
            )"));
        }

        bool hasText(const std::string& text) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText == text)
                    return true;
            return false;
        }

        std::vector<ElementId> buttonsWith(const std::string& text) const
        {
            std::vector<ElementId> out;
            for (ElementId id : withClass("button"))
                if (backend.node(id)->mText == text)
                    out.push_back(id);
            return out;
        }

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }
    };

    TEST_F(SkyrimBookWindow, aPlainBookShowsItsTitleAndFirstPage)
    {
        ASSERT_TRUE(run("win.open('00000a01'); ui.update()"));
        EXPECT_TRUE(get<bool>("win.isOpen()"));
        EXPECT_TRUE(hasText("A Plain Book"));
        EXPECT_TRUE(hasText("Page one"));
        EXPECT_TRUE(hasText("Page 1 of 3"));
        EXPECT_EQ(get<int>("win.pageCount()"), 3);
    }

    TEST_F(SkyrimBookWindow, nextAndPreviousTurnThePagesAndStopAtTheEnds)
    {
        ASSERT_TRUE(run("win.open('00000a01'); ui.update()"));
        const auto next = buttonsWith("Next");
        const auto previous = buttonsWith("Previous");
        ASSERT_EQ(next.size(), 1u);
        ASSERT_EQ(previous.size(), 1u);
        click(previous.front());
        EXPECT_EQ(get<int>("win.page()"), 1);
        click(next.front());
        EXPECT_TRUE(hasText("Page two"));
        EXPECT_TRUE(hasText("Page 2 of 3"));
        click(next.front());
        click(next.front());
        EXPECT_EQ(get<int>("win.page()"), 3);
        EXPECT_TRUE(hasText("Page three"));
        click(previous.front());
        EXPECT_TRUE(hasText("Page two"));
    }

    TEST_F(SkyrimBookWindow, aSkillBookAndASpellTomeSayWhatTheyTeach)
    {
        ASSERT_TRUE(run("win.open('00000a02'); ui.update()"));
        EXPECT_TRUE(hasText("Teaches Enchanting"));
        ASSERT_TRUE(run("win.open('00000a03'); ui.update()"));
        EXPECT_TRUE(hasText("Spell tome: Thunderbolt"));
        EXPECT_FALSE(hasText("Teaches Enchanting"));
        ASSERT_TRUE(run("win.open('00000a01'); ui.update()"));
        EXPECT_FALSE(hasText("Spell tome: Thunderbolt"));
    }

    TEST_F(SkyrimBookWindow, theEffectLineFollowsThePackEachFrame)
    {
        ASSERT_TRUE(run("win.open('00000a02'); ui.update()"));
        EXPECT_FALSE(hasText("Enchanting skill increased by one level."));
        ASSERT_TRUE(run("results['00000a02'] = 'Enchanting skill increased by one level.'; win.update(); ui.update()"));
        EXPECT_TRUE(hasText("Enchanting skill increased by one level."));
    }

    TEST_F(SkyrimBookWindow, aBookWithNoTextOrAnUnknownItemSaysThereIsNothingToRead)
    {
        ASSERT_TRUE(run("win.open('00000a04'); ui.update()"));
        EXPECT_TRUE(hasText("(There is nothing to read.)"));
        EXPECT_EQ(get<int>("win.pageCount()"), 0);
        ASSERT_TRUE(run("win.turn(1); win.open('00000b00'); ui.update()"));
        EXPECT_TRUE(hasText("(There is nothing to read.)"));
    }

    TEST_F(SkyrimBookWindow, closeTakesTheWindowDownAndTellsTheHost)
    {
        ASSERT_TRUE(run("win.open('00000a01'); ui.update()"));
        const auto close = buttonsWith("Close");
        ASSERT_EQ(close.size(), 1u);
        click(close.front());
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        EXPECT_EQ(get<int>("closes"), 1);
        EXPECT_TRUE(withClass("panel").empty());
        ASSERT_TRUE(run("win.update(); win.dismiss()")); // safe when closed
    }

    TEST_F(SkyrimBookWindow, openingAnotherBookReplacesTheWindow)
    {
        ASSERT_TRUE(run("win.open('00000a01'); ui.update(); win.open('00000a02'); ui.update()"));
        EXPECT_EQ(withClass("panel").size(), 1u);
        EXPECT_TRUE(hasText("Twin Secrets"));
        EXPECT_FALSE(hasText("A Plain Book"));
        EXPECT_EQ(get<int>("win.page()"), 1);
    }

    // ---- the Read button of the inventory window

    struct SkyrimReadButton : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.inventory_windows')
                store = { player = { ['0000000f'] = 100, ['00000a01'] = 1, ['00000b02'] = 1 } }
                info = { ['0000000f'] = { 'Gold', 'misc', 0, 1 }, ['00000a01'] = { 'A Plain Book', 'book', 1, 25 },
                         ['00000b02'] = { 'Pebble', 'misc', 0.5, 2 } }
                reads = {}
                inv = {
                  revision = function() return 1 end,
                  known = function(o) return store[o] ~= nil end,
                  items = function(o)
                    local ids = {}
                    for id in pairs(store[o] or {}) do ids[#ids + 1] = id end
                    table.sort(ids)
                    return ids
                  end,
                  count = function(o, id) return (store[o] or {})[id] or 0 end,
                  name = function(id) return info[id][1] end,
                  category = function(id) return info[id][2] end,
                  weight = function(id) return info[id][3] end,
                  value = function(id) return info[id][4] end,
                  gold_item = function() return '0000000f' end,
                  carried = function() return 1 end,
                  capacity = function() return 300 end,
                  equipped = function() return false end,
                  use_kind = function() return '' end,
                }
                env = { ui = ui, inv = inv, transfer = function() end, takeAll = function() end, close = function() end,
                  read = function(id) reads[#reads + 1] = id end }
                win = W.create(env)
            )"));
        }

        std::vector<ElementId> buttonsWith(const std::string& text) const
        {
            std::vector<ElementId> out;
            for (ElementId id : withClass("button"))
                if (backend.node(id)->mText == text)
                    out.push_back(id);
            return out;
        }
    };

    TEST_F(SkyrimReadButton, aBookRowOfThePlayersListGetsAReadButtonThatAsksToRead)
    {
        ASSERT_TRUE(run("win.open(nil, ''); ui.update()"));
        const auto read = buttonsWith("Read");
        ASSERT_EQ(read.size(), 1u); // the book only, not the gold or the pebble
        ASSERT_TRUE(backend.emit(read.front(), EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        ASSERT_EQ(get<int>("#reads"), 1);
        EXPECT_EQ(get<std::string>("reads[1]"), "00000a01");
    }

    TEST_F(SkyrimReadButton, withoutTheReadFunctionABookHasNoButton)
    {
        ASSERT_TRUE(run("env.read = nil; win = W.create(env); win.open(nil, ''); ui.update()"));
        EXPECT_EQ(buttonsWith("Read").size(), 0u);
    }
}
