// Task 0085, move 3, stage 3b: characterization tests of the journal, its quests and topics
// (apps/openmw/mwdialogue/journalimp.*, quest.*, topic.*, journalentry.*). They run the REAL classes on generated
// dialogue records and pin what the code does today, so that moving it behind the Morrowind game pack cannot change a
// result. Quirks are pinned as they are.
#include "dialoguefixture.hpp"

namespace DialogueTest
{
    namespace
    {
        const char* const kEntryMessage = "#{sJournalEntry}";

        class JournalTest : public DialogueRulesTest
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
                        .mQuestStatus = ESM::DialInfo::QS_Finished })
                    .info({ .mId = "q1_40",
                        .mResponse = "Again",
                        .mJournalIndex = 40,
                        .mQuestStatus = ESM::DialInfo::QS_Restart });
                file.dialogue("q2", ESM::Dialogue::Journal)
                    .info({ .mId = "q2_name",
                        .mResponse = "Quest One",
                        .mJournalIndex = 1,
                        .mQuestStatus = ESM::DialInfo::QS_Name })
                    .info({ .mId = "q2_10", .mResponse = "Q2 ten", .mJournalIndex = 10 })
                    .info({ .mId = "q2_50",
                        .mResponse = "Q2 done",
                        .mJournalIndex = 50,
                        .mQuestStatus = ESM::DialInfo::QS_Finished });
                file.dialogue("hello", ESM::Dialogue::Topic)
                    .info({ .mId = "h1", .mResponse = "Hello one" })
                    .info({ .mId = "h2", .mResponse = "Hello two" });
                file.dialogue("rumors", ESM::Dialogue::Topic).info({ .mId = "r1", .mResponse = "Rumor" });
                loadDialogue(file);
                mNpc1 = addNpc("fargoth", "testrace", "", "", false);
                mNpc2 = addNpc("sellus", "testrace", "", "", false);
            }

            int messages(const std::string& text) const
            {
                int count = 0;
                for (const std::string& message : mWindow.mMessages)
                    count += message == text ? 1 : 0;
                return count;
            }

            MWWorld::Ptr mNpc1, mNpc2;
        };

        TEST_F(JournalTest, an_unstarted_quest_has_index_zero_and_no_object)
        {
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 0);
            EXPECT_EQ(mJournal->getQuestOrNull(rid("q1")), nullptr);
            EXPECT_TRUE(mJournal->getQuests().empty());
        }

        TEST_F(JournalTest, an_entry_is_stamped_with_the_three_date_globals_and_announced)
        {
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            ASSERT_EQ(mJournal->getEntries().size(), 1u);
            const MWDialogue::StampedJournalEntry& entry = mJournal->getEntries().front();
            EXPECT_EQ(entry.mTopic, rid("q1"));
            EXPECT_EQ(entry.mInfoId, rid("q1_10"));
            EXPECT_EQ(entry.mText, "Ten");
            EXPECT_EQ(entry.mDay, 12); // dayspassed
            EXPECT_EQ(entry.mMonth, 3);
            EXPECT_EQ(entry.mDayOfMonth, 17);
            EXPECT_EQ(messages(kEntryMessage), 1);
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 10);
        }

        TEST_F(JournalTest, the_quest_object_is_made_by_the_first_entry_and_names_itself_from_the_name_info)
        {
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            const MWDialogue::Quest* quest = mJournal->getQuestOrNull(rid("q1"));
            ASSERT_NE(quest, nullptr);
            EXPECT_EQ(quest->getName(), "Quest One");
            EXPECT_EQ(quest->getTopic(), rid("q1"));
            EXPECT_FALSE(quest->isFinished());
            EXPECT_EQ(quest->size(), 1u);
        }

        TEST_F(JournalTest, the_index_only_rises_through_entries)
        {
            mJournal->addEntry(rid("q1"), 20, mNpc1);
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 20);
            EXPECT_EQ(mJournal->getEntries().size(), 2u);
        }

        TEST_F(JournalTest, the_same_entry_twice_adds_nothing_and_stays_quiet)
        {
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            mWindow.mMessages.clear();
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            EXPECT_EQ(mJournal->getEntries().size(), 1u);
            EXPECT_EQ(mJournal->getQuestOrNull(rid("q1"))->size(), 1u);
            EXPECT_EQ(messages(kEntryMessage), 0);
        }

        TEST_F(JournalTest, a_repeated_entry_whose_index_was_lowered_raises_it_again_with_a_message)
        {
            mJournal->addEntry(rid("q1"), 20, mNpc1);
            mJournal->setJournalIndex(rid("q1"), 5);
            mWindow.mMessages.clear();
            mJournal->addEntry(rid("q1"), 20, mNpc1);
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 20);
            EXPECT_EQ(mJournal->getEntries().size(), 1u);
            EXPECT_EQ(messages(kEntryMessage), 1);
        }

        TEST_F(JournalTest, an_entry_with_empty_text_moves_the_quest_but_is_not_shown)
        {
            mJournal->addEntry(rid("q1"), 25, mNpc1);
            EXPECT_TRUE(mJournal->getEntries().empty());
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 25);
            EXPECT_EQ(mJournal->getQuestOrNull(rid("q1"))->size(), 1u);
            EXPECT_EQ(messages(kEntryMessage), 0);
        }

        TEST_F(JournalTest, the_quest_keeps_entries_in_the_order_they_came_in_not_index_order)
        {
            mJournal->addEntry(rid("q1"), 20, mNpc1);
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            const MWDialogue::Quest* quest = mJournal->getQuestOrNull(rid("q1"));
            ASSERT_EQ(quest->size(), 2u);
            EXPECT_EQ((*quest)[0].mInfoId, rid("q1_20"));
            EXPECT_EQ((*quest)[1].mInfoId, rid("q1_10"));
        }

        TEST_F(JournalTest, a_finishing_entry_finishes_the_quest_and_a_restart_entry_undoes_it)
        {
            mJournal->addEntry(rid("q1"), 30, mNpc1);
            EXPECT_TRUE(mJournal->getQuestOrNull(rid("q1"))->isFinished());
            mJournal->addEntry(rid("q1"), 40, mNpc1);
            EXPECT_FALSE(mJournal->getQuestOrNull(rid("q1"))->isFinished());
        }

        TEST_F(JournalTest, an_ordinary_entry_leaves_the_finished_flag_alone)
        {
            mJournal->addEntry(rid("q1"), 30, mNpc1);
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            EXPECT_TRUE(mJournal->getQuestOrNull(rid("q1"))->isFinished());
        }

        TEST_F(JournalTest, a_restart_entry_also_restarts_other_finished_quests_with_the_same_name)
        {
            mJournal->addEntry(rid("q2"), 50, mNpc1);
            ASSERT_TRUE(mJournal->getQuestOrNull(rid("q2"))->isFinished());
            mJournal->addEntry(rid("q1"), 40, mNpc1);
            EXPECT_FALSE(mJournal->getQuestOrNull(rid("q2"))->isFinished());
        }

        TEST_F(JournalTest, an_entry_that_is_not_a_restart_leaves_other_finished_quests_finished)
        {
            // q1's name is "Quest One"; q2 is renamed by comparing against a quest that has a different name.
            mJournal->addEntry(rid("q1"), 30, mNpc1);
            ASSERT_TRUE(mJournal->getQuestOrNull(rid("q1"))->isFinished());
            mJournal->addEntry(rid("q2"), 10, mNpc1);
            // q2 has the same name as q1 here, but its entry is not a restart: q1 stays finished.
            EXPECT_TRUE(mJournal->getQuestOrNull(rid("q1"))->isFinished());
        }

        TEST_F(JournalTest, an_unknown_journal_index_throws_and_changes_nothing)
        {
            EXPECT_THROW(mJournal->addEntry(rid("q1"), 999, mNpc1), std::runtime_error);
            EXPECT_TRUE(mJournal->getEntries().empty());
            EXPECT_TRUE(mJournal->getQuests().empty());
        }

        TEST_F(JournalTest, an_unknown_quest_throws)
        {
            EXPECT_THROW(mJournal->addEntry(rid("nosuchquest"), 10, mNpc1), std::runtime_error);
        }

        TEST_F(JournalTest, setting_an_index_starts_the_quest_without_an_entry_and_tells_lua)
        {
            mJournal->setJournalIndex(rid("q1"), 20);
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 20);
            EXPECT_TRUE(mJournal->getEntries().empty());
            ASSERT_NE(mJournal->getQuestOrNull(rid("q1")), nullptr);
            EXPECT_EQ(mJournal->getQuestOrNull(rid("q1"))->size(), 0u);
            ASSERT_EQ(mDlgLua.mQuestUpdates.size(), 1u);
            EXPECT_EQ(mDlgLua.mQuestUpdates[0].first, "q1");
            EXPECT_EQ(mDlgLua.mQuestUpdates[0].second, 20);
        }

        TEST_F(JournalTest, the_index_may_be_set_downwards_and_to_a_value_with_no_entry)
        {
            mJournal->setJournalIndex(rid("q1"), 20);
            mJournal->setJournalIndex(rid("q1"), 7);
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 7);
        }

        TEST_F(JournalTest, an_entry_tells_lua_the_new_index_only_when_it_rises)
        {
            mJournal->addEntry(rid("q1"), 20, mNpc1);
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            ASSERT_EQ(mDlgLua.mQuestUpdates.size(), 1u);
            EXPECT_EQ(mDlgLua.mQuestUpdates[0].second, 20);
        }

        TEST_F(JournalTest, getting_a_quest_to_start_makes_it_once)
        {
            MWDialogue::Quest& first = mJournal->getOrStartQuest(rid("q1"));
            MWDialogue::Quest& second = mJournal->getOrStartQuest(rid("q1"));
            EXPECT_EQ(&first, &second);
            EXPECT_EQ(mJournal->getQuests().size(), 1u);
        }

        TEST_F(JournalTest, a_topic_response_is_recorded_with_the_actor_name_and_the_text)
        {
            mJournal->addTopic(rid("hello"), rid("h1"), mNpc1);
            ASSERT_EQ(mJournal->getTopics().size(), 1u);
            const MWDialogue::Topic& topic = mJournal->getTopics().at(rid("hello"));
            EXPECT_EQ(topic.getName(), "hello");
            ASSERT_EQ(topic.size(), 1u);
            EXPECT_EQ(topic[0].mInfoId, rid("h1"));
            EXPECT_EQ(topic[0].mText, "Hello one");
            EXPECT_EQ(topic[0].mActorName, mNpc1.getClass().getName(mNpc1));
        }

        TEST_F(JournalTest, the_same_topic_response_twice_is_kept_once)
        {
            mJournal->addTopic(rid("hello"), rid("h1"), mNpc1);
            mJournal->addTopic(rid("hello"), rid("h1"), mNpc2);
            EXPECT_EQ(mJournal->getTopics().at(rid("hello")).size(), 1u);
        }

        TEST_F(JournalTest, a_topic_unknown_to_the_store_throws)
        {
            EXPECT_THROW(mJournal->addTopic(rid("nosuchtopic"), rid("h1"), mNpc1), std::exception);
        }

        TEST_F(JournalTest, an_unknown_info_of_a_known_topic_throws)
        {
            EXPECT_THROW(mJournal->addTopic(rid("hello"), rid("nosuchinfo"), mNpc1), std::runtime_error);
        }

        TEST_F(JournalTest, removing_the_last_response_of_an_actor_removes_the_last_one_that_actor_gave)
        {
            // Both responses carry the same actor name, so the removal takes the LATER one.
            mJournal->addTopic(rid("hello"), rid("h1"), mNpc1);
            mJournal->addTopic(rid("hello"), rid("h2"), mNpc1);
            mJournal->removeLastAddedTopicResponse(rid("hello"), mNpc1.getClass().getName(mNpc1));
            const MWDialogue::Topic& topic = mJournal->getTopics().at(rid("hello"));
            ASSERT_EQ(topic.size(), 1u);
            EXPECT_EQ(topic[0].mInfoId, rid("h1"));
        }

        TEST_F(JournalTest, removing_the_only_response_removes_the_topic)
        {
            mJournal->addTopic(rid("hello"), rid("h1"), mNpc1);
            mJournal->removeLastAddedTopicResponse(rid("hello"), mNpc1.getClass().getName(mNpc1));
            EXPECT_TRUE(mJournal->getTopics().empty());
        }

        TEST_F(JournalTest, removing_a_response_of_another_actor_changes_nothing)
        {
            mJournal->addTopic(rid("hello"), rid("h1"), mNpc1);
            mJournal->removeLastAddedTopicResponse(rid("hello"), "somebody else");
            EXPECT_EQ(mJournal->getTopics().at(rid("hello")).size(), 1u);
        }

        TEST_F(JournalTest, removing_from_a_topic_nobody_wrote_to_makes_no_topic)
        {
            // getTopic() creates the topic, the removal finds no entry, and the empty topic is erased again.
            mJournal->removeLastAddedTopicResponse(rid("rumors"), "nobody");
            EXPECT_TRUE(mJournal->getTopics().empty());
        }

        TEST_F(JournalTest, clear_forgets_entries_quests_and_topics)
        {
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            mJournal->addTopic(rid("hello"), rid("h1"), mNpc1);
            mJournal->clear();
            EXPECT_TRUE(mJournal->getEntries().empty());
            EXPECT_TRUE(mJournal->getQuests().empty());
            EXPECT_TRUE(mJournal->getTopics().empty());
            EXPECT_EQ(mJournal->countSavedGameRecords(), 0u);
        }

        TEST_F(JournalTest, the_saved_record_count_is_quests_plus_their_entries_plus_journal_plus_topic_entries)
        {
            EXPECT_EQ(mJournal->countSavedGameRecords(), 0u);
            mJournal->addEntry(rid("q1"), 10, mNpc1); // 1 quest + 1 quest entry + 1 journal entry
            EXPECT_EQ(mJournal->countSavedGameRecords(), 3u);
            mJournal->addEntry(rid("q1"), 25, mNpc1); // + 1 quest entry (empty text: not in the journal)
            EXPECT_EQ(mJournal->countSavedGameRecords(), 4u);
            mJournal->addTopic(rid("hello"), rid("h1"), mNpc1); // + 1
            mJournal->addTopic(rid("hello"), rid("h2"), mNpc1); // + 1
            EXPECT_EQ(mJournal->countSavedGameRecords(), 6u);
        }

        TEST_F(JournalTest, the_journal_keeps_entries_of_different_quests_in_the_order_they_were_added)
        {
            mJournal->addEntry(rid("q2"), 10, mNpc1);
            mJournal->addEntry(rid("q1"), 10, mNpc1);
            mJournal->addEntry(rid("q2"), 50, mNpc1);
            ASSERT_EQ(mJournal->getEntries().size(), 3u);
            EXPECT_EQ(mJournal->getEntries()[0].mInfoId, rid("q2_10"));
            EXPECT_EQ(mJournal->getEntries()[1].mInfoId, rid("q1_10"));
            EXPECT_EQ(mJournal->getEntries()[2].mInfoId, rid("q2_50"));
        }
    }
}
