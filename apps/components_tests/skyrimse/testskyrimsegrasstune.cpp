// Task 0153 follow-up: the density the pack ships. The struct's own default (0.3 tufts per density point) is pinned by
// the first grass tests; the pack uses shippedGrassOptions(), thinner, chosen by looking at frames (the first value
// made a continuous carpet where Skyrim shows ground between its tufts). Generated data, no game assets.
#include <game_skyrimse/grass.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    constexpr std::uint32_t kGras = 0x01000E00;
    constexpr std::uint32_t kTexGrass = 5;

    sdk::TerrainCell flatCell()
    {
        sdk::TerrainCell cell;
        cell.world = 0x01000900;
        cell.vertsPerSide = 33;
        cell.hasHeights = true;
        cell.heights.assign(33 * 33, 100.0f);
        cell.baseTextures = { kTexGrass, kTexGrass, kTexGrass, kTexGrass };
        return cell;
    }

    GrassTables tablesOf(std::uint8_t density)
    {
        GrassTables t;
        GrassRecord g;
        g.mCommon.mId = kGras;
        g.mModel = "Landscape\\Grass\\Fescue.nif";
        g.mHasData = true;
        g.mDensity = density;
        g.mMaxSlope = 90;
        t.mKinds[kGras] = g;
        t.mGrassOfTexture[kTexGrass] = { kGras };
        return t;
    }

    TEST(SkyrimGrassShipped, theShippedDensityIsThinnerThanTheStructsDefaultButNotEmpty)
    {
        const GrassPlacementOptions shipped = shippedGrassOptions();
        EXPECT_LT(shipped.mTuftsPerDensityPoint, GrassPlacementOptions().mTuftsPerDensityPoint);
        EXPECT_GT(shipped.mTuftsPerDensityPoint, 0.02f);
        EXPECT_FLOAT_EQ(shipped.mTuftsPerDensityPoint, kShippedTuftsPerDensityPoint);
    }

    TEST(SkyrimGrassShipped, theOtherOptionsAreTheStructsDefaults)
    {
        const GrassPlacementOptions shipped = shippedGrassOptions();
        const GrassPlacementOptions defaults;
        EXPECT_EQ(shipped.mMaxInstancesPerCell, defaults.mMaxInstancesPerCell);
        EXPECT_FLOAT_EQ(shipped.mMinScale, defaults.mMinScale);
        EXPECT_FLOAT_EQ(shipped.mMaxScale, defaults.mMaxScale);
    }

    TEST(SkyrimGrassShipped, aCellOfDensity40HoldsTheShippedCount)
    {
        sdk::GrassCell out;
        placeGrass(flatCell(), 4096.0f, tablesOf(40), {}, shippedGrassOptions(), out);
        const double expected = 40.0 * kShippedTuftsPerDensityPoint * 1024.0;
        EXPECT_NEAR(static_cast<double>(out.instances.size()), expected, expected * 0.05);
    }
}
