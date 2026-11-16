// Task 0184: the equip, unequip and use buttons of the Skyrim SE inventory window (files/data/openeden/skyrimse/
// inventory_windows.lua) over the headless UI backend and a fake `core.skyrimse.inventory` package written in Lua that
// has
// the version 1.1 functions `equipped` and `use_kind`. Written before the module change and run red against the module
// of task 0182 first.
#include "uiluafixture.hpp"

#include <algorithm>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimEquipWindows : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.inventory_windows')
                store = { player = { ['0000000f'] = 100, ['00000b02'] = 1, ['00000c03'] = 2, ['00000a01'] = 5 },
                          ['500'] = { ['00000b02'] = 1, ['00000d04'] = 1 } }
                info = { ['0000000f'] = { 'Gold', 'misc', 0, 1, '' }, ['00000a01'] = { 'Pebble', 'misc', 0.5, 25, '' },
                         ['00000b02'] = { 'Sword', 'weapon', 9, 25, 'weapon' },
                         ['00000c03'] = { 'Potion', 'potion', 0.5, 30, 'potion' },
                         ['00000d04'] = { 'Helm', 'armor', 3, 60, 'armor' } }
                worn = {}
                revision = 1
                calls = {}
                inv = {
                  revision = function() return revision end,
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
                  carried = function() return 3.5 end,
                  capacity = function() return 300 end,
                  equipped = function(id) return worn[id] == true end,
                  use_kind = function(id) return info[id][5] end,
                }
                env = { ui = ui, inv = inv,
                  transfer = function() end, takeAll = function() end, close = function() end,
                  equip = function(id) calls[#calls + 1] = 'equip ' .. id; worn[id] = true; revision = revision + 1 end,
                  unequip = function(id) calls[#calls + 1] = 'unequip ' .. id; worn[id] = nil; revision = revision + 1 end,
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

        std::vector<ElementId> buttonsStartingWith(const std::string& prefix) const
        {
            std::vector<ElementId> out;
            for (ElementId id : withClass("button"))
                if (backend.node(id)->mText.rfind(prefix, 0) == 0)
                    out.push_back(id);
            return out;
        }

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }
    };

    TEST_F(SkyrimEquipWindows, aWeaponAnArmourAndAPotionOfTheInventoryGetOneExtraButtonEach)
    {
        ASSERT_TRUE(run("win.open('500', 'Chest'); ui.update()"));
        // the inventory has a sword and a potion; the chest has a sword and a helm: only the inventory side has them
        EXPECT_EQ(buttonsWith("Equip").size(), 1u); // the sword in the inventory
        EXPECT_EQ(buttonsWith("Use").size(), 1u); // the potion
        EXPECT_EQ(buttonsWith("Unequip").size(), 0u);
    }

    TEST_F(SkyrimEquipWindows, clickingEquipAsksThePackAndTheRowShowsItWorn)
    {
        ASSERT_TRUE(run("win.open('500', 'Chest'); ui.update()"));
        click(buttonsWith("Equip").front());
        ASSERT_EQ(get<int>("#calls"), 1);
        EXPECT_EQ(get<std::string>("calls[1]"), "equip 00000b02");
        ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_EQ(buttonsWith("Equip").size(), 0u);
        ASSERT_EQ(buttonsWith("Unequip").size(), 1u);
        EXPECT_EQ(buttonsStartingWith("Sword [E]").size(), 1u); // the worn one is marked
        click(buttonsWith("Unequip").front());
        EXPECT_EQ(get<std::string>("calls[2]"), "unequip 00000b02");
        ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_EQ(buttonsWith("Equip").size(), 1u);
        EXPECT_EQ(buttonsStartingWith("Sword [E]").size(), 0u);
    }

    TEST_F(SkyrimEquipWindows, clickingUseOnAPotionAsksThePackToUseIt)
    {
        ASSERT_TRUE(run("win.open(nil, ''); ui.update()"));
        click(buttonsWith("Use").front());
        ASSERT_EQ(get<int>("#calls"), 1);
        EXPECT_EQ(get<std::string>("calls[1]"), "use 00000c03");
    }

    TEST_F(SkyrimEquipWindows, theInventoryAloneHasTheButtonsToo)
    {
        ASSERT_TRUE(run("win.open(nil, ''); ui.update()"));
        EXPECT_EQ(buttonsWith("Equip").size(), 1u);
        EXPECT_EQ(buttonsWith("Use").size(), 1u);
    }

    TEST_F(SkyrimEquipWindows, aFrameWithNothingChangedMakesNoProtocolCalls)
    {
        ASSERT_TRUE(run("win.open('500', 'Chest'); ui.update()"));
        const std::size_t calls = backend.calls().size();
        for (int i = 0; i < 20; ++i)
            ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_EQ(backend.calls().size(), calls);
    }

    TEST_F(SkyrimEquipWindows, aPackWithoutTheEquipFunctionsGetsNoExtraButtons)
    {
        ASSERT_TRUE(run("inv.equipped = nil; inv.use_kind = nil; win.open(nil, ''); ui.update()"));
        EXPECT_EQ(buttonsWith("Equip").size(), 0u);
        EXPECT_EQ(buttonsWith("Use").size(), 0u);
    }
}
