// Task 0098, move 5, stage 2: characterization tests of the progression rules (difficulty scaling of damage, levelled
// lists, the spell autocalc of NPCs and of a new player). They pass on the current code; the move (stage 3) must keep
// them green and unedited. See rulesfixture.hpp for the fixture, the cached GMSTs and the draw counting.
#include <algorithm>
#include <set>
#include <sstream>

#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/settings/values.hpp>

#include "apps/openmw/mwmechanics/autocalcspell.hpp"
#include "apps/openmw/mwmechanics/difficultyscaling.hpp"
#include "apps/openmw/mwmechanics/levelledlist.hpp"
#include "apps/openmw/mwmechanics/spellutil.hpp"

#include "rulesfixture.hpp"

namespace CraftingTest
{
    namespace
    {
        // ------------------------------------------------------------------------------------------------ difficulty

        class RulesDifficultyTest : public RulesTest
        {
        protected:
            void SetUp() override
            {
                RulesTest::SetUp();
                mOldDifficulty = Settings::game().mDifficulty.get();
                mOther = makeNpc("test_other");
                mOther2 = makeNpc("test_other2");
            }
            void TearDown() override
            {
                Settings::game().mDifficulty.set(mOldDifficulty);
                mOther = MWWorld::Ptr();
                mOther2 = MWWorld::Ptr();
                RulesTest::TearDown();
            }
            float scaled(int difficulty, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim)
            {
                Settings::game().mDifficulty.set(difficulty);
                return scaleDamage(100.f, attacker, victim);
            }
            int mOldDifficulty = 0;
            MWWorld::Ptr mOther;
            MWWorld::Ptr mOther2;
        };

        // fDifficultyMult is 5 here. term = 0.01 * difficulty.
        TEST_F(RulesDifficultyTest, damage_to_the_player_follows_the_difficulty)
        {
            EXPECT_FLOAT_EQ(scaled(50, mOther, mPlayer), 100.f * (1 + 5.f * 0.5f));
            EXPECT_FLOAT_EQ(scaled(100, mOther, mPlayer), 100.f * (1 + 5.f * 1.f));
            EXPECT_FLOAT_EQ(scaled(-50, mOther, mPlayer), 100.f * (1 + -0.5f / 5.f));
            EXPECT_FLOAT_EQ(scaled(-100, mOther, mPlayer), 100.f * (1 + -1.f / 5.f));
        }

        TEST_F(RulesDifficultyTest, damage_by_the_player_follows_the_difficulty_the_other_way)
        {
            EXPECT_FLOAT_EQ(scaled(50, mPlayer, mOther), 100.f * (1 + -0.5f / 5.f));
            EXPECT_FLOAT_EQ(scaled(100, mPlayer, mOther), 100.f * (1 + -1.f / 5.f));
            EXPECT_FLOAT_EQ(scaled(-50, mPlayer, mOther), 100.f * (1 + 5.f * 0.5f));
            EXPECT_FLOAT_EQ(scaled(-100, mPlayer, mOther), 100.f * (1 + 5.f * 1.f));
        }

        TEST_F(RulesDifficultyTest, neither_the_victim_nor_the_attacker_being_the_player_leaves_the_damage)
        {
            for (int difficulty : { -100, -50, 0, 50, 100 })
                EXPECT_FLOAT_EQ(scaled(difficulty, mOther, mOther2), 100.f) << difficulty;
        }

        TEST_F(RulesDifficultyTest, difficulty_zero_leaves_the_damage_alone)
        {
            EXPECT_FLOAT_EQ(scaled(0, mOther, mPlayer), 100.f);
            EXPECT_FLOAT_EQ(scaled(0, mPlayer, mOther), 100.f);
        }

        TEST_F(RulesDifficultyTest, the_victim_being_the_player_wins_when_the_player_hits_themselves)
        {
            // victim == player is tested first in the code.
            EXPECT_FLOAT_EQ(scaled(50, mPlayer, mPlayer), 100.f * (1 + 5.f * 0.5f));
        }

