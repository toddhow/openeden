// Task 0131 (neutral world, stage N3a-6): Lua and mechanics safety over a neutral cell. A neutral object is an inert
// reference, so every MWWorld::Class virtual that a generic engine path (not guarded by isActor, isItem, isDoor or a
// Lua objectIsInstance) can reach with it answers without throwing, for every ObjectKind; the virtuals that only make
// sense for an actor or an item keep refusing (a runtime_error that Lua turns into a Lua error). Written from the task
// spec before the overrides existed; the first run was against the unmodified class.
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>

#include "apps/openmw/mwclass/neutral.hpp"
#include "apps/openmw/mwgui/tooltips.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/doorstate.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"
#include "apps/openmw/mwworld/ptr.hpp"

namespace
{
    using namespace openeden::sdk::game;

    constexpr ObjectKind kAllKinds[]
        = { ObjectKind::Unknown, ObjectKind::Static, ObjectKind::MovableStatic, ObjectKind::Furniture, ObjectKind::Door,
              ObjectKind::Activator, ObjectKind::Container, ObjectKind::Flora, ObjectKind::Tree, ObjectKind::Light,
              ObjectKind::Item, ObjectKind::Actor, ObjectKind::LeveledList, ObjectKind::Other };

    struct NeutralSafetyFixture : testing::Test
    {
        void SetUp() override { MWClass::Neutral::registerSelf(); }

        static WorldObject object(WorldId id, WorldId base, ObjectKind kind)
        {
            WorldObject o;
            o.id = id;
            o.base = base;
            o.kind = kind;
            o.baseEditorId = "Base" + std::to_string(base);
            o.modelPath = "architecture\\base" + std::to_string(base) + ".nif";
            o.collision.source = CollisionSource::FromModel;
            o.collision.hasBounds = true;
            o.collision.boundsMin = { -10, -20, -30 };
            o.collision.boundsMax = { 10, 20, 30 };
            o.transform.position = { double(id), 2, 3 };
            return o;
        }

        // One object of every kind, loaded into a real CellStore; the pointers stay valid while the store lives.
        std::vector<MWWorld::Ptr> loadAllKinds()
        {
            WorldCell c;
            c.id = 9;
            c.editorId = "SafetyInterior";
            c.interior = true;
            WorldId next = 1;
            for (ObjectKind kind : kAllKinds)
            {
                c.objects.push_back(object(next, 100 + next, kind));
                ++next;
            }
            for (const WorldObject& o : c.objects)
                EXPECT_NE(mStore.getWritable<MWWorld::NeutralBase>().addFromObject("skyrimse", o).mBase, nullptr);
            const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", c);
            mCell = std::make_unique<MWWorld::CellStore>(MWWorld::Cell(record), mStore, mReaders);
            EXPECT_TRUE(mCell->setNeutralCell("skyrimse", c));
            mCell->load();
            std::vector<MWWorld::Ptr> result;
            mCell->forEach(
                [&](const MWWorld::Ptr& ptr) {
                    result.push_back(ptr);
                    return true;
                },
                true);
            return result;
        }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        std::unique_ptr<MWWorld::CellStore> mCell;
    };

    TEST_F(NeutralSafetyFixture, every_kind_loads_so_the_checks_below_cover_all_of_them)
    {
        EXPECT_EQ(loadAllKinds().size(), std::size(kAllKinds));
    }

    TEST_F(NeutralSafetyFixture, the_item_properties_generic_paths_read_answer_instead_of_throwing)
    {
        for (const MWWorld::Ptr& ptr : loadAllKinds())
        {
            const MWWorld::Class& cls = ptr.getClass();
            SCOPED_TRACE(ptr.getCellRef().getRefId().serializeText());
            EXPECT_NO_THROW(EXPECT_EQ(cls.getValue(ptr), 0));
            EXPECT_NO_THROW(EXPECT_EQ(cls.getWeight(ptr), 0.f));
            EXPECT_NO_THROW(EXPECT_EQ(cls.getCapacity(ptr), 0.f));
            EXPECT_NO_THROW(EXPECT_EQ(cls.getEncumbrance(ptr), 0.f));
            EXPECT_NO_THROW(EXPECT_EQ(cls.getNormalizedEncumbrance(ptr), 0.f));
            EXPECT_NO_THROW(EXPECT_FALSE(cls.isPersistent(ptr)));
            EXPECT_NO_THROW(EXPECT_EQ(cls.getEnchantmentPoints(ptr), 0));
            EXPECT_NO_THROW(EXPECT_TRUE(cls.getInventoryIcon(ptr).empty()));
            EXPECT_NO_THROW(EXPECT_FALSE(cls.hasItemHealth(ptr)));
            EXPECT_NO_THROW(EXPECT_EQ(cls.getItemHealth(ptr), 0));
            EXPECT_NO_THROW(EXPECT_EQ(cls.getItemNormalizedHealth(ptr), 0.f));
            EXPECT_NO_THROW(EXPECT_EQ(cls.getItemMaxHealth(ptr), 0));
            EXPECT_NO_THROW(EXPECT_TRUE(cls.getUpSoundId(ptr).empty()));
            EXPECT_NO_THROW(EXPECT_TRUE(cls.getDownSoundId(ptr).empty()));
            EXPECT_NO_THROW(EXPECT_TRUE(cls.getSoundIdFromSndGen(ptr, "foot").empty()));
            EXPECT_NO_THROW(EXPECT_EQ(cls.getBaseGold(ptr), 0));
        }
    }

