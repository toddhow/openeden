// Task 0188: which parts of Morrowind's HUD the neutral combat HUD replaces.
#include <gtest/gtest.h>

#include "apps/openmw/mwgui/hudreplaced.hpp"

namespace MWGui
{
    namespace
    {
        TEST(MWGuiHudReplaced, aNeutralSessionWithTheCombatHudReplacesBarsIconsAndEnemy)
        {
            const ReplacedHudParts parts = replacedHudParts(true, true, true);
            EXPECT_TRUE(parts.mStats);
            EXPECT_TRUE(parts.mWeaponSpell);
            EXPECT_TRUE(parts.mEnemy);
        }

        TEST(MWGuiHudReplaced, aMorrowindSessionIsNeverTouchedWhateverTheSettingsSay)
        {
            for (const bool combat : { false, true })
                for (const bool rml : { false, true })
                    EXPECT_FALSE(replacedHudParts(false, combat, rml).any()) << combat << rml;
        }

        TEST(MWGuiHudReplaced, aNeutralSessionKeepsTheMorrowindHudWhenTheNewOneIsNotDrawn)
        {
            EXPECT_FALSE(replacedHudParts(true, false, true).any()); // no combat: no bars to replace them
            EXPECT_FALSE(replacedHudParts(true, true, false).any()); // no RmlUi: nothing draws the new HUD
            EXPECT_FALSE(replacedHudParts(true, false, false).any());
        }
    }
}
