// Task 0355 (follow-up of stage d): the fog power and maximum of a Skyrim SE interior (XCLL offsets 36 and 76; the
// lighting template's values where the inherit flags 0x100 and 0x200 say so). Generated plugins only; every number is
// written out by hand.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/cell_fog_source.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdkgame = openeden::sdk::game;
    namespace fs = std::filesystem;

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6;
    constexpr std::uint32_t kTemplate = 0x00000C00, kOwn = 0x00000C10, kBoth = 0x00000C20, kPowerOnly = 0x00000C30,
                            kShort = 0x00000C40, kNoXcll = 0x00000C50, kMaxOnly = 0x00000C60;

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    // 92 bytes: the 20 byte prefix (colours and fog distances), the fog power at 36, the fog max at 76, the inherit
    // word at 88; zeros elsewhere. `size` 64 stops after the ambient cube (the fog max and the flags are not there).
    Bytes xcll(float power, float max, std::uint32_t inherit, std::size_t size = 92)
    {
        Bytes d;
        d.u8(1).u8(2).u8(3).u8(0).u8(4).u8(5).u8(6).u8(0).u8(7).u8(8).u8(9).u8(0).f32(100.0f).f32(5000.0f);
        while (d.size() < 36)
            d.u8(0);
        d.f32(power);
        while (d.size() < 76 && d.size() < size)
            d.u8(0);
        if (size == 92)
        {
            d.f32(max);
            while (d.size() < 88)
                d.u8(0);
            d.u32(inherit);
        }
        while (d.size() < size)
            d.u8(0);
        return d;
    }

    Bytes templateRecord()
    {
        Bytes subs;
        subs.append(edid("FogTemplate"));
        subs.append(sub4("DATA", xcll(0.6f, 0.5f, 0)));
        return record4("LGTM", 0, kTemplate, subs);
    }

    Bytes interiorCell(std::uint32_t id, const std::string& name, const Bytes& extraSubs)
    {
        Bytes cellSubs;
        cellSubs.append(edid(name));
        cellSubs.append(sub4("FULL", Bytes().zstring(name)));
        cellSubs.append(sub4("DATA", Bytes().u16(CellFlags::Interior)));
        cellSubs.append(extraSubs);
        Bytes sub = record4("CELL", 0, id, cellSubs);
        sub.append(group4(id, kGrupCellChildren, Bytes()));
        return group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub));
    }

    Bytes withTemplate(Bytes xcllSub)
    {
        xcllSub.append(sub4("LTMP", Bytes().u32(kTemplate)));
        return xcllSub;
    }

    Bytes esm()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        bytes.append(group4(Content::fourCC("LGTM"), kGrupTop, templateRecord()));
        Bytes cells;
        cells.append(interiorCell(kOwn, "OwnCurve", withTemplate(sub4("XCLL", xcll(0.7f, 0.8f, 0)))));
        cells.append(interiorCell(kBoth, "InheritsBoth",
            withTemplate(sub4("XCLL", xcll(0.7f, 0.8f, LightingInherit::FogPower | LightingInherit::FogMax)))));
        cells.append(interiorCell(
            kPowerOnly, "InheritsPower", withTemplate(sub4("XCLL", xcll(0.7f, 0.8f, LightingInherit::FogPower)))));
        cells.append(interiorCell(kMaxOnly, "InheritsMax", withTemplate(sub4("XCLL", xcll(0.7f, 0.8f, LightingInherit::FogMax)))));
        cells.append(interiorCell(kShort, "ShortXcll", sub4("XCLL", xcll(0.4f, 0.0f, 0, 64))));
        cells.append(interiorCell(kNoXcll, "NoXcll", Bytes()));
        bytes.append(group4(Content::fourCC("CELL"), kGrupTop, cells));
        return bytes;
    }

    struct Loaded
    {
        fs::path mDir;
        SkyrimWorldView view;
        Loaded()
        {
            mDir = fs::temp_directory_path()
                / ("oe_skyrim_fog_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            const fs::path file = mDir / "fog.esm";
            {
                std::ofstream out(file, std::ios::binary);
                const std::string s = esm().str();
                out.write(s.data(), static_cast<std::streamsize>(s.size()));
            }
            std::vector<std::string> problems;
            view.load({ file }, problems);
            EXPECT_TRUE(problems.empty());
        }
        ~Loaded()
        {
            std::error_code ec;
            fs::remove_all(mDir, ec);
        }
        bool curve(const char* editorId, double& power, double& max)
        {
            const auto info = view.findCellByEditorId(editorId);
            return info && view.fogCurveForCell(info->id, power, max);
        }
    };

    TEST(SkyrimFogCurve, theCellsOwnPowerAndMaxAreRead)
    {
        Loaded l;
        double power = 0, max = 0;
        ASSERT_TRUE(l.curve("OwnCurve", power, max));
        EXPECT_FLOAT_EQ(static_cast<float>(power), 0.7f);
        EXPECT_FLOAT_EQ(static_cast<float>(max), 0.8f);
    }

    TEST(SkyrimFogCurve, theInheritFlagsTakeEachValueFromTheTemplate)
    {
        Loaded l;
        double power = 0, max = 0;
        ASSERT_TRUE(l.curve("InheritsBoth", power, max));
        EXPECT_FLOAT_EQ(static_cast<float>(power), 0.6f);
        EXPECT_FLOAT_EQ(static_cast<float>(max), 0.5f);
        ASSERT_TRUE(l.curve("InheritsPower", power, max));
        EXPECT_FLOAT_EQ(static_cast<float>(power), 0.6f);
        EXPECT_FLOAT_EQ(static_cast<float>(max), 0.8f);
        ASSERT_TRUE(l.curve("InheritsMax", power, max));
        EXPECT_FLOAT_EQ(static_cast<float>(power), 0.7f);
        EXPECT_FLOAT_EQ(static_cast<float>(max), 0.5f);
    }

    TEST(SkyrimFogCurve, aShortXcllHasItsPowerAndTheLinearMax)
    {
        // 64 bytes end after the ambient cube: the power at 36 is there, the max (offset 76) is not
        Loaded l;
        double power = 0, max = 0;
        ASSERT_TRUE(l.curve("ShortXcll", power, max));
        EXPECT_FLOAT_EQ(static_cast<float>(power), 0.4f);
        EXPECT_EQ(max, 1.0);
    }

    TEST(SkyrimFogCurve, aCellWithNoLightingOrNoSuchCellHasNoCurveAndTheLinearOutputs)
    {
        Loaded l;
        double power = 5, max = 5;
        EXPECT_FALSE(l.curve("NoXcll", power, max));
        EXPECT_EQ(power, 1.0);
        EXPECT_EQ(max, 1.0);
        power = max = 5;
        EXPECT_FALSE(l.view.fogCurveForCell(0x00DEAD00, power, max));
        EXPECT_EQ(power, 1.0);
        EXPECT_EQ(max, 1.0);
    }

    TEST(SkyrimFogCurve, theCellSourceForwardsAndTheDefaultLightingIsLinear)
    {
        Loaded l;
        SkyrimCellSource source(l.view);
        sdkgame::CellFogSource& asFog = source;
        double power = 0, max = 0;
        const auto info = l.view.findCellByEditorId("OwnCurve");
        ASSERT_TRUE(info);
        ASSERT_TRUE(asFog.fogCurveForCell(info->id, power, max));
        EXPECT_FLOAT_EQ(static_cast<float>(power), 0.7f);
        const CellLightingData d;
        EXPECT_EQ(d.mFogPower, 1.0f);
        EXPECT_EQ(d.mFogMax, 1.0f);
    }
}
