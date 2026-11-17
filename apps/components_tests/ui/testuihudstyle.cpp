// Task 0244: the Skyrim-style combat HUD script module (files/data/openeden/hud/skyrimstyle.lua) over the headless UI
// backend and a fake `core.skyrimse.hudstyle` package. Written from the task spec first and run red against a stub.
#include "uiluafixture.hpp"

#include <algorithm>
#include <cstdlib>

namespace
{
    using UiTest::UiLuaFixture;

    struct UiHudStyle : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                mod = require('openeden.hud.skyrimstyle')
                -- the fake package: the numbers the pack would hand over
                style = { isActive = false, p = { 80, 100, 50, 100, 25, 100, 12 }, b = { 0, 0, 0, 1, 0, 0 },
                    bossName = '', barData = {}, nameData = {} }
                function style.active() return style.isActive end
                function style.player() return style.p end
                function style.boss() return style.b end
                function style.boss_name() return style.bossName end
                function style.bars() return style.barData end
                function style.bar_names() return style.nameData end
                clock = 0
                env = {
                  now = function() return clock end,
                  screen = function() return 1280, 720 end,
                  -- the world x, y are pixels already; a point with a negative x is behind the camera
                  project = function(x, y, z) if x < 0 then return nil end return x / 1280, y / 720 end,
                }
                hud = mod.create(ui, style, env)
                function bar(id, x, y, hp, hpmax, flags, ally)
                  return { id, x, y, 150, hp, hpmax, 40, 40, 50, 50, 7, ally and 1 or 0, flags or 0 }
                end
                function setBars(list)
                  local flat, names = {}, {}
                  for _, b in ipairs(list) do
                    for _, v in ipairs(b) do flat[#flat + 1] = v end
                    names[#names + 1] = b.name or 'Actor'
                  end
                  style.barData = flat
                  style.nameData = names
                end
                function frame() hud:update(); ui.update() end
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

        // "208px" -> 208
        double pixels(const std::string& text) const { return std::atof(text.c_str()); }

        std::size_t documents() const
        {
            std::size_t n = 0;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument)
                    ++n;
            return n;
        }

        bool documentNamed(const std::string& name) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument && node.mName == name)
                    return true;
            return false;
        }

        bool shown(const std::string& cls) const
        {
            const std::string display = property(cls, "display");
            return first(cls) != nullptr && display != "none";
        }
    };

    TEST_F(UiHudStyle, buildsNothingWhileCombatIsNotRunning)
    {
        ASSERT_TRUE(run("frame()"));
        EXPECT_EQ(backend.callCount(), 0u);
        EXPECT_EQ(documents(), 0u);
    }

    TEST_F(UiHudStyle, buildsThePlayerBossCrosshairAndEightActorDocuments)
    {
        ASSERT_TRUE(run("style.isActive = true; frame()"));
        EXPECT_TRUE(documentNamed("style-player"));
        EXPECT_TRUE(documentNamed("style-boss"));
        EXPECT_TRUE(documentNamed("style-crosshair"));
        for (int i = 1; i <= 8; ++i)
            EXPECT_TRUE(documentNamed("style-actor-" + std::to_string(i))) << i;
        EXPECT_EQ(documents(), 11u);
    }

    TEST_F(UiHudStyle, thePlayerBarsAreTheirFractionsOfTheBarWidth)
    {
        ASSERT_TRUE(run("style.isActive = true; frame()"));
        const double health = get<double>("mod.PLAYER_WIDTH");
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-health", "width")), health * 0.8);
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-magicka", "width")), health * 0.5);
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-stamina", "width")), health * 0.25);
    }

    TEST_F(UiHudStyle, aFractionOutsideZeroToOneIsClampedAndAZeroMaximumIsEmpty)
    {
        ASSERT_TRUE(run("style.isActive = true; style.p = { 150, 100, -5, 100, 25, 0, 3 }; frame()"));
        const double width = get<double>("mod.PLAYER_WIDTH");
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-health", "width")), width);
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-magicka", "width")), 0.0);
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-stamina", "width")), 0.0);
    }

    TEST_F(UiHudStyle, aPlayerBarWithNoMaximumIsHidden)
    {
        ASSERT_TRUE(run("style.isActive = true; style.p = { 0, 0, 50, 100, 25, 100, 3 }; frame()"));
        EXPECT_EQ(property("style-bar-health", "display"), "none");
        EXPECT_TRUE(shown("style-bar-stamina"));
    }

    TEST_F(UiHudStyle, thePlayerLevelIsShownBesideTheBars)
    {
        ASSERT_TRUE(run("style.isActive = true; frame()"));
        const auto* level = first("style-emblem-level");
        ASSERT_NE(level, nullptr);
        EXPECT_EQ(level->mText, "12");
    }

    // ---- the damage trail ----

    TEST_F(UiHudStyle, theTrailStepHoldsThenDrainsAndAHealFollowsAtOnce)
    {
        ASSERT_TRUE(run(R"(
            t = mod.newTrail(0.8, 0)
            drop = mod.stepTrail(t, 0.4, 0.0)       -- the drop itself: the trail stays
            held = mod.stepTrail(t, 0.4, mod.TRAIL_DELAY * 0.5)
            drained = mod.stepTrail(t, 0.4, mod.TRAIL_DELAY + 0.5)
            gone = mod.stepTrail(t, 0.4, 100)
            healed = mod.stepTrail(t, 0.9, 101)
        )"));
        EXPECT_DOUBLE_EQ(get<double>("drop"), 0.8);
        EXPECT_DOUBLE_EQ(get<double>("held"), 0.8);
        EXPECT_LT(get<double>("drained"), 0.8);
        EXPECT_GT(get<double>("drained"), 0.4);
        EXPECT_DOUBLE_EQ(get<double>("gone"), 0.4);
        EXPECT_DOUBLE_EQ(get<double>("healed"), 0.9);
    }

    TEST_F(UiHudStyle, theTrailNeverFallsBelowTheValueAndANaNIsTreatedAsZero)
    {
        ASSERT_TRUE(run(R"(
            t = mod.newTrail(0.5, 0)
            a = mod.stepTrail(t, 0.5, 1000)
            b = mod.stepTrail(t, 0/0, 1001)
        )"));
        EXPECT_DOUBLE_EQ(get<double>("a"), 0.5);
        EXPECT_DOUBLE_EQ(get<double>("b"), get<double>("b")); // not a NaN
    }

    TEST_F(UiHudStyle, aDroppedPlayerBarLeavesAPaleSegmentThatDrains)
    {
        ASSERT_TRUE(run("style.isActive = true; frame()"));
        const double width = get<double>("mod.PLAYER_WIDTH");
        EXPECT_DOUBLE_EQ(pixels(property("style-trail-health", "width")), width * 0.8);
        ASSERT_TRUE(run("style.p = { 40, 100, 50, 100, 25, 100, 12 }; clock = 0.1; frame()"));
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-health", "width")), width * 0.4);
        EXPECT_DOUBLE_EQ(pixels(property("style-trail-health", "width")), width * 0.8) << "held for a moment";
        ASSERT_TRUE(run("clock = 0.1 + mod.TRAIL_DELAY + 0.4; frame()"));
        const double mid = pixels(property("style-trail-health", "width"));
        EXPECT_LT(mid, width * 0.8);
        EXPECT_GT(mid, width * 0.4);
        ASSERT_TRUE(run("clock = 60; frame()"));
        EXPECT_DOUBLE_EQ(pixels(property("style-trail-health", "width")), width * 0.4);
    }

    TEST_F(UiHudStyle, healingFillsTheBarAndTheTrailWithIt)
    {
        ASSERT_TRUE(run("style.isActive = true; style.p = { 20, 100, 50, 100, 25, 100, 12 }; frame()"));
        ASSERT_TRUE(run("style.p = { 70, 100, 50, 100, 25, 100, 12 }; clock = 0.05; frame()"));
        const double width = get<double>("mod.PLAYER_WIDTH");
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-health", "width")), width * 0.7);
        EXPECT_DOUBLE_EQ(pixels(property("style-trail-health", "width")), width * 0.7);
    }

    // ---- the top bar ----

    TEST_F(UiHudStyle, theTopBarIsHiddenUntilSomethingIsShown)
    {
        ASSERT_TRUE(run("style.isActive = true; frame()"));
        EXPECT_EQ(property("style-boss-box", "display"), "none");
    }

    TEST_F(UiHudStyle, theTopBarShowsTheNameLevelAndHealth)
    {
        ASSERT_TRUE(
            run("style.isActive = true; style.bossName = 'Alduin'; style.b = { 1, 750, 1000, 50, 1, 0 }; frame()"));
        EXPECT_TRUE(shown("style-boss-box"));
        EXPECT_EQ(first("style-boss-name")->mText, "Alduin");
        EXPECT_EQ(first("style-boss-level")->mText, "Lv 50");
        const double width = get<double>("mod.BOSS_WIDTH");
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-boss", "width")), width * 0.75);
    }

    TEST_F(UiHudStyle, anEnemyThatIsNotABossUsesTheSameBarWithAnotherClass)
    {
        ASSERT_TRUE(run("style.isActive = true; style.bossName = 'Hulda'; style.b = { 1, 60, 100, 4, 0, 0 }; frame()"));
        EXPECT_TRUE(shown("style-boss-box"));
        EXPECT_NE(first("style-boss-enemy"), nullptr);
        ASSERT_TRUE(run("style.b = { 1, 600, 1000, 40, 1, 0 }; frame()"));
        EXPECT_EQ(first("style-boss-enemy"), nullptr);
    }

    TEST_F(UiHudStyle, theTopBarFallsAsHitsLandAndGoesWhenThePackStopsShowingIt)
    {
        ASSERT_TRUE(
            run("style.isActive = true; style.bossName = 'Hulda'; style.b = { 1, 100, 100, 4, 0, 0 }; frame()"));
        const double width = get<double>("mod.BOSS_WIDTH");
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-boss", "width")), width);
        ASSERT_TRUE(run("style.b = { 1, 25, 100, 4, 0, 0 }; clock = 0.1; frame()"));
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-boss", "width")), width * 0.25);
        EXPECT_DOUBLE_EQ(pixels(property("style-trail-boss", "width")), width);
        ASSERT_TRUE(run("style.b = { 0, 25, 100, 4, 0, 0 }; frame()"));
        EXPECT_EQ(property("style-boss-box", "display"), "none");
    }

    // ---- the bars over the heads ----

    TEST_F(UiHudStyle, anActorBarIsPlacedAtTheProjectedPoint)
    {
        ASSERT_TRUE(run("style.isActive = true; setBars({ bar(10, 640, 360, 60, 100) }); frame()"));
        EXPECT_TRUE(shown("style-actor-box-1"));
        EXPECT_FALSE(shown("style-actor-box-2"));
        const auto* name = first("style-actor-name-1");
        ASSERT_NE(name, nullptr);
        EXPECT_EQ(name->mText, "Actor");
        EXPECT_EQ(first("style-actor-level-1")->mText, "7");
        const double width = get<double>("mod.ACTOR_WIDTH");
        EXPECT_DOUBLE_EQ(pixels(property("style-actor-fill-1", "width")), width * 0.6);
        // the document hangs centred over the point (the bar's end caps stick out by CAP on both sides)
        double left = -1;
        for (const auto& [id, node] : backend.nodes())
            if (node.mDocument && node.mName == "style-actor-1")
            {
                auto it = node.mProperties.find("left");
                ASSERT_NE(it, node.mProperties.end());
                left = pixels(it->second);
            }
        EXPECT_DOUBLE_EQ(left, 640 - width / 2 - get<double>("mod.CAP"));
    }

    TEST_F(UiHudStyle, aPointBehindTheCameraGetsNoBar)
    {
        ASSERT_TRUE(run("style.isActive = true; setBars({ bar(10, -50, 360, 60, 100) }); frame()"));
        EXPECT_FALSE(shown("style-actor-box-1"));
    }

    TEST_F(UiHudStyle, anActorKeepsItsSlotWhileTheOrderChanges)
    {
        ASSERT_TRUE(run(R"(
            style.isActive = true
            local a = bar(10, 300, 200, 60, 100); a.name = 'Hulda'
            local b = bar(11, 700, 200, 90, 100); b.name = 'Sinmir'
            setBars({ a, b }); frame()
            setBars({ b, a }); frame()
        )"));
        EXPECT_EQ(first("style-actor-name-1")->mText, "Hulda");
        EXPECT_EQ(first("style-actor-name-2")->mText, "Sinmir");
    }

    TEST_F(UiHudStyle, aSlotIsFreedWhenItsActorGoesAndReusedByTheNext)
    {
        ASSERT_TRUE(run(R"(
            style.isActive = true
            setBars({ bar(10, 300, 200, 60, 100), bar(11, 700, 200, 90, 100) }); frame()
            setBars({ bar(11, 700, 200, 90, 100) }); frame()
        )"));
        EXPECT_FALSE(shown("style-actor-box-1"));
        EXPECT_TRUE(shown("style-actor-box-2"));
        ASSERT_TRUE(
            run("local c = bar(12, 100, 100, 100, 100); c.name = 'New'; setBars({ c, bar(11, 700, 200, 90, 100) }); "
                "frame()"));
        EXPECT_EQ(first("style-actor-name-1")->mText, "New");
    }

    TEST_F(UiHudStyle, anActorBarHasItsOwnTrail)
    {
        ASSERT_TRUE(run("style.isActive = true; setBars({ bar(10, 640, 360, 100, 100) }); frame()"));
        ASSERT_TRUE(run("setBars({ bar(10, 640, 360, 40, 100) }); clock = 0.1; frame()"));
        const double width = get<double>("mod.ACTOR_WIDTH");
        EXPECT_DOUBLE_EQ(pixels(property("style-actor-fill-1", "width")), width * 0.4);
        EXPECT_DOUBLE_EQ(pixels(property("style-actor-trail-1", "width")), width);
    }

    TEST_F(UiHudStyle, theSecondaryBarsFollowTheirFlags)
    {
        ASSERT_TRUE(run("style.isActive = true; setBars({ bar(10, 640, 360, 60, 100, 0) }); frame()"));
        EXPECT_EQ(property("style-actor-magicka-1", "display"), "none");
        EXPECT_EQ(property("style-actor-stamina-1", "display"), "none");
        ASSERT_TRUE(run("setBars({ bar(10, 640, 360, 60, 100, 1) }); frame()"));
        EXPECT_NE(property("style-actor-magicka-1", "display"), "none");
        EXPECT_EQ(property("style-actor-stamina-1", "display"), "none");
        ASSERT_TRUE(run("setBars({ bar(10, 640, 360, 60, 100, 3) }); frame()"));
        EXPECT_NE(property("style-actor-stamina-1", "display"), "none");
    }

    TEST_F(UiHudStyle, anAllyIsDrawnInAnotherColour)
    {
        ASSERT_TRUE(
            run("style.isActive = true; setBars({ bar(10, 300, 200, 60, 100, 0, true), bar(11, 700, 200, 60, 100) }); "
                "frame()"));
        const std::string ally = property("style-actor-fill-1", "background-color");
        const std::string foe = property("style-actor-fill-2", "background-color");
        EXPECT_NE(ally, foe);
        EXPECT_NE(ally, "<unset>");
    }

    TEST_F(UiHudStyle, aStillFrameMakesNoProtocolCall)
    {
        ASSERT_TRUE(
            run("style.isActive = true; setBars({ bar(10, 640, 360, 60, 100, 1) }); style.bossName = 'X'; style.b = { "
                "1, 5, 10, 3, 0, 0 }; frame()"));
        const std::size_t calls = backend.callCount();
        ASSERT_TRUE(run("clock = 0.016; frame(); clock = 0.032; frame()"));
        EXPECT_EQ(backend.callCount(), calls);
    }

    TEST_F(UiHudStyle, moreBarsThanSlotsAreIgnored)
    {
        ASSERT_TRUE(run(R"(
            style.isActive = true
            local list = {}
            for i = 1, 12 do list[#list + 1] = bar(100 + i, 100 + i * 50, 200, 60, 100) end
            setBars(list); frame()
        )"));
        EXPECT_TRUE(shown("style-actor-box-8"));
        EXPECT_EQ(documents(), 11u);
    }

    TEST_F(UiHudStyle, theCrosshairIsVisibleWhateverTheAlphaScale)
    {
        ASSERT_TRUE(run("style.isActive = true; frame()"));
        EXPECT_NE(first("style-crosshair"), nullptr);
        EXPECT_EQ(get<std::string>("mod.CROSSHAIR"), "rgba(255, 255, 255, 217)");
    }

    TEST_F(UiHudStyle, destroyRemovesEveryDocument)
    {
        ASSERT_TRUE(run("style.isActive = true; frame(); hud:destroy(); ui.update()"));
        EXPECT_EQ(documents(), 0u);
    }
}
