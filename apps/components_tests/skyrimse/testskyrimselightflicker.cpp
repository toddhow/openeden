// Task 0355 stage h: the pack hands a LIGH record's own flicker numbers (period, intensity amplitude, movement
// amplitude: LIGH DATA +28, +32, +36) to the engine in the neutral LightComponent. Pure, no game data.
#include <game_skyrimse/neutral_world.hpp>

#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace
{
    using namespace game_skyrimse;
    namespace sdkgame = openeden::sdk::game;

    LightData flicker(std::uint32_t flags, float period, float intensity, float movement)
    {
        LightData d;
        d.mRadius = 256;
        d.mFlags = flags;
        d.mFlickerPeriod = period;
        d.mFlickerIntensity = intensity;
        d.mFlickerMovement = movement;
        return d;
    }

    TEST(SkyrimLightFlicker, theRecordsThreeNumbersReachTheComponent)
    {
        ComponentStats stats;
        const auto c = makeLightComponent(flicker(LightFlags::Flicker, 0.3333f, 0.4f, 4.f), stats, NeutralLimits{});
        ASSERT_TRUE(c.has_value());
        EXPECT_EQ(c->animation, sdkgame::LightAnimation::Flicker);
        EXPECT_NEAR(c->flickerPeriod, 0.3333, 1e-4);
        EXPECT_NEAR(c->flickerIntensity, 0.4, 1e-6);
        EXPECT_NEAR(c->flickerMovement, 4.0, 1e-6);
    }

    TEST(SkyrimLightFlicker, aLightThatDoesNotAnimateHandsOnNothing)
    {
        ComponentStats stats;
        const auto c = makeLightComponent(flicker(0, 0.3f, 0.4f, 4.f), stats, NeutralLimits{});
        ASSERT_TRUE(c.has_value());
        EXPECT_EQ(c->animation, sdkgame::LightAnimation::None);
        EXPECT_EQ(c->flickerPeriod, 0.0);
        EXPECT_EQ(c->flickerIntensity, 0.0);
        EXPECT_EQ(c->flickerMovement, 0.0);
    }

    TEST(SkyrimLightFlicker, valuesThatAreNotFiniteOrNegativeBecomeZero)
    {
        ComponentStats stats;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        auto c = makeLightComponent(flicker(LightFlags::Pulse, nan, 0.4f, inf), stats, NeutralLimits{});
        ASSERT_TRUE(c.has_value());
        EXPECT_EQ(c->flickerPeriod, 0.0);
        EXPECT_EQ(c->flickerMovement, 0.0);
        EXPECT_NEAR(c->flickerIntensity, 0.4, 1e-6);
        c = makeLightComponent(flicker(LightFlags::Pulse, -1.f, -0.4f, -4.f), stats, NeutralLimits{});
        ASSERT_TRUE(c.has_value());
        EXPECT_EQ(c->flickerPeriod, 0.0);
        EXPECT_EQ(c->flickerIntensity, 0.0);
        EXPECT_EQ(c->flickerMovement, 0.0);
    }

    TEST(SkyrimLightFlicker, theComponentsOtherFieldsAreUnchanged)
    {
        ComponentStats stats;
        LightData d = flicker(LightFlags::FlickerSlow, 1.f, 0.f, 0.f);
        d.mColor = { 10, 20, 30, 0 };
        const auto c = makeLightComponent(d, stats, NeutralLimits{});
        ASSERT_TRUE(c.has_value());
        EXPECT_EQ(c->animation, sdkgame::LightAnimation::FlickerSlow);
        EXPECT_EQ(c->red, 10);
        EXPECT_EQ(c->green, 20);
        EXPECT_EQ(c->blue, 30);
        EXPECT_EQ(c->flickerPeriod, 1.0);
        EXPECT_EQ(c->flickerIntensity, 0.0);
    }
}
