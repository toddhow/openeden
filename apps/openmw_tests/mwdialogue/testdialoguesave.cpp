// Task 0085, move 3, stage 3b: the save records of the journal and the dialogue manager (ESM::QuestState,
// ESM::JournalEntry, ESM::DialogueState; REC_QUES, REC_JOUR, REC_DIAS). The move must keep the bytes of a save
// identical, so these tests pin (1) what the REAL classes write for a state, against a stream built a second way with
// the ESM structs and the writer directly, (2) a hex golden of one small state, taken from the code before the move,
// (3) that reading and writing again gives the same bytes, and (4) what a load keeps and drops.
#include "dialoguefixture.hpp"

#include <components/esm3/dialoguestate.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/journalentry.hpp>
#include <components/esm3/queststate.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include <cstdio>
#include <cstring>

namespace DialogueTest
{
    namespace
    {
        std::string toHex(const std::string& bytes)
        {
            std::string out;
            char buffer[4];
            for (unsigned char c : bytes)
            {
                std::snprintf(buffer, sizeof(buffer), "%02x", c);
                out += buffer;
            }
            return out;
        }

        std::string fromHex(const std::string& hex)
        {
            std::string out;
            for (size_t i = 0; i + 1 < hex.size(); i += 2)
                out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
            return out;
        }

        /// The names of the records in a save stream, in order (the file header is "TES3").
        std::vector<std::string> recordNames(const std::string& bytes)
        {
            std::vector<std::string> names;
            size_t pos = 0;
            while (pos + 16 <= bytes.size())
            {
                names.emplace_back(bytes.substr(pos, 4));
                std::uint32_t size = 0;
                std::memcpy(&size, bytes.data() + pos + 4, 4);
                pos += 16 + size;
            }
            EXPECT_EQ(pos, bytes.size());
            return names;
        }

        class SaveTest : public DialogueRulesTest
        {
        protected:
            void SetUp() override
            {
                DialogueRulesTest::SetUp();
                DialogueFile file;
                file.dialogue("q1", ESM::Dialogue::Journal)
                    .info({ .mId = "q1_name",
                        .mResponse = "Quest One",
                        .mJournalIndex = 1,
                        .mQuestStatus = ESM::DialInfo::QS_Name })
                    .info({ .mId = "q1_10", .mResponse = "Ten", .mJournalIndex = 10 })
                    .info({ .mId = "q1_20", .mResponse = "Twenty", .mJournalIndex = 20 })
                    .info({ .mId = "q1_25", .mResponse = "", .mJournalIndex = 25 })
                    .info({ .mId = "q1_30",
                        .mResponse = "Done",
                        .mJournalIndex = 30,
                        .mQuestStatus = ESM::DialInfo::QS_Finished });
                file.dialogue("q2", ESM::Dialogue::Journal)
                    .info({ .mId = "q2_name",
                        .mResponse = "Quest Two",
                        .mJournalIndex = 1,
                        .mQuestStatus = ESM::DialInfo::QS_Name })
                    .info({ .mId = "q2_10", .mResponse = "Q2 ten", .mJournalIndex = 10 });
                file.dialogue("hello", ESM::Dialogue::Topic)
                    .info({ .mId = "h1", .mResponse = "Hello one" })
                    .info({ .mId = "h2", .mResponse = "Hello two" });
                file.dialogue("rumors", ESM::Dialogue::Topic).info({ .mId = "r1", .mResponse = "Rumor" });
                loadDialogue(file);
                addFaction("f1", { { "f2", 10 } });
                addFaction("f2", {});
                mNpc1 = addNpc("fargoth", "testrace", "", "", false);
                mNpc2 = addNpc("sellus", "testrace", "", "", false);
            }

            void fill(MWBase::Journal& journal, MWBase::DialogueManager& dialogue)
            {
                journal.addEntry(rid("q1"), 10, mNpc1);
                journal.addEntry(rid("q1"), 20, mNpc1);
                journal.addEntry(rid("q1"), 30, mNpc1);
                journal.addEntry(rid("q2"), 10, mNpc1);
                journal.addEntry(rid("q1"), 25, mNpc1);
                journal.addTopic(rid("hello"), rid("h1"), mNpc1);
                journal.addTopic(rid("hello"), rid("h2"), mNpc1);
                journal.addTopic(rid("rumors"), rid("r1"), mNpc2);
                dialogue.addTopic(rid("rumors"));
                dialogue.addTopic(rid("hello"));
                dialogue.setFactionReaction(rid("f1"), rid("f2"), -5);
                dialogue.modFactionReaction(rid("f2"), rid("f1"), 3);
            }

