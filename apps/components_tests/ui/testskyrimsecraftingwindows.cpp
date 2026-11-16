// Task 0199: the Skyrim SE crafting window of files/data/openeden/skyrimse/crafting_windows.lua over the headless UI
// backend and fakes of the `core.skyrimse.inventory` and `core.skyrimse.crafting` packages written in Lua. NOTE: the
// module was written first here (the Lua half), the tests after, so they were NOT run red first and no mutation was
// run.
#include "uiluafixture.hpp"

#include <algorithm>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimCraftingWindows : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.crafting_windows')
                names = { ['00000a01'] = 'Ingredient A', ['00000a02'] = 'Ingredient B', ['00000b01'] = 'Iron Sword',
                          ['00000b02'] = 'Iron Armor', ['00000c01'] = 'Grand Soul Gem' }
                held = { ['00000a01'] = 3, ['00000a02'] = 1, ['00000b01'] = 1, ['00000b02'] = 1, ['00000c01'] = 1 }
                revision, results, crev, state, bench, last, reason = 1, 0, 1, 'ready', 'alchemy', '', ''
                calls = {}
                local function rec(name) return function(...) calls[#calls + 1] = { name, ... } end end
                inv = {
                  revision = function() return revision end,
                  name = function(id) return names[id] or id end,
                  count = function(o, id) return held[id] or 0 end,
                }
                crafting = {
                  state = function() return state end,
                  reason = function() return reason end,
                  bench = function() return bench end,
                  results = function() return results end,
                  revision = function() return crev end,
                  last_result = function() return last end,
                  ingredients = function() return { '00000a01', '00000a02' } end,
                  ingredient_effects = function(id) return id == '00000a01' and 'Restore Health, ?, ?, ?' or '?, ?, ?, ?' end,
                  preview = function(a, b, c) return 'Restore Health, ?' end,
                  recipes = function() return { '00000d01', '00000d02' } end,
                  recipe_name = function(id) return id == '00000d01' and 'Iron Sword' or 'Iron Armor' end,
                  recipe_needs = function(id) return 'Iron Ingot x2 (0)' end,
                  recipe_ok = function(id) return id == '00000d01' end,
                  recipe_temper = function(id) return id == '00000d02' end,
                  gear = function() return { '00000b01', '00000b02' } end,
                  gear_enchant = function(id) return id == '00000b02' and 'Fire Damage' or '' end,
                  enchantments = function() return { '00000e01' } end,
                  enchant_name = function(id) return 'Fire Damage' end,
                  gems = function() return { '00000c01' } end,
                  gem_size = function(id) return 5 end,
                  enchant_preview = function(i, e, g) return 'Fire Damage 16.6' end,
                }
                env = { ui = ui, inv = inv, crafting = crafting,
                  brew = rec('brew'), eat = rec('eat'), craft = rec('craft'), temper = rec('temper'),
                  disenchant = rec('disenchant'), enchant = rec('enchant'), close = rec('close') }
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

        bool textExists(const std::string& text) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText == text)
                    return true;
            return false;
        }

        std::vector<std::string> documents() const
        {
            std::vector<std::string> names;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument)
                    names.push_back(node.mName);
            return names;
        }

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }

        void clickButton(const std::string& prefix)
        {
            const std::vector<ElementId> found = buttonsWith(prefix);
            ASSERT_EQ(found.size(), 1u) << prefix;
            click(found.front());
        }
    };

    TEST_F(SkyrimCraftingWindows, theAlchemyWindowListsTheIngredientsWithTheirKnownEffects)
    {
        ASSERT_TRUE(run("win.open('Alchemy Lab'); ui.update()"));
        EXPECT_EQ(documents(), (std::vector<std::string>{ "skyrim-crafting" }));
        EXPECT_TRUE(textExists("Alchemy Lab"));
        EXPECT_EQ(buttonsWith("Ingredient A  x3").size(), 1u);
        EXPECT_EQ(buttonsWith("Ingredient B  x1").size(), 1u);
        EXPECT_TRUE(textExists("Restore Health, ?, ?, ?"));
        EXPECT_TRUE(textExists("Chosen: -"));
        EXPECT_TRUE(textExists("Choose two or three ingredients"));
    }

    TEST_F(SkyrimCraftingWindows, choosingIngredientsShowsThePreviewAndBrewAsksForThem)
    {
        ASSERT_TRUE(run("win.open('Alchemy Lab'); ui.update()"));
        clickButton("Ingredient A");
        EXPECT_TRUE(textExists("Chosen: Ingredient A"));
        clickButton("* Ingredient A"); // a chosen one shows a star; clicking again drops it
        EXPECT_TRUE(textExists("Chosen: -"));
        clickButton("Ingredient A");
        clickButton("Ingredient B");
        EXPECT_TRUE(textExists("Chosen: Ingredient A, Ingredient B"));
        EXPECT_TRUE(textExists("Potion: Restore Health, ?"));
        clickButton("Brew");
        ASSERT_EQ(get<int>("#calls"), 1);
        EXPECT_EQ(get<std::string>("calls[1][1]"), "brew");
        EXPECT_EQ(get<std::string>("calls[1][2]"), "00000a01");
        EXPECT_EQ(get<std::string>("calls[1][3]"), "00000a02");
        EXPECT_EQ(get<std::string>("calls[1][4]"), "");
    }

    TEST_F(SkyrimCraftingWindows, atMostThreeIngredientsAreChosenAndClearEmptiesThem)
    {
        ASSERT_TRUE(
            run("names['00000a03'] = 'Ingredient C'; names['00000a04'] = 'Ingredient D'; held['00000a03'] = 1; "
                "held['00000a04'] = 1; "
                "crafting.ingredients = function() return { '00000a01', '00000a02', '00000a03', '00000a04' } end; "
                "win.open('Alchemy Lab'); ui.update()"));
        clickButton("Ingredient A");
        clickButton("Ingredient B");
        clickButton("Ingredient C");
        clickButton("Ingredient D"); // a fourth is not taken
        EXPECT_TRUE(textExists("Chosen: Ingredient A, Ingredient B, Ingredient C"));
        clickButton("Clear");
        EXPECT_TRUE(textExists("Chosen: -"));
    }

    TEST_F(SkyrimCraftingWindows, theEatButtonEatsTheRowsIngredient)
    {
        ASSERT_TRUE(run("win.open('Alchemy Lab'); ui.update()"));
        const std::vector<ElementId> eat = buttonsWith("Eat");
        ASSERT_EQ(eat.size(), 2u);
        click(eat.front());
        ASSERT_EQ(get<int>("#calls"), 1);
        EXPECT_EQ(get<std::string>("calls[1][1]"), "eat");
        EXPECT_EQ(get<std::string>("calls[1][2]"), "00000a01");
    }

    TEST_F(SkyrimCraftingWindows, aChosenIngredientThatIsUsedUpIsDropped)
    {
        ASSERT_TRUE(run("win.open('Alchemy Lab'); ui.update()"));
        clickButton("Ingredient B");
        EXPECT_TRUE(textExists("Chosen: Ingredient B"));
        ASSERT_TRUE(
            run("held['00000a02'] = nil; crafting.ingredients = function() return { '00000a01' } end; "
                "revision = revision + 1; ui.update(); win.update(); ui.update()"));
        EXPECT_TRUE(textExists("Chosen: -"));
        EXPECT_EQ(buttonsWith("Ingredient B").size(), 0u);
    }

    TEST_F(SkyrimCraftingWindows, theLastResultIsShownAndFollowsTheResultsCounter)
    {
        ASSERT_TRUE(run("win.open('Alchemy Lab'); ui.update()"));
        ASSERT_TRUE(run("last = 'ok'; results = results + 1; win.update(); ui.update()"));
        EXPECT_TRUE(textExists("ok"));
        ASSERT_TRUE(run("last = 'not held'; results = results + 1; win.update(); ui.update()"));
        EXPECT_TRUE(textExists("not held"));
    }

    TEST_F(SkyrimCraftingWindows, aForgeListsItsRecipesAndAClickMakesOrImproves)
    {
        ASSERT_TRUE(run("bench = 'forge'; win.open('Forge'); ui.update()"));
        EXPECT_EQ(buttonsWith("Iron Sword").size(), 1u);
        EXPECT_EQ(buttonsWith("Improve Iron Armor").size(), 1u);
        EXPECT_TRUE(textExists("Iron Ingot x2 (0)"));
        clickButton("Iron Sword");
        ASSERT_EQ(get<int>("#calls"), 1);
        EXPECT_EQ(get<std::string>("calls[1][1]"), "craft");
        EXPECT_EQ(get<std::string>("calls[1][2]"), "00000d01");
        clickButton("Improve Iron Armor");
        ASSERT_EQ(get<int>("#calls"), 2);
        EXPECT_EQ(get<std::string>("calls[2][1]"), "temper");
        EXPECT_EQ(get<std::string>("calls[2][2]"), "00000d02");
    }

    TEST_F(SkyrimCraftingWindows, aRecipeThatCannotBeMadeNowSaysSo)
    {
        ASSERT_TRUE(run("bench = 'forge'; win.open('Forge'); ui.update()"));
        EXPECT_EQ(buttonsWith("Iron Sword  (cannot)").size(), 0u); // d01 is ok
        EXPECT_EQ(buttonsWith("Improve Iron Armor  (cannot)").size(), 1u);
    }

    TEST_F(SkyrimCraftingWindows, aBenchWithNoRecipesSaysSo)
    {
        ASSERT_TRUE(
            run("bench = 'smelter'; crafting.recipes = function() return {} end; win.open('Smelter'); ui.update()"));
        EXPECT_TRUE(textExists("This bench has nothing you can make"));
    }

    TEST_F(SkyrimCraftingWindows, theEnchantingWindowNeedsAnItemAnEnchantmentAndASoulGem)
    {
        ASSERT_TRUE(run("bench = 'enchanting'; win.open('Arcane Enchanter'); ui.update()"));
        EXPECT_TRUE(textExists("Choose an item, an enchantment and a filled soul gem"));
        EXPECT_EQ(buttonsWith("Iron Sword").size(), 1u);
        EXPECT_EQ(buttonsWith("Fire Damage").size(),
            1u); // the learned enchantment (the armour's enchantment is a detail text)
        EXPECT_EQ(buttonsWith("Grand Soul Gem").size(), 1u);
        clickButton("Enchant"); // nothing chosen: no call
        EXPECT_EQ(get<int>("#calls"), 0);
        clickButton("Iron Sword");
        clickButton("Fire Damage");
        clickButton("Grand Soul Gem");
        EXPECT_TRUE(textExists("Result: Fire Damage 16.6"));
        clickButton("Enchant");
        ASSERT_EQ(get<int>("#calls"), 1);
        EXPECT_EQ(get<std::string>("calls[1][1]"), "enchant");
        EXPECT_EQ(get<std::string>("calls[1][2]"), "00000b01");
        EXPECT_EQ(get<std::string>("calls[1][3]"), "00000e01");
        EXPECT_EQ(get<std::string>("calls[1][4]"), "00000c01");
    }

    TEST_F(SkyrimCraftingWindows, disenchantingNeedsOnlyTheItem)
    {
        ASSERT_TRUE(run("bench = 'enchanting'; win.open('Arcane Enchanter'); ui.update()"));
        clickButton("Disenchant");
        EXPECT_EQ(get<int>("#calls"), 0);
        clickButton("Iron Armor");
        clickButton("Disenchant");
        ASSERT_EQ(get<int>("#calls"), 1);
        EXPECT_EQ(get<std::string>("calls[1][1]"), "disenchant");
        EXPECT_EQ(get<std::string>("calls[1][2]"), "00000b02");
    }

    TEST_F(SkyrimCraftingWindows, theCloseButtonAsksTheHostToClose)
    {
        ASSERT_TRUE(run("win.open('Alchemy Lab'); ui.update()"));
        clickButton("Close");
        ASSERT_EQ(get<int>("#calls"), 1);
        EXPECT_EQ(get<std::string>("calls[1][1]"), "close");
    }

    TEST_F(SkyrimCraftingWindows, dismissingTakesTheWindowDownAndUpdateDoesNothingAfterwards)
    {
        ASSERT_TRUE(run("win.open('Alchemy Lab'); ui.update()"));
        EXPECT_TRUE(get<bool>("win.isOpen()"));
        ASSERT_TRUE(run("win.dismiss(); ui.update()"));
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        EXPECT_TRUE(documents().empty());
        ASSERT_TRUE(run("revision = revision + 1; win.update(); ui.update()"));
        EXPECT_TRUE(documents().empty());
    }

    TEST_F(SkyrimCraftingWindows, aSessionThatBecomesReadyLaterBuildsTheWindowOfItsBench)
    {
        ASSERT_TRUE(run("state = 'pending'; bench = ''; win.open('Alchemy Lab'); ui.update()"));
        EXPECT_EQ(buttonsWith("Ingredient A").size(), 0u);
        ASSERT_TRUE(run("state = 'ready'; bench = 'alchemy'; win.update(); ui.update()"));
        EXPECT_EQ(buttonsWith("Ingredient A").size(), 1u);
        EXPECT_EQ(buttonsWith("Brew").size(), 1u);
    }

    TEST_F(SkyrimCraftingWindows, aRefusedSessionShowsItsReason)
    {
        ASSERT_TRUE(run(
            "state = 'refused'; reason = 'that is not a crafting bench'; bench = ''; win.open('Chair'); ui.update()"));
        EXPECT_TRUE(textExists("that is not a crafting bench"));
    }
}
