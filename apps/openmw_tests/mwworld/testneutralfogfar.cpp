// Task 0362 item 1: the far fog colour of a game pack's interior reaches the cell and the fog manager: the cell's own fog
// colour is where the fog starts, the far colour is the colour of the fog's end and of the empty background.
#include <gtest/gtest.h>

#include <components/esm3/loadcell.hpp>

#include "apps/openmw/mwrender/fogmanager.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralcellatmosphere.hpp"

namespace
{
    
    TEST(NeutralFogFar, aCellWithoutAFarColourHasNone)
    {
        ESM::Cell record;
        record.mData.mFlags = ESM::Cell::Interior;
        const MWWorld::Cell plain(record);
        EXPECT_FALSE(plain.hasNeutralFogFarColor());
    }

    TEST(NeutralFogFar, theFogManagerKeepsTheCellsColourAndHandsOverTheFarOne)
    {
        ESM::Cell record;
        record.mData.mFlags = ESM::Cell::Interior;
        record.mAmbi.mFog = 0x00201008; // the near colour (packed bytes, as the ESM::Cell holds it)
        MWWorld::Cell cell(record);
        cell.setNeutralFog(0.f, 5000.f);
        cell.setNeutralFogFarColor({ 0.25f, 0.5f, 0.75f });

        MWRender::FogManager manager;
        EXPECT_EQ(manager.getFogOtherColor(false), nullptr);
        manager.configure(8192.f, cell);
        const vsg::vec3* start = manager.getFogOtherColor(false);
        ASSERT_NE(start, nullptr);
        EXPECT_TRUE(manager.getFogOtherIsEnd());
        EXPECT_FLOAT_EQ(start->x, 0.25f);
        EXPECT_FLOAT_EQ(start->y, 0.5f);
        EXPECT_FLOAT_EQ(start->z, 0.75f);
        // the scene's own colour stays the cell's (near) colour: every path that does not blend is as before
        EXPECT_NE(manager.getFogColor(false).x, 0.25f);
        EXPECT_EQ(manager.getFogOtherColor(true), nullptr) << "never under water";
    }

    TEST(NeutralFogFar, everyOtherConfigureGivesTheSingleColourBack)
    {
        ESM::Cell record;
        record.mData.mFlags = ESM::Cell::Interior;
        MWWorld::Cell cell(record);
        cell.setNeutralFog(0.f, 5000.f);
        cell.setNeutralFogFarColor({ 1.f, 1.f, 1.f });
        MWRender::FogManager manager;
        manager.configure(8192.f, cell);
        ASSERT_NE(manager.getFogOtherColor(false), nullptr);
        MWWorld::Cell plain(record);
        manager.configure(8192.f, plain);
        EXPECT_EQ(manager.getFogOtherColor(false), nullptr);
        manager.configure(8192.f, cell);
        manager.configure(8192.f, 0.5f, 0.5f, 1.f, 0.f, vsg::vec4(0, 0, 0, 1));
        EXPECT_EQ(manager.getFogOtherColor(false), nullptr);
        // the weather's own configure can give a start colour
        const vsg::vec3 near(0.1f, 0.2f, 0.3f);
        manager.configure(8192.f, 0.5f, 0.5f, 1.f, 0.f, vsg::vec4(0.9f, 0.9f, 0.9f, 1), &near, false);
        ASSERT_NE(manager.getFogOtherColor(false), nullptr);
        EXPECT_FALSE(manager.getFogOtherIsEnd());
        EXPECT_FLOAT_EQ(manager.getFogOtherColor(false)->y, 0.2f);
        EXPECT_FLOAT_EQ(manager.getFogColor(false).x, 0.9f);
    }

    TEST(NeutralFogFar, aNeutralInteriorTakesTheFarColourItIsGivenWithNoCurve)
    {
        openeden::sdk::game::WorldCell source;
        source.id = 9;
        source.editorId = "FogFarInterior";
        source.interior = true;
        openeden::sdk::game::CellLighting l;
        l.fogNear = 100.0;
        l.fogFar = 3000.0;
        source.lighting = l;
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        const std::array<float, 3> far3{ 0.1f, 0.2f, 0.3f };
        const MWWorld::Cell cell = MWWorld::makeNeutralInteriorCell(record, source, std::nullopt, far3);
        ASSERT_TRUE(cell.hasNeutralFogFarColor());
        EXPECT_FLOAT_EQ(cell.getNeutralFogFarColor()[2], 0.3f);
        EXPECT_FLOAT_EQ(cell.getNeutralFogPower(), 1.f);
        // no far colour given: none
        EXPECT_FALSE(MWWorld::makeNeutralInteriorCell(record, source).hasNeutralFogFarColor());
        // no fog (far 0): the colour is dropped with it
        source.lighting->fogFar = 0.0;
        EXPECT_FALSE(MWWorld::makeNeutralInteriorCell(record, source, std::nullopt, far3).hasNeutralFogFarColor());
    }
}
