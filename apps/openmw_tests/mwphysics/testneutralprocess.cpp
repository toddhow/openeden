// Task 0154 (stage S7 of task 0135): the process levels of neutral actors, pure maths. Expected values are hand-derived
// from the named numbers in neutralprocess.hpp (4096 / 8192 units, 1/20 and 1/5 second, 10 percent hysteresis).
#include <apps/openmw/mwphysics/neutralprocess.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    using namespace MWPhysics::NeutralProcess;

    constexpr float sq(float v)
    {
        return v * v;
    }

    TEST(NeutralProcessClassify, ringsByHorizontalDistance)
    {
        const Config c;
        EXPECT_EQ(classify(0.f, c), Level::High);
        EXPECT_EQ(classify(sq(4096.f), c), Level::High); // the radius itself is inside
        EXPECT_EQ(classify(sq(4096.5f), c), Level::Middle);
        EXPECT_EQ(classify(sq(8192.f), c), Level::Middle);
        EXPECT_EQ(classify(sq(8193.f), c), Level::Low);
        EXPECT_EQ(classify(sq(100000.f), c), Level::Low);
    }

    TEST(NeutralProcessClassify, hostileDistancesAreLow)
    {
        const Config c;
        EXPECT_EQ(classify(std::numeric_limits<float>::quiet_NaN(), c), Level::Low);
        EXPECT_EQ(classify(std::numeric_limits<float>::infinity(), c), Level::Low);
        EXPECT_EQ(classify(-1.f, c), Level::Low);
        EXPECT_EQ(reclassify(Level::High, std::numeric_limits<float>::quiet_NaN(), c), Level::Low);
    }

    TEST(NeutralProcessClassify, aCustomConfigMovesTheRings)
    {
        Config c;
        c.mHighRadius = 100.f;
        c.mMiddleRadius = 200.f;
        EXPECT_EQ(classify(sq(99.f), c), Level::High);
        EXPECT_EQ(classify(sq(150.f), c), Level::Middle);
        EXPECT_EQ(classify(sq(250.f), c), Level::Low);
    }

    TEST(NeutralProcessReclassify, aNearerLevelIsTakenAtTheRadius)
    {
        const Config c;
        EXPECT_EQ(reclassify(Level::Middle, sq(4096.f), c), Level::High);
        EXPECT_EQ(reclassify(Level::Low, sq(8192.f), c), Level::Middle);
        EXPECT_EQ(reclassify(Level::Low, sq(100.f), c), Level::High);
    }

    TEST(NeutralProcessReclassify, aFartherLevelNeedsTheHysteresisMargin)
    {
        const Config c;
        // 4096 * 1.1 = 4505.6
        EXPECT_EQ(reclassify(Level::High, sq(4400.f), c), Level::High);
        EXPECT_EQ(reclassify(Level::High, sq(4600.f), c), Level::Middle);
        // 8192 * 1.1 = 9011.2
        EXPECT_EQ(reclassify(Level::Middle, sq(8800.f), c), Level::Middle);
        EXPECT_EQ(reclassify(Level::Middle, sq(9100.f), c), Level::Low);
        // A High actor that jumps far goes straight past Middle's margin only if it is beyond it.
        EXPECT_EQ(reclassify(Level::High, sq(8800.f), c), Level::Middle);
        EXPECT_EQ(reclassify(Level::High, sq(9100.f), c), Level::Low);
        // No hysteresis when it is switched off.
        Config none;
        none.mHysteresis = 0.f;
        EXPECT_EQ(reclassify(Level::High, sq(4100.f), none), Level::Middle);
    }

    TEST(NeutralProcessReclassify, anActorOnTheRingDoesNotFlicker)
    {
        const Config c;
        Level level = Level::High;
        int changes = 0;
        for (int i = 0; i < 200; ++i)
        {
            const float d = 4096.f + ((i % 2 == 0) ? 30.f : -30.f); // wobbles across the radius
            const Level next = reclassify(level, sq(d), c);
            changes += next != level;
            level = next;
        }
        EXPECT_EQ(changes, 0);
        EXPECT_EQ(level, Level::High);
    }

    TEST(NeutralProcessPeriod, perLevel)
    {
        const Config c;
        EXPECT_FLOAT_EQ(periodOf(Level::High, c), 0.f);
        EXPECT_FLOAT_EQ(periodOf(Level::Middle, c), 0.05f);
        EXPECT_FLOAT_EQ(periodOf(Level::Low, c), 0.2f);
        Config bad;
        bad.mMiddlePeriod = -1.f;
        EXPECT_FLOAT_EQ(periodOf(Level::Middle, bad), 0.f);
    }

    TEST(NeutralProcessSchedule, highStepsEveryFrameWithTheFrameTime)
    {
        const Config c;
        Schedule s;
        float seconds = -1.f;
        for (int i = 0; i < 5; ++i)
        {
            ASSERT_TRUE(s.advance(Level::High, 0.016f, c, seconds));
            EXPECT_FLOAT_EQ(seconds, 0.016f);
        }
        EXPECT_FALSE(s.advance(Level::High, 0.f, c, seconds)); // no time, no step
        EXPECT_FLOAT_EQ(seconds, 0.f);
    }

    TEST(NeutralProcessSchedule, middleStepsWhenThePeriodIsReachedAndHandsOverAllTheTime)
    {
        const Config c;
        Schedule s;
        float seconds = 0.f;
        int frames = 0;
        // 0.016 * 4 = 0.064 >= 0.05 (three frames make only 0.048).
        while (!s.advance(Level::Middle, 0.016f, c, seconds))
            ++frames;
        EXPECT_EQ(frames, 3);
        EXPECT_NEAR(seconds, 0.064f, 1e-6);
        EXPECT_FLOAT_EQ(s.pending(), 0.f);
    }

    TEST(NeutralProcessSchedule, noTimeIsEverLost)
    {
        const Config c;
        for (const Level level : { Level::Middle, Level::Low })
        {
            Schedule s;
            s.seed(0.3f, level, c);
            double given = s.pending();
            double stepped = 0.0;
            for (int i = 0; i < 5000; ++i)
            {
                const float dt = 0.005f + 0.001f * static_cast<float>(i % 17);
                given += dt;
                float seconds = 0.f;
                if (s.advance(level, dt, c, seconds))
                    stepped += seconds;
            }
            EXPECT_NEAR(given, stepped + s.pending(), 1e-3);
        }
    }

    TEST(NeutralProcessSchedule, aSeedPhaseStartsPartWayThroughThePeriod)
    {
        const Config c;
        Schedule early;
        early.seed(0.5f, Level::Low, c); // 0.1 s pending of 0.2
        EXPECT_NEAR(early.pending(), 0.1f, 1e-6);
        float seconds = 0.f;
        EXPECT_FALSE(early.advance(Level::Low, 0.05f, c, seconds));
        EXPECT_TRUE(early.advance(Level::Low, 0.06f, c, seconds)); // 0.21 >= 0.2
        Schedule bad;
        bad.seed(std::numeric_limits<float>::quiet_NaN(), Level::Low, c);
        EXPECT_FLOAT_EQ(bad.pending(), 0.f);
        bad.seed(1.5f, Level::Low, c);
        EXPECT_FLOAT_EQ(bad.pending(), 0.f);
        bad.seed(-0.1f, Level::Low, c);
        EXPECT_FLOAT_EQ(bad.pending(), 0.f);
    }

    TEST(NeutralProcessSchedule, aBadFrameTimeCountsAsZero)
    {
        const Config c;
        Schedule s;
        float seconds = 0.f;
        EXPECT_FALSE(s.advance(Level::Low, std::numeric_limits<float>::quiet_NaN(), c, seconds));
        EXPECT_FALSE(s.advance(Level::Low, -5.f, c, seconds));
        EXPECT_FLOAT_EQ(s.pending(), 0.f);
        EXPECT_FALSE(s.advance(Level::High, std::numeric_limits<float>::infinity(), c, seconds));
    }

    TEST(NeutralProcessSchedule, comingNearerStepsThePendingTimeAtOnce)
    {
        const Config c;
        Schedule s;
        float seconds = 0.f;
        ASSERT_FALSE(s.advance(Level::Low, 0.1f, c, seconds)); // 0.1 pending of 0.2
        ASSERT_TRUE(s.advance(Level::High, 0.016f, c, seconds));
        EXPECT_NEAR(seconds, 0.116f, 1e-6);
        EXPECT_FLOAT_EQ(s.pending(), 0.f);
    }

    TEST(NeutralProcessSchedule, seededActorsDoNotAllStepInTheSameFrame)
    {
        const Config c;
        constexpr int kActors = 20;
        std::vector<Schedule> schedules(kActors);
        for (int i = 0; i < kActors; ++i)
            schedules[i].seed(static_cast<float>(i) / kActors, Level::Low, c);
        int worst = 0;
        std::vector<int> steps(kActors, 0);
        for (int frame = 0; frame < 600; ++frame)
        {
            int stepping = 0;
            for (int i = 0; i < kActors; ++i)
            {
                float seconds = 0.f;
                if (schedules[i].advance(Level::Low, 0.016f, c, seconds))
                {
                    ++stepping;
                    ++steps[i];
                }
            }
            worst = std::max(worst, stepping);
        }
        // 600 frames of 0.016 s; a 0.2 s period is every 13th frame (0.208), about 46 steps each, the phases 0.01 s
        // apart, so at most 3 actors in one frame.
        EXPECT_LE(worst, 3);
        for (int n : steps)
        {
            EXPECT_GE(n, 45);
            EXPECT_LE(n, 48);
        }
    }

    TEST(NeutralProcessSchedule, stepsPerSecondFollowTheLevel)
    {
        const Config c;
        const auto stepsIn = [&](Level level) {
            Schedule s;
            int steps = 0;
            for (int frame = 0; frame < 600; ++frame)
            {
                float seconds = 0.f;
                steps += s.advance(level, 0.016f, c, seconds);
            }
            return steps;
        };
        EXPECT_EQ(stepsIn(Level::High), 600);
        EXPECT_EQ(stepsIn(Level::Middle), 150); // every 4th frame: 3 frames make 0.048 < 0.05, 4 make 0.064
        EXPECT_NEAR(stepsIn(Level::Low), 46, 1); // every 13th frame: 12 frames make 0.192 < 0.2
    }
}
