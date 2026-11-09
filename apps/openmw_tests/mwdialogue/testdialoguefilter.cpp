// Task 0085, move 3, stage 3b: characterization tests of the dialogue filter (apps/openmw/mwdialogue/filter.*,
// selectwrapper.*): which response an actor gives for a topic, given the actor, the player and the world. They run
// the REAL Filter on generated dialogue records and pin what the code does today.
//
// The doubles: the derived disposition of an NPC is 10 plus its base disposition (DlgMechanics); the world has the
// globals the test sets; everything else is the real NPC and creature classes.
#include "dialoguefixture.hpp"

#include "apps/openmw/mwdialogue/filter.hpp"
#include "apps/openmw/mwmechanics/npcstats.hpp"

namespace DialogueTest
{
    namespace
    {
        using F = ESM::DialogueCondition;

        class FilterTest : public DialogueRulesTest
        {
        protected:
            void SetUp() override
            {
                DialogueRulesTest::SetUp();
                addFaction("f1", {});
                addFaction("f2", {});
                mFargoth = addNpc("fargoth", "bosmer", "thief", "f1", false, 2);
                mSellus = addNpc("sellus", "dunmer", "trader", "", true);
                mRat = addCreature("rat");
                // a quest and an item for the Journal and Item conditions
                DialogueFile file;
                file.dialogue("q1", ESM::Dialogue::Journal)
                    .info({ .mId = "q1_name",
                        .mResponse = "Quest One",
                        .mJournalIndex = 1,
                        .mQuestStatus = ESM::DialInfo::QS_Name })
                    .info({ .mId = "q1_10", .mResponse = "Ten", .mJournalIndex = 10 });
                file.dialogue("info refusal", ESM::Dialogue::Topic).info({ .mId = "ir1", .mResponse = "Refused" });
                add(file);
                loadDialogue(file);
            }

            /// Hook for a test to add its own topics before the store is set up: the test writes them into a file
            /// and loads it itself (a store may be set up once, so a test loads one file only).
            virtual void add(DialogueFile&) {}

            std::string said(const MWWorld::Ptr& actor, const std::string& topic, int choice = -1, bool talked = false,
                bool fallback = false)
            {
                MWDialogue::Filter filter(actor, choice, talked);
                const ESM::Dialogue* dialogue = mStore.get<ESM::Dialogue>().find(rid(topic));
                const auto [owner, info] = filter.search(*dialogue, fallback);
                return info == nullptr ? std::string("<none>") : info->mResponse;
            }

            /// The player joins a faction and is given a rank (setFactionRank alone ignores a faction not yet joined).
            void joinPlayer(const char* faction, int rank)
            {
                stats().joinFaction(rid(faction));
                stats().setFactionRank(rid(faction), rank);
            }

            MWWorld::Ptr mFargoth, mSellus, mRat;
        };

        // Each test body is a fixture of its own, so that each can add the topics it asks about.
        class ActorFilterTest : public FilterTest
        {
        protected:
            void add(DialogueFile& file) override
            {
                file.dialogue("t_actor", ESM::Dialogue::Topic)
                    .info({ .mId = "a1", .mResponse = "for sellus", .mActor = "sellus" })
                    .info({ .mId = "a2", .mResponse = "for everybody" });
                file.dialogue("t_race", ESM::Dialogue::Topic)
                    .info({ .mId = "r1", .mResponse = "for dunmer", .mRace = "dunmer" })
                    .info({ .mId = "r2", .mResponse = "for others" });
                file.dialogue("t_class", ESM::Dialogue::Topic)
                    .info({ .mId = "c1", .mResponse = "for thieves", .mClass = "thief" })
                    .info({ .mId = "c2", .mResponse = "for others" });
                file.dialogue("t_faction", ESM::Dialogue::Topic)
                    .info({ .mId = "f1", .mResponse = "for f1 rank 3", .mFaction = "f1", .mRank = 3 })
                    .info({ .mId = "f2", .mResponse = "for f1 rank 2", .mFaction = "f1", .mRank = 2 })
                    .info({ .mId = "f3", .mResponse = "for f2", .mFaction = "f2" })
                    .info({ .mId = "f4", .mResponse = "for the factionless", .mFactionLess = true })
                    .info({ .mId = "f5", .mResponse = "anyone" });
                file.dialogue("t_rank_only", ESM::Dialogue::Topic)
                    .info({ .mId = "k1", .mResponse = "rank 1 of own faction", .mRank = 1 })
                    .info({ .mId = "k2", .mResponse = "anyone" });
                file.dialogue("t_gender", ESM::Dialogue::Topic)
                    .info({ .mId = "g1", .mResponse = "males only", .mGender = 0 })
                    .info({ .mId = "g2", .mResponse = "females only", .mGender = 1 });
            }
        };

