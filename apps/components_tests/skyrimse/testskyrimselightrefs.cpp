// Task 0355 stage i: a placed Skyrim light reference changes its base light. XRDS is a radius delta and XLIG's second
// float a fade delta (settled from the five masters, see the task doc). Generated plugin only (no game data).
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

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
    constexpr float kInf = std::numeric_limits<float>::infinity();

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    LightData base(std::uint32_t radius = 256, float fade = 1.0f, bool hasFade = true)
    {
        LightData d;
        d.mRadius = radius;
        d.mFade = fade;
        d.mHasFade = hasFade;
        return d;
    }

    LightOverrides radius(float v)
    {
        LightOverrides o;
        o.mRadiusDelta = v;
        return o;
    }

    LightOverrides fade(float v)
    {
        LightOverrides o;
        o.mFadeDelta = v;
        return o;
    }

    // ---- the pure rule -----------------------------------------------------------------------------------------

    TEST(SkyrimLightRefs, noOverrideLeavesTheBaseLightAsItIs)
    {
        const LightData out = applyLightOverrides(base(), {});
        EXPECT_EQ(out.mRadius, 256u);
        EXPECT_EQ(out.mFade, 1.0f);
    }

    TEST(SkyrimLightRefs, theRadiusDeltaIsAddedToTheBaseRadiusPositiveOrNegative)
    {
        EXPECT_EQ(applyLightOverrides(base(256), radius(100.0f)).mRadius, 356u);
        EXPECT_EQ(applyLightOverrides(base(256), radius(-80.0f)).mRadius, 176u);
        EXPECT_EQ(applyLightOverrides(base(256), radius(-256.0f)).mRadius, 0u);
    }

    TEST(SkyrimLightRefs, aRadiusBelowMinusTheBaseIsCutToZeroAndNotWrapped)
    {
        EXPECT_EQ(applyLightOverrides(base(256), radius(-300.0f)).mRadius, 0u);
    }

    TEST(SkyrimLightRefs, aRadiusDeltaThatIsNotFiniteOrGarbageIsIgnored)
    {
        EXPECT_EQ(applyLightOverrides(base(256), radius(kInf)).mRadius, 256u);
        EXPECT_EQ(applyLightOverrides(base(256), radius(std::nanf(""))).mRadius, 256u);
        EXPECT_EQ(applyLightOverrides(base(256), radius(3.7e6f)).mRadius, 256u);
        EXPECT_EQ(applyLightOverrides(base(256), radius(-3.7e6f)).mRadius, 256u);
        EXPECT_EQ(
            applyLightOverrides(base(256), radius(static_cast<float>(kMaxLightRadiusDelta))).mRadius, 256u + 16384u);
    }

    TEST(SkyrimLightRefs, theFadeDeltaIsAddedToTheBaseFade)
    {
        const LightData out = applyLightOverrides(base(256, 1.0f), fade(0.5f));
        EXPECT_FLOAT_EQ(out.mFade, 1.5f);
        EXPECT_TRUE(out.mHasFade);
    }

    TEST(SkyrimLightRefs, aBaseWithNoFadeCountsAsOneAndTheOverrideGivesItOne)
    {
        const LightData out = applyLightOverrides(base(256, 1.0f, false), fade(1.0f));
        EXPECT_FLOAT_EQ(out.mFade, 2.0f);
        EXPECT_TRUE(out.mHasFade);
    }

    TEST(SkyrimLightRefs, aFadeThatWouldNotStayAboveZeroOrIsGarbageKeepsTheBaseFade)
    {
        EXPECT_EQ(applyLightOverrides(base(256, 1.0f), fade(-1.0f)).mFade, 1.0f);
        EXPECT_EQ(applyLightOverrides(base(256, 1.0f), fade(-4.5f)).mFade, 1.0f);
        EXPECT_EQ(applyLightOverrides(base(256, 1.0f), fade(kInf)).mFade, 1.0f);
        EXPECT_EQ(applyLightOverrides(base(256, 1.0f), fade(1.0e9f)).mFade, 1.0f);
    }

    TEST(SkyrimLightRefs, theOtherFieldsOfTheBaseAreUntouched)
    {
        LightData b = base(256, 2.0f);
        b.mColor = { 1, 2, 3, 0 };
        b.mFlags = 0x9;
        LightOverrides o = radius(10.0f);
        o.mFadeDelta = 0.5f;
        const LightData out = applyLightOverrides(b, o);
        EXPECT_EQ(out.mColor, b.mColor);
        EXPECT_EQ(out.mFlags, b.mFlags);
        EXPECT_EQ(out.mFov, b.mFov);
    }

    // ---- over a real store -------------------------------------------------------------------------------------

    Bytes lightsEsm(const std::vector<Bytes>& extras)
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
        for (const Bytes& e : extras)
            refSubs.append(e);
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
        Loaded(std::optional<bool> overrides, const std::vector<Bytes>& extras, std::optional<bool> falloff = {})
        {
            mDir = fs::temp_directory_path()
                / ("oe_skyrim_lightrefs_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            mFile = mDir / "hearth.esm";
            std::ofstream out(mFile, std::ios::binary);
            const std::string s = lightsEsm(extras).str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            out.close();
            if (overrides)
                view.setLightOverrides(*overrides);
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

    Bytes xrds(float v)
    {
        return sub4("XRDS", Bytes().f32(v));
    }

    Bytes xlig16(float fov, float fadeOffset)
    {
        return sub4("XLIG", Bytes().f32(fov).f32(fadeOffset).f32(0.0f).f32(1.0f));
    }

    TEST(SkyrimLightRefs, aViewThatWasNotToldKeepsTheBaseLight)
    {
        Loaded l(std::nullopt, { xrds(100.0f), xlig16(0.0f, 0.5f) });
        EXPECT_FALSE(l.view.lightOverrides());
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->radius, 768.0);
        EXPECT_EQ(light->fade, 3.0);
    }

    TEST(SkyrimLightRefs, aViewToldToUseThemGivesTheReferencesLightItsOwnRadiusAndFade)
    {
        Loaded l(true, { xrds(100.0f), xlig16(0.0f, 0.5f) });
        EXPECT_TRUE(l.view.lightOverrides());
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->radius, 868.0);
        EXPECT_EQ(light->fade, 3.5);
    }

    TEST(SkyrimLightRefs, aNegativeRadiusDeltaShrinksTheLight)
    {
        Loaded l(true, { xrds(-268.0f) });
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->radius, 500.0);
        EXPECT_EQ(light->fade, 3.0);
    }

    TEST(SkyrimLightRefs, theOverrideComesBeforeTheFalloffScale)
    {
        Loaded l(true, { xrds(100.0f) }, true);
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_DOUBLE_EQ(light->radius, 868.0 * kLightFalloffRadiusScale);
    }

    TEST(SkyrimLightRefs, theTwentyByteXligReadsTheSameFadeOffset)
    {
        Bytes x;
        x.f32(0.0f).f32(-1.0f).f32(0.0f).f32(1.0f).f32(0.0f);
        Loaded l(true, { sub4("XLIG", x) });
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->fade, 2.0);
    }

    TEST(SkyrimLightRefs, subrecordsOfAnotherSizeAreIgnored)
    {
        Loaded l(true, { sub4("XRDS", Bytes().f32(100.0f).f32(5.0f)), sub4("XLIG", Bytes().f32(0.0f).f32(1.0f)) });
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->radius, 768.0);
        EXPECT_EQ(light->fade, 3.0);
    }

    TEST(SkyrimLightRefs, aGarbageRadiusDeltaLeavesTheBaseRadius)
    {
        Loaded l(true, { xrds(3.7e6f) });
        sdkgame::WorldCell cell;
        const auto* light = l.light(cell);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->radius, 768.0);
    }
}
