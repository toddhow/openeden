#include <components/inertialmotion/locomotionstate.hpp>

#include <gtest/gtest.h>

#include <string>

namespace
{
    using namespace InertialMotion;

    TEST(InertialMotionMovementRequest, ChooseSourceAllSixteenCombinations)
    {
        for (int bits = 0; bits < 16; ++bits)
        {
            const bool inertialActive = (bits & 1) != 0;
            const bool hitReaction = (bits & 2) != 0;
            const bool movementGroupPlaying = (bits & 4) != 0;
            const bool authoredTransitionPlaying = (bits & 8) != 0;
            SCOPED_TRACE("inertialActive=" + std::to_string(inertialActive) + " hitReaction="
                + std::to_string(hitReaction) + " movementGroupPlaying=" + std::to_string(movementGroupPlaying)
                + " authoredTransitionPlaying=" + std::to_string(authoredTransitionPlaying));

            const bool expectIntended
                = inertialActive && !hitReaction && movementGroupPlaying && !authoredTransitionPlaying;
            const MovementRequestSource expected
                = expectIntended ? MovementRequestSource::Intended : MovementRequestSource::Animation;
            EXPECT_EQ(chooseMovementRequestSource(
                          inertialActive, hitReaction, movementGroupPlaying, authoredTransitionPlaying),
                expected);
        }
    }

    TEST(InertialMotionMovementRequest, AnimationGroundSpeedCases)
    {
        EXPECT_FLOAT_EQ(animationGroundSpeed(0.f, 109.f, true), sMinimumAnimationSpeedFraction * 109.f);
        EXPECT_FLOAT_EQ(animationGroundSpeed(80.f, 109.f, true), 80.f);
        EXPECT_FLOAT_EQ(animationGroundSpeed(0.f, 109.f, false), 0.f);
        EXPECT_FLOAT_EQ(animationGroundSpeed(0.f, 0.f, false), 0.f);
        EXPECT_FLOAT_EQ(animationGroundSpeed(30.f, 109.f, false), 30.f);
        EXPECT_FLOAT_EQ(animationGroundSpeed(0.f, 0.f, true), 0.f);
    }

    TEST(InertialMotionMovementRequest, AnimationGroundSpeedIsConstexpr)
    {
        static_assert(animationGroundSpeed(80.f, 109.f, true) == 80.f);
        static_assert(animationGroundSpeed(30.f, 109.f, false) == 30.f);
        SUCCEED();
    }

    TEST(InertialMotionMovementRequest, IntendedRequestReachesWalkSpeedWithinAccelTime)
    {
        const LocomotionParameters params = getPresetParameters(Preset::High);
        constexpr float dt = 1.f / 60.f;
        constexpr float target = 109.f;
        constexpr float maxSpeed = 300.f;
        const float deadline = params.mAccelTime + 0.1f;

        LocomotionState state;
        float previous = 0.f;
        float reachedAt = -1.f;
        float time = 0.f;
        for (int i = 0; i < 600; ++i)
        {
            state.update(vsg::vec2(0.f, target), dt, params, maxSpeed);
            time += dt;
            const float speed = state.getSmoothedSpeed();
            EXPECT_GE(speed, previous - 1e-4f) << "step " << i;
            EXPECT_LE(speed, target + 0.001f) << "step " << i;
            previous = speed;
            if (reachedAt < 0.f && speed >= target - 0.5f)
                reachedAt = time;
        }
        ASSERT_GE(reachedAt, 0.f) << "never reached the intended speed";
        EXPECT_LE(reachedAt, deadline);
    }

    // Documents the feedback loop that the Intended source avoids: if the request is derived from the animation's
    // own root motion, and the animation is scaled to the smoothed speed (floored at 15% of the intended speed),
    // the smoothed speed never leaves the floor.
    TEST(InertialMotionMovementRequest, AnimationRootMotionRequestStallsAtTheFloor)
    {
        const LocomotionParameters params = getPresetParameters(Preset::High);
        constexpr float dt = 1.f / 60.f;
        constexpr float intended = 109.f;
        constexpr float walkVelocityPerAnimationSpeed = 1.0f;

        LocomotionState state;
        for (int i = 0; i < 180; ++i)
        {
            const float requestedSpeed
                = animationGroundSpeed(state.getSmoothedSpeed(), intended, true) * walkVelocityPerAnimationSpeed;
            state.update(vsg::vec2(0.f, requestedSpeed), dt, params, 300.f);
        }
        EXPECT_LT(state.getSmoothedSpeed(), 20.f);
    }
}