    TEST_F(NeutralSafetyFixture, a_door_state_is_idle_and_cannot_be_set)
    {
        for (const MWWorld::Ptr& ptr : loadAllKinds())
        {
            SCOPED_TRACE(ptr.getCellRef().getRefId().serializeText());
            EXPECT_NO_THROW(EXPECT_EQ(ptr.getClass().getDoorState(ptr), MWWorld::DoorState::Idle));
            EXPECT_NO_THROW(ptr.getClass().setDoorState(ptr, MWWorld::DoorState::Opening));
            EXPECT_EQ(ptr.getClass().getDoorState(ptr), MWWorld::DoorState::Idle);
        }
    }

    TEST_F(NeutralSafetyFixture, time_limited_uses_do_nothing)
    {
        for (const MWWorld::Ptr& ptr : loadAllKinds())
        {
            SCOPED_TRACE(ptr.getCellRef().getRefId().serializeText());
            EXPECT_NO_THROW(ptr.getClass().setRemainingUsageTime(ptr, 5.f));
            EXPECT_EQ(ptr.getClass().getRemainingUsageTime(ptr), -1.f);
        }
    }

    // Things that only an actor has keep refusing: the refusal is a runtime_error that Lua turns into a Lua error, and
    // no generic path calls these on an object that is not an actor (isActor() is false for every neutral kind).
    TEST_F(NeutralSafetyFixture, actor_only_properties_still_refuse_and_nothing_claims_to_be_an_actor)
    {
        for (const MWWorld::Ptr& ptr : loadAllKinds())
        {
            const MWWorld::Class& cls = ptr.getClass();
            SCOPED_TRACE(ptr.getCellRef().getRefId().serializeText());
            EXPECT_FALSE(cls.isActor());
            EXPECT_FALSE(cls.isNpc());
            EXPECT_FALSE(cls.isDoor());
            EXPECT_FALSE(cls.isItem(ptr));
            EXPECT_THROW(cls.getCreatureStats(ptr), std::runtime_error);
            EXPECT_THROW(cls.getNpcStats(ptr), std::runtime_error);
            EXPECT_THROW(cls.getMovementSettings(ptr), std::runtime_error);
            EXPECT_THROW(cls.getContainerStore(ptr), std::runtime_error);
            EXPECT_THROW(cls.getInventoryStore(ptr), std::runtime_error);
            EXPECT_FALSE(cls.hasInventoryStore(ptr));
        }
    }

    TEST_F(NeutralSafetyFixture, the_answers_that_were_already_safe_stay_so)
    {
        for (const MWWorld::Ptr& ptr : loadAllKinds())
        {
            const MWWorld::Class& cls = ptr.getClass();
            SCOPED_TRACE(ptr.getCellRef().getRefId().serializeText());
            EXPECT_NO_THROW(cls.getName(ptr));
            EXPECT_NO_THROW(cls.getModel(ptr));
            EXPECT_NO_THROW(cls.getCorrectedModel(ptr));
            EXPECT_NO_THROW(EXPECT_TRUE(cls.getScript(ptr).empty()));
            EXPECT_NO_THROW(cls.hasToolTip(ptr)); // which kinds have one: NeutralActivationFocus below (task 0150)
            EXPECT_NO_THROW(EXPECT_FALSE(cls.canLock(ptr)));
            EXPECT_NO_THROW(EXPECT_FALSE(cls.isEssential(ptr)));
            EXPECT_NO_THROW(EXPECT_TRUE(cls.getEnchantment(ptr).empty()));
            EXPECT_NO_THROW(EXPECT_TRUE(cls.getSound(ptr).empty()));
            EXPECT_NO_THROW(EXPECT_TRUE(cls.getPrimaryFaction(ptr).empty()));
            std::vector<std::string_view> models;
            EXPECT_NO_THROW(cls.getModelsToPreload(ptr, models));
            EXPECT_TRUE(models.empty());
        }
    }

    // Task 0150: the activate key (MWWorld::Player::activate) and the tooltip drop a focused object whose class says it
    // has no tooltip. A neutral actor, door and container are things the player acts on, so they have one; the other
    // kinds (statics, lights, items nothing can take yet, leveled lists) stay inert and silent.
    TEST_F(NeutralSafetyFixture, only_the_kinds_the_player_acts_on_have_a_tooltip_and_so_can_be_activated)
    {
        const std::vector<MWWorld::Ptr> ptrs = loadAllKinds();
        ASSERT_EQ(ptrs.size(), std::size(kAllKinds));
        for (const MWWorld::Ptr& ptr : ptrs)
        {
            const ObjectKind kind = ptr.get<MWWorld::NeutralBase>()->mBase->mKind;
            const bool expected
                = kind == ObjectKind::Actor || kind == ObjectKind::Door || kind == ObjectKind::Container;
            SCOPED_TRACE(static_cast<int>(kind));
            EXPECT_EQ(ptr.getClass().hasToolTip(ptr), expected);
            // The HUD asks for the tooltip text of whatever has one; it must not throw.
            if (expected)
            {
                EXPECT_NO_THROW(EXPECT_FALSE(ptr.getClass().getToolTipInfo(ptr, 1).caption.empty()));
            }
        }
    }
}
