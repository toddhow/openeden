// Task 0085, move 2, stage 2b: characterization tests of MWMechanics::Alchemy. They pass on the current code; stage 2c
// (moving the class) must keep them green and unedited. See craftingfixture.hpp for the fixture and its limits.
//
// The expected potions are what the current code produces for the test inputs below (recorded by running it; the
// first few were also worked out by hand from the formulas in alchemy.cpp and agreed). The rolls are chosen by seed
// (CraftingRulesTest::findSeed), so no number depends on how a standard library maps the generator to a roll.
//
// Inputs: Strength, Intelligence and Luck 50, Alchemy skill set per test, fatigue term 1.25 (not used by alchemy).
// The alchemy factor is skill + 0.1 * intelligence + 0.1 * luck = skill + 10.
#include <sstream>

#include <components/esm3/loadalch.hpp>

#include "apps/openmw/mwmechanics/alchemy.hpp"
#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/magiceffects.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"

#include "craftingfixture.hpp"

namespace CraftingTest
{
    namespace
    {
        using MWMechanics::Alchemy;

        const char* const kMeshes[] = { "standard", "bargain", "cheap", "fresh", "exclusive", "quality" };

        class CraftingAlchemyTest : public CraftingRulesTest
        {
        protected:
            void SetUp() override
            {
                CraftingRulesTest::SetUp();

                addEffect(ESM::MagicEffect::RestoreHealth, 5.f, 0);
                addEffect(ESM::MagicEffect::RestoreFatigue, 4.f, 0);
                addEffect(ESM::MagicEffect::FortifyAttribute, 2.f, ESM::MagicEffect::TargetAttribute);
                addEffect(ESM::MagicEffect::DamageHealth, 3.f, ESM::MagicEffect::Harmful);
                addEffect(ESM::MagicEffect::DrainSkill, 2.f, ESM::MagicEffect::Harmful | ESM::MagicEffect::TargetSkill);
                addEffect(ESM::MagicEffect::Light, 0.5f, ESM::MagicEffect::NoMagnitude);

                const ESM::RefId none;
                const IngredientEffect restoreHealth{ ESM::MagicEffect::RestoreHealth, none, none };
                const IngredientEffect restoreFatigue{ ESM::MagicEffect::RestoreFatigue, none, none };
                const IngredientEffect damageHealth{ ESM::MagicEffect::DamageHealth, none, none };
                const IngredientEffect light{ ESM::MagicEffect::Light, none, none };
                const IngredientEffect fortifyStrength{ ESM::MagicEffect::FortifyAttribute, none,
                    ESM::Attribute::Strength };
                const IngredientEffect fortifyAgility{ ESM::MagicEffect::FortifyAttribute, none,
                    ESM::Attribute::Agility };
                const IngredientEffect drainArmorer{ ESM::MagicEffect::DrainSkill, ESM::Skill::Armorer, none };

                addIngredient("ing_a", { restoreHealth, fortifyStrength, damageHealth, light }, 0.5f, 10);
                addIngredient("ing_b", { restoreHealth, damageHealth, fortifyAgility, restoreFatigue }, 1.f, 20);
                addIngredient("ing_c", { restoreFatigue, fortifyStrength, restoreHealth, drainArmorer }, 0.25f, 5);
                addIngredient("ing_d", { drainArmorer, damageHealth, restoreFatigue, light }, 2.f, 40);
                addIngredient("ing_e", { restoreHealth }, 1.f, 1);
                addIngredient("ing_h1", { damageHealth }, 1.f, 1);
                addIngredient("ing_h2", { damageHealth }, 3.f, 1);
                addIngredient("ing_f", { fortifyStrength }, 1.f, 1);

                addApparatus("mortar_1", ESM::Apparatus::MortarPestle, 1.f);
                addApparatus("mortar_2", ESM::Apparatus::MortarPestle, 2.f);
                addApparatus("mortar_tiny", ESM::Apparatus::MortarPestle, 0.1f);
                addApparatus("alembic", ESM::Apparatus::Alembic, 1.2f);
                addApparatus("calcinator", ESM::Apparatus::Calcinator, 1.5f);
                addApparatus("retort", ESM::Apparatus::Retort, 2.f);
            }

