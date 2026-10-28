#include <components/inertialmotion/locomotionstate.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

namespace
{
    using namespace InertialMotion;

    TEST(LocomotionStateTest, PresetParsingAndParameterMapping)
    {
        EXPECT_EQ(parsePreset("off"), Preset::Off);
        EXPECT_EQ(parsePreset("OFF"), Preset::Off);
        EXPECT_EQ(parsePreset("0"), Preset::Off);
        EXPECT_EQ(parsePreset("false"), Preset::Off);
        EXPECT_EQ(parsePreset("low"), Preset::Low);
        EXPECT_EQ(parsePreset("medium"), Preset::Medium);
        EXPECT_EQ(parsePreset("high"), Preset::High);
        EXPECT_EQ(parsePreset("custom"), Preset::Custom);

        const auto offParams = getPresetParameters(Preset::Off);
        EXPECT_FLOAT_EQ(offParams.mAccelTime, 0.0f);
        EXPECT_FLOAT_EQ(offParams.mStopTime, 0.0f);
        EXPECT_FALSE(offParams.mProceduralLean);

        const auto lowParams = getPresetParameters(Preset::Low);
        EXPECT_FLOAT_EQ(lowParams.mAccelTime, 0.12f);
        EXPECT_FLOAT_EQ(lowParams.mStopTime, 0.08f);
        EXPECT_FLOAT_EQ(lowParams.mMaxLeanDegrees, 3.5f);

        const auto medParams = getPresetParameters(Preset::Medium);
        EXPECT_FLOAT_EQ(medParams.mAccelTime, 0.25f);
        EXPECT_FLOAT_EQ(medParams.mStopTime, 0.18f);
        EXPECT_FLOAT_EQ(medParams.mMaxLeanDegrees, 7.0f);

        const auto highParams = getPresetParameters(Preset::High);
        EXPECT_FLOAT_EQ(highParams.mAccelTime, 0.45f);
        EXPECT_FLOAT_EQ(highParams.mStopTime, 0.35f);
        EXPECT_FLOAT_EQ(highParams.mMaxLeanDegrees, 12.0f);
    }

    TEST(LocomotionStateTest, VanillaBypassWhenDisabled)
    {
        LocomotionState state;
        const auto offParams = getPresetParameters(Preset::Off);
        const osg::Vec2f target(150.f, -80.f);

        const osg::Vec2f result = state.update(target, 0.016f, offParams, 300.f);
        EXPECT_FLOAT_EQ(result.x(), target.x());
        EXPECT_FLOAT_EQ(result.y(), target.y());
        EXPECT_FLOAT_EQ(state.getSmoothedSpeed(), target.length());
        EXPECT_EQ(state.getPhase(), LocomotionPhase::Moving);
    }

    TEST(LocomotionStateTest, SmoothAccelerationRampAndRateLimiting)
    {
        LocomotionState state;
        const auto medParams = getPresetParameters(Preset::Medium);
        const float maxSpeed = 300.f;
        const float dt = 0.01f;
        const osg::Vec2f target(200.f, 0.f);

        float prevSpeed = 0.f;
        const float maxAllowedRate = (maxSpeed / medParams.mAccelTime) * 1.01f;

        for (int i = 0; i < 25; ++i)
        {
            state.update(target, dt, medParams, maxSpeed);
            const float currentSpeed = state.getSmoothedSpeed();
            EXPECT_GE(currentSpeed, prevSpeed); // Monotonically increasing
            const float rate = (currentSpeed - prevSpeed) / dt;
            EXPECT_LE(rate, maxAllowedRate);
            prevSpeed = currentSpeed;
        }

        // After accel time (0.25s = 25 steps of 0.01s), target velocity should be reached
        EXPECT_NEAR(state.getSmoothedVelocity().x(), target.x(), 1e-2f);
        EXPECT_NEAR(state.getSmoothedVelocity().y(), target.y(), 1e-2f);
        EXPECT_EQ(state.getPhase(), LocomotionPhase::Moving);
    }

