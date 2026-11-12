// Task 0129 (M-S2 water): the water of a neutral exterior cell in the engine. MWWorld::Cell takes a water height,
// and WorldModel::findNeutralExterior asks the source's optional CellWaterSource for each grid square (the pack cell
// and the empty squares alike). A source without the interface, or one that says no water, gives a dry cell.
// Written before the implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/game/world_water.hpp>

#include "apps/openmw/mwclass/neutral.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

namespace
{
    using namespace openeden::sdk::game;

    constexpr WorldId kWorld = 20;

    ESM::Cell exteriorRecord(std::int32_t x, std::int32_t y)
    {
        return MWWorld::makeNeutralExteriorCellRecord(ESM::RefId::stringRefId("skyrimse:c77"), "X", x, y);
    }

    TEST(NeutralWater, a_cell_given_a_height_has_water_at_it)
    {
        const ESM::RefId worldspace = MWWorld::neutralWorldspaceId("skyrimse", kWorld);
        const MWWorld::Cell cell(exteriorRecord(6, -2), worldspace, 512.5f);
        EXPECT_TRUE(cell.hasWater());
        EXPECT_FLOAT_EQ(cell.getWaterHeight(), 512.5f);
        EXPECT_TRUE(cell.isExterior());
        EXPECT_EQ(cell.getWorldSpace(), worldspace);
    }

    TEST(NeutralWater, a_cell_given_no_height_is_dry_as_before)
    {
        const ESM::RefId worldspace = MWWorld::neutralWorldspaceId("skyrimse", kWorld);
        const MWWorld::Cell cell(exteriorRecord(6, -2), worldspace, std::nullopt);
        EXPECT_FALSE(cell.hasWater());
        EXPECT_EQ(cell.getWaterHeight(), -std::numeric_limits<float>::max());
        const MWWorld::Cell plain(exteriorRecord(6, -2), worldspace);
        EXPECT_FALSE(plain.hasWater());
    }

    WorldObject plain(WorldId id)
    {
        WorldObject o;
        o.id = id;
        o.base = 900;
        o.kind = ObjectKind::Static;
        o.baseEditorId = "Base";
        o.modelPath = "x.nif";
        o.collision.source = CollisionSource::FromModel;
        o.collision.hasBounds = true;
        o.collision.boundsMin = { -1, -1, -1 };
        o.collision.boundsMax = { 1, 1, 1 };
        return o;
    }

    // Cell 30 is the exterior (2, 3) of world 20; every other square of the world is empty.
    class PlainSource : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "fakegame"; }
        std::size_t cellCount() const override { return 1; }
        std::size_t listCells(std::size_t first, std::size_t max, std::vector<WorldId>& out) const override
        {
            if (first == 0 && max > 0)
                out.push_back(30);
            return out.size();
        }
        bool buildCell(WorldId id, WorldCell& out, std::string& error) override
        {
            out = {};
            out.id = id;
            if (id != 30)
            {
                error = "no such cell";
                return false;
            }
            out.interior = false;
            out.world = kWorld;
            out.hasGrid = true;
            out.gridX = 2;
            out.gridY = 3;
            out.objects.push_back(plain(3001));
            return true;
        }
        WorldId findCellByGrid(WorldId world, std::int32_t x, std::int32_t y) const override
        {
            return world == kWorld && x == 2 && y == 3 ? 30 : kNoWorldId;
        }
        bool worldInfo(WorldId world, WorldSpaceInfo& out) const override
        {
            if (world != kWorld)
                return false;
            out = {};
            out.id = kWorld;
            out.editorId = "TestWorld";
            return true;
        }
    };

    class WaterSource final : public PlainSource, public CellWaterSource
    {
    public:
        bool cellWater(WorldId world, std::int32_t x, std::int32_t y, CellWater& out) const override
        {
            out = {};
            if (world != kWorld)
                return false;
            ++mAsked;
            if (x == 2 && y == 3)
                out = { true, 500.f };
            else if (x == 6 && y == 6)
                out = { false, 0.f };
            else
                out = { true, -14000.f };
            return true;
        }
        mutable int mAsked = 0;
    };

    template <class Source>
    struct Fixture : testing::Test
    {
        void SetUp() override
        {
            MWClass::Neutral::registerSelf();
            std::string error;
            ASSERT_TRUE(mNeutral.addSource(mSource, error)) << error;
            mModel = std::make_unique<MWWorld::WorldModel>(mStore, mReaders);
            mWorldspace = mModel->findNeutralCell(mNeutral, "fakegame", 30).mWorldspace;
            ASSERT_FALSE(mWorldspace.empty());
        }
        MWWorld::CellStore* square(std::int32_t x, std::int32_t y)
        {
            return mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(x, y, mWorldspace));
        }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        Source mSource;
        NeutralWorld::NeutralWorldStore mNeutral;
        std::unique_ptr<MWWorld::WorldModel> mModel;
        ESM::RefId mWorldspace;
    };

    using WithWater = Fixture<WaterSource>;
    using WithoutWater = Fixture<PlainSource>;

    TEST_F(WithWater, a_pack_cell_takes_the_height_the_source_gives)
    {
        MWWorld::CellStore* store = square(2, 3);
        ASSERT_NE(store, nullptr);
        EXPECT_TRUE(store->getCell()->hasWater());
        EXPECT_FLOAT_EQ(store->getWaterLevel(), 500.f);
    }

    TEST_F(WithWater, an_empty_square_still_gets_the_worlds_water)
    {
        MWWorld::CellStore* store = square(-7, 8);
        ASSERT_NE(store, nullptr);
        EXPECT_TRUE(store->getCell()->hasWater());
        EXPECT_FLOAT_EQ(store->getWaterLevel(), -14000.f);
    }

    TEST_F(WithWater, a_square_the_source_says_has_no_water_is_dry)
    {
        MWWorld::CellStore* store = square(6, 6);
        ASSERT_NE(store, nullptr);
        EXPECT_FALSE(store->getCell()->hasWater());
    }

    TEST_F(WithWater, the_source_is_asked_once_for_a_square_that_comes_back)
    {
        ASSERT_NE(square(-7, 8), nullptr);
        const int asked = mSource.mAsked;
        EXPECT_GT(asked, 0);
        ASSERT_NE(square(-7, 8), nullptr);
        EXPECT_EQ(mSource.mAsked, asked);
    }

    TEST_F(WithoutWater, a_source_without_the_interface_gives_dry_cells)
    {
        MWWorld::CellStore* pack = square(2, 3);
        MWWorld::CellStore* empty = square(-7, 8);
        ASSERT_NE(pack, nullptr);
        ASSERT_NE(empty, nullptr);
        EXPECT_FALSE(pack->getCell()->hasWater());
        EXPECT_FALSE(empty->getCell()->hasWater());
    }
}