        TEST_F(RulesDifficultyTest, the_damage_is_scaled_linearly)
        {
            Settings::game().mDifficulty.set(50);
            EXPECT_FLOAT_EQ(scaleDamage(0.f, mOther, mPlayer), 0.f);
            EXPECT_FLOAT_EQ(scaleDamage(10.f, mOther, mPlayer), 35.f);
            EXPECT_FLOAT_EQ(scaleDamage(20.f, mOther, mPlayer), 70.f);
        }

        // ---------------------------------------------------------------------------------------------- levelled lists

        class RulesLevelledTest : public RulesTest
        {
        protected:
            void SetUp() override
            {
                RulesTest::SetUp();
                addMisc("lev_a", 1);
                addMisc("lev_b", 2);
                addMisc("lev_c", 3);
                stats().setLevel(5);
            }

            void addList(const std::string& id, int flags, int chanceNone,
                const std::vector<std::pair<std::string, int>>& items, bool creature = false)
            {
                if (creature)
                {
                    ESM::CreatureLevList list;
                    list.blank();
                    list.mId = rid(id);
                    list.mFlags = flags;
                    list.mChanceNone = static_cast<unsigned char>(chanceNone);
                    for (const auto& [item, level] : items)
                        list.mList.push_back({ rid(item), static_cast<uint16_t>(level) });
                    mStore.insertStatic(list);
                }
                else
                {
                    ESM::ItemLevList list;
                    list.blank();
                    list.mId = rid(id);
                    list.mFlags = flags;
                    list.mChanceNone = static_cast<unsigned char>(chanceNone);
                    for (const auto& [item, level] : items)
                        list.mList.push_back({ rid(item), static_cast<uint16_t>(level) });
                    mStore.insertStatic(list);
                }
            }

            const ESM::ItemLevList& itemList(const std::string& id)
            {
                return *mStore.get<ESM::ItemLevList>().find(rid(id));
            }
            const ESM::CreatureLevList& creatureList(const std::string& id)
            {
                return *mStore.get<ESM::CreatureLevList>().find(rid(id));
            }

            /// Every result over many seeds.
            std::set<std::string> results(
                const ESM::LevelledListBase& list, bool creature, std::optional<int> level = {}, int seeds = 300)
            {
                std::set<std::string> found;
                for (int i = 1; i <= seeds; ++i)
                {
                    // small seeds give small first outputs (minstd), so spread them
                    const unsigned seed = static_cast<unsigned>(i) * 2654435761u % 2147483646u + 1u;
                    Misc::Rng::Generator prng(seed);
                    found.insert(MWMechanics::getLevelledItem(&list, creature, prng, level).getRefIdString());
                }
                return found;
            }
        };

        TEST_F(RulesLevelledTest, only_the_highest_level_entries_at_or_below_the_players_level_are_chosen)
        {
            // player level 5; entries 1, 3 and 7: the highest not above 5 is 3.
            addList("list_highest", 0, 0, { { "lev_a", 1 }, { "lev_b", 3 }, { "lev_c", 7 } });
            EXPECT_EQ(results(itemList("list_highest"), false), std::set<std::string>{ "lev_b" });
        }

        TEST_F(RulesLevelledTest, the_all_levels_flag_takes_every_entry_at_or_below_the_players_level)
        {
            addList("list_all", ESM::ItemLevList::AllLevels, 0, { { "lev_a", 1 }, { "lev_b", 3 }, { "lev_c", 7 } });
            EXPECT_EQ(results(itemList("list_all"), false), (std::set<std::string>{ "lev_a", "lev_b" }));
        }

        TEST_F(RulesLevelledTest, an_explicit_level_replaces_the_players)
        {
            addList("list_level", 0, 0, { { "lev_a", 1 }, { "lev_b", 3 }, { "lev_c", 7 } });
            EXPECT_EQ(results(itemList("list_level"), false, 1), std::set<std::string>{ "lev_a" });
            EXPECT_EQ(results(itemList("list_level"), false, 7), std::set<std::string>{ "lev_c" });
            EXPECT_EQ(results(itemList("list_level"), false, 0), std::set<std::string>{ "" })
                << "nothing is at or below level 0";
        }