            void setAlchemy(float skill) { stats().getSkill(ESM::Skill::Alchemy).setBase(skill); }

            /// Every roll is a success: factor 110 is above the largest roll, 99.
            void expertAlchemist() { setAlchemy(100.f); }

            /// All ingredients to the alchemist's slots, in order; returns the Alchemy set up with the player.
            void fill(Alchemy& alchemy, const std::vector<const char*>& ingredients, int stackSize = 5)
            {
                alchemy.setAlchemist(mPlayer);
                for (const char* id : ingredients)
                    EXPECT_GE(alchemy.addIngredient(give(rid(id), stackSize)), 0) << id;
            }

            void clearInventory()
            {
                MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
                std::vector<MWWorld::Ptr> all;
                for (auto it = store.begin(); it != store.end(); ++it)
                    all.push_back(*it);
                for (const MWWorld::Ptr& item : all)
                    store.remove(item, item.getCellRef().getCount());
            }

            MWWorld::Ptr firstPotion()
            {
                MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
                auto it = store.begin(MWWorld::ContainerStore::Type_Potion);
                return it == store.end() ? MWWorld::Ptr() : *it;
            }

            /// "name v=value w=weight [effect:skill/attribute dDuration mMin-Max rRange aArea]..." of the first potion
            /// in the player's inventory (the model and icon are tested separately).
            std::string potion()
            {
                MWWorld::Ptr ptr = firstPotion();
                if (ptr.isEmpty())
                    return "none";
                const ESM::Potion& record = *ptr.get<ESM::Potion>()->mBase;
                std::ostringstream out;
                out << record.mName << " v=" << record.mData.mValue << " w=" << record.mData.mWeight;
                for (const ESM::IndexedENAMstruct& effect : record.mEffects.mList)
                {
                    out << " [" << effect.mData.mEffectID.getRefIdString();
                    if (!effect.mData.mSkill.empty())
                        out << ":" << effect.mData.mSkill.getRefIdString();
                    if (!effect.mData.mAttribute.empty())
                        out << ":" << effect.mData.mAttribute.getRefIdString();
                    out << " d" << effect.mData.mDuration << " m" << effect.mData.mMagnMin << "-"
                        << effect.mData.mMagnMax << " r" << effect.mData.mRange << " a" << effect.mData.mArea << "]";
                }
                return out.str();
            }

            int potionCount()
            {
                return mPlayer.getClass().getContainerStore(mPlayer).count(
                    firstPotion().isEmpty() ? ESM::RefId() : firstPotion().getCellRef().getRefId());
            }

            int count(const char* id) { return mPlayer.getClass().getContainerStore(mPlayer).count(rid(id)); }

            /// Brews one potion with the given tools from the inventory and returns the first potion's description.
            std::string brewOne(const std::vector<const char*>& tools, const std::vector<const char*>& ingredients)
            {
                clearInventory();
                expertAlchemist();
                for (const char* tool : tools)
                    give(rid(tool), 1);
                Alchemy alchemy;
                fill(alchemy, ingredients);
                int count = 1;
                seedWorld(1);
                EXPECT_EQ(alchemy.create("brew", count), Alchemy::Result_Success);
                EXPECT_EQ(count, 1);
                return potion();
            }
        };

        // ---- knownEffect and effectsDescription ----

        TEST_F(CraftingAlchemyTest, knownEffectFollowsTheSkillInSteps)
        {
            // fWortChanceValue 15: effect indexes 0 and 1 from 15, 2 and 3 from 30, 4 and 5 from 45, 6 and 7 from 60.
            std::string table;
            for (const float skill : { 14.f, 15.f, 29.f, 30.f, 44.f, 45.f, 59.f, 60.f, 200.f })
            {
                stats().getSkill(ESM::Skill::Alchemy).setBase(skill);
                // The player's alchemy skill is the base plus modifiers; the fixture has none.
                for (size_t index = 0; index < 9; ++index)
                    table += Alchemy::knownEffect(index, mPlayer) ? '1' : '0';
                table += ' ';
            }
            EXPECT_EQ(table,
                "000000000 "
                "110000000 "
                "110000000 "
                "111100000 "
                "111100000 "
                "111111000 "
                "111111000 "
                "111111110 "
                "111111110 ");
        }

