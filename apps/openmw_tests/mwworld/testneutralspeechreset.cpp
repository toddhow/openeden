// Task 0273b (review finding 1): what a load or a cell unload does to the scene speech of the neutral world.
//
// In plain words: queued lines, the `Ptr` kept for each speaker and the mouth drivers belong to actors that live in a
// cell. When the game is loaded again, or the actor's cell unloads, the actor is freed: a line that stays queued would
// play from, and subtitle with the name of, an object that is gone. `clear()` drops everything (a load);
// `forgetNeutralSpeaker` drops one actor from the queue, the speaker table and the mouth table (a cell unload).
#include <apps/openmw/mwworld/neutralscenespeech.hpp>

#include <gtest/gtest.h>

#include <map>
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

    TEST(NeutralSpeechReset, clearDropsEveryQueuedLineAndSpeaker)
    {
        NeutralSceneSpeech speech;
        FakeSink sink;
        ASSERT_TRUE(speech.add(&a, "one", { "x/1.fuz" }));
        ASSERT_TRUE(speech.add(&b, "two", { "x/2.fuz" }));
        speech.update(sink); // both lines started
        ASSERT_TRUE(speech.add(&a, "later", { "x/3.fuz" }));
        ASSERT_TRUE(speech.busy(&a));
        speech.clear();
        EXPECT_EQ(speech.pending(), 0u);
        EXPECT_FALSE(speech.busy(&a));
        EXPECT_FALSE(speech.busy(&b));
        sink.mLog.clear();
        sink.mPlaying.clear();
        speech.update(sink);
        EXPECT_TRUE(sink.mLog.empty()) << "nothing may play after the reset";
    }

    TEST(NeutralSpeechReset, clearKeepsTheCountersOfWhatWasPlayed)
    {
        NeutralSceneSpeech speech;
        FakeSink sink;
        speech.add(&a, "one", { "x/1.fuz" });
        speech.update(sink);
        speech.clear();
        EXPECT_EQ(speech.linesStarted(), 1u);
        EXPECT_EQ(speech.filesStarted(), 1u);
    }

    TEST(NeutralSpeechReset, forgettingASpeakerRemovesItEverywhereAndLeavesTheOthers)
    {
        NeutralSceneSpeech speech;
        speech.add(&a, "one", { "x/1.fuz" });
        speech.add(&b, "two", { "x/2.fuz" });
        std::map<const void*, int> speakers{ { &a, 1 }, { &b, 2 } };
        std::map<const void*, int> mouths{ { &a, 1 }, { &b, 2 } };
        MWWorld::forgetNeutralSpeaker(&speech, speakers, mouths, &a);
        EXPECT_FALSE(speech.busy(&a));
        EXPECT_TRUE(speech.busy(&b));
        EXPECT_EQ(speakers.count(&a), 0u);
        EXPECT_EQ(mouths.count(&a), 0u);
        EXPECT_EQ(speakers.count(&b), 1u);
        EXPECT_EQ(mouths.count(&b), 1u);
    }

    TEST(NeutralSpeechReset, forgettingWithNoQueueYetIsHarmless)
    {
        std::map<const void*, int> speakers{ { &a, 1 } };
        std::map<const void*, int> mouths;
        MWWorld::forgetNeutralSpeaker<std::map<const void*, int>, std::map<const void*, int>>(
            nullptr, speakers, mouths, &a);
        EXPECT_TRUE(speakers.empty());
    }
}
