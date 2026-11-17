// Task 0266: the Skyrim SE shout menu window of files/data/openeden/skyrimse/shout_windows.lua over the headless UI
// backend and a fake `core.skyrimse.shouts` package written in Lua. Written before the module and run red against a stub
// module first.
#include "uiluafixture.hpp"

#include <string>
#include <vector>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimShoutMenuWindow : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.shout_windows')
                revision = 1
                souls = 2
                shoutList = {
                  { id = 'UnrelentingForceShout', name = 'Unrelenting Force', equipped = false, canEquip = true,
                    canUnlock = true,
                    words = { { 'Fus', 'Force', 'unlocked' }, { 'Ro', 'Balance', 'learned' }, { '', '', 'unknown' } } },
                  { id = 'FireBreathShout', name = 'Fire Breath', equipped = true, canEquip = true, canUnlock = false,
                    words = { { 'Yol', 'Fire', 'unlocked' }, { '', '', 'unknown' }, { '', '', 'unknown' } } },
                }
                shouts = {
                  revision = function() return revision end,
                  souls = function() return souls end,
                  menu_count = function() return #shoutList end,
                  menu_id = function(i) return shoutList[i + 1] and shoutList[i + 1].id or '' end,
                  menu_name = function(i) return shoutList[i + 1] and shoutList[i + 1].name or '' end,
                  menu_equipped = function(i) return shoutList[i + 1] and shoutList[i + 1].equipped or false end,
                  menu_can_equip = function(i) return shoutList[i + 1] and shoutList[i + 1].canEquip or false end,
                  menu_can_unlock = function(i) return shoutList[i + 1] and shoutList[i + 1].canUnlock or false end,
                  menu_word_count = function(i) return shoutList[i + 1] and #shoutList[i + 1].words or 0 end,
                  menu_word_name = function(i, w) return shoutList[i + 1].words[w + 1][1] end,
                  menu_word_translation = function(i, w) return shoutList[i + 1].words[w + 1][2] end,
                  menu_word_state = function(i, w) return shoutList[i + 1].words[w + 1][3] end,
                }
                unlocked, equipped, closes = {}, {}, 0
                win = W.create({ ui = ui, shouts = shouts,
                  unlock = function(id) unlocked[#unlocked + 1] = id end,
                  equip = function(id) equipped[#equipped + 1] = id end,
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

        const Ui::HeadlessBackend::Node* documentNode() const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument && node.mName == "skyrim-shouts")
                    return &node;
            return nullptr;
        }

        std::size_t documents() const
        {
            std::size_t n = 0;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument && node.mName == "skyrim-shouts")
                    ++n;
            return n;
        }

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }
    };

    TEST_F(SkyrimShoutMenuWindow, openingBuildsOneDocumentWithTheSoulsTheEquippedShoutAndEveryLearnedShout)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_EQ(documents(), 1u);
        EXPECT_TRUE(get<bool>("win.isOpen()"));
        EXPECT_TRUE(textExists("Shouts"));
        EXPECT_TRUE(textExists("Dragon souls: 2"));
        EXPECT_TRUE(textExists("Equipped: Fire Breath"));
        EXPECT_TRUE(textContains("Unrelenting Force"));
        EXPECT_TRUE(textContains("* Fire Breath")); // the equipped one is marked
    }

    TEST_F(SkyrimShoutMenuWindow, eachWordShowsItsStateAndItsTranslation)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_TRUE(textContains("Fus (Force)")); // unlocked
        EXPECT_TRUE(textContains("[Ro (Balance)]")); // learned, not unlocked
        EXPECT_TRUE(textContains("-----")); // unknown
        EXPECT_EQ(buttonsWith("Fus").size(), 0u);
    }

    TEST_F(SkyrimShoutMenuWindow, noEquippedShoutSaysNone)
    {
        ASSERT_TRUE(run("shoutList[2].equipped = false; win.open(); ui.update()"));
        EXPECT_TRUE(textExists("Equipped: none"));
    }

    TEST_F(SkyrimShoutMenuWindow, theUnlockButtonShowsOnlyOnAShoutThatCanUnlockAndAsksForThatShout)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const auto unlock = buttonsWith("Unlock");
        ASSERT_EQ(unlock.size(), 1u); // only the first shout can
        click(unlock.front());
        ASSERT_EQ(get<int>("#unlocked"), 1);
        EXPECT_EQ(get<std::string>("unlocked[1]"), "UnrelentingForceShout");
        EXPECT_EQ(get<int>("#equipped"), 0);
    }

    TEST_F(SkyrimShoutMenuWindow, theEquipButtonShowsOnAShoutWithAnUnlockedWordAndAsksForThatShout)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const auto equip = buttonsWith("Equip");
        ASSERT_EQ(equip.size(), 2u);
        click(equip.back());
        ASSERT_EQ(get<int>("#equipped"), 1);
        EXPECT_EQ(get<std::string>("equipped[1]"), "FireBreathShout");
        ASSERT_TRUE(run("shoutList[1].canEquip = false; revision = 2; win.update(); ui.update()"));
        EXPECT_EQ(buttonsWith("Equip").size(), 1u);
    }

    TEST_F(SkyrimShoutMenuWindow, aChangeKeepsTheSameButtonNodesSoTheControllerFocusStaysOnThem)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const auto before = buttonsWith("Unlock");
        ASSERT_EQ(before.size(), 1u);
        ASSERT_TRUE(run(R"(
            souls = 1
            shoutList[1].words[2][3] = 'unlocked'
            revision = 2
            win.update(); ui.update()
        )"));
        const auto after = buttonsWith("Unlock");
        ASSERT_EQ(after.size(), 1u);
        EXPECT_EQ(after.front(), before.front()); // the same element: nothing was rebuilt
        EXPECT_TRUE(textContains("Fus (Force)    Ro (Balance)"));
        EXPECT_FALSE(textContains("[Ro (Balance)]"));
    }

    TEST_F(SkyrimShoutMenuWindow, noLearnedShoutSaysSo)
    {
        ASSERT_TRUE(run("shoutList = {}; win.open(); ui.update()"));
        EXPECT_TRUE(textExists("You have learned no words of power."));
        EXPECT_EQ(buttonsWith("Unlock").size(), 0u);
    }

    TEST_F(SkyrimShoutMenuWindow, theWindowFollowsThePacksRevision)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        ASSERT_TRUE(run(R"(
            souls = 1
            shoutList[1].words[2][3] = 'unlocked'
            shoutList[1].words[3] = { 'Dah', 'Push', 'learned' }
            shoutList[1].equipped = true
            shoutList[2].equipped = false
            revision = 2
            win.update(); ui.update()
        )"));
        EXPECT_TRUE(textExists("Dragon souls: 1"));
        EXPECT_TRUE(textExists("Equipped: Unrelenting Force"));
        EXPECT_TRUE(textContains("Ro (Balance)"));
        EXPECT_FALSE(textContains("[Ro (Balance)]"));
        EXPECT_TRUE(textContains("[Dah (Push)]"));
    }

    TEST_F(SkyrimShoutMenuWindow, theDocumentOptsInToTheControllerAndTheButtonsAreFocusable)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const auto* doc = documentNode();
        ASSERT_NE(doc, nullptr);
        const auto it = doc->mAttributes.find("nav-capture");
        ASSERT_NE(it, doc->mAttributes.end());
        EXPECT_EQ(it->second, "true");
        for (ElementId id : buttonsWith("Unlock"))
            EXPECT_EQ(backend.node(id)->mAttributes.count("tabindex"), 1u);
    }

    TEST_F(SkyrimShoutMenuWindow, aFrameWithNothingChangedMakesNoProtocolCalls)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const std::size_t calls = backend.calls().size();
        for (int i = 0; i < 20; ++i)
            ASSERT_TRUE(run("win.update(); ui.update()"));
        EXPECT_EQ(backend.calls().size(), calls);
    }

    TEST_F(SkyrimShoutMenuWindow, closeCallsItsHookAndDismissTakesTheDocumentDown)
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
