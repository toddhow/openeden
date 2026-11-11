// Task 0113 (neutral world, stage N3a-5): lights in the engine. The conversion of the SDK's LightComponent to the
// engine's own ESM::Light (the record its light path reads), the cell record's lighting, the light as a fact of the base
// record, and the Morrowind projection of a light record.
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>

#include <components/esm3/loadligh.hpp>

#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutrallight.hpp"
#include "apps/openmw/mwworld/neutralprojection.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"

namespace
{
    using namespace openeden::sdk::game;

    LightComponent light(double radius, std::uint8_t r, std::uint8_t g, std::uint8_t b, double fade = 1.0)
    {
        LightComponent l;
        l.radius = radius;
        l.red = r;
        l.green = g;
        l.blue = b;
        l.fade = fade;
        return l;
    }

    // ---- colour ----------------------------------------------------------------------------------------------

    TEST(NeutralLightColor, aFadeOfOneKeepsTheColour)
    {
        EXPECT_EQ(MWWorld::neutralLightColor(light(1, 255, 160, 80)), (std::array<std::uint8_t, 3>{ 255, 160, 80 }));
    }

    TEST(NeutralLightColor, aFadeBelowOneDimsAndAboveOneBrightensWhileTheColourHasRoom)
    {
        EXPECT_EQ(MWWorld::neutralLightColor(light(1, 200, 100, 50, 0.5)), (std::array<std::uint8_t, 3>{ 100, 50, 25 }));
        EXPECT_EQ(MWWorld::neutralLightColor(light(1, 100, 50, 25, 2.0)), (std::array<std::uint8_t, 3>{ 200, 100, 50 }));
    }

    TEST(NeutralLightColor, aFadeThatWouldPassTheByteIsCappedAtTheBrightestChannelAndKeepsTheHue)
    {
        // 240 * 10 would pass 255; the scale is cut to 255 / 240 = 17 / 16 (exact in binary), so the ratios stay: 255, 127.5 -> 128, 63.75 -> 64.
        EXPECT_EQ(MWWorld::neutralLightColor(light(1, 240, 120, 60, 10.0)), (std::array<std::uint8_t, 3>{ 255, 128, 64 }));
        // An already saturated colour cannot get brighter.
        EXPECT_EQ(MWWorld::neutralLightColor(light(1, 255, 0, 0, 3.0)), (std::array<std::uint8_t, 3>{ 255, 0, 0 }));
    }

    TEST(NeutralLightColor, aFadeOfZeroIsDarkAndAnUnusableFadeLeavesTheColour)
    {
        EXPECT_EQ(MWWorld::neutralLightColor(light(1, 9, 8, 7, 0.0)), (std::array<std::uint8_t, 3>{ 0, 0, 0 }));
        EXPECT_EQ(MWWorld::neutralLightColor(light(1, 9, 8, 7, -1.0)), (std::array<std::uint8_t, 3>{ 9, 8, 7 }));
        EXPECT_EQ(MWWorld::neutralLightColor(light(1, 9, 8, 7, std::numeric_limits<double>::quiet_NaN())),
            (std::array<std::uint8_t, 3>{ 9, 8, 7 }));
        EXPECT_EQ(MWWorld::neutralLightColor(light(1, 0, 0, 0, 5.0)), (std::array<std::uint8_t, 3>{ 0, 0, 0 }));
    }

    TEST(NeutralLightColor, packingPutsRedInTheLowByteAsTheEngineReadsIt)
    {
        EXPECT_EQ(MWWorld::packNeutralColor({ 1, 2, 3 }), 0x00030201u);
        EXPECT_EQ(MWWorld::packNeutralColor({ 255, 255, 255 }), 0x00FFFFFFu);
    }

    // ---- the engine's light record -------------------------------------------------------------------------------