        TEST_F(RulesLevelledTest, creature_lists_use_their_own_all_levels_bit)
        {
            // The "all levels" bit is 0x02 in an item list and 0x01 in a creature list ("the flags are swapped"); the
            // function takes the right bit from the `creature` argument.
            static_assert(ESM::ItemLevList::AllLevels == 0x02 && ESM::CreatureLevList::AllLevels == 0x01);
            addList("clist_all", 1, 0, { { "lev_a", 1 }, { "lev_b", 3 } }, true);
            EXPECT_EQ(results(creatureList("clist_all"), true), (std::set<std::string>{ "lev_a", "lev_b" }));
            addList("clist_bit2", 2, 0, { { "lev_a", 1 }, { "lev_b", 3 } }, true);
            EXPECT_EQ(results(creatureList("clist_bit2"), true), std::set<std::string>{ "lev_b" });
            addList("list_bit1", 1, 0, { { "lev_a", 1 }, { "lev_b", 3 } });
            EXPECT_EQ(results(itemList("list_bit1"), false), std::set<std::string>{ "lev_b" });
            addList("list_bit2", 2, 0, { { "lev_a", 1 }, { "lev_b", 3 } });
            EXPECT_EQ(results(itemList("list_bit2"), false), (std::set<std::string>{ "lev_a", "lev_b" }));
        }

        TEST_F(RulesLevelledTest, the_chance_none_makes_the_result_empty)
        {
            addList("list_never", ESM::ItemLevList::AllLevels, 100, { { "lev_a", 1 } });
            EXPECT_EQ(results(itemList("list_never"), false), std::set<std::string>{ "" });

            addList("list_half", ESM::ItemLevList::AllLevels, 50, { { "lev_a", 1 } });
            EXPECT_EQ(results(itemList("list_half"), false), (std::set<std::string>{ "", "lev_a" }));
        }

        TEST_F(RulesLevelledTest, the_none_roll_is_below_the_chance_and_is_drawn_first_and_always)
        {
            addList("list_edge", ESM::ItemLevList::AllLevels, 40, { { "lev_a", 1 } });
            // roll 39 is below 40: nothing; roll 40: the item. One roll, then one die with a single side.
            Misc::Rng::Generator low(seedWithRoll(39));
            EXPECT_TRUE(MWMechanics::getLevelledItem(&itemList("list_edge"), false, low).empty());
            Misc::Rng::Generator high(seedWithRoll(40));
            EXPECT_EQ(MWMechanics::getLevelledItem(&itemList("list_edge"), false, high).getRefIdString(), "lev_a");
        }

        TEST_F(RulesLevelledTest, the_draws_are_one_none_roll_then_one_die_over_the_candidates)
        {
            addList("list_two", ESM::ItemLevList::AllLevels, 0, { { "lev_a", 1 }, { "lev_b", 1 } });
            for (unsigned seed : { 1u, 2u, 3u, 77u })
            {
                Misc::Rng::Generator prng(seed);
                MWMechanics::getLevelledItem(&itemList("list_two"), false, prng);
                Misc::Rng::Generator sibling(seed);
                Misc::Rng::roll0to99(sibling);
                Misc::Rng::rollDice(2, sibling);
                for (int i = 0; i < 4; ++i)
                    EXPECT_EQ(Misc::Rng::roll0to99(prng), Misc::Rng::roll0to99(sibling)) << seed;
            }
            // and the result is the die's pick, candidates in list order
            for (unsigned seed : { 1u, 2u, 3u, 77u })
            {
                Misc::Rng::Generator prng(seed);
                const std::string got
                    = MWMechanics::getLevelledItem(&itemList("list_two"), false, prng).getRefIdString();
                Misc::Rng::Generator sibling(seed);
                Misc::Rng::roll0to99(sibling);
                const int pick = Misc::Rng::rollDice(2, sibling);
                EXPECT_EQ(got, pick == 0 ? "lev_a" : "lev_b") << seed;
            }
        }

        TEST_F(RulesLevelledTest, nothing_is_drawn_for_a_list_with_no_candidate_after_the_none_roll)
        {
            addList("list_high", 0, 0, { { "lev_a", 9 } });
            Misc::Rng::Generator prng(5);
            EXPECT_TRUE(MWMechanics::getLevelledItem(&itemList("list_high"), false, prng).empty());
            Misc::Rng::Generator sibling(5);
            Misc::Rng::roll0to99(sibling);
            for (int i = 0; i < 4; ++i)
                EXPECT_EQ(Misc::Rng::roll0to99(prng), Misc::Rng::roll0to99(sibling));
        }

