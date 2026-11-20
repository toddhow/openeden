// Task 0361b follow-up: the weather's image space CONTRAST. The game's contrast pivots on the scene's average
// brightness; ours pivots on mid grey of the display value, so a contrast above 1 crushes every shadow to black (the
// Whiterun ground in a cliff's shadow: display value 0.08 became 0). Contrast is left out of the weather grade as it is
// of the cell grade (neutralcellatmosphere.hpp). Hand-worked numbers.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "apps/openmw/mwworld/neutralcellatmosphere.hpp"

namespace
{
    TEST(MWWorldNeutralWeatherGrade, the_default_share_leaves_contrast_neutral)
    {
        EXPECT_FLOAT_EQ(MWWorld::neutralWeatherGradeContrast(1.4f), 1.f);
        EXPECT_FLOAT_EQ(MWWorld::neutralWeatherGradeContrast(0.5f), 1.f);
    }

    TEST(MWWorldNeutralWeatherGrade, a_share_scales_the_distance_from_one)
    {
        EXPECT_FLOAT_EQ(MWWorld::neutralWeatherGradeContrast(1.4f, 1.f), 1.4f);
        EXPECT_FLOAT_EQ(MWWorld::neutralWeatherGradeContrast(1.4f, 0.5f), 1.2f);
        EXPECT_FLOAT_EQ(MWWorld::neutralWeatherGradeContrast(0.8f, 0.5f), 0.9f);
    }

    TEST(MWWorldNeutralWeatherGrade, a_value_that_is_not_a_number_is_neutral)
    {
        EXPECT_FLOAT_EQ(MWWorld::neutralWeatherGradeContrast(std::numeric_limits<float>::quiet_NaN(), 1.f), 1.f);
        EXPECT_FLOAT_EQ(MWWorld::neutralWeatherGradeContrast(1.4f, std::numeric_limits<float>::infinity()), 1.f);
    }

    TEST(MWWorldNeutralWeatherGrade, a_shadow_is_not_crushed_to_black_by_the_default)
    {
        const float shadow = 0.08f; // the display value of the ground in a cliff's shadow
        const float contrast = MWWorld::neutralWeatherGradeContrast(1.4f);
        EXPECT_GT((shadow - 0.5f) * contrast + 0.5f, 0.05f);
    }
}