    TEST(NeutralEsmLight, radiusAndColourArriveAsTheEnginesLightPathReadsThem)
    {
        const ESM::Light record = MWWorld::makeNeutralEsmLight(light(256.4, 255, 160, 80));
        EXPECT_EQ(record.mData.mRadius, 256);
        EXPECT_EQ(static_cast<std::uint32_t>(record.mData.mColor), 0x0050A0FFu);
        EXPECT_NE(record.mData.mFlags & ESM::Light::Dynamic, 0);
    }

    TEST(NeutralEsmLight, theAnimationSetsExactlyOneOfTheEnginesFourBits)
    {
        const auto flagsOf = [](LightAnimation a) {
            LightComponent l = light(1, 1, 1, 1);
            l.animation = a;
            return MWWorld::makeNeutralEsmLight(l).mData.mFlags;
        };
        const int animBits = ESM::Light::Flicker | ESM::Light::FlickerSlow | ESM::Light::Pulse | ESM::Light::PulseSlow;
        EXPECT_EQ(flagsOf(LightAnimation::None) & animBits, 0);
        EXPECT_EQ(flagsOf(LightAnimation::Flicker) & animBits, ESM::Light::Flicker);
        EXPECT_EQ(flagsOf(LightAnimation::FlickerSlow) & animBits, ESM::Light::FlickerSlow);
        EXPECT_EQ(flagsOf(LightAnimation::Pulse) & animBits, ESM::Light::Pulse);
        EXPECT_EQ(flagsOf(LightAnimation::PulseSlow) & animBits, ESM::Light::PulseSlow);
    }

