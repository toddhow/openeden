// Task 0243: a fight cry or taunt that finds no line backs off (a named gap of ours) instead of asking again on every
// update, and the pickpocket and crime kinds. No game data. Written against stubs first and run red (see the stage
// log).
#include <game_skyrimse/barks.hpp>
#include <game_skyrimse/crime.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse::barks;
    using CrimeKind = game_skyrimse::crime::Kind;

    struct BackoffRig
    {
        struct Call
        {
            Kind mKind;
            float mClock;
        };
        std::vector<Call> mCalls;
        std::vector<Actor> mActors;
        std::unique_ptr<BarkSystem> mSystem;
        bool mHaveLine = false;
        std::size_t mSpokenLines = 0;

        explicit BackoffRig(Settings settings = Settings())
        {
            settings.mIdleMin = settings.mIdleMax = 1.0e6f; // no chatter
            Hooks hooks;
            hooks.mChoose = [this](Kind kind, std::uint64_t, std::uint32_t, Line& out) {
                mCalls.push_back({ kind, mSystem->clock() });
                if (!mHaveLine)
                    return false;
                out.mText = "Die!";
                out.mVoice = { "voice/skyrim.esm/x/y.fuz" };
                return true;
            };
            hooks.mSpeak = [this](std::uint64_t, const Line&) {
                ++mSpokenLines;
                return true;
            };
            mSystem = std::make_unique<BarkSystem>(settings, std::move(hooks));
            mActors.push_back({ 5, 300.0, 0.0, 0.0, false });
        }
        void step(float seconds = 0.1f) { mSystem->update(seconds, mActors, true, 0.0, 0.0, 0.0); }
        void run(float seconds)
        {
            for (float t = 0.f; t < seconds; t += 0.1f)
                step(0.1f);
        }
        std::vector<float> clocksOf(Kind kind) const
        {
            std::vector<float> clocks;
            for (const Call& call : mCalls)
                if (call.mKind == kind)
                    clocks.push_back(call.mClock);
            return clocks;
        }
        void engage()
        {
            step();
            mSystem->onEvent(Kind::Engage, 5);
            step();
        }
    };

    TEST(SkyrimBarkBackoff, theGapIsAPositiveNamedSetting)
    {
        EXPECT_GT(Settings().mNoLineGap, 0.f);
    }

    TEST(SkyrimBarkBackoff, aBlowCryThatFindsNoLineAsksAgainOnlyAfterTheGap)
    {
        BackoffRig rig;
        rig.engage();
        rig.run(60.f);
        const std::vector<float> clocks = rig.clocksOf(Kind::Attack);
        ASSERT_GE(clocks.size(), 2u);
        for (std::size_t i = 1; i < clocks.size(); ++i)
            EXPECT_GE(clocks[i] - clocks[i - 1], rig.mSystem->settings().mNoLineGap - 0.15f) << i;
        // 60 s of 0.1 s updates, a due timer that fails every update before: hundreds of questions
        EXPECT_LE(clocks.size(), 60u / 4u);
    }

    TEST(SkyrimBarkBackoff, aTauntThatFindsNoLineAsksAgainOnlyAfterTheGap)
    {
        BackoffRig rig;
        rig.engage();
        rig.run(80.f);
        const std::vector<float> clocks = rig.clocksOf(Kind::Taunt);
        ASSERT_GE(clocks.size(), 2u);
        for (std::size_t i = 1; i < clocks.size(); ++i)
            EXPECT_GE(clocks[i] - clocks[i - 1], rig.mSystem->settings().mNoLineGap - 0.15f) << i;
    }

    TEST(SkyrimBarkBackoff, aCryTriesAgainAfterTheGapAndSpeaksOnceALineExists)
    {
        BackoffRig rig;
        rig.engage();
        rig.run(20.f);
        EXPECT_EQ(rig.mSpokenLines, 0u);
        rig.mHaveLine = true;
        rig.run(rig.mSystem->settings().mNoLineGap + 1.f);
        EXPECT_GE(rig.mSpokenLines, 1u);
    }

    TEST(SkyrimBarkBackoff, aLineThatIsSaidKeepsTheNormalTimers)
    {
        BackoffRig rig;
        rig.mHaveLine = true;
        rig.engage();
        rig.run(60.f);
        const std::vector<float> clocks = rig.clocksOf(Kind::Attack);
        ASSERT_GE(clocks.size(), 3u);
        // fCombatDialogueAttack 5 to 10 s: never closer than the minimum
        for (std::size_t i = 1; i < clocks.size(); ++i)
            EXPECT_GE(clocks[i] - clocks[i - 1], rig.mSystem->settings().mAttackMin - 0.15f);
    }

    TEST(SkyrimBarkBackoff, aRefusedSpeakAlsoBacksOff)
    {
        BackoffRig rig;
        rig.mHaveLine = true;
        Hooks hooks;
        std::vector<float> clocks;
        hooks.mChoose = [&](Kind kind, std::uint64_t, std::uint32_t, Line& out) {
            if (kind == Kind::Attack)
                clocks.push_back(rig.mSystem->clock());
            out.mText = "x";
            out.mVoice = { "voice/a.fuz" };
            return true;
        };
        hooks.mSpeak = [](std::uint64_t, const Line&) { return false; };
        Settings settings;
        settings.mIdleMin = settings.mIdleMax = 1.0e6f;
        rig.mSystem = std::make_unique<BarkSystem>(settings, std::move(hooks));
        rig.engage();
        rig.run(40.f);
        ASSERT_GE(clocks.size(), 2u);
        for (std::size_t i = 1; i < clocks.size(); ++i)
            EXPECT_GE(clocks[i] - clocks[i - 1], rig.mSystem->settings().mNoLineGap - 0.15f);
    }

    // ---- a reaction asks again when a pick finds no line (a line gated by a random roll)

    TEST(SkyrimBarkBackoff, aReactionTriesAnotherPickWhenTheFirstFindsNoLine)
    {
        int calls = 0;
        Hooks hooks;
        hooks.mChoose = [&](Kind, std::uint64_t, std::uint32_t, Line& out) {
            if (calls++ < 2)
                return false;
            out.mText = "Who is there?";
            out.mVoice = { "voice/a.fuz" };
            return true;
        };
        hooks.mSpeak = [](std::uint64_t, const Line&) { return true; };
        Settings settings;
        settings.mIdleMin = settings.mIdleMax = 1.0e6f;
        BarkSystem system(settings, std::move(hooks));
        const std::vector<Actor> actors{ { 5, 300.0, 0.0, 0.0, false } };
        system.update(0.1f, actors, true, 0.0, 0.0, 0.0);
        system.onEvent(Kind::Notice, 5);
        system.update(0.1f, actors, true, 0.0, 0.0, 0.0);
        EXPECT_EQ(system.stats().mSpoken[static_cast<int>(Kind::Notice)], 1u);
        EXPECT_EQ(calls, 3);
        EXPECT_EQ(system.stats().mNoLine, 0u) << "counted only when the last try fails";
    }

    TEST(SkyrimBarkBackoff, aReactionThatNeverFindsALineStopsAfterAFewPicksAndIsCountedOnce)
    {
        int calls = 0;
        std::vector<std::string> trace;
        Hooks hooks;
        hooks.mChoose = [&](Kind, std::uint64_t, std::uint32_t, Line&) {
            ++calls;
            return false;
        };
        hooks.mSpeak = [](std::uint64_t, const Line&) { return true; };
        hooks.mTrace = [&](const std::string& line) { trace.push_back(line); };
        Settings settings;
        settings.mIdleMin = settings.mIdleMax = 1.0e6f;
        BarkSystem system(settings, std::move(hooks));
        const std::vector<Actor> actors{ { 5, 300.0, 0.0, 0.0, false } };
        system.update(0.1f, actors, true, 0.0, 0.0, 0.0);
        system.onEvent(Kind::Steal, 5);
        system.update(0.1f, actors, true, 0.0, 0.0, 0.0);
        EXPECT_GE(calls, 2);
        EXPECT_LE(calls, 8);
        EXPECT_EQ(system.stats().mNoLine, 1u);
        EXPECT_EQ(trace.size(), 1u);
    }

    // ---- the crime kinds

    TEST(SkyrimBarkCrimeKinds, everyCrimeMapsToItsOwnSubtype)
    {
        EXPECT_EQ(subtypeOf(kindOfCrime(CrimeKind::Theft)), "STEA");
        EXPECT_EQ(subtypeOf(kindOfCrime(CrimeKind::Assault)), "ASSA");
        EXPECT_EQ(subtypeOf(kindOfCrime(CrimeKind::Murder)), "MURD");
        EXPECT_EQ(subtypeOf(kindOfCrime(CrimeKind::Trespass)), "TRES");
    }

    TEST(SkyrimBarkCrimeKinds, aCaughtPickpocketUsesThePickpocketSubtypeOfTheData)
    {
        EXPECT_EQ(subtypeOf(kindOfCrime(CrimeKind::Pickpocket)), "PICN");
        EXPECT_EQ(nameOf(kindOfCrime(CrimeKind::Pickpocket)), "pickpocket");
        EXPECT_EQ(subtypeOf(Kind::PickpocketCombat), "PICC");
        EXPECT_TRUE(isCombat(Kind::Pickpocket));
    }

    TEST(SkyrimBarkCrimeKinds, aPickpocketWitnessSpeaksItsLineAndTriesSeveralFilesLikeAnyReaction)
    {
        BackoffRig rig;
        int calls = 0;
        Hooks hooks;
        std::vector<Kind> kinds;
        hooks.mChoose = [&](Kind kind, std::uint64_t, std::uint32_t, Line& out) {
            kinds.push_back(kind);
            out.mText = "Thief!";
            if (calls++ >= 2)
                out.mVoice = { "voice/a.fuz" };
            return true;
        };
        hooks.mSpeak = [&](std::uint64_t, const Line&) { return true; };
        Settings settings;
        settings.mIdleMin = settings.mIdleMax = 1.0e6f;
        rig.mSystem = std::make_unique<BarkSystem>(settings, std::move(hooks));
        rig.step();
        rig.mSystem->onEvent(Kind::Pickpocket, 5);
        rig.step();
        EXPECT_EQ(rig.mSystem->stats().mSpoken[static_cast<int>(Kind::Pickpocket)], 1u);
        EXPECT_EQ(kinds.size(), 3u) << "two lines without a file, then one with";
    }
}