        TEST_F(CraftingAlchemyTest, effectsDescriptionRevealsOneEffectPerFifteenSkillPoints)
        {
            MWWorld::Ptr ingredient = give(rid("ing_c"), 1);
            auto describe = [&](float skill) {
                std::string out;
                for (const std::string& effect : Alchemy::effectsDescription(ingredient, skill))
                    out += effect + "|";
                return out;
            };
            EXPECT_EQ(describe(14.f), "");
            EXPECT_EQ(describe(15.f), "RestoreFatigue|");
            EXPECT_EQ(describe(30.f), "RestoreFatigue|Fortify Strength|");
            EXPECT_EQ(describe(45.f), "RestoreFatigue|Fortify Strength|RestoreHealth|");
            EXPECT_EQ(describe(60.f), "RestoreFatigue|Fortify Strength|RestoreHealth|Drain Armorer|");
            EXPECT_EQ(describe(500.f), "RestoreFatigue|Fortify Strength|RestoreHealth|Drain Armorer|");
        }

        // ---- slots ----

        TEST_F(CraftingAlchemyTest, ingredientsFillTheFourSlotsInOrderAndDuplicatesAreRefused)
        {
            Alchemy alchemy;
            alchemy.setAlchemist(mPlayer);
            MWWorld::Ptr a = give(rid("ing_a"), 1);
            MWWorld::Ptr b = give(rid("ing_b"), 1);
            MWWorld::Ptr c = give(rid("ing_c"), 1);
            MWWorld::Ptr d = give(rid("ing_d"), 1);
            MWWorld::Ptr e = give(rid("ing_e"), 1);

            EXPECT_EQ(alchemy.addIngredient(a), 0);
            EXPECT_EQ(alchemy.addIngredient(a), -1) << "the same ingredient twice";
            EXPECT_EQ(alchemy.addIngredient(b), 1);
            EXPECT_EQ(alchemy.addIngredient(c), 2);
            EXPECT_EQ(alchemy.addIngredient(d), 3);
            EXPECT_EQ(alchemy.addIngredient(e), -1) << "no free slot";

            alchemy.removeIngredient(1);
            alchemy.removeIngredient(1); // empty slot: no-op
            alchemy.removeIngredient(9); // outside: no-op
            EXPECT_EQ(alchemy.addIngredient(e), 1) << "the lowest free slot";
        }

        TEST_F(CraftingAlchemyTest, theBestApparatusOfEachTypeIsChosenFromTheInventory)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            give(rid("mortar_2"), 1);
            give(rid("retort"), 1);
            Alchemy alchemy;
            alchemy.setAlchemist(mPlayer);

            std::string slots;
            for (auto it = alchemy.beginTools(); it != alchemy.endTools(); ++it)
                slots += (it->isEmpty() ? std::string("-") : it->getCellRef().getRefId().getRefIdString()) + ",";
            EXPECT_EQ(slots, "mortar_2,-,-,retort,");
        }

        TEST_F(CraftingAlchemyTest, anApparatusAddedByHandGoesToTheSlotOfItsTypeAndCanBeRemoved)
        {
            Alchemy alchemy;
            alchemy.setAlchemist(mPlayer);
            MWWorld::Ptr calcinator = give(rid("calcinator"), 1);
            alchemy.addApparatus(calcinator);
            auto slot = [&](size_t index) { return (alchemy.beginTools() + index)->isEmpty() ? "-" : "x"; };
            EXPECT_STREQ(slot(ESM::Apparatus::Calcinator), "x");
            EXPECT_STREQ(slot(ESM::Apparatus::MortarPestle), "-");
            alchemy.removeApparatus(ESM::Apparatus::Calcinator);
            EXPECT_STREQ(slot(ESM::Apparatus::Calcinator), "-");
            alchemy.removeApparatus(17); // outside: no-op
        }

        TEST_F(CraftingAlchemyTest, clearRemovesTheAlchemistAndTheIngredients)
        {
            Alchemy alchemy;
            fill(alchemy, { "ing_a", "ing_b" });
            alchemy.clear();
            EXPECT_EQ(alchemy.beginIngredients(), alchemy.endIngredients());
        }

        // ---- effects ----

