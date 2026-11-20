// Task 0361e follow-up (toddhow, 2026-11-20): in a Skyrim session the player walks and runs at the speeds of the
// Skyrim clips (82.45 and 350.87 units a second), so the walk and run cycles play at their own rate. Morrowind keeps the
// attribute based speeds. Written first (the header did not exist: compile-red).
#include <apps/openmw/mwphysics/neutralpathfollower.hpp>
#include <apps/openmw/mwworld/neutralplayerspeed.hpp>

#include <gtest/gtest.h>

namespace
{
    using MWWorld::neutralPlayerSpeed;

    TEST(NeutralPlayerSpeed, aWalkingPlayerOfASkyrimSessionMovesAtTheWalkClipSpeed)
    {
        EXPECT_FLOAT_EQ(neutralPlayerSpeed(true, true, false, 149.f), MWPhysics::NeutralPath::kWalkSpeed);
    }

    TEST(NeutralPlayerSpeed, aRunningPlayerOfASkyrimSessionMovesAtTheRunClipSpeed)
    {
        EXPECT_FLOAT_EQ(neutralPlayerSpeed(true, true, true, 272.f), MWPhysics::NeutralPath::kRunSpeed);
    }

    TEST(NeutralPlayerSpeed, aSessionThatIsNotSkyrimKeepsTheAttributeBasedSpeed)
    {
        EXPECT_FLOAT_EQ(neutralPlayerSpeed(false, true, false, 149.f), 149.f);
        EXPECT_FLOAT_EQ(neutralPlayerSpeed(false, true, true, 272.f), 272.f);
    }

    TEST(NeutralPlayerSpeed, withTheSettingOffTheSpeedIsTheOldOne)
    {
        EXPECT_FLOAT_EQ(neutralPlayerSpeed(true, false, false, 149.f), 149.f);
        EXPECT_FLOAT_EQ(neutralPlayerSpeed(true, false, true, 272.f), 272.f);
    }

    TEST(NeutralPlayerSpeed, aStandingOrParalysedPlayerStaysAtZero)
    {
        // the caller answers 0 for a paralysed, knocked down or dead actor before this is asked; a zero speed is kept
        EXPECT_FLOAT_EQ(neutralPlayerSpeed(true, true, true, 0.f), 0.f);
    }

    TEST(NeutralPlayerSpeed, theClipSpeedsAreTheOnesTheMoverAndTheGaitUse)
    {
        EXPECT_FLOAT_EQ(MWPhysics::NeutralPath::kWalkSpeed, 82.45f);
        EXPECT_FLOAT_EQ(MWPhysics::NeutralPath::kRunSpeed, 350.87f);
    }
}
