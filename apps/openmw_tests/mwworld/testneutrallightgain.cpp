// Task 0355 stage b: the brightness a Skyrim light's fade has beyond what the 8 bit colour of the engine's light record
// can hold (neutralLightColor stops at 255 in the brightest channel) becomes the record's NeutralGain multiplier when
// asked for; without it makeNeutralEsmLight is as before.
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <components/esm3/loadligh.hpp>

#include "apps/openmw/mwworld/neutrallight.hpp"

namespace
{
    using namespace openeden::sdk::game;

    LightComponent light(std::uint8_t r, std::uint8_t g, std::uint8_t b, double fade)
    {
        LightComponent l;
        l.radius = 256;
        l.red = r;
        l.green = g;
        l.blue = b;
        l.fade = fade;
        return l;
    }

    TEST(NeutralLightGain, aFadeTheColourHasRoomForNeedsNoGain)
    {
        EXPECT_EQ(MWWorld::neutralLightGain(light(85, 67, 40, 3.0)), 1.f);
        EXPECT_EQ(MWWorld::neutralLightGain(light(255, 160, 80, 1.0)), 1.f);
        EXPECT_EQ(MWWorld::neutralLightGain(light(255, 160, 80, 0.5)), 1.f);
    }

    TEST(NeutralLightGain, aFadeThatPassesTheByteKeepsTheRestAsGain)
    {
        // 255 * 3 = 765: the colour takes the byte (scale 1), the gain is the whole 3.
        EXPECT_FLOAT_EQ(MWWorld::neutralLightGain(light(255, 235, 153, 3.0)), 3.f);
        // 170 * 1.5 = 255 exactly: no gain.
        EXPECT_FLOAT_EQ(MWWorld::neutralLightGain(light(160, 170, 169, 1.5)), 1.f);
        // 200 with fade 2.5: the colour is scaled by 255 / 200 = 1.275, the gain is 2.5 / 1.275.
        EXPECT_NEAR(MWWorld::neutralLightGain(light(200, 100, 50, 2.5)), 2.5 / 1.275, 1e-5);
    }

    TEST(NeutralLightGain, theColourAndTheGainTogetherGiveTheFadedColour)
    {
        const LightComponent l = light(200, 100, 50, 2.5);
        const auto colour = MWWorld::neutralLightColor(l);
        const float gain = MWWorld::neutralLightGain(l);
        EXPECT_NEAR(colour[0] * gain / 255.0, 200 * 2.5 / 255.0, 0.01);
        EXPECT_NEAR(colour[1] * gain / 255.0, 100 * 2.5 / 255.0, 0.01);
    }

    TEST(NeutralLightGain, nothingForANegativeLightAnAbsurdFadeOrNoColour)
    {
        LightComponent dark = light(255, 255, 255, 4.0);
        dark.negative = true;
        EXPECT_EQ(MWWorld::neutralLightGain(dark), 1.f);
        EXPECT_EQ(MWWorld::neutralLightGain(light(0, 0, 0, 5.0)), 1.f);
        EXPECT_EQ(MWWorld::neutralLightGain(light(255, 255, 255, std::numeric_limits<double>::infinity())), 1.f);
        EXPECT_EQ(MWWorld::neutralLightGain(light(255, 255, 255, std::numeric_limits<double>::quiet_NaN())), 1.f);
        EXPECT_LE(MWWorld::neutralLightGain(light(255, 255, 255, 1.0e9)), 64.f);
    }

    TEST(NeutralLightGain, theRecordCarriesTheGainOnlyWhenAskedFor)
    {
        const LightComponent l = light(255, 201, 119, 3.0);
        const ESM::Light plain = MWWorld::makeNeutralEsmLight(l);
        EXPECT_EQ(plain.mData.mFlags & ESM::Light::NeutralGain, 0);
        const ESM::Light off = MWWorld::makeNeutralEsmLight(l, false);
        EXPECT_EQ(off.mData.mFlags, plain.mData.mFlags);
        EXPECT_EQ(off.mData.mWeight, plain.mData.mWeight);
        const ESM::Light on = MWWorld::makeNeutralEsmLight(l, true);
        EXPECT_NE(on.mData.mFlags & ESM::Light::NeutralGain, 0);
        EXPECT_FLOAT_EQ(on.mData.mWeight, 3.f);
        EXPECT_EQ(on.mData.mColor, plain.mData.mColor);
        EXPECT_EQ(on.mData.mRadius, plain.mData.mRadius);
    }

    TEST(NeutralLightGain, aLightThatNeedsNoGainHasNoFlagEvenWhenAskedFor)
    {
        const ESM::Light on = MWWorld::makeNeutralEsmLight(light(85, 67, 40, 3.0), true);
        EXPECT_EQ(on.mData.mFlags & ESM::Light::NeutralGain, 0);
    }
}