            static std::string writeAll(const MWBase::Journal& journal, const MWBase::DialogueManager& dialogue)
            {
                auto stream = std::make_unique<std::stringstream>();
                std::stringstream* raw = stream.get();
                ESM::ESMWriter writer;
                writer.setFormatVersion(ESM::DefaultFormatVersion);
                writer.save(*raw);
                Loading::Listener listener;
                journal.write(writer, listener);
                dialogue.write(writer, listener);
                writer.close();
                return raw->str();
            }

            // The loop of MWState::LoadGame for the records of these two classes.
            static void readAll(const std::string& bytes, MWBase::Journal& journal, MWBase::DialogueManager& dialogue)
            {
                ESM::ESMReader reader;
                reader.open(std::make_unique<std::istringstream>(bytes), "save");
                while (reader.hasMoreRecs())
                {
                    ESM::NAME n = reader.getRecName();
                    reader.getRecHeader();
                    switch (n.toInt())
                    {
                        case ESM::REC_JOUR:
                        case ESM::REC_QUES:
                            journal.readRecord(reader, n.toInt());
                            break;
                        case ESM::REC_DIAS:
                            dialogue.readRecord(reader, n.toInt());
                            break;
                        default:
                            reader.skipRecord();
                    }
                }
            }

            MWWorld::Ptr mNpc1, mNpc2;
        };

        // A second way to write the state that fill() makes: the ESM structs, in the order the save game uses.
        std::string referenceStream(const std::string& actor1, const std::string& actor2)
        {
            std::stringstream stream;
            ESM::ESMWriter writer;
            writer.setFormatVersion(ESM::DefaultFormatVersion);
            writer.save(stream);
            auto id = [](const char* value) { return ESM::RefId::stringRefId(value); };

            auto quest = [&](const char* topic, int state, bool finished) {
                ESM::QuestState record;
                record.mTopic = id(topic);
                record.mState = state;
                record.mFinished = finished;
                writer.startRecord(ESM::REC_QUES);
                record.save(writer);
                writer.endRecord(ESM::REC_QUES);
            };
            auto entry = [&](ESM::JournalEntry::Type type, const char* topic, const char* info, const char* text,
                             const std::string& actor) {
                ESM::JournalEntry record;
                record.mType = type;
                record.mTopic = id(topic);
                record.mInfo = id(info);
                record.mText = text;
                record.mActorName = actor;
                record.mDay = 12;
                record.mMonth = 3;
                record.mDayOfMonth = 17;
                writer.startRecord(ESM::REC_JOUR);
                record.save(writer);
                writer.endRecord(ESM::REC_JOUR);
            };
            const auto quest_ = ESM::JournalEntry::Type_Quest;
            const auto journal_ = ESM::JournalEntry::Type_Journal;
            const auto topic_ = ESM::JournalEntry::Type_Topic;

            quest("q1", 30, true);
            entry(quest_, "q1", "q1_10", "Ten", actor1);
            entry(quest_, "q1", "q1_20", "Twenty", actor1);
            entry(quest_, "q1", "q1_30", "Done", actor1);
            entry(quest_, "q1", "q1_25", "", actor1);
            quest("q2", 10, false);
            entry(quest_, "q2", "q2_10", "Q2 ten", actor1);
            entry(journal_, "q1", "q1_10", "Ten", actor1);
            entry(journal_, "q1", "q1_20", "Twenty", actor1);
            entry(journal_, "q1", "q1_30", "Done", actor1);
            entry(journal_, "q2", "q2_10", "Q2 ten", actor1);
            entry(topic_, "hello", "h1", "Hello one", actor1);
            entry(topic_, "hello", "h2", "Hello two", actor1);
            entry(topic_, "rumors", "r1", "Rumor", actor2);

            ESM::DialogueState state;
            state.mKnownTopics = { id("hello"), id("rumors") };
            state.mChangedFactionReaction[id("f1")][id("f2")] = -5;
            state.mChangedFactionReaction[id("f2")][id("f1")] = 3;
            writer.startRecord(ESM::REC_DIAS);
            state.save(writer);
            writer.endRecord(ESM::REC_DIAS);
            writer.close();
            return stream.str();
        }

