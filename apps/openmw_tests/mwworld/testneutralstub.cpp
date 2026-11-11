// Task 0105 (stage N3a-2): the stub player of a neutral-world session (design document 10.4 R4). Written from the task
// spec before the implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include <components/esm3/defaultgmsts.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadglob.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadskil.hpp>
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

    // Task 0107 (stage N3a-2b): the globals the World reads while it is built.
    TEST(NeutralStubGlobals, an_empty_store_gets_month_as_a_static_record)
    {
        MWWorld::ESMStore store;
        EXPECT_EQ(MWWorld::insertNeutralStubGlobals(store), 1u);
        const ESM::Global* month = store.get<ESM::Global>().search(ESM::RefId::stringRefId("month"));
        ASSERT_NE(month, nullptr);
        EXPECT_EQ(month->mValue.getInteger(), 0);
    }

    TEST(NeutralStubGlobals, a_global_that_exists_is_left_alone_and_a_second_call_adds_nothing)
    {
        MWWorld::ESMStore store;
        ESM::Global real;
        real.mId = ESM::RefId::stringRefId("month");
        real.mValue = ESM::Variant(7);
        real.mRecordFlags = 0;
        store.insertStatic(real);
        EXPECT_EQ(MWWorld::insertNeutralStubGlobals(store), 0u);
        EXPECT_EQ(store.get<ESM::Global>().search(ESM::RefId::stringRefId("month"))->mValue.getInteger(), 7);

        MWWorld::ESMStore other;
        EXPECT_EQ(MWWorld::insertNeutralStubGlobals(other), 1u);
        EXPECT_EQ(MWWorld::insertNeutralStubGlobals(other), 0u);
    }

    TEST(NeutralStubGameSettings, an_empty_store_gets_morrowinds_numeric_defaults)
    {
        MWWorld::ESMStore store;
        const std::size_t added = MWWorld::insertNeutralStubGameSettings(store);
        EXPECT_EQ(added, 258u + 89u);
        const ESM::GameSetting* swim = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId("fSwimHeightScale"));
        ASSERT_NE(swim, nullptr);
        EXPECT_FLOAT_EQ(swim->mValue.getFloat(), 0.9f);
        const ESM::GameSetting* delta = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId("i1stPersonSneakDelta"));
        ASSERT_NE(delta, nullptr);
        EXPECT_EQ(delta->mValue.getInteger(), 10);
    }

    TEST(NeutralStubGameSettings, a_setting_the_content_defined_keeps_its_value_and_a_second_call_adds_nothing)
    {
        MWWorld::ESMStore store;
        ESM::GameSetting real;
        real.mId = ESM::RefId::stringRefId("fSwimHeightScale");
        real.mValue = ESM::Variant(2.5f);
        real.mRecordFlags = 0;
        store.insertStatic(real);
        EXPECT_EQ(MWWorld::insertNeutralStubGameSettings(store), 258u + 89u - 1u);
        EXPECT_FLOAT_EQ(store.get<ESM::GameSetting>().find("fSwimHeightScale")->mValue.getFloat(), 2.5f);
        EXPECT_EQ(MWWorld::insertNeutralStubGameSettings(store), 0u);
    }

    // Decision 4 of task 0109: the editor's table and the engine's stubs are ONE table now (components/esm3). This pins
    // the values the engine's stubs had when the table was a generated copy: FNV-1a over each name and the 32 bits of
    // its value, floats first then integers, hashed from the stub records the engine really inserts.
    TEST(NeutralStubGameSettings, the_stub_values_are_the_values_the_generated_copy_had)
    {
        MWWorld::ESMStore store;
        ASSERT_EQ(MWWorld::insertNeutralStubGameSettings(store), 347u);
        std::uint64_t hash = 0xcbf29ce484222325ull;
        const auto feed = [&hash](const void* data, std::size_t size) {
            const unsigned char* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 0x100000001b3ull;
            }
        };
        for (std::size_t i = 0; i < ESM::DefaultGmsts::FloatCount; ++i)
        {
            const std::string name = ESM::DefaultGmsts::Floats[i];
            const ESM::GameSetting* record = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId(name));
            ASSERT_NE(record, nullptr) << name;
            const float value = record->mValue.getFloat();
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            feed(name.data(), name.size());
            feed(&bits, sizeof(bits));
        }
        for (std::size_t i = 0; i < ESM::DefaultGmsts::IntCount; ++i)
        {
            const std::string name = ESM::DefaultGmsts::Ints[i];
            const ESM::GameSetting* record = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId(name));
            ASSERT_NE(record, nullptr) << name;
            const std::uint32_t bits = static_cast<std::uint32_t>(record->mValue.getInteger());
            feed(name.data(), name.size());
            feed(&bits, sizeof(bits));
        }
        EXPECT_EQ(hash, 0xcc1e4ff9c6814b32ull);
    }

    // --- Task 0109 (stage N3a-3): the string game settings and the skills ---------------------------------------------

    TEST(NeutralStubText, drops_the_leading_s_and_turns_underscores_into_spaces)
    {
        EXPECT_EQ(MWWorld::neutralStubGmstText("sQuick_Save"), "Quick Save");
        EXPECT_EQ(MWWorld::neutralStubGmstText("sCancel"), "Cancel");
        EXPECT_EQ(MWWorld::neutralStubGmstText("sGamma_Correction"), "Gamma Correction");
        EXPECT_EQ(MWWorld::neutralStubGmstText("fNotAString"), "fNotAString");
        EXPECT_EQ(MWWorld::neutralStubGmstText("s"), "");
        EXPECT_EQ(MWWorld::neutralStubGmstText(""), "");
    }

    TEST(NeutralStubGameSettingStrings, an_empty_store_gets_every_string_of_the_table)
    {
        MWWorld::ESMStore store;
        EXPECT_EQ(MWWorld::insertNeutralStubGameSettingStrings(store), ESM::DefaultGmsts::StringCount);
        for (std::size_t i = 0; i < ESM::DefaultGmsts::StringCount; ++i)
        {
            const ESM::GameSetting* record
                = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId(ESM::DefaultGmsts::Strings[i]));
            ASSERT_NE(record, nullptr) << ESM::DefaultGmsts::Strings[i];
            EXPECT_EQ(record->mValue.getType(), ESM::VT_String);
            EXPECT_EQ(record->mValue.getString(), MWWorld::neutralStubGmstText(ESM::DefaultGmsts::Strings[i]));
        }
        const ESM::GameSetting* cancel = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId("sCancel"));
        ASSERT_NE(cancel, nullptr);
        EXPECT_EQ(cancel->mValue.getString(), "Cancel");
    }

    TEST(NeutralStubGameSettingStrings, a_setting_the_content_defined_keeps_its_text_and_a_second_call_adds_nothing)
    {
        MWWorld::ESMStore store;
        ESM::GameSetting real;
        real.mId = ESM::RefId::stringRefId("sCancel");
        real.mValue = ESM::Variant(std::string("Abbrechen"));
        real.mRecordFlags = 0;
        store.insertStatic(real);
        EXPECT_EQ(MWWorld::insertNeutralStubGameSettingStrings(store), ESM::DefaultGmsts::StringCount - 1);
        EXPECT_EQ(store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId("sCancel"))->mValue.getString(),
            "Abbrechen");
        EXPECT_EQ(MWWorld::insertNeutralStubGameSettingStrings(store), 0u);
    }

    TEST(NeutralStubSkills, an_empty_store_gets_all_the_skills_in_index_order)
    {
        MWWorld::ESMStore store;
        EXPECT_EQ(MWWorld::insertNeutralStubSkills(store), static_cast<std::size_t>(ESM::Skill::Length));
        for (int index = 0; index < ESM::Skill::Length; ++index)
        {
            const ESM::Skill* skill = store.get<ESM::Skill>().search(ESM::Skill::indexToRefId(index));
            ASSERT_NE(skill, nullptr) << index;
            EXPECT_EQ(ESM::Skill::refIdToIndex(skill->mId), index);
        }
        EXPECT_EQ(MWWorld::insertNeutralStubSkills(store), 0u);
    }

    TEST(NeutralStubNewGameGlobals, an_empty_store_gets_the_chargen_state_and_a_second_call_adds_nothing)
    {
        MWWorld::ESMStore store;
        EXPECT_EQ(MWWorld::insertNeutralStubNewGameGlobals(store), 1u);
        const ESM::Global* record = store.get<ESM::Global>().search(ESM::RefId::stringRefId("chargenstate"));
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->mValue.getInteger(), 0);
        EXPECT_EQ(MWWorld::insertNeutralStubNewGameGlobals(store), 0u);
    }

    TEST(NeutralStubMagicEffects, an_empty_store_gets_all_the_effects_and_a_second_call_adds_nothing)
    {
        MWWorld::ESMStore store;
        EXPECT_EQ(MWWorld::insertNeutralStubMagicEffects(store), static_cast<std::size_t>(ESM::MagicEffect::Length));
        for (int index = 0; index < ESM::MagicEffect::Length; ++index)
            EXPECT_NE(store.get<ESM::MagicEffect>().search(ESM::MagicEffect::indexToRefId(index)), nullptr) << index;
        EXPECT_EQ(MWWorld::insertNeutralStubMagicEffects(store), 0u);
    }

    TEST(NeutralStubPlayer, the_stub_player_is_alive)
    {
        MWWorld::ESMStore store;
        MWWorld::insertNeutralStubPlayer(store);
        const ESM::NPC* player = store.get<ESM::NPC>().find(playerIdOf());
        EXPECT_GT(player->mNpdt.mHealth, 0);
        EXPECT_GT(player->mNpdt.mFatigue, 0);
        EXPECT_GT(player->mNpdt.mLevel, 0);
        EXPECT_GT(player->mNpdt.mAttributes[0], 0);
    }
}
