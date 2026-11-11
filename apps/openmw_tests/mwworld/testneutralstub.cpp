// Task 0105 (stage N3a-2): the stub player of a neutral-world session (design document 10.4 R4). Written from the task
// spec before the implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"
#include "apps/openmw/mwworld/neutralstub.hpp"

namespace
{
    // A function, not a namespace-scope object: a RefId made during static initialisation would intern the spelling
    // "Player" before any test runs, which changes how other tests in this binary print the same id.
    ESM::RefId playerIdOf()
    {
        return ESM::RefId::stringRefId("Player");
    }

    std::string writeStore(const MWWorld::ESMStore& store)
    {
        std::stringstream stream;
        ESM::ESMWriter writer;
        writer.setFormatVersion(ESM::DefaultFormatVersion);
        writer.save(stream);
        Loading::Listener listener;
        store.write(writer, listener);
        writer.close();
        return stream.str();
    }

    // A player record as a Morrowind content file would define it, so the "already there" path can be shown.
    void insertRealPlayer(MWWorld::ESMStore& store)
    {
        ESM::Race race;
        race.blank();
        race.mId = ESM::RefId::stringRefId("real race");
        store.insertStatic(race);
        ESM::Class cls;
        cls.blank();
        cls.mId = ESM::RefId::stringRefId("real class");
        store.insertStatic(cls);
        ESM::NPC npc;
        npc.blank();
        npc.mId = playerIdOf();
        npc.mName = "Real Player";
        npc.mRace = race.mId;
        npc.mClass = cls.mId;
        store.insertStatic(npc);
    }

    TEST(NeutralStubPlayer, ids_are_stable_and_distinct)
    {
        EXPECT_FALSE(MWWorld::neutralStubRaceId().empty());
        EXPECT_FALSE(MWWorld::neutralStubClassId().empty());
        EXPECT_NE(MWWorld::neutralStubRaceId(), MWWorld::neutralStubClassId());
        EXPECT_EQ(MWWorld::neutralStubRaceId(), MWWorld::neutralStubRaceId());
        EXPECT_NE(MWWorld::neutralStubRaceId(), playerIdOf());
    }

    TEST(NeutralStubPlayer, an_empty_store_gets_a_player_a_race_and_a_class)
    {
        MWWorld::ESMStore store;
        EXPECT_FALSE(store.hasNeutralStubs());
        ASSERT_TRUE(MWWorld::insertNeutralStubPlayer(store));
        EXPECT_TRUE(store.hasNeutralStubs());
        store.setUp();

        const ESM::NPC* player = store.get<ESM::NPC>().search(playerIdOf());
        ASSERT_NE(player, nullptr);
        EXPECT_EQ(player->mRace, MWWorld::neutralStubRaceId());
        EXPECT_EQ(player->mClass, MWWorld::neutralStubClassId());
        ASSERT_NE(store.get<ESM::Race>().search(MWWorld::neutralStubRaceId()), nullptr);
        ASSERT_NE(store.get<ESM::Class>().search(MWWorld::neutralStubClassId()), nullptr);
        EXPECT_EQ(store.get<ESM::NPC>().getSize(), 1u);
        EXPECT_EQ(store.get<ESM::Race>().getSize(), 1u);
        EXPECT_EQ(store.get<ESM::Class>().getSize(), 1u);
        // Static records: nothing dynamic until the engine's own movePlayerRecord.
        EXPECT_EQ(store.get<ESM::NPC>().getDynamicSize(), 0u);
        EXPECT_EQ(store.find(playerIdOf()), ESM::REC_NPC_);
    }

    TEST(NeutralStubPlayer, the_engines_player_checks_pass_on_the_stub)
    {
        MWWorld::ESMStore store;
        ASSERT_TRUE(MWWorld::insertNeutralStubPlayer(store));
        store.setUp();
        ESM::ReadersCache readers;
        EXPECT_NO_THROW(store.validateRecords(readers));
        EXPECT_NO_THROW(store.movePlayerRecord());
        EXPECT_NO_THROW(store.checkPlayer());
        // validateRecords did not replace the stub's race or class.
        const ESM::NPC* player = store.get<ESM::NPC>().find(playerIdOf());
        EXPECT_EQ(player->mRace, MWWorld::neutralStubRaceId());
        EXPECT_EQ(player->mClass, MWWorld::neutralStubClassId());
        // World::clear keeps working: clearDynamic moves the player record again.
        EXPECT_NO_THROW(store.clearDynamic());
        EXPECT_NE(store.get<ESM::NPC>().search(playerIdOf()), nullptr);
    }