        TEST_F(ActorFilterTest, an_info_naming_an_actor_is_for_that_actor_only)
        {
            EXPECT_EQ(said(mSellus, "t_actor"), "for sellus");
            EXPECT_EQ(said(mFargoth, "t_actor"), "for everybody");
        }

        TEST_F(ActorFilterTest, a_creature_gets_only_infos_naming_it)
        {
            // a2 has no actor: "Creatures must not have topics aside of those specific to their id"
            EXPECT_EQ(said(mRat, "t_actor"), "<none>");
            EXPECT_EQ(said(mRat, "t_race"), "<none>");
        }

        TEST_F(ActorFilterTest, race_and_class_filters_compare_with_the_npc_record)
        {
            EXPECT_EQ(said(mSellus, "t_race"), "for dunmer");
            EXPECT_EQ(said(mFargoth, "t_race"), "for others");
            EXPECT_EQ(said(mFargoth, "t_class"), "for thieves");
            EXPECT_EQ(said(mSellus, "t_class"), "for others");
        }

        TEST_F(ActorFilterTest, a_faction_info_needs_the_actors_faction_and_at_least_the_rank)
        {
            // fargoth: faction f1, rank 2: rank 3 fails, rank 2 matches
            EXPECT_EQ(said(mFargoth, "t_faction"), "for f1 rank 2");
        }

        TEST_F(ActorFilterTest, the_factionless_flag_matches_only_actors_without_a_faction)
        {
            // sellus has no faction: f1..f3 fail on the faction, f4 matches
            EXPECT_EQ(said(mSellus, "t_faction"), "for the factionless");
        }

        TEST_F(ActorFilterTest, a_rank_without_a_faction_uses_the_actors_own_faction)
        {
            EXPECT_EQ(said(mFargoth, "t_rank_only"), "rank 1 of own faction"); // rank 2 >= 1
            // sellus has no faction: her rank is -1, below 1
            EXPECT_EQ(said(mSellus, "t_rank_only"), "anyone");
        }

        TEST_F(ActorFilterTest, a_gender_zero_info_is_for_males_and_one_for_females)
        {
            EXPECT_EQ(said(mFargoth, "t_gender"), "males only");
            EXPECT_EQ(said(mSellus, "t_gender"), "females only");
        }

        class PlayerFilterTest : public FilterTest
        {
        protected:
            void add(DialogueFile& file) override
            {
                file.dialogue("t_pcfaction", ESM::Dialogue::Topic)
                    .info({ .mId = "p1", .mResponse = "f2 member rank 2", .mPcFaction = "f2", .mPcRank = 2 })
                    .info({ .mId = "p2", .mResponse = "f2 member", .mPcFaction = "f2" })
                    .info({ .mId = "p3", .mResponse = "pc rank 1 in the speaker's faction", .mPcRank = 1 })
                    .info({ .mId = "p4", .mResponse = "anyone" });
                file.dialogue("t_cell", ESM::Dialogue::Topic)
                    .info({ .mId = "l1", .mResponse = "in balmora", .mCell = "Balmora" })
                    .info({ .mId = "l2", .mResponse = "elsewhere" });
            }
        };

        TEST_F(PlayerFilterTest, the_player_faction_and_rank_are_checked)
        {
            EXPECT_EQ(said(mSellus, "t_pcfaction"), "anyone");
            joinPlayer("f2", 1);
            EXPECT_EQ(said(mSellus, "t_pcfaction"), "f2 member");
            joinPlayer("f2", 2);
            EXPECT_EQ(said(mSellus, "t_pcfaction"), "f2 member rank 2");
        }

        TEST_F(PlayerFilterTest, a_player_rank_without_a_faction_uses_the_speakers_faction)
        {
            // fargoth is in f1; the player has no rank there
            EXPECT_EQ(said(mFargoth, "t_pcfaction"), "anyone");
            joinPlayer("f1", 0);
            EXPECT_EQ(said(mFargoth, "t_pcfaction"), "anyone"); // 0 < 1
            joinPlayer("f1", 1);
            EXPECT_EQ(said(mFargoth, "t_pcfaction"), "pc rank 1 in the speaker's faction");
        }