    TEST(LocomotionStateTest, DecelerationBrakingToZero)
    {
        LocomotionState state;
        const auto medParams = getPresetParameters(Preset::Medium);
        const float maxSpeed = 300.f;

        // Seed with running speed
        state.reset(osg::Vec2f(250.f, 0.f));
        EXPECT_EQ(state.getPhase(), LocomotionPhase::Moving);

        // Release stick/keys (target = 0, 0)
        const osg::Vec2f target(0.f, 0.f);
        const float dt = 0.01f;

        // At step 1, should enter Stopping phase
        state.update(target, dt, medParams, maxSpeed);
        EXPECT_EQ(state.getPhase(), LocomotionPhase::Stopping);

        // Step through stop time (0.18s = 18 steps of 0.01s)
        for (int i = 1; i < 18; ++i)
        {
            state.update(target, dt, medParams, maxSpeed);
        }

        // Fully stopped
        EXPECT_NEAR(state.getSmoothedSpeed(), 0.0f, 1e-4f);
        EXPECT_EQ(state.getPhase(), LocomotionPhase::Idle);
    }

    TEST(LocomotionStateTest, 180ReversalEntersSkidPhase)
    {
        LocomotionState state;
        const auto medParams = getPresetParameters(Preset::Medium);
        const float maxSpeed = 300.f;

        // Running forward in +Y
        state.reset(osg::Vec2f(0.f, 200.f));

        // Slam input in reverse (-Y)
        const osg::Vec2f reverseTarget(0.f, -200.f);
        const float dt = 0.02f;

        state.update(reverseTarget, dt, medParams, maxSpeed);
        // Opposing dot product must trigger Skidding phase
        EXPECT_EQ(state.getPhase(), LocomotionPhase::Skidding);

        // Continue updating until momentum passes through zero and flips
        bool passedThroughZero = false;
        for (int i = 0; i < 40; ++i)
        {
            state.update(reverseTarget, dt, medParams, maxSpeed);
            if (state.getSmoothedVelocity().y() <= 0.0f)
            {
                passedThroughZero = true;
                break;
            }
        }
        EXPECT_TRUE(passedThroughZero);
    }

    TEST(LocomotionStateTest, StepSizeInvarianceAcrossFrequencies)
    {
        // 0.24 seconds of acceleration simulated at 30 Hz vs 60 Hz vs 120 Hz
        const auto medParams = getPresetParameters(Preset::Medium);
        const float maxSpeed = 300.f;
        const osg::Vec2f target(180.f, 120.f);

        LocomotionState state30;
        const float dt30 = 1.0f / 30.0f;
        for (int i = 0; i < 6; ++i) // 0.20s
            state30.update(target, dt30, medParams, maxSpeed);

        LocomotionState state60;
        const float dt60 = 1.0f / 60.0f;
        for (int i = 0; i < 12; ++i) // 0.20s
            state60.update(target, dt60, medParams, maxSpeed);

        EXPECT_NEAR(state30.getSmoothedVelocity().x(), state60.getSmoothedVelocity().x(), 1e-3f);
        EXPECT_NEAR(state30.getSmoothedVelocity().y(), state60.getSmoothedVelocity().y(), 1e-3f);
    }

    TEST(LocomotionStateTest, YawHeadingSmoothingAndCriticalDamping)
    {
        LocomotionState state;
        const auto medParams = getPresetParameters(Preset::Medium);

        // Moving towards +X (yaw = pi/2)
        state.reset(osg::Vec2f(100.f, 0.f), 0.0f);

        float currentYaw = 0.0f;
        const float dt = 0.02f;

        // Headings smooth over half-life (25 steps * 0.02s = 0.50s > 4 half-lives)
        for (int i = 0; i < 25; ++i)
        {
            currentYaw = state.updateYaw(currentYaw, dt, medParams, false);
        }

        // Should be tracking towards pi/2 (~1.5708) within 0.10 rad
        constexpr float kTargetYaw = std::numbers::pi_v<float> * 0.5f;
        EXPECT_NEAR(currentYaw, kTargetYaw, 0.10f);
        EXPECT_GT(state.getAngularVelocity(), 0.0f);
    }
}
