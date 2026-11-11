// Tests for task 0106, second slice (3): apps/openmw/mwmechanics/corpserest.hpp, the decision to move a dead actor to
// where its ragdoll came to rest. Written from the spec before the code.
//
// In plain words: while the ragdoll still moves, nothing happens. When every body is asleep, the actor is moved once to
// the pelvis' x and y and to the ground below it, unless that is hardly a move at all, or so far that it must be a
// glitch, or no ground was found.

#include "apps/openmw/mwmechanics/corpserest.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using MWMechanics::CorpseRestDecision;
    using MWMechanics::CorpseRestInput;
    using MWMechanics::decideCorpseRest;

    constexpr double kUnits = 69.99125109;

    CorpseRestInput lying()
    {
        CorpseRestInput in;
        in.mSettled = true;
        in.mActor[0] = 1000.0;
        in.mActor[1] = -2000.0;
        in.mActor[2] = 130.0; // just above the ground the ragdoll lies on
        in.mPelvis[0] = 1000.0 + 3.0 * kUnits;
        in.mPelvis[1] = -2000.0 - 1.0 * kUnits;
        in.mPelvis[2] = 140.0;
        in.mHasGround = true;
        in.mGroundZ = 120.0;
        return in;
    }

    TEST(CorpseRest, whileTheRagdollStillMovesNothingHappensAndTheCorpseIsNotDone)
    {
        CorpseRestInput in = lying();
        in.mSettled = false;
        const CorpseRestDecision d = decideCorpseRest(in);
        EXPECT_FALSE(d.mMove);
        EXPECT_FALSE(d.mDone);
    }

    TEST(CorpseRest, aSettledRagdollMovesTheActorToThePelvisXYAndTheGroundBelow)
    {
        const CorpseRestInput in = lying();
        const CorpseRestDecision d = decideCorpseRest(in);
        EXPECT_TRUE(d.mMove);
        EXPECT_TRUE(d.mDone);
        EXPECT_DOUBLE_EQ(d.mPosition[0], in.mPelvis[0]);
        EXPECT_DOUBLE_EQ(d.mPosition[1], in.mPelvis[1]);
        EXPECT_DOUBLE_EQ(d.mPosition[2], in.mGroundZ);
    }

    TEST(CorpseRest, aCorpseAlreadyHandledIsLeftAlone)
    {
        CorpseRestInput in = lying();
        in.mAlreadyDone = true;
        const CorpseRestDecision d = decideCorpseRest(in);
        EXPECT_FALSE(d.mMove);
        EXPECT_TRUE(d.mDone);
    }

    TEST(CorpseRest, aRagdollThatEndedUpNextToTheActorIsNotWorthAMove)
    {
        CorpseRestInput in = lying();
        in.mPelvis[0] = in.mActor[0] + 0.2 * kUnits;
        in.mPelvis[1] = in.mActor[1];
        const CorpseRestDecision d = decideCorpseRest(in);
        EXPECT_FALSE(d.mMove);
        EXPECT_TRUE(d.mDone);
    }

    TEST(CorpseRest, noGroundBelowMeansNoMoveButTheCorpseIsDone)
    {
        CorpseRestInput in = lying();
        in.mHasGround = false;
        const CorpseRestDecision d = decideCorpseRest(in);
        EXPECT_FALSE(d.mMove);
        EXPECT_TRUE(d.mDone);
    }

    TEST(CorpseRest, aRagdollFarAwayIsTakenForAGlitchAndTheActorStays)
    {
        CorpseRestInput in = lying();
        in.mPelvis[0] = in.mActor[0] + 60.0 * kUnits;
        const CorpseRestDecision d = decideCorpseRest(in);
        EXPECT_FALSE(d.mMove);
        EXPECT_TRUE(d.mDone);
    }

    TEST(CorpseRest, theBoundariesOfTheShiftAreHonoured)
    {
        CorpseRestInput in = lying();
        in.mPelvis[1] = in.mActor[1];
        in.mPelvis[0] = in.mActor[0] + 0.36 * kUnits;
        EXPECT_TRUE(decideCorpseRest(in).mMove);
        in.mPelvis[0] = in.mActor[0] + 0.34 * kUnits;
        EXPECT_FALSE(decideCorpseRest(in).mMove);
        in.mPelvis[0] = in.mActor[0] + 39.0 * kUnits;
        EXPECT_TRUE(decideCorpseRest(in).mMove);
        in.mPelvis[0] = in.mActor[0] + 41.0 * kUnits;
        EXPECT_FALSE(decideCorpseRest(in).mMove);
    }

    TEST(CorpseRest, aSettledActorStandingOverItsRagdollButAboveTheGroundIsLowered)
    {
        // Close in x and y but the actor hangs 3 m above the ground the ragdoll lies on: that is a move too.
        CorpseRestInput in = lying();
        in.mPelvis[0] = in.mActor[0];
        in.mPelvis[1] = in.mActor[1];
        in.mActor[2] = in.mGroundZ + 3.0 * kUnits;
        const CorpseRestDecision d = decideCorpseRest(in);
        EXPECT_TRUE(d.mMove);
        EXPECT_DOUBLE_EQ(d.mPosition[2], in.mGroundZ);
    }
}