        TEST_F(PlayerFilterTest, the_cell_name_matches_by_prefix_without_regard_to_case)
        {
            mDlgWorld.mCellName = "Balmora, Guild of Mages";
            EXPECT_EQ(said(mSellus, "t_cell"), "in balmora");
            mDlgWorld.mCellName = "Seyda Neen";
            EXPECT_EQ(said(mSellus, "t_cell"), "elsewhere");
            mDlgWorld.mCellName = "BALMORA";
            EXPECT_EQ(said(mSellus, "t_cell"), "in balmora");
        }

        class DispositionFilterTest : public FilterTest
        {
        protected:
            void add(DialogueFile& file) override
            {
                file.dialogue("t_disp", ESM::Dialogue::Topic)
                    .info({ .mId = "d1", .mResponse = "friendly", .mDisposition = 60 })
                    .info({ .mId = "d2", .mResponse = "neutral", .mDisposition = 30 });
                file.dialogue("t_only_friendly", ESM::Dialogue::Topic)
                    .info({ .mId = "o1", .mResponse = "only if friendly", .mDisposition = 90 });
            }
        };

        TEST_F(DispositionFilterTest, an_info_needs_the_derived_disposition_to_reach_its_own)
        {
            mSellus.getClass().getNpcStats(mSellus).setBaseDisposition(50); // derived 60
            EXPECT_EQ(said(mSellus, "t_disp"), "friendly");
            mSellus.getClass().getNpcStats(mSellus).setBaseDisposition(49); // derived 59
            EXPECT_EQ(said(mSellus, "t_disp"), "neutral");
            mSellus.getClass().getNpcStats(mSellus).setBaseDisposition(19); // derived 29
            EXPECT_EQ(said(mSellus, "t_disp"), "<none>");
        }

        TEST_F(DispositionFilterTest, too_low_a_disposition_falls_back_to_info_refusal_only_when_asked)
        {
            mSellus.getClass().getNpcStats(mSellus).setBaseDisposition(0);
            EXPECT_EQ(said(mSellus, "t_only_friendly", -1, false, false), "<none>");
            MWDialogue::Filter filter(mSellus, -1, false);
            const ESM::Dialogue& dialogue = *mStore.get<ESM::Dialogue>().find(rid("t_only_friendly"));
            const auto [owner, info] = filter.search(dialogue, true);
            ASSERT_NE(info, nullptr);
            EXPECT_EQ(info->mResponse, "Refused");
            EXPECT_EQ(owner->mId, rid("info refusal"));
        }

        TEST_F(DispositionFilterTest, a_creature_ignores_disposition)
        {
            // creatures are only matched by their own id; the topic has no actor, so there is nothing, but the
            // disposition test itself passes for a creature (see the list below)
            MWDialogue::Filter filter(mRat, -1, false);
            const ESM::Dialogue& dialogue = *mStore.get<ESM::Dialogue>().find(rid("t_only_friendly"));
            EXPECT_TRUE(filter.list(dialogue, false, true).empty());
        }

        TEST_F(DispositionFilterTest, list_with_search_all_returns_every_match_in_record_order)
        {
            mSellus.getClass().getNpcStats(mSellus).setBaseDisposition(60);
            MWDialogue::Filter filter(mSellus, -1, false);
            const ESM::Dialogue& dialogue = *mStore.get<ESM::Dialogue>().find(rid("t_disp"));
            const auto all = filter.list(dialogue, false, true);
            ASSERT_EQ(all.size(), 2u);
            EXPECT_EQ(all[0].second->mId, rid("d1"));
            EXPECT_EQ(all[1].second->mId, rid("d2"));
            EXPECT_EQ(filter.list(dialogue, false, false).size(), 1u);
        }

        TEST_F(DispositionFilterTest, an_inverted_disposition_test_keeps_the_infos_the_actor_does_not_reach)
        {
            mSellus.getClass().getNpcStats(mSellus).setBaseDisposition(40); // derived 50
            MWDialogue::Filter filter(mSellus, -1, false);
            const ESM::Dialogue& dialogue = *mStore.get<ESM::Dialogue>().find(rid("t_disp"));
            const auto inverted = filter.list(dialogue, false, true, true);
            ASSERT_EQ(inverted.size(), 1u);
            EXPECT_EQ(inverted[0].second->mId, rid("d1")); // 50 < 60; the 30 info is reached, so not listed
        }

