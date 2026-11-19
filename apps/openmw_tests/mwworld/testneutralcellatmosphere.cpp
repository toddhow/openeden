// Task 0355 stages c and d: the cell's image space grade and the interior fog a game pack's neutral interior asks for
// (apps/openmw/mwworld/neutralcellatmosphere.hpp). Written before the code; the first run was against stubs. Every
// expected number is written out by hand.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <components/esm3/loadcell.hpp>
#include <components/settings/values.hpp>

#include "apps/openmw/mwrender/fogmanager.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralcellatmosphere.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::neutralCellGrade;
    using MWWorld::neutralInteriorFog;

    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

    TEST(NeutralCellGrade, noGradeIsTheOffGrade)
    {
        const MWWorld::NeutralCellGrade g = neutralCellGrade(nullptr);
        EXPECT_FALSE(g.on);
        EXPECT_FLOAT_EQ(g.stops, 0.f);
        EXPECT_FLOAT_EQ(g.saturation, 1.f);
        EXPECT_FLOAT_EQ(g.contrast, 1.f);
        EXPECT_FLOAT_EQ(g.tintAmount, 0.f);
    }

    TEST(NeutralCellGrade, theValuesGoThroughAndBrightnessBecomesStops)
    {
        ImageSpaceGrade in;
        in.saturation = 0.5f;
        in.brightness = 2.f;
        in.contrast = 1.5f;
        in.tintAmount = 0.3f;
        in.tint = { 0.1f, 0.2f, 0.3f };
        const MWWorld::NeutralCellGrade g = neutralCellGrade(&in, 1.f);
        EXPECT_TRUE(g.on);
        EXPECT_FLOAT_EQ(g.stops, 1.f); // log2 2
        EXPECT_FLOAT_EQ(g.saturation, 0.5f);
        EXPECT_FLOAT_EQ(g.contrast, 1.5f);
        EXPECT_FLOAT_EQ(g.tintAmount, 0.3f);
        EXPECT_FLOAT_EQ(g.tint[0], 0.1f);
        EXPECT_FLOAT_EQ(g.tint[1], 0.2f);
        EXPECT_FLOAT_EQ(g.tint[2], 0.3f);
        in.brightness = 0.5f;
        EXPECT_FLOAT_EQ(neutralCellGrade(&in).stops, -1.f);
    }

    TEST(NeutralCellGrade, theContrastShareScalesTheDistanceFromNeutralAndDefaultsToNone)
    {
        // Added after the first in-game frames: a full contrast crushed Bleak Falls Barrow (kCellGradeContrastShare).
        ImageSpaceGrade in;
        in.contrast = 1.5f;
        EXPECT_FLOAT_EQ(MWWorld::kCellGradeContrastShare, 0.f);
        EXPECT_FLOAT_EQ(neutralCellGrade(&in).contrast, 1.f);
        EXPECT_FLOAT_EQ(neutralCellGrade(&in, 0.5f).contrast, 1.25f);
        EXPECT_FLOAT_EQ(neutralCellGrade(&in, 1.f).contrast, 1.5f);
        in.contrast = 0.8f;
        EXPECT_FLOAT_EQ(neutralCellGrade(&in, 0.5f).contrast, 0.9f);
        EXPECT_FLOAT_EQ(neutralCellGrade(&in, std::numeric_limits<float>::quiet_NaN()).contrast, 1.f);
    }

    TEST(NeutralCellGrade, brightnessHasTheWeatherManagersFloorAndTheRenderersCeiling)
    {
        ImageSpaceGrade in;
        in.brightness = 0.01f;
        EXPECT_FLOAT_EQ(neutralCellGrade(&in).stops, -2.f); // floor 0.25 = 2^-2
        in.brightness = 100.f;
        EXPECT_FLOAT_EQ(neutralCellGrade(&in).stops, 2.f); // the renderer's clamp
        in.brightness = 0.f;
        EXPECT_FLOAT_EQ(neutralCellGrade(&in).stops, -2.f);
    }

    TEST(NeutralCellGrade, valuesThatAreNotNumbersAreTheNeutralOnes)
    {
        ImageSpaceGrade in;
        in.saturation = kNan;
        in.brightness = kNan;
        in.contrast = kNan;
        in.tintAmount = kNan;
        in.tint = { kNan, 0.5f, kNan };
        const MWWorld::NeutralCellGrade g = neutralCellGrade(&in);
        EXPECT_TRUE(g.on);
        EXPECT_FLOAT_EQ(g.stops, 0.f);
        EXPECT_FLOAT_EQ(g.saturation, 1.f);
        EXPECT_FLOAT_EQ(g.contrast, 1.f);
        EXPECT_FLOAT_EQ(g.tintAmount, 0.f);
        EXPECT_FLOAT_EQ(g.tint[0], 1.f);
        EXPECT_FLOAT_EQ(g.tint[1], 0.5f);
        EXPECT_FLOAT_EQ(g.tint[2], 1.f);
    }

    CellLighting lighting(double fogNear, double fogFar)
    {
        CellLighting l;
        l.fogNear = fogNear;
        l.fogFar = fogFar;
        return l;
    }

    TEST(NeutralInteriorFog, theCellsDistancesAreTheLinearFog)
    {
        const CellLighting l = lighting(340.0, 14000.0);
        const auto fog = neutralInteriorFog(&l, true);
        ASSERT_TRUE(fog.has_value());
        EXPECT_FLOAT_EQ(fog->start, 340.f);
        EXPECT_FLOAT_EQ(fog->end, 14000.f);
        const CellLighting zeroNear = lighting(0.0, 4500.0);
        ASSERT_TRUE(neutralInteriorFog(&zeroNear, true).has_value());
        EXPECT_FLOAT_EQ(neutralInteriorFog(&zeroNear, true)->start, 0.f);
    }

    TEST(NeutralInteriorFog, nothingWhenOffOrWithoutLighting)
    {
        const CellLighting l = lighting(0.0, 4500.0);
        EXPECT_FALSE(neutralInteriorFog(&l, false).has_value());
        EXPECT_FALSE(neutralInteriorFog(nullptr, true).has_value());
    }

    TEST(NeutralInteriorFog, noFogIsZeroAndZeroAndBadDistancesAreRefused)
    {
        const CellLighting none = lighting(0.0, 0.0);
        const CellLighting equal = lighting(500.0, 500.0);
        const CellLighting inverted = lighting(900.0, 100.0);
        const CellLighting negativeNear = lighting(-5.0, 1000.0);
        const CellLighting nanFar = lighting(0.0, std::numeric_limits<double>::quiet_NaN());
        const CellLighting infFar = lighting(0.0, std::numeric_limits<double>::infinity());
        for (const CellLighting* l : { &none, &equal, &inverted, &negativeNear, &nanFar, &infFar })
            EXPECT_FALSE(neutralInteriorFog(l, true).has_value()) << l->fogNear << " " << l->fogFar;
    }

    TEST(NeutralCellFog, aCellHasNoNeutralFogUnlessSetAndItsFogManagerPathIsTheCells)
    {
        ESM::Cell record;
        record.mData.mFlags = ESM::Cell::Interior;
        record.mAmbi.mFog = 0x00605040; // 8 bit colour, stored as the record stores it
        MWWorld::Cell plain(record);
        EXPECT_FALSE(plain.hasNeutralFog());

        MWWorld::Cell fogged(record);
        fogged.setNeutralFog(340.f, 14000.f);
        EXPECT_TRUE(fogged.hasNeutralFog());
        EXPECT_FLOAT_EQ(fogged.getNeutralFogStart(), 340.f);
        EXPECT_FLOAT_EQ(fogged.getNeutralFogEnd(), 14000.f);

        MWRender::FogManager manager;
        manager.configure(8192.f, fogged);
        EXPECT_FLOAT_EQ(manager.getFogStart(false), 340.f);
        EXPECT_FLOAT_EQ(manager.getFogEnd(false), 14000.f);
        const vsg::vec4 colour = manager.getFogColor(false);
        EXPECT_NEAR(colour.x, 0x40 / 255.f, 1e-4f);
        EXPECT_NEAR(colour.y, 0x50 / 255.f, 1e-4f);
        EXPECT_NEAR(colour.z, 0x60 / 255.f, 1e-4f);
    }

    TEST(NeutralCellFog, theCellObjectTakesTheFogOfItsLightingWhenTheSettingIsOn)
    {
        WorldCell source;
        source.id = 7;
        source.editorId = "FogInterior";
        source.interior = true;
        CellLighting l = lighting(100.0, 3000.0);
        l.fogColor = { 10, 20, 30 };
        source.lighting = l;
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);

        const MWWorld::Cell on = MWWorld::makeNeutralInteriorCell(record, source);
        EXPECT_TRUE(on.hasNeutralFog());
        EXPECT_FLOAT_EQ(on.getNeutralFogStart(), 100.f);
        EXPECT_FLOAT_EQ(on.getNeutralFogEnd(), 3000.f);

        source.lighting->fogFar = 0.0; // "no fog"
        EXPECT_FALSE(MWWorld::makeNeutralInteriorCell(record, source).hasNeutralFog());
        source.lighting.reset();
        EXPECT_FALSE(MWWorld::makeNeutralInteriorCell(record, source).hasNeutralFog());
    }

    TEST(NeutralCellFog, theSettingTurnsItOff)
    {
        WorldCell source;
        source.id = 8;
        source.editorId = "FogInteriorOff";
        source.interior = true;
        source.lighting = lighting(0.0, 4500.0);
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        Settings::game().mNeutralInteriorFog.set(false);
        const bool off = MWWorld::makeNeutralInteriorCell(record, source).hasNeutralFog();
        Settings::game().mNeutralInteriorFog.set(true);
        EXPECT_FALSE(off);
        EXPECT_TRUE(MWWorld::makeNeutralInteriorCell(record, source).hasNeutralFog());
    }

    TEST(NeutralCellFog, theDefaultsAreOnBecauseTheyOnlyReachNeutralCells)
    {
        EXPECT_TRUE(Settings::game().mNeutralInteriorFog.get());
        EXPECT_TRUE(Settings::game().mNeutralCellImageSpace.get());
    }
}
