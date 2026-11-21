// Task 0362 item 2: a Skyrim light's movement amplitude reaches the engine's light record
// (ESM::Light::mNeutralMovement), cut to a quarter of the radius and 64 units, only for a light with its own flicker
// and only when asked.
#include <gtest/gtest.h>

#include <limits>

#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/lightflicker.hpp>

#include "apps/openmw/mwworld/neutrallight.hpp"

namespace
{
    using namespace openeden::sdk::game;

    LightComponent light(double radius, double movement, LightAnimation animation = LightAnimation::Flicker)
    {
        LightComponent l;
        l.radius = radius;
        l.animation = animation;
        l.flickerPeriod = 0.3;
        l.flickerIntensity = 0.4;
        l.flickerMovement = movement;
        return l;
    }

    TEST(NeutralLightMovement, theAmplitudeIsKeptUpToAQuarterOfTheRadius)
    {
        EXPECT_FLOAT_EQ(MWWorld::neutralLightMovement(light(256, 5.0)), 5.f);
        EXPECT_FLOAT_EQ(MWWorld::neutralLightMovement(light(256, 100.0)), 64.f); // a quarter of 256 is 64
        EXPECT_FLOAT_EQ(MWWorld::neutralLightMovement(light(80, 100.0)), 20.f); // a quarter of 80
        EXPECT_FLOAT_EQ(MWWorld::neutralLightMovement(light(2048, 512.0)), 64.f); // the absolute limit
    }

    TEST(NeutralLightMovement, noneOrUnusableNumbersAreZero)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        EXPECT_EQ(MWWorld::neutralLightMovement(light(256, 0.0)), 0.f);
        EXPECT_EQ(MWWorld::neutralLightMovement(light(256, -4.0)), 0.f);
        EXPECT_EQ(MWWorld::neutralLightMovement(light(256, nan)), 0.f);
        EXPECT_EQ(MWWorld::neutralLightMovement(light(nan, 4.0)), 0.f);
        EXPECT_EQ(MWWorld::neutralLightMovement(light(0, 4.0)), 0.f);
    }

    TEST(NeutralLightMovement, theRecordCarriesItOnlyWhenAskedAndOnlyWithItsOwnFlicker)
    {
        const LightComponent l = light(256, 5.0);
        EXPECT_FLOAT_EQ(MWWorld::makeNeutralEsmLight(l, false, true, 1u, false, true).mNeutralMovement, 5.f);
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(l, false, true, 1u, false, false).mNeutralMovement, 0.f);
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(l).mNeutralMovement, 0.f);
        // no flicker of its own (the setting off): the movement has nothing to ride on
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(l, false, false, 1u, false, true).mNeutralMovement, 0.f);
        // a light that does not animate
        const LightComponent plain = light(256, 5.0, LightAnimation::None);
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(plain, false, true, 1u, false, true).mNeutralMovement, 0.f);
    }
}
