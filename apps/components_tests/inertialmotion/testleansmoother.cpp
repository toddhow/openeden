#include <components/inertialmotion/leansmoother.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    using namespace InertialMotion;

    constexpr float sFrameDt = 1.0f / 60.0f;
    constexpr float sRateSlack = 1e-6f;

    // Runs the smoother for the given number of steps toward a constant target and returns every value reached.
    std::vector<float> run(
        LeanSmoother& smoother, float target, float dt, int steps, const LeanSmootherParameters& params)
    {
        std::vector<float> values;
        values.reserve(static_cast<std::size_t>(steps));
        for (int i = 0; i < steps; ++i)
        {
            values.push_back(smoother.update(target, dt, params));
        }
        return values;
    }

    TEST(LeanSmootherTest, StartsAtRest)
    {
        LeanSmoother smoother;
        EXPECT_EQ(smoother.getValue(), 0.0f);
        EXPECT_EQ(smoother.getVelocity(), 0.0f);
    }

    TEST(LeanSmootherTest, ZeroTargetFromRestStaysExactlyZero)
    {
        LeanSmoother smoother;
        const LeanSmootherParameters params;
        for (int i = 0; i < 100; ++i)
        {
            EXPECT_EQ(smoother.update(0.0f, sFrameDt, params), 0.0f);
        }
        EXPECT_EQ(smoother.getValue(), 0.0f);
        EXPECT_EQ(smoother.getVelocity(), 0.0f);
    }

    TEST(LeanSmootherTest, InvalidInputsChangeNothing)
    {
        LeanSmoother smoother;
        const LeanSmootherParameters params;
        run(smoother, 0.1f, sFrameDt, 10, params);

        const float value = smoother.getValue();
        const float velocity = smoother.getVelocity();
        ASSERT_GT(value, 0.0f);

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();

        EXPECT_EQ(smoother.update(0.1f, 0.0f, params), value);
        EXPECT_EQ(smoother.update(0.1f, -0.01f, params), value);
        EXPECT_EQ(smoother.update(0.1f, nan, params), value);
        EXPECT_EQ(smoother.update(0.1f, inf, params), value);
        EXPECT_EQ(smoother.update(nan, sFrameDt, params), value);
        EXPECT_EQ(smoother.update(inf, sFrameDt, params), value);
        EXPECT_EQ(smoother.update(-inf, sFrameDt, params), value);

        EXPECT_EQ(smoother.getValue(), value);
        EXPECT_EQ(smoother.getVelocity(), velocity);
    }

    TEST(LeanSmootherTest, FirstStepAfterTargetStepIsSmall)
    {
        LeanSmoother smoother;
        const LeanSmootherParameters params;
        const float value = smoother.update(0.2f, sFrameDt, params);
        EXPECT_GT(value, 0.0f);
        EXPECT_LT(std::abs(value), 0.1f * 0.2f);
    }

    TEST(LeanSmootherTest, VelocityRampsFromZeroWithoutJumps)
    {
        LeanSmoother smoother;
        const LeanSmootherParameters params;
        const std::vector<float> values = run(smoother, 0.1f, sFrameDt, 1, params);
        ASSERT_EQ(values.size(), 1u);

        float peak = std::abs(smoother.getVelocity());
        float previous = smoother.getVelocity();
        float maxDelta = std::abs(previous);
        for (int i = 0; i < 120; ++i)
        {
            smoother.update(0.1f, sFrameDt, params);
            const float velocity = smoother.getVelocity();
            peak = std::max(peak, std::abs(velocity));
            maxDelta = std::max(maxDelta, std::abs(velocity - previous));
            previous = velocity;
        }
        ASSERT_GT(peak, 0.0f);
        EXPECT_LT(maxDelta, 0.75f * peak);
    }

    TEST(LeanSmootherTest, ConvergesMonotonicallyWithoutOvershoot)
    {
        LeanSmoother smoother;
        const LeanSmootherParameters params;
        const float target = 0.1f;
        float previous = 0.0f;
        for (int i = 0; i < 600; ++i)
        {
            const float value = smoother.update(target, sFrameDt, params);
            EXPECT_TRUE(std::isfinite(value));
            EXPECT_GE(value, previous - 1e-7f);
            EXPECT_LE(value, target + 1e-6f);
            previous = value;
        }
        EXPECT_NEAR(previous, target, 1e-3f);
    }

    TEST(LeanSmootherTest, ConvergesWithinOnePercentAfterTenSmoothTimes)
    {
        LeanSmoother smoother;
        const LeanSmootherParameters params;
        const float target = 0.1f;
        const int steps = static_cast<int>(std::ceil(10.0f * params.mSmoothTime / sFrameDt));
        const std::vector<float> values = run(smoother, target, sFrameDt, steps, params);
        EXPECT_LE(std::abs(target - values.back()), 0.01f * target);
    }

    TEST(LeanSmootherTest, ChangePerStepNeverExceedsMaxRate)
    {
        const LeanSmootherParameters params;
        const float dts[] = { sFrameDt, 1.0f / 30.0f, 0.05f, 0.1f };
        const float targets[] = { 100.0f, -100.0f, 0.2f };
        for (const float dt : dts)
        {
            for (const float target : targets)
            {
                LeanSmoother smoother;
                float previous = 0.0f;
                for (int i = 0; i < 200; ++i)
                {
                    const float value = smoother.update(target, dt, params);
                    EXPECT_TRUE(std::isfinite(value));
                    EXPECT_LE(std::abs(value - previous), params.mMaxRate * dt + sRateSlack);
                    EXPECT_LE(std::abs(value), std::abs(target) + 1e-4f);
                    previous = value;
                }
            }
        }
    }

    TEST(LeanSmootherTest, LargeTimeStepIsStable)
    {
        LeanSmoother smoother;
        const LeanSmootherParameters params;
        const float target = 0.1f;
        float previous = 0.0f;
        for (int i = 0; i < 10; ++i)
        {
            const float value = smoother.update(target, 1.0f, params);
            EXPECT_TRUE(std::isfinite(value));
            EXPECT_GE(value, previous - 1e-7f);
            EXPECT_LE(value, target + 1e-6f);
            previous = value;
        }
        EXPECT_GT(previous, 0.95f * target);
    }

    TEST(LeanSmootherTest, NearlyFrameRateIndependent)
    {
        const LeanSmootherParameters params;
        const float target = 0.1f;

        LeanSmoother slow;
        const std::vector<float> slowValues = run(slow, target, 1.0f / 30.0f, 30, params);
        LeanSmoother fast;
        const std::vector<float> fastValues = run(fast, target, 1.0f / 120.0f, 120, params);

        EXPECT_NEAR(slowValues.back(), fastValues.back(), 0.03f * target);
    }

    TEST(LeanSmootherTest, NegativeTargetMirrorsPositiveTarget)
    {
        const LeanSmootherParameters params;
        LeanSmoother positive;
        LeanSmoother negative;
        for (int i = 0; i < 200; ++i)
        {
            const float up = positive.update(0.1f, sFrameDt, params);
            const float down = negative.update(-0.1f, sFrameDt, params);
            EXPECT_NEAR(down, -up, 1e-6f);
            EXPECT_LE(down, 1e-7f);
            EXPECT_GE(down, -0.1f - 1e-6f);
        }
        EXPECT_NEAR(positive.getVelocity(), -negative.getVelocity(), 1e-6f);
    }

    TEST(LeanSmootherTest, TargetFlippingSignDoesNotStepValueOrVelocity)
    {
        LeanSmoother smoother;
        const LeanSmootherParameters params;
        run(smoother, 0.1f, sFrameDt, 60, params);

        float previousValue = smoother.getValue();
        float previousVelocity = smoother.getVelocity();
        float peak = std::abs(previousVelocity);
        float maxDelta = 0.0f;
        for (int i = 0; i < 180; ++i)
        {
            const float value = smoother.update(-0.1f, sFrameDt, params);
            const float velocity = smoother.getVelocity();
            EXPECT_LE(std::abs(value - previousValue), params.mMaxRate * sFrameDt + sRateSlack);
            peak = std::max(peak, std::abs(velocity));
            maxDelta = std::max(maxDelta, std::abs(velocity - previousVelocity));
            previousValue = value;
            previousVelocity = velocity;
        }
        ASSERT_GT(peak, 0.0f);
        EXPECT_LT(maxDelta, 0.75f * peak);
        EXPECT_LT(smoother.getValue(), 0.0f);
    }

    TEST(LeanSmootherTest, ResetReturnsToRest)
    {
        LeanSmoother smoother;
        const LeanSmootherParameters params;
        run(smoother, 0.1f, sFrameDt, 20, params);
        ASSERT_NE(smoother.getValue(), 0.0f);
        ASSERT_NE(smoother.getVelocity(), 0.0f);

        smoother.reset();
        EXPECT_EQ(smoother.getValue(), 0.0f);
        EXPECT_EQ(smoother.getVelocity(), 0.0f);
    }

    TEST(LeanSmootherTest, SameInputsGiveBitIdenticalOutputs)
    {
        const LeanSmootherParameters params;
        LeanSmoother first;
        LeanSmoother second;
        for (int i = 0; i < 300; ++i)
        {
            const float target = (i % 100 < 50) ? 0.15f : -0.05f;
            const float a = first.update(target, sFrameDt, params);
            const float b = second.update(target, sFrameDt, params);
            EXPECT_EQ(std::memcmp(&a, &b, sizeof(float)), 0);
            const float va = first.getVelocity();
            const float vb = second.getVelocity();
            EXPECT_EQ(std::memcmp(&va, &vb, sizeof(float)), 0);
        }
    }
}
