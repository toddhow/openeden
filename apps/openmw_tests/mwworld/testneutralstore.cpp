// Task 0105 (stage N3a-2 of the neutral world model): the base records of the neutral world, Store<NeutralBase> and the
// RefId scheme. Written from the task spec before the implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include <components/esm/refid.hpp>

#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::NeutralBase;
    using MWWorld::Store;
    using Status = Store<NeutralBase>::Status;

    WorldObject makeObject(WorldId placement, WorldId base, std::string model = "architecture\\whiterun\\wrfloor.nif")
    {
        WorldObject o;
        o.id = placement;
        o.base = base;
        o.kind = ObjectKind::Static;
        o.baseEditorId = "WRFloor01";
        o.modelPath = std::move(model);
        o.collision.source = CollisionSource::FromModel;
        o.collision.hasBounds = true;
        o.collision.boundsMin = { -1.0, -2.0, -3.0 };
        o.collision.boundsMax = { 1.0, 2.0, 3.0 };
        o.transform.position = { 10.0, 20.0, 30.0 };
        o.payload.handle = 0x1234;
        return o;
    }

    TEST(NeutralBaseId, a_placement_with_a_base_is_keyed_by_the_base)
    {
        std::string error;
        const ESM::RefId id = MWWorld::neutralBaseId("skyrimse", makeObject(7, 42), error);
        EXPECT_EQ(id, ESM::RefId::stringRefId("skyrimse:b42")) << error;
        EXPECT_TRUE(error.empty());
    }

    TEST(NeutralBaseId, a_placement_without_a_base_is_keyed_by_itself)
    {
        std::string error;
        const ESM::RefId id = MWWorld::neutralBaseId("skyrimse", makeObject(7, kNoWorldId), error);
        EXPECT_EQ(id, ESM::RefId::stringRefId("skyrimse:o7")) << error;
    }

    TEST(NeutralBaseId, the_id_does_not_depend_on_the_placement_when_there_is_a_base)
    {
        std::string error;
        EXPECT_EQ(MWWorld::neutralBaseId("skyrimse", makeObject(1, 42), error),
            MWWorld::neutralBaseId("skyrimse", makeObject(999, 42), error));
    }

    TEST(NeutralBaseId, games_and_kinds_of_key_never_collide)
    {
        std::string error;
        const ESM::RefId a = MWWorld::neutralBaseId("skyrimse", makeObject(5, 5), error);
        const ESM::RefId b = MWWorld::neutralBaseId("morrowind", makeObject(5, 5), error);
        const ESM::RefId c = MWWorld::neutralBaseId("skyrimse", makeObject(5, kNoWorldId), error);
        EXPECT_NE(a, b);
        EXPECT_NE(a, c);
        EXPECT_NE(b, c);
        EXPECT_FALSE(a.empty());
        EXPECT_FALSE(b.empty());
        EXPECT_FALSE(c.empty());
    }

    TEST(NeutralBaseId, a_bad_game_id_or_an_object_with_no_identity_is_refused_with_a_message)
    {
        for (const std::string& game :
            { std::string(), std::string("a:b"), std::string("tab\there"), std::string("nl\n") })
        {
            std::string error;
            EXPECT_TRUE(MWWorld::neutralBaseId(game, makeObject(1, 2), error).empty()) << game;
            EXPECT_FALSE(error.empty()) << game;
        }
        std::string error;
        EXPECT_TRUE(MWWorld::neutralBaseId("skyrimse", makeObject(kNoWorldId, kNoWorldId), error).empty());
        EXPECT_FALSE(error.empty());
    }

    TEST(NeutralBaseStore, a_new_object_makes_a_record_with_its_facts)
    {
        Store<NeutralBase> store;
        const WorldObject o = makeObject(7, 42);
        const auto r = store.addFromObject("skyrimse", o);
        ASSERT_EQ(r.mStatus, Status::Added) << r.mError;
        ASSERT_NE(r.mBase, nullptr);
        EXPECT_EQ(r.mBase->mId, ESM::RefId::stringRefId("skyrimse:b42"));
        EXPECT_EQ(r.mBase->mGameId, "skyrimse");
        EXPECT_EQ(r.mBase->mBase, 42u);
        EXPECT_EQ(r.mBase->mPlacement, kNoWorldId);
        EXPECT_EQ(r.mBase->mKind, ObjectKind::Static);
        EXPECT_EQ(r.mBase->mEditorId, "WRFloor01");
        EXPECT_EQ(r.mBase->mModel, o.modelPath);
        EXPECT_EQ(r.mBase->mCollision, o.collision);
        EXPECT_EQ(r.mBase->mPayload, o.payload);
        EXPECT_EQ(store.getSize(), 1u);
        EXPECT_EQ(store.search(r.mBase->mId), r.mBase);
        EXPECT_EQ(store.find(r.mBase->mId), r.mBase);
    }

    TEST(NeutralBaseStore, a_placement_without_a_base_records_the_placement)
    {
        Store<NeutralBase> store;
        const auto r = store.addFromObject("skyrimse", makeObject(7, kNoWorldId));
        ASSERT_EQ(r.mStatus, Status::Added) << r.mError;
        EXPECT_EQ(r.mBase->mBase, kNoWorldId);
        EXPECT_EQ(r.mBase->mPlacement, 7u);
    }

    TEST(NeutralBaseStore, many_placements_of_one_base_share_one_record_and_the_transform_is_not_a_base_fact)
    {
        Store<NeutralBase> store;
        WorldObject a = makeObject(1, 42);
        WorldObject b = makeObject(2, 42);
        b.transform.position = { -5.0, 6.0, 7.0 };
        b.transform.scale = 2.0;
        b.initiallyDisabled = true;
        b.persistent = true;
        const auto ra = store.addFromObject("skyrimse", a);
        const auto rb = store.addFromObject("skyrimse", b);
        EXPECT_EQ(ra.mStatus, Status::Added);
        EXPECT_EQ(rb.mStatus, Status::Existing);
        EXPECT_EQ(ra.mBase, rb.mBase);
        EXPECT_EQ(store.getSize(), 1u);
        EXPECT_EQ(store.conflicts(), 0u);
    }

    TEST(NeutralBaseStore, one_base_with_other_facts_is_a_conflict_that_keeps_the_first)
    {
        Store<NeutralBase> store;
        const auto first = store.addFromObject("skyrimse", makeObject(1, 42, "a.nif"));
        const auto second = store.addFromObject("skyrimse", makeObject(2, 42, "b.nif"));
        ASSERT_EQ(first.mStatus, Status::Added);
        EXPECT_EQ(second.mStatus, Status::Conflict);
        EXPECT_EQ(second.mBase, first.mBase);
        EXPECT_EQ(second.mBase->mModel, "a.nif");
        EXPECT_FALSE(second.mError.empty());
        EXPECT_NE(second.mError.find("skyrimse"), std::string::npos);
        EXPECT_NE(second.mError.find("42"), std::string::npos);
        EXPECT_EQ(store.conflicts(), 1u);
        EXPECT_EQ(store.getSize(), 1u);
    }

    TEST(NeutralBaseStore, each_fact_of_a_base_matters_for_equality)
    {
        // kind, editor id, model, collision and payload each make a conflict when they differ.
        const auto mutate = [](auto change) {
            Store<NeutralBase> store;
            (void)store.addFromObject("skyrimse", makeObject(1, 42));
            WorldObject o = makeObject(2, 42);
            change(o);
            return store.addFromObject("skyrimse", o).mStatus;
        };
        EXPECT_EQ(mutate([](WorldObject& o) { o.kind = ObjectKind::Door; }), Status::Conflict);
        EXPECT_EQ(mutate([](WorldObject& o) { o.baseEditorId = "Other"; }), Status::Conflict);
        EXPECT_EQ(mutate([](WorldObject& o) { o.modelPath = "x.nif"; }), Status::Conflict);
        EXPECT_EQ(mutate([](WorldObject& o) { o.collision.source = CollisionSource::Box; }), Status::Conflict);
        EXPECT_EQ(mutate([](WorldObject& o) { o.collision.boundsMax.z = 9.0; }), Status::Conflict);
        EXPECT_EQ(mutate([](WorldObject& o) { o.payload.handle = 99; }), Status::Conflict);
        EXPECT_EQ(mutate([](WorldObject&) {}), Status::Existing);
    }

    TEST(NeutralBaseStore, placements_with_no_base_are_separate_records)
    {
        Store<NeutralBase> store;
        EXPECT_EQ(store.addFromObject("skyrimse", makeObject(1, kNoWorldId, "a.nif")).mStatus, Status::Added);
        EXPECT_EQ(store.addFromObject("skyrimse", makeObject(2, kNoWorldId, "b.nif")).mStatus, Status::Added);
        EXPECT_EQ(store.getSize(), 2u);
    }

    TEST(NeutralBaseStore, the_same_ids_of_two_games_are_two_records)
    {
        Store<NeutralBase> store;
        EXPECT_EQ(store.addFromObject("skyrimse", makeObject(1, 42)).mStatus, Status::Added);
        EXPECT_EQ(store.addFromObject("morrowind", makeObject(1, 42)).mStatus, Status::Added);
        EXPECT_EQ(store.getSize(), 2u);
    }

    TEST(NeutralBaseStore, an_unusable_object_is_refused_and_changes_nothing)
    {
        Store<NeutralBase> store;
        const auto bad = store.addFromObject("a:b", makeObject(1, 2));
        EXPECT_EQ(bad.mStatus, Status::Refused);
        EXPECT_EQ(bad.mBase, nullptr);
        EXPECT_FALSE(bad.mError.empty());
        EXPECT_EQ(store.addFromObject("skyrimse", makeObject(kNoWorldId, kNoWorldId)).mStatus, Status::Refused);
        EXPECT_EQ(store.getSize(), 0u);
    }

    TEST(NeutralBaseStore, search_and_find_of_an_unknown_id)
    {
        Store<NeutralBase> store;
        const ESM::RefId id = ESM::RefId::stringRefId("skyrimse:b1");
        EXPECT_EQ(store.search(id), nullptr);
        try
        {
            (void)store.find(id);
            FAIL() << "find of an unknown id must throw";
        }
        catch (const std::runtime_error& e)
        {
            EXPECT_NE(std::string(e.what()).find("skyrimse:b1"), std::string::npos) << e.what();
        }
    }

    TEST(NeutralBaseStore, the_cap_refuses_new_records_but_still_answers_known_ones)
    {
        Store<NeutralBase> store(2);
        EXPECT_EQ(store.addFromObject("g", makeObject(1, 1)).mStatus, Status::Added);
        EXPECT_EQ(store.addFromObject("g", makeObject(2, 2)).mStatus, Status::Added);
        const auto over = store.addFromObject("g", makeObject(3, 3));
        EXPECT_EQ(over.mStatus, Status::Refused);
        EXPECT_FALSE(over.mError.empty());
        EXPECT_EQ(store.getSize(), 2u);
        EXPECT_EQ(store.addFromObject("g", makeObject(4, 1)).mStatus, Status::Existing);
        EXPECT_EQ(store.search(ESM::RefId::stringRefId("g:b3")), nullptr);
    }

    TEST(NeutralBaseStore, records_are_listed_in_the_order_they_were_added)
    {
        Store<NeutralBase> store;
        for (WorldId base : { 9u, 3u, 7u, 3u, 1u })
            (void)store.addFromObject("g", makeObject(100 + base, base));
        std::vector<WorldId> seen;
        for (const NeutralBase* b : store.all())
            seen.push_back(b->mBase);
        EXPECT_EQ(seen, (std::vector<WorldId>{ 9, 3, 7, 1 }));
    }

    TEST(NeutralBaseStore, pointers_stay_valid_while_more_records_are_added)
    {
        Store<NeutralBase> store;
        const NeutralBase* first = store.addFromObject("g", makeObject(1, 1)).mBase;
        ASSERT_NE(first, nullptr);
        for (WorldId i = 2; i < 3000; ++i)
            (void)store.addFromObject("g", makeObject(i, i));
        EXPECT_EQ(store.search(ESM::RefId::stringRefId("g:b1")), first);
        EXPECT_EQ(first->mBase, 1u);
        EXPECT_EQ(store.all().front(), first);
    }

    TEST(NeutralBaseStore, removing_a_game_drops_only_its_records_and_keeps_the_order)
    {
        Store<NeutralBase> store;
        (void)store.addFromObject("a", makeObject(1, 1));
        (void)store.addFromObject("b", makeObject(1, 1));
        (void)store.addFromObject("a", makeObject(2, 2));
        const NeutralBase* keep = store.addFromObject("b", makeObject(2, 2)).mBase;
        store.removeGame("a");
        EXPECT_EQ(store.getSize(), 2u);
        EXPECT_EQ(store.search(ESM::RefId::stringRefId("a:b1")), nullptr);
        EXPECT_EQ(store.search(ESM::RefId::stringRefId("a:b2")), nullptr);
        ASSERT_NE(store.search(ESM::RefId::stringRefId("b:b2")), nullptr);
        EXPECT_EQ(store.search(ESM::RefId::stringRefId("b:b2")), keep);
        ASSERT_EQ(store.all().size(), 2u);
        EXPECT_EQ(store.all()[0]->mId, ESM::RefId::stringRefId("b:b1"));
        EXPECT_EQ(store.all()[1]->mId, ESM::RefId::stringRefId("b:b2"));
        // Removing an unknown game does nothing; the game can be added again afterwards.
        store.removeGame("zzz");
        EXPECT_EQ(store.getSize(), 2u);
        EXPECT_EQ(store.addFromObject("a", makeObject(1, 1)).mStatus, Status::Added);
    }

    TEST(NeutralBaseStore, clear_empties_the_store)
    {
        Store<NeutralBase> store;
        (void)store.addFromObject("a", makeObject(1, 1));
        store.clear();
        EXPECT_EQ(store.getSize(), 0u);
        EXPECT_TRUE(store.all().empty());
        EXPECT_EQ(store.search(ESM::RefId::stringRefId("a:b1")), nullptr);
    }

    // --- Inside ESMStore: Morrowind's stores are not changed by the new one ---

    template <class Tuple>
    struct CountDynamic;

    template <class... S>
    struct CountDynamic<std::tuple<S...>>
    {
        static constexpr std::size_t value = (std::size_t(std::is_convertible_v<S*, MWWorld::DynamicStore*>) + ...);
    };

    TEST(NeutralBaseInEsmStore, the_store_is_in_the_tuple_but_is_not_a_dynamic_store)
    {
        static_assert(!std::is_convertible_v<Store<NeutralBase>*, MWWorld::DynamicStore*>);
        static_assert(std::is_base_of_v<MWWorld::StoreBase, Store<NeutralBase>>);
        MWWorld::ESMStore store;
        EXPECT_EQ(store.get<NeutralBase>().getSize(), 0u);

        // What ESMStore::begin() iterates is exactly the dynamic stores of the tuple: the new store adds none.
        std::size_t iterated = 0;
        for (const MWWorld::DynamicStore* s : store)
        {
            (void)s;
            ++iterated;
        }
        EXPECT_EQ(iterated, CountDynamic<MWWorld::ESMStore::StoreTuple>::value);
    }

    TEST(NeutralBaseInEsmStore, the_writable_store_keeps_what_is_added)
    {
        MWWorld::ESMStore store;
        const auto r = store.getWritable<NeutralBase>().addFromObject("skyrimse", makeObject(1, 42));
        ASSERT_EQ(r.mStatus, Status::Added);
        EXPECT_EQ(store.get<NeutralBase>().search(ESM::RefId::stringRefId("skyrimse:b42")), r.mBase);
        // The ESM id index and the saved-record count know nothing of it.
        EXPECT_EQ(store.find(ESM::RefId::stringRefId("skyrimse:b42")), 0);
        const std::size_t before = store.countSavedGameRecords();
        EXPECT_EQ(before, 1u);
    }
}