        TEST_F(RulesLevelledTest, an_empty_list_gives_nothing)
        {
            addList("list_empty", ESM::ItemLevList::AllLevels, 0, {});
            EXPECT_EQ(results(itemList("list_empty"), false), std::set<std::string>{ "" });
        }

        TEST_F(RulesLevelledTest, a_missing_item_gives_nothing)
        {
            addList("list_ghost", ESM::ItemLevList::AllLevels, 0, { { "no_such_item", 1 } });
            EXPECT_EQ(results(itemList("list_ghost"), false), std::set<std::string>{ "" });
        }

        TEST_F(RulesLevelledTest, a_nested_list_is_followed_with_the_same_level_and_generator)
        {
            addList("list_inner", 0, 0, { { "lev_a", 1 }, { "lev_b", 3 } });
            addList("list_outer", ESM::ItemLevList::AllLevels, 0, { { "list_inner", 1 } });
            EXPECT_EQ(results(itemList("list_outer"), false), std::set<std::string>{ "lev_b" });
            // the explicit level is passed down
            EXPECT_EQ(results(itemList("list_outer"), false, 2), std::set<std::string>{ "lev_a" });

            // two none rolls and two dice
            Misc::Rng::Generator prng(9);
            MWMechanics::getLevelledItem(&itemList("list_outer"), false, prng);
            Misc::Rng::Generator sibling(9);
            Misc::Rng::roll0to99(sibling);
            Misc::Rng::rollDice(1, sibling);
            Misc::Rng::roll0to99(sibling);
            Misc::Rng::rollDice(1, sibling);
            for (int i = 0; i < 4; ++i)
                EXPECT_EQ(Misc::Rng::roll0to99(prng), Misc::Rng::roll0to99(sibling));
        }

        TEST_F(RulesLevelledTest, a_nested_list_can_be_a_creature_list)
        {
            addList("clist_inner", ESM::CreatureLevList::AllLevels, 0, { { "lev_c", 1 } }, true);
            addList("list_to_creatures", ESM::ItemLevList::AllLevels, 0, { { "clist_inner", 1 } });
            EXPECT_EQ(results(itemList("list_to_creatures"), false), std::set<std::string>{ "lev_c" });
        }

        TEST_F(RulesLevelledTest, the_players_level_is_read_when_no_level_is_given)
        {
            addList("list_level2", 0, 0, { { "lev_a", 1 }, { "lev_b", 3 }, { "lev_c", 7 } });
            stats().setLevel(7);
            EXPECT_EQ(results(itemList("list_level2"), false), std::set<std::string>{ "lev_c" });
            stats().setLevel(2);
            EXPECT_EQ(results(itemList("list_level2"), false), std::set<std::string>{ "lev_a" });
        }

        // --------------------------------------------------------------------------------------------- spell autocalc

        class RulesAutocalcTest : public RulesTest
        {
        protected:
            struct EffectSpec
            {
                const char* mEffect;
                ESM::RefId mSkill;
                ESM::RefId mAttribute;
                int mRange;
                int mArea;
                int mDuration;
                int mMagMin;
                int mMagMax;
            };

