// Task 0361 (a): the exposure a game pack's exterior is lifted by at dusk and night. Expectations written by hand.
#include <gtest/gtest.h>

#include <limits>

#include "apps/openmw/mwworld/neutralweather.hpp"

namespace
{
    using MWWorld::neutralNightLiftStops;

    TEST(NeutralNightLift, daylight_is_not_lifted)
    {
        EXPECT_FLOAT_EQ(neutralNightLiftStops(0.f, 1.5f), 0.f);
    }

    TEST(NeutralNightLift, full_night_is_the_whole_setting)
    {
        EXPECT_FLOAT_EQ(neutralNightLiftStops(1.f, 1.5f), 1.5f);
    }

    TEST(NeutralNightLift, a_third_of_the_night_is_a_third_of_the_setting)
    {
        EXPECT_NEAR(neutralNightLiftStops(1.f / 3.f, 1.5f), 0.5f, 1e-6f);
    }

    TEST(NeutralNightLift, a_setting_of_zero_lifts_nothing)
    {
        EXPECT_FLOAT_EQ(neutralNightLiftStops(1.f, 0.f), 0.f);
    }

    TEST(NeutralNightLift, shares_and_settings_are_cut_to_their_ranges)
    {
        EXPECT_FLOAT_EQ(neutralNightLiftStops(2.f, 1.f), 1.f);
        EXPECT_FLOAT_EQ(neutralNightLiftStops(-1.f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(neutralNightLiftStops(1.f, 9.f), 3.f);
        EXPECT_FLOAT_EQ(neutralNightLiftStops(1.f, -2.f), 0.f);
    }

    TEST(NeutralNightLift, input_that_is_not_a_number_lifts_nothing)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EXPECT_FLOAT_EQ(neutralNightLiftStops(nan, 1.5f), 0.f);
        EXPECT_FLOAT_EQ(neutralNightLiftStops(1.f, nan), 0.f);
        EXPECT_FLOAT_EQ(neutralNightLiftStops(inf, 1.5f), 0.f);
        EXPECT_FLOAT_EQ(neutralNightLiftStops(1.f, inf), 0.f);
    }
}
