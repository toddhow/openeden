// Task 0281: the powers section of the Skyrim SE shout menu (files/data/openeden/skyrimse/shout_windows.lua) over the
// headless UI backend, a fake `core.skyrimse.shouts` package and a fake `core.skyrimse.character` package written in
// Lua. Written before the section and run red against the module without it.
#include "uiluafixture.hpp"

#include <string>
#include <vector>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimPowersWindow : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.shout_windows')
                revision, chrRevision, now = 1, 1, 100000
                shoutList = {}
                shouts = {
                  revision = function() return revision end,
                  souls = function() return 0 end,
                  menu_count = function() return #shoutList end,
                  menu_id = function(i) return '' end, menu_name = function(i) return '' end,
                  menu_equipped = function(i) return false end, menu_can_equip = function(i) return false end,
                  menu_can_unlock = function(i) return false end, menu_word_count = function(i) return 0 end,
                  menu_word_name = function(i, w) return '' end, menu_word_translation = function(i, w) return '' end,
                  menu_word_state = function(i, w) return 'unknown' end,
                }
                powerList = {
                  { name = 'PowerNordBattleCry', source = 'Race', support = 'works', cooldown = 0 },
                  { name = 'VampireInvisibilityPC', source = 'Vampirism', support = 'works', cooldown = 12300 },
                  { name = 'PowerRedguardAdrenaline', source = 'Race', support = 'none', cooldown = 0 },
                }
                chosen = -1
                chr = {
                  revision = function() return chrRevision end,
                  power_count = function() return #powerList end,
                  power_name = function(i) return powerList[i + 1] and powerList[i + 1].name or '' end,
                  power_source = function(i) return powerList[i + 1] and powerList[i + 1].source or '' end,
                  power_support = function(i) return powerList[i + 1] and powerList[i + 1].support or '' end,
                  power_cooldown = function(i, t) return powerList[i + 1] and powerList[i + 1].cooldown or 0 end,
                  power_chosen = function() return chosen end,
                }
                picked = {}
                closes = 0
                win = W.create({ ui = ui, shouts = shouts, chr = chr,
                  now = function() return now end,
                  choosePower = function(name) picked[#picked + 1] = name end,
                  unlock = function() end, equip = function() end,
                  close = function() closes = closes + 1 end })
            )"));
        }

        std::vector<ElementId> buttonsWith(const std::string& text) const
        {
            std::vector<ElementId> out;
            for (ElementId id : withClass("button"))
            {
                const auto& node = *backend.node(id);
                const auto display = node.mProperties.find("display");
                const bool hidden = display != node.mProperties.end() && display->second == "none";
                if (!hidden && node.mText.find(text) != std::string::npos)
                    out.push_back(id);
            }
            return out;
        }

        bool textExists(const std::string& text) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText == text)
                    return true;
            return false;
        }

        bool textContains(const std::string& part) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (!node.mDocument && node.mText.find(part) != std::string::npos)
                    return true;
            return false;
        }

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }
    };

    TEST_F(SkyrimPowersWindow, theCooldownTextIsReadableAtEverySize)
    {
        EXPECT_EQ(get<std::string>("W.cooldownText(0)"), "Ready");
        EXPECT_EQ(get<std::string>("W.cooldownText(-5)"), "Ready");
        EXPECT_EQ(get<std::string>("W.cooldownText(30)"), "Ready in under a minute");
        EXPECT_EQ(get<std::string>("W.cooldownText(60)"), "Ready in 1m");
        EXPECT_EQ(get<std::string>("W.cooldownText(45 * 60)"), "Ready in 45m");
        EXPECT_EQ(get<std::string>("W.cooldownText(3600)"), "Ready in 1h 0m");
        EXPECT_EQ(get<std::string>("W.cooldownText(12300)"), "Ready in 3h 25m");
        EXPECT_EQ(get<std::string>("W.cooldownText(86400)"), "Ready in 24h 0m");
    }

    TEST_F(SkyrimPowersWindow, aPowerNameIsMadeReadableFromItsEditorId)
    {
        EXPECT_EQ(get<std::string>("W.powerTitle('PowerNordBattleCry')"), "Power Nord Battle Cry");
        EXPECT_EQ(get<std::string>("W.powerTitle('VampireInvisibilityPC')"), "Vampire Invisibility PC");
        EXPECT_EQ(get<std::string>("W.powerTitle('')"), "");
    }

    TEST_F(SkyrimPowersWindow, withoutTheCharacterPackageThereIsNoPowersSection)
    {
        ASSERT_TRUE(run(R"(
            win2 = W.create({ ui = ui, shouts = shouts, unlock = function() end, equip = function() end,
              close = function() end })
            win2.open(); ui.update()
        )"));
        EXPECT_FALSE(textExists("Powers"));
        EXPECT_FALSE(textContains("Vampirism"));
    }

    TEST_F(SkyrimPowersWindow, theSectionListsEveryPowerWithItsSourceAndCooldown)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_TRUE(textExists("Powers"));
        EXPECT_TRUE(textContains("Power Nord Battle Cry"));
        EXPECT_TRUE(textContains("Vampire Invisibility PC"));
        EXPECT_TRUE(textContains("Power Redguard Adrenaline"));
        EXPECT_TRUE(textExists("Race"));
        EXPECT_TRUE(textExists("Vampirism"));
        EXPECT_TRUE(textExists("Ready"));
        EXPECT_TRUE(textExists("Ready in 3h 25m"));
        EXPECT_TRUE(textExists("Cannot be used")); // the unsupported one
    }

    TEST_F(SkyrimPowersWindow, noPowersSaysSo)
    {
        ASSERT_TRUE(run("powerList = {}; win.open(); ui.update()"));
        EXPECT_TRUE(textExists("You have no powers."));
        EXPECT_EQ(buttonsWith("Equip").size(), 0u);
    }

    TEST_F(SkyrimPowersWindow, theEquipButtonShowsOnlyOnUsablePowersAndChoosesThatPowerByName)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const auto equip = buttonsWith("Equip");
        ASSERT_EQ(equip.size(), 2u); // not the unsupported one
        click(equip.back());
        ASSERT_EQ(get<int>("#picked"), 1);
        EXPECT_EQ(get<std::string>("picked[1]"), "VampireInvisibilityPC");
    }

    TEST_F(SkyrimPowersWindow, theChosenPowerIsMarkedAndItsButtonUnequipsByChoosingNothing)
    {
        ASSERT_TRUE(run("chosen = 0; chrRevision = 2; win.open(); ui.update()"));
        EXPECT_TRUE(textContains("* Power Nord Battle Cry"));
        EXPECT_TRUE(textExists("Equipped power: Power Nord Battle Cry"));
        const auto unequip = buttonsWith("Unequip");
        ASSERT_EQ(unequip.size(), 1u);
        EXPECT_EQ(buttonsWith("Equip").size(), 1u) << "the other usable power can still be equipped";
        click(unequip.front());
        ASSERT_EQ(get<int>("#picked"), 1);
        EXPECT_EQ(get<std::string>("picked[1]"), "");
    }

    TEST_F(SkyrimPowersWindow, noChoiceSaysTheKeyUsesTheFirstReadyPower)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_TRUE(textExists("Equipped power: none (the P key uses the first ready power)"));
    }

    TEST_F(SkyrimPowersWindow, aChangeOfTheChoiceKeepsTheSameButtonNodesSoTheControllerFocusStaysOnThem)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const auto before = buttonsWith("Equip");
        ASSERT_EQ(before.size(), 2u);
        ASSERT_TRUE(run("chosen = 0; chrRevision = 2; win.update(); ui.update()"));
        EXPECT_TRUE(textExists("Equipped power: Power Nord Battle Cry"));
        const auto after = buttonsWith("Equip");
        ASSERT_EQ(after.size(), 1u);
        EXPECT_EQ(after.front(), before.back()) << "the Equip button of the other power is the same element";
    }

    TEST_F(SkyrimPowersWindow, theCooldownFollowsTheGameClockOnceAMinuteAndNotBetween)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_TRUE(textExists("Ready in 3h 25m"));
        ASSERT_TRUE(run("powerList[2].cooldown = 12240; now = now + 60; win.update(); ui.update()"));
        EXPECT_TRUE(textExists("Ready in 3h 24m"));
        ASSERT_TRUE(run("powerList[2].cooldown = 5; now = now + 10; win.update(); ui.update()"));
        EXPECT_TRUE(textExists("Ready in 3h 24m")) << "ten seconds on: the same game minute, not re-read";
    }

    TEST_F(SkyrimPowersWindow, aFrameWithNothingChangedMakesNoProtocolCalls)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const std::size_t calls = backend.calls().size();
        for (int i = 0; i < 20; ++i)
            ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_EQ(backend.calls().size(), calls);
    }

    TEST_F(SkyrimPowersWindow, thePowersChangeWithTheCharacterRevision)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        ASSERT_TRUE(run(R"(
            powerList[#powerList + 1] = { name = 'PowerBeastForm', source = 'Lycanthropy', support = 'works',
              cooldown = 0 }
            chrRevision = 2
            win.update(); ui.update()
        )"));
        EXPECT_TRUE(textContains("Power Beast Form"));
        EXPECT_TRUE(textExists("Lycanthropy"));
    }
}
