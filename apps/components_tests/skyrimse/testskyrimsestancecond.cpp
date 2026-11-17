// Task 0235: the player's stance (sneaking, drawn weapon or spell) answers the conversation conditions IsSneaking (fn
// 286), IsWeaponOut (263) and IsWeaponMagicOut (101), so the hello lines that care about a drawn weapon and the idle
// lines that care about a sneaking player are chosen by their own conditions. Written before the code and run red
// against a stub. No game data.
#include <game_skyrimse/dialogue.hpp>

#include <gtest/gtest.h>

#include <span>

namespace
{
    using namespace game_skyrimse;

    constexpr FormId kNpc = 0x01000200;

    survey::Ctda cond(
        std::uint16_t function, std::uint32_t runOn, std::uint8_t compare, float value, std::uint32_t reference = 0)
    {
        survey::Ctda c;
        c.mFunction = function;
        c.mRunOn = runOn;
        c.mCompare = compare;
        c.mComparison = value;
        c.mReference = reference;
        return c;
    }

    struct StanceRig
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };

        StanceRig()
        {
            DialogueSpeaker s;
            s.mBase = kNpc;
            mWorld.setSpeaker(s);
        }
        bool holds(const survey::Ctda& c)
        {
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    constexpr std::uint32_t kTarget = 1; // run-on: the player, as the hello lines use it
    constexpr std::uint32_t kReference = 2; // run-on: a reference; the idle lines name the player's, 0x14
    constexpr std::uint32_t kPlayerRef = 0x14;
    constexpr std::uint8_t kEqual = 0;

    TEST(SkyrimStanceCondition, withNothingToldNobodySneaksAndNothingIsDrawn)
    {
        StanceRig rig;
        EXPECT_FALSE(rig.holds(cond(286, kReference, kEqual, 1.f, kPlayerRef)));
        EXPECT_TRUE(rig.holds(cond(263, kTarget, kEqual, 0.f)));
        EXPECT_FALSE(rig.holds(cond(101, kTarget, kEqual, 1.f)));
    }

    TEST(SkyrimStanceCondition, aSneakingPlayerAnswersIsSneakingOnTheReferenceAndTheTarget)
    {
        StanceRig rig;
        rig.mWorld.setPlayerStance(true, 0);
        EXPECT_TRUE(rig.holds(cond(286, kReference, kEqual, 1.f, kPlayerRef)));
        EXPECT_TRUE(rig.holds(cond(286, kTarget, kEqual, 1.f)));
    }

    TEST(SkyrimStanceCondition, theSpeakerDoesNotSneakBecauseThePlayerDoes)
    {
        StanceRig rig;
        rig.mWorld.setPlayerStance(true, 0);
        EXPECT_FALSE(rig.holds(cond(286, 0, kEqual, 1.f)));
    }

    TEST(SkyrimStanceCondition, aDrawnWeaponIsWeaponOutTwoAndMagicOutOne)
    {
        StanceRig rig;
        rig.mWorld.setPlayerStance(false, 1);
        EXPECT_TRUE(rig.holds(cond(263, kTarget, kEqual, 2.f)));
        EXPECT_FALSE(rig.holds(cond(263, kTarget, kEqual, 0.f)));
        EXPECT_TRUE(rig.holds(cond(101, kTarget, kEqual, 1.f)));
    }

    TEST(SkyrimStanceCondition, aDrawnSpellIsMagicOutButNotWeaponOut)
    {
        StanceRig rig;
        rig.mWorld.setPlayerStance(false, 2);
        EXPECT_TRUE(rig.holds(cond(263, kTarget, kEqual, 0.f)));
        EXPECT_TRUE(rig.holds(cond(101, kTarget, kEqual, 1.f)));
    }

    TEST(SkyrimStanceCondition, theStanceIsTheLatestOneTold)
    {
        StanceRig rig;
        rig.mWorld.setPlayerStance(true, 1);
        rig.mWorld.setPlayerStance(false, 0);
        EXPECT_FALSE(rig.holds(cond(286, kTarget, kEqual, 1.f)));
        EXPECT_FALSE(rig.holds(cond(101, kTarget, kEqual, 1.f)));
    }

    TEST(SkyrimStanceCondition, aNonsenseDrawStateIsNothingDrawn)
    {
        StanceRig rig;
        rig.mWorld.setPlayerStance(false, 200);
        EXPECT_TRUE(rig.holds(cond(263, kTarget, kEqual, 0.f)));
        EXPECT_FALSE(rig.holds(cond(101, kTarget, kEqual, 1.f)));
    }

    TEST(SkyrimStanceCondition, theWorldCountsTheStanceFunctionsAsAnswered)
    {
        StanceRig rig;
        EXPECT_TRUE(rig.mWorld.answers(cond(263, kTarget, kEqual, 2.f)));
        EXPECT_TRUE(rig.mWorld.answers(cond(101, kTarget, kEqual, 1.f)));
        EXPECT_TRUE(rig.mWorld.answers(cond(286, kTarget, kEqual, 1.f)));
    }
}