        TEST_F(SaveTest, a_state_is_written_as_the_same_bytes_as_the_esm_structs_give)
        {
            fill(*mJournal, *mDialogue);
            const std::string actual = writeAll(*mJournal, *mDialogue);
            const std::string expected = referenceStream(
                std::string(mNpc1.getClass().getName(mNpc1)), std::string(mNpc2.getClass().getName(mNpc2)));
            EXPECT_EQ(toHex(actual), toHex(expected));
        }

        TEST_F(SaveTest, the_records_come_quests_first_each_with_its_entries_then_the_journal_then_topics_then_dialogue)
        {
            fill(*mJournal, *mDialogue);
            const std::vector<std::string> names = recordNames(writeAll(*mJournal, *mDialogue));
            const std::vector<std::string> expected = { "TES3", "QUES", "JOUR", "JOUR", "JOUR", "JOUR", "QUES", "JOUR",
                "JOUR", "JOUR", "JOUR", "JOUR", "JOUR", "JOUR", "JOUR", "DIAS" };
            EXPECT_EQ(names, expected);
        }

        TEST_F(SaveTest, the_counted_records_are_the_records_written)
        {
            fill(*mJournal, *mDialogue);
            const std::vector<std::string> names = recordNames(writeAll(*mJournal, *mDialogue));
            // everything but the file header
            EXPECT_EQ(mJournal->countSavedGameRecords() + mDialogue->countSavedGameRecords(), names.size() - 1);
        }

        TEST_F(SaveTest, an_empty_state_writes_only_an_empty_dialogue_record)
        {
            const std::string bytes = writeAll(*mJournal, *mDialogue);
            EXPECT_EQ(recordNames(bytes), (std::vector<std::string>{ "TES3", "DIAS" }));
            EXPECT_EQ(mJournal->countSavedGameRecords(), 0u);
            EXPECT_EQ(mDialogue->countSavedGameRecords(), 1u);
        }

        TEST_F(SaveTest, reading_a_save_and_writing_it_again_gives_the_same_bytes)
        {
            fill(*mJournal, *mDialogue);
            const std::string first = writeAll(*mJournal, *mDialogue);

            MWDialogue::Journal journal2;
            MWDialogue::DialogueManager dialogue2(mExtensions, mTranslation);
            readAll(first, journal2, dialogue2);
            const std::string second = writeAll(journal2, dialogue2);
            EXPECT_EQ(toHex(second), toHex(first));
        }

        TEST_F(SaveTest, a_loaded_state_has_what_was_saved)
        {
            fill(*mJournal, *mDialogue);
            const std::string bytes = writeAll(*mJournal, *mDialogue);

            MWDialogue::Journal journal2;
            MWDialogue::DialogueManager dialogue2(mExtensions, mTranslation);
            readAll(bytes, journal2, dialogue2);

            EXPECT_EQ(journal2.getJournalIndex(rid("q1")), 30);
            EXPECT_TRUE(journal2.getQuestOrNull(rid("q1"))->isFinished());
            EXPECT_EQ(journal2.getJournalIndex(rid("q2")), 10);
            ASSERT_EQ(journal2.getEntries().size(), 4u);
            EXPECT_EQ(journal2.getEntries()[3].mText, "Q2 ten");
            EXPECT_EQ(journal2.getEntries()[3].mDay, 12);
            EXPECT_EQ(journal2.getEntries()[3].mMonth, 3);
            EXPECT_EQ(journal2.getEntries()[3].mDayOfMonth, 17);
            EXPECT_EQ(journal2.getQuestOrNull(rid("q1"))->size(), 4u);
            EXPECT_EQ(journal2.getTopics().at(rid("hello")).size(), 2u);
            EXPECT_EQ(journal2.getTopics().at(rid("rumors"))[0].mActorName, mNpc2.getClass().getName(mNpc2));
            EXPECT_EQ(dialogue2.getFactionReaction(rid("f1"), rid("f2")), -5);
            EXPECT_EQ(dialogue2.getFactionReaction(rid("f2"), rid("f1")), 3);
            // the topics the player knows are not visible without an actor; the second write above proves them
        }

