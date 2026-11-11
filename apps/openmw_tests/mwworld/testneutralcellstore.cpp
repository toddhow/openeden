// Task 0109 (neutral world, stage N3a-3): a neutral cell loaded into a real MWWorld::CellStore. The cell lists what the
// neutral cell lists (count, ids, transforms, kinds): the N1 projection tests in reverse. Written from the task spec
// before the implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>
#include <components/misc/convert.hpp>

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

    struct NeutralCellStoreFixture : testing::Test
    {
        void SetUp() override { MWClass::Neutral::registerSelf(); }

        static WorldObject object(WorldId id, WorldId base, ObjectKind kind, Vec3d position, double scale = 1.0)
        {
            WorldObject o;
            o.id = id;
            o.base = base;
            o.kind = kind;
            o.baseEditorId = "Base" + std::to_string(base);
            o.modelPath = "architecture\\whiterun\\base" + std::to_string(base) + ".nif";
            o.collision.source = CollisionSource::FromModel;
            o.collision.hasBounds = true;
            o.collision.boundsMin = { -10, -20, -30 };
            o.collision.boundsMax = { 10, 20, 30 };
            o.transform.position = position;
            o.transform.scale = scale;
            return o;
        }

        static WorldCell cell()
        {
            WorldCell c;
            c.id = 5;
            c.editorId = "TestInterior";
            c.interior = true;
            c.objects.push_back(object(1, 100, ObjectKind::Static, { 1, 2, 3 }));
            c.objects.push_back(object(2, 100, ObjectKind::Static, { 4, 5, 6 }, 2.0)); // the same base twice
            c.objects.push_back(object(3, 101, ObjectKind::Door, { -7, 8, -9 }));
            c.objects.push_back(object(4, 102, ObjectKind::Light, { 100, 200, 300 }));
            c.objects.push_back(object(5, kNoWorldId, ObjectKind::Other, { 0, 0, 0 })); // a placement with no base
            WorldObject disabled = object(6, 103, ObjectKind::Activator, { 9, 9, 9 });
            disabled.initiallyDisabled = true;
            c.objects.push_back(disabled);
            return c;
        }

        // The engine's side of MWWorld::WorldModel::emplaceNeutralInteriorCellStore: the bases, then the cell.
        void fill(const WorldCell& c)
        {
            for (const WorldObject& o : c.objects)
                ASSERT_NE(mStore.getWritable<MWWorld::NeutralBase>().addFromObject("skyrimse", o).mBase, nullptr);
        }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
    };

    TEST_F(NeutralCellStoreFixture, a_loaded_neutral_cell_lists_what_the_neutral_cell_lists)
    {
        const WorldCell source = cell();
        fill(source);
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        ASSERT_TRUE(store.setNeutralCell("skyrimse", source));
        EXPECT_TRUE(store.isNeutral());
        EXPECT_EQ(store.getState(), MWWorld::CellStore::State_Unloaded);
        store.load();
        EXPECT_EQ(store.getState(), MWWorld::CellStore::State_Loaded);
        EXPECT_EQ(store.neutralLoaded(), source.objects.size());
        EXPECT_EQ(store.neutralSkipped(), 0u);

        std::map<std::uint32_t, MWWorld::Ptr> byIndex;
        store.forEach(
            [&](const MWWorld::Ptr& ptr) {
                byIndex[ptr.getCellRef().getRefNum().mIndex] = ptr;
                return true;
            },
            true);
        // The disabled object is listed too (includeDeleted only concerns deleted ones; a disabled one is enabled-flag).
        ASSERT_EQ(byIndex.size(), source.objects.size());

        for (const WorldObject& o : source.objects)
        {
            const auto found = byIndex.find(static_cast<std::uint32_t>(o.id));
            ASSERT_NE(found, byIndex.end()) << o.id;
            const MWWorld::Ptr& ptr = found->second;
            EXPECT_EQ(ptr.getType(), MWWorld::NeutralBase::sRecordId);
            EXPECT_EQ(&ptr.getClass(), &MWWorld::Class::get(MWWorld::NeutralBase::sRecordId));
            EXPECT_EQ(ptr.get<MWWorld::NeutralBase>()->mBase->mKind, o.kind);
            EXPECT_EQ(ptr.get<MWWorld::NeutralBase>()->mBase->mModel, o.modelPath);
            EXPECT_FALSE(ptr.getCellRef().getRefNum().hasContentFile());
            EXPECT_EQ(ptr.getCellRef().getRefNum().mContentFile, MWWorld::kNeutralRefNumFile);
            const ESM::Position& pos = ptr.getRefData().getPosition();
            EXPECT_FLOAT_EQ(pos.pos[0], static_cast<float>(o.transform.position.x));
            EXPECT_FLOAT_EQ(pos.pos[1], static_cast<float>(o.transform.position.y));
            EXPECT_FLOAT_EQ(pos.pos[2], static_cast<float>(o.transform.position.z));
            EXPECT_FLOAT_EQ(ptr.getCellRef().getScale(), static_cast<float>(o.transform.scale));
            EXPECT_EQ(ptr.getRefData().isEnabled(), !o.initiallyDisabled) << o.id;
        }
    }

    TEST_F(NeutralCellStoreFixture, many_placements_share_one_base)
    {
        const WorldCell source = cell();
        fill(source);
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        ASSERT_TRUE(store.setNeutralCell("skyrimse", source));
        store.load();
        std::vector<const MWWorld::NeutralBase*> bases;
        store.forEach(
            [&](const MWWorld::Ptr& ptr) {
                bases.push_back(ptr.get<MWWorld::NeutralBase>()->mBase);
                return true;
            },
            true);
        std::sort(bases.begin(), bases.end());
        bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
        EXPECT_EQ(bases.size(), 5u); // bases 100, 101, 102, 103 and the placement-keyed one
        EXPECT_EQ(mStore.get<MWWorld::NeutralBase>().getSize(), 5u);
    }

    TEST_F(NeutralCellStoreFixture, the_rotation_reaches_the_reference_as_the_engines_angles)
    {
        WorldCell source = cell();
        const float angles[3] = { 0.25f, -0.5f, 1.75f };
        const vsg::dquat q = Misc::Convert::makeOsgQuat(angles);
        source.objects[0].transform.rotation = Quatd{ q.x, q.y, q.z, q.w };
        fill(source);
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        ASSERT_TRUE(store.setNeutralCell("skyrimse", source));
        store.load();
        bool seen = false;
        store.forEach(
            [&](const MWWorld::Ptr& ptr) {
                if (ptr.getCellRef().getRefNum().mIndex != 1)
                    return true;
                seen = true;
                const vsg::dquat back = Misc::Convert::makeOsgQuat(ptr.getRefData().getPosition());
                const double dot = std::abs(back.x * q.x + back.y * q.y + back.z * q.z + back.w * q.w);
                EXPECT_GT(dot, 1.0 - 1e-9);
                return true;
            },
            true);
        EXPECT_TRUE(seen);
    }

    TEST_F(NeutralCellStoreFixture, an_object_without_a_base_record_is_left_out_and_counted)
    {
        const WorldCell source = cell();
        // Only some of the bases are in the store.
        ASSERT_NE(mStore.getWritable<MWWorld::NeutralBase>().addFromObject("skyrimse", source.objects[0]).mBase, nullptr);
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        ASSERT_TRUE(store.setNeutralCell("skyrimse", source));
        store.load();
        EXPECT_EQ(store.neutralLoaded(), 2u); // placements 1 and 2 share base 100
        EXPECT_EQ(store.neutralSkipped(), source.objects.size() - 2);
    }

    TEST_F(NeutralCellStoreFixture, a_placement_the_engine_cannot_hold_is_left_out_and_counted)
    {
        WorldCell source = cell();
        source.objects[0].transform.scale = -1.0; // not usable
        fill(source);
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        ASSERT_TRUE(store.setNeutralCell("skyrimse", source));
        store.load();
        EXPECT_EQ(store.neutralSkipped(), 1u);
        EXPECT_EQ(store.neutralLoaded(), source.objects.size() - 1);
    }

    TEST_F(NeutralCellStoreFixture, a_cell_that_is_loaded_cannot_become_neutral_and_a_plain_cell_is_not_neutral)
    {
        ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", cell());
        MWWorld::CellStore plain{ MWWorld::Cell(record), mStore, mReaders };
        EXPECT_FALSE(plain.isNeutral());
        EXPECT_EQ(plain.neutralCell(), nullptr);
        plain.load();
        EXPECT_EQ(plain.neutralLoaded(), 0u);
        EXPECT_FALSE(plain.setNeutralCell("skyrimse", cell()));
        EXPECT_FALSE(plain.isNeutral());
    }

    TEST_F(NeutralCellStoreFixture, the_cell_keeps_its_own_copy_of_the_snapshot)
    {
        WorldCell source = cell();
        fill(source);
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        ASSERT_TRUE(store.setNeutralCell("skyrimse", source));
        source.objects.clear(); // the source store may drop its cell at its next request
        ASSERT_NE(store.neutralCell(), nullptr);
        EXPECT_EQ(store.neutralCell()->objects.size(), 6u);
        store.load();
        EXPECT_EQ(store.neutralLoaded(), 6u);
    }

    TEST_F(NeutralCellStoreFixture, an_object_can_be_inserted_into_another_cell)
    {
        const WorldCell source = cell();
        fill(source);
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", source);
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        ASSERT_TRUE(store.setNeutralCell("skyrimse", source));
        store.load();
        MWWorld::Ptr first;
        store.forEach(
            [&](const MWWorld::Ptr& ptr) {
                first = ptr;
                return false;
            },
            true);
        ASSERT_FALSE(first.isEmpty());

        ESM::Cell otherRecord = MWWorld::makeNeutralCellRecord("skyrimse", source);
        otherRecord.mName = "Other";
        MWWorld::CellStore other{ MWWorld::Cell(otherRecord), mStore, mReaders };
        other.load();
        // What Neutral::copyToCellImpl does.
        const MWWorld::Ptr copy(other.insert(first.get<MWWorld::NeutralBase>()), &other);
        EXPECT_EQ(copy.getType(), MWWorld::NeutralBase::sRecordId);
        EXPECT_EQ(copy.get<MWWorld::NeutralBase>()->mBase, first.get<MWWorld::NeutralBase>()->mBase);
    }

    TEST(NeutralClass, a_marker_model_and_the_live_things_have_no_model)
    {
        MWClass::Neutral::registerSelf();
        MWWorld::NeutralBase base;
        base.mId = ESM::RefId::stringRefId("skyrimse:b1");
        base.mGameId = "skyrimse";
        base.mKind = ObjectKind::Static;
        base.mModel = "markers\\xmarker.nif";
        ESM::CellRef ref;
        ref.mRefNum = { 1, MWWorld::kNeutralRefNumFile };
        ref.mRefID = base.mId;
        ref.mScale = 1.f;
        MWWorld::LiveCellRef<MWWorld::NeutralBase> live(ref, &base);
        MWWorld::Ptr ptr(&live, nullptr);
        EXPECT_EQ(ptr.getClass().getModel(ptr), "");

        base.mModel = "architecture\\whiterun\\wrfloor.nif";
        EXPECT_EQ(ptr.getClass().getModel(ptr), "architecture\\whiterun\\wrfloor.nif");

        for (const ObjectKind kind : { ObjectKind::Actor, ObjectKind::Item, ObjectKind::LeveledList })
        {
            base.mKind = kind;
            EXPECT_EQ(ptr.getClass().getModel(ptr), "") << static_cast<int>(kind);
        }
        EXPECT_FALSE(ptr.getClass().isActor());
        EXPECT_FALSE(ptr.getClass().hasToolTip(ptr));
        EXPECT_TRUE(ptr.getClass().getScript(ptr).empty());
    }
}
