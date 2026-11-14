// Task 0162: the virtualised List of `openeden.ui`: only the rows in view (plus an overscan) exist as elements. Written
// from the spec (docs/tasks/0162-ui-element-protocol-and-lua-library.md) and run red against a stub library first.
#include "uiluafixture.hpp"

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct UiList : UiLuaFixture
    {
        std::size_t countCalls(const std::string& prefix) const
        {
            std::size_t n = 0;
            for (const std::string& line : backend.calls())
                if (line.rfind(prefix, 0) == 0)
                    ++n;
            return n;
        }

        void makeBigList(int count = 10000)
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                items = {}
                for i = 1, )"
                + std::to_string(count) + R"( do items[i] = { id = i, name = 'item ' .. i } end
                renders = 0
                doc = ui.Document{ name = 'inv', build = function()
                    return ui.List{ items = items, key = function(it) return it.id end, rowHeight = 20, height = 100,
                        render = function(it) renders = renders + 1; return ui.Text{ text = it.name } end }
                end }
                ui.update()
            )"));
        }

        ElementId listElement() const { return withClass("list").front(); }
        ElementId bodyElement() const { return withClass("list-body").front(); }
        std::vector<std::string> rowTexts() const
        {
            std::vector<std::string> texts;
            for (ElementId row : withClass("list-row"))
                for (ElementId child : backend.node(row)->mChildren)
                    texts.push_back(backend.node(child)->mText);
            std::sort(texts.begin(), texts.end());
            return texts;
        }
    };

    TEST_F(UiList, OnlyTheVisibleRowsAndTheOverscanExist)
    {
        makeBigList();
        EXPECT_EQ(withClass("list-row").size(), 7u); // rows 0..4 in view, 2 overscan below
        EXPECT_EQ(get<int>("renders"), 7);
        EXPECT_LT(backend.liveElements(), 30u);
    }

    TEST_F(UiList, TheListIsScrollableAndItsBodyHasTheFullHeight)
    {
        makeBigList();
        const auto* list = backend.node(listElement());
        EXPECT_EQ(list->mProperties.at("height"), "100dp");
        EXPECT_EQ(list->mProperties.at("overflow-y"), "auto");
        EXPECT_TRUE(list->mListens.count(EventKind::Scroll));
        EXPECT_EQ(backend.node(bodyElement())->mProperties.at("height"), "200000dp");
        EXPECT_EQ(backend.node(bodyElement())->mProperties.at("position"), "relative");
    }

    TEST_F(UiList, RowsAreAbsolutelyPositionedByIndex)
    {
        makeBigList();
        for (ElementId row : withClass("list-row"))
        {
            const auto& props = backend.node(row)->mProperties;
            EXPECT_EQ(props.at("position"), "absolute");
            EXPECT_EQ(props.at("height"), "20dp");
        }
        std::set<std::string> tops;
        for (ElementId row : withClass("list-row"))
            tops.insert(backend.node(row)->mProperties.at("top"));
        EXPECT_EQ(tops, (std::set<std::string>{ "0dp", "20dp", "40dp", "60dp", "80dp", "100dp", "120dp" }));
    }

    TEST_F(UiList, ScrollingByOneRowCreatesOneRowAndDestroysNone)
    {
        makeBigList();
        backend.clearCalls();
        backend.emit(listElement(), EventKind::Scroll, "20");
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(countCalls("create "), 2u); // the row and its Text
        EXPECT_EQ(countCalls("destroy "), 0u);
        EXPECT_EQ(withClass("list-row").size(), 8u);
    }

    TEST_F(UiList, ScrollingFiveRowsRecyclesTheRowsThatLeft)
    {
        makeBigList();
        backend.clearCalls();
        backend.emit(listElement(), EventKind::Scroll, "100");
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(withClass("list-row").size(), 9u); // rows 3..11
        EXPECT_EQ(countCalls("destroy "), 3u); // rows 0..2 left
        EXPECT_EQ(get<int>("renders"), 7 + 9 - 4); // 4 rows entered... 8..11
    }

    TEST_F(UiList, AFarScrollShowsTheRowsThere)
    {
        makeBigList();
        backend.emit(listElement(), EventKind::Scroll, "4000");
        ASSERT_TRUE(run("ui.update()"));
        const auto texts = rowTexts();
        ASSERT_EQ(texts.size(), 9u);
        EXPECT_NE(std::find(texts.begin(), texts.end(), "item 201"), texts.end()); // index 200 is at 4000dp
        EXPECT_EQ(std::find(texts.begin(), texts.end(), "item 1"), texts.end());
        EXPECT_LT(backend.liveElements(), 40u);
    }

    TEST_F(UiList, ScrollingBackAndForthNeverGrowsTheElementCount)
    {
        makeBigList();
        std::size_t peak = 0;
        for (int step = 0; step < 300; ++step)
        {
            backend.emit(listElement(), EventKind::Scroll, std::to_string((step * 37) % 1500));
            ASSERT_TRUE(run("ui.update()"));
            peak = std::max(peak, backend.liveElements());
        }
        EXPECT_LT(peak, 40u);
    }

    TEST_F(UiList, ScrollingToTheEndClampsToTheLastRows)
    {
        makeBigList();
        backend.emit(listElement(), EventKind::Scroll, "999999");
        ASSERT_TRUE(run("ui.update()"));
        const auto texts = rowTexts();
        EXPECT_NE(std::find(texts.begin(), texts.end(), "item 10000"), texts.end());
        EXPECT_LE(withClass("list-row").size(), 9u);
    }

    TEST_F(UiList, ABadScrollValueIsIgnored)
    {
        makeBigList();
        backend.clearCalls();
        backend.emit(listElement(), EventKind::Scroll, "not a number");
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(backend.callCount(), 0u);
    }

    TEST_F(UiList, ReversingASmallListMovesRowsWithoutCreatingOrDestroying)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            local function make() local t = {} for i = 1, 5 do t[i] = { id = i, name = 'n' .. i } end return t end
            items = ui.Value(make())
            doc = ui.Document{ name = 'd', build = function()
                return ui.List{ items = items, key = function(it) return it.id end, rowHeight = 10, height = 200,
                    render = function(it) return ui.Text{ text = it.name } end }
            end }
            ui.update()
        )"));
        ASSERT_EQ(withClass("list-row").size(), 5u);
        backend.clearCalls();
        ASSERT_TRUE(run(R"(
            local t = items:peek()
            local r = {}
            for i = #t, 1, -1 do r[#r + 1] = t[i] end
            items:set(r)
            ui.update()
        )"));
        EXPECT_EQ(countCalls("create "), 0u);
        EXPECT_EQ(countCalls("destroy "), 0u);
        EXPECT_EQ(countCalls("setProperty "), 4u); // the middle row keeps its place
    }

    TEST_F(UiList, RemovingAnItemDestroysItsRowAndMovesTheRest)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            local t = {} for i = 1, 4 do t[i] = { id = i, name = 'n' .. i } end
            items = ui.Value(t)
            doc = ui.Document{ name = 'd', build = function()
                return ui.List{ items = items, key = function(it) return it.id end, rowHeight = 10, height = 200,
                    render = function(it) return ui.Text{ text = it.name } end }
            end }
            ui.update()
        )"));
        backend.clearCalls();
        ASSERT_TRUE(run(R"(
            local t = items:peek()
            items:set({ t[1], t[3], t[4] })
            ui.update()
        )"));
        EXPECT_EQ(countCalls("destroy "), 1u);
        EXPECT_EQ(countCalls("create "), 0u);
        EXPECT_EQ(rowTexts(), (std::vector<std::string>{ "n1", "n3", "n4" }));
        EXPECT_EQ(backend.node(bodyElement())->mProperties.at("height"), "30dp");
    }

    TEST_F(UiList, AppendingAnItemInViewCreatesOneRowAndGrowsTheBody)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            items = ui.Value({ { id = 1, name = 'a' } })
            doc = ui.Document{ name = 'd', build = function()
                return ui.List{ items = items, key = function(it) return it.id end, rowHeight = 10, height = 200,
                    render = function(it) return ui.Text{ text = it.name } end }
            end }
            ui.update()
        )"));
        backend.clearCalls();
        ASSERT_TRUE(run("items:set({ items:peek()[1], { id = 2, name = 'b' } }); ui.update()"));
        EXPECT_EQ(countCalls("create "), 2u); // row + text
        EXPECT_EQ(backend.node(bodyElement())->mProperties.at("height"), "20dp");
    }

    TEST_F(UiList, ARowsScopeEndsWhenItLeavesTheWindow)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            names = {}
            local t = {}
            for i = 1, 100 do t[i] = { id = i, name = ui.Value('r' .. i) } end
            all = t
            doc = ui.Document{ name = 'd', build = function()
                return ui.List{ items = t, key = function(it) return it.id end, rowHeight = 20, height = 40,
                    render = function(it) return ui.Text{ text = it.name } end }
            end }
            ui.update()
        )"));
        backend.emit(listElement(), EventKind::Scroll, "1000");
        ASSERT_TRUE(run("ui.update()"));
        backend.clearCalls();
        ASSERT_TRUE(run("all[1].name:set('changed'); ui.update()"));
        EXPECT_EQ(backend.callCount(), 0u) << "item 1's row is gone, so its text effect must be too";
        ASSERT_TRUE(run("all[51].name:set('visible'); ui.update()"));
        EXPECT_EQ(backend.callCount(), 1u);
    }

    TEST_F(UiList, AnEmptyListHasNoRowsAndNoHeight)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Document{ name = 'd', build = function()
                return ui.List{ items = {}, key = function(it) return it end, rowHeight = 10, height = 50,
                    render = function(it) return ui.Text{ text = 'x' } end }
            end }
            ui.update()
        )"));
        EXPECT_EQ(withClass("list-row").size(), 0u);
        EXPECT_EQ(backend.node(bodyElement())->mProperties.at("height"), "0dp");
    }

    TEST_F(UiList, TheListNeedsItsFixedSizes)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            local ok, err = pcall(ui.Document, { name = 'd', build = function()
                return ui.List{ items = {}, key = function(it) return it end, render = function() end }
            end })
            assert(not ok and tostring(err):find('rowHeight'), tostring(err))
        )"));
    }

    TEST_F(UiList, DestroyingTheDocumentEndsTheRowsAndListener)
    {
        makeBigList();
        backend.clearCalls();
        ASSERT_TRUE(run("doc:destroy(); ui.update()"));
        EXPECT_EQ(backend.calls().size(), 1u);
        EXPECT_EQ(backend.liveElements(), 0u);
    }
}
