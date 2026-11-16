// Task 0205: the Skyrim SE shout cooldown indicator (files/data/openeden/skyrimse/shouthud.lua) over the headless UI
// backend and a fake `core.skyrimse.shouts` package written in Lua.
#include "uiluafixture.hpp"

#include <string>

namespace
{
    using UiTest::UiLuaFixture;

    struct SkyrimShoutHud : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                H = require('openeden.skyrimse.shouthud')
                state = { ready = true, name = '', cooldown = 0, total = 0 }
                shouts = {
                  ready = function() return state.ready end,
                  equipped_name = function() return state.name end,
                  cooldown = function() return state.cooldown end,
                  cooldown_total = function() return state.total end,
                }
                hud = H.create({ ui = ui, shouts = shouts })
            )"));
        }

        std::size_t documents(const std::string& name) const
        {
            std::size_t n = 0;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument && node.mName == name)
                    ++n;
            return n;
        }

        void frame() { ASSERT_TRUE(run("hud.update(); ui.update()")); }
    };

    TEST_F(SkyrimShoutHud, theFractionIsTheShareOfTheRecoveryStillToRun)
    {
        EXPECT_DOUBLE_EQ(get<double>("H.fraction(7.5, 15)"), 0.5);
        EXPECT_DOUBLE_EQ(get<double>("H.fraction(15, 15)"), 1.0);
        EXPECT_DOUBLE_EQ(get<double>("H.fraction(0, 15)"), 0.0);
        EXPECT_DOUBLE_EQ(get<double>("H.fraction(5, 0)"), 0.0);
        EXPECT_DOUBLE_EQ(get<double>("H.fraction(99, 15)"), 1.0); // never more than a full bar
        EXPECT_DOUBLE_EQ(get<double>("H.fraction(0/0, 15)"), 0.0); // nan
        EXPECT_DOUBLE_EQ(get<double>("H.fraction(nil, 15)"), 0.0);
    }

    TEST_F(SkyrimShoutHud, theBarWidthIsWholePixelsWithinItsBorder)
    {
        EXPECT_EQ(get<int>("H.barWidth(0)"), 0);
        EXPECT_EQ(get<int>("H.barWidth(1)"), 216);
        EXPECT_EQ(get<int>("H.barWidth(0.5)"), 108);
        EXPECT_EQ(get<int>("H.barWidth(2)"), 216);
        EXPECT_EQ(get<int>("H.barWidth(-1)"), 0);
    }

    TEST_F(SkyrimShoutHud, theLabelNamesTheShoutAndTheSecondsLeftOrReady)
    {
        EXPECT_EQ(get<std::string>("H.label('Unrelenting Force', 12.34)"), "Unrelenting Force  12.3 s");
        EXPECT_EQ(get<std::string>("H.label('Unrelenting Force', 0)"), "Unrelenting Force  ready");
        EXPECT_EQ(get<std::string>("H.label('Unrelenting Force', nil)"), "Unrelenting Force  ready");
    }

    TEST_F(SkyrimShoutHud, nothingIsBuiltWhileNoShoutIsEquippedOrThePackIsNotReady)
    {
        frame();
        EXPECT_FALSE(get<bool>("hud.isBuilt()"));
        EXPECT_EQ(documents("skyrim-shout"), 0u);
        ASSERT_TRUE(run("state.name = 'Unrelenting Force'; state.ready = false"));
        frame();
        EXPECT_FALSE(get<bool>("hud.isBuilt()"));
    }

    TEST_F(SkyrimShoutHud, anEquippedShoutShowsReadyWithAFullEmptyBar)
    {
        ASSERT_TRUE(run("state.name = 'Unrelenting Force'"));
        frame();
        EXPECT_TRUE(get<bool>("hud.isBuilt()"));
        EXPECT_EQ(documents("skyrim-shout"), 1u);
        EXPECT_EQ(get<std::string>("hud.text()"), "Unrelenting Force  ready");
        EXPECT_EQ(get<std::string>("hud.barPixels()"), "0px");
    }

    TEST_F(SkyrimShoutHud, theBarAndTheSecondsFollowTheCooldown)
    {
        ASSERT_TRUE(run("state.name = 'Unrelenting Force'; state.cooldown = 7.5; state.total = 15"));
        frame();
        EXPECT_EQ(get<std::string>("hud.text()"), "Unrelenting Force  7.5 s");
        EXPECT_EQ(get<std::string>("hud.barPixels()"), "108px");
        ASSERT_TRUE(run("state.cooldown = 1.5"));
        frame();
        EXPECT_EQ(get<std::string>("hud.barPixels()"), "22px"); // 0.1 x 216 = 21.6
        ASSERT_TRUE(run("state.cooldown = 0"));
        frame();
        EXPECT_EQ(get<std::string>("hud.text()"), "Unrelenting Force  ready");
        EXPECT_EQ(get<std::string>("hud.barPixels()"), "0px");
        EXPECT_EQ(documents("skyrim-shout"), 1u); // one document throughout
    }

    TEST_F(SkyrimShoutHud, theIndicatorGoesWhenTheShoutIsUnequipped)
    {
        ASSERT_TRUE(run("state.name = 'Unrelenting Force'"));
        frame();
        ASSERT_TRUE(get<bool>("hud.isBuilt()"));
        ASSERT_TRUE(run("state.name = ''"));
        frame();
        EXPECT_FALSE(get<bool>("hud.isBuilt()"));
        EXPECT_EQ(documents("skyrim-shout"), 0u);
    }
}
