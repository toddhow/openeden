// Task 0194: the scene lines of a game pack are played one after the other per actor (voice files in order, the
// subtitle when the line starts). A fake sink stands for the sound manager.
#include <apps/openmw/mwworld/neutralscenespeech.hpp>

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace
{
    using MWWorld::NeutralSceneSpeech;

    struct FakeSink final : NeutralSceneSpeech::Sink
    {
        bool active(const void* actor) override { return mPlaying.count(actor) != 0; }
        void say(const void* actor, const std::string& path) override
        {
            mLog.push_back("say " + path);
            mPlaying.insert(actor);
        }
        void subtitle(const void*, const std::string& text) override { mLog.push_back("sub " + text); }
        std::set<const void*> mPlaying;
        std::vector<std::string> mLog;
    };

    const int a = 0;
    const int b = 0;

    TEST(NeutralSceneSpeech, aLineShowsItsSubtitleAndPlaysItsFilesInOrder)
    {
        NeutralSceneSpeech speech;
        FakeSink sink;
        ASSERT_TRUE(speech.add(&a, "hello", { "x/1.fuz", "x/2.fuz" }));
        speech.update(sink);
        EXPECT_EQ(sink.mLog, (std::vector<std::string>{ "sub hello", "say x/1.fuz" }));
        speech.update(sink); // the first file still plays
        EXPECT_EQ(sink.mLog.size(), 2u);
        sink.mPlaying.clear();
        speech.update(sink);
        EXPECT_EQ(sink.mLog.back(), "say x/2.fuz");
        sink.mPlaying.clear();
        speech.update(sink);
        EXPECT_EQ(speech.pending(), 0u);
        EXPECT_EQ(speech.linesStarted(), 1u);
        EXPECT_EQ(speech.filesStarted(), 2u);
    }

    TEST(NeutralSceneSpeech, theNextLineWaitsForTheLastFileOfTheFirst)
    {
        NeutralSceneSpeech speech;
        FakeSink sink;
        speech.add(&a, "one", { "x/1.fuz" });
        speech.add(&a, "two", { "x/2.fuz" });
        speech.update(sink);
        speech.update(sink);
        EXPECT_EQ(speech.linesStarted(), 1u);
        sink.mPlaying.clear();
        speech.update(sink);
        EXPECT_EQ(speech.linesStarted(), 2u);
        EXPECT_EQ(sink.mLog.back(), "say x/2.fuz");
    }

    TEST(NeutralSceneSpeech, aLineWithNoVoiceShowsOnlyItsSubtitle)
    {
        NeutralSceneSpeech speech;
        FakeSink sink;
        speech.add(&a, "quiet", {});
        speech.update(sink);
        EXPECT_EQ(sink.mLog, (std::vector<std::string>{ "sub quiet" }));
        EXPECT_EQ(speech.pending(), 0u);
    }

    TEST(NeutralSceneSpeech, twoActorsSpeakAtTheSameTime)
    {
        NeutralSceneSpeech speech;
        FakeSink sink;
        speech.add(&a, "one", { "x/1.fuz" });
        speech.add(&b, "two", { "x/2.fuz" });
        speech.update(sink);
        EXPECT_EQ(speech.filesStarted(), 2u);
    }

    TEST(NeutralSceneSpeech, aFullQueueDropsAndCountsAndRemoveForgetsTheActor)
    {
        NeutralSceneSpeech speech;
        for (std::size_t i = 0; i < NeutralSceneSpeech::kMaxQueuedLines; ++i)
            ASSERT_TRUE(speech.add(&a, "x", {}));
        EXPECT_FALSE(speech.add(&a, "y", {}));
        EXPECT_EQ(speech.dropped(), 1u);
        speech.remove(&a);
        EXPECT_EQ(speech.pending(), 0u);
    }
}
