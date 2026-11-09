// Task 0085, move 3, stage 3b: characterization tests of the dialogue manager (apps/openmw/mwdialogue/
// dialoguemanagerimp.*): a scripted conversation (greeting, topics, result scripts run by the real compiler and
// interpreter, a choice, goodbye), persuasion, service refusal, say(), the known topics, the faction reactions. They
// run the REAL manager, journal, filter and script opcodes on generated dialogue records and pin what the code does
// today.
//
// The doubles: derived disposition is 10 plus the actor's base disposition; persuasion outcomes come from the test
// (DlgMechanics); the sound manager and Lua manager record what they are asked.
#include "dialoguefixture.hpp"

#include <components/esm3/loadmisc.hpp>
#include <components/settings/values.hpp>

#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/npcstats.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"

namespace DialogueTest
{
    namespace
    {
        using F = ESM::DialogueCondition;

        class ManagerTest : public DialogueRulesTest
        {
        protected:
            void SetUp() override
            {
                DialogueRulesTest::SetUp();
                addFaction("f1", { { "f2", 10 } });
                addFaction("f2", {});
                mFargoth = addNpc("fargoth", "bosmer", "thief", "f1", false, 2);
                mSellus = addNpc("sellus", "dunmer", "trader", "", true);
                mRat = addCreature("rat");
                mFargoth.getClass().getNpcStats(mFargoth).setBaseDisposition(40);
                mSellus.getClass().getNpcStats(mSellus).setBaseDisposition(40);

                DialogueFile file;
                file.dialogue("q1", ESM::Dialogue::Journal)
                    .info({ .mId = "q1_name",
                        .mResponse = "Quest One",
                        .mJournalIndex = 1,
                        .mQuestStatus = ESM::DialInfo::QS_Name })
                    .info({ .mId = "q1_10", .mResponse = "Ten", .mJournalIndex = 10 })
                    .info({ .mId = "q1_20", .mResponse = "Twenty", .mJournalIndex = 20 });
                file.dialogue("greeting 0", ESM::Dialogue::Greeting)
                    .info({ .mId = "g_sellus",
                        .mResponse = "Welcome, %Name.",
                        .mResultScript = "Journal \"q1\", 10",
                        .mActor = "sellus" });
                file.dialogue("greeting 1", ESM::Dialogue::Greeting)
                    .info({ .mId = "g_any", .mResponse = "Hello there. Ask about secret." });
                file.dialogue("job", ESM::Dialogue::Topic)
                    .info({ .mId = "j_sellus", .mResponse = "Selling things.", .mActor = "sellus" })
                    .info({ .mId = "j_any",
                        .mResponse = "I trade goods. Ask about rumors.",
                        .mResultScript = "Journal \"q1\", 20" });
                file.dialogue("rumors", ESM::Dialogue::Topic).info({ .mId = "r1", .mResponse = "Nothing new." });
                file.dialogue("secret", ESM::Dialogue::Topic)
                    .info({ .mId = "s1", .mResponse = "Psst.", .mResultScript = "AddTopic \"rumors\"" });
                file.dialogue("broken", ESM::Dialogue::Topic)
                    .info({ .mId = "b1", .mResponse = "Unscripted.", .mResultScript = "this is not a script" });
                file.dialogue("farewell", ESM::Dialogue::Topic)
                    .info({ .mId = "w1", .mResponse = "Goodbye then.", .mResultScript = "Goodbye" });
                file.dialogue("choosey", ESM::Dialogue::Topic)
                    .info({ .mId = "c_yes",
                        .mResponse = "Good.",
                        .mResultScript = "Journal \"q1\", 20",
                        .mSelects = { condition(F::Function_Choice, F::Comp_Eq, 1) } })
                    .info({ .mId = "c_no",
                        .mResponse = "Pity.",
                        .mSelects = { condition(F::Function_Choice, F::Comp_Eq, 2) } })
                    .info({ .mId = "c_ask",
                        .mResponse = "Do you want it?",
                        .mResultScript = "Choice \"Yes\" 1 \"No\" 2",
                        .mSelects = { condition(F::Function_Global, F::Comp_Gt, 0, "askopen") } });
                file.dialogue("Admire Success", ESM::Dialogue::Persuasion)
                    .info({ .mId = "as1", .mResponse = "You flatter me." });
                file.dialogue("Admire Fail", ESM::Dialogue::Persuasion).info({ .mId = "af1", .mResponse = "No." });
                file.dialogue("Bribe Success", ESM::Dialogue::Persuasion)
                    .info({ .mId = "bs1", .mResponse = "Pleasure." });
                file.dialogue("Bribe Fail", ESM::Dialogue::Persuasion).info({ .mId = "bf1", .mResponse = "No." });
                file.dialogue("Service Refusal", ESM::Dialogue::Topic)
                    .info({ .mId = "sr1",
                        .mResponse = "I do not trade with you.",
                        .mDisposition = 60,
                        .mSelects = { condition(F::Function_Choice, F::Comp_Eq, 1) } });
                file.dialogue("Info Refusal", ESM::Dialogue::Topic).info({ .mId = "ir1", .mResponse = "Refused" });
                file.dialogue("hit", ESM::Dialogue::Voice)
                    .info({ .mId = "v_hit", .mResponse = "Ouch!", .mResultScript = "Journal \"q1\", 10" });
                file.dialogue("idle", ESM::Dialogue::Voice).info({ .mId = "v_idle", .mResponse = "La la." });
                loadDialogue(file);
            }

