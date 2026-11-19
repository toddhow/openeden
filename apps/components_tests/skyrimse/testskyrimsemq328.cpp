#include <game_skyrimse/triggers.hpp>

#include <gtest/gtest.h>

#include <cmath>

// Task 0328: a box trigger turned about X and Y as well as Z. MQ105SetStage20 is placed with the rotation
// (pi/2, pi/2, 0): hand-worked below, its three bounds lie along world Y, world Z and world X.

namespace
{
    using namespace game_skyrimse;

    constexpr double kPi = 3.14159265358979323846;

    TriggerVolume box(double bx, double by, double bz, double rx, double ry, double rz)
    {
        TriggerVolume v;
        v.mType = kPrimitiveBox;
        v.mBounds[0] = bx;
        v.mBounds[1] = by;
        v.mBounds[2] = bz;
        v.mRotationX = rx;
        v.mRotationY = ry;
        v.mRotationZ = rz;
        v.mAcceptBox = true;
        return v;
    }

    // Local x -> world Y, local y -> world Z, local z -> world X (Skyrim turns about Z first, then Y, then X).
    TEST(SkyrimTriggerBox328, aBoxTurnedAQuarterAboutXAndYHasItsBoundsOnTheOtherAxes)
    {
        const TriggerVolume v = box(375.0, 326.0, 1150.0, kPi / 2, kPi / 2, 0.0);
        EXPECT_TRUE(volumeContains(v, 1100, 0, 0)); // along world X: the long bound
        EXPECT_TRUE(volumeContains(v, -1100, 0, 0));
        EXPECT_FALSE(volumeContains(v, 1200, 0, 0));
        EXPECT_TRUE(volumeContains(v, 0, 370, 0)); // along world Y: the first bound
        EXPECT_FALSE(volumeContains(v, 0, 380, 0));
        EXPECT_TRUE(volumeContains(v, 0, 0, 320)); // along world Z: the second bound
        EXPECT_FALSE(volumeContains(v, 0, 0, 330));
    }

    TEST(SkyrimTriggerBox328, aRotationAboutZAloneIsAsBefore)
    {
        const TriggerVolume v = box(100.0, 10.0, 10.0, 0.0, 0.0, kPi / 2);
        EXPECT_TRUE(volumeContains(v, 0, -90, 0));
        EXPECT_FALSE(volumeContains(v, 90, 0, 0));
    }

    TEST(SkyrimTriggerBox328, aNonFiniteTurnIsNotSupported)
    {
        EXPECT_FALSE(triggerSupported(box(1, 1, 1, std::nan(""), 0.0, 0.0)));
        EXPECT_FALSE(triggerSupported(box(1, 1, 1, 0.0, std::nan(""), 0.0)));
    }
}