            void SetUp() override
            {
                RulesTest::SetUp();

                // schools: Destruction holds 2 spells, the rest 1
                const std::pair<ESM::RefId, int> schools[]
                    = { { ESM::Skill::Alteration, 1 }, { ESM::Skill::Conjuration, 1 }, { ESM::Skill::Destruction, 2 },
                          { ESM::Skill::Illusion, 1 }, { ESM::Skill::Mysticism, 1 }, { ESM::Skill::Restoration, 1 } };
                for (const auto& [id, limit] : schools)
                {
                    ESM::Skill skill;
                    skill.blank();
                    skill.mId = *id.getIf<ESM::SkillId>();
                    skill.mName = skill.mId.getValue();
                    ESM::MagicSchool school;
                    school.mAutoCalcMax = limit;
                    skill.mSchool = school;
                    // the crafting fixture already inserted a plain record under this id
                    mStore.getWritable<ESM::Skill>().insertStatic(skill);
                }

                addMagicEffect("eff_fire", 10.f, ESM::Skill::Destruction, 0);
                addMagicEffect("eff_heal", 8.f, ESM::Skill::Restoration, 0);
                addMagicEffect("eff_charm", 6.f, ESM::Skill::Illusion, 0);
                addMagicEffect("eff_skill", 4.f, ESM::Skill::Alteration, ESM::MagicEffect::TargetSkill);
                addMagicEffect("eff_attr", 5.f, ESM::Skill::Mysticism, ESM::MagicEffect::TargetAttribute);

                const int both = ESM::Spell::F_Autocalc | ESM::Spell::F_PCStart;
                addSpell("sp_fire_small", both, 0, { { "eff_fire", {}, {}, 1, 0, 0, 1, 5 } });
                addSpell("sp_fire_med", both, 0, { { "eff_fire", {}, {}, 1, 0, 5, 10, 20 } });
                addSpell("sp_fire_big", both, 0, { { "eff_fire", {}, {}, 2, 5, 10, 30, 40 } });
                addSpell("sp_heal", both, 0, { { "eff_heal", {}, {}, 0, 0, 10, 5, 10 } });
                addSpell("sp_charm", ESM::Spell::F_Autocalc, 0, { { "eff_charm", {}, {}, 1, 0, 20, 10, 20 } });
                addSpell("sp_skill", both, 0, { { "eff_skill", ESM::Skill::Armorer, {}, 0, 0, 30, 5, 10 } });
                addSpell("sp_attr", both, 0, { { "eff_attr", {}, ESM::Attribute::Strength, 1, 0, 30, 5, 10 } });
                addSpell("sp_pc_cheap", ESM::Spell::F_PCStart, 3, { { "eff_fire", {}, {}, 0, 0, 0, 1, 1 } });
                addSpell("sp_pc_mid", ESM::Spell::F_PCStart, 8, { { "eff_heal", {}, {}, 0, 0, 0, 1, 1 } });
                addSpell("sp_pc_dear", ESM::Spell::F_PCStart, 20, { { "eff_fire", {}, {}, 0, 0, 0, 1, 1 } });
                addSpell("sp_never", 0, 5, { { "eff_fire", {}, {}, 0, 0, 0, 1, 1 } });
                addSpell("sp_power", both, 5, { { "eff_fire", {}, {}, 0, 0, 0, 1, 1 } }, ESM::Spell::ST_Power);
                addSpell("sp_always", ESM::Spell::F_Autocalc | ESM::Spell::F_Always, 0,
                    { { "eff_fire", {}, {}, 0, 0, 0, 1, 1 } });
            }

            void addMagicEffect(const char* id, float baseCost, const ESM::RefId& school, int flags)
            {
                ESM::MagicEffect effect;
                effect.blank();
                effect.mId = rid(id);
                effect.mName = id;
                effect.mData.mBaseCost = baseCost;
                effect.mData.mFlags = flags;
                effect.mData.mSchool = school;
                mStore.insertStatic(effect);
            }

            void addSpell(const char* id, int flags, int cost, const std::vector<EffectSpec>& effects,
                int type = ESM::Spell::ST_Spell)
            {
                ESM::Spell spell;
                spell.blank();
                spell.mId = rid(id);
                spell.mName = id;
                spell.mData.mType = type;
                spell.mData.mFlags = flags;
                spell.mData.mCost = cost;
                uint32_t index = 0;
                for (const EffectSpec& e : effects)
                {
                    ESM::IndexedENAMstruct effect;
                    effect.mIndex = index++;
                    effect.mData.mEffectID = rid(e.mEffect);
                    effect.mData.mSkill = e.mSkill;
                    effect.mData.mAttribute = e.mAttribute;
                    effect.mData.mRange = e.mRange;
                    effect.mData.mArea = e.mArea;
                    effect.mData.mDuration = e.mDuration;
                    effect.mData.mMagnMin = e.mMagMin;
                    effect.mData.mMagnMax = e.mMagMax;
                    spell.mEffects.mList.push_back(effect);
                }
                mStore.insertStatic(spell);
            }

            struct Actor
            {
                std::map<ESM::RefId, MWMechanics::SkillValue> mSkills;
                std::map<ESM::RefId, MWMechanics::AttributeValue> mAttributes;
            };

