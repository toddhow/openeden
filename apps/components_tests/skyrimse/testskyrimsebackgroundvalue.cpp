// Task 0355 stage c follow-up: a value built on a worker that the caller picks up without blocking. Written before the
// code (the first run was against a stub that never builds anything).
#include <game_skyrimse/background_value.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
    using game_skyrimse::BackgroundValue;
    using namespace std::chrono_literals;

    bool waitReady(BackgroundValue<int>& v)
    {
        for (int i = 0; i < 2000 && !v.ready(); ++i)
            std::this_thread::sleep_for(1ms);
        return v.ready();
    }

    TEST(SkyrimBackgroundValue, aValueNeverStartedIsNotReadyAndTakeGivesNull)
    {
        BackgroundValue<int> v;
        EXPECT_FALSE(v.started());
        EXPECT_FALSE(v.ready());
        EXPECT_EQ(v.tryGet(), nullptr);
        EXPECT_EQ(v.take(), nullptr);
    }

    TEST(SkyrimBackgroundValue, askingWhileTheBuildIsStillRunningReturnsAtOnceWithNothing)
    {
        std::atomic<bool> gate{ false };
        BackgroundValue<int> v;
        v.start([&] {
            while (!gate.load())
                std::this_thread::sleep_for(1ms);
            return 7;
        });
        const auto begun = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i)
        {
            EXPECT_FALSE(v.ready());
            EXPECT_EQ(v.tryGet(), nullptr);
        }
        EXPECT_LT(std::chrono::steady_clock::now() - begun, 500ms); // the build is held shut: nothing waited for it
        gate.store(true);
        ASSERT_TRUE(waitReady(v));
        ASSERT_NE(v.tryGet(), nullptr);
        EXPECT_EQ(*v.tryGet(), 7);
    }

    TEST(SkyrimBackgroundValue, theBuildRunsOnceAndOnAnotherThread)
    {
        std::atomic<int> runs{ 0 };
        std::thread::id builder;
        BackgroundValue<int> v;
        const auto make = [&] {
            ++runs;
            builder = std::this_thread::get_id();
            return 3;
        };
        v.start(make);
        v.start(make);
        EXPECT_TRUE(v.started());
        ASSERT_TRUE(waitReady(v));
        EXPECT_EQ(runs.load(), 1);
        EXPECT_NE(builder, std::this_thread::get_id());
    }

    TEST(SkyrimBackgroundValue, takeWaitsForARunningBuild)
    {
        BackgroundValue<int> v;
        v.start([] {
            std::this_thread::sleep_for(50ms);
            return 9;
        });
        const int* value = v.take();
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, 9);
        EXPECT_TRUE(v.ready());
    }

    TEST(SkyrimBackgroundValue, destroyingWhileTheBuildRunsWaitsForIt)
    {
        std::atomic<bool> finished{ false };
        {
            BackgroundValue<int> v;
            v.start([&] {
                std::this_thread::sleep_for(50ms);
                finished.store(true);
                return 1;
            });
        }
        EXPECT_TRUE(finished.load());
    }
}
