// Task 0231: the bark rules (when a Skyrim NPC speaks without being talked to) over a fake world. No game data.
#include <game_skyrimse/barks.hpp>
#include <game_skyrimse/combat_stats.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::barks;

    struct Said
    {
        Kind mKind;
        std::uint64_t mReference;
        float mAt;
    };

    /// The system under test with hooks that record what happens.
    struct BarkRig
    {
        std::vector<Said> mSaid;
        std::vector<std::string> mTrace;
        bool mHaveLine = true;
        bool mHaveVoice = true;
        bool mAccept = true;
        std::uint32_t mLastPick = 0;
        std::vector<std::uint32_t> mPicks;
        std::vector<Actor> mActors;
        std::unique_ptr<BarkSystem> mSystem;
        double mPx = 0.0, mPy = 0.0, mPz = 0.0;
        bool mHavePlayer = true;
        bool mReady = true;

        explicit BarkRig(Settings settings = Settings())
        {
            Hooks hooks;
            hooks.mChoose = [this](Kind, std::uint64_t, std::uint32_t pick, Line& out) {
                mPicks.push_back(pick);
                if (!mHaveLine)
                    return false;
                out.mText = "Hello there, traveller.";
                if (mHaveVoice)
                    out.mVoice = { "voice/skyrim.esm/x/y.fuz" };
                else
                    out.mVoice.clear();
                return true;
            };
            hooks.mSpeak = [this](std::uint64_t reference, const Line&) {
                if (!mAccept)
                    return false;
                mSaid.push_back({ Kind::Count, reference, mSystem->clock() });
                return true;
            };
            hooks.mTrace = [this](const std::string& line) { mTrace.push_back(line); };
            hooks.mReady = [this] { return mReady; };
            mSystem = std::make_unique<BarkSystem>(settings, std::move(hooks));
        }

        void actor(std::uint64_t reference, double x, double y = 0.0, bool dead = false)
        {
            for (Actor& a : mActors)
                if (a.mReference == reference)
                {
                    a.mX = x;
                    a.mY = y;
                    a.mDead = dead;
                    return;
                }
            mActors.push_back({ reference, x, y, 0.0, dead });
        }

        void step(float seconds = 0.1f) { mSystem->update(seconds, mActors, mHavePlayer, mPx, mPy, mPz); }
        void run(float seconds)
        {
            for (float t = 0.f; t < seconds; t += 0.1f)
                step(0.1f);
        }
        std::size_t said() const { return mSaid.size(); }
        std::size_t spoken(Kind kind) const
        {
            return static_cast<std::size_t>(mSystem->stats().mSpoken[static_cast<int>(kind)]);
        }
    };

    TEST(SkyrimBarks, everyKindNamesItsSubtype)
    {
        EXPECT_EQ(subtypeOf(Kind::Hello), "HELO");
        EXPECT_EQ(subtypeOf(Kind::Idle), "IDLE");
        EXPECT_EQ(subtypeOf(Kind::Engage), "NOTC");
        EXPECT_EQ(subtypeOf(Kind::EngageAlerted), "ALTC");
        EXPECT_EQ(subtypeOf(Kind::Attack), "ATCK");
        EXPECT_EQ(subtypeOf(Kind::Taunt), "TAUT");
        EXPECT_EQ(subtypeOf(Kind::Hit), "HIT_");
        EXPECT_EQ(subtypeOf(Kind::Flee), "FLEE");
        EXPECT_EQ(subtypeOf(Kind::Death), "DETH");
        EXPECT_EQ(subtypeOf(Kind::Lost), "LOTN");
        EXPECT_EQ(subtypeOf(Kind::Calm), "COTN");
        EXPECT_EQ(subtypeOf(Kind::Assault), "ASSA");
        EXPECT_EQ(subtypeOf(Kind::Steal), "STEA");
        EXPECT_EQ(subtypeOf(Kind::Trespass), "TRES");
        EXPECT_TRUE(subtypeOf(Kind::Count).empty());
        EXPECT_EQ(nameOf(Kind::Hello), "hello");
        EXPECT_EQ(nameOf(Kind::Death), "death");
    }

    TEST(SkyrimBarks, combatKindsAreTheFightOnes)
    {
        EXPECT_FALSE(isCombat(Kind::Hello));
        EXPECT_FALSE(isCombat(Kind::Idle));
        EXPECT_TRUE(isCombat(Kind::Engage));
        EXPECT_TRUE(isCombat(Kind::Attack));
        EXPECT_TRUE(isCombat(Kind::Death));
        EXPECT_TRUE(isCombat(Kind::Calm));
    }

    TEST(SkyrimBarks, noTableGivesTheGameDefaultsAndATableReplacesThem)
    {
        const Settings d = fromGameSettings(nullptr);
        EXPECT_FLOAT_EQ(d.mHelloGap, 5.f);
        EXPECT_FLOAT_EQ(d.mIdleMin, 10.f);
        EXPECT_FLOAT_EQ(d.mIdleMax, 20.f);
        EXPECT_FLOAT_EQ(d.mTauntMax, 30.f);
        SettingsTable table;
        table.set("fAIGreetingTimer", 7.f);
        table.set("fCombatDialogueAttackMinElapsedTime", 2.f);
        table.set("fCombatDialogueAttackMaxElapsedTime", 3.f);
        table.set("fGameplayVoiceFilePadding", 0.25f);
        const Settings s = fromGameSettings(&table);
        EXPECT_FLOAT_EQ(s.mHelloGap, 7.f);
        EXPECT_FLOAT_EQ(s.mAttackMin, 2.f);
        EXPECT_FLOAT_EQ(s.mAttackMax, 3.f);
        EXPECT_FLOAT_EQ(s.mVoicePadding, 0.25f);
        EXPECT_FLOAT_EQ(s.mIdleMin, 10.f); // not in the table: the default stays
    }

    TEST(SkyrimBarks, anActorGreetsWhenThePlayerComesNear)
    {
        BarkRig rig;
        rig.actor(1, 1000.0);
        rig.run(1.f);
        EXPECT_EQ(rig.said(), 0u);
        rig.actor(1, 300.0);
        rig.step();
        ASSERT_EQ(rig.said(), 1u);
        EXPECT_EQ(rig.mSaid[0].mReference, 1u);
        EXPECT_EQ(rig.spoken(Kind::Hello), 1u);
        rig.run(2.f); // still near: no second hello
        EXPECT_EQ(rig.spoken(Kind::Hello), 1u);
    }

    TEST(SkyrimBarks, anActorAlreadyNearWhenFirstSeenDoesNotGreet)
    {
        BarkRig rig;
        rig.actor(1, 100.0);
        rig.run(3.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 0u);
    }

    TEST(SkyrimBarks, theSameActorGreetsAgainOnlyAfterTheResetTime)
    {
        BarkRig rig;
        rig.actor(1, 1000.0);
        rig.step();
        rig.actor(1, 300.0);
        rig.step();
        ASSERT_EQ(rig.spoken(Kind::Hello), 1u);
        rig.actor(1, 1000.0); // leaves and returns soon: nothing
        rig.step();
        rig.actor(1, 300.0);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Hello), 1u);
        rig.actor(1, 1000.0);
        rig.run(rig.mSystem->settings().mHelloReset + 1.f);
        rig.actor(1, 300.0);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Hello), 2u);
    }

    TEST(SkyrimBarks, twoActorsComingNearTogetherGreetOneAtATimeFiveSecondsApart)
    {
        BarkRig rig;
        rig.actor(1, 1000.0);
        rig.actor(2, 1000.0);
        rig.step();
        rig.actor(1, 350.0);
        rig.actor(2, 300.0);
        rig.step();
        ASSERT_EQ(rig.said(), 1u);
        EXPECT_EQ(rig.mSaid[0].mReference, 2u); // the nearer first
        rig.run(4.f);
        EXPECT_EQ(rig.said(), 1u);
        rig.run(2.f);
        ASSERT_EQ(rig.said(), 2u);
        EXPECT_EQ(rig.mSaid[1].mReference, 1u);
        EXPECT_GE(rig.mSaid[1].mAt - rig.mSaid[0].mAt, rig.mSystem->settings().mHelloGap - 0.01f);
    }

    TEST(SkyrimBarks, aDeadActorOrOneOnAnotherFloorDoesNotGreet)
    {
        BarkRig rig;
        rig.actor(1, 1000.0);
        rig.actor(2, 1000.0);
        rig.step();
        rig.actor(1, 300.0, 0.0, true);
        rig.actor(2, 300.0);
        rig.mActors[1].mZ = 900.0;
        rig.step();
        EXPECT_EQ(rig.said(), 0u);
    }

    TEST(SkyrimBarks, idleChatterComesAfterTheGamesIntervalAndOnlyWhenTheActorIsClose)
    {
        BarkRig rig;
        rig.actor(1, 600.0);
        rig.actor(2, 3000.0);
        rig.run(9.f);
        EXPECT_EQ(rig.said(), 0u);
        rig.run(11.5f); // past the longest interval
        EXPECT_GE(rig.spoken(Kind::Idle), 1u);
        ASSERT_GE(rig.said(), 1u);
        EXPECT_EQ(rig.mSaid[0].mReference, 1u);
        EXPECT_GE(rig.mSaid[0].mAt, 10.f - 0.01f);
        EXPECT_LE(rig.mSaid[0].mAt, 20.2f);
    }

    TEST(SkyrimBarks, noPlayerMeansSilence)
    {
        BarkRig rig;
        rig.mHavePlayer = false;
        rig.actor(1, 300.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.run(30.f);
        EXPECT_EQ(rig.said(), 0u);
    }

    TEST(SkyrimBarks, anEngagementShoutsOnceAndMarksTheActorEngaged)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Engage), 1u);
        EXPECT_TRUE(rig.mSystem->engaged(1));
        EXPECT_EQ(rig.mSystem->engagedCount(), 1u);
    }

    TEST(SkyrimBarks, anAlertedEngagementUsesItsOwnKind)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.mSystem->onEvent(Kind::EngageAlerted, 1);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::EngageAlerted), 1u);
        EXPECT_EQ(rig.spoken(Kind::Engage), 0u);
    }

    TEST(SkyrimBarks, aFarFighterStaysSilentButIsEngaged)
    {
        BarkRig rig;
        rig.actor(1, 9000.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.run(40.f);
        EXPECT_EQ(rig.said(), 0u);
        EXPECT_TRUE(rig.mSystem->engaged(1));
    }

    TEST(SkyrimBarks, anEngagedFighterAttacksAndTauntsOnTheGamesTimers)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.run(36.f);
        EXPECT_GE(rig.spoken(Kind::Attack), 2u);
        EXPECT_LE(rig.spoken(Kind::Attack), 7u);
        EXPECT_GE(rig.spoken(Kind::Taunt), 1u);
        EXPECT_LE(rig.spoken(Kind::Taunt), 3u);
    }

    TEST(SkyrimBarks, anEngagedActorDoesNotGreetOrChat)
    {
        BarkRig rig;
        rig.actor(1, 1000.0);
        rig.step();
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.actor(1, 300.0);
        rig.run(25.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 0u);
        EXPECT_EQ(rig.spoken(Kind::Idle), 0u);
    }

    TEST(SkyrimBarks, twoShoutsInOneUpdateAreOneBecauseOfTheCombatGap)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.actor(2, 500.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.mSystem->onEvent(Kind::Engage, 2);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Engage), 1u);
        EXPECT_TRUE(rig.mSystem->engaged(2)); // still engaged, only the shout was skipped
    }

    TEST(SkyrimBarks, aHurtActorSaysAHitLineNoMoreOftenThanTheHitGap)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.mSystem->onEvent(Kind::Hit, 1);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Hit), 1u);
        rig.mSystem->onEvent(Kind::Hit, 1);
        rig.run(1.f);
        EXPECT_EQ(rig.spoken(Kind::Hit), 1u);
        rig.run(4.f);
        rig.mSystem->onEvent(Kind::Hit, 1);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Hit), 2u);
    }

    TEST(SkyrimBarks, fleeingDeathLostAndCalmEachSpeakOnce)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.actor(2, 600.0);
        rig.actor(3, 700.0);
        rig.actor(4, 800.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.mSystem->onEvent(Kind::Engage, 2);
        rig.run(3.f);
        rig.mSystem->onEvent(Kind::Flee, 1);
        rig.run(3.f);
        rig.mSystem->onEvent(Kind::Death, 2);
        rig.run(3.f);
        rig.mSystem->onEvent(Kind::Engage, 3);
        rig.run(3.f);
        rig.mSystem->onEvent(Kind::Lost, 3);
        rig.run(3.f);
        rig.mSystem->onEvent(Kind::Engage, 4);
        rig.run(3.f);
        rig.mSystem->onEvent(Kind::Calm, 4);
        rig.run(3.f);
        EXPECT_EQ(rig.spoken(Kind::Flee), 1u);
        EXPECT_EQ(rig.spoken(Kind::Death), 1u);
        EXPECT_EQ(rig.spoken(Kind::Lost), 1u);
        EXPECT_EQ(rig.spoken(Kind::Calm), 1u);
    }

    TEST(SkyrimBarks, deathAndCalmEndTheEngagement)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.actor(2, 500.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.run(1.f);
        rig.mSystem->onEvent(Kind::Engage, 2);
        rig.run(1.f);
        ASSERT_EQ(rig.mSystem->engagedCount(), 2u);
        rig.mSystem->onEvent(Kind::Death, 1);
        rig.mSystem->onEvent(Kind::Calm, 2);
        rig.run(1.f);
        EXPECT_EQ(rig.mSystem->engagedCount(), 0u);
    }

    TEST(SkyrimBarks, aLineWithNoVoiceFileIsNotSaidAndCounted)
    {
        BarkRig rig;
        rig.mHaveVoice = false;
        rig.actor(1, 500.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.step();
        EXPECT_EQ(rig.said(), 0u);
        EXPECT_EQ(rig.mSystem->stats().mNoVoice, 1u);
    }

    TEST(SkyrimBarks, noPassingLineAndARefusedSpeakAreCounted)
    {
        BarkRig rig;
        rig.mHaveLine = false;
        rig.actor(1, 500.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.step();
        EXPECT_EQ(rig.mSystem->stats().mNoLine, 1u);
        BarkRig refusing;
        refusing.mAccept = false;
        refusing.actor(1, 500.0);
        refusing.mSystem->onEvent(Kind::Engage, 1);
        refusing.step();
        EXPECT_EQ(refusing.mSystem->stats().mRefused, 1u);
        EXPECT_EQ(refusing.spoken(Kind::Engage), 0u);
    }

    TEST(SkyrimBarks, aFailedCalmBarkIsTriedAgainSoonForTheSameActor)
    {
        BarkRig rig;
        rig.mHaveLine = false;
        rig.actor(1, 1000.0);
        rig.step();
        rig.actor(1, 300.0);
        rig.step();
        EXPECT_EQ(rig.mSystem->stats().mNoLine, 1u);
        rig.mHaveLine = true;
        rig.run(rig.mSystem->settings().mRetry + 0.5f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 1u);
    }

    TEST(SkyrimBarks, anActorThatIsStillSpeakingDoesNotStartAnotherLine)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.step();
        ASSERT_EQ(rig.spoken(Kind::Engage), 1u);
        rig.mSystem->onEvent(Kind::Flee, 1);
        rig.step(0.1f); // the engage line is still running
        EXPECT_EQ(rig.spoken(Kind::Flee), 0u);
    }

    TEST(SkyrimBarks, theQueueIsCappedAndTheDropsAreCounted)
    {
        Settings s;
        s.mMaxEvents = 4;
        BarkRig rig(s);
        for (std::uint64_t i = 1; i <= 10; ++i)
            rig.mSystem->onEvent(Kind::Hit, i);
        EXPECT_EQ(rig.mSystem->stats().mEventsDropped, 6u);
    }

    TEST(SkyrimBarks, theSameInputsGiveTheSamePicksAndTimes)
    {
        auto play = [](BarkRig& rig) {
            rig.actor(1, 500.0);
            rig.actor(2, 700.0);
            rig.mSystem->onEvent(Kind::Engage, 1);
            for (int i = 0; i < 400; ++i)
                rig.step(0.1f);
        };
        BarkRig a, b;
        play(a);
        play(b);
        ASSERT_EQ(a.mSaid.size(), b.mSaid.size());
        for (std::size_t i = 0; i < a.mSaid.size(); ++i)
        {
            EXPECT_EQ(a.mSaid[i].mReference, b.mSaid[i].mReference);
            EXPECT_FLOAT_EQ(a.mSaid[i].mAt, b.mSaid[i].mAt);
        }
        EXPECT_EQ(a.mPicks, b.mPicks);
        Settings other;
        other.mSeed = 99;
        BarkRig c(other);
        play(c);
        EXPECT_NE(a.mPicks, c.mPicks);
    }

    TEST(SkyrimBarks, nothingRunsAndTheClockStandsStillUntilTheWorldIsReady)
    {
        BarkRig rig;
        rig.mReady = false;
        rig.actor(1, 1000.0);
        rig.run(5.f);
        EXPECT_FLOAT_EQ(rig.mSystem->clock(), 0.f);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.step();
        EXPECT_EQ(rig.mSystem->engagedCount(), 0u); // a moment of a fight before the data is read is dropped
        rig.mReady = true;
        rig.step();
        EXPECT_GT(rig.mSystem->clock(), 0.f);
        EXPECT_EQ(rig.said(), 0u);
    }

    TEST(SkyrimBarks, aStartTimeHoldsEverythingBackSoTheTimelineIsTheSameWhenevertheWorldIsReady)
    {
        auto run = [](float readyAt) {
            Settings s;
            s.mStartAt = 20.f;
            BarkRig rig(s);
            rig.mReady = false;
            rig.actor(1, 600.0);
            float t = 0.f;
            for (int i = 0; i < 600; ++i)
            {
                if (t >= readyAt)
                    rig.mReady = true;
                rig.step(0.1f);
                t += 0.1f;
            }
            std::vector<float> times;
            for (const Said& said : rig.mSaid)
                times.push_back(said.mAt);
            return times;
        };
        const std::vector<float> early = run(1.f);
        const std::vector<float> late = run(15.f);
        ASSERT_FALSE(early.empty());
        EXPECT_EQ(early, late); // ready before the start time: the same moments either way
        for (const float at : early)
            EXPECT_GE(at, 0.f);
        BarkRig rig([] {
            Settings s;
            s.mStartAt = 20.f;
            return s;
        }());
        rig.actor(1, 600.0);
        rig.run(19.f);
        EXPECT_FLOAT_EQ(rig.mSystem->clock(), 0.f); // the bark clock has not started
        rig.run(2.f);
        EXPECT_GT(rig.mSystem->clock(), 0.f);
    }

    TEST(SkyrimBarks, theTraceLinesCarryNoClockSoRunsCompare)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.mSystem->onEvent(Kind::Engage, 1);
        rig.step();
        ASSERT_EQ(rig.mTrace.size(), 1u);
        EXPECT_EQ(rig.mTrace[0], "BARK engage 1 voice/skyrim.esm/x/y.fuz \"Hello there, traveller.\"");
    }

    TEST(SkyrimBarks, aReactionKindSpeaksFromItsOwnSubtypeOnRequest)
    {
        BarkRig rig;
        rig.actor(1, 500.0);
        rig.mSystem->onEvent(Kind::Assault, 1);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Assault), 1u);
        EXPECT_FALSE(rig.mSystem->engaged(1)); // a witness is not a fighter
    }
}
