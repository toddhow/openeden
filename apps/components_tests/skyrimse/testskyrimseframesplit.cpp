// Task 0323: the pack's GameSimulation callback is one trace scope in the engine (phase.GameSimulation) and a pack
// cannot use the engine's tracer (its own copy of the flag), so the pack splits that callback itself: the Papyrus
// host's frame against everything else, in steady-clock microseconds that match the engine's trace. A developer aid
// (environment variable), off by default; off it records nothing and makes no text. Written before the header.
#include <game_skyrimse/frame_split.hpp>

#include <gtest/gtest.h>

namespace
{
    using game_skyrimse::FrameSplit;

    TEST(SkyrimFrameSplit, offRecordsNothingAndMakesNoText)
    {
        FrameSplit split(false);
        EXPECT_FALSE(split.on());
        EXPECT_TRUE(split.add(0, 9000, 20000).empty());
        EXPECT_TRUE(split.summary().empty());
        EXPECT_EQ(split.frames(), 0u);
    }

    TEST(SkyrimFrameSplit, aQuickFrameCountsButMakesNoLine)
    {
        FrameSplit split(true, 5000);
        EXPECT_TRUE(split.add(1000, 1100, 1300).empty());
        EXPECT_EQ(split.frames(), 1u);
        EXPECT_EQ(split.papyrusMicroseconds(), 100);
        EXPECT_EQ(split.restMicroseconds(), 200);
    }

    TEST(SkyrimFrameSplit, aSlowFrameMakesALineWithTheAbsoluteTimesAndBothParts)
    {
        FrameSplit split(true, 5000);
        const std::string line = split.add(2000000, 2007000, 2008000);
        EXPECT_EQ(line, "SKYFRAME b=2000000 e=2008000 papyrus=7000 rest=1000");
        EXPECT_EQ(split.slowFrames(), 1u);
        EXPECT_EQ(split.worstMicroseconds(), 8000);
    }

    TEST(SkyrimFrameSplit, theSlowLimitIsInclusiveOnTheTotal)
    {
        FrameSplit split(true, 5000);
        EXPECT_TRUE(split.add(0, 1000, 4999).empty());
        EXPECT_FALSE(split.add(0, 1000, 5000).empty());
    }

    TEST(SkyrimFrameSplit, theSummaryHasTotalsWorstAndTheSlowCount)
    {
        FrameSplit split(true, 5000);
        split.add(0, 100, 200);
        split.add(0, 6000, 6500);
        const std::string text = split.summary();
        EXPECT_NE(text.find("SKYFRAME summary: 2 frames"), std::string::npos) << text;
        EXPECT_NE(text.find("papyrus 6100 us"), std::string::npos) << text;
        EXPECT_NE(text.find("rest 600 us"), std::string::npos) << text;
        EXPECT_NE(text.find("worst 6500 us"), std::string::npos) << text;
        EXPECT_NE(text.find("1 over 5000 us"), std::string::npos) << text;
    }

    TEST(SkyrimFrameSplit, aBackwardsClockIsClampedToZeroNotNegative)
    {
        FrameSplit split(true, 5000);
        split.add(100, 50, 40);
        EXPECT_EQ(split.papyrusMicroseconds(), 0);
        EXPECT_EQ(split.restMicroseconds(), 0);
    }

    TEST(SkyrimFrameSplit, theEnvironmentTextTurnsItOn)
    {
        EXPECT_FALSE(FrameSplit::wanted(nullptr));
        EXPECT_FALSE(FrameSplit::wanted(""));
        EXPECT_FALSE(FrameSplit::wanted("0"));
        EXPECT_TRUE(FrameSplit::wanted("1"));
    }

    TEST(SkyrimSectionMarks, stretchesAreTheGapsBetweenMarksAndSameNamesAdd)
    {
        game_skyrimse::SectionMarks marks;
        marks.start(1000);
        marks.mark("flush", 1300);
        marks.mark("place", 1350);
        marks.mark("flush", 1400);
        EXPECT_EQ(marks.total(), 400);
        EXPECT_EQ(marks.line(), "SKYPAPYRUS b=1000 total=400 flush=350 place=50");
    }

    TEST(SkyrimSectionMarks, aNewFrameForgetsTheOldOneAndAClockStepBackIsZero)
    {
        game_skyrimse::SectionMarks marks;
        marks.start(0);
        marks.mark("a", 10);
        marks.start(100);
        marks.mark("b", 90);
        EXPECT_EQ(marks.line(), "SKYPAPYRUS b=100 total=0 b=0");
    }

    TEST(SkyrimSectionMarks, onlyTheFirstSixteenNamesAreKept)
    {
        game_skyrimse::SectionMarks marks;
        marks.start(0);
        static const char* const names[] = { "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "b0", "b1",
            "b2", "b3", "b4", "b5", "b6", "b7" };
        std::int64_t t = 0;
        for (const char* name : names)
            marks.mark(name, t += 10);
        EXPECT_EQ(marks.total(), 180);
        const std::string line = marks.line();
        EXPECT_NE(line.find("b5=10"), std::string::npos);
        EXPECT_EQ(line.find("b6="), std::string::npos);
    }
}
