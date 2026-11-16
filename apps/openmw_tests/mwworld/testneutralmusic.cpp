// Task 0209 (M-S7): the director of a game pack's music, driven by a fake source and a fake sink. Time is the sum of
// the dt given; every expected value is written out by hand.
#include <gtest/gtest.h>

#include <deque>
#include <limits>
#include <string>
#include <vector>

#include "apps/openmw/mwworld/neutralmusic.hpp"

namespace
{
    using namespace MWWorld;
    namespace sdk = openeden::sdk::game;

    struct FakeSource final : sdk::MusicSource
    {
        bool mReady = true;
        bool mHasType = true;
        sdk::MusicTypeDescription mType;
        std::deque<sdk::MusicPlan> mPlans; // the plans given out in order; the last one repeats
        bool mPlansFail = false;
        int mTypeAsks = 0;
        int mPlanAsks = 0;
        std::vector<std::uint32_t> mPicksSeen;

        bool musicEnabled() override { return true; }
        bool musicReady() override { return mReady; }
        bool musicType(const sdk::MusicContext&, sdk::MusicTypeDescription& out) override
        {
            ++mTypeAsks;
            out = mType;
            return mHasType;
        }
        bool musicPlan(sdk::WorldId, const sdk::MusicContext& context, sdk::MusicPlan& out) override
        {
            ++mPlanAsks;
            mPicksSeen.push_back(context.pick);
            if (mPlansFail || mPlans.empty())
                return false;
            out = mPlans.front();
            if (mPlans.size() > 1)
                mPlans.pop_front();
            return true;
        }
    };

    struct FakeSink final : NeutralMusicSink
    {
        struct Call
        {
            std::string what;
            std::string file;
            float fade = 0.f;
            friend bool operator==(const Call&, const Call&) = default;
        };
        std::vector<Call> mCalls;
        std::vector<std::string> mLog;
        bool mPlaying = false;
        bool mStartsPlaying = true;

        void stream(const std::string& path, float fade) override
        {
            mCalls.push_back({ "stream", path, fade });
            mPlaying = mStartsPlaying;
        }
        void fadeOut(float fade) override
        {
            mCalls.push_back({ "fadeOut", "", fade });
            mPlaying = false;
        }
        void claim() override { mCalls.push_back({ "claim", "", 0.f }); }
        bool playing() override { return mPlaying; }
        void log(const std::string& line) override { mLog.push_back(line); }

        int count(const std::string& what) const
        {
            int n = 0;
            for (const Call& c : mCalls)
                n += c.what == what ? 1 : 0;
            return n;
        }
    };

    sdk::MusicPlan fileplan(const std::string& name, const std::string& file)
    {
        sdk::MusicPlan p;
        p.trackName = name;
        sdk::MusicItem item;
        item.file = file;
        item.name = name;
        p.items.push_back(item);
        return p;
    }

    sdk::MusicPlan silence(float seconds)
    {
        sdk::MusicPlan p;
        p.trackName = "Silence";
        sdk::MusicItem item;
        item.kind = sdk::MusicItemKind::Silence;
        item.seconds = seconds;
        p.items.push_back(item);
        return p;
    }

    sdk::MusicTypeDescription type(sdk::WorldId id, const std::string& name, float fade, std::uint32_t flags = 0)
    {
        sdk::MusicTypeDescription t;
        t.id = id;
        t.editorId = name;
        t.fadeSeconds = fade;
        t.flags = flags;
        t.reason = "cell";
        return t;
    }

    void run(NeutralMusicDirector& d, float seconds, sdk::WorldId cell = 5, float dt = 0.1f)
    {
        const int steps = static_cast<int>(seconds / dt + 0.5f);
        for (int i = 0; i < steps; ++i)
            d.update(dt, cell, 12.f);
    }

