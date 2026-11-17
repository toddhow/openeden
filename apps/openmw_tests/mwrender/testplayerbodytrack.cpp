// Task 0222 (the player has a Skyrim body): the pure rules of the player's drawn body. The gait is driven by the speed
// the player really moved at (his position delta over the frame time); a swing is told by a token that grows; the body
// is rebuilt only when the pack's revision changes. No renderer, no file.
#include <apps/openmw/mwrender/playerbodytrack.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace MWRender::PlayerBodyTrack;

    TEST(PlayerBodyGroundSpeed, theFirstSampleHasNoSpeed)
    {
        GroundSpeed speed;
        EXPECT_FLOAT_EQ(speed.update(100.0, 200.0, 0.0166f), 0.f);
    }

    TEST(PlayerBodyGroundSpeed, theSpeedIsTheHorizontalDistanceOverTheFrameTime)
    {
        GroundSpeed speed;
        (void)speed.update(0.0, 0.0, 0.1f);
        EXPECT_NEAR(speed.update(3.0, 4.0, 0.1f), 50.f, 1e-3f); // 5 units in 0.1 s
        EXPECT_NEAR(speed.update(3.0, 4.0, 0.1f), 0.f, 1e-3f); // standing still
    }

    TEST(PlayerBodyGroundSpeed, aZeroOrNegativeOrNonFiniteFrameTimeKeepsTheLastSpeedAndTheLastPlace)
    {
        GroundSpeed speed;
        (void)speed.update(0.0, 0.0, 0.1f);
        EXPECT_NEAR(speed.update(10.0, 0.0, 0.1f), 100.f, 1e-3f);
        for (const float dt :
            { 0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
            EXPECT_NEAR(speed.update(500.0, 0.0, dt), 100.f, 1e-3f) << dt;
        // the place was not taken over by the refused frames: the next delta is from x = 10
        EXPECT_NEAR(speed.update(20.0, 0.0, 0.1f), 100.f, 1e-3f);
    }

    TEST(PlayerBodyGroundSpeed, aJumpOfMoreThanTheTeleportDistanceIsNotAWalk)
    {
        GroundSpeed speed;
        (void)speed.update(0.0, 0.0, 0.1f);
        EXPECT_FLOAT_EQ(speed.update(kTeleportUnits + 1.0, 0.0, 0.1f), 0.f);
        // and the next frame measures from the new place
        EXPECT_NEAR(speed.update(kTeleportUnits + 6.0, 0.0, 0.1f), 50.f, 1e-3f);
    }

    TEST(PlayerBodyGroundSpeed, nonFinitePositionsGiveNoSpeed)
    {
        GroundSpeed speed;
        (void)speed.update(0.0, 0.0, 0.1f);
        EXPECT_FLOAT_EQ(speed.update(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.1f), 0.f);
        EXPECT_TRUE(std::isfinite(speed.update(1.0, 0.0, 0.1f)));
    }

    TEST(PlayerBodyGroundSpeed, resetForgetsThePlace)
    {
        GroundSpeed speed;
        (void)speed.update(0.0, 0.0, 0.1f);
        speed.reset();
        EXPECT_FLOAT_EQ(speed.update(50.0, 0.0, 0.1f), 0.f);
    }

    TEST(PlayerBodySwingWatch, aGrowingTokenFiresOncePerSwing)
    {
        SwingWatch watch;
        EXPECT_FALSE(watch.poll(0));
        EXPECT_TRUE(watch.poll(1));
        EXPECT_FALSE(watch.poll(1));
        EXPECT_TRUE(watch.poll(2));
        EXPECT_TRUE(watch.poll(5)); // two swings between polls count as one change
        EXPECT_FALSE(watch.poll(5));
    }

    TEST(PlayerBodySwingWatch, aSwingAlreadyUnderwayWhenWatchingBeginsIsAdoptedNotPlayed)
    {
        SwingWatch watch;
        watch.adopt(7);
        EXPECT_FALSE(watch.poll(7));
        EXPECT_TRUE(watch.poll(8));
    }

    TEST(PlayerBodyRebuildGate, nothingIsWantedWhileThePackIsNotReady)
    {
        RebuildGate gate;
        EXPECT_FALSE(gate.wants(0));
    }

    TEST(PlayerBodyRebuildGate, aNewRevisionIsWantedUntilItIsBuilt)
    {
        RebuildGate gate;
        EXPECT_TRUE(gate.wants(3));
        gate.built(3);
        EXPECT_FALSE(gate.wants(3));
        EXPECT_TRUE(gate.wants(4));
    }

    TEST(PlayerBodyRebuildGate, aPackThatGoesBackToNotReadyIsNotRebuiltAndKeepsTheOldBody)
    {
        RebuildGate gate;
        gate.built(3);
        EXPECT_FALSE(gate.wants(0));
        EXPECT_FALSE(gate.wants(3));
    }

    TEST(PlayerBodyRebuildGate, aFailedBuildIsNotRetriedEveryFrame)
    {
        RebuildGate gate;
        EXPECT_TRUE(gate.wants(3));
        gate.failed(3);
        EXPECT_FALSE(gate.wants(3));
        EXPECT_TRUE(gate.wants(4)); // a change of the picture tries again
    }
}
