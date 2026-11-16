// Task 0235: the reactions of the bark rules: an NPC that notices a sneaking player (NOTA), a witness of a murder
// (MURD), a player who draws a weapon near it (the stance edge), over a fake world. No game data. Written before the
// code and run red against stubs (see the stage log of the task).
#include <game_skyrimse/barks.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse::barks;

    struct ReactRig
    {
        std::vector<std::pair<Kind, std::uint64_t>> mChosen;
        std::vector<std::uint64_t> mSpoken;
        std::vector<std::string> mTrace;
        std::vector<Actor> mActors;
        std::unique_ptr<BarkSystem> mSystem;
        bool mHaveLine = true;
        int mNoVoiceCalls = 0; ///< the first this many lines chosen have no voice file
        int mCalls = 0;

        explicit ReactRig(Settings settings = Settings())
        {
            settings.mIdleMin = settings.mIdleMax = 1.0e6f; // no chatter to confuse the counts
            Hooks hooks;
            hooks.mChoose = [this](Kind kind, std::uint64_t reference, std::uint32_t, Line& out) {
                mChosen.push_back({ kind, reference });
                if (!mHaveLine)
                    return false;
                out.mText = "What was that?";
                if (mCalls++ >= mNoVoiceCalls)
                    out.mVoice = { "voice/skyrim.esm/x/y.fuz" };
                return true;
            };
            hooks.mSpeak = [this](std::uint64_t reference, const Line&) {
                mSpoken.push_back(reference);
                return true;
            };
            hooks.mTrace = [this](const std::string& line) { mTrace.push_back(line); };
            mSystem = std::make_unique<BarkSystem>(settings, std::move(hooks));
        }
        void actor(std::uint64_t reference, double x, double y = 0.0)
        {
            for (Actor& a : mActors)
                if (a.mReference == reference)
                {
                    a.mX = x;
                    a.mY = y;
                    return;
                }
            mActors.push_back({ reference, x, y, 0.0, false });
        }
        void step(float seconds = 0.1f) { mSystem->update(seconds, mActors, true, 0.0, 0.0, 0.0); }
        void run(float seconds)
        {
            for (float t = 0.f; t < seconds; t += 0.1f)
                step(0.1f);
        }
        std::size_t spoken(Kind kind) const
        {
            return static_cast<std::size_t>(mSystem->stats().mSpoken[static_cast<int>(kind)]);
        }
    };

    TEST(SkyrimBarkReactions, theNewKindsHaveTheirSubtypesAndNames)
    {
        EXPECT_EQ(subtypeOf(Kind::Notice), "NOTA");
        EXPECT_EQ(subtypeOf(Kind::Murder), "MURD");
        EXPECT_EQ(nameOf(Kind::Notice), "notice");
        EXPECT_EQ(nameOf(Kind::Murder), "murder");
        EXPECT_FALSE(isCombat(Kind::Notice));
        EXPECT_TRUE(isCombat(Kind::Murder));
        EXPECT_TRUE(subtypeOf(Kind::Count).empty());
        EXPECT_GT(kKindCount, static_cast<std::size_t>(Kind::Murder));
        EXPECT_GT(kKindCount, static_cast<std::size_t>(Kind::Notice));
    }

    TEST(SkyrimBarkReactions, anActorThatNoticesTheSneakingPlayerSaysItsNoticeLine)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.step();
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 1u);
        ASSERT_EQ(rig.mChosen.size(), 1u);
        EXPECT_EQ(rig.mChosen[0].first, Kind::Notice);
        EXPECT_EQ(rig.mChosen[0].second, 5u);
    }

    TEST(SkyrimBarkReactions, theSameActorDoesNotNoticeAgainWithinTheGap)
    {
        Settings s;
        s.mNoticeGap = 20.f;
        ReactRig rig(s);
        rig.actor(5, 300.0);
        rig.actor(6, 400.0);
        rig.step();
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        rig.run(5.f); // its line is over
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 1u) << "within the gap";
        rig.mSystem->onEvent(Kind::Notice, 6);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 2u) << "another actor has its own gap";
        rig.run(21.f);
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 3u) << "after the gap";
    }

    TEST(SkyrimBarkReactions, aNoticeThatFindsNoLineDoesNotStartTheGap)
    {
        Settings s;
        s.mNoticeGap = 20.f;
        ReactRig rig(s);
        rig.actor(5, 300.0);
        rig.step();
        rig.mHaveLine = false;
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 0u);
        rig.mHaveLine = true;
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 1u);
    }

    TEST(SkyrimBarkReactions, aFarActorIsNotHeardNoticing)
    {
        ReactRig rig;
        rig.actor(5, 3000.0);
        rig.step();
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 0u);
        EXPECT_TRUE(rig.mChosen.empty());
    }

    TEST(SkyrimBarkReactions, anActorThatIsFightingDoesNotSayItsNoticeLine)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.step();
        rig.mSystem->onEvent(Kind::Engage, 5);
        rig.step();
        const std::size_t before = rig.mChosen.size();
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 0u);
        EXPECT_EQ(rig.mChosen.size(), before);
    }

    TEST(SkyrimBarkReactions, aWitnessOfAMurderCriesOut)
    {
        ReactRig rig;
        rig.actor(5, 500.0);
        rig.step();
        rig.mSystem->onEvent(Kind::Murder, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Murder), 1u);
    }

    TEST(SkyrimBarkReactions, aNoticeWaitsForTheLineThatIsStillRunning)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.step();
        rig.mSystem->onEvent(Kind::Assault, 5);
        rig.step();
        ASSERT_EQ(rig.spoken(Kind::Assault), 1u);
        rig.run(1.0f); // the combat gap is over, the line is not
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 0u);
    }

    // ---- the player's stance (a drawn weapon) -------------------------------------------------------------------

    TEST(SkyrimBarkReactions, drawingAWeaponNearAnActorMakesItSayAHelloAgain)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.actor(6, 3000.0);
        rig.step(); // first sight: near, no hello
        rig.run(2.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 0u);
        rig.mSystem->onPlayerStance(true);
        rig.step();
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Hello), 1u) << "the near actor comments";
        ASSERT_FALSE(rig.mChosen.empty());
        EXPECT_EQ(rig.mChosen[0].second, 5u);
    }

    TEST(SkyrimBarkReactions, aStanceThatDoesNotChangeSaysNothing)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.step();
        rig.run(2.f);
        rig.mSystem->onPlayerStance(false);
        rig.run(2.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 0u);
        rig.mSystem->onPlayerStance(true);
        rig.run(2.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 1u);
        rig.mSystem->onPlayerStance(true); // still drawn
        rig.run(10.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 1u);
    }

    TEST(SkyrimBarkReactions, aPutAwayWeaponSaysNothingAndAFreshDrawIsHeardAgainAfterTheReset)
    {
        Settings s;
        s.mStanceGap = 30.f;
        ReactRig rig(s);
        rig.actor(5, 300.0);
        rig.step();
        rig.mSystem->onPlayerStance(true);
        rig.run(2.f);
        ASSERT_EQ(rig.spoken(Kind::Hello), 1u);
        rig.mSystem->onPlayerStance(false);
        rig.run(2.f);
        rig.mSystem->onPlayerStance(true); // drawn again soon: the same actor does not repeat itself
        rig.run(2.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 1u);
        rig.mSystem->onPlayerStance(false);
        rig.run(31.f);
        rig.mSystem->onPlayerStance(true);
        rig.run(2.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 2u);
    }

    TEST(SkyrimBarkReactions, aStanceEdgeNeedsAPlayer)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.step();
        rig.mSystem->onPlayerStance(true);
        rig.mSystem->update(0.1f, rig.mActors, false, 0.0, 0.0, 0.0);
        rig.run(2.f);
        // the edge was consumed while there was no player: nothing is said later for it
        EXPECT_EQ(rig.spoken(Kind::Hello), 0u);
    }

    TEST(SkyrimBarkReactions, aSneakingPlayerIsNotGreetedUntilHeStandsUp)
    {
        ReactRig rig;
        rig.actor(5, 900.0);
        rig.mSystem->onPlayerStance(false, true);
        rig.step();
        rig.actor(5, 300.0); // comes near while the player sneaks
        rig.run(3.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 0u);
        rig.mSystem->onPlayerStance(false, false); // he stands up
        rig.run(3.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 1u);
    }

    TEST(SkyrimBarkReactions, aSneakingPlayerWhoDrawsAWeaponIsNotGreetedEither)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.mSystem->onPlayerStance(false, true);
        rig.step();
        rig.mSystem->onPlayerStance(true, true);
        rig.run(3.f);
        EXPECT_EQ(rig.spoken(Kind::Hello), 0u);
    }

    TEST(SkyrimBarkReactions, aNoticeIsStillSaidToASneakingPlayer)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.mSystem->onPlayerStance(false, true);
        rig.step();
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 1u);
    }

    TEST(SkyrimBarkReactions, aReactionWhoseFirstLineHasNoVoiceTriesAnotherLine)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.step();
        rig.mNoVoiceCalls = 2; // the first two picks have no voice file
        rig.mSystem->onEvent(Kind::Notice, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Notice), 1u);
        EXPECT_EQ(rig.mChosen.size(), 3u);
        EXPECT_EQ(rig.mSystem->stats().mNoVoice, 2u);
    }

    TEST(SkyrimBarkReactions, aReactionGivesUpAfterAFewLinesWithNoVoice)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.step();
        rig.mNoVoiceCalls = 1000;
        rig.mSystem->onEvent(Kind::Steal, 5);
        rig.step();
        EXPECT_EQ(rig.spoken(Kind::Steal), 0u);
        EXPECT_LE(rig.mChosen.size(), 8u) << "a bounded number of tries";
        EXPECT_GE(rig.mChosen.size(), 2u);
    }

    TEST(SkyrimBarkReactions, aHelloWithNoVoiceIsNotRetriedAtOnce)
    {
        ReactRig rig;
        rig.actor(5, 900.0);
        rig.step();
        rig.actor(5, 300.0);
        rig.mNoVoiceCalls = 1000;
        rig.step();
        EXPECT_EQ(rig.mChosen.size(), 1u) << "the calm barks keep their own retry after a second";
    }

    TEST(SkyrimBarkReactions, aFighterWhoseBlowCriesFindNoLineLeavesNoTraceEveryFrame)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.step();
        rig.mSystem->onEvent(Kind::Engage, 5);
        rig.step();
        rig.mHaveLine = false; // no usable line for its voice type from now on
        rig.mTrace.clear();
        rig.run(40.f); // the blow cry and taunt timers come due and fail on every update from then on
        std::size_t misses = 0;
        for (const std::string& line : rig.mTrace)
            if (line.find("no line passes") != std::string::npos || line.find("no voice file") != std::string::npos)
                ++misses;
        EXPECT_EQ(misses, 0u) << "a periodic cry that finds no line is not a trace line every frame";
        EXPECT_GT(rig.mSystem->stats().mNoLine, 0u) << "it still tried";
    }

    TEST(SkyrimBarkReactions, aReactionThatFindsNoLineLeavesOneTraceLine)
    {
        ReactRig rig;
        rig.actor(5, 300.0);
        rig.step();
        rig.mHaveLine = false;
        rig.mSystem->onEvent(Kind::Steal, 5);
        rig.step();
        std::size_t misses = 0;
        for (const std::string& line : rig.mTrace)
            if (line.find("no line passes") != std::string::npos)
                ++misses;
        EXPECT_EQ(misses, 1u);
    }
}
