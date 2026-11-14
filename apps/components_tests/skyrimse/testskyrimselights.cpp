// Task 0113, stage N3a-5: lights and interior lighting in the Skyrim SE adapter. Generated plugins only (no game data);
// the aggregate check on the user's own plugins is in testskyrimseneutralworldreal.cpp. Layouts: LIGH DATA 48 bytes,
// FNAM 4 bytes, CELL XCLL 64 or 92 bytes (verified on the five masters, see records.hpp).
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
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
    constexpr std::uint32_t kStat = 0x00000801, kTorch = 0x00000811, kGlow = 0x00000812, kDark = 0x00000813,
                            kBroken = 0x00000814, kCellLit = 0x00000900, kCellPlain = 0x00000910,
                            kCellShort = 0x00000920, kCellOdd = 0x00000930, kRefStat = 0x00000A01,
                            kRefTorch = 0x00000A02, kRefGlow = 0x00000A03, kRefDark = 0x00000A04,
                            kRefBroken = 0x00000A05;

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    // LIGH DATA as the game stores it: time, radius, colour R G B A, flags, falloff, FOV, near clip, flicker period,
    // intensity amplitude, movement amplitude, value, weight.
    Bytes lightData(std::uint32_t radius, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint32_t flags)
    {
        Bytes d;
        d.u32(static_cast<std::uint32_t>(-1)).u32(radius).u8(r).u8(g).u8(b).u8(0).u32(flags);
        d.f32(1.0f).f32(90.0f).f32(0.0f).f32(0.5f).f32(0.25f).f32(0.125f).u32(25).f32(0.5f);
        return d;
    }

    Bytes lighRecord(
        std::uint32_t id, const std::string& name, const Bytes& data, bool withFade, float fade, bool withModel = true)
    {
        Bytes subs;
        subs.append(edid(name));
        subs.append(sub4("OBND", Bytes().u16(0xFFF6).u16(0xFFF6).u16(0).u16(10).u16(10).u16(20)));
        if (withModel)
            subs.append(sub4("MODL", Bytes().zstring("lights\\torch.nif")));
        subs.append(sub4("DATA", data));
        if (withFade)
            subs.append(sub4("FNAM", Bytes().f32(fade)));
        return record4("LIGH", 0, id, subs);
    }

    Bytes statRecord()
    {
        Bytes subs;
        subs.append(edid("StoneWall"));
        subs.append(sub4("OBND", Bytes().u16(0xFFF6).u16(0xFFF6).u16(0).u16(10).u16(10).u16(20)));
        subs.append(sub4("MODL", Bytes().zstring("architecture\\wall.nif")));
        return record4("STAT", 0, kStat, subs);
    }

    Bytes refr(std::uint32_t id, std::uint32_t base, float x)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(base)));
        subs.append(sub4("DATA", Bytes().f32(x).f32(2).f32(3).f32(0).f32(0).f32(0)));
        return record4("REFR", 0, id, subs);
    }

    // XCLL: ambient, directional, fog colour (R G B A each), fog near and far, then zero bytes up to `size` (the part
    // this stage does not decode).
    Bytes xcll(std::size_t size)
    {
        Bytes d;
        d.u8(20).u8(19).u8(18).u8(0).u8(64).u8(73).u8(74).u8(0).u8(88).u8(83).u8(71).u8(0);
        d.f32(512.0f).f32(2000.0f);
        while (d.size() < size)
            d.u8(0);
        return d;
    }

    Bytes interiorCell(std::uint32_t id, const std::string& name, const Bytes& extraSubs, const Bytes& refs)
    {
        Bytes cellSubs;
        cellSubs.append(edid(name));
        cellSubs.append(sub4("FULL", Bytes().zstring(name)));
        cellSubs.append(sub4("DATA", Bytes().u16(CellFlags::Interior)));
        cellSubs.append(extraSubs);
        Bytes cellRec = record4("CELL", 0, id, cellSubs);
        Bytes children;
        if (refs.size() > 0)
            children.append(group4(id, kGrupTemporary, refs));
        Bytes sub = cellRec;
        sub.append(group4(id, kGrupCellChildren, children));
        return group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub));
    }

    Bytes lightsEsm()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, statRecord()));
        Bytes lights;
        // A flickering orange torch: Dynamic (1) | Flicker (8).
        lights.append(lighRecord(kTorch, "TestTorch", lightData(256, 255, 160, 80, 0x9), true, 1.5f));
        // A pulsing, off-by-default, negative light with no model and no fade: Negative (4) | OffByDefault (0x20) |
        // Pulse (0x80) | Dynamic (1)... 0xA5.
        lights.append(lighRecord(kDark, "TestDark", lightData(100, 10, 20, 30, 0xA5), false, 0.0f, false));
        // A slow flicker AND a slow pulse together (0x40 | 0x100): the flicker wins.
        lights.append(lighRecord(kGlow, "TestGlow", lightData(64, 1, 2, 3, 0x140), true, 0.25f));
        // A light with a hostile radius.
        lights.append(lighRecord(kBroken, "TestBroken", lightData(0xFFFFFFF0u, 1, 1, 1, 1), true, 1.0f));
        bytes.append(group4(Content::fourCC("LIGH"), kGrupTop, lights));
        Bytes refs;
        refs.append(refr(kRefStat, kStat, 1));
        refs.append(refr(kRefTorch, kTorch, 2));
        refs.append(refr(kRefGlow, kGlow, 3));
        refs.append(refr(kRefDark, kDark, 4));
        refs.append(refr(kRefBroken, kBroken, 5));
        Bytes cells = interiorCell(kCellLit, "LitHall", sub4("XCLL", xcll(92)), refs);
        cells.append(interiorCell(kCellPlain, "PlainHall", Bytes(), Bytes()));
        cells.append(interiorCell(kCellShort, "ShortLightHall", sub4("XCLL", xcll(64)), Bytes()));
        cells.append(interiorCell(kCellOdd, "OddLightHall", sub4("XCLL", xcll(60)), Bytes()));
        bytes.append(group4(Content::fourCC("CELL"), kGrupTop, cells));
        return bytes;
    }

    struct Plugin
    {
        fs::path mDir;
        fs::path mFile;
        Plugin()
        {
            mDir = fs::temp_directory_path()
                / ("oe_skyrim_lights_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            mFile = mDir / "lights.esm";
            std::ofstream out(mFile, std::ios::binary);
            const std::string s = lightsEsm().str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
        }
        ~Plugin()
        {
            std::error_code ec;
            fs::remove_all(mDir, ec);
        }
    };

    const sdkgame::WorldObject* byId(const sdkgame::WorldCell& c, sdkgame::WorldId id)
    {
        for (const sdkgame::WorldObject& o : c.objects)
            if (o.id == id)
                return &o;
        return nullptr;
    }

    // ---- makeLightComponent, on hand-built LIGH data ---------------------------------------------------------

    LightData data(std::uint32_t radius, std::uint32_t flags = 0, bool hasFade = false, float fade = 1.0f)
    {
        LightData d;
        d.mRadius = radius;
        d.mColor = { 200, 100, 50, 0 };
        d.mFlags = flags;
        d.mHasFade = hasFade;
        d.mFade = fade;
        return d;
    }

    TEST(SkyrimLightComponent, radiusColourAndTheRawFlagsAreCopied)
    {
        ComponentStats stats;
        const auto c = makeLightComponent(data(300, 0x3001), stats, {});
        ASSERT_TRUE(c);
        EXPECT_EQ(c->radius, 300.0);
        EXPECT_EQ(c->red, 200);
        EXPECT_EQ(c->green, 100);
        EXPECT_EQ(c->blue, 50);
        EXPECT_EQ(c->flags, 0x3001u);
        EXPECT_EQ(c->fade, 1.0);
        EXPECT_EQ(c->animation, sdkgame::LightAnimation::None);
        EXPECT_FALSE(c->negative);
        EXPECT_FALSE(c->offByDefault);
    }

    TEST(SkyrimLightComponent, eachAnimationBitMapsAndTheEnginesOrderDecidesWhenSeveralAreSet)
    {
        ComponentStats stats;
        EXPECT_EQ(
            makeLightComponent(data(1, LightFlags::Flicker), stats, {})->animation, sdkgame::LightAnimation::Flicker);
        EXPECT_EQ(makeLightComponent(data(1, LightFlags::FlickerSlow), stats, {})->animation,
            sdkgame::LightAnimation::FlickerSlow);
        EXPECT_EQ(makeLightComponent(data(1, LightFlags::Pulse), stats, {})->animation, sdkgame::LightAnimation::Pulse);
        EXPECT_EQ(makeLightComponent(data(1, LightFlags::PulseSlow), stats, {})->animation,
            sdkgame::LightAnimation::PulseSlow);
        // Flicker before FlickerSlow before Pulse before PulseSlow (components/mwanimation/light.cpp tests them so).
        EXPECT_EQ(makeLightComponent(data(1, LightFlags::Pulse | LightFlags::Flicker), stats, {})->animation,
            sdkgame::LightAnimation::Flicker);
        EXPECT_EQ(makeLightComponent(data(1, LightFlags::PulseSlow | LightFlags::FlickerSlow), stats, {})->animation,
            sdkgame::LightAnimation::FlickerSlow);
        EXPECT_EQ(makeLightComponent(data(1, LightFlags::PulseSlow | LightFlags::Pulse), stats, {})->animation,
            sdkgame::LightAnimation::Pulse);
    }

    TEST(SkyrimLightComponent, negativeAndOffByDefaultAreSeparateFlags)
    {
        ComponentStats stats;
        const auto c = makeLightComponent(data(1, LightFlags::Negative | LightFlags::OffByDefault), stats, {});
        EXPECT_TRUE(c->negative);
        EXPECT_TRUE(c->offByDefault);
        const auto d = makeLightComponent(data(1, LightFlags::OffByDefault), stats, {});
        EXPECT_FALSE(d->negative);
        EXPECT_TRUE(d->offByDefault);
        // The bits that are not ours (spot, shadows) change nothing but the raw word.
        const auto e
            = makeLightComponent(data(1, LightFlags::SpotLight | LightFlags::OmnidirectionalShadow), stats, {});
        EXPECT_FALSE(e->negative);
        EXPECT_FALSE(e->offByDefault);
        EXPECT_EQ(e->animation, sdkgame::LightAnimation::None);
    }

    TEST(SkyrimLightComponent, theFadeIsUsedWhenUsableAndOtherwiseOneAndCounted)
    {
        ComponentStats stats;
        EXPECT_EQ(makeLightComponent(data(1, 0, true, 2.5f), stats, {})->fade, 2.5);
        EXPECT_EQ(stats.mLightFadesReset, 0u);
        EXPECT_EQ(makeLightComponent(data(1, 0, false, 9.0f), stats, {})->fade, 1.0); // no FNAM: the value is not read
        EXPECT_EQ(stats.mLightFadesReset, 0u);
        EXPECT_EQ(makeLightComponent(data(1, 0, true, std::numeric_limits<float>::quiet_NaN()), stats, {})->fade, 1.0);
        EXPECT_EQ(makeLightComponent(data(1, 0, true, -2.0f), stats, {})->fade, 1.0);
        EXPECT_EQ(makeLightComponent(data(1, 0, true, 0.0f), stats, {})->fade, 1.0);
        EXPECT_EQ(makeLightComponent(data(1, 0, true, 1.0e9f), stats, {})->fade, 1.0);
        EXPECT_EQ(stats.mLightFadesReset, 4u);
    }

    TEST(SkyrimLightComponent, aRadiusPastTheLimitGivesNoLightAndIsCounted)
    {
        ComponentStats stats;
        NeutralLimits limits;
        EXPECT_TRUE(makeLightComponent(data(static_cast<std::uint32_t>(limits.mMaxLightRadius)), stats, limits));
        EXPECT_FALSE(makeLightComponent(data(static_cast<std::uint32_t>(limits.mMaxLightRadius) + 1), stats, limits));
        EXPECT_EQ(stats.mLightsDropped, 1u);
        EXPECT_TRUE(makeLightComponent(data(0), stats, limits)); // a zero radius is a light that reaches nothing: kept
    }

    TEST(SkyrimCellLightingComponent, coloursAndDistancesAreCopied)
    {
        CellLightingData in;
        in.mAmbient = { 1, 2, 3 };
        in.mDirectional = { 4, 5, 6 };
        in.mFogColor = { 7, 8, 9 };
        in.mFogNear = 100.0f;
        in.mFogFar = 4000.0f;
        const auto out = makeCellLighting(in);
        ASSERT_TRUE(out);
        EXPECT_EQ(out->ambient, (std::array<std::uint8_t, 3>{ 1, 2, 3 }));
        EXPECT_EQ(out->directional, (std::array<std::uint8_t, 3>{ 4, 5, 6 }));
        EXPECT_EQ(out->fogColor, (std::array<std::uint8_t, 3>{ 7, 8, 9 }));
        EXPECT_EQ(out->fogNear, 100.0);
        EXPECT_EQ(out->fogFar, 4000.0);
        in.mFogFar = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(makeCellLighting(in));
    }

    // ---- over the real store ------------------------------------------------------------------------------------

    struct Loaded
    {
        Plugin plugin;
        SkyrimWorldView view;
        Loaded()
        {
            std::vector<std::string> problems;
            view.load({ plugin.mFile }, problems);
            EXPECT_TRUE(problems.empty());
        }
        bool cell(const char* editorId, sdkgame::WorldCell& out)
        {
            const auto info = view.findCellByEditorId(editorId);
            std::string error;
            return info && loadNeutralCell(view, info->id, out, error);
        }
    };

    TEST(SkyrimNeutralLights, aPlacedLightCarriesItsBasesLight)
    {
        Loaded l;
        sdkgame::WorldCell cell;
        ASSERT_TRUE(l.cell("LitHall", cell));
        const auto* torch = byId(cell, kRefTorch);
        ASSERT_NE(torch, nullptr);
        EXPECT_EQ(torch->kind, sdkgame::ObjectKind::Light);
        ASSERT_TRUE(torch->light);
        EXPECT_EQ(torch->light->radius, 256.0);
        EXPECT_EQ(torch->light->red, 255);
        EXPECT_EQ(torch->light->green, 160);
        EXPECT_EQ(torch->light->blue, 80);
        EXPECT_EQ(torch->light->fade, 1.5);
        EXPECT_EQ(torch->light->animation, sdkgame::LightAnimation::Flicker);
        EXPECT_FALSE(torch->light->offByDefault);
        EXPECT_EQ(torch->light->flags, 0x9u);
        // The light does not change the rest of the object.
        EXPECT_EQ(torch->modelPath, "lights\\torch.nif");
        EXPECT_EQ(torch->collision.source, sdkgame::CollisionSource::FromModel);
    }

    TEST(SkyrimNeutralLights, aLightWithNoModelAndNoFadeIsStillALight)
    {
        Loaded l;
        sdkgame::WorldCell cell;
        ASSERT_TRUE(l.cell("LitHall", cell));
        const auto* dark = byId(cell, kRefDark);
        ASSERT_NE(dark, nullptr);
        EXPECT_TRUE(dark->modelPath.empty());
        ASSERT_TRUE(dark->light);
        EXPECT_TRUE(dark->light->negative);
        EXPECT_TRUE(dark->light->offByDefault);
        EXPECT_EQ(dark->light->animation, sdkgame::LightAnimation::Pulse);
        EXPECT_EQ(dark->light->fade, 1.0);
        const auto* glow = byId(cell, kRefGlow);
        ASSERT_TRUE(glow && glow->light);
        EXPECT_EQ(glow->light->animation, sdkgame::LightAnimation::FlickerSlow);
        EXPECT_EQ(glow->light->fade, 0.25);
    }

    TEST(SkyrimNeutralLights, aStaticHasNoLightAndAHostileRadiusGivesNoneAndIsCounted)
    {
        Loaded l;
        sdkgame::WorldCell cell;
        ASSERT_TRUE(l.cell("LitHall", cell));
        const auto* wall = byId(cell, kRefStat);
        ASSERT_NE(wall, nullptr);
        EXPECT_FALSE(wall->light);
        const auto* broken = byId(cell, kRefBroken);
        ASSERT_NE(broken, nullptr);
        EXPECT_FALSE(broken->light);
        EXPECT_EQ(l.view.stats().mComponents.mLightsDropped, 1u);
    }

    TEST(SkyrimNeutralLights, theCellsXcllBecomesItsLighting)
    {
        Loaded l;
        sdkgame::WorldCell cell;
        ASSERT_TRUE(l.cell("LitHall", cell));
        ASSERT_TRUE(cell.lighting);
        EXPECT_EQ(cell.lighting->ambient, (std::array<std::uint8_t, 3>{ 20, 19, 18 }));
        EXPECT_EQ(cell.lighting->directional, (std::array<std::uint8_t, 3>{ 64, 73, 74 }));
        EXPECT_EQ(cell.lighting->fogColor, (std::array<std::uint8_t, 3>{ 88, 83, 71 }));
        EXPECT_EQ(cell.lighting->fogNear, 512.0);
        EXPECT_EQ(cell.lighting->fogFar, 2000.0);
    }

    TEST(SkyrimNeutralLights, theShorterXcllSizeIsReadAndAnUnknownSizeIsIgnored)
    {
        Loaded l;
        sdkgame::WorldCell cell;
        ASSERT_TRUE(l.cell("ShortLightHall", cell));
        ASSERT_TRUE(cell.lighting);
        EXPECT_EQ(cell.lighting->ambient, (std::array<std::uint8_t, 3>{ 20, 19, 18 }));
        ASSERT_TRUE(l.cell("OddLightHall", cell));
        EXPECT_FALSE(cell.lighting);
    }

    TEST(SkyrimNeutralLights, aCellWithoutXcllHasNoLighting)
    {
        Loaded l;
        sdkgame::WorldCell cell;
        ASSERT_TRUE(l.cell("PlainHall", cell));
        EXPECT_FALSE(cell.lighting);
    }

    TEST(SkyrimNeutralLights, theLightsAndTheLightingAreTheSameOnASecondLoad)
    {
        Loaded l;
        sdkgame::WorldCell a, b;
        ASSERT_TRUE(l.cell("LitHall", a));
        ASSERT_TRUE(l.cell("LitHall", b));
        EXPECT_EQ(a, b);
    }
}
