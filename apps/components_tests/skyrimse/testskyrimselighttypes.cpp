// Task 0355 stage g: what a Skyrim LIGH record's flag word says about the light's kind. The bit meanings are the
// Mutagen Skyrim schema's (SpotLight 0x200, ShadowSpotlight 0x400, ShadowHemisphere 0x800, ShadowOmnidirectional
// 0x1000, PortalStrict 0x2000 = a culling hint, not a shadow); stage a had them shifted by one place. Pure: no plugin.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/records.hpp>

#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace game_skyrimse;
    namespace sdkgame = openeden::sdk::game;

    LightData data(std::uint32_t flags, float fov = 90.f)
    {
        LightData d;
        d.mRadius = 256;
        d.mColor = { 200, 100, 50, 0 };
        d.mFlags = flags;
        d.mFov = fov;
        return d;
    }

    sdkgame::LightComponent component(const LightData& d)
    {
        ComponentStats stats;
        const auto c = makeLightComponent(d, stats, {});
        EXPECT_TRUE(c.has_value());
        return c.value_or(sdkgame::LightComponent{});
    }

    TEST(SkyrimLightTypes, theFlagBitsAreMutagensNotTheShiftedOnesOfStageA)
    {
        EXPECT_EQ(LightFlags::SpotLight, 0x200u);
        EXPECT_EQ(LightFlags::SpotShadow, 0x400u);
        EXPECT_EQ(LightFlags::HemisphereShadow, 0x800u);
        EXPECT_EQ(LightFlags::OmnidirectionalShadow, 0x1000u);
        EXPECT_EQ(LightFlags::PortalStrict, 0x2000u);
    }

    TEST(SkyrimLightTypes, aSpotShadowLightIsASpotThatCastsWithItsFov)
    {
        const auto c = component(data(LightFlags::Dynamic | LightFlags::SpotShadow, 45.f));
        EXPECT_DOUBLE_EQ(c.spotFov, 45.0);
        EXPECT_EQ(c.shadow, sdkgame::LightShadow::Casts);
    }

    TEST(SkyrimLightTypes, theHemisphereAndOmnidirectionalBitsCastButAreNotSpots)
    {
        const auto hemi = component(data(LightFlags::Dynamic | LightFlags::HemisphereShadow));
        EXPECT_EQ(hemi.shadow, sdkgame::LightShadow::Casts);
        EXPECT_EQ(hemi.spotFov, 0.0);
        const auto omni = component(data(LightFlags::Flicker | LightFlags::OmnidirectionalShadow));
        EXPECT_EQ(omni.shadow, sdkgame::LightShadow::Casts);
        EXPECT_EQ(omni.spotFov, 0.0);
    }

    TEST(SkyrimLightTypes, aLightWithNoShadowBitNeverCastsAndPortalStrictIsNotAShadow)
    {
        EXPECT_EQ(component(data(LightFlags::Dynamic)).shadow, sdkgame::LightShadow::Never);
        EXPECT_EQ(component(data(LightFlags::Dynamic | LightFlags::PortalStrict)).shadow, sdkgame::LightShadow::Never);
        EXPECT_EQ(component(data(0)).spotFov, 0.0);
    }

    TEST(SkyrimLightTypes, theSpotBitAloneIsAConeToo)
    {
        const auto c = component(data(LightFlags::SpotLight, 60.f));
        EXPECT_DOUBLE_EQ(c.spotFov, 60.0);
        EXPECT_EQ(c.shadow, sdkgame::LightShadow::Never);
    }

    TEST(SkyrimLightTypes, aFovWithoutTheSpotBitIsIgnoredAndAnAbsurdFovIsNoCone)
    {
        EXPECT_EQ(component(data(LightFlags::OmnidirectionalShadow, 30.f)).spotFov, 0.0);
        for (const float fov : { 0.f, -5.f, 0.5f, 171.f, 360.f, std::numeric_limits<float>::quiet_NaN(),
                 std::numeric_limits<float>::infinity() })
            EXPECT_EQ(component(data(LightFlags::SpotShadow, fov)).spotFov, 0.0) << fov;
        EXPECT_DOUBLE_EQ(component(data(LightFlags::SpotShadow, 170.f)).spotFov, 170.0);
        EXPECT_DOUBLE_EQ(component(data(LightFlags::SpotShadow, 1.f)).spotFov, 1.0);
    }

    TEST(SkyrimLightTypes, theRawFlagWordAndTheOtherFieldsAreUntouched)
    {
        const auto c = component(data(LightFlags::Dynamic | LightFlags::Flicker | LightFlags::SpotShadow, 90.f));
        EXPECT_EQ(c.flags, LightFlags::Dynamic | LightFlags::Flicker | LightFlags::SpotShadow);
        EXPECT_EQ(c.animation, sdkgame::LightAnimation::Flicker);
    }

    TEST(SkyrimLightTypes, aDefaultComponentSaysNothing)
    {
        const sdkgame::LightComponent plain;
        EXPECT_EQ(plain.spotFov, 0.0);
        EXPECT_EQ(plain.shadow, sdkgame::LightShadow::Unspecified);
    }
}
