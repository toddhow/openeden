// Task 0125 (stage E2 of the Skyrim exteriors plan, docs/research/0053): exterior cells of a game pack's neutral world
// in the engine: the worldspace id, the synthetic exterior record, MWWorld::Cell for it, the lookups of
// WorldModel and the door decision for an exterior of a supported world. Written from the task spec before the
// implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>
#include <components/esmbridge/util.hpp>

#include <components/worldmodel/store.hpp>

#include "apps/openmw/mwclass/neutral.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutraldoor.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::NeutralDestination;
    using MWWorld::NeutralDoorOutcome;

    constexpr WorldId kWorld = 20; // a world the source can describe
    constexpr WorldId kOtherWorld = 21; // one it cannot

    // ---- ids and the record ---------------------------------------------------------------------------------------

    TEST(NeutralExteriorIds, the_worldspace_id_names_the_game_and_the_world)
    {
        const ESM::RefId id = MWWorld::neutralWorldspaceId("skyrimse", 20);
        EXPECT_FALSE(id.empty());
        EXPECT_EQ(id, ESM::RefId::stringRefId("skyrimse:w20"));
        EXPECT_NE(id, MWWorld::neutralWorldspaceId("skyrimse", 21));
        EXPECT_NE(id, MWWorld::neutralWorldspaceId("other", 20));
        EXPECT_NE(id, ESM::Cell::sDefaultWorldspaceId);
    }

    TEST(NeutralExteriorIds, a_game_id_that_cannot_be_carried_gives_no_id)
    {
        EXPECT_TRUE(MWWorld::neutralWorldspaceId("", 1).empty());
        EXPECT_TRUE(MWWorld::neutralWorldspaceId("a:b", 1).empty());
        EXPECT_TRUE(MWWorld::neutralEmptyExteriorCellId("a:b", 1, 0, 0).empty());
    }

    TEST(NeutralExteriorIds, an_empty_exterior_cell_id_is_per_world_and_square)
    {
        const ESM::RefId id = MWWorld::neutralEmptyExteriorCellId("skyrimse", 20, -3, 4);
        EXPECT_EQ(id, ESM::RefId::stringRefId("skyrimse:e20.-3.4"));
        EXPECT_NE(id, MWWorld::neutralEmptyExteriorCellId("skyrimse", 20, 3, 4));
        EXPECT_NE(id, MWWorld::neutralEmptyExteriorCellId("skyrimse", 21, -3, 4));
        // Never the id of a pack cell ("<game>:c<id>").
        EXPECT_NE(id, MWWorld::neutralCellRecordId("skyrimse", 20));
    }

    TEST(NeutralExteriorRecord, an_exterior_record_has_the_grid_the_name_and_no_water_and_is_not_an_interior)
    {
        const ESM::RefId id = ESM::RefId::stringRefId("skyrimse:c77");
        const ESM::Cell record = MWWorld::makeNeutralExteriorCellRecord(id, "WhiterunExt", -5, 9);
        EXPECT_EQ(record.mId, id);
        EXPECT_EQ(record.mName, "WhiterunExt");
        EXPECT_FALSE(record.mData.mFlags & ESM::Cell::Interior);
        EXPECT_FALSE(record.mData.mFlags & ESM::Cell::HasWater);
        EXPECT_EQ(record.mData.mX, -5);
        EXPECT_EQ(record.mData.mY, 9);
        EXPECT_EQ(record.getGridX(), -5);
        EXPECT_EQ(record.getGridY(), 9);
        EXPECT_TRUE(record.mContextList.empty());
    }

    TEST(NeutralExteriorCell, the_cell_takes_the_given_worldspace_and_has_no_water)
    {
        const ESM::RefId worldspace = MWWorld::neutralWorldspaceId("skyrimse", 20);
        const ESM::Cell record = MWWorld::makeNeutralExteriorCellRecord(ESM::RefId::stringRefId("skyrimse:c77"), "X", 6, -2);
        const MWWorld::Cell cell(record, worldspace);
        EXPECT_TRUE(cell.isExterior());
        EXPECT_FALSE(cell.isQuasiExterior());
        EXPECT_FALSE(cell.hasWater());
        EXPECT_EQ(cell.getGridX(), 6);
        EXPECT_EQ(cell.getGridY(), -2);
        EXPECT_EQ(cell.getWorldSpace(), worldspace);
        EXPECT_EQ(cell.getId(), ESM::RefId::stringRefId("skyrimse:c77"));
        // Skyrim's cell is 4096 units and its grid radius 2: the existing rules for any other worldspace.
        EXPECT_EQ(ESM::getCellSize(cell.getWorldSpace()), 4096);
        EXPECT_EQ(cell.getExteriorCellLocation(), ESM::ExteriorCellLocation(6, -2, worldspace));
    }

    TEST(NeutralExteriorCell, the_plain_morrowind_constructor_is_unchanged)
    {
        ESM::Cell record;
        record.mData.mFlags = 0;
        record.mData.mX = 3;
        record.mData.mY = 4;
        record.mId = ESM::RefId::stringRefId("c");
        const MWWorld::Cell cell(record);
        EXPECT_TRUE(cell.isExterior());
        EXPECT_TRUE(cell.hasWater());
        EXPECT_EQ(cell.getWorldSpace(), ESM::Cell::sDefaultWorldspaceId);
        EXPECT_EQ(ESM::getCellSize(cell.getWorldSpace()), 8192);
    }

    // ---- the door decision ----------------------------------------------------------------------------------------

    TEST(NeutralExteriorDoor, an_exterior_of_a_supported_world_teleports_to_the_arrival)
    {
        WorldObject door;
        door.id = 7;
        door.base = 500;
        door.kind = ObjectKind::Door;
        TeleportComponent t;
        t.destinationCell = 42;
        t.destinationDoor = 8;
        t.destination.position = { 100.5, -200.25, 300.0 };
        door.teleport = t;
        const auto d = MWWorld::decideNeutralDoor(
            door, true, false, [](WorldId) { return NeutralDestination::ExteriorWorld; });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Teleport);
        EXPECT_EQ(d.mDestinationCell, 42u);
        EXPECT_FLOAT_EQ(d.mArrival.pos[0], 100.5f);
        EXPECT_FLOAT_EQ(d.mArrival.pos[1], -200.25f);
    }

    TEST(NeutralExteriorDoor, an_exterior_of_an_unsupported_world_is_still_refused)
    {
        WorldObject door;
        door.id = 7;
        door.base = 500;
        door.kind = ObjectKind::Door;
        TeleportComponent t;
        t.destinationCell = 42;
        door.teleport = t;
        const auto d
            = MWWorld::decideNeutralDoor(door, true, false, [](WorldId) { return NeutralDestination::Exterior; });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Exterior);
    }

    // ---- WorldModel ------------------------------------------------------------------------------------------------

    WorldObject plain(WorldId id, WorldId base, ObjectKind kind)
    {
        WorldObject o;
        o.id = id;
        o.base = base;
        o.kind = kind;
        o.baseEditorId = "Base" + std::to_string(base);
        o.modelPath = "x" + std::to_string(base) + ".nif";
        o.collision.source = CollisionSource::FromModel;
        o.collision.hasBounds = true;
        o.collision.boundsMin = { -1, -1, -1 };
        o.collision.boundsMax = { 1, 1, 1 };
        return o;
    }

    // Cells: 10 an interior; 30 the exterior (2, 3) of world 20 with one object; 31 the exterior (2, 4) of world 20 with
    // two; 32 the persistent exterior of world 20 (no grid); 40 an exterior of world 21 (no terrain support).
    class ExteriorSource final : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "fakegame"; }
        std::size_t cellCount() const override { return 5; }
        std::size_t listCells(std::size_t first, std::size_t max, std::vector<WorldId>& out) const override
        {
            std::size_t n = 0;
            for (WorldId id : { 10u, 30u, 31u, 32u, 40u })
            {
                if (n++ < first)
                    continue;
                if (out.size() >= max)
                    break;
                out.push_back(id);
            }
            return out.size();
        }
        bool buildCell(WorldId id, WorldCell& out, std::string& error) override
        {
            ++mBuilds;
            out = {};
            out.id = id;
            if (id == 10)
            {
                out.editorId = "Inn";
                out.interior = true;
                out.objects.push_back(plain(1001, 900, ObjectKind::Static));
                return true;
            }
            if (id == 30 || id == 31 || id == 32 || id == 40)
            {
                out.interior = false;
                out.world = id == 40 ? kOtherWorld : kWorld;
                if (id == 30 || id == 31)
                {
                    out.hasGrid = true;
                    out.gridX = 2;
                    out.gridY = id == 30 ? 3 : 4;
                    out.editorId = id == 30 ? "Ext30" : "";
                    out.objects.push_back(plain(id * 100 + 1, 901, ObjectKind::Static));
                    if (id == 31)
                        out.objects.push_back(plain(id * 100 + 2, 902, ObjectKind::Static));
                }
                return true;
            }
            error = "no such cell";
            return false;
        }
        WorldId findCellByGrid(WorldId world, std::int32_t x, std::int32_t y) const override
        {
            if (world == kWorld && x == 2 && y == 3)
                return 30;
            if (world == kWorld && x == 2 && y == 4)
                return 31;
            return kNoWorldId;
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
        int mBuilds = 0;
    };

    struct ExteriorFixture : testing::Test
    {
        void SetUp() override
        {
            MWClass::Neutral::registerSelf();
            std::string error;
            ASSERT_TRUE(mNeutral.addSource(mSource, error)) << error;
            mModel = std::make_unique<MWWorld::WorldModel>(mStore, mReaders);
        }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        ExteriorSource mSource;
        NeutralWorld::NeutralWorldStore mNeutral;
        std::unique_ptr<MWWorld::WorldModel> mModel;
    };

    TEST_F(ExteriorFixture, an_exterior_of_a_supported_world_is_found_with_its_worldspace_and_grid)
    {
        const auto r = mModel->findNeutralCell(mNeutral, "fakegame", 30);
        EXPECT_EQ(r.mStatus, MWWorld::NeutralCellStatus::Exterior);
        EXPECT_TRUE(r.mWorldSupported);
        EXPECT_EQ(r.mWorldspace, MWWorld::neutralWorldspaceId("fakegame", kWorld));
        EXPECT_TRUE(r.mHasGrid);
        EXPECT_EQ(r.mGridX, 2);
        EXPECT_EQ(r.mGridY, 3);
        EXPECT_EQ(r.mStore, nullptr);
        ASSERT_NE(mModel->neutralWorldspace(r.mWorldspace), nullptr);
        EXPECT_EQ(mModel->neutralWorldspace(r.mWorldspace)->mGameId, "fakegame");
        EXPECT_EQ(mModel->neutralWorldspace(r.mWorldspace)->mWorld, kWorld);
    }

    TEST_F(ExteriorFixture, the_persistent_cell_has_a_worldspace_but_no_grid)
    {
        const auto r = mModel->findNeutralCell(mNeutral, "fakegame", 32);
        EXPECT_EQ(r.mStatus, MWWorld::NeutralCellStatus::Exterior);
        EXPECT_TRUE(r.mWorldSupported);
        EXPECT_FALSE(r.mHasGrid);
        EXPECT_EQ(r.mWorldspace, MWWorld::neutralWorldspaceId("fakegame", kWorld));
    }

    TEST_F(ExteriorFixture, an_exterior_of_a_world_the_source_cannot_describe_is_not_supported_and_registers_nothing)
    {
        const auto r = mModel->findNeutralCell(mNeutral, "fakegame", 40);
        EXPECT_EQ(r.mStatus, MWWorld::NeutralCellStatus::Exterior);
        EXPECT_FALSE(r.mWorldSupported);
        EXPECT_EQ(mModel->neutralWorldspace(MWWorld::neutralWorldspaceId("fakegame", kOtherWorld)), nullptr);
        EXPECT_EQ(mStore.get<MWWorld::NeutralBase>().getSize(), 0u);
    }

    TEST_F(ExteriorFixture, no_other_worldspace_is_a_neutral_one)
    {
        EXPECT_EQ(mModel->neutralWorldspace(ESM::Cell::sDefaultWorldspaceId), nullptr);
        EXPECT_EQ(mModel->neutralWorldspace(MWWorld::neutralWorldspaceId("fakegame", kWorld)), nullptr); // not yet found
        const ESM::ExteriorCellLocation morrowind(0, 0, ESM::Cell::sDefaultWorldspaceId);
        EXPECT_EQ(mModel->findNeutralExterior(mNeutral, morrowind), nullptr);
    }

    TEST_F(ExteriorFixture, a_registered_worlds_grid_square_with_a_pack_cell_becomes_an_exterior_store_with_its_objects)
    {
        const ESM::RefId worldspace = mModel->findNeutralCell(mNeutral, "fakegame", 30).mWorldspace;
        MWWorld::CellStore* store = mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(2, 3, worldspace));
        ASSERT_NE(store, nullptr);
        EXPECT_TRUE(store->isNeutral());
        EXPECT_TRUE(store->isExterior());
        EXPECT_FALSE(store->getCell()->hasWater());
        EXPECT_EQ(store->getCell()->getGridX(), 2);
        EXPECT_EQ(store->getCell()->getGridY(), 3);
        EXPECT_EQ(store->getCell()->getWorldSpace(), worldspace);
        store->load();
        EXPECT_EQ(store->neutralLoaded(), 1u);
        // The same store comes back, by location and by its record id.
        EXPECT_EQ(mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(2, 3, worldspace)), store);
        EXPECT_EQ(&mModel->getCell(store->getCell()->getId()), store);
    }

    TEST_F(ExteriorFixture, a_square_the_pack_has_no_cell_for_is_an_empty_exterior_cell)
    {
        const ESM::RefId worldspace = mModel->findNeutralCell(mNeutral, "fakegame", 30).mWorldspace;
        MWWorld::CellStore* store = mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(-7, 8, worldspace));
        ASSERT_NE(store, nullptr);
        EXPECT_TRUE(store->isExterior());
        EXPECT_EQ(store->getCell()->getGridX(), -7);
        EXPECT_EQ(store->getCell()->getGridY(), 8);
        EXPECT_EQ(store->getCell()->getWorldSpace(), worldspace);
        store->load();
        EXPECT_EQ(store->neutralLoaded(), 0u);
        EXPECT_EQ(store->count(), 0u);
        EXPECT_EQ(mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(-7, 8, worldspace)), store);
    }

    TEST_F(ExteriorFixture, two_squares_are_two_stores_and_building_one_twice_asks_the_source_once)
    {
        const ESM::RefId worldspace = mModel->findNeutralCell(mNeutral, "fakegame", 30).mWorldspace;
        MWWorld::CellStore* a = mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(2, 3, worldspace));
        MWWorld::CellStore* b = mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(2, 4, worldspace));
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        EXPECT_NE(a, b);
        b->load();
        EXPECT_EQ(b->neutralLoaded(), 2u);
        const int builds = mSource.mBuilds;
        EXPECT_EQ(mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(2, 4, worldspace)), b);
        EXPECT_EQ(mSource.mBuilds, builds);
    }

    TEST_F(ExteriorFixture, the_base_records_of_an_exterior_cell_go_into_the_store_and_interiors_are_unchanged)
    {
        const ESM::RefId worldspace = mModel->findNeutralCell(mNeutral, "fakegame", 30).mWorldspace;
        ASSERT_NE(mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(2, 4, worldspace)), nullptr);
        EXPECT_EQ(mStore.get<MWWorld::NeutralBase>().getSize(), 2u); // bases 901 and 902
        const auto interior = mModel->findNeutralCell(mNeutral, "fakegame", 10);
        EXPECT_EQ(interior.mStatus, MWWorld::NeutralCellStatus::Found);
        EXPECT_FALSE(interior.mWorldSupported);
        EXPECT_FALSE(interior.mStore->isExterior());
    }
}
