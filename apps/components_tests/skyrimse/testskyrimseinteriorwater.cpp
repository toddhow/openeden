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

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6,
                            kGrupTemporary = 9;
    constexpr std::uint32_t kStat = 0x801, kDoor = 0x802;

    Bytes statRecord()
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Wall")));
        subs.append(sub4("OBND", Bytes().u16(0xFFF6).u16(0xFFF6).u16(0).u16(10).u16(10).u16(20)));
        subs.append(sub4("MODL", Bytes().zstring("architecture\wall.nif")));
        return record4("STAT", 0, kStat, subs);
    }

    Bytes doorRecord()
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Door")));
        subs.append(sub4("OBND", Bytes().u16(0xFFF6).u16(0xFFF6).u16(0).u16(10).u16(10).u16(20)));
        subs.append(sub4("MODL", Bytes().zstring("architecture\door.nif")));
        return record4("DOOR", 0, kDoor, subs);
    }

    // A placed reference at height z; a door also has a teleport (XTEL: door, position, rotation, flags).
    Bytes refr(std::uint32_t id, std::uint32_t base, float z, bool teleport)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(base)));
        subs.append(sub4("DATA", Bytes().f32(10).f32(20).f32(z).f32(0).f32(0).f32(0)));
        if (teleport)
            subs.append(sub4("XTEL", Bytes().u32(0x0A00).f32(0).f32(0).f32(0).f32(0).f32(0).f32(0).u32(0)));
        return record4("REFR", 0, id, subs);
    }

    /// `statZ` and `doorZ` are the heights of the cell's references: a static and a teleport door.
    Bytes interiorCell(std::uint32_t id, const std::string& name, std::uint16_t flags, const std::vector<float>& xclw,
        std::vector<float> statZ = { -60.0f }, std::vector<float> doorZ = {})
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring(name)));
        subs.append(sub4("DATA", Bytes().u16(flags)));
        for (float w : xclw)
            subs.append(sub4("XCLW", Bytes().f32(w)));
        Bytes sub = record4("CELL", 0, id, subs);
        Bytes refs;
        std::uint32_t n = id * 16;
        for (float z : statZ)
            refs.append(refr(n++, kStat, z, false));
        for (float z : doorZ)
            refs.append(refr(n++, kDoor, z, true));
        Bytes children;
        if (refs.size() > 0)
            children.append(group4(id, kGrupTemporary, refs));
        sub.append(group4(id, kGrupCellChildren, children));
        return group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub));
    }

    constexpr std::uint16_t kInterior = CellFlags::Interior;
    constexpr std::uint16_t kWater = CellFlags::Interior | CellFlags::HasWater;
    constexpr float kUnset = -2147483648.0f;

    Bytes pluginBytes()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, statRecord()));
        bytes.append(group4(Content::fourCC("DOOR"), kGrupTop, doorRecord()));
        Bytes cells = interiorCell(0x900, "WetHall", kWater, { 100.0f });
        cells.append(interiorCell(0x910, "ZeroHall", kWater, { 0.0f }));
        cells.append(interiorCell(0x920, "UnsetHall", kWater, { kUnset }));
        cells.append(interiorCell(0x930, "NoFlagHall", kInterior, { 5.0f }));
        cells.append(interiorCell(0x940, "NoHeightHall", kWater, {}));
        cells.append(interiorCell(0x950, "DryHall", kInterior, {}));
        cells.append(interiorCell(0x960, "NanHall", kWater, { std::numeric_limits<float>::quiet_NaN() }));
        cells.append(interiorCell(0x970, "DeepHall", kWater, { -250.5f }, { -400.0f }));
        // The follow-up cases: content far above the plane, a deeply flooded entrance, a door wet up to a Ratway's depth.
        cells.append(interiorCell(0x980, "HighHall", kWater, { 0.0f }, { 7500.0f, 8400.0f }, { 8800.0f }));
        cells.append(interiorCell(0x990, "FloodedDoorHall", kWater, { 0.0f }, { -900.0f, 300.0f }, { -1100.0f, 400.0f }));
        cells.append(interiorCell(0x9A0, "RatwayHall", kWater, { 0.0f }, { -900.0f, 300.0f }, { -125.0f, 264.0f }));
        cells.append(interiorCell(0x9B0, "BareHall", kWater, { 0.0f }, {}, {}));
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
        bool water(const char* editorId, sdkgame::CellWater& out, bool loadFirst = true)
        {
            const auto info = view.findCellByEditorId(editorId);
            if (!info)
                return false;
            if (loadFirst) // the engine builds the cell first, which is when the view sees its content
            {
                sdkgame::WorldCell cell;
                std::string error;
                EXPECT_TRUE(loadNeutralCell(view, info->id, cell, error)) << error;
            }
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

    TEST(SkyrimInteriorWater, aPlaneUnderAllTheContentIsDry)
    {
        Loaded l;
        sdkgame::CellWater w;
        EXPECT_FALSE(l.water("HighHall", w));
        EXPECT_FALSE(l.water("BareHall", w));
    }

    TEST(SkyrimInteriorWater, aDeeplyFloodedEntranceMakesTheCellDry)
    {
        Loaded l;
        sdkgame::CellWater w;
        EXPECT_FALSE(l.water("FloodedDoorHall", w));
    }

    TEST(SkyrimInteriorWater, anEntranceWetToARatwaysDepthKeepsTheWater)
    {
        Loaded l;
        sdkgame::CellWater w;
        ASSERT_TRUE(l.water("RatwayHall", w));
        EXPECT_FLOAT_EQ(w.height, 0.0f);
    }

    TEST(SkyrimInteriorWater, aCellThatWasNotBuiltYetFallsBackToTheFlagAndHeight)
    {
        Loaded l;
        sdkgame::CellWater w;
        ASSERT_TRUE(l.water("HighHall", w, false)); // no content seen: only the record's own rule
        EXPECT_FLOAT_EQ(w.height, 0.0f);
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

    // ---- the plausibility rule (follow-up: dungeons whose content is far from the plane at 0.0) ----

    InteriorWaterEvidence evidence(float minObject, std::optional<float> minDoor)
    {
        InteriorWaterEvidence e;
        e.mAnyObject = true;
        e.mMinObjectZ = minObject;
        e.mAnyDoor = minDoor.has_value();
        e.mMinDoorZ = minDoor.value_or(0.f);
        return e;
    }

    TEST(SkyrimInteriorWaterPlausible, contentBelowThePlaneAndDryEntrancesIsBelieved)
    {
        EXPECT_TRUE(interiorWaterPlausible(0.f, evidence(-941.f, 264.f)));
        EXPECT_TRUE(interiorWaterPlausible(0.f, evidence(-300.f, std::nullopt)));
    }

    TEST(SkyrimInteriorWaterPlausible, aPlaneUnderAllTheContentIsNotBelieved)
    {
        EXPECT_FALSE(interiorWaterPlausible(0.f, evidence(7529.f, 8821.f))); // Embershard Mine
        EXPECT_FALSE(interiorWaterPlausible(0.f, evidence(480.f, 954.f))); // Lost Prospect Mine
        EXPECT_FALSE(interiorWaterPlausible(0.f, evidence(0.f, std::nullopt))); // nothing strictly under it
    }

    TEST(SkyrimInteriorWaterPlausible, aPlaneThatFloodsAnEntranceDeeplyIsNotBelieved)
    {
        EXPECT_FALSE(interiorWaterPlausible(0.f, evidence(-1154.f, -1153.f))); // Labyrinthian
        EXPECT_FALSE(interiorWaterPlausible(0.f, evidence(-2208.f, -2048.f))); // Mzulft
    }

    TEST(SkyrimInteriorWaterPlausible, anEntranceWetUpToOneActorHeightIsStillBelieved)
    {
        EXPECT_TRUE(interiorWaterPlausible(0.f, evidence(-941.f, -125.f))); // Riften Ratway
        EXPECT_TRUE(interiorWaterPlausible(0.f, evidence(-941.f, -kEntranceWetMargin)));
        EXPECT_FALSE(interiorWaterPlausible(0.f, evidence(-941.f, -kEntranceWetMargin - 1.f)));
    }

    TEST(SkyrimInteriorWaterPlausible, noContentAndBadNumbersAreNotBelieved)
    {
        EXPECT_FALSE(interiorWaterPlausible(0.f, InteriorWaterEvidence{}));
        EXPECT_FALSE(interiorWaterPlausible(std::numeric_limits<float>::quiet_NaN(), evidence(-5.f, std::nullopt)));
        EXPECT_FALSE(interiorWaterPlausible(0.f, evidence(std::numeric_limits<float>::quiet_NaN(), std::nullopt)));
    }
}
