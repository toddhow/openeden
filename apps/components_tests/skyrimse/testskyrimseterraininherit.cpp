// Task 0361b: a child world that borrows its parent's land (WhiterunWorld, PNAM usage "Land") has its OWN LAND records for
// some cells that carry only part of the data: no vertex colours (VCLR) and no base texture (BTXT) for some quadrants.
// Measured on Skyrim.esm: WhiterunWorld cell (6, -3) has no VCLR and a BTXT for one quadrant of four, Tamriel's (6, -3)
// has both. The game takes what the child lacks from the parent's land of the same cell; without it the cell drew the
// default ground texture and white vertex colours, a hard seam against its neighbours. Generated plugins, no game data.
#include <game_skyrimse/terrain_source.hpp>

#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    constexpr std::uint32_t kGrupWorld = 1, kGrupExtBlock = 4, kGrupExtSub = 5, kGrupCellChildren = 6,
                            kGrupTemporary = 9;
    constexpr std::uint32_t kParent = 0x01000901, kChild = 0x01000900;
    constexpr std::uint32_t kLtexGrass = 0x01000C00, kLtexRock = 0x01000C01, kLtexSand = 0x01000C02;

    Bytes vhgtFlat()
    {
        Bytes data;
        data.f32(0.f);
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
    Bytes vtxt()
    {
        return sub4("VTXT", Bytes().u16(0).u16(0).f32(1.0f));
    }
    Bytes vclr(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        Bytes data;
        for (std::uint32_t i = 0; i < 33 * 33; ++i)
            data.u8(r).u8(g).u8(b);
        return sub4("VCLR", data);
    }

    Bytes land(std::uint32_t id, const Bytes& extra)
    {
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u32(31)));
        subs.append(vhgtFlat());
        subs.append(extra);
        return record4("LAND", 0, id, subs);
    }

    Bytes cellWithLand(std::uint32_t cellId, const Bytes& landRec)
    {
        Bytes cell = record4("CELL", 0, cellId,
            sub4("DATA", Bytes().u16(0x0002)).append(sub4("XCLC", Bytes().u32(0).u32(0).u32(0))));
        cell.append(group4(cellId, kGrupCellChildren, group4(cellId, kGrupTemporary, landRec)));
        return cell;
    }

    Bytes worldGroup(std::uint32_t worldId, const Bytes& worldRecord, const Bytes& cells)
    {
        Bytes world = worldRecord;
        world.append(group4(worldId, kGrupWorld, group4(0, kGrupExtBlock, group4(0, kGrupExtSub, cells))));
        return group4(Content::fourCC("WRLD"), 0, world);
    }

    LoadOrder orderOf()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    // The parent world has a complete cell (0, 0): grass base in all four quadrants and vertex colours (10, 20, 30). The
    // child has its own LAND for (0, 0) holding only a rock base in quadrant 2 and a layer in quadrant 0.
    struct Fixture
    {
        SkyrimTerrainIndex mIndex;
        explicit Fixture(std::uint16_t usage, bool childHasColours = false)
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes parentSubs;
            parentSubs.append(sub4("EDID", Bytes().zstring("Parent")));
            parentSubs.append(sub4("DNAM", Bytes().f32(-27000.0f).f32(-14000.0f)));
            Bytes parentExtra;
            for (std::uint8_t q = 0; q < 4; ++q)
                parentExtra.append(btxt(kLtexGrass, q));
            parentExtra.append(vclr(10, 20, 30));
            bytes.append(worldGroup(kParent, record4("WRLD", 0, kParent, parentSubs),
                cellWithLand(0x01000A01, land(0x01000B01, parentExtra))));

            Bytes childSubs;
            childSubs.append(sub4("EDID", Bytes().zstring("Child")));
            childSubs.append(sub4("WNAM", Bytes().u32(kParent)));
            childSubs.append(sub4("PNAM", Bytes().u16(usage)));
            childSubs.append(sub4("DNAM", Bytes().f32(-27000.0f).f32(-14000.0f)));
            Bytes childExtra;
            childExtra.append(btxt(kLtexRock, 2));
            childExtra.append(atxt(kLtexSand, 0, 0));
            childExtra.append(vtxt());
            if (childHasColours)
                childExtra.append(vclr(200, 100, 50));
            bytes.append(worldGroup(kChild, record4("WRLD", 0, kChild, childSubs),
                cellWithLand(0x01000A00, land(0x01000B00, childExtra))));

            Bytes ltex;
            const std::pair<std::uint32_t, const char*> textures[] = { { kLtexGrass, "Grass" }, { kLtexRock, "Rock" },
                { kLtexSand, "Sand" } };
            Bytes txst;
            for (const auto& [id, name] : textures)
            {
                const std::uint32_t set = id + 0x10;
                ltex.append(record4(
                    "LTEX", 0, id, sub4("EDID", Bytes().zstring(name)).append(sub4("TNAM", Bytes().u32(set)))));
                txst.append(record4("TXST", 0, set, sub4("TX00", Bytes().zstring(std::string("Landscape\\") + name + ".DDS"))));
            }
            bytes.append(group4(Content::fourCC("LTEX"), 0, ltex));
            bytes.append(group4(Content::fourCC("TXST"), 0, txst));
            std::string error;
            EXPECT_TRUE(mIndex.addPluginBytes(bytes.str(), "test.esp", orderOf(), error)) << error;
        }
        sdk::TerrainCell build(std::uint32_t world)
        {
            sdk::TerrainCell cell;
            std::string error;
            EXPECT_EQ(mIndex.buildTerrain(world, 0, 0, cell, error), sdk::TerrainResult::Built) << error;
            return cell;
        }
    };

    TEST(SkyrimTerrainInherit, aChildLandWithoutVertexColoursTakesTheParentsOfTheSameCell)
    {
        Fixture f(ParentUsage::Land);
        const sdk::TerrainCell cell = f.build(kChild);
        ASSERT_TRUE(cell.hasColours);
        ASSERT_GE(cell.colours.size(), 3u);
        EXPECT_EQ(cell.colours[0], 10);
        EXPECT_EQ(cell.colours[1], 20);
        EXPECT_EQ(cell.colours[2], 30);
    }

    TEST(SkyrimTerrainInherit, aQuadrantWithoutABaseTextureTakesTheParentsAndTheOwnIsKept)
    {
        Fixture f(ParentUsage::Land);
        const sdk::TerrainCell cell = f.build(kChild);
        EXPECT_EQ(cell.baseTextures[0], kLtexGrass);
        EXPECT_EQ(cell.baseTextures[1], kLtexGrass);
        EXPECT_EQ(cell.baseTextures[2], kLtexRock); // its own BTXT
        EXPECT_EQ(cell.baseTextures[3], kLtexGrass);
    }

    TEST(SkyrimTerrainInherit, theChildsOwnLayersAreKept)
    {
        Fixture f(ParentUsage::Land);
        const sdk::TerrainCell cell = f.build(kChild);
        ASSERT_EQ(cell.layers[0].size(), 1u);
        EXPECT_EQ(cell.layers[0][0].texture, kLtexSand);
    }

    TEST(SkyrimTerrainInherit, theChildsOwnVertexColoursAreKept)
    {
        Fixture f(ParentUsage::Land, true);
        const sdk::TerrainCell cell = f.build(kChild);
        ASSERT_TRUE(cell.hasColours);
        EXPECT_EQ(cell.colours[0], 200);
    }

    TEST(SkyrimTerrainInherit, aWorldThatDoesNotLendItsLandInheritsNothing)
    {
        Fixture f(ParentUsage::Map);
        const sdk::TerrainCell cell = f.build(kChild);
        EXPECT_FALSE(cell.hasColours);
        EXPECT_EQ(cell.baseTextures[0], 0u); // the default ground texture
        EXPECT_EQ(cell.baseTextures[2], kLtexRock);
    }

    TEST(SkyrimTerrainInherit, theParentsOwnLandIsNotChanged)
    {
        Fixture f(ParentUsage::Land);
        const sdk::TerrainCell cell = f.build(kParent);
        EXPECT_EQ(cell.colours[0], 10);
        EXPECT_EQ(cell.baseTextures[2], kLtexGrass);
    }
}
