// Task 0189: the Skyrim SE barter window of files/data/openeden/skyrimse/barter_windows.lua over the headless UI
// backend and fakes of the `core.skyrimse.inventory` and `core.skyrimse.barter` packages written in Lua. Written before
// the module and run red with the module absent first.
#include "uiluafixture.hpp"

#include <algorithm>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimBarterWindows : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.barter_windows')
                GOLD = '0000000f'
                store = {
                  player = { [GOLD] = 100, ['00000b02'] = 2, ['00000a01'] = 3 },
                  ['700'] = { [GOLD] = 50, ['00000c03'] = 4, ['00000b02'] = 1 },
                }
                info = { [GOLD] = { 'Gold', 'misc' }, ['00000a01'] = { 'Pebble', 'misc' },
                         ['00000b02'] = { 'Sword', 'weapon' }, ['00000c03'] = { 'Bread', 'potion' } }
                buyPrice = { ['00000c03'] = 8, ['00000b02'] = 39 }
                sellPrice = { ['00000b02'] = 8, ['00000c03'] = 2 } -- the vendor does not buy the pebble
                revision, trades, state, reason, last = 1, 0, 'ready', '', ''
                buys, sells, closes = {}, {}, 0
                inv = {
                  revision = function() return revision end,
                  items = function(o)
                    local ids = {}
                    for id in pairs(store[o] or {}) do ids[#ids + 1] = id end
                    table.sort(ids, function(a, b) return info[a][1] < info[b][1] end)
                    return ids
                  end,
                  count = function(o, id) return (store[o] or {})[id] or 0 end,
                  name = function(id) return info[id][1] end,
                  gold_item = function() return GOLD end,
                }
                barter = {
                  state = function() return state end,
                  reason = function() return reason end,
                  owner = function() return state == 'ready' and '700' or '' end,
                  vendor_gold = function() return store['700'][GOLD] or 0 end,
                  buy_price = function(id) return buyPrice[id] or 0 end,
                  sell_price = function(id) return sellPrice[id] or 0 end,
                  sellable = function(id) return (store.player[id] or 0) end,
                  last_result = function() return last end,
                  trades = function() return trades end,
                }
                function move(from, to, id, n, price, buying)
                  store[from][id] = store[from][id] - n
                  if store[from][id] == 0 then store[from][id] = nil end
                  store[to][id] = (store[to][id] or 0) + n
                  local g = price * n
                  local payer, payee = buying and 'player' or '700', buying and '700' or 'player'
                  store[payer][GOLD] = store[payer][GOLD] - g
                  store[payee][GOLD] = (store[payee][GOLD] or 0) + g
                  revision = revision + 1; trades = trades + 1; last = 'ok'
                end
                env = { ui = ui, inv = inv, barter = barter,
                  buy = function(id, n) buys[#buys + 1] = { id, n }; move('700', 'player', id, n, buyPrice[id], true) end,
                  sell = function(id, n) sells[#sells + 1] = { id, n }; move('player', '700', id, n, sellPrice[id], false) end,
                  close = function() closes = closes + 1 end }
                win = W.create(env)
            )"));
        }

        std::vector<ElementId> buttonsWith(const std::string& prefix) const
        {
            std::vector<ElementId> out;
            for (ElementId id : withClass("button"))
                if (backend.node(id)->mText.rfind(prefix, 0) == 0)
                    out.push_back(id);
            return out;
        }

        std::vector<std::string> documents() const
        {
            std::vector<std::string> names;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument)
                    names.push_back(node.mName);
            std::sort(names.begin(), names.end());
            return names;
        }

        bool textExists(const std::string& text) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText == text)
                    return true;
            return false;
        }

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }
    };

    TEST_F(SkyrimBarterWindows, openingBuildsTheTwoColumnsWithGoldAndTheMerchantsName)
    {
        ASSERT_TRUE(run("win.open('Hulda'); ui.update()"));
        EXPECT_EQ(documents(), (std::vector<std::string>{ "skyrim-barter-player", "skyrim-barter-vendor" }));
        EXPECT_TRUE(textExists("Your items"));
        EXPECT_TRUE(textExists("Hulda"));
        EXPECT_TRUE(textExists("Your gold 100"));
        EXPECT_TRUE(textExists("Merchant gold 50"));
        EXPECT_TRUE(get<bool>("win.isOpen()"));
    }

    TEST_F(SkyrimBarterWindows, theVendorColumnListsItsStockWithBuyPricesAndNoGold)
    {
        ASSERT_TRUE(run("win.open('Hulda'); ui.update()"));
        EXPECT_EQ(buttonsWith("Bread  x4").size(), 1u);
        EXPECT_TRUE(textExists("8g"));
        EXPECT_EQ(buttonsWith("Gold").size(), 0u); // gold is the merchant's purse, not stock
    }

    TEST_F(SkyrimBarterWindows, thePlayerColumnListsOnlyWhatTheMerchantWillBuyWithSellPrices)
    {
        ASSERT_TRUE(run("win.open('Hulda'); ui.update()"));
        EXPECT_EQ(buttonsWith("Pebble").size(), 0u);
        EXPECT_EQ(buttonsWith("Gold").size(), 0u);
        EXPECT_EQ(buttonsWith("Sword  x2").size(), 1u);
    }

    TEST_F(SkyrimBarterWindows, clickingAVendorRowBuysOneAndItsAllButtonBuysTheStack)
    {
        ASSERT_TRUE(run("store.player = { [GOLD] = 100 }; win.open('Hulda'); ui.update()")); // nothing to sell
        click(buttonsWith("Bread").front());
        ASSERT_EQ(get<int>("#buys"), 1);
        EXPECT_EQ(get<std::string>("buys[1][1]"), "00000c03");
        EXPECT_EQ(get<int>("buys[1][2]"), 1);
        ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_TRUE(textExists("Your gold 92"));
        EXPECT_TRUE(textExists("Merchant gold 58"));
        EXPECT_EQ(buttonsWith("Bread  x3").size(), 1u);
    }

    TEST_F(SkyrimBarterWindows, theAllButtonBuysTheWholeStack)
    {
        ASSERT_TRUE(
            run("store.player = { [GOLD] = 100 }; store['700']['00000b02'] = nil; win.open('Hulda'); ui.update()"));
        const auto all = buttonsWith("All");
        ASSERT_EQ(all.size(), 1u);
        click(all.front());
        ASSERT_EQ(get<int>("#buys"), 1);
        EXPECT_EQ(get<int>("buys[1][2]"), 4);
    }

    TEST_F(SkyrimBarterWindows, clickingAPlayerRowSellsOne)
    {
        ASSERT_TRUE(run("win.open('Hulda'); ui.update()"));
        click(buttonsWith("Sword  x2").front());
        ASSERT_EQ(get<int>("#sells"), 1);
        EXPECT_EQ(get<std::string>("sells[1][1]"), "00000b02");
        EXPECT_EQ(get<int>("sells[1][2]"), 1);
        ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_TRUE(textExists("Your gold 108"));
        EXPECT_TRUE(textExists("Merchant gold 42"));
    }

    TEST_F(SkyrimBarterWindows, theStatusLineShowsTheLastResult)
    {
        ASSERT_TRUE(run("win.open('Hulda'); ui.update()"));
        ASSERT_TRUE(run("last = 'not enough gold'; trades = trades + 1; win.update(); ui.update()"));
        EXPECT_TRUE(textExists("not enough gold"));
    }

    TEST_F(SkyrimBarterWindows, closeCallsTheHook)
    {
        ASSERT_TRUE(run("win.open('Hulda'); ui.update()"));
        click(buttonsWith("Close").front());
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimBarterWindows, aPendingSessionShowsOpeningAndFillsWhenReady)
    {
        ASSERT_TRUE(run("state = 'pending'; win.open('Hulda'); ui.update()"));
        EXPECT_TRUE(textExists("Opening..."));
        EXPECT_EQ(buttonsWith("Bread").size(), 0u);
        ASSERT_TRUE(run("state = 'ready'; win.update(); ui.update()"));
        EXPECT_FALSE(textExists("Opening..."));
        EXPECT_EQ(buttonsWith("Bread").size(), 1u);
    }

    TEST_F(SkyrimBarterWindows, aRefusedSessionShowsTheReasonAndOnlyClose)
    {
        ASSERT_TRUE(run("state = 'refused'; reason = 'not a vendor'; win.open('Brenuin'); ui.update()"));
        EXPECT_TRUE(textExists("not a vendor"));
        EXPECT_EQ(buttonsWith("Bread").size(), 0u);
        EXPECT_EQ(buttonsWith("Close").size(), 1u);
    }

    TEST_F(SkyrimBarterWindows, dismissTakesTheDocumentsDown)
    {
        ASSERT_TRUE(run("win.open('Hulda'); ui.update()"));
        ASSERT_TRUE(run("win.dismiss(); ui.update()"));
        EXPECT_TRUE(documents().empty());
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        ASSERT_TRUE(run("win.dismiss(); ui.update()")); // again: harmless
    }

    TEST_F(SkyrimBarterWindows, aFrameWithNothingChangedMakesNoProtocolCalls)
    {
        ASSERT_TRUE(run("win.open('Hulda'); ui.update()"));
        const std::size_t calls = backend.calls().size();
        for (int i = 0; i < 20; ++i)
            ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_EQ(backend.calls().size(), calls);
    }
}
