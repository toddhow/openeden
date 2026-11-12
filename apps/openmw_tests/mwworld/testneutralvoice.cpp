// Task 0143: the voice lines of a neutral conversation turn are played one after the other. Pure: no sound manager.
// Written before the queue.
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutraldialogue.hpp"

namespace
{
    using MWWorld::NeutralVoiceQueue;

    TEST(NeutralVoiceQueue, anEmptyQueueStartsNothing)
    {
        NeutralVoiceQueue queue;
        EXPECT_FALSE(queue.poll(false).has_value());
        EXPECT_FALSE(queue.poll(true).has_value());
        EXPECT_EQ(queue.pending(), 0u);
    }

    TEST(NeutralVoiceQueue, theFirstLineStartsAtOnceAndTheNextWaitsForItToEnd)
    {
        NeutralVoiceQueue queue;
        queue.set({ "voice/a.fuz", "voice/b.fuz" });
        EXPECT_EQ(queue.pending(), 2u);
        EXPECT_EQ(queue.poll(false), std::optional<std::string>("voice/a.fuz"));
        EXPECT_FALSE(queue.poll(true).has_value());
        EXPECT_FALSE(queue.poll(true).has_value());
        EXPECT_EQ(queue.poll(false), std::optional<std::string>("voice/b.fuz"));
        EXPECT_FALSE(queue.poll(false).has_value());
        EXPECT_EQ(queue.started(), 2u);
        EXPECT_EQ(queue.pending(), 0u);
    }

    TEST(NeutralVoiceQueue, aLineThatCouldNotPlayLetsTheNextOneStart)
    {
        // The session reports "not playing" right after a start that failed (the decoder refused the file).
        NeutralVoiceQueue queue;
        queue.set({ "a", "b", "c" });
        EXPECT_EQ(queue.poll(false), std::optional<std::string>("a"));
        EXPECT_EQ(queue.poll(false), std::optional<std::string>("b"));
        EXPECT_EQ(queue.poll(false), std::optional<std::string>("c"));
        EXPECT_FALSE(queue.poll(false).has_value());
    }

    TEST(NeutralVoiceQueue, aNewTurnDropsWhatWasPending)
    {
        NeutralVoiceQueue queue;
        queue.set({ "a", "b" });
        EXPECT_EQ(queue.poll(false), std::optional<std::string>("a"));
        queue.set({ "c" });
        EXPECT_EQ(queue.pending(), 1u);
        EXPECT_EQ(queue.poll(false), std::optional<std::string>("c"));
        EXPECT_FALSE(queue.poll(false).has_value());
    }

    TEST(NeutralVoiceQueue, clearDropsEverythingAndStartedCountsOnlyTheCurrentTurn)
    {
        NeutralVoiceQueue queue;
        queue.set({ "a", "b" });
        EXPECT_EQ(queue.poll(false), std::optional<std::string>("a"));
        EXPECT_EQ(queue.started(), 1u);
        queue.clear();
        EXPECT_EQ(queue.pending(), 0u);
        EXPECT_EQ(queue.started(), 0u);
        EXPECT_FALSE(queue.poll(false).has_value());
    }

    TEST(NeutralVoiceQueue, emptyPathsAreSkippedAndTheNumberOfLinesIsCapped)
    {
        NeutralVoiceQueue queue;
        std::vector<std::string> many(100, "x");
        many.insert(many.begin(), "");
        queue.set(many);
        EXPECT_LE(queue.pending(), 64u);
        EXPECT_EQ(queue.poll(false), std::optional<std::string>("x"));
    }
}
