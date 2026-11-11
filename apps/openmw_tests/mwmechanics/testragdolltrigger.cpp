// Tests for the decision to ragdoll a dead actor (task 0106, stage b): apps/openmw/mwmechanics/ragdolltrigger.hpp.
// Written from the spec: a dead NPC gets one ragdoll if the setting is on; a corpse that was loaded dead, the player,
// a creature and an actor under a scripted animation get none.

#include "apps/openmw/mwmechanics/ragdolltrigger.hpp"

#include <gtest/gtest.h>

namespace
{
    using MWMechanics::RagdollTriggerInput;
    using MWMechanics::shouldStartActorRagdoll;

    RagdollTriggerInput deadNpc()
    {
        RagdollTriggerInput in;
        in.mSettingOn = true;
        in.mDead = true;
        in.mSawAliveHere = true;
        in.mIsNpc = true;
        in.mHasBodyAndBones = true;
        return in;
    }

    TEST(RagdollTrigger, aDeadNpcSeenAliveStartsOne)
    {
        EXPECT_TRUE(shouldStartActorRagdoll(deadNpc()));
    }

    TEST(RagdollTrigger, withTheSettingOffNothingEverStarts)
    {
        RagdollTriggerInput in = deadNpc();
        in.mSettingOn = false;
        EXPECT_FALSE(shouldStartActorRagdoll(in));
    }

    TEST(RagdollTrigger, aLivingActorStartsNothing)
    {
        RagdollTriggerInput in = deadNpc();
        in.mDead = false;
        EXPECT_FALSE(shouldStartActorRagdoll(in));
    }

    TEST(RagdollTrigger, aCorpseLoadedDeadStartsNothing)
    {
        RagdollTriggerInput in = deadNpc();
        in.mSawAliveHere = false;
        EXPECT_FALSE(shouldStartActorRagdoll(in));
    }

    TEST(RagdollTrigger, oneTryPerController)
    {
        RagdollTriggerInput in = deadNpc();
        in.mAlreadyTried = true;
        EXPECT_FALSE(shouldStartActorRagdoll(in));
    }

    TEST(RagdollTrigger, creaturesAndThePlayerGetNone)
    {
        RagdollTriggerInput creature = deadNpc();
        creature.mIsNpc = false;
        EXPECT_FALSE(shouldStartActorRagdoll(creature));
        RagdollTriggerInput player = deadNpc();
        player.mIsPlayer = true;
        EXPECT_FALSE(shouldStartActorRagdoll(player));
    }

    TEST(RagdollTrigger, noAnimationObjectOrAScriptedAnimationGetsNone)
    {
        RagdollTriggerInput noBones = deadNpc();
        noBones.mHasBodyAndBones = false;
        EXPECT_FALSE(shouldStartActorRagdoll(noBones));
        RagdollTriggerInput scripted = deadNpc();
        scripted.mScriptedAnimation = true;
        EXPECT_FALSE(shouldStartActorRagdoll(scripted));
    }

    TEST(RagdollTrigger, everyOtherCombinationOfTheEightFlagsIsRefusedExceptTheOne)
    {
        int accepted = 0;
        for (unsigned bits = 0; bits < 256; ++bits)
        {
            RagdollTriggerInput in;
            in.mSettingOn = bits & 1;
            in.mDead = bits & 2;
            in.mSawAliveHere = bits & 4;
            in.mAlreadyTried = bits & 8;
            in.mIsNpc = bits & 16;
            in.mIsPlayer = bits & 32;
            in.mHasBodyAndBones = bits & 64;
            in.mScriptedAnimation = bits & 128;
            if (shouldStartActorRagdoll(in))
                ++accepted;
        }
        EXPECT_EQ(accepted, 1);
    }
}