    TEST(NeutralStubPlayer, a_second_call_changes_nothing)
    {
        MWWorld::ESMStore store;
        ASSERT_TRUE(MWWorld::insertNeutralStubPlayer(store));
        EXPECT_FALSE(MWWorld::insertNeutralStubPlayer(store));
        store.setUp();
        EXPECT_EQ(store.get<ESM::NPC>().getSize(), 1u);
        EXPECT_EQ(store.get<ESM::Race>().getSize(), 1u);
        EXPECT_EQ(store.get<ESM::Class>().getSize(), 1u);
    }

    TEST(NeutralStubPlayer, a_store_that_has_a_player_is_not_touched)
    {
        MWWorld::ESMStore store;
        insertRealPlayer(store);
        EXPECT_FALSE(MWWorld::insertNeutralStubPlayer(store));
        EXPECT_FALSE(store.hasNeutralStubs());
        store.setUp();
        const ESM::NPC* player = store.get<ESM::NPC>().find(playerIdOf());
        EXPECT_EQ(player->mName, "Real Player");
        EXPECT_EQ(store.get<ESM::NPC>().getSize(), 1u);
        EXPECT_EQ(store.get<ESM::Race>().getSize(), 1u);
        EXPECT_EQ(store.get<ESM::Class>().getSize(), 1u);
        EXPECT_EQ(store.get<ESM::Race>().search(MWWorld::neutralStubRaceId()), nullptr);
    }

    TEST(NeutralStubPlayer, the_stub_never_reaches_a_save)
    {
        MWWorld::ESMStore store;
        ASSERT_TRUE(MWWorld::insertNeutralStubPlayer(store));
        store.setUp();
        store.movePlayerRecord(); // what World::World does: the player becomes a dynamic record, which is what is saved
        ASSERT_EQ(store.get<ESM::NPC>().getDynamicSize(), 1u);
        EXPECT_THROW((void)writeStore(store), std::runtime_error);
    }

    TEST(NeutralStubPlayer, the_refusal_names_the_reason)
    {
        MWWorld::ESMStore store;
        ASSERT_TRUE(MWWorld::insertNeutralStubPlayer(store));
        store.setUp();
        store.movePlayerRecord();
        try
        {
            (void)writeStore(store);
            FAIL() << "writing a store with the stub player must throw";
        }
        catch (const std::runtime_error& e)
        {
            EXPECT_NE(std::string(e.what()).find("stub"), std::string::npos) << e.what();
        }
    }

    TEST(NeutralStubPlayer, a_real_player_is_still_saved_exactly_as_before)
    {
        // The control for the test above: with a real player the same call writes an NPC_ record.
        MWWorld::ESMStore store;
        insertRealPlayer(store);
        store.setUp();
        store.movePlayerRecord();
        const std::string bytes = writeStore(store);
        EXPECT_NE(bytes.find("NPC_"), std::string::npos);
        EXPECT_NE(bytes.find("Real Player"), std::string::npos);
        // And a store with no stubs writes the same bytes twice.
        EXPECT_EQ(bytes, writeStore(store));
    }

    TEST(NeutralStubPlayer, the_stub_does_not_touch_the_neutral_base_store_or_the_record_counts)
    {
        MWWorld::ESMStore store;
        ASSERT_TRUE(MWWorld::insertNeutralStubPlayer(store));
        store.setUp();
        EXPECT_EQ(store.get<MWWorld::NeutralBase>().getSize(), 0u);
        // Nothing else grew: the cell, activator and door stores stay empty.
        EXPECT_EQ(store.get<ESM::Cell>().getSize(), 0u);
        EXPECT_EQ(store.get<ESM::Activator>().getSize(), 0u);
        EXPECT_EQ(store.get<ESM::Static>().getSize(), 0u);
        EXPECT_EQ(store.get<ESM::Door>().getSize(), 0u);
    }
}
