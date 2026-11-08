// Task 0083 review fix (finding 2): ESMStore::load and loadESM4 read records through Content::DecoderRegistry. These
// tests pin what the loop did before the registry, on generated plugins (no game data), so that "the same as before"
// does not depend on a run over the user's own Morrowind.
#include <gtest/gtest.h>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm4/loadstat.hpp>
#include <components/esm4/reader.hpp>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "apps/components_tests/contentsource/pluginbuilder.hpp"
#include "apps/openmw/mwstate/loading.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/store.hpp"

namespace
{
    struct Loading : MWState::Loading
    {
        bool run(float) override { return false; }
    };

    // One generated TES3 plugin: a header and the records the test writes.
    class Tes3File
    {
    public:
        Tes3File()
            : mStream(std::make_unique<std::stringstream>())
        {
            mWriter.setFormatVersion(ESM::DefaultFormatVersion);
            mWriter.save(*mStream);
        }

        Tes3File& dialogue(const std::string& id)
        {
            ESM::Dialogue record;
            record.blank();
            record.mId = ESM::RefId::stringRefId(id);
            record.mStringId = id;
            mWriter.startRecord(ESM::REC_DIAL);
            record.save(mWriter, false);
            mWriter.endRecord(ESM::REC_DIAL);
            return *this;
        }

        Tes3File& info(const std::string& id)
        {
            ESM::DialInfo record;
            record.blank();
            record.mId = ESM::RefId::stringRefId(id);
            mWriter.startRecord(ESM::REC_INFO);
            record.save(mWriter, false);
            mWriter.endRecord(ESM::REC_INFO);
            return *this;
        }

        Tes3File& staticRecord(
            const std::string& id, const std::string& model, bool deleted = false, std::uint32_t flags = 0)
        {
            ESM::Static record;
            record.blank();
            record.mId = ESM::RefId::stringRefId(id);
            record.mModel = model;
            mWriter.startRecord(ESM::REC_STAT, flags);
            record.save(mWriter, deleted);
            mWriter.endRecord(ESM::REC_STAT);
            return *this;
        }

        Tes3File& magicEffect()
        {
            ESM::MagicEffect record;
            record.blank();
            record.mId = ESM::MagicEffect::WaterBreathing;
            mWriter.startRecord(ESM::REC_MGEF);
            record.save(mWriter, false);
            mWriter.endRecord(ESM::REC_MGEF);
            return *this;
        }

        Tes3File& skill()
        {
            ESM::Skill record;
            record.blank();
            record.mId = ESM::Skill::Block;
            mWriter.startRecord(ESM::REC_SKIL);
            record.save(mWriter, false);
            mWriter.endRecord(ESM::REC_SKIL);
            return *this;
        }

        // An empty record of a type that no store and no special case claims, or that the loader skips.
        Tes3File& emptyRecord(const char (&type)[5])
        {
            const ESM::NAME name(type);
            mWriter.startRecord(name);
            mWriter.endRecord(name);
            return *this;
        }

        std::unique_ptr<std::istream> finish() { return std::move(mStream); }

    private:
        std::unique_ptr<std::stringstream> mStream;
        ESM::ESMWriter mWriter;
    };

    void load(MWWorld::ESMStore& store, ESM::Dialogue*& dialogue, int index, Tes3File& file)
    {
        Loading loading;
        ESM::ESMReader reader;
        reader.setIndex(index);
        reader.open(file.finish(), "test");
        store.load(reader, loading, dialogue);
    }

    const ESM::Dialogue* dialogueOf(const MWWorld::ESMStore& store, const std::string& id)
    {
        return store.get<ESM::Dialogue>().search(ESM::RefId::stringRefId(id));
    }

    TEST(MWWorldEsmStoreDispatch, an_info_attaches_to_the_dialogue_before_it)
    {
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File file;
        file.dialogue("greeting").info("i0").info("i1");
        load(store, dialogue, 0, file);
        store.setUp();
        const ESM::Dialogue* found = dialogueOf(store, "greeting");
        ASSERT_NE(found, nullptr);
        ASSERT_EQ(found->mInfo.size(), 2u);
        // Infos with no prev link are inserted at the front by readInfo; only membership is pinned here.
        std::vector<ESM::RefId> ids;
        for (const ESM::DialInfo& info : found->mInfo)
            ids.push_back(info.mId);
        EXPECT_NE(std::find(ids.begin(), ids.end(), ESM::RefId::stringRefId("i0")), ids.end());
        EXPECT_NE(std::find(ids.begin(), ids.end(), ESM::RefId::stringRefId("i1")), ids.end());
    }

