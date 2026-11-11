#include <components/inertialmotion/locomotionstate.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace InertialMotion;

    TEST(InertialMotionStrideShape, IsTheRatioOfTheFramesRootSpeedToTheMean)
    {
        EXPECT_FLOAT_EQ(strideShape(128.f, 128.f), 1.f);
        EXPECT_FLOAT_EQ(strideShape(155.f, 128.f), 155.f / 128.f);
        EXPECT_FLOAT_EQ(strideShape(113.f, 128.f), 113.f / 128.f);
    }

    TEST(InertialMotionStrideShape, IsClampedSoOneBadFrameCannotLaunchOrFreezeTheActor)
    {
        EXPECT_FLOAT_EQ(strideShape(10000.f, 128.f), sMaxStrideShape);
        EXPECT_FLOAT_EQ(strideShape(0.f, 128.f), sMinStrideShape);
        EXPECT_FLOAT_EQ(strideShape(5.f, 128.f), sMinStrideShape);
    }

    TEST(InertialMotionStrideShape, IsOneWhenThereIsNothingToCompare)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EXPECT_FLOAT_EQ(strideShape(100.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(strideShape(100.f, 0.5f), 1.f);
        EXPECT_FLOAT_EQ(strideShape(100.f, -3.f), 1.f);
        EXPECT_FLOAT_EQ(strideShape(nan, 128.f), 1.f);
        EXPECT_FLOAT_EQ(strideShape(128.f, nan), 1.f);
        EXPECT_FLOAT_EQ(strideShape(inf, 128.f), 1.f);
        EXPECT_FLOAT_EQ(strideShape(-5.f, 128.f), 1.f);
    }

    TEST(InertialMotionStrideShape, AverageOfAFullCycleOfRealisticRootSpeedsIsOne)
    {
        // The walk cycle's root speeds of docs/research/0052, scaled to their own mean: the shape averages to one, so
        // the mean ground speed stays the smoothed speed.
        const float rootSpeeds[] = { 113.f, 122.f, 131.f, 151.f, 155.f, 150.f, 131.f, 115.f, 113.f, 122.f };
        float total = 0.f;
        for (float speed : rootSpeeds)
            total += speed;
        const float mean = total / static_cast<float>(std::size(rootSpeeds));
        float shapes = 0.f;
        for (float speed : rootSpeeds)
            shapes += strideShape(speed, mean);
        EXPECT_NEAR(shapes / static_cast<float>(std::size(rootSpeeds)), 1.f, 1e-4f);
    }

    TEST(InertialMotionStrideShape, IsConstexpr)
    {
        constexpr float shape = strideShape(64.f, 128.f);
        EXPECT_FLOAT_EQ(shape, 0.5f);
    }
}
