// Task 0355 stage g: makeNeutralEsmLight keeps a spot light's cone and the game's shadow rule in the engine's light
// record when asked for ([Game] neutral light types); without it the record is as before.
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <components/esm3/loadligh.hpp>

#include "apps/openmw/mwworld/neutrallight.hpp"

namespace
{
    using namespace openeden::sdk::game;

    constexpr int typeBits = ESM::Light::NeutralSpot | ESM::Light::NeutralShadowCasts | ESM::Light::NeutralShadowNever;

    LightComponent light(double spotFov, LightShadow shadow)
    {
        LightComponent l;
        l.radius = 256;
        l.red = 200;
        l.green = 100;
        l.blue = 50;
        l.spotFov = spotFov;
        l.shadow = shadow;
        return l;
    }

    TEST(NeutralLightTypes, aSpotAndItsShadowRuleAreKeptWhenAskedFor)
    {
        const ESM::Light r = MWWorld::makeNeutralEsmLight(light(45.0, LightShadow::Casts), false, false, 0u, true);
        EXPECT_NE(r.mData.mFlags & ESM::Light::NeutralSpot, 0);
        EXPECT_FLOAT_EQ(r.mNeutralSpotFov, 45.f);
        EXPECT_NE(r.mData.mFlags & ESM::Light::NeutralShadowCasts, 0);
        EXPECT_EQ(r.mData.mFlags & ESM::Light::NeutralShadowNever, 0);
        EXPECT_NE(r.mData.mFlags & ESM::Light::Dynamic, 0);
    }

    TEST(NeutralLightTypes, neverIsKeptAndUnspecifiedIsNoRule)
    {
        const ESM::Light never = MWWorld::makeNeutralEsmLight(
            light(0.0, LightShadow::Never), false, false, 0u, true);
        EXPECT_NE(never.mData.mFlags & ESM::Light::NeutralShadowNever, 0);
        EXPECT_EQ(never.mData.mFlags & (ESM::Light::NeutralShadowCasts | ESM::Light::NeutralSpot), 0);
        const ESM::Light none = MWWorld::makeNeutralEsmLight(
            light(0.0, LightShadow::Unspecified), false, false, 0u, true);
        EXPECT_EQ(none.mData.mFlags & typeBits, 0);
    }

    TEST(NeutralLightTypes, withoutTheSettingTheRecordIsAsBefore)
    {
        const LightComponent l = light(45.0, LightShadow::Casts);
        const ESM::Light off = MWWorld::makeNeutralEsmLight(l, false, false);
        const ESM::Light old = MWWorld::makeNeutralEsmLight(l, false);
        EXPECT_EQ(off.mData.mFlags & typeBits, 0);
        EXPECT_EQ(off.mNeutralSpotFov, 0.f);
        EXPECT_EQ(off.mData.mFlags, old.mData.mFlags);
        EXPECT_EQ(off.mData.mColor, old.mData.mColor);
        EXPECT_EQ(off.mData.mRadius, old.mData.mRadius);
    }

    TEST(NeutralLightTypes, anAbsurdConeIsNoSpot)
    {
        for (const double fov : { 0.0, 0.5, 171.0, -4.0, std::numeric_limits<double>::quiet_NaN(),
                 std::numeric_limits<double>::infinity() })
        {
            const ESM::Light r = MWWorld::makeNeutralEsmLight(light(fov, LightShadow::Casts), false, false, 0u, true);
            EXPECT_EQ(r.mData.mFlags & ESM::Light::NeutralSpot, 0) << fov;
            EXPECT_EQ(r.mNeutralSpotFov, 0.f) << fov;
        }
    }

    TEST(NeutralLightTypes, theNewBitsAreNotFileFormatBitsAndBlankClearsTheCone)
    {
        EXPECT_GT(ESM::Light::NeutralSpot, ESM::Light::PulseSlow);
        EXPECT_NE(ESM::Light::NeutralSpot, ESM::Light::NeutralShadowCasts);
        EXPECT_NE(ESM::Light::NeutralShadowCasts, ESM::Light::NeutralShadowNever);
        EXPECT_NE(ESM::Light::NeutralShadowNever, ESM::Light::NeutralGain);
        ESM::Light r;
        r.mNeutralSpotFov = 70.f;
        r.blank();
        EXPECT_EQ(r.mNeutralSpotFov, 0.f);
    }
}
