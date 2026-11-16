// Task 0202: an object a script created (PlaceAtMe) put into a loaded neutral CellStore. Written AFTER the code (the
// engine
// glue was built in one pass with the stubs of the pure parts); the first run was already green, so the evidence that
// these
// tests can fail is by mutation (see the task's stage log).
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>

#include <openeden/game/world_source_service.hpp>

#include "apps/openmw/mwclass/neutral.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"
#include "apps/openmw/mwworld/ptr.hpp"

namespace
{
    using namespace openeden::sdk::game;

    struct CreatedFixture : testing::Test
    {
        void SetUp() override { MWClass::Neutral::registerSelf(); }

        static WorldObject object(WorldId id, WorldId base, Vec3d position)
        {
            WorldObject o;
            o.id = id;
            o.base = base;
            o.kind = ObjectKind::Static;
            o.baseEditorId = "Base" + std::to_string(base);
            o.modelPath = "clutter\\base" + std::to_string(base) + ".nif";
            o.collision.source = CollisionSource::FromModel;
            o.transform.position = position;
            return o;
        }

        static WorldCell cell()
        {
            WorldCell c;
            c.id = 5;
            c.editorId = "TestInterior";
            c.interior = true;
            c.objects.push_back(object(1, 100, { 1, 2, 3 }));
            c.objects.push_back(object(2, 101, { 4, 5, 6 }));
            return c;
        }

        void load(const WorldCell& source, MWWorld::CellStore& store)
        {
            for (const WorldObject& o : source.objects)
                ASSERT_NE(mStore.getWritable<MWWorld::NeutralBase>().addFromObject("skyrimse", o).mBase, nullptr);
            ASSERT_TRUE(store.setNeutralCell("skyrimse", source));
            store.load();
        }

        std::size_t listed(MWWorld::CellStore& store)
        {
            std::size_t n = 0;
            store.forEachNeutralRef([&](const MWWorld::LiveCellRef<MWWorld::NeutralBase>&) { ++n; });
            return n;
        }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
    };

    TEST_F(CreatedFixture, a_created_object_is_listed_with_its_id_as_the_reference_number_and_in_the_snapshot)
    {
        const WorldCell source = cell();
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        load(source, store);
        ASSERT_EQ(listed(store), 2u);

        const WorldObject made = object(kCreatedObjectBase + 1, 100, { 10, 20, 30 });
        std::string error;
        MWWorld::LiveCellRef<MWWorld::NeutralBase>* live = store.insertNeutralCreated(made, false, error);
        ASSERT_NE(live, nullptr) << error;
        EXPECT_EQ(listed(store), 3u);
        EXPECT_EQ(live->mRef.getRefNum().mIndex, static_cast<std::uint32_t>(made.id));
        EXPECT_EQ(live->mRef.getRefNum().mContentFile, MWWorld::kNeutralRefNumFile);
        EXPECT_TRUE(live->mData.isEnabled());
        EXPECT_FLOAT_EQ(live->mData.getPosition().pos[1], 20.f);
        EXPECT_TRUE(store.isNeutralCreated(made.id));
        EXPECT_FALSE(store.isNeutralCreated(1));
        const WorldObject* found = store.neutralObject(made.id);
        ASSERT_NE(found, nullptr) << "a lookup by id finds it (the snapshot stays ascending)";
        EXPECT_EQ(found->base, 100u);
        EXPECT_EQ(store.neutralObject(2)->id, 2u) << "the pack's own placements are still found";
    }

    TEST_F(CreatedFixture, a_created_object_is_visited_by_forEach_like_every_other_reference)
    {
        const WorldCell source = cell();
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        load(source, store);
        const auto count = [&] {
            std::size_t visited = 0;
            store.forEach(
                [&](const MWWorld::Ptr&) {
                    ++visited;
                    return true;
                },
                true);
            return visited;
        };
        ASSERT_EQ(count(), 2u);
        std::string error;
        ASSERT_NE(store.insertNeutralCreated(object(kCreatedObjectBase + 1, 100, { 0, 0, 0 }), false, error), nullptr);
        EXPECT_EQ(count(), 3u) << "the merged list the visitors walk was refreshed";
    }

    TEST_F(CreatedFixture, a_created_object_can_start_disabled_so_that_enable_adds_it_to_the_scene)
    {
        const WorldCell source = cell();
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        load(source, store);
        std::string error;
        MWWorld::LiveCellRef<MWWorld::NeutralBase>* live
            = store.insertNeutralCreated(object(kCreatedObjectBase + 1, 100, { 0, 0, 0 }), true, error);
        ASSERT_NE(live, nullptr) << error;
        EXPECT_FALSE(live->mData.isEnabled());
    }

    TEST_F(CreatedFixture, ids_must_keep_the_snapshot_ascending_and_the_base_must_be_known)
    {
        const WorldCell source = cell();
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        load(source, store);
        std::string error;
        ASSERT_NE(store.insertNeutralCreated(object(kCreatedObjectBase + 5, 100, { 0, 0, 0 }), false, error), nullptr);
        EXPECT_EQ(store.insertNeutralCreated(object(kCreatedObjectBase + 3, 100, { 0, 0, 0 }), false, error), nullptr);
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(store.insertNeutralCreated(object(kCreatedObjectBase + 5, 100, { 0, 0, 0 }), false, error), nullptr)
            << "the same id twice";
        // a base nobody described: its record is not in the store
        EXPECT_EQ(store.insertNeutralCreated(object(kCreatedObjectBase + 6, 999, { 0, 0, 0 }), false, error), nullptr);
        EXPECT_NE(error.find("no base record"), std::string::npos) << error;
        EXPECT_EQ(listed(store), 3u) << "only the one that was fine was added";
    }

    TEST_F(CreatedFixture, an_unloaded_cell_takes_nothing)
    {
        const WorldCell source = cell();
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        ASSERT_TRUE(store.setNeutralCell("skyrimse", source));
        std::string error;
        EXPECT_EQ(store.insertNeutralCreated(object(kCreatedObjectBase + 1, 100, { 0, 0, 0 }), false, error), nullptr);
        EXPECT_FALSE(error.empty());
    }
}
