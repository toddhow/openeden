// Task 0355 (follow-up of stage d): the fog power and maximum of a game pack interior reach the cell object and the
// fog manager, made safe on the way (apps/openmw/mwworld/neutralcellatmosphere.hpp). Hand-worked numbers.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <components/esm3/loadcell.hpp>
#include <components/render/fogcurve.hpp>
#include <components/settings/values.hpp>

#include "apps/openmw/mwrender/fogmanager.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralcellatmosphere.hpp"

namespace
{
    using MWWorld::NeutralFogCurve;
    using MWWorld::neutralFogCurve;
    using Pair = std::pair<double, double>;
    constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

    TEST(NeutralFogCurve, aSaneCurveGoesThroughUnchanged)
    {
        const auto c = neutralFogCurve(Pair{ 0.7, 0.8 }, true);
        ASSERT_TRUE(c.has_value());
        EXPECT_FLOAT_EQ(c->power, 0.7f);
        EXPECT_FLOAT_EQ(c->max, 0.8f);
        const auto powerOnly = neutralFogCurve(Pair{ 0.6, 1.0 }, true);
        ASSERT_TRUE(powerOnly.has_value());
        EXPECT_FLOAT_EQ(powerOnly->max, 1.f);
    }

    TEST(NeutralFogCurve, theLinearCurveTheSettingOffAndNoValuesGiveNothing)
    {
        EXPECT_FALSE(neutralFogCurve(Pair{ 1.0, 1.0 }, true).has_value());
        EXPECT_FALSE(neutralFogCurve(Pair{ 0.7, 0.8 }, false).has_value());
        EXPECT_FALSE(neutralFogCurve(std::nullopt, true).has_value());
    }

    TEST(NeutralFogCurve, theWildNumbersOfTheDataBecomeOne)
    {
        // The five masters have fog max 6000, 8 and 13000 (garbage) and 22 zeros ("not set"): all mean linear. A power
        // that is not a number, zero, negative or past the encodable 255/64 is also 1.
        EXPECT_FALSE(neutralFogCurve(Pair{ 1.0, 6000.0 }, true).has_value());
        EXPECT_FALSE(neutralFogCurve(Pair{ 1.0, 8.0 }, true).has_value());
        EXPECT_FALSE(neutralFogCurve(Pair{ 1.0, 13000.0 }, true).has_value());
        EXPECT_FALSE(neutralFogCurve(Pair{ 1.0, 0.0 }, true).has_value());
        EXPECT_FALSE(neutralFogCurve(Pair{ 0.0, 1.0 }, true).has_value());
        EXPECT_FALSE(neutralFogCurve(Pair{ -2.0, 1.0 }, true).has_value());
        EXPECT_FALSE(neutralFogCurve(Pair{ kNan, kNan }, true).has_value());
        EXPECT_FALSE(neutralFogCurve(Pair{ 50.0, 1.0 }, true).has_value());
        // one bad half does not spoil the other
        const auto c = neutralFogCurve(Pair{ 0.7, 13000.0 }, true);
        ASSERT_TRUE(c.has_value());
        EXPECT_FLOAT_EQ(c->power, 0.7f);
        EXPECT_FLOAT_EQ(c->max, 1.f);
        const auto d = neutralFogCurve(Pair{ kNan, 0.5 }, true);
        ASSERT_TRUE(d.has_value());
        EXPECT_FLOAT_EQ(d->power, 1.f);
        EXPECT_FLOAT_EQ(d->max, 0.5f);
    }

    TEST(NeutralFogCurve, theCellAndTheFogManagerCarryIt)
    {
        ESM::Cell record;
        record.mData.mFlags = ESM::Cell::Interior;
        MWWorld::Cell plain(record);
        EXPECT_EQ(plain.getNeutralFogPower(), 1.f);
        EXPECT_EQ(plain.getNeutralFogMax(), 1.f);

        MWWorld::Cell fogged(record);
        fogged.setNeutralFog(0.f, 12000.f, 0.7f, 0.8f);
        EXPECT_FLOAT_EQ(fogged.getNeutralFogPower(), 0.7f);
        EXPECT_FLOAT_EQ(fogged.getNeutralFogMax(), 0.8f);

        MWRender::FogManager manager;
        EXPECT_EQ(manager.getFogCurve(false), Render::FogCurve::Curve{});
        manager.configure(8192.f, fogged);
        EXPECT_FLOAT_EQ(manager.getFogCurve(false).power, 0.7f);
        EXPECT_FLOAT_EQ(manager.getFogCurve(false).max, 0.8f);
        EXPECT_EQ(manager.getFogCurve(true), Render::FogCurve::Curve{}) << "never under water";
        // a cell with no neutral fog, and the fog's other configure, put the plain fog back
        manager.configure(8192.f, plain);
        EXPECT_EQ(manager.getFogCurve(false), Render::FogCurve::Curve{});
        manager.configure(8192.f, fogged);
        manager.configure(8192.f, 0.5f, 0.5f, 1.f, 0.f, vsg::vec4(0, 0, 0, 1));
        EXPECT_EQ(manager.getFogCurve(false), Render::FogCurve::Curve{});
    }

    TEST(NeutralFogCurve, aNeutralInteriorTakesTheCurveItIsGivenOnlyWithItsFog)
    {
        openeden::sdk::game::WorldCell source;
        source.id = 9;
        source.editorId = "FogCurveInterior";
        source.interior = true;
        openeden::sdk::game::CellLighting l;
        l.fogNear = 100.0;
        l.fogFar = 3000.0;
        source.lighting = l;
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);

        const MWWorld::Cell on = MWWorld::makeNeutralInteriorCell(record, source, NeutralFogCurve{ 0.7f, 0.8f });
        EXPECT_TRUE(on.hasNeutralFog());
        EXPECT_FLOAT_EQ(on.getNeutralFogPower(), 0.7f);
        EXPECT_FLOAT_EQ(on.getNeutralFogMax(), 0.8f);
        // no curve given: linear
        EXPECT_EQ(MWWorld::makeNeutralInteriorCell(record, source).getNeutralFogPower(), 1.f);
        // no fog distances (far 0): no fog at all, the curve is dropped with it
        source.lighting->fogFar = 0.0;
        const MWWorld::Cell none = MWWorld::makeNeutralInteriorCell(record, source, NeutralFogCurve{ 0.7f, 0.8f });
        EXPECT_FALSE(none.hasNeutralFog());
        EXPECT_EQ(none.getNeutralFogPower(), 1.f);
    }
}