            void start(const MWWorld::Ptr& actor)
            {
                Responses responses;
                ASSERT_TRUE(mDialogue->startDialogue(actor, &responses));
            }

            int messages(const std::string& text) const
            {
                int count = 0;
                for (const std::string& message : mWindow.mMessages)
                    count += message == text ? 1 : 0;
                return count;
            }

            MWWorld::Ptr mFargoth, mSellus, mRat;
        };

        TEST_F(ManagerTest, the_greeting_is_the_first_matching_greeting_in_topic_order)
        {
            Responses responses;
            EXPECT_TRUE(mDialogue->startDialogue(mFargoth, &responses));
            ASSERT_EQ(responses.mLines.size(), 1u);
            EXPECT_EQ(responses.mLines[0].mTitle, "");
            EXPECT_EQ(responses.mLines[0].mText, "Hello there. Ask about secret.");
            ASSERT_EQ(mDlgLua.mResponses.size(), 1u);
            EXPECT_EQ(mDlgLua.mResponses[0].mInfo, "g_any");
            EXPECT_EQ(mDlgLua.mResponses[0].mTopic, "greeting 1");
        }

        TEST_F(ManagerTest, an_actor_with_its_own_greeting_gets_that_one_because_it_sorts_first)
        {
            Responses responses;
            EXPECT_TRUE(mDialogue->startDialogue(mSellus, &responses));
            ASSERT_EQ(responses.mLines.size(), 1u);
            // %Name is replaced by the actor's name
            EXPECT_EQ(responses.mLines[0].mText, "Welcome, " + std::string(mSellus.getClass().getName(mSellus)) + ".");
        }

        TEST_F(ManagerTest, the_greeting_runs_its_result_script_and_updates_the_world_globals_first)
        {
            Responses responses;
            const int before = mDlgWorld.mGlobalUpdates;
            mDialogue->startDialogue(mSellus, &responses);
            EXPECT_GT(mDlgWorld.mGlobalUpdates, before);
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 10);
        }

        TEST_F(ManagerTest, the_player_is_marked_as_talked_to_after_a_greeting)
        {
            EXPECT_FALSE(mFargoth.getClass().getCreatureStats(mFargoth).hasTalkedToPlayer());
            start(mFargoth);
            EXPECT_TRUE(mFargoth.getClass().getCreatureStats(mFargoth).hasTalkedToPlayer());
        }

        TEST_F(ManagerTest, a_creature_has_no_greeting)
        {
            Responses responses;
            EXPECT_FALSE(mDialogue->startDialogue(mRat, &responses));
            EXPECT_TRUE(responses.mLines.empty());
        }

        TEST_F(ManagerTest, a_dead_actor_is_not_spoken_to)
        {
            mFargoth.getClass().getCreatureStats(mFargoth).setHealth(MWMechanics::DynamicStat<float>(0.f));
            ASSERT_TRUE(mFargoth.getClass().getCreatureStats(mFargoth).isDead());
            Responses responses;
            EXPECT_FALSE(mDialogue->startDialogue(mFargoth, &responses));
        }

