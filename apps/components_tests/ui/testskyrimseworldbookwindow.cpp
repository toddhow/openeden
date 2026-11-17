// Task 0234: the Take button of the book window for a book lying in the world (files/data/openeden/skyrimse/
// book_window.lua, env.take and open(item, placement, takeable)) and the Use button the inventory window gives a scroll
// (inventory_windows.lua), over the headless UI backend and fake Lua packages. Written before the window change and run
// red against the windows of task 0203 first (see the task's stage log).
#include "uiluafixture.hpp"

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimWorldBookWindow : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.book_window')
                lb = {
                  book_known = function(id) return id == '00000a01' end,
                  book_title = function(id) return 'A Note' end,
                  book_kind = function(id) return 'book' end,
                  book_teaches = function(id) return '' end,
                  book_pages = function(id) return { 'Meet me at dawn.' } end,
                  book_result = function(id) return '' end,
                }
                takes = {}
                closes = 0
                env = { ui = ui, lb = lb, close = function() closes = closes + 1 end,
                        take = function(placement, item) takes[#takes + 1] = placement .. ' ' .. item end }
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

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }
    };

    TEST_F(SkyrimWorldBookWindow, aBookLyingInTheWorldThatCanBeTakenShowsATakeButton)
    {
        ASSERT_TRUE(run("win.open('00000a01', '0000a001', true); ui.update()"));
        EXPECT_EQ(buttonsWith("Take").size(), 1u);
        EXPECT_EQ(buttonsWith("Close").size(), 1u);
        EXPECT_EQ(buttonsWith("Next").size(), 1u);
        EXPECT_EQ(buttonsWith("Previous").size(), 1u);
    }

    TEST_F(SkyrimWorldBookWindow, clickingTakeAsksThePackForThatPlacementAndTheBookAndClosesTheWindow)
    {
        ASSERT_TRUE(run("win.open('00000a01', '0000a001', true); ui.update()"));
        click(buttonsWith("Take").front());
        ASSERT_EQ(get<int>("#takes"), 1);
        EXPECT_EQ(get<std::string>("takes[1]"), "0000a001 00000a01");
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimWorldBookWindow, aBookThatCannotBeTakenHasNoTakeButton)
    {
        ASSERT_TRUE(run("win.open('00000a01', '0000a001', false); ui.update()"));
        EXPECT_EQ(buttonsWith("Take").size(), 0u);
        EXPECT_EQ(buttonsWith("Close").size(), 1u);
        ASSERT_TRUE(run("win.take()"));
        EXPECT_EQ(get<int>("#takes"), 0) << "even a direct call does not take what cannot be taken";
    }

    TEST_F(SkyrimWorldBookWindow, aBookOpenedFromTheInventoryHasNoTakeButton)
    {
        ASSERT_TRUE(run("win.open('00000a01'); ui.update()"));
        EXPECT_EQ(buttonsWith("Take").size(), 0u);
        EXPECT_EQ(buttonsWith("Close").size(), 1u);
    }

    TEST_F(SkyrimWorldBookWindow, withNoTakeHookThereIsNoTakeButtonEither)
    {
        ASSERT_TRUE(
            run("env2 = { ui = ui, lb = lb }; win2 = W.create(env2); win2.open('00000a01', '0000a001', true); "
                "ui.update()"));
        EXPECT_EQ(buttonsWith("Take").size(), 0u);
    }

    TEST_F(SkyrimWorldBookWindow, theNextBookOpenedFromTheInventoryForgetsThePlacementOfTheLast)
    {
        ASSERT_TRUE(run("win.open('00000a01', '0000a001', true); ui.update()"));
        ASSERT_TRUE(run("win.open('00000a01'); ui.update()"));
        EXPECT_EQ(buttonsWith("Take").size(), 0u);
        ASSERT_TRUE(run("win.take()"));
        EXPECT_EQ(get<int>("#takes"), 0);
    }

    TEST_F(SkyrimWorldBookWindow, closingTheWindowTakesNothing)
    {
        ASSERT_TRUE(run("win.open('00000a01', '0000a001', true); ui.update()"));
        click(buttonsWith("Close").front());
        EXPECT_EQ(get<int>("#takes"), 0);
        EXPECT_EQ(get<int>("closes"), 1);
        EXPECT_FALSE(get<bool>("win.isOpen()"));
    }

    // ---- the scroll row of the inventory window --------------------------------------------------------------------

    struct SkyrimScrollRow : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.inventory_windows')
                store = { player = { ['0000000f'] = 100, ['00000e05'] = 2 } }
                info = { ['0000000f'] = { 'Gold', 'misc', 0, 1, '' }, ['00000e05'] = { 'Firebolt Scroll', 'scroll', 0.5, 30, 'scroll' } }
                calls = {}
                inv = {
                  revision = function() return 1 end,
                  known = function(o) return store[o] ~= nil end,
                  items = function(o)
                    local ids = {}
                    for id in pairs(store[o] or {}) do ids[#ids + 1] = id end
                    table.sort(ids, function(a, b) return a < b end)
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
                  equipped = function(id) return false end,
                  use_kind = function(id) return info[id][5] end,
                }
                env = { ui = ui, inv = inv, transfer = function() end, takeAll = function() end, close = function() end,
                        use = function(id) calls[#calls + 1] = 'use ' .. id end }
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

    TEST_F(SkyrimScrollRow, aScrollOfTheInventoryGetsAUseButtonThatAsksThePackToUseIt)
    {
        ASSERT_TRUE(run("win.open(nil, ''); ui.update()"));
        ASSERT_EQ(buttonsWith("Use").size(), 1u);
        ASSERT_TRUE(backend.emit(buttonsWith("Use").front(), EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        ASSERT_EQ(get<int>("#calls"), 1);
        EXPECT_EQ(get<std::string>("calls[1]"), "use 00000e05");
        EXPECT_EQ(buttonsWith("Equip").size(), 0u);
        EXPECT_EQ(buttonsWith("Read").size(), 0u);
    }
}