    TEST(NeutralMusic, whileTheSourceIsNotReadyTheChannelIsClaimedAndNothingElseHappens)
    {
        FakeSource source;
        source.mReady = false;
        source.mPlans = { fileplan("A", "music/a.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 3.f);
        ASSERT_EQ(sink.mCalls.size(), 1u);
        EXPECT_EQ(sink.mCalls[0].what, "claim");
        EXPECT_EQ(source.mTypeAsks, 0);
        EXPECT_EQ(source.mPlanAsks, 0);
        source.mReady = true;
        source.mType = type(7, "T", 0.f);
        run(d, 0.5f);
        EXPECT_EQ(sink.count("claim"), 1) << "claimed once";
        EXPECT_EQ(sink.count("stream"), 1);
    }

    TEST(NeutralMusic, theFirstUpdateClaimsTheChannelAndStartsTheTypesFirstTrackWithItsFade)
    {
        FakeSource source;
        source.mType = type(7, "TypeTavern", 6.f);
        source.mPlans = { fileplan("A", "music/tavern/a.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        d.update(0.1f, 5, 12.f);
        ASSERT_EQ(sink.mCalls.size(), 2u);
        EXPECT_EQ(sink.mCalls[0].what, "claim");
        EXPECT_EQ(sink.mCalls[1], (FakeSink::Call{ "stream", "music/tavern/a.xwm", 6.f }));
        EXPECT_TRUE(d.hasType());
        EXPECT_EQ(d.picks(), 1u);
        ASSERT_GE(sink.mLog.size(), 3u);
        EXPECT_NE(sink.mLog[1].find("type TypeTavern"), std::string::npos);
        EXPECT_NE(sink.mLog[2].find("track A"), std::string::npos);
    }

    TEST(NeutralMusic, aTrackThatKeepsPlayingIsNotStartedAgain)
    {
        FakeSource source;
        source.mType = type(7, "T", 0.f);
        source.mPlans = { fileplan("A", "music/a.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 20.f);
        EXPECT_EQ(sink.count("stream"), 1);
        EXPECT_EQ(source.mPlanAsks, 1);
    }

    TEST(NeutralMusic, theTypeIsAskedAtMostEveryQuarterSecond)
    {
        FakeSource source;
        source.mType = type(7, "T", 0.f);
        source.mPlans = { fileplan("A", "music/a.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 10.f, 5, 0.0166667f);
        // The first ask at once, then one per 0.25 s: about 41 asks in 10 s, never one per frame (600).
        EXPECT_GE(source.mTypeAsks, 38);
        EXPECT_LE(source.mTypeAsks, 42);
    }

    TEST(NeutralMusic, whenTheFileEndsTheNextPlanIsAskedWithTheNextPickNumber)
    {
        FakeSource source;
        source.mType = type(7, "T", 0.f);
        source.mPlans = { fileplan("A", "music/a.xwm"), fileplan("B", "music/b.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 2.f);
        EXPECT_EQ(sink.count("stream"), 1);
        sink.mPlaying = false; // the file ended
        run(d, 1.f);
        EXPECT_EQ(sink.count("stream"), 2);
        EXPECT_EQ(sink.mCalls.back(), (FakeSink::Call{ "stream", "music/b.xwm", 0.f }));
        ASSERT_EQ(source.mPicksSeen.size(), 2u);
        EXPECT_EQ(source.mPicksSeen[0], 0u);
        EXPECT_EQ(source.mPicksSeen[1], 1u);
    }

    TEST(NeutralMusic, aNewTypeStreamsItsTrackWithItsOwnFade)
    {
        FakeSource source;
        source.mType = type(7, "Calm", 3.f);
        source.mPlans = { fileplan("A", "music/calm.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 2.f);
        source.mType = type(8, "Combat", 9.f);
        source.mPlans = { fileplan("C", "music/combat.xwm") };
        run(d, 0.6f);
        EXPECT_EQ(sink.mCalls.back(), (FakeSink::Call{ "stream", "music/combat.xwm", 9.f }));
        EXPECT_EQ(d.type().editorId, "Combat");
        // And back: the calm type streams again with ITS fade.
        source.mType = type(7, "Calm", 3.f);
        source.mPlans = { fileplan("A", "music/calm.xwm") };
        run(d, 0.6f);
        EXPECT_EQ(sink.mCalls.back(), (FakeSink::Call{ "stream", "music/calm.xwm", 3.f }));
    }

    TEST(NeutralMusic, aSilenceWaitsItsSecondsAndThenTheNextPlanStarts)
    {
        FakeSource source;
        source.mType = type(7, "T", 0.f);
        source.mPlans = { silence(2.f), fileplan("A", "music/a.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 1.8f);
        EXPECT_EQ(sink.count("stream"), 0);
        run(d, 0.6f); // 2.4 s in: the 2 s silence is over
        EXPECT_EQ(sink.count("stream"), 1);
    }

    TEST(NeutralMusic, aSilenceFirstFadesTheOldMusicOutWhenTheTypeChanges)
    {
        FakeSource source;
        source.mType = type(7, "Calm", 0.f);
        source.mPlans = { fileplan("A", "music/a.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 1.f);
        source.mType = type(9, "Quiet", 4.f);
        source.mPlans = { silence(30.f) };
        run(d, 0.6f);
        EXPECT_EQ(sink.mCalls.back(), (FakeSink::Call{ "fadeOut", "", 4.f }));
    }

    TEST(NeutralMusic, aTypeThatPlaysOneSelectionStopsAskingForPlansAfterIt)
    {
        FakeSource source;
        source.mType = type(7, "Once", 0.f, 0x01);
        source.mPlans = { fileplan("A", "music/a.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 1.f);
        sink.mPlaying = false;
        run(d, 5.f);
        EXPECT_TRUE(d.done());
        EXPECT_EQ(sink.count("stream"), 1);
        EXPECT_EQ(source.mPlanAsks, 1);
        // A different type starts again.
        source.mType = type(8, "Other", 0.f);
        run(d, 0.6f);
        EXPECT_EQ(sink.count("stream"), 2);
    }

    TEST(NeutralMusic, aPlaceWithNoTypeFadesTheMusicOutOnceAndAComeBackPlaysAgain)
    {
        FakeSource source;
        source.mType = type(7, "T", 5.f);
        source.mPlans = { fileplan("A", "music/a.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 1.f);
        source.mHasType = false;
        run(d, 2.f);
        EXPECT_EQ(sink.count("fadeOut"), 1);
        EXPECT_FALSE(d.hasType());
        source.mHasType = true;
        run(d, 0.6f);
        EXPECT_EQ(sink.count("stream"), 2);
    }

    TEST(NeutralMusic, aTypeWithNoPlayablePlanIsRetriedAfterFiveSeconds)
    {
        FakeSource source;
        source.mType = type(7, "T", 0.f);
        source.mPlansFail = true;
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        run(d, 4.5f);
        EXPECT_EQ(source.mPlanAsks, 1);
        run(d, 1.f);
        EXPECT_EQ(source.mPlanAsks, 2);
        EXPECT_EQ(sink.count("stream"), 0);
    }

    TEST(NeutralMusic, filesThatEndAtOnceMakeTheDirectorWaitInsteadOfSpinning)
    {
        FakeSource source;
        source.mType = type(7, "T", 0.f);
        source.mPlans = { fileplan("A", "music/a.xwm") };
        FakeSink sink;
        sink.mStartsPlaying = false; // the stream never starts (no device, an unreadable file)
        NeutralMusicDirector d(source, sink);
        run(d, 20.f);
        // Three quick failures (0.5 s each), then 5 s of waiting, and so on: far fewer streams than one per second.
        EXPECT_LE(sink.count("stream"), 12);
        EXPECT_GE(sink.count("stream"), 3);
    }

    TEST(NeutralMusic, twoDirectorsGivenTheSameInputsWriteTheSameLog)
    {
        std::vector<std::string> logs[2];
        std::vector<FakeSink::Call> calls[2];
        for (int i = 0; i < 2; ++i)
        {
            FakeSource source;
            source.mType = type(7, "T", 2.f);
            source.mPlans = { silence(1.f), fileplan("A", "music/a.xwm"), fileplan("B", "music/b.xwm") };
            FakeSink sink;
            NeutralMusicDirector d(source, sink);
            for (int k = 0; k < 20; ++k)
            {
                run(d, 1.5f, 5, 0.0166667f);
                sink.mPlaying = false;
            }
            logs[i] = sink.mLog;
            calls[i] = sink.mCalls;
        }
        EXPECT_EQ(logs[0], logs[1]);
        EXPECT_EQ(calls[0], calls[1]);
        EXPECT_FALSE(logs[0].empty());
    }

    TEST(NeutralMusic, aHostileDtChangesNothing)
    {
        FakeSource source;
        source.mType = type(7, "T", 0.f);
        source.mPlans = { fileplan("A", "music/a.xwm") };
        FakeSink sink;
        NeutralMusicDirector d(source, sink);
        d.update(-1.f, 5, 12.f);
        d.update(std::numeric_limits<float>::quiet_NaN(), 5, 12.f);
        EXPECT_TRUE(sink.mCalls.empty());
    }
}
