// Task 0244: the pieces of Morrowind's HUD that the pack setting `hud style` replaces beyond task 0188's.
#include <gtest/gtest.h>

#include "apps/openmw/mwgui/hudreplaced.hpp"

namespace MWGui
{
    namespace
    {
        TEST(MWGuiHudReplacedStyle, withoutTheStyleNothingNewIsReplaced)
        {
            const ReplacedHudParts parts = replacedHudParts(true, true, true, false, true, true);
            EXPECT_FALSE(parts.mSneak);
            EXPECT_FALSE(parts.mMinimap);
            EXPECT_TRUE(parts.mStats); // task 0188's parts are as before
        }

        TEST(MWGuiHudReplacedStyle, theStyleWithTheSneakEyeAndTheCompassReplacesTheSneakIconAndTheMinimap)
        {
            const ReplacedHudParts parts = replacedHudParts(true, true, true, true, true, true);
            EXPECT_TRUE(parts.mSneak);
            EXPECT_TRUE(parts.mMinimap);
        }

        TEST(MWGuiHudReplacedStyle, eachPieceNeedsItsOwnReplacement)
        {
            const ReplacedHudParts noEye = replacedHudParts(true, true, true, true, false, true);
            EXPECT_FALSE(noEye.mSneak);
            EXPECT_TRUE(noEye.mMinimap);
            const ReplacedHudParts noCompass = replacedHudParts(true, true, true, true, true, false);
            EXPECT_TRUE(noCompass.mSneak);
            EXPECT_FALSE(noCompass.mMinimap);
        }

        TEST(MWGuiHudReplacedStyle, aMorrowindSessionIsNeverTouchedWhateverTheSettingsSay)
        {
            for (const bool combat : { false, true })
                for (const bool rml : { false, true })
                    EXPECT_FALSE(replacedHudParts(false, combat, rml, true, true, true).any()) << combat << rml;
        }

        TEST(MWGuiHudReplacedStyle, withoutTheCombatHudOrRmlUiNothingIsReplaced)
        {
            EXPECT_FALSE(replacedHudParts(true, false, true, true, true, true).any());
            EXPECT_FALSE(replacedHudParts(true, true, false, true, true, true).any());
        }

        TEST(MWGuiHudReplacedStyle, anyCountsTheNewPieces)
        {
            ReplacedHudParts parts;
            EXPECT_FALSE(parts.any());
            parts.mSneak = true;
            EXPECT_TRUE(parts.any());
            parts.mSneak = false;
            parts.mMinimap = true;
            EXPECT_TRUE(parts.any());
        }
    }
}
