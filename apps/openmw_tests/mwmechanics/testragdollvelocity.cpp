// Tests for task 0106, second slice (1): apps/openmw/mwmechanics/ragdollvelocity.hpp. Written from the spec before the
// code. In plain words: while an actor lives its movement is tracked; at death the ragdoll starts with that velocity,
// not with zero, and never faster than a set limit; a teleport is not a velocity.

#include "apps/openmw/mwmechanics/ragdollvelocity.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using MWMechanics::ActorVelocityTracker;
    using MWMechanics::clampSpeed;

    constexpr double kDt = 1.0 / 60.0;

    TEST(RagdollVelocity, anActorThatNeverMovedHasNoVelocity)
    {
        ActorVelocityTracker tracker;
        double v[3] = { 1, 2, 3 };
        tracker.velocity(v);
        EXPECT_EQ(v[0], 0.0);
        EXPECT_EQ(v[1], 0.0);
        EXPECT_EQ(v[2], 0.0);
        for (int i = 0; i < 30; ++i)
            tracker.update(10.0, 20.0, 30.0, kDt);
        tracker.velocity(v);
        EXPECT_NEAR(std::abs(v[0]) + std::abs(v[1]) + std::abs(v[2]), 0.0, 1e-9);
    }

    TEST(RagdollVelocity, steadyMovementGivesThatVelocityAfterAFewFrames)
    {
        ActorVelocityTracker tracker;
        double x = 0.0;
        for (int i = 0; i < 20; ++i)
        {
            x += 200.0 * kDt;
            tracker.update(x, 5.0, 0.0, kDt);
        }
        double v[3];
        tracker.velocity(v);
        EXPECT_NEAR(v[0], 200.0, 1.0);
        EXPECT_NEAR(v[1], 0.0, 1e-6);
        EXPECT_NEAR(v[2], 0.0, 1e-6);
    }

    TEST(RagdollVelocity, theNewestFramesCountMoreThanTheOldOnes)
    {
        ActorVelocityTracker tracker;
        double x = 0.0;
        for (int i = 0; i < 30; ++i)
        {
            x += 100.0 * kDt;
            tracker.update(x, 0.0, 0.0, kDt);
        }
        for (int i = 0; i < 3; ++i)
        {
            x += 300.0 * kDt;
            tracker.update(x, 0.0, 0.0, kDt);
        }
        double v[3];
        tracker.velocity(v);
        EXPECT_GT(v[0], 250.0);
        EXPECT_LE(v[0], 300.0 + 1e-6);
    }

    TEST(RagdollVelocity, aTeleportIsNotAVelocityAndClearsIt)
    {
        ActorVelocityTracker tracker;
        double x = 0.0;
        for (int i = 0; i < 10; ++i)
        {
            x += 200.0 * kDt;
            tracker.update(x, 0.0, 0.0, kDt);
        }
        tracker.update(x + 5000.0, 0.0, 0.0, kDt);
        double v[3];
        tracker.velocity(v);
        EXPECT_EQ(v[0], 0.0);
        EXPECT_EQ(v[1], 0.0);
        EXPECT_EQ(v[2], 0.0);
        // and the tracking carries on from the new place
        double y = x + 5000.0;
        for (int i = 0; i < 20; ++i)
        {
            y += 100.0 * kDt;
            tracker.update(y, 0.0, 0.0, kDt);
        }
        tracker.velocity(v);
        EXPECT_NEAR(v[0], 100.0, 1.0);
    }

    TEST(RagdollVelocity, aFrameWithNoDurationIsIgnored)
    {
        ActorVelocityTracker tracker;
        tracker.update(0.0, 0.0, 0.0, kDt);
        tracker.update(10.0, 0.0, 0.0, 0.0);
        tracker.update(-5.0, 0.0, 0.0, -1.0);
        double v[3];
        tracker.velocity(v);
        EXPECT_EQ(v[0], 0.0);
    }

    TEST(RagdollVelocity, resetForgetsEverything)
    {
        ActorVelocityTracker tracker;
        double x = 0.0;
        for (int i = 0; i < 10; ++i)
        {
            x += 200.0 * kDt;
            tracker.update(x, 0.0, 0.0, kDt);
        }
        tracker.reset();
        double v[3];
        tracker.velocity(v);
        EXPECT_EQ(v[0], 0.0);
        // the first update after a reset has no earlier position: still no velocity
        tracker.update(x + 3.0, 0.0, 0.0, kDt);
        tracker.velocity(v);
        EXPECT_EQ(v[0], 0.0);
    }

    TEST(RagdollVelocity, theSpeedIsClampedKeepingTheDirection)
    {
        double v[3] = { 3000.0, 4000.0, 0.0 }; // 5000 units per second
        clampSpeed(v, 1000.0);
        EXPECT_NEAR(v[0], 600.0, 1e-9);
        EXPECT_NEAR(v[1], 800.0, 1e-9);
        EXPECT_NEAR(v[2], 0.0, 1e-9);
        double slow[3] = { 3.0, 4.0, 0.0 };
        clampSpeed(slow, 1000.0);
        EXPECT_EQ(slow[0], 3.0);
        EXPECT_EQ(slow[1], 4.0);
        double nan[3] = { std::nan(""), 1.0, 2.0 };
        clampSpeed(nan, 1000.0);
        EXPECT_EQ(nan[0], 0.0);
        EXPECT_EQ(nan[1], 0.0);
        EXPECT_EQ(nan[2], 0.0);
    }
}
