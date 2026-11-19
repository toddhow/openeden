// Task 0358: the lights `--bake-gi` finds in a neutral (Skyrim) interior are the lights the engine draws there: the
// same colour byte, radius and gain, none for a light that is off by default.
#include <gtest/gtest.h>

#include <components/esm3/loadligh.hpp>

#include "apps/openmw/mwworld/neutrallight.hpp"

namespace
{
    using namespace openeden::sdk::game;

    LightComponent lightOf(double radius, std::uint8_t r, std::uint8_t g, std::uint8_t b, double fade)
    {
        LightComponent l;
        l.radius = radius;
        l.red = r;
        l.green = g;
        l.blue = b;
        l.fade = fade;
        return l;
    }

    TEST(NeutralGiLight, aLightThatIsOffByDefaultIsNotBaked)
    {
        LightComponent l = lightOf(256, 255, 200, 100, 1.0);
        l.offByDefault = true;
        EXPECT_FALSE(MWWorld::neutralGiLight(l, true).has_value());
    }

    TEST(NeutralGiLight, aLightWithNoRadiusIsNotBaked)
    {
        EXPECT_FALSE(MWWorld::neutralGiLight(lightOf(0, 255, 200, 100, 1.0), true).has_value());
    }

    TEST(NeutralGiLight, theColourAndRadiusAreTheEnginesLights)
    {
        const LightComponent l = lightOf(300, 200, 100, 50, 1.0);
        const auto baked = MWWorld::neutralGiLight(l, true);
        ASSERT_TRUE(baked.has_value());
        const ESM::Light record = MWWorld::makeNeutralEsmLight(l, true);
        EXPECT_EQ(baked->mColor, static_cast<std::uint32_t>(record.mData.mColor));
        EXPECT_FLOAT_EQ(baked->mRadius, static_cast<float>(record.mData.mRadius));
        EXPECT_FLOAT_EQ(baked->mRadius, 300.f);
        EXPECT_FLOAT_EQ(baked->mGain, 1.f);
        EXPECT_FALSE(baked->mNegative);
    }

    TEST(NeutralGiLight, aFadeAboveTheColourBytesRoomIsAGainOnlyWhenAskedFor)
    {
        const LightComponent l = lightOf(256, 200, 100, 50, 3.0); // 3 x 200 = 600, the byte stops at 255
        const auto with = MWWorld::neutralGiLight(l, true);
        const auto without = MWWorld::neutralGiLight(l, false);
        ASSERT_TRUE(with.has_value());
        ASSERT_TRUE(without.has_value());
        EXPECT_NEAR(with->mGain, 3.0f / (255.0f / 200.0f), 1e-4f);
        EXPECT_FLOAT_EQ(without->mGain, 1.f);
        EXPECT_EQ(with->mColor, without->mColor);
    }

    TEST(NeutralGiLight, aNegativeLightSaysSo)
    {
        LightComponent l = lightOf(256, 100, 100, 100, 1.0);
        l.negative = true;
        const auto baked = MWWorld::neutralGiLight(l, true);
        ASSERT_TRUE(baked.has_value());
        EXPECT_TRUE(baked->mNegative);
    }
}
