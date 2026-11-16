// Task 0188: the combat HUD's crosshair drew no pixel because RmlUi's rgba() takes the alpha as 0 to 255 (not 0 to 1):
// 'rgba(255, 255, 255, 0.85)' is an alpha of 0.85 out of 255, which is invisible. Measured in game: the same red
// frame with `rgba(255, 0, 0, 217)` draws, with `0.85` and with `1` it does not.
#include "uiluafixture.hpp"

#include <cstdlib>

namespace
{
    using UiTest::UiLuaFixture;

    struct UiCombatHudCrosshair : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                hudmod = require('openeden.hud.skyrimcombat')
                combat = { isActive = true, v = { 80, 100, 100, 100, 60, 100, 0, 0, 0, 0 } }
                function combat.active() return combat.isActive end
                function combat.hud() return combat.v end
                function combat.enemy_name() return '' end
                hud = hudmod.create(ui, combat)
                hud:update(); ui.update()
            )"));
        }
    };

    // The fourth number of an `rgba(r, g, b, a)` text, or -1.
    double alphaOf(const std::string& text)
    {
        if (text.rfind("rgba(", 0) != 0)
            return text.rfind("rgb(", 0) == 0 ? 255.0 : -1.0;
        const std::size_t comma = text.rfind(',');
        return comma == std::string::npos ? -1.0 : std::atof(text.c_str() + comma + 1);
    }

    TEST_F(UiCombatHudCrosshair, theCrosshairBarsHaveAnAlphaRmlUiCanSee)
    {
        const auto ids = withClass("combat-crosshair");
        ASSERT_EQ(ids.size(), 1u);
        const auto& cross = backend.nodes().at(ids.front());
        ASSERT_EQ(cross.mChildren.size(), 2u);
        for (const auto child : cross.mChildren)
        {
            const auto& bar = backend.nodes().at(child);
            const auto it = bar.mProperties.find("background-color");
            ASSERT_NE(it, bar.mProperties.end());
            // RmlUi's alpha is 0 to 255: anything at or below 1 is invisible
            EXPECT_GT(alphaOf(it->second), 1.0) << it->second;
        }
    }
}
