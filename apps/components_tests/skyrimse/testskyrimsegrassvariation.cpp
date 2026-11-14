// Task 0153 follow-up 2: what each tuft takes from the data besides its place: the land colour under it times the
// GRAS colour range's random brightness, the ground's slope when the record says "fit to slope", and the sway speed of
// its wave period. Generated data, no game assets. Written before the code (run red against stubs).
#include <game_skyrimse/grass.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    constexpr std::uint32_t kGras = 0x01000E00;
    constexpr std::uint32_t kTexGrass = 5;

    sdk::TerrainCell cellWith(float slopeX = 0.0f)
    {
        sdk::TerrainCell cell;
        cell.world = 0x01000900;
        cell.vertsPerSide = 33;
        cell.hasHeights = true;
        cell.heights.resize(33 * 33);
        for (int row = 0; row < 33; ++row)
            for (int col = 0; col < 33; ++col)
                cell.heights[row * 33 + col] = 100.0f + slopeX * static_cast<float>(col) * (4096.0f / 32.0f);
        cell.baseTextures = { kTexGrass, kTexGrass, kTexGrass, kTexGrass };
        return cell;
    }

    void setColour(sdk::TerrainCell& cell, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        cell.hasColours = true;
        cell.colours.resize(33 * 33 * 3);
        for (std::size_t i = 0; i < 33 * 33; ++i)
        {
            cell.colours[i * 3] = r;
            cell.colours[i * 3 + 1] = g;
            cell.colours[i * 3 + 2] = b;
        }
    }

    GrassTables tables(float colourRange, std::uint8_t flags, float wavePeriod = 0.0f)
    {
        GrassTables t;
        GrassRecord g;
        g.mCommon.mId = kGras;
        g.mModel = "Landscape\\Grass\\Fescue.nif";
        g.mHasData = true;
        g.mDensity = 60;
        g.mMaxSlope = 90;
        g.mColorRange = colourRange;
        g.mFlags = flags;
        g.mWavePeriod = wavePeriod;
        t.mKinds[kGras] = g;
        t.mGrassOfTexture[kTexGrass] = { kGras };
        return t;
    }

    sdk::GrassCell place(const sdk::TerrainCell& cell, const GrassTables& t)
    {
        sdk::GrassCell out;
        placeGrass(cell, 4096.0f, t, {}, shippedGrassOptions(), out);
        return out;
    }

    TEST(SkyrimGrassVariation, aDefaultInstanceChangesNothing)
    {
        const sdk::GrassInstance instance;
        EXPECT_EQ(instance.tiltX, 0.0f);
        EXPECT_EQ(instance.tiltY, 0.0f);
        EXPECT_EQ(instance.tintR, 1.0f);
        EXPECT_EQ(instance.tintG, 1.0f);
        EXPECT_EQ(instance.tintB, 1.0f);
        EXPECT_EQ(instance.waveSpeed, 1.0f);
    }

    TEST(SkyrimGrassVariation, theLandColourTintsTheTuftAndNoColourRangeKeepsItExact)
    {
        sdk::TerrainCell cell = cellWith();
        setColour(cell, 255, 128, 0);
        const sdk::GrassCell out = place(cell, tables(0.0f, 0));
        ASSERT_GT(out.instances.size(), 100u);
        for (const sdk::GrassInstance& i : out.instances)
        {
            EXPECT_NEAR(i.tintR, 1.0f, 1e-5f);
            EXPECT_NEAR(i.tintG, 128.0f / 255.0f, 1e-5f);
            EXPECT_NEAR(i.tintB, 0.0f, 1e-5f);
        }
    }

    TEST(SkyrimGrassVariation, theLandColourIsInterpolatedBetweenVertices)
    {
        sdk::TerrainCell cell = cellWith();
        setColour(cell, 0, 0, 0);
        // The right part of the cell (columns 16 and up) is white; between columns 15 and 16 the colour ramps.
        for (int row = 0; row < 33; ++row)
            for (int col = 16; col < 33; ++col)
                for (int c = 0; c < 3; ++c)
                    cell.colours[(row * 33 + col) * 3 + c] = 255;
        const sdk::GrassCell out = place(cell, tables(0.0f, 0));
        ASSERT_GT(out.instances.size(), 100u);
        bool sawRamp = false;
        for (const sdk::GrassInstance& i : out.instances)
        {
            const float u = i.x / 4096.0f * 32.0f; // column coordinate
            const float expected = std::clamp(u - 15.0f, 0.0f, 1.0f);
            EXPECT_NEAR(i.tintR, expected, 0.01f) << u;
            sawRamp = sawRamp || (i.tintR > 0.05f && i.tintR < 0.95f);
        }
        EXPECT_TRUE(sawRamp);
    }

    TEST(SkyrimGrassVariation, withNoLandColoursTheTintIsOnlyTheBrightness)
    {
        const sdk::GrassCell out = place(cellWith(), tables(0.0f, 0));
        ASSERT_GT(out.instances.size(), 100u);
        for (const sdk::GrassInstance& i : out.instances)
        {
            EXPECT_FLOAT_EQ(i.tintR, 1.0f);
            EXPECT_FLOAT_EQ(i.tintG, 1.0f);
            EXPECT_FLOAT_EQ(i.tintB, 1.0f);
        }
    }

    TEST(SkyrimGrassVariation, theColourRangeVariesTheBrightnessWithinItsBoundsAndKeepsTheHue)
    {
        sdk::TerrainCell cell = cellWith();
        setColour(cell, 200, 100, 50);
        const sdk::GrassCell out = place(cell, tables(0.3f, 0));
        ASSERT_GT(out.instances.size(), 200u);
        float lo = 10.0f;
        float hi = -10.0f;
        for (const sdk::GrassInstance& i : out.instances)
        {
            const float brightness = i.tintR / (200.0f / 255.0f);
            lo = std::min(lo, brightness);
            hi = std::max(hi, brightness);
            EXPECT_NEAR(i.tintG / i.tintR, 0.5f, 1e-4f);
            EXPECT_NEAR(i.tintB / i.tintR, 0.25f, 1e-4f);
        }
        EXPECT_GE(lo, 0.7f - 1e-4f);
        EXPECT_LE(hi, 1.3f + 1e-4f);
        EXPECT_LT(lo, 0.85f); // it really varies
        EXPECT_GT(hi, 1.15f);
    }

    TEST(SkyrimGrassVariation, aColourRangeAboveOneIsClampedSoNothingGoesNegative)
    {
        const sdk::GrassCell out = place(cellWith(), tables(5.0f, 0));
        ASSERT_GT(out.instances.size(), 100u);
        for (const sdk::GrassInstance& i : out.instances)
        {
            EXPECT_GE(i.tintR, 0.0f);
            EXPECT_LE(i.tintR, 2.0f + 1e-4f);
        }
    }

    TEST(SkyrimGrassVariation, fitToSlopeTiltsTuftsToTheGroundNormal)
    {
        // Height rises 0.5 per unit toward +x: the normal is (-0.5, 0, 1) / sqrt(1.25).
        const sdk::GrassCell out = place(cellWith(0.5f), tables(0.0f, GrassFlags::FitToSlope));
        ASSERT_GT(out.instances.size(), 100u);
        for (const sdk::GrassInstance& i : out.instances)
        {
            EXPECT_NEAR(i.tiltX, -0.5f / std::sqrt(1.25f), 1e-3f);
            EXPECT_NEAR(i.tiltY, 0.0f, 1e-3f);
        }
    }

    TEST(SkyrimGrassVariation, withoutTheFitToSlopeFlagTuftsStandUpright)
    {
        const sdk::GrassCell out = place(cellWith(0.5f), tables(0.0f, 0));
        ASSERT_GT(out.instances.size(), 100u);
        for (const sdk::GrassInstance& i : out.instances)
        {
            EXPECT_EQ(i.tiltX, 0.0f);
            EXPECT_EQ(i.tiltY, 0.0f);
        }
    }

    TEST(SkyrimGrassVariation, theTintAndTiltAreDeterministic)
    {
        sdk::TerrainCell cell = cellWith(0.3f);
        setColour(cell, 180, 200, 90);
        const GrassTables t = tables(0.3f, GrassFlags::FitToSlope, 4.0f);
        const sdk::GrassCell a = place(cell, t);
        const sdk::GrassCell b = place(cell, t);
        ASSERT_EQ(a.instances.size(), b.instances.size());
        for (std::size_t i = 0; i < a.instances.size(); ++i)
        {
            EXPECT_EQ(a.instances[i].tintR, b.instances[i].tintR);
            EXPECT_EQ(a.instances[i].tiltX, b.instances[i].tiltX);
            EXPECT_EQ(a.instances[i].waveSpeed, b.instances[i].waveSpeed);
        }
    }

    TEST(SkyrimGrassVariation, theSwaySpeedIsTheReferencePeriodOverTheRecordsClamped)
    {
        EXPECT_FLOAT_EQ(waveSpeedOf(kWaveReferencePeriod), 1.0f);
        EXPECT_FLOAT_EQ(waveSpeedOf(kWaveReferencePeriod / 1.5f), 1.5f);
        EXPECT_FLOAT_EQ(waveSpeedOf(kWaveReferencePeriod * 1.25f), 0.8f);
        EXPECT_FLOAT_EQ(waveSpeedOf(0.0001f), kWaveSpeedMax);
        EXPECT_FLOAT_EQ(waveSpeedOf(600.0f), kWaveSpeedMin);
    }

    TEST(SkyrimGrassVariation, aWavePeriodThatIsNotUsableMeansTheEnginesOwnSpeed)
    {
        EXPECT_FLOAT_EQ(waveSpeedOf(0.0f), 1.0f);
        EXPECT_FLOAT_EQ(waveSpeedOf(-3.0f), 1.0f);
        EXPECT_FLOAT_EQ(waveSpeedOf(std::nanf("")), 1.0f);
        EXPECT_FLOAT_EQ(waveSpeedOf(INFINITY), 1.0f);
    }

    TEST(SkyrimGrassVariation, everyTuftCarriesItsKindsSwaySpeed)
    {
        const sdk::GrassCell out = place(cellWith(), tables(0.0f, 0, 5.0f));
        ASSERT_GT(out.instances.size(), 100u);
        for (const sdk::GrassInstance& i : out.instances)
            EXPECT_FLOAT_EQ(i.waveSpeed, waveSpeedOf(5.0f));
    }

    TEST(SkyrimGrassVariation, thePositionRangeIsAlreadyInTheRandomPlacement)
    {
        // Tufts are placed at seeded random points of each 128 unit square, not on a grid, so the record's position
        // range (a jitter around grid points) adds nothing: x takes many distinct values.
        const sdk::GrassCell out = place(cellWith(), tables(0.0f, 0));
        ASSERT_GT(out.instances.size(), 100u);
        std::vector<float> xs;
        for (const sdk::GrassInstance& i : out.instances)
            xs.push_back(i.x);
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        EXPECT_GT(xs.size(), out.instances.size() * 9 / 10);
    }
}