        TEST_F(ManagerTest, a_topic_gives_the_first_matching_response_titled_with_the_topic_name)
        {
            start(mFargoth);
            Responses responses;
            mDialogue->keywordSelected("job", &responses);
            ASSERT_EQ(responses.mLines.size(), 1u);
            EXPECT_EQ(responses.mLines[0].mTitle, "job");
            EXPECT_EQ(responses.mLines[0].mText, "I trade goods. Ask about rumors.");
            // sellus' own answer is not for fargoth
            Responses sellus;
            start(mSellus);
            mDialogue->keywordSelected("job", &sellus);
            ASSERT_EQ(sellus.mLines.size(), 1u);
            EXPECT_EQ(sellus.mLines[0].mText, "Selling things.");
        }

        TEST_F(ManagerTest, a_topic_runs_its_result_script_and_tells_lua)
        {
            start(mFargoth);
            mDlgLua.mResponses.clear();
            Responses responses;
            mDialogue->keywordSelected("job", &responses);
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 20);
            ASSERT_EQ(mDlgLua.mResponses.size(), 1u);
            EXPECT_EQ(mDlgLua.mResponses[0].mInfo, "j_any");
        }

        TEST_F(ManagerTest, a_topic_response_is_written_into_the_journal_under_the_actor)
        {
            start(mFargoth);
            Responses responses;
            mDialogue->keywordSelected("job", &responses);
            ASSERT_EQ(mJournal->getTopics().count(rid("job")), 1u);
            const MWDialogue::Topic& topic = mJournal->getTopics().at(rid("job"));
            ASSERT_EQ(topic.size(), 1u);
            EXPECT_EQ(topic[0].mInfoId, rid("j_any"));
            EXPECT_EQ(topic[0].mActorName, mFargoth.getClass().getName(mFargoth));
            EXPECT_TRUE(mDialogue->inJournal(rid("job"), rid("j_any")));
            EXPECT_FALSE(mDialogue->inJournal(rid("job"), rid("j_sellus")));
            EXPECT_FALSE(mDialogue->inJournal(rid("rumors"), rid("r1")));
        }

        TEST_F(ManagerTest, a_topic_named_in_a_response_becomes_known_if_the_actor_can_answer_it)
        {
            start(mFargoth);
            Responses responses;
            mDialogue->keywordSelected("job", &responses); // the text says "rumors"; the greeting taught "secret"
            const std::list<std::string> topics = mDialogue->getAvailableTopics();
            EXPECT_EQ(std::vector<std::string>(topics.begin(), topics.end()),
                (std::vector<std::string>{ "rumors", "secret" }));
        }

        TEST_F(ManagerTest, the_greeting_text_also_teaches_topics)
        {
            start(mFargoth); // "Ask about secret."
            const std::list<std::string> topics = mDialogue->getAvailableTopics();
            EXPECT_EQ(std::vector<std::string>(topics.begin(), topics.end()), (std::vector<std::string>{ "secret" }));
        }

        TEST_F(ManagerTest, known_topics_are_listed_in_case_insensitive_order_and_only_if_the_actor_answers)
        {
            start(mFargoth);
            mDialogue->addTopic(rid("secret"));
            mDialogue->addTopic(rid("job"));
            mDialogue->addTopic(rid("farewell"));
            mDialogue->addTopic(
                rid("Service Refusal")); // not a thing the actor can answer in the topic list? it is a topic
            const std::list<std::string> topics = mDialogue->getAvailableTopics();
            std::vector<std::string> v(topics.begin(), topics.end());
            EXPECT_TRUE(std::is_sorted(v.begin(), v.end(),
                [](const std::string& a, const std::string& b) { return Misc::StringUtils::ciLess(a, b); }));
            EXPECT_NE(std::find(v.begin(), v.end(), "job"), v.end());
            EXPECT_NE(std::find(v.begin(), v.end(), "secret"), v.end());
            EXPECT_NE(std::find(v.begin(), v.end(), "farewell"), v.end());
        }

        TEST_F(ManagerTest, a_topic_the_player_does_not_know_is_not_listed_even_if_the_actor_answers_it)
        {
            start(mFargoth);
            const std::list<std::string> topics = mDialogue->getAvailableTopics();
            EXPECT_EQ(std::count(topics.begin(), topics.end(), "job"), 0);
        }

        TEST_F(ManagerTest, topic_flags_mark_actor_specific_and_exhausted_topics)
        {
            start(mSellus);
            mDialogue->addTopic(rid("job"));
            mDialogue->addTopic(rid("rumors"));
            mDialogue->getAvailableTopics();
            EXPECT_EQ(mDialogue->getTopicFlag(rid("job")), MWBase::DialogueManager::TopicType::Specific);
            EXPECT_EQ(mDialogue->getTopicFlag(rid("rumors")), 0);
            Responses responses;
            mDialogue->keywordSelected("job", &responses);
            mDialogue->getAvailableTopics();
            EXPECT_EQ(mDialogue->getTopicFlag(rid("job")), MWBase::DialogueManager::TopicType::Exhausted);
            EXPECT_EQ(mDialogue->getTopicFlag(rid("nosuchtopic")), 0);
        }

        TEST_F(ManagerTest, an_exhausted_topic_whose_answer_names_an_unknown_topic_is_not_exhausted)
        {
            start(mFargoth);
            mDialogue->addTopic(rid("job"));
            Responses responses;
            mDialogue->keywordSelected("job", &responses);
            // "rumors" was learned from the text, so job is exhausted now
            mDialogue->getAvailableTopics();
            EXPECT_EQ(mDialogue->getTopicFlag(rid("job")), MWBase::DialogueManager::TopicType::Exhausted);
            // forget the topics: "rumors" is not known, so the answer still leads somewhere new
            mDialogue->clear();
            mDialogue->addTopic(rid("job"));
            mDialogue->getAvailableTopics();
            EXPECT_EQ(mDialogue->getTopicFlag(rid("job")), 0);
        }

        TEST_F(ManagerTest, a_result_script_can_teach_a_topic)
        {
            start(mFargoth);
            Responses responses;
            mDialogue->addTopic(rid("secret"));
            mDialogue->keywordSelected("secret", &responses);
            const std::list<std::string> topics = mDialogue->getAvailableTopics();
            EXPECT_EQ(std::count(topics.begin(), topics.end(), "rumors"), 1);
        }

        TEST_F(ManagerTest, a_result_script_that_does_not_compile_is_reported_and_does_not_stop_the_response)
        {
            start(mFargoth);
            Responses responses;
            EXPECT_NO_THROW(mDialogue->keywordSelected("broken", &responses));
            ASSERT_EQ(responses.mLines.size(), 1u);
            EXPECT_EQ(responses.mLines[0].mText, "Unscripted.");
        }

        TEST_F(ManagerTest, a_keyword_that_is_not_a_topic_is_ignored)
        {
            start(mFargoth);
            Responses responses;
            mDialogue->keywordSelected("greeting 1", &responses); // a greeting, not a topic
            mDialogue->keywordSelected("nosuchtopic", &responses);
            EXPECT_TRUE(responses.mLines.empty());
        }

        TEST_F(ManagerTest, the_goodbye_command_ends_the_conversation_and_starting_one_resets_it)
        {
            start(mFargoth);
            EXPECT_FALSE(mDialogue->isGoodbye());
            Responses responses;
            mDialogue->keywordSelected("farewell", &responses);
            EXPECT_TRUE(mDialogue->isGoodbye());
            EXPECT_FALSE(mDialogue->isInChoice());
            start(mFargoth);
            EXPECT_FALSE(mDialogue->isGoodbye());
        }

        TEST_F(ManagerTest, a_choice_command_offers_choices_and_blocks_topics_until_answered)
        {
            start(mFargoth);
            Responses responses;
            mDialogue->keywordSelected("choosey", &responses);
            ASSERT_EQ(responses.mLines.size(), 1u);
            EXPECT_EQ(responses.mLines[0].mText, "Do you want it?");
            EXPECT_TRUE(mDialogue->isInChoice());
            const auto& choices = mDialogue->getChoices();
            ASSERT_EQ(choices.size(), 2u);
            EXPECT_EQ(choices[0], (std::pair<std::string, int>{ "Yes", 1 }));
            EXPECT_EQ(choices[1], (std::pair<std::string, int>{ "No", 2 }));
            // topics cannot be asked while a choice is open
            Responses blocked;
            mDialogue->keywordSelected("job", &blocked);
            EXPECT_TRUE(blocked.mLines.empty());
        }

        TEST_F(ManagerTest, answering_a_choice_selects_the_info_for_that_answer_and_closes_the_choice)
        {
            start(mFargoth);
            Responses question;
            mDialogue->keywordSelected("choosey", &question);
            Responses answer;
            mDialogue->questionAnswered(1, &answer);
            ASSERT_EQ(answer.mLines.size(), 1u);
            EXPECT_EQ(answer.mLines[0].mText, "Good.");
            EXPECT_EQ(answer.mLines[0].mTitle, "");
            EXPECT_FALSE(mDialogue->isInChoice());
            EXPECT_TRUE(mDialogue->getChoices().empty());
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 20); // the answer's result script ran
        }

        TEST_F(ManagerTest, the_other_answer_selects_the_other_info)
        {
            start(mFargoth);
            Responses question;
            mDialogue->keywordSelected("choosey", &question);
            Responses answer;
            mDialogue->questionAnswered(2, &answer);
            ASSERT_EQ(answer.mLines.size(), 1u);
            EXPECT_EQ(answer.mLines[0].mText, "Pity.");
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 0);
        }

        TEST_F(ManagerTest, an_answer_that_only_the_question_info_accepts_asks_the_question_again)
        {
            // the question info has no Choice condition, so it matches any answer the others do not take
            start(mFargoth);
            Responses question;
            mDialogue->keywordSelected("choosey", &question);
            Responses answer;
            mDialogue->questionAnswered(7, &answer);
            ASSERT_EQ(answer.mLines.size(), 1u);
            EXPECT_EQ(answer.mLines[0].mText, "Do you want it?");
            EXPECT_TRUE(mDialogue->isInChoice());
            EXPECT_EQ(mDialogue->getChoices().size(), 2u);
        }

        TEST_F(ManagerTest, an_answer_nothing_matches_closes_the_choice_quietly)
        {
            mDlgWorld.mGlobals["askopen"] = 1.f;
            start(mFargoth);
            Responses question;
            mDialogue->keywordSelected("choosey", &question);
            ASSERT_TRUE(mDialogue->isInChoice());
            mDlgWorld.mGlobals["askopen"] = 0.f; // now not even the question info matches
            Responses answer;
            mDialogue->questionAnswered(7, &answer);
            EXPECT_TRUE(answer.mLines.empty());
            EXPECT_FALSE(mDialogue->isInChoice());
            EXPECT_TRUE(mDialogue->getChoices().empty());
        }

        TEST_F(ManagerTest, an_answer_adds_the_topic_response_to_the_journal_of_the_last_topic)
        {
            start(mFargoth);
            Responses question;
            mDialogue->keywordSelected("choosey", &question);
            Responses answer;
            mDialogue->questionAnswered(1, &answer);
            // the question and the answer are both recorded under "choosey"
            const MWDialogue::Topic& topic = mJournal->getTopics().at(rid("choosey"));
            EXPECT_EQ(topic.size(), 2u);
        }

        TEST_F(ManagerTest, adding_a_choice_by_hand_opens_a_choice)
        {
            mDialogue->addChoice("Maybe", 3);
            EXPECT_TRUE(mDialogue->isInChoice());
            ASSERT_EQ(mDialogue->getChoices().size(), 1u);
            EXPECT_EQ(mDialogue->getChoices()[0], (std::pair<std::string, int>{ "Maybe", 3 }));
        }

        TEST_F(ManagerTest, persuasion_runs_the_success_topic_with_the_gmst_as_title_and_moves_disposition)
        {
            start(mFargoth);
            mDlgMechanics.mPersuadeSuccess = true;
            mDlgMechanics.mPersuadeTemp = 5;
            mDlgMechanics.mPersuadePerm = 2;
            Responses responses;
            mDialogue->persuade(MWBase::MechanicsManager::PT_Admire, &responses);
            ASSERT_EQ(responses.mLines.size(), 1u);
            EXPECT_EQ(responses.mLines[0].mTitle, "Admire success");
            EXPECT_EQ(responses.mLines[0].mText, "You flatter me.");
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), 45);
            EXPECT_EQ(mDlgMechanics.mPersuasionTypes, (std::vector<int>{ MWBase::MechanicsManager::PT_Admire }));
            // the player used speechcraft
            ASSERT_EQ(mDlgLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mDlgLua.mSkillUses[0].mSkill, "Speechcraft");
            EXPECT_EQ(mDlgLua.mSkillUses[0].mUseType, ESM::Skill::Speechcraft_Success);
        }

        TEST_F(ManagerTest, a_failed_persuasion_runs_the_fail_topic)
        {
            start(mFargoth);
            mDlgMechanics.mPersuadeSuccess = false;
            mDlgMechanics.mPersuadeTemp = -3;
            mDlgMechanics.mPersuadePerm = -1;
            Responses responses;
            mDialogue->persuade(MWBase::MechanicsManager::PT_Admire, &responses);
            ASSERT_EQ(responses.mLines.size(), 1u);
            EXPECT_EQ(responses.mLines[0].mTitle, "Admire fail");
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), 37);
            ASSERT_EQ(mDlgLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mDlgLua.mSkillUses[0].mUseType, ESM::Skill::Speechcraft_Fail);
        }

        TEST_F(ManagerTest, ending_the_conversation_makes_the_permanent_change_permanent)
        {
            start(mFargoth);
            mDlgMechanics.mPersuadeSuccess = true;
            mDlgMechanics.mPersuadeTemp = 5;
            mDlgMechanics.mPersuadePerm = 2;
            Responses responses;
            mDialogue->persuade(MWBase::MechanicsManager::PT_Admire, &responses);
            mDialogue->goodbyeSelected();
            // original 40 + permanent 2
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), 42);
            // and ending again changes nothing
            mDialogue->goodbyeSelected();
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), 42);
        }

        TEST_F(ManagerTest, the_permanent_disposition_is_clamped_so_the_derived_value_stays_between_0_and_100)
        {
            start(mFargoth);
            mDlgMechanics.mPersuadeSuccess = true;
            mDlgMechanics.mPersuadeTemp = 1;
            mDlgMechanics.mPersuadePerm = 200;
            Responses responses;
            mDialogue->persuade(MWBase::MechanicsManager::PT_Admire, &responses);
            mDialogue->goodbyeSelected();
            // derived with base 0 is 10, so the largest base is 100 - 10
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), 90);
        }

        TEST_F(ManagerTest, a_negative_permanent_change_never_pushes_the_original_disposition_below_zero)
        {
            // temp > 0 and perm > 0 with original + perm + earlier permanent < 0 clamps perm
            start(mFargoth);
            mFargoth.getClass().getNpcStats(mFargoth).setBaseDisposition(-30);
            mDlgMechanics.mPersuadeSuccess = true;
            mDlgMechanics.mPersuadeTemp = 4;
            mDlgMechanics.mPersuadePerm = 4;
            Responses responses;
            mDialogue->persuade(MWBase::MechanicsManager::PT_Admire, &responses);
            // original -30, perm 4: -26 < 0, so perm becomes 30 and the current disposition is -30 + 4
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), -26);
            mDialogue->goodbyeSelected();
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), 0);
        }

        TEST_F(ManagerTest, a_successful_bribe_moves_the_gold_from_the_player_to_the_actor)
        {
            ESM::Miscellaneous gold;
            gold.blank();
            gold.mId = MWWorld::ContainerStore::sGoldId;
            gold.mData.mValue = 1;
            gold.mData.mWeight = 0.f;
            mStore.insertStatic(gold);
            give(MWWorld::ContainerStore::sGoldId, 150);
            start(mFargoth);
            mDlgMechanics.mPersuadeSuccess = true;
            Responses responses;
            mDialogue->persuade(MWBase::MechanicsManager::PT_Bribe100, &responses);
            ASSERT_EQ(responses.mLines.size(), 1u);
            EXPECT_EQ(responses.mLines[0].mTitle, "Bribe success");
            MWWorld::ContainerStore& player = mPlayer.getClass().getContainerStore(mPlayer);
            MWWorld::ContainerStore& actor = mFargoth.getClass().getContainerStore(mFargoth);
            EXPECT_EQ(player.count(MWWorld::ContainerStore::sGoldId), 50);
            EXPECT_EQ(actor.count(MWWorld::ContainerStore::sGoldId), 100);
        }

        TEST_F(ManagerTest, a_failed_bribe_keeps_the_gold)
        {
            ESM::Miscellaneous gold;
            gold.blank();
            gold.mId = MWWorld::ContainerStore::sGoldId;
            gold.mData.mValue = 1;
            gold.mData.mWeight = 0.f;
            mStore.insertStatic(gold);
            give(MWWorld::ContainerStore::sGoldId, 150);
            start(mFargoth);
            mDlgMechanics.mPersuadeSuccess = false;
            Responses responses;
            mDialogue->persuade(MWBase::MechanicsManager::PT_Bribe100, &responses);
            MWWorld::ContainerStore& player = mPlayer.getClass().getContainerStore(mPlayer);
            EXPECT_EQ(player.count(MWWorld::ContainerStore::sGoldId), 150);
        }

        TEST_F(ManagerTest, a_barter_disposition_change_is_temporary_by_default)
        {
            start(mFargoth);
            mDialogue->applyBarterDispositionChange(7);
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), 47);
            // "gets discarded when dialogue ends by default"
            mDialogue->goodbyeSelected();
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), 40);
        }

        TEST_F(ManagerTest, a_barter_disposition_change_is_permanent_when_the_setting_says_so)
        {
            struct Restore
            {
                ~Restore() { Settings::game().mBarterDispositionChangeIsPermanent.set(false); }
            } restore;
            Settings::game().mBarterDispositionChangeIsPermanent.set(true);
            start(mFargoth);
            mDialogue->applyBarterDispositionChange(7);
            mDialogue->goodbyeSelected();
            EXPECT_EQ(mFargoth.getClass().getNpcStats(mFargoth).getBaseDisposition(), 47);
        }

        TEST_F(ManagerTest, a_barter_disposition_change_with_no_actor_does_nothing)
        {
            EXPECT_NO_THROW(mDialogue->applyBarterDispositionChange(7));
        }

        TEST_F(ManagerTest, a_refused_service_answers_with_the_service_refusal_topic)
        {
            start(mFargoth); // base 40: derived 50, below 60
            Responses responses;
            EXPECT_TRUE(mDialogue->checkServiceRefused(&responses, MWBase::DialogueManager::Barter));
            ASSERT_EQ(responses.mLines.size(), 1u);
            EXPECT_EQ(responses.mLines[0].mTitle, "Service refused");
            EXPECT_EQ(responses.mLines[0].mText, "I do not trade with you.");
        }

        TEST_F(ManagerTest, a_service_is_not_refused_when_the_disposition_is_high_enough_or_the_service_differs)
        {
            start(mFargoth);
            Responses responses;
            EXPECT_FALSE(mDialogue->checkServiceRefused(&responses, MWBase::DialogueManager::Repair));
            mFargoth.getClass().getNpcStats(mFargoth).setBaseDisposition(50); // derived 60
            EXPECT_FALSE(mDialogue->checkServiceRefused(&responses, MWBase::DialogueManager::Barter));
            EXPECT_TRUE(responses.mLines.empty());
        }

        TEST_F(ManagerTest, say_gives_a_voice_line_and_runs_its_script)
        {
            EXPECT_TRUE(mDialogue->say(mFargoth, rid("hit")));
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 10);
            ASSERT_EQ(mDlgLua.mResponses.size(), 1u);
            EXPECT_EQ(mDlgLua.mResponses[0].mInfo, "v_hit");
        }

        TEST_F(ManagerTest, say_shows_a_subtitle_only_when_the_setting_is_on)
        {
            struct Restore
            {
                ~Restore() { Settings::gui().mSubtitles.set(false); }
            } restore;
            mDialogue->say(mFargoth, rid("idle"));
            EXPECT_EQ(messages("La la."), 0);
            Settings::gui().mSubtitles.set(true);
            mDialogue->say(mFargoth, rid("idle"));
            EXPECT_EQ(messages("La la."), 1);
        }

        TEST_F(ManagerTest, say_does_nothing_while_the_actor_is_already_speaking)
        {
            mSound.mSayActive = true;
            EXPECT_FALSE(mDialogue->say(mFargoth, rid("hit")));
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 0);
        }

        TEST_F(ManagerTest, an_npc_does_not_speak_while_swimming_or_knocked_down)
        {
            mDlgWorld.mSwimming = true;
            EXPECT_FALSE(mDialogue->say(mFargoth, rid("hit")));
            mDlgWorld.mSwimming = false;
            mFargoth.getClass().getCreatureStats(mFargoth).setKnockedDown(true);
            EXPECT_FALSE(mDialogue->say(mFargoth, rid("hit")));
            mFargoth.getClass().getCreatureStats(mFargoth).setKnockedDown(false);
            EXPECT_TRUE(mDialogue->say(mFargoth, rid("hit")));
        }

        TEST_F(ManagerTest, say_returns_false_when_no_info_matches_the_actor)
        {
            // a creature has no response in a topic that names no actor
            EXPECT_FALSE(mDialogue->say(mRat, rid("hit")));
        }

        TEST_F(ManagerTest, faction_reactions_start_from_the_faction_record)
        {
            EXPECT_EQ(mDialogue->getFactionReaction(rid("f1"), rid("f2")), 10);
            EXPECT_EQ(mDialogue->getFactionReaction(rid("f2"), rid("f1")), 0);
            EXPECT_EQ(mDialogue->getFactionReactionOverrides(rid("f1")), nullptr);
        }

        TEST_F(ManagerTest, setting_and_changing_a_faction_reaction_keeps_an_override)
        {
            mDialogue->modFactionReaction(rid("f1"), rid("f2"), -4); // 10 - 4
            EXPECT_EQ(mDialogue->getFactionReaction(rid("f1"), rid("f2")), 6);
            mDialogue->modFactionReaction(rid("f1"), rid("f2"), -4);
            EXPECT_EQ(mDialogue->getFactionReaction(rid("f1"), rid("f2")), 2);
            mDialogue->setFactionReaction(rid("f2"), rid("f1"), 33);
            EXPECT_EQ(mDialogue->getFactionReaction(rid("f2"), rid("f1")), 33);
            const std::map<ESM::RefId, int>* overrides = mDialogue->getFactionReactionOverrides(rid("f1"));
            ASSERT_NE(overrides, nullptr);
            EXPECT_EQ(overrides->at(rid("f2")), 2);
        }

        TEST_F(ManagerTest, a_faction_reaction_of_a_faction_that_does_not_exist_throws)
        {
            EXPECT_THROW(mDialogue->modFactionReaction(rid("nosuch"), rid("f2"), 1), std::exception);
            EXPECT_THROW(mDialogue->setFactionReaction(rid("f1"), rid("nosuch"), 1), std::exception);
            EXPECT_THROW(mDialogue->getFactionReaction(rid("nosuch"), rid("f1")), std::exception);
            EXPECT_THROW(mDialogue->getFactionReactionOverrides(rid("nosuch")), std::exception);
        }

        TEST_F(ManagerTest, clearing_the_last_info_of_an_actor_removes_the_topic_response_it_gave)
        {
            start(mFargoth);
            Responses responses;
            mDialogue->keywordSelected("job", &responses);
            ASSERT_EQ(mJournal->getTopics().count(rid("job")), 1u);
            mDialogue->clearInfoActor(mSellus); // another actor: nothing
            EXPECT_EQ(mJournal->getTopics().count(rid("job")), 1u);
            mDialogue->clearInfoActor(mFargoth);
            EXPECT_EQ(mJournal->getTopics().count(rid("job")), 0u);
        }

        TEST_F(ManagerTest, the_known_topics_are_cleared_with_the_manager)
        {
            mDialogue->addTopic(rid("job"));
            mDialogue->clear();
            start(mFargoth);
            const std::list<std::string> topics = mDialogue->getAvailableTopics();
            EXPECT_EQ(std::count(topics.begin(), topics.end(), "job"), 0);
        }
    }
}