        class ConditionFilterTest : public FilterTest
        {
        protected:
            static InfoSpec with(const char* id, const char* text, F condition)
            {
                InfoSpec spec;
                spec.mId = id;
                spec.mResponse = text;
                spec.mSelects = { condition };
                return spec;
            }

            void add(DialogueFile& file) override
            {
                // one topic per condition kind: [the conditional info, a fallback "no"]
                auto topic = [&](const char* id, F condition) {
                    file.dialogue(id, ESM::Dialogue::Topic)
                        .info(with((std::string(id) + "_y").c_str(), "yes", condition))
                        .info({ .mId = std::string(id) + "_n", .mResponse = "no" });
                };
                topic("c_journal_eq", condition(F::Function_Journal, F::Comp_Eq, 10, "q1"));
                topic("c_journal_ne", condition(F::Function_Journal, F::Comp_Ne, 10, "q1"));
                topic("c_journal_gt", condition(F::Function_Journal, F::Comp_Gt, 10, "q1"));
                topic("c_journal_ge", condition(F::Function_Journal, F::Comp_Ge, 10, "q1"));
                topic("c_journal_ls", condition(F::Function_Journal, F::Comp_Ls, 10, "q1"));
                topic("c_journal_le", condition(F::Function_Journal, F::Comp_Le, 10, "q1"));
                topic("c_global", condition(F::Function_Global, F::Comp_Gt, 5, "mygold"));
                topic("c_unknown_global", condition(F::Function_Global, F::Comp_Gt, 5, "noglobal"));
                topic("c_item", condition(F::Function_Item, F::Comp_Ge, 3, "iron_dagger"));
                topic("c_dead", condition(F::Function_Dead, F::Comp_Eq, 2, "rat"));
                topic("c_choice", condition(F::Function_Choice, F::Comp_Eq, 1));
                topic("c_pclevel", condition(F::Function_PcLevel, F::Comp_Ge, 2));
                topic("c_level", condition(F::Function_Level, F::Comp_Eq, 5));
                topic("c_pcgender", condition(F::Function_PcGender, F::Comp_Eq, 0));
                topic("c_weather", condition(F::Function_Weather, F::Comp_Eq, 0));
                topic("c_talked", condition(F::Function_TalkedToPc, F::Comp_Eq, 1));
                topic("c_local", condition(F::Function_Local, F::Comp_Eq, 1, "myvar"));
                topic("c_notlocal", condition(F::Function_NotLocal, F::Comp_Eq, 1, "myvar"));
                topic("c_notid", condition(F::Function_NotId, F::Comp_Eq, 1, "fargoth"));
                topic("c_notrace", condition(F::Function_NotRace, F::Comp_Eq, 1, "dunmer"));
                topic("c_notclass", condition(F::Function_NotClass, F::Comp_Eq, 1, "thief"));
                topic("c_notfaction", condition(F::Function_NotFaction, F::Comp_Eq, 1, "f1"));
                topic("c_samesex", condition(F::Function_SameSex, F::Comp_Eq, 1));
                topic("c_samerace", condition(F::Function_SameRace, F::Comp_Eq, 1));
                topic("c_samefaction", condition(F::Function_SameFaction, F::Comp_Eq, 1));
                topic("c_werewolf", condition(F::Function_Werewolf, F::Comp_Eq, 1));
                topic("c_pcstrength", condition(F::Function_PcStrength, F::Comp_Ge, 50));
                topic("c_pcsneak", condition(F::Function_PcSneak, F::Comp_Eq, 30));
                topic("c_reputation", condition(F::Function_PcReputation, F::Comp_Ge, 7));
                topic("c_pccrime", condition(F::Function_PcCrimeLevel, F::Comp_Gt, 0));
                topic("c_facreaction_high", condition(F::Function_FacReactionHighest, F::Comp_Gt, 0));
                topic("c_detected", condition(F::Function_Detected, F::Comp_Eq, 1));
                topic("c_hello", condition(F::Function_Hello, F::Comp_Ge, 50));
                topic("c_unknown_fn", condition(F::Function_PcWerewolfKills, F::Comp_Eq, 0));
            }
        };

