// Task 0098, move 4, stage 2: Morrowind's script commands run by the real compiler and interpreter.
//
// In plain words: these tests give the game a small script, the way a quest does as the "result script" of a line of
// dialogue, and let the real compile table, the real handlers of the commands and the real actor classes run it, behind
// the same test doubles of the game's managers that the dialogue tests use. They then check what the script did to the
// actor or told the doubles. They pin what the current code does, so that moving the command set into the Morrowind
// game pack cannot change it. Records and numbers are test inputs, not Morrowind's data.
//
// Each test has one script because a dialogue is loaded into the store once per test.
#include <gtest/gtest.h>

#include <string>

#include <components/esm3/loadmisc.hpp>

#include "apps/openmw/mwmechanics/aisequence.hpp"
#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/npcstats.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"
#include "apps/openmw_tests/mwdialogue/dialoguefixture.hpp"

namespace DialogueTest
{
    namespace
    {
        class CommandRunTest : public DialogueRulesTest
        {
        protected:
            void SetUp() override
            {
                DialogueRulesTest::SetUp();
                addFaction("f1", {});
                mActor = addNpc("scripted", "dunmer", "thief", "f1", false, 0);
                ESM::Miscellaneous gold;
                gold.blank();
                gold.mId = rid("gold_001");
                gold.mName = "Gold";
                gold.mData.mValue = 1;
                mStore.insertStatic(gold);
            }

            // The script is the result script of the one greeting, which the actor gets when a conversation starts.
            void run(const std::string& script)
            {
                DialogueFile file;
                file.dialogue("q1", ESM::Dialogue::Journal)
                    .info({ .mId = "q1_30", .mResponse = "Thirty", .mJournalIndex = 30 });
                file.dialogue("greeting 0", ESM::Dialogue::Greeting)
                    .info({ .mId = "g_run", .mResponse = "Hi.", .mResultScript = script, .mActor = "scripted" });
                loadDialogue(file);
                Responses responses;
                ASSERT_TRUE(mDialogue->startDialogue(mActor, &responses));
            }

            MWMechanics::CreatureStats& stats() { return mActor.getClass().getCreatureStats(mActor); }

            MWWorld::Ptr mActor;
        };

        TEST_F(CommandRunTest, a_message_box_command_reaches_the_window_manager)
        {
            run("MessageBox \"hello from a script\"");
            ASSERT_EQ(mWindow.mMessages.size(), 1u);
            EXPECT_EQ(mWindow.mMessages[0], "hello from a script");
        }

        TEST_F(CommandRunTest, set_and_mod_attribute_commands_change_the_actors_base_attribute)
        {
            run("SetStrength 70\nModStrength 5\nSetAgility 20\nModAgility -3");
            EXPECT_EQ(stats().getAttribute(ESM::Attribute::Strength).getBase(), 75.f);
            EXPECT_EQ(stats().getAttribute(ESM::Attribute::Agility).getBase(), 17.f);
        }

        TEST_F(CommandRunTest, skill_and_health_commands_change_and_read_the_actor)
        {
            run("SetBlock 40\nModBlock 7\nSetHealth 80\nif ( GetBlock == 47 )\n MessageBox \"block\"\nendif\n"
                "if ( GetHealth == 80 )\n MessageBox \"health\"\nendif");
            ASSERT_EQ(mWindow.mMessages.size(), 2u);
            EXPECT_EQ(mWindow.mMessages[0], "block");
            EXPECT_EQ(mWindow.mMessages[1], "health");
            EXPECT_EQ(stats().getHealth().getBase(), 80.f);
        }

        TEST_F(CommandRunTest, set_level_changes_the_level)
        {
            run("SetLevel 12");
            EXPECT_EQ(stats().getLevel(), 12);
        }

        TEST_F(CommandRunTest, functions_in_a_condition_read_the_actor_and_the_world)
        {
            // strength is 50, level 5; the player is in no cell, so GetInterior gives 0
            run("if ( GetStrength == 50 )\n MessageBox \"strength\"\nendif\n"
                "if ( GetLevel == 5 )\n MessageBox \"level\"\nendif\n"
                "if ( GetInterior == 0 )\n MessageBox \"exterior\"\nendif\n"
                "if ( GetStrength == 51 )\n MessageBox \"wrong\"\nendif");
            ASSERT_EQ(mWindow.mMessages.size(), 3u);
            EXPECT_EQ(mWindow.mMessages[0], "strength");
            EXPECT_EQ(mWindow.mMessages[1], "level");
            EXPECT_EQ(mWindow.mMessages[2], "exterior");
        }

        TEST_F(CommandRunTest, add_item_and_remove_item_change_the_actors_inventory)
        {
            run("AddItem \"gold_001\" 12\nRemoveItem \"gold_001\" 5");
            EXPECT_EQ(mActor.getClass().getContainerStore(mActor).count(rid("gold_001")), 7);
        }

        TEST_F(CommandRunTest, get_item_count_reads_the_inventory)
        {
            run("AddItem \"gold_001\" 4\nif ( GetItemCount \"gold_001\" == 4 )\n MessageBox \"four\"\nendif");
            ASSERT_EQ(mWindow.mMessages.size(), 1u);
            EXPECT_EQ(mWindow.mMessages[0], "four");
        }

        TEST_F(CommandRunTest, journal_command_reaches_the_journal)
        {
            run("Journal \"q1\", 30");
            EXPECT_EQ(mJournal->getJournalIndex(rid("q1")), 30);
        }

        TEST_F(CommandRunTest, say_hands_the_sound_file_to_the_sound_manager)
        {
            run("Say \"vo\\x.mp3\" \"text\"");
            ASSERT_EQ(mSound.mSaid.size(), 1u);
            EXPECT_EQ(mSound.mSaid[0], "sound/vo/x.mp3");
        }

        TEST_F(CommandRunTest, an_unknown_command_compiles_to_nothing)
        {
            run("MessageBox \"before\"\nThisCommandDoesNotExist 3\nMessageBox \"after\"");
            EXPECT_TRUE(mWindow.mMessages.empty());
        }

        TEST_F(CommandRunTest, console_only_commands_are_not_available_to_a_dialogue_script)
        {
            run("MessageBox \"before\"\nUser1\nMessageBox \"after\"");
            EXPECT_TRUE(mWindow.mMessages.empty());
        }

        TEST_F(CommandRunTest, ai_wander_puts_a_package_on_the_actor)
        {
            run("AiWander 100 5 0 10");
            EXPECT_FALSE(stats().getAiSequence().isEmpty());
        }
    }
}
