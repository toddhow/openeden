#include <components/inertialmotion/locomotionstate.hpp>

#include <gtest/gtest.h>

#include <string>

namespace
{
    using namespace InertialMotion;

    TEST(InertialMotionAuthoredStart, OnlyWhenEveryConditionHolds)
    {
        for (int bits = 0; bits < 32; ++bits)
        {
            const bool inertialActive = (bits & 1) != 0;
            const bool authoredEnabled = (bits & 2) != 0;
            const bool clipExists = (bits & 4) != 0;
            const bool hasInput = (bits & 8) != 0;
            const bool standing = (bits & 16) != 0;
            SCOPED_TRACE("bits=" + std::to_string(bits));
            const bool expected = inertialActive && authoredEnabled && clipExists && hasInput && standing;
            EXPECT_EQ(shouldPlayAuthoredStart(inertialActive, authoredEnabled, clipExists, hasInput, standing, 0.f),
                expected);
        }
    }

    TEST(InertialMotionAuthoredStart, OnlyBelowTheMaximumSpeed)
    {
        EXPECT_TRUE(shouldPlayAuthoredStart(true, true, true, true, true, 0.f));
        EXPECT_TRUE(shouldPlayAuthoredStart(true, true, true, true, true, sAuthoredStartMaxSpeed - 0.01f));
        EXPECT_FALSE(shouldPlayAuthoredStart(true, true, true, true, true, sAuthoredStartMaxSpeed));
        EXPECT_FALSE(shouldPlayAuthoredStart(true, true, true, true, true, 130.f));
    }

    TEST(InertialMotionAuthoredStart, IsAvailableOnTheFirstFrameOfInput)
    {
        // The choice of the movement group is made at the top of a frame, before the physics step that first
        // moves the actor: the locomotion state is still the idle one. The rule must hold then (it used to demand
        // the Starting phase, which exists only from the next frame on, so no start clip could ever be chosen).
        LocomotionState state;
        ASSERT_EQ(state.getPhase(), LocomotionPhase::Idle);
        ASSERT_EQ(state.getSmoothedSpeed(), 0.f);
        EXPECT_TRUE(shouldPlayAuthoredStart(true, true, true, true, true, state.getSmoothedSpeed()));

        // After the first physics step the phase is Starting: that one frame was the only time the old rule held,
        // and by then the actor's movement state is no longer 'none'.
        const LocomotionParameters params = getPresetParameters(Preset::High);
        state.update({ 0.f, 128.f }, 1.f / 60.f, params, 300.f);
        EXPECT_EQ(state.getPhase(), LocomotionPhase::Starting);
    }

    TEST(InertialMotionAuthoredStart, IsConstexpr)
    {
        constexpr bool allHold = shouldPlayAuthoredStart(true, true, true, true, true, 0.f);
        constexpr bool inertialOff = shouldPlayAuthoredStart(false, true, true, true, true, 0.f);
        EXPECT_TRUE(allHold);
        EXPECT_FALSE(inertialOff);
    }
}
