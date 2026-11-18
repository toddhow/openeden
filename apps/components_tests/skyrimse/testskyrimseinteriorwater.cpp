// Task 0291 (swimming): the water of an INTERIOR cell of Skyrim SE as SDK values (world_water.hpp 0.2). Generated
// plugins only. The rule was measured on Skyrim.esm (590 interior cells carry an XCLW; the "has water" cell flag 0x0002
// is set on 175 of them, 71 with a real height of 0.0, the rest with the -2^31 "unset" sentinel): water needs the flag
// AND a usable height.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_water.hpp>

#include <gtest/gtest.h>

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

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6;

    Bytes interiorCell(std::uint32_t id, const std::string& name, std::uint16_t flags, const std::vector<float>& xclw)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring(name)));
        subs.append(sub4("DATA", Bytes().u16(flags)));
        for (float w : xclw)
            subs.append(sub4("XCLW", Bytes().f32(w)));
        Bytes sub = record4("CELL", 0, id, subs);
        sub.append(group4(id, kGrupCellChildren, Bytes()));
        return group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub));
    }

    constexpr std::uint16_t kInterior = CellFlags::Interior;
    constexpr std::uint16_t kWater = CellFlags::Interior | CellFlags::HasWater;
    constexpr float kUnset = -2147483648.0f;

    Bytes pluginBytes()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        Bytes cells = interiorCell(0x900, "WetHall", kWater, { 100.0f });
        cells.append(interiorCell(0x910, "ZeroHall", kWater, { 0.0f }));
        cells.append(interiorCell(0x920, "UnsetHall", kWater, { kUnset }));
        cells.append(interiorCell(0x930, "NoFlagHall", kInterior, { 5.0f }));
        cells.append(interiorCell(0x940, "NoHeightHall", kWater, {}));
        cells.append(interiorCell(0x950, "DryHall", kInterior, {}));
        cells.append(interiorCell(0x960, "NanHall", kWater, { std::numeric_limits<float>::quiet_NaN() }));
        cells.append(interiorCell(0x970, "DeepHall", kWater, { -250.5f }));
        bytes.append(group4(Content::fourCC("CELL"), kGrupTop, cells));
        return bytes;
    }

    struct Loaded
    {
        fs::path mDir;
        fs::path mFile;
        SkyrimWorldView view;
        Loaded()
        {
            mDir = fs::temp_directory_path()
                / ("oe_skyrim_intwater_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            mFile = mDir / "water.esm";
            {
                std::ofstream out(mFile, std::ios::binary);
                const std::string s = pluginBytes().str();
                out.write(s.data(), static_cast<std::streamsize>(s.size()));
            }
            std::vector<std::string> problems;
            view.load({ mFile }, problems);
            EXPECT_TRUE(problems.empty());
        }
        ~Loaded()
        {
            std::error_code ec;
            fs::remove_all(mDir, ec);
        }
        // The answer the game pack's source gives for a cell, through the optional SDK interface.
        bool water(const char* editorId, sdkgame::CellWater& out)
        {
            const auto info = view.findCellByEditorId(editorId);
            if (!info)
                return false;
            SkyrimCellSource source(view);
            const sdkgame::CellWaterSource& iface = source;
            return iface.interiorWater(info->id, out);
        }
    };

    CellRecord record(std::uint16_t flags, std::optional<float> height)
    {
        CellRecord c;
        c.mFlags = flags;
        c.mWaterHeight = height;
        return c;
    }

    // ---- the rule on a record ----

    TEST(SkyrimInteriorWaterRule, theFlagAndAUsableHeightGiveWater)
    {
        ASSERT_TRUE(record(kWater, 100.0f).interiorWaterHeight());
        EXPECT_FLOAT_EQ(*record(kWater, 100.0f).interiorWaterHeight(), 100.0f);
        EXPECT_FLOAT_EQ(*record(kWater, 0.0f).interiorWaterHeight(), 0.0f);
        EXPECT_FLOAT_EQ(*record(kWater, -250.5f).interiorWaterHeight(), -250.5f);
    }

    TEST(SkyrimInteriorWaterRule, noFlagNoHeightTheSentinelAndNanGiveNone)
    {
        EXPECT_FALSE(record(kInterior, 5.0f).interiorWaterHeight());
        EXPECT_FALSE(record(kWater, std::nullopt).interiorWaterHeight());
        EXPECT_FALSE(record(kWater, kUnset).interiorWaterHeight());
        EXPECT_FALSE(record(kWater, -std::numeric_limits<float>::max()).interiorWaterHeight());
        EXPECT_FALSE(record(kWater, std::numeric_limits<float>::max()).interiorWaterHeight());
        EXPECT_FALSE(record(kWater, std::numeric_limits<float>::infinity()).interiorWaterHeight());
        EXPECT_FALSE(record(kWater, std::numeric_limits<float>::quiet_NaN()).interiorWaterHeight());
    }

    TEST(SkyrimInteriorWaterRule, anExteriorCellHasNoInteriorWater)
    {
        EXPECT_FALSE(record(CellFlags::HasWater, 100.0f).interiorWaterHeight());
    }

    // ---- through the view and the source ----

    TEST(SkyrimInteriorWater, aCellWithTheFlagAndAHeightHasWaterAtIt)
    {
        Loaded l;
        sdkgame::CellWater w;
        ASSERT_TRUE(l.water("WetHall", w));
        EXPECT_TRUE(w.present);
        EXPECT_FLOAT_EQ(w.height, 100.0f);
    }

    TEST(SkyrimInteriorWater, zeroAndNegativeHeightsAreRealWater)
    {
        Loaded l;
        sdkgame::CellWater zero;
        ASSERT_TRUE(l.water("ZeroHall", zero));
        EXPECT_FLOAT_EQ(zero.height, 0.0f);
        sdkgame::CellWater deep;
        ASSERT_TRUE(l.water("DeepHall", deep));
        EXPECT_FLOAT_EQ(deep.height, -250.5f);
    }

    TEST(SkyrimInteriorWater, theOtherCellsAreDry)
    {
        Loaded l;
        for (const char* name : { "UnsetHall", "NoFlagHall", "NoHeightHall", "DryHall", "NanHall" })
        {
            sdkgame::CellWater w;
            w.present = true;
            w.height = 77.0f;
            EXPECT_FALSE(l.water(name, w)) << name;
            EXPECT_FALSE(w.present) << name; // the answer is cleared
        }
    }

    TEST(SkyrimInteriorWater, anUnknownCellHasNone)
    {
        Loaded l;
        SkyrimCellSource source(l.view);
        const sdkgame::CellWaterSource& iface = source;
        sdkgame::CellWater w;
        EXPECT_FALSE(iface.interiorWater(0x0DEADBEE, w));
        EXPECT_FALSE(iface.interiorWater(sdkgame::kNoWorldId, w));
    }

    TEST(SkyrimInteriorWater, theQuestionIsTheSameWhenAskedTwiceAndAfterALoad)
    {
        Loaded l;
        sdkgame::CellWater a, b;
        ASSERT_TRUE(l.water("WetHall", a));
        sdkgame::WorldCell cell;
        std::string error;
        const auto info = l.view.findCellByEditorId("WetHall");
        ASSERT_TRUE(info);
        ASSERT_TRUE(loadNeutralCell(l.view, info->id, cell, error)) << error;
        ASSERT_TRUE(l.water("WetHall", b));
        EXPECT_EQ(a, b);
    }
}