        // A save made by the code before the move: the records of a small state, as hex (the TES3 file header in
        // front of them is left out). A save made before the move must load after it, and writing the loaded state
        // again must give these same bytes.
        // State: quest q1 at index 10 with the entry q1_10 (day 12, month 3, day of month 17) written for the actor
        // "fargoth"; topic hello with h1; the player knows the topic hello; the faction reaction f1 -> f2 is -5.
        const char* const kGoldenHex
            = "515545531f00000000000000000000005945544f02000000713151535441040000000a0000005146494e01000000004a4f55"
              "522e00000000000000000000004a45545904000000020000005945544f0200000071315945494e0500000071315f31305445"
              "58540300000054656e4a4f55525200000000000000000000004a45545904000000000000005945544f020000007131594549"
              "4e0500000071315f3130544558540300000054656e4a454441040000000c0000004a454d4f04000000030000004a45444d04"
              "000000110000004a4f55524300000000000000000000004a45545904000000010000005945544f0500000068656c6c6f5945"
              "494e020000006831544558540900000048656c6c6f206f6e654143545f07000000666172676f7468444941532d0000000000"
              "000000000000544f50490500000068656c6c6f4641435402000000663152454132020000006632494e545604000000fbffff"
              "ff";

        std::string withoutFileHeader(const std::string& bytes)
        {
            std::uint32_t size = 0;
            std::memcpy(&size, bytes.data() + 4, 4);
            return bytes.substr(16 + size);
        }

        TEST_F(SaveTest, a_small_state_is_written_as_the_bytes_the_code_before_the_move_wrote)
        {
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            mJournal->addTopic(rid("hello"), rid("h1"), mNpc1);
            mDialogue->addTopic(rid("hello"));
            mDialogue->setFactionReaction(rid("f1"), rid("f2"), -5);
            const std::string bytes = writeAll(*mJournal, *mDialogue);
            EXPECT_EQ(toHex(withoutFileHeader(bytes)), kGoldenHex);
        }

        TEST_F(SaveTest, a_save_made_before_the_move_loads_and_writes_the_same_bytes_again)
        {
            const std::string header = writeAll(*mJournal, *mDialogue).substr(0, 16 + 308);
            const std::string old = header + fromHex(kGoldenHex);
            MWDialogue::Journal journal2;
            MWDialogue::DialogueManager dialogue2(mExtensions, mTranslation);
            readAll(old, journal2, dialogue2);
            EXPECT_EQ(journal2.getJournalIndex(rid("q1")), 10);
            EXPECT_EQ(journal2.getEntries().size(), 1u);
            EXPECT_EQ(dialogue2.getFactionReaction(rid("f1"), rid("f2")), -5);
            EXPECT_EQ(toHex(withoutFileHeader(writeAll(journal2, dialogue2))), kGoldenHex);
        }

        // What a load drops: records naming things the loaded content files do not have.
        std::string handBuilt(const std::function<void(ESM::ESMWriter&)>& records)
        {
            std::stringstream stream;
            ESM::ESMWriter writer;
            writer.setFormatVersion(ESM::DefaultFormatVersion);
            writer.save(stream);
            records(writer);
            writer.close();
            return stream.str();
        }

        void writeEntry(ESM::ESMWriter& writer, ESM::JournalEntry::Type type, const char* topic, const char* info)
        {
            ESM::JournalEntry record;
            record.mType = type;
            record.mTopic = ESM::RefId::stringRefId(topic);
            record.mInfo = ESM::RefId::stringRefId(info);
            record.mText = "t";
            writer.startRecord(ESM::REC_JOUR);
            record.save(writer);
            writer.endRecord(ESM::REC_JOUR);
        }

        TEST_F(SaveTest, a_load_drops_entries_whose_topic_or_info_the_content_files_do_not_have)
        {
            const std::string bytes = handBuilt([](ESM::ESMWriter& writer) {
                writeEntry(writer, ESM::JournalEntry::Type_Journal, "q1", "q1_10"); // kept
                writeEntry(writer, ESM::JournalEntry::Type_Journal, "q1", "gone"); // unknown info
                writeEntry(writer, ESM::JournalEntry::Type_Journal, "nosuchquest", "q1_10"); // unknown topic
                writeEntry(writer, ESM::JournalEntry::Type_Topic, "hello", "h1"); // kept
                writeEntry(writer, ESM::JournalEntry::Type_Topic, "hello", "gone");
                writeEntry(writer, ESM::JournalEntry::Type_Quest, "q2", "q2_10"); // kept
                writeEntry(writer, ESM::JournalEntry::Type_Quest, "q2", "gone");
            });
            readAll(bytes, *mJournal, *mDialogue);
            EXPECT_EQ(mJournal->getEntries().size(), 1u);
            EXPECT_EQ(mJournal->getTopics().at(rid("hello")).size(), 1u);
            EXPECT_EQ(mJournal->getQuestOrNull(rid("q2"))->size(), 1u);
        }

