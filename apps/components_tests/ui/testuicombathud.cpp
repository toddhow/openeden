// Task 0180 (stage S5-11): the combat HUD script module (files/data/openeden/hud/skyrimcombat.lua) over the headless UI
// backend and a fake `core.skyrimse.combat` package. Written from the task spec first and run red against a stub.
#include "uiluafixture.hpp"

#include <algorithm>

namespace
{
    using UiTest::UiLuaFixture;

    struct UiCombatHud : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                hudmod = require('openeden.hud.skyrimcombat')
                -- the fake package: the numbers the pack would hand over
                combat = { isActive = false, name = '',
                    v = { 80, 100, 100, 100, 60, 100, 0, 0, 0, 0 } }
                function combat.active() return combat.isActive end
                function combat.hud() return combat.v end
                function combat.enemy_name() return combat.name end
                hud = hudmod.create(ui, combat)
            )"));
        }

        const Ui::HeadlessBackend::Node* first(const std::string& cls) const
        {
            const auto ids = withClass(cls);
            if (ids.empty())
                return nullptr;
            auto it = backend.nodes().find(ids.front());
            return it == backend.nodes().end() ? nullptr : &it->second;
        }

        std::string property(const std::string& cls, const std::string& name) const
        {
            const auto* node = first(cls);
            if (node == nullptr)
                return "<no such element>";
            auto it = node->mProperties.find(name);
            return it == node->mProperties.end() ? "<unset>" : it->second;
        }
    };

    TEST_F(UiCombatHud, buildsNothingWhileCombatIsNotRunning)
    {
        ASSERT_TRUE(run("hud:update(); ui.update()"));
        EXPECT_EQ(backend.callCount(), 0u);
        EXPECT_TRUE(withClass("combat-crosshair").empty());
    }

    TEST_F(UiCombatHud, buildsThreeDocumentsOnceCombatRuns)
    {
        ASSERT_TRUE(run("combat.isActive = true; hud:update(); ui.update()"));
        std::vector<std::string> names;
        for (const auto& [id, node] : backend.nodes())
            if (node.mDocument)
                names.push_back(node.mName);
        std::sort(names.begin(), names.end());
        ASSERT_EQ(names.size(), 3u);
        EXPECT_EQ(names[0], "combat-crosshair");
        EXPECT_EQ(names[1], "combat-enemy");
        EXPECT_EQ(names[2], "combat-player");
        EXPECT_NE(first("combat-crosshair"), nullptr);
    }

    TEST_F(UiCombatHud, thePlayerBarsAreTheirFractionsOfTheMaximum)
    {
        ASSERT_TRUE(run(
            "combat.isActive = true; combat.v = { 80, 100, 50, 100, 25, 100, 0, 0, 0, 0 }; hud:update(); ui.update()"));
        EXPECT_EQ(property("combat-fill-health", "width"), "80%");
        EXPECT_EQ(property("combat-fill-magicka", "width"), "50%");
        EXPECT_EQ(property("combat-fill-stamina", "width"), "25%");
    }

    TEST_F(UiCombatHud, aFractionOutsideZeroToOneIsClamped)
    {
        ASSERT_TRUE(run(
            "combat.isActive = true; combat.v = { 150, 100, -5, 100, 25, 0, 0, 0, 0, 0 }; hud:update(); ui.update()"));
        EXPECT_EQ(property("combat-fill-health", "width"), "100%");
        EXPECT_EQ(property("combat-fill-magicka", "width"), "0%");
        EXPECT_EQ(property("combat-fill-stamina", "width"), "0%")
            << "a zero maximum is an empty bar, not a division by zero";
    }

    TEST_F(UiCombatHud, aBarWithNoMaximumIsHidden)
    {
        ASSERT_TRUE(run(
            "combat.isActive = true; combat.v = { 0, 0, 100, 100, 60, 100, 0, 0, 0, 0 }; hud:update(); ui.update()"));
        EXPECT_EQ(property("combat-bar-health", "display"), "none");
        EXPECT_NE(property("combat-bar-stamina", "display"), "none");
    }

    TEST_F(UiCombatHud, theEnemyBarIsHiddenUntilAnEnemyIsShown)
    {
        ASSERT_TRUE(run("combat.isActive = true; hud:update(); ui.update()"));
        EXPECT_EQ(property("combat-enemy-box", "display"), "none");
        ASSERT_TRUE(
            run("combat.v = { 80, 100, 100, 100, 60, 100, 1, 67.5, 75, 0 }; combat.name = 'Hulda'; hud:update(); "
                "ui.update()"));
        EXPECT_NE(property("combat-enemy-box", "display"), "none");
        EXPECT_EQ(property("combat-fill-enemy", "width"), "90%");
        const auto* name = first("combat-enemy-name");
        ASSERT_NE(name, nullptr);
        EXPECT_EQ(name->mText, "Hulda");
    }

    TEST_F(UiCombatHud, theEnemyBarFallsAsHitsLand)
    {
        ASSERT_TRUE(
            run("combat.isActive = true; combat.name = 'Hulda'; combat.v = { 80, 100, 100, 100, 60, 100, 1, 75, 75, 0 "
                "}; hud:update(); ui.update()"));
        EXPECT_EQ(property("combat-fill-enemy", "width"), "100%");
        ASSERT_TRUE(run("combat.v = { 80, 100, 100, 100, 60, 100, 1, 37.5, 75, 0 }; hud:update(); ui.update()"));
        EXPECT_EQ(property("combat-fill-enemy", "width"), "50%");
        ASSERT_TRUE(run("combat.v = { 80, 100, 100, 100, 60, 100, 1, 0, 75, 1 }; hud:update(); ui.update()"));
        EXPECT_EQ(property("combat-fill-enemy", "width"), "0%");
    }

    TEST_F(UiCombatHud, theEnemyBarGoesWhenThePackStopsShowingIt)
    {
        ASSERT_TRUE(
            run("combat.isActive = true; combat.name = 'Hulda'; combat.v = { 80, 100, 100, 100, 60, 100, 1, 75, 75, 0 "
                "}; hud:update(); ui.update()"));
        ASSERT_TRUE(run("combat.v = { 80, 100, 100, 100, 60, 100, 0, 75, 75, 0 }; hud:update(); ui.update()"));
        EXPECT_EQ(property("combat-enemy-box", "display"), "none");
    }

    TEST_F(UiCombatHud, aFrameWithUnchangedNumbersMakesNoProtocolCall)
    {
        ASSERT_TRUE(run("combat.isActive = true; hud:update(); ui.update()"));
        const std::size_t calls = backend.callCount();
        const std::size_t commits = backend.commitCount();
        ASSERT_TRUE(run("for i = 1, 200 do hud:update(); ui.update() end"));
        EXPECT_EQ(backend.callCount(), calls);
        EXPECT_EQ(backend.commitCount(), commits);
    }

    TEST_F(UiCombatHud, aChangeSmallerThanAHalfPercentMakesNoProtocolCall)
    {
        ASSERT_TRUE(
            run("combat.isActive = true; combat.v = { 80, 100, 100, 100, 60, 100, 0, 0, 0, 0 }; hud:update(); "
                "ui.update()"));
        const std::size_t calls = backend.callCount();
        ASSERT_TRUE(run("combat.v = { 80.1, 100, 100, 100, 60.1, 100, 0, 0, 0, 0 }; hud:update(); ui.update()"));
        EXPECT_EQ(backend.callCount(), calls);
    }

    TEST_F(UiCombatHud, destroyRemovesTheDocuments)
    {
        ASSERT_TRUE(run("combat.isActive = true; hud:update(); ui.update(); hud:destroy(); ui.update()"));
        for (const auto& [id, node] : backend.nodes())
            EXPECT_FALSE(node.mDocument) << node.mName;
    }

    TEST_F(UiCombatHud, aMissingValueTableIsSurvived)
    {
        ASSERT_TRUE(run("combat.isActive = true; combat.hud = function() return {} end; hud:update(); ui.update()"));
        EXPECT_NE(first("combat-crosshair"), nullptr);
    }
}