            static Actor makeActor(float skill, float attribute, float intelligence)
            {
                Actor actor;
                for (int i = 0; i < ESM::Skill::Length; ++i)
                {
                    MWMechanics::SkillValue value;
                    value.setBase(skill);
                    actor.mSkills[ESM::Skill::indexToRefId(i)] = value;
                }
                for (int i = 0; i < ESM::Attribute::Length; ++i)
                {
                    MWMechanics::AttributeValue value;
                    value.setBase(attribute);
                    actor.mAttributes[ESM::Attribute::indexToRefId(i)] = value;
                }
                actor.mAttributes[ESM::Attribute::Intelligence].setBase(intelligence);
                return actor;
            }

            static std::string join(const std::vector<ESM::RefId>& ids)
            {
                std::string text;
                for (const ESM::RefId& id : ids)
                    text += (text.empty() ? "" : ",") + id.getRefIdString();
                return text.empty() ? "-" : text;
            }

            const ESM::Race* raceWithPowers()
            {
                ESM::Race race;
                race.blank();
                race.mId = rid("race_with_powers");
                race.mPowers.mList = { rid("sp_heal"), rid("sp_pc_mid") };
                return mStore.insertStatic(race);
            }
        };

        TEST_F(RulesAutocalcTest, the_selected_spells_match_the_recording)
        {
            struct Case
            {
                const char* mName;
                float mSkill;
                float mAttribute;
                float mIntelligence;
            };
            const Case cases[] = {
                { "weak", 5.f, 30.f, 30.f },
                { "low", 20.f, 40.f, 40.f },
                { "mid", 50.f, 50.f, 50.f },
                { "skilled", 80.f, 60.f, 60.f },
                { "strong", 100.f, 100.f, 100.f },
                { "clever", 40.f, 40.f, 120.f },
            };
            const ESM::Race* race = raceWithPowers();
            std::ostringstream out;
            for (const Case& c : cases)
            {
                const Actor actor = makeActor(c.mSkill, c.mAttribute, c.mIntelligence);
                out << c.mName
                    << " npc: " << join(MWMechanics::autoCalcNpcSpells(actor.mSkills, actor.mAttributes, nullptr))
                    << '\n';
                out << c.mName
                    << " npc+race: " << join(MWMechanics::autoCalcNpcSpells(actor.mSkills, actor.mAttributes, race))
                    << '\n';
                out << c.mName
                    << " player: " << join(MWMechanics::autoCalcPlayerSpells(actor.mSkills, actor.mAttributes, nullptr))
                    << '\n';
                out << c.mName << " player+race: "
                    << join(MWMechanics::autoCalcPlayerSpells(actor.mSkills, actor.mAttributes, race)) << '\n';
            }
            expectRecording("rules_autocalc.golden.txt", out.str());
        }

        // The magicka test (`base magicka < iAutoSpellTimesCanCast * cost` for NPCs, `< cost` for a new player) is made
        // at the edge: an actor whose intelligence just allows a spell and one whose intelligence is one short.
        TEST_F(RulesAutocalcTest, the_magicka_edge_matches_the_recording)
        {
            std::ostringstream out;
            for (const char* name :
                { "sp_fire_small", "sp_fire_med", "sp_heal", "sp_skill", "sp_pc_cheap", "sp_pc_mid" })
            {
                const ESM::Spell* spell = mStore.get<ESM::Spell>().find(rid(name));
                const int cost = MWMechanics::calcSpellCost(*spell);
                // fNPCbaseMagickaMult 3 against iAutoSpellTimesCanCast 3, fPCbaseMagickaMult 1: intelligence == cost is
                // the edge for both.
                for (int intelligence : { cost - 1, cost })
                {
                    const Actor actor = makeActor(100.f, 100.f, static_cast<float>(intelligence));
                    out << name << " cost=" << cost << " int=" << intelligence
                        << " npc: " << join(MWMechanics::autoCalcNpcSpells(actor.mSkills, actor.mAttributes, nullptr))
                        << " player: "
                        << join(MWMechanics::autoCalcPlayerSpells(actor.mSkills, actor.mAttributes, nullptr)) << '\n';
                }
            }
            expectRecording("rules_autocalc_edges.golden.txt", out.str());
        }
    }
}