        TEST_F(SaveTest, a_load_keeps_a_quest_state_and_reapplies_its_index_through_the_quest)
        {
            const std::string bytes = handBuilt([](ESM::ESMWriter& writer) {
                ESM::QuestState known;
                known.mTopic = ESM::RefId::stringRefId("q1");
                known.mState = 20;
                known.mFinished = 1;
                writer.startRecord(ESM::REC_QUES);
                known.save(writer);
                writer.endRecord(ESM::REC_QUES);
                ESM::QuestState unknown;
                unknown.mTopic = ESM::RefId::stringRefId("nosuchquest");
                unknown.mState = 5;
                writer.startRecord(ESM::REC_QUES);
                unknown.save(writer);
                writer.endRecord(ESM::REC_QUES);
            });
            readAll(bytes, *mJournal, *mDialogue);
            EXPECT_EQ(mJournal->getQuests().size(), 1u);
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 20);
            EXPECT_TRUE(mJournal->getQuestOrNull(rid("q1"))->isFinished());
            // setIndex() runs on load, so Lua is told
            ASSERT_EQ(mDlgLua.mQuestUpdates.size(), 1u);
            EXPECT_EQ(mDlgLua.mQuestUpdates[0], (std::pair<std::string, int>{ "q1", 20 }));
        }

        TEST_F(SaveTest, a_load_keeps_known_topics_the_store_has_and_replaces_the_faction_reactions)
        {
            mDialogue->setFactionReaction(rid("f1"), rid("f2"), 99);
            const std::string bytes = handBuilt([](ESM::ESMWriter& writer) {
                ESM::DialogueState state;
                state.mKnownTopics = { ESM::RefId::stringRefId("hello"), ESM::RefId::stringRefId("gone") };
                state.mChangedFactionReaction[ESM::RefId::stringRefId("f2")][ESM::RefId::stringRefId("f1")] = 8;
                writer.startRecord(ESM::REC_DIAS);
                state.save(writer);
                writer.endRecord(ESM::REC_DIAS);
            });
            readAll(bytes, *mJournal, *mDialogue);
            // the map is replaced, not merged: f1 -> f2 falls back to the faction record's own value (10)
            EXPECT_EQ(mDialogue->getFactionReaction(rid("f1"), rid("f2")), 10);
            EXPECT_EQ(mDialogue->getFactionReaction(rid("f2"), rid("f1")), 8);
            // the known topics: only "hello" survives; the written state shows it
            const std::string out = withoutFileHeader(writeAll(*mJournal, *mDialogue));
            EXPECT_NE(out.find("hello"), std::string::npos);
            EXPECT_EQ(out.find("gone"), std::string::npos);
        }

        TEST_F(SaveTest, a_load_adds_to_the_known_topics_it_finds_and_does_not_clear_them)
        {
            mDialogue->addTopic(rid("rumors"));
            const std::string bytes = handBuilt([](ESM::ESMWriter& writer) {
                ESM::DialogueState state;
                state.mKnownTopics = { ESM::RefId::stringRefId("hello") };
                writer.startRecord(ESM::REC_DIAS);
                state.save(writer);
                writer.endRecord(ESM::REC_DIAS);
            });
            readAll(bytes, *mJournal, *mDialogue);
            const std::string out = withoutFileHeader(writeAll(*mJournal, *mDialogue));
            EXPECT_NE(out.find("hello"), std::string::npos);
            EXPECT_NE(out.find("rumors"), std::string::npos);
        }

        TEST_F(SaveTest, clear_forgets_the_known_topics_and_not_the_faction_reactions)
        {
            mDialogue->addTopic(rid("rumors"));
            mDialogue->setFactionReaction(rid("f1"), rid("f2"), 4);
            mDialogue->clear();
            const std::string out = withoutFileHeader(writeAll(*mJournal, *mDialogue));
            EXPECT_EQ(out.find("rumors"), std::string::npos);
            // the faction reactions are NOT cleared by clear() (a quirk of the code, pinned)
            EXPECT_EQ(mDialogue->getFactionReaction(rid("f1"), rid("f2")), 4);
        }
    }
}
