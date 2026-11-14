// Task 0149: the Skyrim SE terrain index hands over each quadrant's alpha layers (ATXT + VTXT) as SDK values.
// Generated plugins, no game data.
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
    constexpr std::uint32_t kWorld = 0x01000900;
    constexpr std::uint32_t kLtexGrass = 0x01000C00, kLtexRock = 0x01000C01, kLtexBroken = 0x01000C02,
                            kTxstGrass = 0x01000C10, kTxstRock = 0x01000C11;
    constexpr std::uint32_t kDangling = 0x01000C99;

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
    Bytes vtxt(const std::vector<std::pair<std::uint16_t, float>>& entries)
    {
        Bytes data;
        for (const auto& e : entries)
            data.u16(e.first).u16(0).f32(e.second);
        return sub4("VTXT", data);
    }

    Bytes landRecord(std::uint32_t id, const Bytes& layers)
    {
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u32(31)));
        subs.append(vhgtFlat());
        for (std::uint8_t q = 0; q < 4; ++q)
            subs.append(btxt(kLtexGrass, q));
        subs.append(layers);
        return record4("LAND", 0, id, subs);
    }

    Bytes cellWithLand(std::uint32_t cellId, std::int32_t x, std::int32_t y, const Bytes& land)
    {
        Bytes cell = record4("CELL", 0, cellId,
            sub4("DATA", Bytes().u16(0x0002))
                .append(sub4(
                    "XCLC", Bytes().u32(static_cast<std::uint32_t>(x)).u32(static_cast<std::uint32_t>(y)).u32(0))));
        cell.append(group4(cellId, kGrupCellChildren, group4(cellId, kGrupTemporary, land)));
        return cell;
    }

    LoadOrder orderOf()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    // One world with one cell (0, 0) whose land has the given layer subrecords.
    struct Fixture
    {
        SkyrimTerrainIndex mIndex;
        explicit Fixture(const Bytes& layers)
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes subs;
            subs.append(sub4("EDID", Bytes().zstring("W")));
            subs.append(sub4("DNAM", Bytes().f32(-27000.0f).f32(-14000.0f)));
            Bytes world = record4("WRLD", 0, kWorld, subs);
            Bytes cells = cellWithLand(0x01000A00, 0, 0, landRecord(0x01000B00, layers));
            world.append(group4(kWorld, kGrupWorld, group4(0, kGrupExtBlock, group4(0, kGrupExtSub, cells))));
            bytes.append(group4(Content::fourCC("WRLD"), 0, world));
            bytes.append(group4(Content::fourCC("LTEX"), 0,
                record4("LTEX", 0, kLtexGrass,
                    sub4("EDID", Bytes().zstring("Grass")).append(sub4("TNAM", Bytes().u32(kTxstGrass))))
                    .append(record4("LTEX", 0, kLtexRock,
                        sub4("EDID", Bytes().zstring("Rock")).append(sub4("TNAM", Bytes().u32(kTxstRock)))))
                    .append(record4("LTEX", 0, kLtexBroken, sub4("EDID", Bytes().zstring("Broken"))))));
            bytes.append(group4(Content::fourCC("TXST"), 0,
                record4("TXST", 0, kTxstGrass, sub4("TX00", Bytes().zstring("Landscape\\Grass.DDS")))
                    .append(record4("TXST", 0, kTxstRock, sub4("TX00", Bytes().zstring("Landscape\\Rock.DDS"))))));
            std::string error;
            EXPECT_TRUE(mIndex.addPluginBytes(bytes.str(), "test.esp", orderOf(), error)) << error;
        }
        sdk::TerrainCell build()
        {
            sdk::TerrainCell cell;
            std::string error;
            EXPECT_EQ(mIndex.buildTerrain(kWorld, 0, 0, cell, error), sdk::TerrainResult::Built) << error;
            return cell;
        }
    };

    TEST(SkyrimTerrainLayers, theTerrainTypesAreVersion1_1)
    {
        EXPECT_EQ(sdk::kWorldTerrainVersion.major, 1u);
        EXPECT_EQ(sdk::kWorldTerrainVersion.minor, 1u);
        EXPECT_EQ(sdk::kTerrainLayerSide, 17u);
    }

    TEST(SkyrimTerrainLayers, aCellWithoutAlphaLayersHasNoLayers)
    {
        Fixture f{ Bytes() };
        const sdk::TerrainCell cell = f.build();
        for (const auto& q : cell.layers)
            EXPECT_TRUE(q.empty());
    }

    TEST(SkyrimTerrainLayers, aLayerKeepsItsTextureAndItsOpacityGrid)
    {
        // Positions are row * 17 + column: 20 is row 1, column 3; 288 is the last vertex.
        Bytes layers;
        layers.append(atxt(kLtexRock, 0, 0));
        layers.append(vtxt({ { 0, 1.0f }, { 20, 0.5f }, { 288, 0.25f } }));
        Fixture f{ layers };
        const sdk::TerrainCell cell = f.build();
        ASSERT_EQ(cell.layers[0].size(), 1u);
        const sdk::TerrainLayer& layer = cell.layers[0][0];
        EXPECT_EQ(layer.texture, kLtexRock);
        ASSERT_EQ(layer.opacity.size(), 17u * 17u);
        EXPECT_EQ(layer.opacity[0], 255);
        EXPECT_EQ(layer.opacity[20], 128); // 0.5 * 255 rounded
        EXPECT_EQ(layer.opacity[288], 64); // 0.25 * 255 rounded
        EXPECT_EQ(layer.opacity[1], 0);
        EXPECT_EQ(layer.opacity[17 * 3 + 1], 0);
        for (std::size_t q = 1; q < 4; ++q)
            EXPECT_TRUE(cell.layers[q].empty());
    }

    TEST(SkyrimTerrainLayers, layersAreInTheirQuadrantsAndInLayerNumberOrderNotFileOrder)
    {
        Bytes layers;
        layers.append(atxt(kLtexRock, 3, 2)).append(vtxt({ { 5, 1.0f } })); // quadrant 3, layer 2 first in the file
        layers.append(atxt(kLtexGrass, 3, 0)).append(vtxt({ { 6, 1.0f } }));
        layers.append(atxt(kLtexRock, 3, 1)).append(vtxt({ { 7, 1.0f } }));
        layers.append(atxt(kLtexGrass, 1, 0)).append(vtxt({ { 8, 1.0f } }));
        Fixture f{ layers };
        const sdk::TerrainCell cell = f.build();
        EXPECT_TRUE(cell.layers[0].empty());
        EXPECT_TRUE(cell.layers[2].empty());
        ASSERT_EQ(cell.layers[1].size(), 1u);
        EXPECT_EQ(cell.layers[1][0].opacity[8], 255);
        ASSERT_EQ(cell.layers[3].size(), 3u);
        EXPECT_EQ(cell.layers[3][0].opacity[6], 255); // layer 0
        EXPECT_EQ(cell.layers[3][1].opacity[7], 255); // layer 1
        EXPECT_EQ(cell.layers[3][2].opacity[5], 255); // layer 2
    }

    TEST(SkyrimTerrainLayers, aLayersSameNumberKeepsFileOrder)
    {
        Bytes layers;
        layers.append(atxt(kLtexRock, 0, 4)).append(vtxt({ { 1, 1.0f } }));
        layers.append(atxt(kLtexGrass, 0, 4)).append(vtxt({ { 2, 1.0f } }));
        Fixture f{ layers };
        const sdk::TerrainCell cell = f.build();
        ASSERT_EQ(cell.layers[0].size(), 2u);
        EXPECT_EQ(cell.layers[0][0].opacity[1], 255);
        EXPECT_EQ(cell.layers[0][1].opacity[2], 255);
    }

    TEST(SkyrimTerrainLayers, nullDanglingAndUnusableLayerTexturesAreTheDefault)
    {
        Bytes layers;
        layers.append(atxt(0, 0, 0)).append(vtxt({ { 1, 1.0f } }));
        layers.append(atxt(kDangling, 0, 1)).append(vtxt({ { 2, 1.0f } }));
        layers.append(atxt(kLtexBroken, 0, 2)).append(vtxt({ { 3, 1.0f } }));
        Fixture f{ layers };
        const sdk::TerrainCell cell = f.build();
        ASSERT_EQ(cell.layers[0].size(), 3u);
        for (const sdk::TerrainLayer& layer : cell.layers[0])
            EXPECT_EQ(layer.texture, 0u);
    }

    TEST(SkyrimTerrainLayers, aLayerWithNoOpacityDataAddsNothing)
    {
        Bytes layers;
        layers.append(atxt(kLtexRock, 0, 0)); // no VTXT follows
        layers.append(atxt(kLtexRock, 0, 1)).append(vtxt({ { 4, 1.0f } }));
        Fixture f{ layers };
        const sdk::TerrainCell cell = f.build();
        ASSERT_EQ(cell.layers[0].size(), 1u);
        EXPECT_EQ(cell.layers[0][0].opacity[4], 255);
    }

    TEST(SkyrimTerrainLayers, opacityOutsideZeroToOneIsDroppedByTheDecoderNotDrawn)
    {
        Bytes layers;
        layers.append(atxt(kLtexRock, 0, 0)).append(vtxt({ { 1, 3.0f }, { 2, -1.0f }, { 3, 0.5f } }));
        Fixture f{ layers };
        const sdk::TerrainCell cell = f.build();
        ASSERT_EQ(cell.layers[0].size(), 1u);
        EXPECT_EQ(cell.layers[0][0].opacity[1], 0);
        EXPECT_EQ(cell.layers[0][0].opacity[2], 0);
        EXPECT_EQ(cell.layers[0][0].opacity[3], 128);
    }
}
