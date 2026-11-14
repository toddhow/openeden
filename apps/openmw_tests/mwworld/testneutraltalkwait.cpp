// Task 0135 stage S6g: the wait between activating a neutral actor and the conversation box (a sleeper must stand up
// first, the box pauses the world). Pure: the session tells NeutralTalkWait each frame whether the pack says the actor
// can talk.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "apps/openmw/mwworld/neutraldialogue.hpp"

namespace
{
    using MWWorld::NeutralTalkWait;
    using Result = NeutralTalkWait::Result;

    TEST(NeutralTalkWait, waitsWhileTheActorIsNotReady)
    {
        NeutralTalkWait wait;
        wait.begin();
        for (int i = 0; i < 60; ++i)
            EXPECT_EQ(wait.step(1.f / 60.f, false), Result::Waiting);
        EXPECT_TRUE(wait.active());
    }

    TEST(NeutralTalkWait, startsTheTalkAsSoonAsTheActorIsReady)
    {
        NeutralTalkWait wait;
        wait.begin();
        EXPECT_EQ(wait.step(0.5f, false), Result::Waiting);
        EXPECT_EQ(wait.step(0.5f, true), Result::Ready);
        EXPECT_FALSE(wait.active());
        EXPECT_EQ(wait.step(0.5f, true), Result::Waiting); // nothing is waited for any more: no second start
    }

    TEST(NeutralTalkWait, anActorThatIsReadyAtOnceStartsOnTheFirstStep)
    {
        NeutralTalkWait wait;
        wait.begin();
        EXPECT_EQ(wait.step(0.f, true), Result::Ready);
    }

    TEST(NeutralTalkWait, aBrokenClipDoesNotBlockTheTalkForever)
    {
        NeutralTalkWait wait;
        wait.begin();
        float t = 0.f;
        Result r = Result::Waiting;
        while (r == Result::Waiting && t < 60.f)
        {
            r = wait.step(0.1f, false);
            t += 0.1f;
        }
        EXPECT_EQ(r, Result::TimedOut);
        EXPECT_NEAR(t, NeutralTalkWait::kMaxSeconds, 0.2f);
        EXPECT_FALSE(wait.active());
    }

    TEST(NeutralTalkWait, readyWinsOverTheTimeoutInTheSameStep)
    {
        NeutralTalkWait wait;
        wait.begin();
        EXPECT_EQ(wait.step(NeutralTalkWait::kMaxSeconds + 1.f, true), Result::Ready);
    }

    TEST(NeutralTalkWait, pausedAndBadTimeNeverCountsTowardsTheTimeout)
    {
        NeutralTalkWait wait;
        wait.begin();
        for (int i = 0; i < 1000; ++i)
        {
            EXPECT_EQ(wait.step(0.f, false), Result::Waiting); // a paused game passes 0
            EXPECT_EQ(wait.step(-1.f, false), Result::Waiting);
            EXPECT_EQ(wait.step(std::numeric_limits<float>::quiet_NaN(), false), Result::Waiting);
            EXPECT_EQ(wait.step(std::numeric_limits<float>::infinity(), false), Result::Waiting);
        }
        EXPECT_FLOAT_EQ(wait.elapsed(), 0.f);
    }

    TEST(NeutralTalkWait, canBeBegunAgainAfterItEnded)
    {
        NeutralTalkWait wait;
        wait.begin();
        EXPECT_EQ(wait.step(0.f, true), Result::Ready);
        wait.begin();
        EXPECT_TRUE(wait.active());
        EXPECT_FLOAT_EQ(wait.elapsed(), 0.f);
        wait.clear();
        EXPECT_FALSE(wait.active());
        EXPECT_EQ(wait.step(10.f, false), Result::Waiting);
    }
}
