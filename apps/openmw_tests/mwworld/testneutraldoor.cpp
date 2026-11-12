// Task 0115 (neutral world, stage N4 engine half): doors and moving between cells in a neutral session. The pure
// decision, the placement lookups and the destination cell by pack id. Written from the task spec before the
// implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>
#include <components/misc/convert.hpp>

#include <components/worldmodel/store.hpp>

#include "apps/openmw/mwclass/neutral.hpp"
#include "apps/openmw/mwworld/action.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutraldoor.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"
#include "apps/openmw/mwworld/ptr.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::NeutralDestination;
    using MWWorld::NeutralDoorOutcome;

    Quatd engineQuat(const ESM::Position& p)
    {
        const vsg::dquat q = Misc::Convert::makeOsgQuat(p);
        return Quatd{ q.x, q.y, q.z, q.w };
    }

    double rotationDistance(const Quatd& a, const Quatd& b)
    {
        const double dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        return std::sqrt(std::max(0.0, 1.0 - std::min(1.0, dot * dot)));
    }

    Quatd unit(double x, double y, double z, double w)
    {
        const double n = std::sqrt(x * x + y * y + z * z + w * w);
        return Quatd{ x / n, y / n, z / n, w / n };
    }

    WorldObject doorObject(WorldId id, WorldId destinationCell)
    {
        WorldObject o;
        o.id = id;
        o.base = 500;
        o.kind = ObjectKind::Door;
        o.baseEditorId = "Door";
        TeleportComponent t;
        t.destinationCell = destinationCell;
        t.destinationDoor = id + 1;
        t.destination.position = { 100.5, -200.25, 300.0 };
        t.destination.rotation = unit(0.1, 0.2, 0.3, 0.9);
        o.teleport = t;
        return o;
    }

    NeutralDestination alwaysInterior(WorldId)
    {
        return NeutralDestination::Interior;
    }

    // ---- the pure decision ------------------------------------------------------------------------------------

    TEST(NeutralDoorDecision, a_teleport_door_to_an_interior_teleports_to_that_cell_at_the_arrival)
    {
        const WorldObject door = doorObject(7, 42);
        const auto d = MWWorld::decideNeutralDoor(door, true, false, alwaysInterior);
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Teleport);
        EXPECT_EQ(d.mDestinationCell, 42u);
        EXPECT_FLOAT_EQ(d.mArrival.pos[0], 100.5f);
        EXPECT_FLOAT_EQ(d.mArrival.pos[1], -200.25f);
        EXPECT_FLOAT_EQ(d.mArrival.pos[2], 300.0f);
    }

    TEST(NeutralDoorDecision, the_destination_is_the_teleports_cell_not_the_doors_own)
    {
        WorldObject door = doorObject(7, 42);
        const auto d = MWWorld::decideNeutralDoor(door, true, false, alwaysInterior);
        EXPECT_NE(d.mDestinationCell, 7u);
        EXPECT_NE(d.mDestinationCell, door.base);
    }

    TEST(NeutralDoorDecision, the_classifier_is_asked_about_the_destination_cell_once)
    {
        WorldId asked = 0;
        int calls = 0;
        const WorldObject door = doorObject(7, 42);
        (void)MWWorld::decideNeutralDoor(door, true, false, [&](WorldId c) {
            asked = c;
            ++calls;
            return NeutralDestination::Interior;
        });
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(asked, 42u);
    }

    TEST(NeutralDoorDecision, an_exterior_destination_is_refused)
    {
        const WorldObject door = doorObject(7, 42);
        const auto d
            = MWWorld::decideNeutralDoor(door, true, false, [](WorldId) { return NeutralDestination::Exterior; });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Exterior);
        EXPECT_EQ(d.mDestinationCell, kNoWorldId);
    }

    TEST(NeutralDoorDecision, an_unavailable_destination_is_refused_as_nowhere)
    {
        const WorldObject door = doorObject(7, 42);
        const auto d
            = MWWorld::decideNeutralDoor(door, true, false, [](WorldId) { return NeutralDestination::Unavailable; });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Nowhere);
    }

    TEST(NeutralDoorDecision, a_teleport_with_no_destination_cell_leads_nowhere_and_asks_nothing)
    {
        const WorldObject door = doorObject(7, kNoWorldId);
        int calls = 0;
        const auto d = MWWorld::decideNeutralDoor(door, true, false, [&](WorldId) {
            ++calls;
            return NeutralDestination::Interior;
        });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Nowhere);
        EXPECT_EQ(calls, 0);
    }

    TEST(NeutralDoorDecision, a_locked_door_is_refused_before_anything_else_is_asked)
    {
        WorldObject door = doorObject(7, 42);
        door.lock = LockComponent{ true, 25, 99 };
        int calls = 0;
        const auto d = MWWorld::decideNeutralDoor(door, true, false, [&](WorldId) {
            ++calls;
            return NeutralDestination::Interior;
        });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Locked);
        EXPECT_EQ(calls, 0);
    }

    TEST(NeutralDoorDecision, a_lock_that_is_not_locked_does_not_refuse)
    {
        WorldObject door = doorObject(7, 42);
        door.lock = LockComponent{ false, 0, kNoWorldId };
        EXPECT_EQ(MWWorld::decideNeutralDoor(door, true, false, alwaysInterior).mOutcome, NeutralDoorOutcome::Teleport);
    }

    TEST(NeutralDoorDecision, the_unlock_aid_opens_a_locked_door)
    {
        WorldObject door = doorObject(7, 42);
        door.lock = LockComponent{ true, 25, 99 };
        const auto d = MWWorld::decideNeutralDoor(door, true, true, alwaysInterior);
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Teleport);
        EXPECT_EQ(d.mDestinationCell, 42u);
    }

    TEST(NeutralDoorDecision, a_door_with_no_teleport_is_inert)
    {
        WorldObject door = doorObject(7, 42);
        door.teleport.reset();
        EXPECT_EQ(MWWorld::decideNeutralDoor(door, true, false, alwaysInterior).mOutcome, NeutralDoorOutcome::Inert);
    }

    TEST(NeutralDoorDecision, a_locked_door_with_no_teleport_is_still_locked)
    {
        WorldObject door = doorObject(7, 42);
        door.teleport.reset();
        door.lock = LockComponent{ true, 0, kNoWorldId };
        EXPECT_EQ(MWWorld::decideNeutralDoor(door, true, false, alwaysInterior).mOutcome, NeutralDoorOutcome::Locked);
    }

    TEST(NeutralDoorDecision, every_kind_but_a_door_is_not_a_door_even_with_a_teleport)
    {
        for (ObjectKind kind : { ObjectKind::Static, ObjectKind::Activator, ObjectKind::Container, ObjectKind::Light,
                 ObjectKind::Item, ObjectKind::Actor, ObjectKind::Other, ObjectKind::Unknown })
        {
            WorldObject o = doorObject(7, 42);
            o.kind = kind;
            for (bool player : { true, false })
                EXPECT_EQ(MWWorld::decideNeutralDoor(o, player, true, alwaysInterior).mOutcome,
                    NeutralDoorOutcome::NotADoor);
        }
    }

    TEST(NeutralDoorDecision, somebody_other_than_the_player_is_refused_without_asking)
    {
        const WorldObject door = doorObject(7, 42);
        int calls = 0;
        const auto d = MWWorld::decideNeutralDoor(door, false, true, [&](WorldId) {
            ++calls;
            return NeutralDestination::Interior;
        });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::NotAPlayer);
        EXPECT_EQ(calls, 0);
    }

    TEST(NeutralDoorDecision, only_refusals_with_something_to_say_have_a_message_key)
    {
        EXPECT_FALSE(MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::Locked).empty());
        EXPECT_FALSE(MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::Exterior).empty());
        EXPECT_FALSE(MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::Nowhere).empty());
        EXPECT_NE(MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::Locked),
            MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::Exterior));
        EXPECT_NE(MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::Nowhere),
            MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::Exterior));
        EXPECT_TRUE(MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::Teleport).empty());
        EXPECT_TRUE(MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::Inert).empty());
        EXPECT_TRUE(MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::NotADoor).empty());
        EXPECT_TRUE(MWWorld::neutralDoorMessageKey(NeutralDoorOutcome::NotAPlayer).empty());
    }

    // ---- the arrival and the placement lookups -------------------------------------------------------------------

    TEST(NeutralDoorArrival, random_rotations_come_back_through_the_engines_quaternion)
    {
        std::mt19937_64 rng(12345);
        std::normal_distribution<double> n(0.0, 1.0);
        for (int i = 0; i < 2000; ++i)
        {
            TeleportComponent t;
            t.destination.rotation = unit(n(rng), n(rng), n(rng), n(rng));
            const ESM::Position p = MWWorld::neutralArrivalPosition(t);
            EXPECT_LT(rotationDistance(engineQuat(p), t.destination.rotation), 1e-5) << i;
        }
    }

    TEST(NeutralDoorArrival, gimbal_lock_poses_come_back_too)
    {
        const double s = std::sqrt(0.5);
        for (const Quatd& q : { unit(0, s, 0, s), unit(0, -s, 0, s), unit(s, 0, 0, s), unit(0, 0, s, s),
                 unit(0.5, 0.5, 0.5, 0.5) })
        {
            TeleportComponent t;
            t.destination.rotation = q;
            EXPECT_LT(rotationDistance(engineQuat(MWWorld::neutralArrivalPosition(t)), q), 1e-5);
        }
    }

    TEST(NeutralDoorArrival, the_position_is_not_converted)
    {
        TeleportComponent t;
        t.destination.position = { -1.0, 2.0, 3.5 };
        const ESM::Position p = MWWorld::neutralArrivalPosition(t);
        EXPECT_FLOAT_EQ(p.pos[0], -1.0f);
        EXPECT_FLOAT_EQ(p.pos[1], 2.0f);
        EXPECT_FLOAT_EQ(p.pos[2], 3.5f);
    }

    TEST(NeutralPlacementOf, is_the_exact_inverse_of_neutral_ref_num)
    {
        std::mt19937_64 rng(99);
        std::vector<WorldId> ids = { 1, 2, 0xffffffffu, 0x80000000u, 0x7fffffffu };
        for (int i = 0; i < 1000; ++i)
            ids.push_back(rng() & 0xffffffffu);
        for (WorldId id : ids)
        {
            ESM::RefNum ref;
            if (id == kNoWorldId || !MWWorld::neutralRefNum(id, ref))
                continue;
            const auto back = MWWorld::neutralPlacementOf(ref);
            ASSERT_TRUE(back.has_value()) << id;
            EXPECT_EQ(*back, id);
        }
    }

    TEST(NeutralPlacementOf, refuses_any_other_reference_number)
    {
        ESM::RefNum ref{ .mIndex = 5, .mContentFile = 0 };
        EXPECT_FALSE(MWWorld::neutralPlacementOf(ref).has_value());
        ref.mContentFile = -1;
        EXPECT_FALSE(MWWorld::neutralPlacementOf(ref).has_value());
        ref.mContentFile = MWWorld::kNeutralRefNumFile + 1;
        EXPECT_FALSE(MWWorld::neutralPlacementOf(ref).has_value());
    }

    // ---- CellStore::neutralObject and the destination cell by pack id ---------------------------------------------

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

    // A source with scripted cells: 10 and 11 are interiors with a door each, 12 is an exterior, 13 fails to build.
    class DoorSource final : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "fakegame"; }
        std::size_t cellCount() const override { return 4; }
        std::size_t listCells(std::size_t first, std::size_t max, std::vector<WorldId>& out) const override
        {
            std::size_t n = 0;
            for (WorldId id : { 10u, 11u, 12u, 13u })
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
            if (id == 10 || id == 11)
            {
                out.editorId = id == 10 ? "InnA" : "InnB";
                out.interior = true;
                out.objects.push_back(plain(id * 100 + 1, 900, ObjectKind::Static));
                WorldObject door = plain(id * 100 + 2, 901, ObjectKind::Door);
                TeleportComponent t;
                t.destinationCell = id == 10 ? 11 : 10;
                door.teleport = t;
                out.objects.push_back(door);
                return true;
            }
            if (id == 12)
            {
                out.editorId = "";
                out.interior = false;
                return true;
            }
            error = "scripted failure";
            return false;
        }
        WorldId findCellByEditorId(std::string_view name) const override
        {
            if (name == "InnA")
                return 10;
            if (name == "InnB")
                return 11;
            return kNoWorldId;
        }
        int mBuilds = 0;
    };

    struct DoorWorldFixture : testing::Test
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
        DoorSource mSource;
        NeutralWorld::NeutralWorldStore mNeutral;
        std::unique_ptr<MWWorld::WorldModel> mModel;
    };

    TEST_F(DoorWorldFixture, a_cell_is_found_by_pack_id_and_is_the_same_store_as_by_editor_id)
    {
        const auto byId = mModel->findNeutralCell(mNeutral, "fakegame", 10);
        ASSERT_EQ(byId.mStatus, MWWorld::NeutralCellStatus::Found) << byId.mError;
        ASSERT_NE(byId.mStore, nullptr);
        EXPECT_TRUE(byId.mStore->isNeutral());
        EXPECT_FALSE(byId.mReused);
        // The by-name lookup the scene does after the teleport finds the very same store.
        EXPECT_EQ(mModel->findInterior("InnA", false), byId.mStore);
        EXPECT_EQ(&mModel->getCell(byId.mStore->getCell()->getId()), byId.mStore);
    }

    TEST_F(DoorWorldFixture, a_second_request_reuses_the_store_and_builds_nothing)
    {
        const auto first = mModel->findNeutralCell(mNeutral, "fakegame", 10);
        ASSERT_EQ(first.mStatus, MWWorld::NeutralCellStatus::Found);
        const std::size_t bases = mStore.get<MWWorld::NeutralBase>().getSize();
        const auto second = mModel->findNeutralCell(mNeutral, "fakegame", 10);
        ASSERT_EQ(second.mStatus, MWWorld::NeutralCellStatus::Found);
        EXPECT_EQ(second.mStore, first.mStore);
        EXPECT_TRUE(second.mReused);
        EXPECT_EQ(mStore.get<MWWorld::NeutralBase>().getSize(), bases);
    }

    TEST_F(DoorWorldFixture, an_exterior_is_refused_and_nothing_is_added)
    {
        const auto r = mModel->findNeutralCell(mNeutral, "fakegame", 12);
        EXPECT_EQ(r.mStatus, MWWorld::NeutralCellStatus::Exterior);
        EXPECT_EQ(r.mStore, nullptr);
        EXPECT_EQ(mStore.get<MWWorld::NeutralBase>().getSize(), 0u);
    }

    TEST_F(DoorWorldFixture, a_failing_source_and_an_unknown_id_and_an_unknown_game_fail_with_a_reason)
    {
        const auto failing = mModel->findNeutralCell(mNeutral, "fakegame", 13);
        EXPECT_EQ(failing.mStatus, MWWorld::NeutralCellStatus::Failed);
        EXPECT_EQ(failing.mStore, nullptr);
        EXPECT_FALSE(failing.mError.empty());
        const auto unknown = mModel->findNeutralCell(mNeutral, "fakegame", 999);
        EXPECT_EQ(unknown.mStatus, MWWorld::NeutralCellStatus::Failed);
        EXPECT_FALSE(unknown.mError.empty());
        const auto game = mModel->findNeutralCell(mNeutral, "othergame", 10);
        EXPECT_EQ(game.mStatus, MWWorld::NeutralCellStatus::Failed);
        EXPECT_FALSE(game.mError.empty());
        const auto none = mModel->findNeutralCell(mNeutral, "fakegame", kNoWorldId);
        EXPECT_EQ(none.mStatus, MWWorld::NeutralCellStatus::Failed);
    }

    TEST_F(DoorWorldFixture, the_door_of_a_built_cell_is_found_by_its_placement_and_leads_back)
    {
        const auto a = mModel->findNeutralCell(mNeutral, "fakegame", 10);
        ASSERT_EQ(a.mStatus, MWWorld::NeutralCellStatus::Found);
        a.mStore->load();
        const WorldObject* door = a.mStore->neutralObject(1002);
        ASSERT_NE(door, nullptr);
        EXPECT_EQ(door->kind, ObjectKind::Door);
        ASSERT_TRUE(door->teleport.has_value());
        EXPECT_EQ(a.mStore->neutralGameId(), "fakegame");
        const auto b = mModel->findNeutralCell(mNeutral, "fakegame", door->teleport->destinationCell);
        ASSERT_EQ(b.mStatus, MWWorld::NeutralCellStatus::Found);
        EXPECT_NE(b.mStore, a.mStore);
        const WorldObject* back = b.mStore->neutralObject(1102);
        ASSERT_NE(back, nullptr);
        EXPECT_EQ(back->teleport->destinationCell, 10u);
        const auto again = mModel->findNeutralCell(mNeutral, "fakegame", back->teleport->destinationCell);
        EXPECT_EQ(again.mStore, a.mStore);
    }

    TEST_F(DoorWorldFixture, neutral_object_finds_each_placement_and_none_for_an_unknown_one)
    {
        const auto a = mModel->findNeutralCell(mNeutral, "fakegame", 10);
        ASSERT_EQ(a.mStatus, MWWorld::NeutralCellStatus::Found);
        for (WorldId id : { 1001u, 1002u })
        {
            const WorldObject* o = a.mStore->neutralObject(id);
            ASSERT_NE(o, nullptr) << id;
            EXPECT_EQ(o->id, id);
        }
        EXPECT_EQ(a.mStore->neutralObject(1003), nullptr);
        EXPECT_EQ(a.mStore->neutralObject(0), nullptr);
        EXPECT_EQ(a.mStore->neutralObject(5000), nullptr);
    }

    TEST_F(DoorWorldFixture, a_cell_that_is_not_neutral_has_no_placements)
    {
        ESM::Cell record;
        record.mId = ESM::RefId::stringRefId("plain");
        record.mName = "Plain";
        record.mData.mFlags = ESM::Cell::Interior;
        MWWorld::CellStore store{ MWWorld::Cell(record), mStore, mReaders };
        EXPECT_EQ(store.neutralObject(1), nullptr);
    }

    TEST_F(DoorWorldFixture, an_activation_of_a_kind_that_is_not_a_door_is_the_null_action)
    {
        const auto a = mModel->findNeutralCell(mNeutral, "fakegame", 10);
        ASSERT_EQ(a.mStatus, MWWorld::NeutralCellStatus::Found);
        a.mStore->load();
        MWWorld::Ptr staticPtr;
        a.mStore->forEach(
            [&](const MWWorld::Ptr& ptr) {
                if (ptr.getCellRef().getRefNum().mIndex == 1001)
                    staticPtr = ptr;
                return true;
            },
            true);
        ASSERT_FALSE(staticPtr.isEmpty());
        const auto action = staticPtr.getClass().activate(staticPtr, staticPtr);
        ASSERT_NE(action, nullptr);
        EXPECT_TRUE(action->isNullAction());
    }
}