        TEST_F(ConditionFilterTest, journal_conditions_compare_the_quest_index_with_all_six_operators)
        {
            for (int index : { 0, 9, 10, 11 })
            {
                mJournal->setJournalIndex(rid("q1"), index);
                const std::string at = " at index " + std::to_string(index);
                EXPECT_EQ(said(mSellus, "c_journal_eq"), index == 10 ? "yes" : "no") << at;
                EXPECT_EQ(said(mSellus, "c_journal_ne"), index != 10 ? "yes" : "no") << at;
                EXPECT_EQ(said(mSellus, "c_journal_gt"), index > 10 ? "yes" : "no") << at;
                EXPECT_EQ(said(mSellus, "c_journal_ge"), index >= 10 ? "yes" : "no") << at;
                EXPECT_EQ(said(mSellus, "c_journal_ls"), index < 10 ? "yes" : "no") << at;
                EXPECT_EQ(said(mSellus, "c_journal_le"), index <= 10 ? "yes" : "no") << at;
            }
        }

        TEST_F(ConditionFilterTest, a_global_condition_compares_the_float_and_ignores_a_global_that_is_missing)
        {
            mDlgWorld.mGlobals["mygold"] = 4.f;
            EXPECT_EQ(said(mSellus, "c_global"), "no");
            mDlgWorld.mGlobals["mygold"] = 5.5f;
            EXPECT_EQ(said(mSellus, "c_global"), "yes");
            // "ignore this filter if the global doesn't exist": the condition passes
            EXPECT_EQ(said(mSellus, "c_unknown_global"), "yes");
        }

        TEST_F(ConditionFilterTest, an_item_condition_counts_the_players_stack)
        {
            // no item record: the count is read from the container and is zero
            EXPECT_EQ(said(mSellus, "c_item"), "no");
        }

        TEST_F(ConditionFilterTest, a_dead_condition_asks_the_mechanics_manager_for_the_death_count)
        {
            mDlgMechanics.mDeaths["rat"] = 1;
            EXPECT_EQ(said(mSellus, "c_dead"), "no");
            mDlgMechanics.mDeaths["rat"] = 2;
            EXPECT_EQ(said(mSellus, "c_dead"), "yes");
        }

        TEST_F(ConditionFilterTest, a_choice_condition_fails_outside_a_choice_and_otherwise_compares_it)
        {
            EXPECT_EQ(said(mSellus, "c_choice", -1), "no");
            EXPECT_EQ(said(mSellus, "c_choice", 1), "yes");
            EXPECT_EQ(said(mSellus, "c_choice", 2), "no");
        }

        TEST_F(ConditionFilterTest, player_and_actor_numbers_come_from_their_stats)
        {
            stats().setLevel(1);
            EXPECT_EQ(said(mSellus, "c_pclevel"), "no");
            stats().setLevel(2);
            EXPECT_EQ(said(mSellus, "c_pclevel"), "yes");
            EXPECT_EQ(said(mSellus, "c_level"), "yes"); // the generated NPCs are level 5
            EXPECT_EQ(said(mSellus, "c_pcgender"), "yes"); // the player is male
            EXPECT_EQ(said(mSellus, "c_pcstrength"), "yes"); // attribute 50
            EXPECT_EQ(said(mSellus, "c_pcsneak"), "yes"); // skill 30
        }

        TEST_F(ConditionFilterTest, the_player_reputation_and_bounty_come_from_the_npc_stats)
        {
            EXPECT_EQ(said(mSellus, "c_reputation"), "no");
            stats().setReputation(7);
            EXPECT_EQ(said(mSellus, "c_reputation"), "yes");
            EXPECT_EQ(said(mSellus, "c_pccrime"), "no");
            stats().setBounty(1);
            EXPECT_EQ(said(mSellus, "c_pccrime"), "yes");
        }

        TEST_F(ConditionFilterTest, weather_conditions_are_rejected_in_interiors)
        {
            mDlgWorld.mWeather = 0;
            EXPECT_EQ(said(mSellus, "c_weather"), "yes");
            mDlgWorld.mExterior = false;
            EXPECT_EQ(said(mSellus, "c_weather"), "no");
        }

        TEST_F(ConditionFilterTest, the_talked_flag_is_the_filters_own_argument)
        {
            EXPECT_EQ(said(mSellus, "c_talked", -1, false), "no");
            EXPECT_EQ(said(mSellus, "c_talked", -1, true), "yes");
        }

        TEST_F(ConditionFilterTest, local_conditions_need_a_script_with_the_variable)
        {
            // no actor in this fixture has a script: Local fails and NotLocal passes
            EXPECT_EQ(said(mSellus, "c_local"), "no");
            EXPECT_EQ(said(mSellus, "c_notlocal"), "yes");
        }

