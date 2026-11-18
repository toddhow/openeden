// Task 0312: the worldspace id text of a neutral world ("<gameId>:w<world>") read back into its parts, so that a Lua
// script can ask for an exterior cell of another world by that text (the ship to Solstheim).
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutralcell.hpp"

namespace
{
    using namespace MWWorld;

    TEST(NeutralWorldspaceText, theTextOfWorldspaceIdIsReadBack)
    {
        const ESM::RefId id = neutralWorldspaceId("skyrimse", 0x04000800);
        std::string game;
        openeden::sdk::game::WorldId world = 0;
        ASSERT_TRUE(parseNeutralWorldspaceText(id.getRefIdString(), game, world));
        EXPECT_EQ(game, "skyrimse");
        EXPECT_EQ(world, 0x04000800u);
    }

    TEST(NeutralWorldspaceText, aTextThatIsNotAWorldspaceIdIsRefused)
    {
        std::string game;
        openeden::sdk::game::WorldId world = 0;
        for (const char* text : { "", "skyrimse", "skyrimse:w", "skyrimse:wabc", ":w12", "skyrimse:w12x", "a:b:w3",
                 "skyrimse:w99999999999", "Balmora, Caius Cosades' House" })
            EXPECT_FALSE(parseNeutralWorldspaceText(text, game, world)) << text;
    }

    TEST(NeutralWorldspaceText, worldNoneIsRefused)
    {
        std::string game;
        openeden::sdk::game::WorldId world = 7;
        EXPECT_FALSE(parseNeutralWorldspaceText("skyrimse:w0", game, world));
    }
}
