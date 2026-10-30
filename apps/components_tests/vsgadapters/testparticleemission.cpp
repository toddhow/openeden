#include <components/vsgadapters/nif/particle.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    // All inputs below are exactly representable in float, so the expected carries are exact.

    TEST(VsgAdaptersParticlesToEmitTest, returnsWholePartAndKeepsFraction)
    {
        float carry = 0.f;
        // 5/s for 0.5 s = 2.5
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.5f, 5.f), 2);
        EXPECT_FLOAT_EQ(carry, 0.5f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, exactWholeLeavesNoFraction)
    {
        float carry = 0.f;
        // 4/s for 0.5 s = 2.0
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.5f, 4.f), 2);
        EXPECT_FLOAT_EQ(carry, 0.f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, fractionsAccumulateAcrossCallsAndEmitOnceWhole)
    {
        float carry = 0.f;
        // 4/s for 0.125 s = 0.5 per call
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.125f, 4.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.5f);
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.125f, 4.f), 1);
        EXPECT_FLOAT_EQ(carry, 0.f);
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.125f, 4.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.5f);
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.125f, 4.f), 1);
        EXPECT_FLOAT_EQ(carry, 0.f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, quarterStepsEmitEveryFourthCall)
    {
        float carry = 0.f;
        // 2/s for 0.125 s = 0.25 per call
        int emitted = 0;
        for (int i = 0; i < 3; ++i)
            emitted += vsgAdapters::particlesToEmit(carry, 0.125f, 2.f);
        EXPECT_EQ(emitted, 0);
        EXPECT_FLOAT_EQ(carry, 0.75f);
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.125f, 2.f), 1);
        EXPECT_FLOAT_EQ(carry, 0.f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, largeDtEmitsManyAtOnce)
    {
        float carry = 0.f;
        // 100/s for 10 s = 1000
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 10.f, 100.f), 1000);
        EXPECT_FLOAT_EQ(carry, 0.f);

        // 3/s for 2.5 s = 7.5
        float carry2 = 0.f;
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry2, 2.5f, 3.f), 7);
        EXPECT_FLOAT_EQ(carry2, 0.5f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, preExistingCarryIsAdded)
    {
        float carry = 0.75f;
        // 0.75 + 4/s * 0.125 s = 1.25
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.125f, 4.f), 1);
        EXPECT_FLOAT_EQ(carry, 0.25f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, preExistingCarryReachingSeveralWholes)
    {
        float carry = 0.5f;
        // 0.5 + 2/s * 1.25 s = 3.0
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 1.25f, 2.f), 3);
        EXPECT_FLOAT_EQ(carry, 0.f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, zeroDtEmitsNothingAndKeepsCarry)
    {
        float carry = 0.75f;
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.f, 10.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.75f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, negativeDtEmitsNothingAndKeepsCarry)
    {
        float carry = 0.75f;
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, -0.5f, 10.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.75f);
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, -100.f, 1000.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.75f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, zeroRateEmitsNothingAndKeepsCarry)
    {
        float carry = 0.5f;
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 1.f, 0.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.5f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, negativeRateEmitsNothingAndKeepsCarry)
    {
        float carry = 0.5f;
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 1.f, -4.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.5f);
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 100.f, -1000.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.5f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, bothDtAndRateNonPositiveEmitNothingAndKeepCarry)
    {
        float carry = 0.25f;
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, -1.f, -1.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.25f);
        EXPECT_EQ(vsgAdapters::particlesToEmit(carry, 0.f, 0.f), 0);
        EXPECT_FLOAT_EQ(carry, 0.25f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, resultIsNeverNegative)
    {
        const float carries[] = { 0.f, 0.5f, -0.5f, -3.f, -1000.f };
        const float dts[] = { -10.f, -0.1f, 0.f, 0.001f, 0.1f, 10.f };
        const float rates[] = { -1000.f, -1.f, 0.f, 0.5f, 30.f, 1000.f };
        for (float initialCarry : carries)
            for (float dt : dts)
                for (float rate : rates)
                {
                    float carry = initialCarry;
                    EXPECT_GE(vsgAdapters::particlesToEmit(carry, dt, rate), 0)
                        << "carry " << initialCarry << " dt " << dt << " rate " << rate;
                }
    }

    // Over a long run the total emitted matches floor(rate * total time) within one particle, whatever the step size.
    void expectLongRunTotal(int steps, float dt, float perSecond)
    {
        float carry = 0.f;
        long long emitted = 0;
        for (int i = 0; i < steps; ++i)
        {
            const int n = vsgAdapters::particlesToEmit(carry, dt, perSecond);
            ASSERT_GE(n, 0);
            emitted += n;
        }
        const double expected = std::floor(static_cast<double>(steps) * dt * perSecond);
        EXPECT_NEAR(static_cast<double>(emitted), expected, 1.0)
            << steps << " steps of " << dt << " s at " << perSecond << "/s";
    }

    TEST(VsgAdaptersParticlesToEmitTest, longRunAt60HzEmitsTotalWithinOne)
    {
        // 1000 steps of 1/60 s at 30/s: about 500.
        expectLongRunTotal(1000, 1.f / 60.f, 30.f);
    }

    TEST(VsgAdaptersParticlesToEmitTest, longRunTotalDoesNotDependOnStepSize)
    {
        // The same 1000/60 s of simulated time at 30/s with different step sizes.
        expectLongRunTotal(6000, 1.f / 360.f, 30.f);
        expectLongRunTotal(2400, 1.f / 144.f, 30.f);
        expectLongRunTotal(500, 1.f / 30.f, 30.f);
        expectLongRunTotal(100, 1.f / 6.f, 30.f);
        // Slow rates where each step adds well under one particle.
        expectLongRunTotal(10000, 1.f / 60.f, 0.7f);
        // Fast rates where each step emits several.
        expectLongRunTotal(1000, 1.f / 60.f, 1234.f);
    }
}
