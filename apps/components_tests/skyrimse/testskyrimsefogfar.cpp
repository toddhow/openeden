// Task 0362 item 1: the FAR fog colour of a Skyrim SE interior (XCLL offset 72 of a 92 byte record; an LGTM DATA the same;
// the template's where the fog colour inherit flag 0x4 says so). Generated plugins only; every number is written out by
// hand.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/cell_fog_source.hpp>

#include <gtest/gtest.h>

#include <array>
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
    constexpr std::uint32_t kTemplate = 0x00000D00, kOwn = 0x00000D10, kInherits = 0x00000D20, kShort = 0x00000D30,
                            kNoXcll = 0x00000D40, kExterior = 0x00000D50;

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    // 92 bytes: near fog colour (7, 8, 9) at 8, far fog colour `far` at 72, the inherit word at 88. 64 bytes have no far.
    Bytes xcll(std::uint8_t fr, std::uint8_t fg, std::uint8_t fb, std::uint32_t inherit, std::size_t size = 92)
    {
        Bytes d;
        d.u8(1).u8(2).u8(3).u8(0).u8(4).u8(5).u8(6).u8(0).u8(7).u8(8).u8(9).u8(0).f32(100.0f).f32(5000.0f);
        while (d.size() < 72 && d.size() < size)
            d.u8(0);
        if (size == 92)
        {
            d.u8(fr).u8(fg).u8(fb).u8(0);
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
        subs.append(edid("FarTemplate"));
        subs.append(sub4("DATA", xcll(200, 210, 220, 0)));
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
        cells.append(interiorCell(kOwn, "OwnFar", withTemplate(sub4("XCLL", xcll(30, 40, 50, 0)))));
        cells.append(
            interiorCell(kInherits, "InheritsFar", withTemplate(sub4("XCLL", xcll(30, 40, 50, LightingInherit::FogColor)))));
        cells.append(interiorCell(kShort, "ShortXcll", sub4("XCLL", xcll(0, 0, 0, 0, 64))));
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
                / ("oe_skyrim_fogfar_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            const fs::path file = mDir / "fogfar.esm";
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
        bool farColour(const char* editorId, std::array<std::uint8_t, 3>& colour)
        {
            const auto info = view.findCellByEditorId(editorId);
            return info && view.fogFarColorForCell(info->id, colour);
        }
    };

    TEST(SkyrimFogFar, theCellsOwnFarColourIsRead)
    {
        Loaded l;
        std::array<std::uint8_t, 3> c{};
        ASSERT_TRUE(l.farColour("OwnFar", c));
        EXPECT_EQ(c, (std::array<std::uint8_t, 3>{ 30, 40, 50 }));
    }

    TEST(SkyrimFogFar, theFogColourInheritFlagTakesTheTemplatesFarColour)
    {
        Loaded l;
        std::array<std::uint8_t, 3> c{};
        ASSERT_TRUE(l.farColour("InheritsFar", c));
        EXPECT_EQ(c, (std::array<std::uint8_t, 3>{ 200, 210, 220 }));
    }

    TEST(SkyrimFogFar, aShortXcllANoXcllAndAnUnknownCellHaveNoFarColour)
    {
        Loaded l;
        std::array<std::uint8_t, 3> c{ 9, 9, 9 };
        EXPECT_FALSE(l.farColour("ShortXcll", c));
        EXPECT_EQ(c, (std::array<std::uint8_t, 3>{ 0, 0, 0 }));
        EXPECT_FALSE(l.farColour("NoXcll", c));
        c = { 9, 9, 9 };
        EXPECT_FALSE(l.view.fogFarColorForCell(0x00DEAD00, c));
        EXPECT_EQ(c, (std::array<std::uint8_t, 3>{ 0, 0, 0 }));
    }

    TEST(SkyrimFogFar, theCellSourceForwardsAndTheDefaultInterfaceHasNone)
    {
        Loaded l;
        SkyrimCellSource source(l.view);
        sdkgame::CellFogSource& asFog = source;
        std::array<std::uint8_t, 3> c{};
        const auto info = l.view.findCellByEditorId("OwnFar");
        ASSERT_TRUE(info);
        ASSERT_TRUE(asFog.fogFarColorForCell(info->id, c));
        EXPECT_EQ(c[0], 30);
        struct OnlyCurve : sdkgame::CellFogSource
        {
            bool fogCurveForCell(sdkgame::WorldId, double&, double&) override { return false; }
        } only;
        c = { 5, 5, 5 };
        EXPECT_FALSE(only.fogFarColorForCell(1, c));
        EXPECT_EQ(c, (std::array<std::uint8_t, 3>{ 0, 0, 0 }));
        EXPECT_EQ(sdkgame::kCellFogSourceVersion.minor, 2u);
        const CellLightingData d;
        EXPECT_FALSE(d.mHasFogFar);
    }
}
