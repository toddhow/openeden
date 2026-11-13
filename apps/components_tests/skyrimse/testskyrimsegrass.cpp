// Task 0153 (milestone M-S2): Skyrim SE grass. The GRAS decoder, the dominant ground texture of a point, the pure
// placement of tufts on a cell's land, and the terrain index's per-square grass, all on generated data. No game data.
#include <game_skyrimse/grass.hpp>
#include <game_skyrimse/terrain_source.hpp>

#include <gtest/gtest.h>

#include <cmath>
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
    constexpr std::uint32_t kLtex = 0x01000C00, kTxst = 0x01000C10, kGras = 0x01000E00, kGras2 = 0x01000E01;
    constexpr std::uint32_t kTexGrass = 5, kTexRock = 6; // the SDK texture ids of the placement tests

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct Fixture
    {
        LoadOrder mOrder = oneMaster();
        std::unique_ptr<Content::Plugin> mPlugin = [] {
            Content::Error error;
            return Content::Plugin::open(std::make_unique<std::istringstream>(
                                             tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(),
                                             std::ios::binary),
                "test.esp", error);
        }();
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mLimits, mNotes, false }; }
    };

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord raw(std::uint32_t id = kGras)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC("GRAS");
        r.mFormId = id;
        r.mOffset = 0x1234;
        return r;
    }

    Bytes grassData(std::uint8_t density, std::uint8_t minSlope, std::uint8_t maxSlope, std::uint16_t units,
        std::uint32_t type, float posRange, float heightRange, float colourRange, float wave, std::uint8_t flags)
    {
        Bytes d;
        d.u8(density).u8(minSlope).u8(maxSlope).u8(0).u16(units).u16(0).u32(type);
        d.f32(posRange).f32(heightRange).f32(colourRange).f32(wave).u8(flags).u8(0).u8(0).u8(0);
        return d;
    }

    // ---- the decoder -----------------------------------------------------------------------------------------------

    TEST(SkyrimGrassRecord, fieldsAreDecoded)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("GrassPlainsFescue01")));
        subs.append(sub4("MODL", Bytes().zstring("Landscape\\Grass\\GrassPlainsFescue01.nif")));
        subs.append(sub4("DATA", grassData(85, 3, 40, 12, 4, 20.0f, 0.25f, 0.5f, 10.0f, 0x03)));
        GrassRecord g;
        std::string why;
        ASSERT_TRUE(decodeGrass(view(subs), raw(), f.context(), g, why)) << why;
        EXPECT_EQ(g.mCommon.mEditorId, "GrassPlainsFescue01");
        EXPECT_EQ(g.mModel, "Landscape\\Grass\\GrassPlainsFescue01.nif");
        EXPECT_TRUE(g.mHasData);
        EXPECT_EQ(g.mDensity, 85);
        EXPECT_EQ(g.mMinSlope, 3);
        EXPECT_EQ(g.mMaxSlope, 40);
        EXPECT_EQ(g.mUnitsFromWater, 12);
        EXPECT_EQ(g.mUnitsFromWaterType, 4u);
        EXPECT_FLOAT_EQ(g.mPositionRange, 20.0f);
        EXPECT_FLOAT_EQ(g.mHeightRange, 0.25f);
        EXPECT_FLOAT_EQ(g.mColorRange, 0.5f);
        EXPECT_FLOAT_EQ(g.mWavePeriod, 10.0f);
        EXPECT_EQ(g.mFlags, 0x03);
    }

    TEST(SkyrimGrassRecord, aDataOfAnotherSizeIsCountedAndNotUsed)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("G")));
        subs.append(sub4("DATA", Bytes().u8(50).u8(0).u8(90)));
        GrassRecord g;
        std::string why;
        ASSERT_TRUE(decodeGrass(view(subs), raw(), f.context(), g, why)) << why;
        EXPECT_FALSE(g.mHasData);
        EXPECT_EQ(f.mNotes.mOddBy["GRAS.DATA"], 1u);
    }

    TEST(SkyrimGrassRecord, aNonFiniteFloatMakesTheDataUnused)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("DATA", grassData(50, 0, 90, 0, 0, std::nanf(""), 0.1f, 0.0f, 0.0f, 0)));
        GrassRecord g;
        std::string why;
        ASSERT_TRUE(decodeGrass(view(subs), raw(), f.context(), g, why)) << why;
        EXPECT_FALSE(g.mHasData);
        EXPECT_EQ(f.mNotes.mOddBy["GRAS.DATA"], 1u);
    }

    TEST(SkyrimGrassRecord, aCutShortSubrecordIsALocatedError)
    {
        Fixture f;
        Bytes damaged; // a DATA that claims 32 bytes and holds 10
        damaged.u32(Content::fourCC("DATA")).u16(32);
        for (int i = 0; i < 10; ++i)
            damaged.u8(1);
        GrassRecord g;
        std::string why;
        EXPECT_FALSE(decodeGrass(view(damaged), raw(), f.context(), g, why));
        EXPECT_FALSE(why.empty());
    }

    // ---- the dominant ground texture ------------------------------------------------------------------------------

    sdk::TerrainCell flatCell(float z = 100.0f)
    {
        sdk::TerrainCell cell;
        cell.world = kWorld;
        cell.vertsPerSide = 33;
        cell.hasHeights = true;
        cell.heights.assign(33 * 33, z);
        cell.baseTextures = { kTexGrass, kTexGrass, kTexGrass, kTexGrass };
        return cell;
    }

    sdk::TerrainLayer layerOf(sdk::WorldId texture, std::uint8_t opacity)
    {
        sdk::TerrainLayer layer;
        layer.texture = texture;
        layer.opacity.assign(17 * 17, opacity);
        return layer;
    }

    TEST(SkyrimGrassDominant, theBaseTextureWinsWithNoLayers)
    {
        const sdk::TerrainCell cell = flatCell();
        EXPECT_EQ(dominantTexture(cell, 0, 0.3f, 0.7f), kTexGrass);
    }

    TEST(SkyrimGrassDominant, aFullyOpaqueLayerWins)
    {
        sdk::TerrainCell cell = flatCell();
        cell.layers[2].push_back(layerOf(kTexRock, 255));
        EXPECT_EQ(dominantTexture(cell, 2, 0.5f, 0.5f), kTexRock);
        EXPECT_EQ(dominantTexture(cell, 1, 0.5f, 0.5f), kTexGrass); // another quadrant
    }

    TEST(SkyrimGrassDominant, aThinLayerLosesAndAHalfLayerTiesToTheLayer)
    {
        sdk::TerrainCell cell = flatCell();
        cell.layers[0].push_back(layerOf(kTexRock, 100)); // 0.39: the base keeps 0.61
        EXPECT_EQ(dominantTexture(cell, 0, 0.5f, 0.5f), kTexGrass);
        cell.layers[0][0] = layerOf(kTexRock, 128); // 0.502: the layer
        EXPECT_EQ(dominantTexture(cell, 0, 0.5f, 0.5f), kTexRock);
    }

    TEST(SkyrimGrassDominant, aLayerDrawnLaterTakesTheShareOfTheOnesBelow)
    {
        sdk::TerrainCell cell = flatCell();
        cell.layers[0].push_back(layerOf(7, 255));
        cell.layers[0].push_back(layerOf(kTexRock, 255)); // covers the layer below completely
        EXPECT_EQ(dominantTexture(cell, 0, 0.5f, 0.5f), kTexRock);
    }

    TEST(SkyrimGrassDominant, opacityIsInterpolatedBetweenVertices)
    {
        sdk::TerrainCell cell = flatCell();
        sdk::TerrainLayer layer = layerOf(kTexRock, 0);
        for (std::uint32_t row = 0; row < 17; ++row)
            for (std::uint32_t col = 9; col < 17; ++col)
                layer.opacity[row * 17 + col] = 255;
        cell.layers[0].push_back(layer);
        EXPECT_EQ(dominantTexture(cell, 0, 7.0f / 16.0f, 0.5f), kTexGrass);
        EXPECT_EQ(dominantTexture(cell, 0, 8.7f / 16.0f, 0.5f), kTexRock); // past the half way point of 8 to 9
        EXPECT_EQ(dominantTexture(cell, 0, 8.3f / 16.0f, 0.5f), kTexGrass);
        EXPECT_EQ(dominantTexture(cell, 0, 8.5f / 16.0f, 0.5f), kTexRock); // exactly half each: the layer on top wins
        EXPECT_EQ(dominantTexture(cell, 0, 1.0f, 1.0f), kTexRock); // the far corner
    }

    TEST(SkyrimGrassDominant, aBadQuadrantOrASizeThatDoesNotFitIsTheDefault)
    {
        sdk::TerrainCell cell = flatCell();
        EXPECT_EQ(dominantTexture(cell, 4, 0.5f, 0.5f), 0u);
        sdk::TerrainLayer odd = layerOf(kTexRock, 255);
        odd.opacity.resize(10);
        cell.layers[0].push_back(odd);
        EXPECT_EQ(dominantTexture(cell, 0, 0.5f, 0.5f), kTexGrass); // a layer of the wrong size adds nothing
    }

    // ---- the placement ----------------------------------------------------------------------------------------------

    GrassRecord kindOf(std::uint32_t id, std::uint8_t density, const char* model = "Landscape\\Grass\\Fescue.nif")
    {
        GrassRecord g;
        g.mCommon.mId = id;
        g.mModel = model;
        g.mHasData = true;
        g.mDensity = density;
        g.mMinSlope = 0;
        g.mMaxSlope = 90;
        g.mHeightRange = 0.2f;
        return g;
    }

    GrassTables tablesOf(std::uint8_t density)
    {
        GrassTables t;
        t.mKinds[kGras] = kindOf(kGras, density);
        t.mGrassOfTexture[kTexGrass] = { kGras };
        return t;
    }

    sdk::GrassCell place(const sdk::TerrainCell& cell, const GrassTables& tables, sdk::CellWater water = {},
        GrassPlacementOptions options = {})
    {
        sdk::GrassCell out;
        placeGrass(cell, 4096.0f, tables, water, options, out);
        return out;
    }

    TEST(SkyrimGrassPlace, theCountFollowsTheDensity)
    {
        const sdk::GrassCell out = place(flatCell(), tablesOf(50));
        // 50 points * 0.3 tufts per point * 1024 squares
        const double expected = 50 * 0.3 * 1024;
        EXPECT_NEAR(static_cast<double>(out.instances.size()), expected, expected * 0.03);
        EXPECT_EQ(out.requested, static_cast<std::uint32_t>(expected));
        ASSERT_EQ(out.models.size(), 1u);
        EXPECT_EQ(out.models[0].modelPath, "meshes\\landscape\\grass\\fescue.nif");
    }

    TEST(SkyrimGrassPlace, theSameInputGivesTheSameListAndAnotherCellAnotherList)
    {
        const sdk::TerrainCell cell = flatCell();
        const GrassTables tables = tablesOf(40);
        const sdk::GrassCell a = place(cell, tables);
        const sdk::GrassCell b = place(cell, tables);
        ASSERT_EQ(a.instances.size(), b.instances.size());
        for (std::size_t i = 0; i < a.instances.size(); ++i)
        {
            EXPECT_EQ(a.instances[i].x, b.instances[i].x);
            EXPECT_EQ(a.instances[i].y, b.instances[i].y);
            EXPECT_EQ(a.instances[i].yaw, b.instances[i].yaw);
            EXPECT_EQ(a.instances[i].scale, b.instances[i].scale);
        }
        sdk::TerrainCell other = cell;
        other.gridX = 7;
        const sdk::GrassCell c = place(other, tables);
        ASSERT_FALSE(c.instances.empty());
        EXPECT_NE(c.instances[0].x - 7 * 4096.0f, a.instances[0].x);
    }

    TEST(SkyrimGrassPlace, everyTuftLiesInsideItsCellAtTheGroundHeight)
    {
        sdk::TerrainCell cell = flatCell(250.0f);
        cell.gridX = 2;
        cell.gridY = -3;
        const sdk::GrassCell out = place(cell, tablesOf(60));
        ASSERT_FALSE(out.instances.empty());
        for (const sdk::GrassInstance& i : out.instances)
        {
            EXPECT_GE(i.x, 2 * 4096.0f);
            EXPECT_LT(i.x, 3 * 4096.0f);
            EXPECT_GE(i.y, -3 * 4096.0f);
            EXPECT_LT(i.y, -2 * 4096.0f);
            EXPECT_FLOAT_EQ(i.z, 250.0f);
            EXPECT_GE(i.yaw, 0.0f);
            EXPECT_LT(i.yaw, 6.2832f);
            EXPECT_GE(i.scale, 0.8f - 1e-4f);
            EXPECT_LE(i.scale, 1.2f + 1e-4f);
            EXPECT_LT(i.model, out.models.size());
        }
    }

    TEST(SkyrimGrassPlace, theHeightFollowsTheLand)
    {
        sdk::TerrainCell cell = flatCell();
        for (std::uint32_t row = 0; row < 33; ++row)
            for (std::uint32_t col = 0; col < 33; ++col)
                cell.heights[row * 33 + col] = 10.0f * static_cast<float>(col); // a ramp along x: 10 per 128 units
        const sdk::GrassCell out = place(cell, tablesOf(30));
        ASSERT_FALSE(out.instances.empty());
        for (const sdk::GrassInstance& i : out.instances)
            EXPECT_NEAR(i.z, i.x / 128.0f * 10.0f, 0.01f);
    }

    TEST(SkyrimGrassPlace, aCellWithTooManyTuftsIsThinnedTogether)
    {
        GrassTables tables = tablesOf(100);
        tables.mKinds[kGras2] = kindOf(kGras2, 100, "Landscape\\Grass\\Other.nif");
        tables.mGrassOfTexture[kTexGrass] = { kGras, kGras2 };
        const sdk::GrassCell out = place(flatCell(), tables);
        EXPECT_EQ(out.requested, 61440u);
        EXPECT_LE(out.instances.size(), 30000u * 102 / 100);
        EXPECT_GE(out.instances.size(), 30000u * 98 / 100);
        ASSERT_EQ(out.models.size(), 2u);
        std::size_t a = 0, b = 0;
        for (const sdk::GrassInstance& i : out.instances)
            (i.model == 0 ? a : b)++;
        EXPECT_NEAR(static_cast<double>(a), static_cast<double>(b), 600.0); // thinned by the same factor
    }

    TEST(SkyrimGrassPlace, theSlopeLimitsAreHonoured)
    {
        sdk::TerrainCell cell = flatCell();
        for (std::uint32_t row = 0; row < 33; ++row)
            for (std::uint32_t col = 0; col < 33; ++col)
                cell.heights[row * 33 + col] = 128.0f * static_cast<float>(col); // 45 degrees
        GrassTables tables = tablesOf(50);
        tables.mKinds[kGras].mMaxSlope = 30;
        EXPECT_TRUE(place(cell, tables).instances.empty());
        tables.mKinds[kGras].mMaxSlope = 50;
        EXPECT_FALSE(place(cell, tables).instances.empty());
        tables.mKinds[kGras].mMinSlope = 46;
        EXPECT_TRUE(place(cell, tables).instances.empty());
    }

    TEST(SkyrimGrassPlace, theWaterRuleKeepsGrassOutOfTheWater)
    {
        const sdk::TerrainCell cell = flatCell(100.0f);
        GrassTables tables = tablesOf(50);
        sdk::CellWater water;
        water.present = true;
        water.height = 150.0f; // the ground is 50 under the surface
        EXPECT_TRUE(place(cell, tables, water).instances.empty());
        water.height = 80.0f; // 20 above
        EXPECT_FALSE(place(cell, tables, water).instances.empty());
        tables.mKinds[kGras].mUnitsFromWater = 30; // type 0: at least 30 above the water
        EXPECT_TRUE(place(cell, tables, water).instances.empty());
        tables.mKinds[kGras].mUnitsFromWater = 10;
        EXPECT_FALSE(place(cell, tables, water).instances.empty());
    }

    TEST(SkyrimGrassPlace, grassStandsOnlyOnTheGroundThatNamesIt)
    {
        sdk::TerrainCell cell = flatCell();
        sdk::TerrainLayer layer = layerOf(kTexRock, 0);
        for (std::uint32_t row = 0; row < 17; ++row)
            for (std::uint32_t col = 9; col < 17; ++col)
                layer.opacity[row * 17 + col] = 255;
        cell.layers[0].push_back(layer); // the south-west quadrant's east part is rock
        const sdk::GrassCell out = place(cell, tablesOf(60));
        std::size_t inRock = 0, inGrassQuadrant = 0, elsewhere = 0;
        for (const sdk::GrassInstance& i : out.instances)
        {
            const bool southWest = i.x < 2048.0f && i.y < 2048.0f;
            if (southWest && i.x > 1100.0f)
                ++inRock;
            else if (southWest && i.x < 1000.0f)
                ++inGrassQuadrant;
            else if (!southWest)
                ++elsewhere;
        }
        EXPECT_EQ(inRock, 0u);
        EXPECT_GT(inGrassQuadrant, 1000u);
        EXPECT_GT(elsewhere, 5000u);
    }

    TEST(SkyrimGrassPlace, nothingIsPlacedWithoutHeightsOrGrassOrData)
    {
        sdk::TerrainCell cell = flatCell();
        cell.hasHeights = false;
        EXPECT_TRUE(place(cell, tablesOf(50)).instances.empty());
        EXPECT_TRUE(place(flatCell(), GrassTables()).instances.empty());
        GrassTables noData = tablesOf(50);
        noData.mKinds[kGras].mHasData = false;
        EXPECT_TRUE(place(flatCell(), noData).instances.empty());
        GrassTables zero = tablesOf(0);
        EXPECT_TRUE(place(flatCell(), zero).instances.empty());
        GrassTables noModel = tablesOf(50);
        noModel.mKinds[kGras].mModel.clear();
        EXPECT_TRUE(place(flatCell(), noModel).instances.empty());
        sdk::TerrainCell wrongSize = flatCell();
        wrongSize.heights.resize(10);
        EXPECT_TRUE(place(wrongSize, tablesOf(50)).instances.empty());
    }

    TEST(SkyrimGrassPlace, heightRangeIsClampedToTheOptions)
    {
        GrassTables tables = tablesOf(50);
        tables.mKinds[kGras].mHeightRange = 50.0f; // absurd: variation is limited to one
        GrassPlacementOptions options;
        options.mMinScale = 0.5f;
        options.mMaxScale = 1.5f;
        const sdk::GrassCell out = place(flatCell(), tables, {}, options);
        ASSERT_FALSE(out.instances.empty());
        for (const sdk::GrassInstance& i : out.instances)
        {
            EXPECT_GE(i.scale, 0.5f);
            EXPECT_LE(i.scale, 1.5f);
        }
    }

    // ---- the terrain index ------------------------------------------------------------------------------------------

    Bytes vhgtFlat(float offset)
    {
        Bytes data;
        data.f32(offset);
        for (std::uint32_t i = 0; i < 33 * 33; ++i)
            data.u8(0);
        data.u8(0).u8(0).u8(0);
        return sub4("VHGT", data);
    }

    Bytes pluginWithGrass(bool deleteGrass)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("W")));
        subs.append(sub4("DNAM", Bytes().f32(0.0f).f32(-14000.0f)));
        Bytes land = record4("LAND", 0, 0x01000B00,
            sub4("DATA", Bytes().u32(31)).append(vhgtFlat(5.0f)).append(
                sub4("BTXT", Bytes().u32(kLtex).u8(0).u8(0).u16(0))).append(
                sub4("BTXT", Bytes().u32(kLtex).u8(1).u8(0).u16(0))).append(
                sub4("BTXT", Bytes().u32(kLtex).u8(2).u8(0).u16(0))).append(
                sub4("BTXT", Bytes().u32(kLtex).u8(3).u8(0).u16(0))));
        Bytes cell = record4("CELL", 0, 0x01000A00,
            sub4("DATA", Bytes().u16(0x0002)).append(sub4("XCLC", Bytes().u32(1).u32(0).u32(0))));
        cell.append(group4(0x01000A00, kGrupCellChildren, group4(0x01000A00, kGrupTemporary, land)));
        Bytes world = record4("WRLD", 0, kWorld, subs);
        world.append(group4(kWorld, kGrupWorld, group4(0, kGrupExtBlock, group4(0, kGrupExtSub, cell))));
        bytes.append(group4(Content::fourCC("WRLD"), 0, world));
        bytes.append(group4(Content::fourCC("LTEX"), 0,
            record4("LTEX", 0, kLtex,
                sub4("EDID", Bytes().zstring("Ground")).append(sub4("TNAM", Bytes().u32(kTxst))).append(
                    sub4("GNAM", Bytes().u32(kGras))))));
        bytes.append(group4(Content::fourCC("TXST"), 0,
            record4("TXST", 0, kTxst, sub4("TX00", Bytes().zstring("Landscape\\Ground.DDS")))));
        bytes.append(group4(Content::fourCC("GRAS"), 0,
            record4("GRAS", deleteGrass ? 0x20u : 0u, kGras,
                sub4("EDID", Bytes().zstring("Fescue")).append(sub4("MODL", Bytes().zstring("Landscape\\Grass\\Fescue.nif"))).append(
                    sub4("DATA", grassData(40, 0, 90, 0, 0, 0.0f, 0.1f, 0.0f, 0.0f, 0))))));
        return bytes;
    }

    TEST(SkyrimGrassIndex, aSquaresGrassComesFromItsGroundTextures)
    {
        SkyrimTerrainIndex index;
        std::string error;
        ASSERT_TRUE(index.addPluginBytes(pluginWithGrass(false).str(), "test.esp", oneMaster(), error)) << error;
        ASSERT_EQ(index.grassTables().mKinds.size(), 1u);
        ASSERT_EQ(index.grassTables().mGrassOfTexture.count(kLtex), 1u);
        sdk::GrassCell out;
        ASSERT_TRUE(index.grassForCell(kWorld, 1, 0, out, error)) << error;
        const double expected = 40 * 0.3 * 1024;
        EXPECT_NEAR(static_cast<double>(out.instances.size()), expected, expected * 0.04);
        for (const sdk::GrassInstance& i : out.instances)
        {
            EXPECT_GE(i.x, 4096.0f);
            EXPECT_LT(i.x, 8192.0f);
            EXPECT_FLOAT_EQ(i.z, 40.0f); // the offset 5 steps of 8
        }
    }

    TEST(SkyrimGrassIndex, aSquareWithNoLandAnUnknownWorldAndADeletedGrassGiveNothing)
    {
        SkyrimTerrainIndex index;
        std::string error;
        ASSERT_TRUE(index.addPluginBytes(pluginWithGrass(false).str(), "test.esp", oneMaster(), error)) << error;
        sdk::GrassCell out;
        EXPECT_FALSE(index.grassForCell(kWorld, 9, 9, out, error));
        EXPECT_TRUE(out.instances.empty());
        EXPECT_FALSE(index.grassForCell(0x0badf00d, 1, 0, out, error));

        SkyrimTerrainIndex deleted;
        ASSERT_TRUE(deleted.addPluginBytes(pluginWithGrass(true).str(), "test.esp", oneMaster(), error)) << error;
        EXPECT_TRUE(deleted.grassForCell(kWorld, 1, 0, out, error));
        EXPECT_TRUE(out.instances.empty());
    }
}
