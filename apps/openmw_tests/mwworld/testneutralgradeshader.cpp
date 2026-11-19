// Task 0355 stage c: when the tonemap carries the image space grade (the GRADE shader define). The grade is in the
// shader when a game pack's image space is asked for; the new cell setting counts only in a neutral world, because it
// is on by default and a Morrowind session must keep its tonemap pipeline.
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutralcellatmosphere.hpp"

namespace
{
    using MWWorld::neutralGradeShaderWanted;

    // Arguments: neutral world, neutral weather image space, neutral image space effects, neutral cell image space.
    TEST(NeutralGradeShader, theOlderTwoSettingsStillAskForIt)
    {
        EXPECT_TRUE(neutralGradeShaderWanted(false, true, false, false));
        EXPECT_TRUE(neutralGradeShaderWanted(false, false, true, false));
        EXPECT_TRUE(neutralGradeShaderWanted(true, true, true, false));
    }

    TEST(NeutralGradeShader, theCellSettingCountsOnlyInANeutralWorld)
    {
        EXPECT_TRUE(neutralGradeShaderWanted(true, false, false, true));
        EXPECT_FALSE(neutralGradeShaderWanted(false, false, false, true)); // Morrowind: never
    }

    TEST(NeutralGradeShader, nothingAsksForItWhenNothingIsOn)
    {
        EXPECT_FALSE(neutralGradeShaderWanted(false, false, false, false));
        EXPECT_FALSE(neutralGradeShaderWanted(true, false, false, false));
    }
}