        TEST_F(CraftingAlchemyTest, sharedEffectsAreListedInSlotPairOrderWithoutRepeats)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            auto names = [&](const std::vector<const char*>& ingredients) {
                Alchemy alchemy;
                fill(alchemy, ingredients);
                std::string out;
                for (const MWMechanics::EffectKey& key : alchemy.listEffects())
                    out += key.mId.getRefIdString() + (key.mArg.empty() ? "" : ":" + key.mArg.getRefIdString()) + " ";
                return out;
            };
            EXPECT_EQ(names({ "ing_a" }), "");
            EXPECT_EQ(names({ "ing_a", "ing_b" }), "RestoreHealth DamageHealth ");
            EXPECT_EQ(names({ "ing_b", "ing_a" }), "RestoreHealth DamageHealth ");
            EXPECT_EQ(names({ "ing_a", "ing_b", "ing_c" }),
                "RestoreHealth DamageHealth FortifyAttribute:Strength RestoreFatigue ");
            EXPECT_EQ(names({ "ing_c", "ing_b", "ing_a" }),
                "RestoreFatigue RestoreHealth FortifyAttribute:Strength DamageHealth ");
            EXPECT_EQ(names({ "ing_a", "ing_b", "ing_c", "ing_d" }),
                "RestoreHealth DamageHealth FortifyAttribute:Strength Light RestoreFatigue DrainSkill:Armorer ");
        }

        TEST_F(CraftingAlchemyTest, theSuggestedNameIsTheFirstEffectsDescription)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            Alchemy alchemy;
            fill(alchemy, { "ing_c", "ing_a" });
            // ing_c's first effect shared with ing_a is its second, FortifyAttribute on Strength.
            EXPECT_EQ(alchemy.suggestPotionName(), "Fortify Strength");
            Alchemy lonely;
            fill(lonely, { "ing_a" });
            EXPECT_EQ(lonely.suggestPotionName(), "");
        }

        // ---- the result codes ----

        TEST_F(CraftingAlchemyTest, createRefusesWithTheFirstMissingRequirement)
        {
            expertAlchemist();
            int count = 1;
            {
                Alchemy alchemy; // no mortar and pestle
                fill(alchemy, { "ing_a", "ing_b" });
                EXPECT_EQ(alchemy.create("x", count), Alchemy::Result_NoMortarAndPestle);
            }
            give(rid("mortar_1"), 1);
            {
                Alchemy alchemy;
                fill(alchemy, { "ing_a" });
                EXPECT_EQ(alchemy.create("x", count), Alchemy::Result_LessThanTwoIngredients);
            }
            {
                Alchemy alchemy;
                fill(alchemy, { "ing_a", "ing_b" });
                EXPECT_EQ(alchemy.create("", count), Alchemy::Result_NoName);
            }
            EXPECT_EQ(count, 1) << "the count is only changed by a brew that was tried";
            EXPECT_EQ(mWorld.mBreakInvisibilityCalls, 0);
            EXPECT_EQ(potion(), "none");
        }

        TEST_F(CraftingAlchemyTest, ingredientsWithNothingInCommonAreUsedUpWithoutAPotion)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            Alchemy alchemy;
            fill(alchemy, { "ing_e", "ing_h1" }, 2);
            int count = 1;
            seedWorld(7);
            Misc::Rng::Generator sibling(7);
            EXPECT_EQ(alchemy.create("nothing", count), Alchemy::Result_NoEffects);
            EXPECT_EQ(count, 1);
            EXPECT_EQ(this->count("ing_e"), 1) << "one of each is removed";
            EXPECT_EQ(this->count("ing_h1"), 1);
            EXPECT_EQ(potion(), "none");
            EXPECT_EQ(mWorld.mPrng, sibling) << "no draw";
            EXPECT_EQ(mWorld.mBreakInvisibilityCalls, 0);
        }

        // ---- the potion ----

        TEST_F(CraftingAlchemyTest, mortarAndPestleAlone)
        {
            // x = 110 * 1 * 0.5 = 55, value int(55 * 2) = 110, RestoreHealth (cost 5): magnitude (55 / 1.5) / 5 = 7.33,
            // duration (55 / 0.5) / 5 = 22. The weight is the mean of the two ingredients' (1 and 0.5).
            EXPECT_EQ(
                brewOne({ "mortar_1" }, { "ing_e", "ing_a" }), "brew v=110 w=0.75 [RestoreHealth d22 m7-7 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, aBetterMortarScalesTheStrengthAndTheValue)
        {
            // x = 110 * 2 * 0.5 = 110, value 220, magnitude (110 / 1.5) / 5 = 14.67, duration 44.
            EXPECT_EQ(
                brewOne({ "mortar_2" }, { "ing_e", "ing_a" }), "brew v=220 w=0.75 [RestoreHealth d44 m15-15 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, aRetortAddsItsQualityToAGoodEffect)
        {
            // magnitude 7.33 + 2 = 9.33, duration 22 + 2
            EXPECT_EQ(brewOne({ "mortar_1", "retort" }, { "ing_e", "ing_a" }),
                "brew v=110 w=0.75 [RestoreHealth d24 m9-9 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, aCalcinatorAloneAddsItsQualityToAGoodEffect)
        {
            // magnitude 7.33 + 1.5 = 8.83, duration 22 + 1.5 = 23.5 rounds up
            EXPECT_EQ(brewOne({ "mortar_1", "calcinator" }, { "ing_e", "ing_a" }),
                "brew v=110 w=0.75 [RestoreHealth d24 m9-9 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, aRetortAndACalcinatorTogetherForAGoodEffect)
        {
            // quality 2 * 2 + 1.5 = 5.5: magnitude 12.83, duration 27.5 rounds up
            EXPECT_EQ(brewOne({ "mortar_1", "retort", "calcinator" }, { "ing_e", "ing_a" }),
                "brew v=110 w=0.75 [RestoreHealth d28 m13-13 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, aHarmfulEffectWithNoApparatusBeyondTheMortar)
        {
            // DamageHealth (cost 3): magnitude (55 / 1.5) / 3 = 12.22, duration (55 / 0.5) / 3 = 36.67
            EXPECT_EQ(
                brewOne({ "mortar_1" }, { "ing_h1", "ing_h2" }), "brew v=110 w=2 [DamageHealth d37 m12-12 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, aRetortDoesNothingForAHarmfulEffect)
        {
            EXPECT_EQ(brewOne({ "mortar_1", "retort" }, { "ing_h1", "ing_h2" }),
                "brew v=110 w=2 [DamageHealth d37 m12-12 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, anAlembicDividesAHarmfulEffect)
        {
            // quality 1 + 1.2 = 2.2: magnitude 12.22 / 2.2 = 5.56, duration 36.67 / 2.2 = 16.67
            EXPECT_EQ(brewOne({ "mortar_1", "alembic" }, { "ing_h1", "ing_h2" }),
                "brew v=110 w=2 [DamageHealth d17 m6-6 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, aCalcinatorAloneIncreasesAHarmfulEffect)
        {
            // magnitude 12.22 + 1.5 = 13.72, duration 36.67 + 1.5 = 38.17
            EXPECT_EQ(brewOne({ "mortar_1", "calcinator" }, { "ing_h1", "ing_h2" }),
                "brew v=110 w=2 [DamageHealth d38 m14-14 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, anAlembicAndACalcinatorTogetherForAHarmfulEffect)
        {
            // quality 2 * 1.2 + 3 * 1.5 = 6.9: magnitude 1.77, duration 5.31
            EXPECT_EQ(brewOne({ "mortar_1", "alembic", "calcinator" }, { "ing_h1", "ing_h2" }),
                "brew v=110 w=2 [DamageHealth d5 m2-2 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, anEffectWithNoMagnitudeKeepsMagnitudeOneAndTheToolsAddAHalfPointMore)
        {
            // Light (NoMagnitude, cost 0.5) is shared by ing_a and ing_d: duration (55 / 0.5) / 0.5 = 220. With no
            // magnitude a retort adds its quality plus 0.5 (2.5), not the quality alone: 222.5 rounds to 223.
            EXPECT_EQ(brewOne({ "mortar_1" }, { "ing_a", "ing_d" }),
                "brew v=110 w=1.25 [DamageHealth d37 m12-12 r0 a0] [Light d220 m1-1 r0 a0]");
            EXPECT_EQ(brewOne({ "mortar_1", "retort" }, { "ing_a", "ing_d" }),
                "brew v=110 w=1.25 [DamageHealth d37 m12-12 r0 a0] [Light d223 m1-1 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, aSkillAndAnAttributeEffectNameWhatTheyAffect)
        {
            EXPECT_EQ(brewOne({ "mortar_1" }, { "ing_a", "ing_c" }),
                "brew v=110 w=0.375 [RestoreHealth d22 m7-7 r0 a0] [FortifyAttribute:Strength d55 m18-18 r0 a0]");
            EXPECT_EQ(brewOne({ "mortar_1" }, { "ing_c", "ing_d" }),
                "brew v=110 w=1.125 [RestoreFatigue d28 m9-9 r0 a0] [DrainSkill:Armorer d55 m18-18 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, aWeakAlchemistLosesTheEffectsThatRoundToZero)
        {
            // Alchemy 1: factor 11, x = 5.5, value 11. RestoreHealth magnitude (5.5 / 1.5) / 5 = 0.73 rounds to 1,
            // duration 2.2 rounds to 2: the effect is still there.
            setAlchemy(1.f);
            give(rid("mortar_1"), 1);
            Alchemy alchemy;
            fill(alchemy, { "ing_e", "ing_a" });
            int count = 1;
            seedWorld(findSeed([](Misc::Rng::Generator& g) { return Misc::Rng::roll0to99(g) <= 10; }));
            EXPECT_EQ(alchemy.create("weak", count), Alchemy::Result_Success);
            EXPECT_EQ(potion(), "weak v=11 w=0.75 [RestoreHealth d2 m1-1 r0 a0]");
        }

        TEST_F(CraftingAlchemyTest, whenEveryEffectRoundsAwayTheIngredientsAreSpentForNothing)
        {
            // Alchemy 0: factor 10; a mortar of quality 0.1 gives x = 10 * 0.1 * 0.5 = 0.5, magnitude 0.07 rounds to 0.
            setAlchemy(0.f);
            give(rid("mortar_tiny"), 1);
            Alchemy alchemy;
            fill(alchemy, { "ing_e", "ing_a" }, 3);
            int count = 1;
            seedWorld(9);
            Misc::Rng::Generator sibling(9);
            EXPECT_EQ(alchemy.create("nothing", count), Alchemy::Result_RandomFailure);
            EXPECT_EQ(count, 0);
            EXPECT_EQ(this->count("ing_e"), 2);
            EXPECT_EQ(this->count("ing_a"), 2);
            EXPECT_EQ(potion(), "none");
            EXPECT_EQ(mWorld.mPrng, sibling) << "no draw: the check for an empty effect list comes before the roll";
            EXPECT_TRUE(mLua.mSkillUses.empty());
        }

        TEST_F(CraftingAlchemyTest, theModelAndIconFollowTheDiceRollOfTheSixNames)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            for (int index = 0; index < 6; ++index)
            {
                // The first draw is the success roll; the second is the six-sided die.
                const unsigned seed = findSeed([index](Misc::Rng::Generator& g) {
                    Misc::Rng::roll0to99(g);
                    return Misc::Rng::rollDice(6, g) == index;
                });
                Alchemy alchemy;
                fill(alchemy, { "ing_e", "ing_a" }, 1);
                int count = 1;
                seedWorld(seed);
                // A new name each time: a potion with the effects, name, weight and value of an existing dynamic record
                // reuses that record, model and icon included.
                ASSERT_EQ(alchemy.create("bottle" + std::to_string(index), count), Alchemy::Result_Success);
                MWWorld::Ptr ptr = firstPotion();
                const ESM::Potion& record = *ptr.get<ESM::Potion>()->mBase;
                EXPECT_EQ(record.mModel, std::string("m\\misc_potion_") + kMeshes[index] + "_01.nif");
                EXPECT_EQ(record.mIcon, std::string("m\\tx_potion_") + kMeshes[index] + "_01.dds");
                mPlayer.getClass().getContainerStore(mPlayer).remove(ptr, ptr.getCellRef().getCount());
                mPlayer.getClass().getContainerStore(mPlayer).remove(rid("ing_e"), 10);
                mPlayer.getClass().getContainerStore(mPlayer).remove(rid("ing_a"), 10);
            }
        }

        TEST_F(CraftingAlchemyTest, aSuccessUsesTwoDrawsAndCountsAsASkillUse)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            Alchemy alchemy;
            fill(alchemy, { "ing_e", "ing_a" });
            const unsigned seed = 3;
            seedWorld(seed);
            Misc::Rng::Generator sibling(seed);
            Misc::Rng::roll0to99(sibling);
            Misc::Rng::rollDice(6, sibling);

            int count = 1;
            EXPECT_EQ(alchemy.create("p", count), Alchemy::Result_Success);
            EXPECT_EQ(mWorld.mPrng, sibling) << "one roll for the chance, one die for the bottle";
            EXPECT_EQ(mWorld.mBreakInvisibilityCalls, 1);
            ASSERT_EQ(mLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mLua.mSkillUses[0].mSkill, "Alchemy");
            EXPECT_EQ(mLua.mSkillUses[0].mUseType, ESM::Skill::Alchemy_CreatePotion);
            EXPECT_EQ(this->count("ing_e"), 4);
            EXPECT_EQ(this->count("ing_a"), 4);
        }

        // ---- chance ----

        TEST_F(CraftingAlchemyTest, theChanceIsTheAlchemyFactorAgainstARollOfZeroToNinetyNine)
        {
            // Alchemy 40: factor 50. The attempt fails when the factor is below the roll: 50 succeeds, 51 does not.
            setAlchemy(40.f);
            give(rid("mortar_1"), 1);
            {
                Alchemy alchemy;
                fill(alchemy, { "ing_e", "ing_a" });
                int count = 1;
                seedWorld(findSeed([](Misc::Rng::Generator& g) { return Misc::Rng::roll0to99(g) == 51; }));
                EXPECT_EQ(alchemy.create("p", count), Alchemy::Result_RandomFailure);
                EXPECT_EQ(count, 0);
                EXPECT_EQ(this->count("ing_e"), 4) << "the ingredients are used up by a failure too";
                EXPECT_TRUE(mLua.mSkillUses.empty());
                EXPECT_EQ(potion(), "none");
            }
            {
                Alchemy alchemy;
                fill(alchemy, { "ing_e", "ing_a" });
                int count = 1;
                seedWorld(findSeed([](Misc::Rng::Generator& g) { return Misc::Rng::roll0to99(g) == 50; }));
                EXPECT_EQ(alchemy.create("p", count), Alchemy::Result_Success);
                EXPECT_EQ(count, 1);
            }
        }

        // ---- several potions at once ----

        TEST_F(CraftingAlchemyTest, theNumberOfPotionsToBrewIsTheSmallestStack)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            Alchemy alchemy;
            alchemy.setAlchemist(mPlayer);
            alchemy.addIngredient(give(rid("ing_e"), 7));
            EXPECT_EQ(alchemy.countPotionsToBrew(), 0) << "not ready with one ingredient";
            alchemy.addIngredient(give(rid("ing_a"), 3));
            alchemy.setPotionName("p");
            EXPECT_EQ(alchemy.countPotionsToBrew(), 3);
        }

        TEST_F(CraftingAlchemyTest, brewingSeveralDrawsARollThenADieForEachSuccessAndOnlyARollForEachFailure)
        {
            setAlchemy(40.f); // factor 50: a roll of 0 to 50 succeeds
            give(rid("mortar_1"), 1);
            Alchemy alchemy;
            fill(alchemy, { "ing_e", "ing_a" }, 6);

            // A seed whose first six rolls give a mix of failures and successes, replayed on a sibling.
            const unsigned seed = findSeed([](Misc::Rng::Generator& g) {
                int successes = 0;
                for (int i = 0; i < 6; ++i)
                {
                    if (Misc::Rng::roll0to99(g) <= 50)
                    {
                        Misc::Rng::rollDice(6, g);
                        ++successes;
                    }
                }
                return successes == 3;
            });
            Misc::Rng::Generator sibling(seed);
            int expected = 0;
            for (int i = 0; i < 6; ++i)
            {
                if (Misc::Rng::roll0to99(sibling) <= 50)
                {
                    Misc::Rng::rollDice(6, sibling);
                    ++expected;
                }
            }
            ASSERT_EQ(expected, 3);

            seedWorld(seed);
            int count = 6;
            EXPECT_EQ(alchemy.create("p", count), Alchemy::Result_Success);
            EXPECT_EQ(count, 3);
            EXPECT_EQ(mWorld.mPrng, sibling);
            EXPECT_EQ(this->count("ing_e"), 0) << "six attempts use up six of each";
            EXPECT_EQ(mLua.mSkillUses.size(), 3u);
            EXPECT_EQ(mWorld.mBreakInvisibilityCalls, 1) << "once for the whole batch";
            int potions = 0;
            MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
            for (auto it = store.begin(MWWorld::ContainerStore::Type_Potion); it != store.end(); ++it)
                potions += it->getCellRef().getCount();
            EXPECT_EQ(potions, 3);
        }

        TEST_F(CraftingAlchemyTest, whenEveryAttemptFailsTheResultIsRandomFailureAndTheCountZero)
        {
            setAlchemy(0.f); // factor 10
            give(rid("mortar_1"), 1);
            Alchemy alchemy;
            fill(alchemy, { "ing_e", "ing_a" }, 4);
            // Factor 10 against rolls: only 0 to 10 succeed; every roll of the seed is above 10.
            const unsigned seed = findSeed([](Misc::Rng::Generator& g) {
                for (int i = 0; i < 4; ++i)
                    if (Misc::Rng::roll0to99(g) <= 10)
                        return false;
                return true;
            });
            seedWorld(seed);
            int count = 4;
            EXPECT_EQ(alchemy.create("p", count), Alchemy::Result_RandomFailure);
            EXPECT_EQ(count, 0);
            EXPECT_EQ(potion(), "none");
            EXPECT_EQ(this->count("ing_e"), 0);
        }

        // ---- the potion records ----

        TEST_F(CraftingAlchemyTest, aSecondIdenticalPotionReusesTheDynamicRecord)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            auto brew = [&]() {
                Alchemy alchemy;
                fill(alchemy, { "ing_e", "ing_a" }, 1);
                int count = 1;
                seedWorld(11);
                EXPECT_EQ(alchemy.create("same", count), Alchemy::Result_Success);
            };
            brew();
            EXPECT_EQ(mStore.get<ESM::Potion>().getDynamicSize(), 1u);
            const ESM::RefId first = firstPotion().getCellRef().getRefId();
            brew();
            EXPECT_EQ(mStore.get<ESM::Potion>().getDynamicSize(), 1u)
                << "same effects, name, mesh: the record is reused";
            EXPECT_EQ(firstPotion().getCellRef().getRefId(), first);
            EXPECT_EQ(potionCount(), 2);
        }

        TEST_F(CraftingAlchemyTest, aDifferentNameMakesANewRecord)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            for (const char* name : { "one", "two" })
            {
                Alchemy alchemy;
                fill(alchemy, { "ing_e", "ing_a" }, 1);
                int count = 1;
                seedWorld(11);
                EXPECT_EQ(alchemy.create(name, count), Alchemy::Result_Success);
            }
            EXPECT_EQ(mStore.get<ESM::Potion>().getDynamicSize(), 2u);
        }

        TEST_F(CraftingAlchemyTest, aBaseCostOfZeroIsAnError)
        {
            expertAlchemist();
            give(rid("mortar_1"), 1);
            addEffect(ESM::MagicEffect::Invisibility, 0.f, 0);
            addIngredient("ing_i1", { { ESM::MagicEffect::Invisibility, ESM::RefId(), ESM::RefId() } }, 1.f, 1);
            addIngredient("ing_i2", { { ESM::MagicEffect::Invisibility, ESM::RefId(), ESM::RefId() } }, 1.f, 1);
            Alchemy alchemy;
            alchemy.setAlchemist(mPlayer);
            alchemy.addIngredient(give(rid("ing_i1"), 1));
            try
            {
                alchemy.addIngredient(give(rid("ing_i2"), 1));
                FAIL() << "expected an exception";
            }
            catch (const std::runtime_error& error)
            {
                EXPECT_EQ(std::string(error.what()), "invalid base cost for magic effect Invisibility");
            }
        }
    }
}
