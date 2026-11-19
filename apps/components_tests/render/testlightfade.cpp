// Task 0355 stage j: the distance fade of a game pack's interior lights and the clip distance's far plane
// (components/render/lightfade.hpp). Written before the code; the first run was against stubs. Every number is written
// out by hand.
#include <gtest/gtest.h>

#include <limits>

#include <components/render/lightfade.hpp>

namespace
{
    using Render::LightFade::effectiveFarPlane;
    using Render::LightFade::factor;

    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

    TEST(RenderLightFade, nothingFadesWhenBothDistancesAreZero)
    {
        EXPECT_FLOAT_EQ(factor(0.f, 0.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(factor(1e6f, 0.f, 0.f), 1.f);
    }

    TEST(RenderLightFade, fullBeforeTheBeginAndNoneAtTheEnd)
    {
        EXPECT_FLOAT_EQ(factor(0.f, 4000.f, 5000.f), 1.f);
        EXPECT_FLOAT_EQ(factor(3999.f, 4000.f, 5000.f), 1.f);
        EXPECT_FLOAT_EQ(factor(4000.f, 4000.f, 5000.f), 1.f);
        EXPECT_FLOAT_EQ(factor(5000.f, 4000.f, 5000.f), 0.f);
        EXPECT_FLOAT_EQ(factor(9000.f, 4000.f, 5000.f), 0.f);
    }

    TEST(RenderLightFade, linearBetweenTheTwo)
    {
        EXPECT_FLOAT_EQ(factor(4250.f, 4000.f, 5000.f), 0.75f);
        EXPECT_FLOAT_EQ(factor(4500.f, 4000.f, 5000.f), 0.5f);
        EXPECT_FLOAT_EQ(factor(4750.f, 4000.f, 5000.f), 0.25f);
        // The data also has (100, 5000): a long ramp from 100.
        EXPECT_FLOAT_EQ(factor(2550.f, 100.f, 5000.f), 0.5f);
    }

    TEST(RenderLightFade, anEndEqualToTheBeginIsACut)
    {
        // Five lighting templates of the five masters write 11,000 and 11,000.
        EXPECT_FLOAT_EQ(factor(10999.f, 11000.f, 11000.f), 1.f);
        EXPECT_FLOAT_EQ(factor(11000.f, 11000.f, 11000.f), 0.f);
        EXPECT_FLOAT_EQ(factor(12000.f, 11000.f, 11000.f), 0.f);
    }

    TEST(RenderLightFade, aZeroOrNegativeEndOrNumbersThatAreNoneMeanNoFade)
    {
        EXPECT_FLOAT_EQ(factor(500.f, 100.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(factor(500.f, 100.f, -3.f), 1.f);
        EXPECT_FLOAT_EQ(factor(500.f, kNan, 1000.f), 1.f);
        EXPECT_FLOAT_EQ(factor(500.f, 0.f, kNan), 1.f);
        EXPECT_FLOAT_EQ(factor(kNan, 0.f, 1000.f), 1.f);
        EXPECT_FLOAT_EQ(factor(1e9f, 0.f, std::numeric_limits<float>::infinity()), 1.f);
    }

    TEST(RenderLightFade, aBeginBelowZeroCountsAsZeroAndAnEndBelowTheBeginIsACutAtTheBegin)
    {
        EXPECT_FLOAT_EQ(factor(500.f, -100.f, 1000.f), 0.5f);
        EXPECT_FLOAT_EQ(factor(1999.f, 2000.f, 1000.f), 1.f);
        EXPECT_FLOAT_EQ(factor(2000.f, 2000.f, 1000.f), 0.f);
    }

    TEST(RenderLightFadeFarPlane, aClipDistanceBelowTheFarPlaneWins)
    {
        EXPECT_FLOAT_EQ(effectiveFarPlane(7168.f, 2000.f), 2000.f);
        EXPECT_FLOAT_EQ(effectiveFarPlane(40000.f, 12000.f), 12000.f);
    }

    TEST(RenderLightFadeFarPlane, noClipADistanceAboveTheFarPlaneOrNonsenseKeepsTheFarPlane)
    {
        EXPECT_FLOAT_EQ(effectiveFarPlane(7168.f, 0.f), 7168.f);
        EXPECT_FLOAT_EQ(effectiveFarPlane(7168.f, -1.f), 7168.f);
        EXPECT_FLOAT_EQ(effectiveFarPlane(7168.f, 9000.f), 7168.f);
        EXPECT_FLOAT_EQ(effectiveFarPlane(7168.f, 7168.f), 7168.f);
        EXPECT_FLOAT_EQ(effectiveFarPlane(7168.f, kNan), 7168.f);
    }
}
