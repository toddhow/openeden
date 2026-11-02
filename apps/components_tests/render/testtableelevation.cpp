// Tests for Render::Sun::tableElevation (task 0034 decision 4, task 0047 D4).
//
// Written from the task text alone: a smooth function of the game hour that is 0 in the middle of Morrowind's sunrise
// and sunset windows, highest in the middle of the day, negative at night, continuous across midnight, and never
// above the elevation of the weather manager's own noon sun.

#include <components/render/sunmath.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace Render::Sun;

    // Morrowind's defaults: sunrise 6 for 2 hours, sunset 18 for 2 hours (Weather_Sunrise_Time and the like).
    // The windows are [NightEnd, DayStart] = [6, 8] and [DayEnd, NightStart] = [18, 20].
    const TimeOfDayWindows defaults{ 6.f, 8.f, 18.f, 20.f };
    constexpr float degrees = 3.14159265f / 180.f;

    TEST(RenderSunTableElevation, ZeroInTheMiddleOfBothWindows)
    {
        EXPECT_NEAR(tableElevation(7.f, defaults), 0.f, 1e-5f);
        EXPECT_NEAR(tableElevation(19.f, defaults), 0.f, 1e-5f);
    }

    TEST(RenderSunTableElevation, HighestAtTheMiddleOfTheDayAndNeverAboveTheNoonSun)
    {
        const float noon = tableElevation(13.f, defaults);
        EXPECT_NEAR(noon, maxElevation, 1e-4f);
        EXPECT_LT(noon, 90.f * degrees);
        for (float h = 0.f; h < 24.f; h += 0.25f)
            EXPECT_LE(tableElevation(h, defaults), noon + 1e-5f) << h;
    }

    TEST(RenderSunTableElevation, MorrowindSunsetHourIsLowInTheSky)
    {
        // The point of the function: at 18:00 the colour tables are sunsetting, and the elevation is already low.
        EXPECT_LT(tableElevation(18.5f, defaults), 10.f * degrees);
        EXPECT_GT(tableElevation(18.5f, defaults), 0.f);
        // And lower than the drawn sun would be (about 30 degrees at 17:30), at the same hour.
        EXPECT_LT(tableElevation(17.5f, defaults), 25.f * degrees);
    }

    TEST(RenderSunTableElevation, NegativeAtNightAndSymmetric)
    {
        EXPECT_LT(tableElevation(22.f, defaults), 0.f);
        EXPECT_LT(tableElevation(2.f, defaults), 0.f);
        // Day: 12 hours from 7 to 19; mirrored around the middle.
        EXPECT_NEAR(tableElevation(10.f, defaults), tableElevation(16.f, defaults), 1e-4f);
        EXPECT_NEAR(tableElevation(22.f, defaults), tableElevation(4.f, defaults), 1e-4f);
        EXPECT_NEAR(tableElevation(1.f, defaults), -maxElevation, 1e-4f);
    }

    TEST(RenderSunTableElevation, ContinuousAcrossMidnightAndWrapsTheDay)
    {
        EXPECT_NEAR(tableElevation(23.999f, defaults), tableElevation(0.001f, defaults), 1e-3f);
        EXPECT_NEAR(tableElevation(24.f, defaults), tableElevation(0.f, defaults), 1e-5f);
        EXPECT_NEAR(tableElevation(30.f, defaults), tableElevation(6.f, defaults), 1e-4f);
        EXPECT_NEAR(tableElevation(-2.f, defaults), tableElevation(22.f, defaults), 1e-4f);
    }

    TEST(RenderSunTableElevation, ContinuousAtEveryWindowEdge)
    {
        for (const float edge : { 6.f, 8.f, 18.f, 20.f })
            EXPECT_NEAR(tableElevation(edge - 0.001f, defaults), tableElevation(edge + 0.001f, defaults), 2e-3f)
                << edge;
    }

    TEST(RenderSunTableElevation, SmoothSteps)
    {
        float previous = tableElevation(0.f, defaults);
        for (float h = 0.01f; h <= 24.f; h += 0.01f)
        {
            const float now = tableElevation(h, defaults);
            EXPECT_LT(std::abs(now - previous), 0.01f) << h;
            previous = now;
        }
    }

    TEST(RenderSunTableElevation, FollowsOtherWindows)
    {
        // A mod that moves the sunset to 21:00 for 1 hour: the zero is at 21.5.
        const TimeOfDayWindows late{ 5.f, 7.f, 21.f, 22.f };
        EXPECT_NEAR(tableElevation(6.f, late), 0.f, 1e-5f);
        EXPECT_NEAR(tableElevation(21.5f, late), 0.f, 1e-5f);
        EXPECT_GT(tableElevation(20.f, late), 0.f);
        EXPECT_LT(tableElevation(23.f, late), 0.f);
    }

    TEST(RenderSunTableElevation, BadInputGivesZeroNotNaN)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(tableElevation(nan, defaults), 0.f);
        const TimeOfDayWindows same{ 6.f, 6.f, 6.f, 6.f };
        EXPECT_FALSE(std::isnan(tableElevation(3.f, same)));
        const TimeOfDayWindows inf{ nan, nan, nan, nan };
        EXPECT_FALSE(std::isnan(tableElevation(3.f, inf)));
    }
}