    TEST(MWWorldEsmStoreDispatch, the_dialogue_is_carried_into_the_next_file)
    {
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File first;
        first.dialogue("greeting").info("i0");
        load(store, dialogue, 0, first);
        // The second file has INFO records and no DIAL: they go to the first file's dialogue, as EsmLoader relies on.
        Tes3File second;
        second.info("i1");
        load(store, dialogue, 1, second);
        store.setUp();
        const ESM::Dialogue* found = dialogueOf(store, "greeting");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->mInfo.size(), 2u);
    }

    TEST(MWWorldEsmStoreDispatch, another_record_between_a_dialogue_and_an_info_drops_the_info)
    {
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File file;
        file.dialogue("greeting").staticRecord("rock", "meshes/rock.nif").info("lost");
        load(store, dialogue, 0, file);
        store.setUp();
        const ESM::Dialogue* found = dialogueOf(store, "greeting");
        ASSERT_NE(found, nullptr);
        EXPECT_TRUE(found->mInfo.empty());
        EXPECT_EQ(dialogue, nullptr);
    }

    TEST(MWWorldEsmStoreDispatch, a_deleted_record_does_not_end_the_dialogue)
    {
        // The deleted path erases and moves on before the dialogue pointer is touched.
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File file;
        file.dialogue("greeting").staticRecord("rock", "meshes/rock.nif", true).info("kept");
        load(store, dialogue, 0, file);
        store.setUp();
        const ESM::Dialogue* found = dialogueOf(store, "greeting");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->mInfo.size(), 1u);
    }

    TEST(MWWorldEsmStoreDispatch, an_info_with_no_dialogue_is_skipped_and_the_load_goes_on)
    {
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File file;
        file.info("orphan").staticRecord("rock", "meshes/rock.nif");
        EXPECT_NO_THROW(load(store, dialogue, 0, file));
        store.setUp();
        EXPECT_EQ(store.get<ESM::Dialogue>().getSize(), 0);
        EXPECT_NE(store.get<ESM::Static>().search(ESM::RefId::stringRefId("rock")), nullptr);
    }

    TEST(MWWorldEsmStoreDispatch, a_later_file_deletes_and_a_still_later_one_restores_a_record)
    {
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File master;
        master.staticRecord("rock", "meshes/rock.nif");
        load(store, dialogue, 0, master);
        Tes3File deleter;
        deleter.staticRecord("rock", "meshes/rock.nif", true);
        load(store, dialogue, 1, deleter);
        EXPECT_EQ(store.get<ESM::Static>().search(ESM::RefId::stringRefId("rock")), nullptr);
        Tes3File restorer;
        restorer.staticRecord("rock", "meshes/other.nif");
        load(store, dialogue, 2, restorer);
        store.setUp();
        const ESM::Static* rock = store.get<ESM::Static>().search(ESM::RefId::stringRefId("rock"));
        ASSERT_NE(rock, nullptr);
        EXPECT_EQ(rock->mModel, "meshes/other.nif");
    }

    TEST(MWWorldEsmStoreDispatch, an_ignored_record_is_skipped_unread)
    {
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File file;
        file.staticRecord("hidden", "meshes/hidden.nif", false, ESM::FLAG_Ignored)
            .staticRecord("shown", "meshes/shown.nif");
        load(store, dialogue, 0, file);
        store.setUp();
        EXPECT_EQ(store.get<ESM::Static>().search(ESM::RefId::stringRefId("hidden")), nullptr);
        EXPECT_NE(store.get<ESM::Static>().search(ESM::RefId::stringRefId("shown")), nullptr);
    }

    TEST(MWWorldEsmStoreDispatch, magic_effects_and_skills_load_into_their_stores)
    {
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File file;
        file.magicEffect().skill();
        load(store, dialogue, 0, file);
        store.setUp();
        EXPECT_NE(store.get<ESM::MagicEffect>().search(ESM::MagicEffect::WaterBreathing), nullptr);
        EXPECT_NE(store.get<ESM::Skill>().search(ESM::Skill::Block), nullptr);
    }

    TEST(MWWorldEsmStoreDispatch, project_file_records_are_skipped)
    {
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File file;
        file.emptyRecord("FILT").emptyRecord("DBGP").staticRecord("rock", "meshes/rock.nif");
        EXPECT_NO_THROW(load(store, dialogue, 0, file));
        store.setUp();
        EXPECT_NE(store.get<ESM::Static>().search(ESM::RefId::stringRefId("rock")), nullptr);
    }

    TEST(MWWorldEsmStoreDispatch, an_unknown_record_type_is_an_error_naming_it)
    {
        MWWorld::ESMStore store;
        ESM::Dialogue* dialogue = nullptr;
        Tes3File file;
        file.emptyRecord("ZZZZ");
        try
        {
            load(store, dialogue, 0, file);
            FAIL() << "an unknown record was accepted";
        }
        catch (const std::runtime_error& e)
        {
            EXPECT_EQ(std::string(e.what()), "Unknown record: ZZZZ");
        }
    }

    // ESM4: the record's FourCC chooses the store.
    std::string tes4Plugin(const ContentTests::Bytes& records)
    {
        ContentTests::Bytes file = ContentTests::tes4Header(1.7f, 0, "a", "d", 1, {});
        file.append(records);
        return file.str();
    }

    void loadEsm4(MWWorld::ESMStore& store, const std::string& bytes)
    {
        ESM4::Reader reader(
            std::make_unique<std::istringstream>(bytes, std::ios::binary), "test.esp", nullptr, nullptr);
        reader.setModIndex(0);
        store.loadESM4(reader);
    }

    TEST(MWWorldEsmStoreDispatch, an_esm4_static_goes_to_the_static_store_and_an_unknown_type_is_skipped)
    {
        using namespace ContentTests;
        Bytes statData;
        statData.append(sub4("EDID", Bytes().zstring("StatOne"))).append(sub4("MODL", Bytes().zstring("a.nif")));
        Bytes records;
        records.append(record4("STAT", 0, 0x800, statData));
        records.append(record4("ZZZZ", 0, 0x801, Bytes().u32(1)));
        MWWorld::ESMStore store;
        EXPECT_NO_THROW(loadEsm4(store, tes4Plugin(records)));
        EXPECT_EQ(store.get<ESM4::Static>().getSize(), 1);
        EXPECT_EQ(store.get<ESM4::Light>().getSize(), 0);
        EXPECT_EQ(store.get<ESM4::Door>().getSize(), 0);
    }
}
