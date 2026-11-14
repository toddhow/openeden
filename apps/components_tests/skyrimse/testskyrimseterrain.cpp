// Task 0120, stage E1: the Skyrim SE terrain index (SDK terrain values from the E0 decoders) on generated plugins.
// No game data.
#include <game_skyrimse/terrain_source.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    constexpr std::uint32_t kGrupWorld = 1, kGrupExtBlock = 4, kGrupExtSub = 5, kGrupCellChildren = 6,
                            kGrupTemporary = 9;
    constexpr std::uint32_t kDeleted = 0x20;
    constexpr std::uint32_t kWorld = 0x01000900;
    constexpr std::uint32_t kLtexGrass = 0x01000C00, kLtexBroken = 0x01000C01, kTxstGrass = 0x01000C10;
    constexpr std::uint32_t kDangling = 0x01000C99;

    Bytes vhgtFlat(float offset)
    {
        Bytes data;
        data.f32(offset);
        for (std::uint32_t i = 0; i < 33 * 33; ++i)
            data.u8(0);
        data.u8(0).u8(0).u8(0);
        return sub4("VHGT", data);
    }
    Bytes btxt(std::uint32_t tex, std::uint8_t quadrant)
    {
        return sub4("BTXT", Bytes().u32(tex).u8(quadrant).u8(0).u16(0));
    }
    Bytes atxt(std::uint32_t tex, std::uint8_t quadrant, std::uint16_t layer)
    {
        return sub4("ATXT", Bytes().u32(tex).u8(quadrant).u8(0).u16(layer));
    }
    Bytes vtxt(const std::vector<std::pair<std::uint16_t, float>>& entries)
    {
        Bytes data;
        for (const auto& e : entries)
            data.u16(e.first).u16(0).f32(e.second);
        return sub4("VTXT", data);
    }

    Bytes landRecord(std::uint32_t id, float offset, std::uint32_t flags = 0, bool textures = false)
    {
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u32(31)));
        subs.append(vhgtFlat(offset));
        if (textures)
        {
            subs.append(btxt(kLtexGrass, 0));
            subs.append(btxt(0, 1));
            subs.append(btxt(kDangling, 2));
            subs.append(btxt(kLtexBroken, 3));
            subs.append(atxt(kLtexGrass, 0, 0));
            subs.append(vtxt({ { 0, 1.0f }, { 288, 0.5f } }));
            subs.append(atxt(0, 1, 0)); // a null layer
            subs.append(vtxt({ { 5, 0.25f } }));
        }
        return record4("LAND", flags, id, subs);
    }

    // One exterior cell with its land, wrapped in WRLD > world children > block > sub-block > cell > temporary.
    Bytes cellWithLand(std::uint32_t cellId, std::int32_t x, std::int32_t y, const Bytes& land)
    {
        Bytes cell = record4("CELL", 0, cellId,
            sub4("DATA", Bytes().u16(0x0002))
                .append(sub4(
                    "XCLC", Bytes().u32(static_cast<std::uint32_t>(x)).u32(static_cast<std::uint32_t>(y)).u32(0))));
        cell.append(group4(cellId, kGrupCellChildren, group4(cellId, kGrupTemporary, land)));
        return cell;
    }

    Bytes worldGroup(const Bytes& worldRecord, const Bytes& cells)
    {
        Bytes world = worldRecord;
        world.append(group4(kWorld, kGrupWorld, group4(0, kGrupExtBlock, group4(0, kGrupExtSub, cells))));
        return group4(Content::fourCC("WRLD"), 0, world);
    }

    Bytes pluginA()
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("W")));
        subs.append(sub4("WNAM", Bytes().u32(0x00000D01)));
        subs.append(sub4("PNAM", Bytes().u16(0x0009)));
        subs.append(sub4("NAM2", Bytes().u32(0x00000D03)));
        subs.append(sub4("DNAM", Bytes().f32(-27000.0f).f32(-14000.0f)));
        subs.append(sub4("NAM0", Bytes().f32(-8192.0f).f32(-8192.0f)));
        subs.append(sub4("NAM9", Bytes().f32(8192.0f).f32(8192.0f)));
        Bytes cells = cellWithLand(0x01000A00, 3, -2, landRecord(0x01000B00, 3.0f, 0, true));
        cells.append(cellWithLand(0x01000A01, 4, 4, landRecord(0x01000B01, 1.0f)));
        bytes.append(worldGroup(record4("WRLD", 0, kWorld, subs), cells));
        bytes.append(group4(Content::fourCC("LTEX"), 0,
            record4("LTEX", 0, kLtexGrass,
                sub4("EDID", Bytes().zstring("Grass")).append(sub4("TNAM", Bytes().u32(kTxstGrass))))
                .append(record4("LTEX", 0, kLtexBroken, sub4("EDID", Bytes().zstring("Broken"))))));
        bytes.append(group4(Content::fourCC("TXST"), 0,
            record4("TXST", 0, kTxstGrass,
                sub4("TX00", Bytes().zstring("Landscape\\Grass.DDS"))
                    .append(sub4("TX01", Bytes().zstring("landscape/grass_n.dds"))))));
        return bytes;
    }

    // A later plugin: overrides the world's default land height only, replaces the land of (3,-2), deletes (4,4).
    Bytes pluginB()
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 }, { "test.esp", 0 } }, 24, 44);
        Bytes cells = cellWithLand(0x01000A00, 3, -2, landRecord(0x02000B10, 10.0f));
        cells.append(cellWithLand(0x01000A01, 4, 4, landRecord(0x02000B11, 0.0f, kDeleted)));
        bytes.append(worldGroup(record4("WRLD", 0, kWorld, sub4("DNAM", Bytes().f32(-100.0f).f32(-14000.0f))), cells));
        return bytes;
    }

    LoadOrder orderOf(bool second)
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        if (second)
        {
            EXPECT_TRUE(order.add("over.esp", false, err));
        }
        return order;
    }

    struct Fixture
    {
        SkyrimTerrainIndex mIndex;
        Fixture(bool both = false)
        {
            std::string error;
            EXPECT_TRUE(mIndex.addPluginBytes(pluginA().str(), "test.esp", orderOf(both), error)) << error;
            if (both)
            {
                EXPECT_TRUE(mIndex.addPluginBytes(pluginB().str(), "over.esp", orderOf(true), error)) << error;
            }
        }
    };

    TEST(SkyrimTerrainIndex, aWorldIsDescribedFromItsRecord)
    {
        Fixture f;
        std::vector<sdk::WorldSpaceInfo> worlds;
        ASSERT_EQ(f.mIndex.listWorlds(worlds), 1u);
        const sdk::WorldSpaceInfo& w = worlds[0];
        EXPECT_EQ(w.id, kWorld);
        EXPECT_EQ(w.editorId, "W");
        EXPECT_EQ(w.parent, 0x00000D01u);
        EXPECT_EQ(w.parentUsage, 0x09u);
        EXPECT_FLOAT_EQ(w.cellSizeUnits, 4096.f);
        EXPECT_EQ(w.vertsPerSide, 33u);
        EXPECT_FLOAT_EQ(w.defaultLandHeight, -27000.f);
        sdk::WorldSpaceInfo again;
        EXPECT_TRUE(f.mIndex.worldInfo(kWorld, again));
        EXPECT_EQ(again.editorId, "W");
        EXPECT_FALSE(f.mIndex.worldInfo(0x0badf00d, again));
    }

    TEST(SkyrimTerrainIndex, aCellsLandIsDecodedIntoSdkValues)
    {
        Fixture f;
        sdk::TerrainCell cell;
        std::string error;
        ASSERT_EQ(f.mIndex.buildTerrain(kWorld, 3, -2, cell, error), sdk::TerrainResult::Built) << error;
        EXPECT_EQ(cell.world, kWorld);
        EXPECT_EQ(cell.gridX, 3);
        EXPECT_EQ(cell.gridY, -2);
        EXPECT_EQ(cell.vertsPerSide, 33u);
        ASSERT_TRUE(cell.hasHeights);
        ASSERT_EQ(cell.heights.size(), 33u * 33u);
        EXPECT_FLOAT_EQ(cell.heights[0], 24.f); // offset 3 steps of 8
        EXPECT_FLOAT_EQ(cell.heights[33 * 33 - 1], 24.f);
        EXPECT_FALSE(cell.hasNormals);
        EXPECT_FALSE(cell.hasColours);
        EXPECT_EQ(f.mIndex.stats().mLandReads, 1u);
    }

    TEST(SkyrimTerrainIndex, nullAndDanglingAndUnusableTexturesAreTheDefault)
    {
        Fixture f;
        sdk::TerrainCell cell;
        std::string error;
        ASSERT_EQ(f.mIndex.buildTerrain(kWorld, 3, -2, cell, error), sdk::TerrainResult::Built) << error;
        EXPECT_EQ(cell.baseTextures[0], kLtexGrass);
        EXPECT_EQ(cell.baseTextures[1], 0u); // stored null
        EXPECT_EQ(cell.baseTextures[2], 0u); // in no LTEX
        EXPECT_EQ(cell.baseTextures[3], 0u); // an LTEX with no texture set
    }

    TEST(SkyrimTerrainIndex, groundTexturesAreVfsPathsWithTheDefaultFirst)
    {
        Fixture f;
        std::vector<sdk::GroundTexture> list;
        ASSERT_EQ(f.mIndex.listGroundTextures(list), 2u); // the default and the one usable LTEX
        EXPECT_EQ(list[0].id, 0u);
        EXPECT_EQ(list[0].diffusePath, "textures\\landscape\\dirt02.dds");
        EXPECT_EQ(list[1].id, kLtexGrass);
        EXPECT_EQ(list[1].diffusePath, "textures\\landscape\\grass.dds");
    }

    TEST(SkyrimTerrainIndex, aCellWithoutLandAndAnUnknownWorldAreTold)
    {
        Fixture f;
        sdk::TerrainCell cell;
        std::string error;
        EXPECT_EQ(f.mIndex.buildTerrain(kWorld, 99, 99, cell, error), sdk::TerrainResult::NoLand);
        EXPECT_EQ(f.mIndex.buildTerrain(0x0badf00d, 3, -2, cell, error), sdk::TerrainResult::Failed);
        EXPECT_NE(error.find("world"), std::string::npos) << error;
        EXPECT_TRUE(cell.heights.empty());
    }

    TEST(SkyrimTerrainIndex, aLaterPluginReplacesTheLandMergesTheWorldAndCanDeleteLand)
    {
        Fixture f(true);
        sdk::TerrainCell cell;
        std::string error;
        ASSERT_EQ(f.mIndex.buildTerrain(kWorld, 3, -2, cell, error), sdk::TerrainResult::Built) << error;
        EXPECT_FLOAT_EQ(cell.heights[0], 80.f); // the later plugin's offset 10
        EXPECT_EQ(cell.baseTextures[0], 0u); // the whole record is replaced, textures included
        EXPECT_EQ(f.mIndex.buildTerrain(kWorld, 4, 4, cell, error), sdk::TerrainResult::NoLand); // deleted
        sdk::WorldSpaceInfo w;
        ASSERT_TRUE(f.mIndex.worldInfo(kWorld, w));
        EXPECT_FLOAT_EQ(w.defaultLandHeight, -100.f); // changed by the override
        EXPECT_EQ(w.editorId, "W"); // kept from the first record
        EXPECT_EQ(w.parent, 0x00000D01u);
        EXPECT_EQ(w.parentUsage, 0x09u);
        EXPECT_EQ(f.mIndex.stats().mWorlds, 1u);
    }

    TEST(SkyrimTerrainIndex, aFileThatIsNotAPluginIsRefusedWithAMessage)
    {
        SkyrimTerrainIndex index;
        std::string error;
        EXPECT_FALSE(index.addPluginBytes("not a plugin at all", "junk.esp", orderOf(false), error));
        EXPECT_FALSE(error.empty());
        EXPECT_NE(error.find("junk.esp"), std::string::npos) << error;
        EXPECT_EQ(index.stats().mPlugins, 0u);
    }

    TEST(SkyrimTerrainIndex, everyBuildReadsTheRecordAgainAndTheIndexHoldsNoLandData)
    {
        Fixture f;
        sdk::TerrainCell a, b;
        std::string error;
        ASSERT_EQ(f.mIndex.buildTerrain(kWorld, 4, 4, a, error), sdk::TerrainResult::Built) << error;
        ASSERT_EQ(f.mIndex.buildTerrain(kWorld, 4, 4, b, error), sdk::TerrainResult::Built) << error;
        EXPECT_EQ(a.heights, b.heights);
        EXPECT_EQ(f.mIndex.stats().mLandReads, 2u);
        EXPECT_EQ(f.mIndex.stats().mLands, 2u);
    }
}