        TEST_F(ConditionFilterTest, the_not_conditions_compare_with_the_actor_record)
        {
            EXPECT_EQ(said(mFargoth, "c_notid"), "no");
            EXPECT_EQ(said(mSellus, "c_notid"), "yes");
            EXPECT_EQ(said(mSellus, "c_notrace"), "no");
            EXPECT_EQ(said(mFargoth, "c_notrace"), "yes");
            EXPECT_EQ(said(mFargoth, "c_notclass"), "no");
            EXPECT_EQ(said(mSellus, "c_notclass"), "yes");
            EXPECT_EQ(said(mFargoth, "c_notfaction"), "no");
            EXPECT_EQ(said(mSellus, "c_notfaction"), "yes");
        }

        TEST_F(ConditionFilterTest, the_same_conditions_compare_the_player_with_the_actor)
        {
            // the player (testrace, male, no faction) against sellus (dunmer, female) and fargoth (bosmer, male, f1)
            EXPECT_EQ(said(mFargoth, "c_samesex"), "yes");
            EXPECT_EQ(said(mSellus, "c_samesex"), "no");
            EXPECT_EQ(said(mSellus, "c_samerace"), "no");
            joinPlayer("f1", 0);
            EXPECT_EQ(said(mFargoth, "c_samefaction"), "yes");
            EXPECT_EQ(said(mSellus, "c_samefaction"), "no");
        }

        TEST_F(ConditionFilterTest, a_creature_has_no_race_class_or_sex_conditions_to_pass_and_is_not_a_werewolf)
        {
            EXPECT_EQ(said(mSellus, "c_werewolf"), "no");
        }

        TEST_F(ConditionFilterTest, the_awareness_check_asks_the_mechanics_manager)
        {
            mDlgMechanics.mAware = true;
            EXPECT_EQ(said(mSellus, "c_detected"), "yes");
            mDlgMechanics.mAware = false;
            EXPECT_EQ(said(mSellus, "c_detected"), "no");
        }

        TEST_F(ConditionFilterTest, the_highest_faction_reaction_looks_at_the_players_factions)
        {
            // fargoth is in f1; the player is in no faction, so the value stays at its start, 0
            EXPECT_EQ(said(mFargoth, "c_facreaction_high"), "no");
            joinPlayer("f2", 0);
            mDialogue->setFactionReaction(rid("f1"), rid("f2"), 12);
            EXPECT_EQ(said(mFargoth, "c_facreaction_high"), "yes");
        }

        TEST_F(ConditionFilterTest, an_ai_setting_condition_reads_the_actors_setting)
        {
            // argument index 0 is hello
            mSellus.getClass().getCreatureStats(mSellus).setAiSetting(MWMechanics::AiSetting::Hello, 60);
            EXPECT_EQ(said(mSellus, "c_hello"), "yes");
            mSellus.getClass().getCreatureStats(mSellus).setAiSetting(MWMechanics::AiSetting::Hello, 40);
            EXPECT_EQ(said(mSellus, "c_hello"), "no");
        }

        TEST_F(ConditionFilterTest, the_first_matching_info_wins_and_a_failing_condition_falls_through)
        {
            mJournal->setJournalIndex(rid("q1"), 10);
            EXPECT_EQ(said(mSellus, "c_journal_eq"), "yes");
            mJournal->setJournalIndex(rid("q1"), 0);
            EXPECT_EQ(said(mSellus, "c_journal_eq"), "no");
        }

        TEST_F(ConditionFilterTest, could_potentially_match_ignores_runtime_state_but_not_the_actor)
        {
            const ESM::Dialogue& dialogue = *mStore.get<ESM::Dialogue>().find(rid("c_notid"));
            MWDialogue::Filter fargoth(mFargoth, -1, false);
            MWDialogue::Filter sellus(mSellus, -1, false);
            const ESM::DialInfo& conditional = dialogue.mInfo.front();
            EXPECT_FALSE(fargoth.couldPotentiallyMatch(conditional)); // NotId fargoth: a static filter
            EXPECT_TRUE(sellus.couldPotentiallyMatch(conditional));
            const ESM::Dialogue& journal = *mStore.get<ESM::Dialogue>().find(rid("c_journal_eq"));
            EXPECT_TRUE(fargoth.couldPotentiallyMatch(journal.mInfo.front())); // journal: runtime state, ignored
        }
    }
}
