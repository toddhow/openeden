// Task 0355 stage b: Skyrim's local lights (lantern, brazier, hearth, candle) make pools of light only if the engine's
// point light ends where Skyrim's does. These tests pin the radius mapping (the pack's NeutralLimits and
// SkyrimWorldView::setLightFalloff) and its effect on the engine's falloff, through the C++ twin of the shader's
// `attenuate` (Render::Gi::attenuate, components/render/probebake.hpp). Generated plugin only (no game data).
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <components/render/probebake.hpp>

#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdkgame = openeden::sdk::game;
    namespace fs = std::filesystem;

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6,
                            kGrupTemporary = 9;
    constexpr std::uint32_t kTorch = 0x00000811, kCell = 0x00000900, kRefTorch = 0x00000A02;

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    LightData data(std::uint32_t radius)
    {
        LightData d;
        d.mRadius = radius;
        d.mColor = { 200, 100, 50, 0 };
        return d;
    }

    // ---- the mapping ------------------------------------------------------------------------------------------

    TEST(SkyrimLocalLights, theDefaultLimitsKeepTheRecordsRadius)
    {
        ComponentStats stats;
        NeutralLimits limits;
        EXPECT_EQ(limits.mLightRadiusScale, 1.0);
        EXPECT_EQ(makeLightComponent(data(256), stats, limits)->radius, 256.0);
    }

    TEST(SkyrimLocalLights, aRadiusScaleGivesTheEngineThatShareOfTheRadiusAndChangesNothingElse)
    {
        ComponentStats stats;
        NeutralLimits limits;
        limits.mLightRadiusScale = kLightFalloffRadiusScale;
        LightData d = data(768);
        d.mFlags = LightFlags::Flicker;
        d.mHasFade = true;
        d.mFade = 3.0f;
        const auto c = makeLightComponent(d, stats, limits);
        ASSERT_TRUE(c);
        EXPECT_EQ(c->radius, 768.0 * kLightFalloffRadiusScale);
        EXPECT_EQ(c->red, 200);
        EXPECT_EQ(c->green, 100);
        EXPECT_EQ(c->blue, 50);
        EXPECT_EQ(c->fade, 3.0);
        EXPECT_EQ(c->animation, sdkgame::LightAnimation::Flicker);
    }

    TEST(SkyrimLocalLights, theHostileRadiusLimitStillLooksAtTheRecordsOwnRadius)
    {
        ComponentStats stats;
        NeutralLimits limits;
        limits.mLightRadiusScale = 0.5;
        EXPECT_TRUE(makeLightComponent(data(static_cast<std::uint32_t>(limits.mMaxLightRadius)), stats, limits));
        EXPECT_FALSE(makeLightComponent(data(static_cast<std::uint32_t>(limits.mMaxLightRadius) + 1), stats, limits));
    }

    TEST(SkyrimLocalLights, aScaleThatIsNotFiniteOrNotAboveZeroIsTakenAsOne)
    {
        ComponentStats stats;
        for (const double bad : { 0.0, -0.5, std::numeric_limits<double>::quiet_NaN(),
                 std::numeric_limits<double>::infinity() })
        {
            NeutralLimits limits;
            limits.mLightRadiusScale = bad;
            EXPECT_EQ(makeLightComponent(data(256), stats, limits)->radius, 256.0) << bad;
        }
    }

    // ---- what the engine's falloff does with it -----------------------------------------------------------------

    // Skyrim: full strength at the light, nothing at the radius, in a straight line (falloff exponent 1).
    double skyrimProfile(double distanceOverRadius)
    {
        return std::clamp(1.0 - distanceOverRadius, 0.0, 1.0);
    }

    double engineShare(double recordRadius, double scale, double distanceOverRadius)
    {
        const Render::Gi::LightFalloff falloff; // the Morrowind LightAttenuation_* values the game ships
        return Render::Gi::attenuate(falloff, static_cast<float>(recordRadius * scale),
            static_cast<float>(recordRadius * distanceOverRadius));
    }

    TEST(SkyrimLocalLights, theEnginesOwnFalloffStillGivesAThirdOfTheLightAtTheRecordsRadius)
    {
        // The control: with the record's radius the pool never ends (this is why rooms looked evenly lit).
        EXPECT_GT(engineShare(256, 1.0, 1.0), 0.3);
        EXPECT_GT(engineShare(256, 1.0, 1.5), 0.1);
    }

    TEST(SkyrimLocalLights, withTheMappedRadiusTheLightIsNearlyOutAtTheRecordsRadiusAndEndsSoonAfter)
    {
        EXPECT_LT(engineShare(256, kLightFalloffRadiusScale, 1.0), 0.1);
        EXPECT_EQ(engineShare(256, kLightFalloffRadiusScale, 1.25), 0.0);
        EXPECT_EQ(engineShare(256, kLightFalloffRadiusScale, 3.0), 0.0);
        // The bright core stays: a quarter of the way out it still gives at least two thirds of the colour.
        EXPECT_GT(engineShare(256, kLightFalloffRadiusScale, 0.25), 0.66);
        EXPECT_NEAR(engineShare(256, kLightFalloffRadiusScale, 0.0), 1.0, 1e-6);
    }

    // The light a flat floor gets in all, the share at distance d counted over the ring of that distance.
    double floorFlux(double scale, bool skyrim)
    {
        double sum = 0.0;
        const int n = 6000;
        const double top = 3.0;
        for (int i = 0; i < n; ++i)
        {
            const double d = (i + 0.5) / n * top;
            sum += (skyrim ? skyrimProfile(d) : engineShare(256, scale, d)) * d * (top / n);
        }
        return sum;
    }

    TEST(SkyrimLocalLights, theMappedLightGivesAFloorAboutTheSameTotalLightAsSkyrimsAndTheEnginesOwnGivesFarMore)
    {
        const double skyrim = floorFlux(1.0, true);
        EXPECT_NEAR(floorFlux(kLightFalloffRadiusScale, false) / skyrim, 1.0, 0.06);
        EXPECT_GT(floorFlux(1.0, false) / skyrim, 2.0);
    }

    TEST(SkyrimLocalLights, theMappedFalloffStaysCloseToSkyrimsAcrossTheWholeRadiusAndForEveryRadius)
    {
        for (const double radius : { 64.0, 128.0, 256.0, 768.0, 2000.0 })
            for (int i = 0; i <= 20; ++i)
            {
                const double d = i / 20.0;
                EXPECT_NEAR(engineShare(radius, kLightFalloffRadiusScale, d), skyrimProfile(d), 0.22)
                    << "radius " << radius << " d/r " << d;
            }
    }

    TEST(SkyrimLocalLights, beyondAThirdOfTheRadiusTheMappedLightIsNeverBrighterThanTheUnmappedOne)
    {
        for (int i = 4; i <= 20; ++i)
        {
            const double d = i / 20.0; // 0.2 to 1.0
            EXPECT_LE(engineShare(256, kLightFalloffRadiusScale, d), engineShare(256, 1.0, d) + 1e-9) << d;
        }
    }

    // ---- over a real store --------------------------------------------------------------------------------------

    Bytes lightsEsm()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        Bytes lights;
        Bytes subs;
        subs.append(edid("TestHearth"));
        subs.append(sub4("OBND", Bytes().u16(0xFFF6).u16(0xFFF6).u16(0).u16(10).u16(10).u16(20)));
        subs.append(sub4("MODL", Bytes().zstring("lights\\torch.nif")));
        Bytes d;
        d.u32(static_cast<std::uint32_t>(-1)).u32(768).u8(255).u8(201).u8(119).u8(0).u32(0x9);
        d.f32(1.0f).f32(90.0f).f32(0.0f).f32(0.5f).f32(0.25f).f32(0.125f).u32(25).f32(0.5f);
        subs.append(sub4("DATA", d));
        subs.append(sub4("FNAM", Bytes().f32(3.0f)));
        lights.append(record4("LIGH", 0, kTorch, subs));
        bytes.append(group4(Content::fourCC("LIGH"), kGrupTop, lights));
        Bytes refs;
        Bytes refSubs;
        refSubs.append(sub4("NAME", Bytes().u32(kTorch)));
        refSubs.append(sub4("DATA", Bytes().f32(2).f32(2).f32(3).f32(0).f32(0).f32(0)));
        refs.append(record4("REFR", 0, kRefTorch, refSubs));
        Bytes cellSubs;
        cellSubs.append(edid("HearthHall"));
        cellSubs.append(sub4("FULL", Bytes().zstring("HearthHall")));
        cellSubs.append(sub4("DATA", Bytes().u16(CellFlags::Interior)));
        Bytes cellRec = record4("CELL", 0, kCell, cellSubs);
        Bytes children;
        children.append(group4(kCell, kGrupTemporary, refs));
        Bytes sub = cellRec;
        sub.append(group4(kCell, kGrupCellChildren, children));
        bytes.append(group4(Content::fourCC("CELL"), kGrupTop,
            group4(kCell % 10, kGrupInteriorBlock, group4((kCell / 10) % 10, kGrupInteriorSub, sub))));
        return bytes;
    }

    struct Loaded
    {
        fs::path mDir;
        fs::path mFile;
        SkyrimWorldView view;
        explicit Loaded(std::optional<bool> falloff)
        {
            mDir = fs::temp_directory_path()
                / ("oe_skyrim_locallights_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            mFile = mDir / "hearth.esm";
            std::ofstream out(mFile, std::ios::binary);
            const std::string s = lightsEsm().str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            out.close();
            if (falloff)
                view.setLightFalloff(*falloff);
            std::vector<std::string> problems;
            view.load({ mFile }, problems);
            EXPECT_TRUE(problems.empty());
        }
        ~Loaded()
        {
            std::error_code ec;
            fs::remove_all(mDir, ec);
        }
        const sdkgame::LightComponent* light(sdkgame::WorldCell& cell)
        {
            const auto info = view.findCellByEditorId("HearthHall");
            std::string error;
            if (!info || !loadNeutralCell(view, info->id, cell, error))
                return nullptr;
            for (const sdkgame::WorldObject& o : cell.objects)
                if (o.light)
                    return &*o.light;
            return nullptr;
        }
    };

    TEST(SkyrimLocalLights, aViewThatWasNotToldKeepsTheRecordsRadius)
    {
        Loaded l(std::nullopt);
        EXPECT_FALSE(l.view.lightFalloff());
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->radius, 768.0);
    }

    TEST(SkyrimLocalLights, aViewToldToUseTheFalloffGivesTheLightTheMappedRadius)
    {
        Loaded l(true);
        EXPECT_TRUE(l.view.lightFalloff());
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->radius, 768.0 * kLightFalloffRadiusScale);
        EXPECT_EQ(light->fade, 3.0); // the brightness is the record's own
        EXPECT_EQ(light->red, 255);
    }

    TEST(SkyrimLocalLights, aViewToldNotToKeepsTheRecordsRadius)
    {
        Loaded l(false);
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->radius, 768.0);
    }
}