    TEST(NeutralEsmLight, negativeIsAFlagAndOffByDefaultIsNot)
    {
        LightComponent l = light(1, 1, 1, 1);
        l.negative = true;
        l.offByDefault = true;
        const int flags = MWWorld::makeNeutralEsmLight(l).mData.mFlags;
        EXPECT_NE(flags & ESM::Light::Negative, 0);
        EXPECT_EQ(flags & ESM::Light::OffDefault, 0) << "the class decides whether to ask for the light at all";
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(light(1, 1, 1, 1)).mData.mFlags & ESM::Light::Negative, 0);
    }

    TEST(NeutralEsmLight, hostileRadiiAreClamped)
    {
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(light(-5.0, 1, 1, 1)).mData.mRadius, 0);
        EXPECT_EQ(
            MWWorld::makeNeutralEsmLight(light(std::numeric_limits<double>::quiet_NaN(), 1, 1, 1)).mData.mRadius, 0);
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(light(1.0e30, 1, 1, 1)).mData.mRadius,
            std::numeric_limits<std::int32_t>::max());
    }

    // ---- the cell record -------------------------------------------------------------------------------------

    TEST(NeutralCellLighting, aCellWithLightingUsesItAndOneWithoutKeepsTheFlatAmbient)
    {
        WorldCell cell;
        cell.id = 7;
        cell.editorId = "Lit";
        cell.interior = true;
        const ESM::Cell flat = MWWorld::makeNeutralCellRecord("skyrimse", cell);
        EXPECT_EQ(flat.mAmbi.mAmbient, MWWorld::kNeutralCellAmbient);

        CellLighting lighting;
        lighting.ambient = { 20, 19, 18 };
        lighting.directional = { 64, 73, 74 };
        lighting.fogColor = { 88, 83, 71 };
        lighting.fogNear = 512;
        lighting.fogFar = 2000;
        cell.lighting = lighting;
        const ESM::Cell lit = MWWorld::makeNeutralCellRecord("skyrimse", cell);
        EXPECT_EQ(lit.mAmbi.mAmbient, 0x00121314u);
        EXPECT_EQ(lit.mAmbi.mSunlight, 0x004A4940u);
        EXPECT_EQ(lit.mAmbi.mFog, 0x00475358u);
        EXPECT_EQ(lit.mAmbi.mFogDensity, 0.f) << "Morrowind's fog density has no counterpart in a pair of distances";
    }

    // ---- the base record -------------------------------------------------------------------------------------------

    WorldObject lightObject(WorldId placement, WorldId base, const LightComponent& l)
    {
        WorldObject o;
        o.id = placement;
        o.base = base;
        o.kind = ObjectKind::Light;
        o.baseEditorId = "Torch";
        o.modelPath = "lights\\torch.nif";
        o.light = l;
        return o;
    }

    TEST(NeutralBaseLight, theLightIsAFactOfTheBase)
    {
        using Status = MWWorld::Store<MWWorld::NeutralBase>::Status;
        MWWorld::Store<MWWorld::NeutralBase> store;
        const auto first = store.addFromObject("skyrimse", lightObject(1, 50, light(256, 255, 160, 80)));
        ASSERT_EQ(first.mStatus, Status::Added);
        ASSERT_TRUE(first.mBase->mLight);
        EXPECT_EQ(first.mBase->mLight->radius, 256.0);
        // Another placement of the same base with the same light is the same record.
        const auto second = store.addFromObject("skyrimse", lightObject(2, 50, light(256, 255, 160, 80)));
        EXPECT_EQ(second.mStatus, Status::Existing);
        // A different light under the same base is a conflict and the first one stays.
        const auto third = store.addFromObject("skyrimse", lightObject(3, 50, light(300, 255, 160, 80)));
        EXPECT_EQ(third.mStatus, Status::Conflict);
        EXPECT_EQ(third.mBase->mLight->radius, 256.0);
        // An object without a light is another set of facts for that base id.
        WorldObject plain = lightObject(4, 50, light(256, 255, 160, 80));
        plain.light.reset();
        EXPECT_EQ(store.addFromObject("skyrimse", plain).mStatus, Status::Conflict);
    }

    // ---- the Morrowind projection of a light record ------------------------------------------------------------

    TEST(NeutralProjectionLight, aMorrowindLightRecordBecomesTheSameComponentTheEngineWouldDraw)
    {
        ESM::Light record;
        record.blank();
        record.mData.mRadius = 177;
        record.mData.mColor = 0x00508CC8; // red 0xC8, green 0x8C, blue 0x50
        record.mData.mFlags = ESM::Light::Dynamic | ESM::Light::Carry | ESM::Light::Flicker | ESM::Light::OffDefault;
        const LightComponent c = MWWorld::lightOf(record);
        EXPECT_EQ(c.radius, 177.0);
        EXPECT_EQ(c.red, 0xC8);
        EXPECT_EQ(c.green, 0x8C);
        EXPECT_EQ(c.blue, 0x50);
        EXPECT_EQ(c.fade, 1.0);
        EXPECT_EQ(c.animation, LightAnimation::Flicker);
        EXPECT_TRUE(c.offByDefault);
        EXPECT_FALSE(c.negative);
        EXPECT_EQ(c.flags, static_cast<std::uint32_t>(record.mData.mFlags));
    }

    TEST(NeutralProjectionLight, theConversionBackGivesTheRecordTheEngineStartedFromForTheDrawnFields)
    {
        // Morrowind record -> neutral component -> the engine's light record (what a neutral-world Morrowind light
        // would be drawn from): radius, colour and the animation bit agree with the original.
        for (int animation : { 0, int(ESM::Light::Flicker), int(ESM::Light::FlickerSlow), int(ESM::Light::Pulse),
                 int(ESM::Light::PulseSlow) })
        {
            ESM::Light record;
            record.blank();
            record.mData.mRadius = 300;
            record.mData.mColor = 0x00102030;
            record.mData.mFlags = ESM::Light::Dynamic | animation | ESM::Light::Negative;
            const ESM::Light back = MWWorld::makeNeutralEsmLight(MWWorld::lightOf(record));
            EXPECT_EQ(back.mData.mRadius, record.mData.mRadius);
            EXPECT_EQ(static_cast<std::uint32_t>(back.mData.mColor), static_cast<std::uint32_t>(record.mData.mColor));
            const int bits = ESM::Light::Flicker | ESM::Light::FlickerSlow | ESM::Light::Pulse | ESM::Light::PulseSlow
                | ESM::Light::Negative;
            EXPECT_EQ(back.mData.mFlags & bits, record.mData.mFlags & bits);
        }
    }
}
