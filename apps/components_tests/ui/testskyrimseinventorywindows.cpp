// Task 0182: the Skyrim SE inventory and container windows of files/data/openeden/skyrimse/inventory_windows.lua over
// the headless UI backend and a fake `core.skyrimse.inventory` package written in Lua. Written before the module and
// run red against a stub module first.
#include "uiluafixture.hpp"

#include <algorithm>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimInventoryWindows : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.inventory_windows')
                -- a fake of the pack's namespace: owners hold item -> count
                store = { player = { ['0000000f'] = 100 }, ['500'] = { ['00000a01'] = 5, ['00000b02'] = 1 } }
                info = { ['0000000f'] = { 'Gold', 'misc', 0, 1 }, ['00000a01'] = { 'Pebble', 'misc', 0.5, 25 },
                         ['00000b02'] = { 'Sword', 'weapon', 9, 25 } }
                revision = 1
                transfers, takeAlls, closes = {}, {}, 0
                inv = {
                  revision = function() return revision end,
                  known = function(o) return store[o] ~= nil end,
                  items = function(o)
                    local ids = {}
                    for id in pairs(store[o] or {}) do ids[#ids + 1] = id end
                    table.sort(ids, function(a, b)
                      local ia, ib = info[a], info[b]
                      if ia[2] ~= ib[2] then return ia[2] == 'weapon' end
                      return ia[1] < ib[1] end)
                    return ids
                  end,
                  count = function(o, id) return (store[o] or {})[id] or 0 end,
                  name = function(id) return info[id][1] end,
                  category = function(id) return info[id][2] end,
                  weight = function(id) return info[id][3] end,
                  value = function(id) return info[id][4] end,
                  gold_item = function() return '0000000f' end,
                  carried = function() return 3.5 end,
                  capacity = function() return 300 end,
                }
                function move(from, to, id, n)
                  store[from][id] = store[from][id] - n
                  if store[from][id] == 0 then store[from][id] = nil end
                  store[to][id] = (store[to][id] or 0) + n
                  revision = revision + 1
                end
                env = { ui = ui, inv = inv,
                  transfer = function(from, to, id, n) transfers[#transfers + 1] = { from, to, id, n }; move(from, to, id, n) end,
                  takeAll = function(from, to) takeAlls[#takeAlls + 1] = { from, to } end,
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

    TEST_F(SkyrimInventoryWindows, openingBesideAContainerBuildsTwoDocumentsWithTheirRows)
    {
        ASSERT_TRUE(run("win.open('500', 'Chest'); ui.update()"));
        EXPECT_EQ(documents(), (std::vector<std::string>{ "skyrim-container", "skyrim-inventory" }));
        EXPECT_TRUE(textExists("Inventory"));
        EXPECT_TRUE(textExists("Chest"));
        EXPECT_TRUE(textExists("Weight 3.5 / 300"));
        EXPECT_TRUE(textExists("Gold 100"));
        EXPECT_EQ(buttonsWith("Pebble  x5").size(), 1u);
        EXPECT_EQ(buttonsWith("Sword").size(), 1u);
        EXPECT_EQ(buttonsWith("Gold  x100").size(), 1u);
        EXPECT_TRUE(get<bool>("win.isOpen()"));
        EXPECT_EQ(get<std::string>("win.owner()"), "500");
    }

    TEST_F(SkyrimInventoryWindows, theInventoryAloneHasNoContainerDocument)
    {
        ASSERT_TRUE(run("win.open(nil, ''); ui.update()"));
        EXPECT_EQ(documents(), (std::vector<std::string>{ "skyrim-inventory" }));
        // with nothing open on the right a click on an item moves nothing
        click(buttonsWith("Gold").front());
        EXPECT_EQ(get<int>("#transfers"), 0);
    }

    TEST_F(SkyrimInventoryWindows, clickingAContainerRowTakesOneAndItsAllButtonTakesTheStack)
    {
        ASSERT_TRUE(run("win.open('500', 'Chest'); ui.update()"));
        click(buttonsWith("Pebble").front());
        ASSERT_EQ(get<int>("#transfers"), 1);
        EXPECT_EQ(get<std::string>("transfers[1][1]"), "500");
        EXPECT_EQ(get<std::string>("transfers[1][2]"), "player");
        EXPECT_EQ(get<std::string>("transfers[1][3]"), "00000a01");
        EXPECT_EQ(get<int>("transfers[1][4]"), 1);
        // the lists follow the pack's revision on the next update: the pebble count is 4 now
        ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_EQ(buttonsWith("Pebble  x4").size(), 1u);
        EXPECT_EQ(buttonsWith("Pebble  x5").size(), 0u);
        EXPECT_EQ(buttonsWith("Pebble").size(), 2u); // x4 in the chest, one (no count shown) in the inventory
    }

    TEST_F(SkyrimInventoryWindows, anItemOfTheInventoryIsPutIntoTheContainer)
    {
        ASSERT_TRUE(run("win.open('500', 'Chest'); ui.update()"));
        const auto gold = buttonsWith("Gold  x100");
        ASSERT_EQ(gold.size(), 1u);
        click(gold.front());
        ASSERT_EQ(get<int>("#transfers"), 1);
        EXPECT_EQ(get<std::string>("transfers[1][1]"), "player");
        EXPECT_EQ(get<std::string>("transfers[1][2]"), "500");
        EXPECT_EQ(get<int>("transfers[1][4]"), 1);
        ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_TRUE(textExists("Gold 99"));
    }

    TEST_F(SkyrimInventoryWindows, takeAllAndCloseCallTheirHooks)
    {
        ASSERT_TRUE(run("win.open('500', 'Chest'); ui.update()"));
        click(buttonsWith("Take all").front());
        ASSERT_EQ(get<int>("#takeAlls"), 1);
        EXPECT_EQ(get<std::string>("takeAlls[1][1]"), "500");
        EXPECT_EQ(get<std::string>("takeAlls[1][2]"), "player");
        click(buttonsWith("Close").front());
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimInventoryWindows, dismissTakesTheDocumentsDown)
    {
        ASSERT_TRUE(run("win.open('500', 'Chest'); ui.update()"));
        ASSERT_TRUE(run("win.dismiss(); ui.update()"));
        EXPECT_TRUE(documents().empty());
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        ASSERT_TRUE(run("win.dismiss(); ui.update()")); // again: harmless
    }

    TEST_F(SkyrimInventoryWindows, anOwnerThatIsNotRegisteredYetShowsOpeningAndFillsWhenKnown)
    {
        ASSERT_TRUE(run("win.open('600', 'Corpse'); ui.update()"));
        EXPECT_TRUE(textExists("Opening..."));
        EXPECT_EQ(buttonsWith("Sword").size(), 0u);
        ASSERT_TRUE(run("store['600'] = { ['00000b02'] = 1 }; win.update(); ui.update()"));
        EXPECT_FALSE(textExists("Opening..."));
        EXPECT_EQ(buttonsWith("Sword").size(), 1u);
    }

    TEST_F(SkyrimInventoryWindows, aFrameWithNothingChangedMakesNoProtocolCalls)
    {
        ASSERT_TRUE(run("win.open('500', 'Chest'); ui.update()"));
        const std::size_t calls = backend.calls().size();
        for (int i = 0; i < 20; ++i)
            ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_EQ(backend.calls().size(), calls);
    }
}
