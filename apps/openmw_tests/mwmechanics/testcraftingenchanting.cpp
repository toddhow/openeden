// Task 0085, move 2, stage 2b: characterization tests of MWMechanics::Enchanting. They pass on the current code; stage
// 2c (moving the class) must keep them green and unedited. See craftingfixture.hpp for the fixture and its limits.
//
// The numbers are what the current code produces for the test inputs below; the cost formula was also worked out by
// hand from the comment above Enchanting::getEffectCosts in enchanting.cpp and agreed for the first cases. The roll is
// chosen by seed, so no number depends on how a standard library maps the generator to a roll.
//
// Effect costs (fEffectCostMult 0.5, effect base costs set below), as a running total over the effect list:
//   RestoreHealth (base 5) magnitude 10-10, duration 10, area 0 (counts as 1), self:
//     ((10 + 10) * 10 + 1) * 5 * 0.5 * 0.05 = 25.125
//   FortifyAttribute (base 2) magnitude 2-4, duration 5, area 0, touch:
//     ((2 + 4) * 5 + 1) * 2 * 0.5 * 0.05 = 1.55, running total 26.675
// A target-range effect multiplies the running total by 1.5. The points of an effect list are the SUM of these
// running totals (25.125 + 26.675 = 51.8 for the two effects); the price uses the last total only.
#include <cmath>

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/settings/values.hpp>

#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/enchanting.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"

#include "craftingfixture.hpp"

namespace CraftingTest
{
    namespace
    {
        using MWMechanics::Enchanting;

        ESM::IndexedENAMstruct effect(const ESM::RefId& id, int range, int area, int duration, int magnMin, int magnMax,
            const ESM::RefId& attribute = ESM::RefId())
        {
            ESM::IndexedENAMstruct result{};
            result.mData.mEffectID = id;
            result.mData.mAttribute = attribute;
            result.mData.mRange = range;
            result.mData.mArea = area;
            result.mData.mDuration = duration;
            result.mData.mMagnMin = magnMin;
            result.mData.mMagnMax = magnMax;
            return result;
        }

        ESM::EffectList list(std::vector<ESM::IndexedENAMstruct> effects)
        {
            ESM::EffectList result;
            result.mList = std::move(effects);
            result.updateIndexes();
            return result;
        }

        class CraftingEnchantingTest : public CraftingRulesTest
        {
        protected:
            void SetUp() override
            {
                CraftingRulesTest::SetUp();

                addEffect(ESM::MagicEffect::RestoreHealth, 5.f, 0);
                addEffect(ESM::MagicEffect::FortifyAttribute, 2.f, ESM::MagicEffect::TargetAttribute);

                for (const auto& [id, soul] : { std::pair{ "crea_small", 300 }, std::pair{ "crea_big", 500 } })
                {
                    ESM::Creature creature;
                    creature.blank();
                    creature.mId = rid(id);
                    creature.mData.mSoul = soul;
                    mStore.insertStatic(creature);
                }

                auto weapon = [&](const char* id, int type, int enchant) {
                    ESM::Weapon record;
                    record.blank();
                    record.mId = rid(id);
                    record.mName = id;
                    record.mData.mType = type;
                    record.mData.mEnchant = enchant;
                    record.mData.mHealth = 100;
                    record.mData.mWeight = 5.f;
                    record.mData.mValue = 50;
                    mStore.insertStatic(record);
                };
                weapon("sword", ESM::Weapon::LongBladeOneHand, 200);
                weapon("bow", ESM::Weapon::MarksmanBow, 200);
                weapon("dart", ESM::Weapon::MarksmanThrown, 200);
                weapon("arrow", ESM::Weapon::Arrow, 200);

                ESM::Armor armor;
                armor.blank();
                armor.mId = rid("cuirass");
                armor.mName = "cuirass";
                armor.mData.mEnchant = 300;
                armor.mData.mHealth = 100;
                mStore.insertStatic(armor);

                ESM::Book book;
                book.blank();
                book.mId = rid("scroll");
                book.mName = "scroll";
                book.mData.mIsScroll = 1;
                book.mData.mEnchant = 50;
                mStore.insertStatic(book);

                for (const char* name : { "gem", "Misc_SoulGem_Azura", "gold_001" })
                {
                    ESM::Miscellaneous misc;
                    misc.blank();
                    misc.mId = rid(name);
                    misc.mName = name;
                    misc.mData.mWeight = 1.f;
                    misc.mData.mValue = 1;
                    mStore.insertStatic(misc);
                }

                // The enchanter of a service: a second actor with a gold pool.
                ESM::NPC enchanter = mNpc;
                enchanter.mId = rid("enchanter");
                const ESM::NPC* stored = mStore.insertStatic(enchanter);
                ESM::CellRef cellRef;
                cellRef.blank();
                cellRef.mRefID = stored->mId;
                cellRef.mRefNum = ESM::RefNum{ .mIndex = 2, .mContentFile = 0 };
                mEnchanterRef = std::make_unique<MWWorld::LiveCellRef<ESM::NPC>>(cellRef, stored);
                mEnchanter = MWWorld::Ptr(mEnchanterRef.get());
            }

            void TearDown() override
            {
                Settings::game().mProjectilesEnchantMultiplier.set(0.f);
                mEnchanter = MWWorld::Ptr();
                mEnchanterRef.reset();
                CraftingRulesTest::TearDown();
            }

            MWWorld::Ptr giveGem(const char* id, const char* soul)
            {
                MWWorld::Ptr gem = give(rid(id), 1);
                gem.getCellRef().setSoul(rid(soul));
                return gem;
            }

            /// Restore Health, self, 10 to 10 for 10 s: 25.125 points.
            ESM::EffectList oneEffect() { return list({ effect(ESM::MagicEffect::RestoreHealth, 0, 0, 10, 10, 10) }); }

            /// Plus Fortify Strength, touch, 2 to 4 for 5 s: 26.675 points.
            ESM::EffectList twoEffects()
            {
                return list({ effect(ESM::MagicEffect::RestoreHealth, 0, 0, 10, 10, 10),
                    effect(ESM::MagicEffect::FortifyAttribute, 1, 0, 5, 2, 4, ESM::Attribute::Strength) });
            }

            MWWorld::Ptr mEnchanter;
            std::unique_ptr<MWWorld::LiveCellRef<ESM::NPC>> mEnchanterRef;
        };

        // ---- points, costs and limits ----

        TEST_F(CraftingEnchantingTest, theCostOfAnEffectListIsARunningTotal)
        {
            MWWorld::Ptr sword = give(rid("sword"), 1);
            Enchanting enchanting;
            enchanting.setOldItem(sword);
            enchanting.setEffect(oneEffect());
            EXPECT_NEAR(enchanting.getEnchantPoints(true), 25.125, 1e-4);
            EXPECT_FLOAT_EQ(enchanting.getEnchantPoints(false), 25.f);

            enchanting.setEffect(twoEffects());
            EXPECT_NEAR(enchanting.getEnchantPoints(true), 51.8, 1e-4) << "25.125 + 26.675";
            EXPECT_FLOAT_EQ(enchanting.getEnchantPoints(false), 51.f) << "floor(25.125) + floor(26.675)";
        }

        TEST_F(CraftingEnchantingTest, aTargetRangeEffectMultipliesTheRunningTotalByOneAndAHalf)
        {
            Enchanting enchanting;
            enchanting.setOldItem(give(rid("sword"), 1));
            enchanting.setEffect(list({ effect(ESM::MagicEffect::RestoreHealth, 0, 0, 10, 10, 10),
                effect(ESM::MagicEffect::FortifyAttribute, 2, 0, 5, 2, 4, ESM::Attribute::Strength) }));
            // running totals 25.125 and (25.125 + 1.55) * 1.5 = 40.0125, which add up to 65.1375
            EXPECT_NEAR(enchanting.getEnchantPoints(true), 65.1375, 1e-3);
        }

        TEST_F(CraftingEnchantingTest, magnitudesAndAreasBelowOneCountAsOne)
        {
            Enchanting enchanting;
            enchanting.setOldItem(give(rid("sword"), 1));
            enchanting.setEffect(list({ effect(ESM::MagicEffect::RestoreHealth, 0, 0, 10, 0, 0) }));
            // ((1 + 1) * 10 + 1) * 5 * 0.5 * 0.05 = 2.625
            EXPECT_NEAR(enchanting.getEnchantPoints(true), 2.625, 1e-4);
            enchanting.setEffect(list({ effect(ESM::MagicEffect::RestoreHealth, 0, 0, 0, 0, 0) }));
            // ((1 + 1) * 0 + 1) * 5 * 0.5 * 0.05 = 0.125, raised to the minimum cost of 1
            EXPECT_NEAR(enchanting.getEnchantPoints(true), 1.0, 1e-6);
        }

        TEST_F(CraftingEnchantingTest, aConstantEffectUsesTheConstantDurationAndHasNoCastCost)
        {
            // Only armor, clothing and weapons can be constant; a gem of at least 400 points allows the style.
            MWWorld::Ptr cuirass = give(rid("cuirass"), 1);
            Enchanting enchanting;
            enchanting.setEnchanter(mPlayer);
            enchanting.setOldItem(cuirass);
            enchanting.setSoulGem(giveGem("gem", "crea_big"));
            enchanting.setEffect(oneEffect());
            enchanting.nextCastStyle();
            enchanting.nextCastStyle();
            ASSERT_EQ(enchanting.getCastStyle(), ESM::Enchantment::ConstantEffect);
            // ((10 + 10) * 100 + 1) * 5 * 0.5 * 0.05 = 250.125
            EXPECT_NEAR(enchanting.getEnchantPoints(true), 250.125, 1e-3);
            EXPECT_EQ(enchanting.getEffectiveCastCost(), 1) << "the base cast cost is 0, the effective one at least 1";
        }

        TEST_F(CraftingEnchantingTest, theEffectiveCastCostFollowsTheEnchantSkill)
        {
            Enchanting enchanting;
            enchanting.setEnchanter(mPlayer);
            enchanting.setOldItem(give(rid("sword"), 1));
            enchanting.setEffect(oneEffect());
            // base cost int(25.125 floored) = 25; skill 30: 25 - 0.25 * (30 - 10) = 20
            EXPECT_EQ(enchanting.getEffectiveCastCost(), 20);
            stats().getSkill(ESM::Skill::Enchant).setBase(110.f);
            // 25 - 0.25 * 100 = 0, at least 1
            EXPECT_EQ(enchanting.getEffectiveCastCost(), 1);
            stats().getSkill(ESM::Skill::Enchant).setBase(10.f);
            EXPECT_EQ(enchanting.getEffectiveCastCost(), 25);
        }

        TEST_F(CraftingEnchantingTest, theMaximumIsTheItemsPointsTimesTheEnchantmentMultiplier)
        {
            Enchanting enchanting;
            EXPECT_EQ(enchanting.getMaxEnchantValue(), 0) << "no item";
            enchanting.setOldItem(give(rid("sword"), 1));
            EXPECT_EQ(enchanting.getMaxEnchantValue(), 20) << "200 * fEnchantmentMult 0.1";
            enchanting.setOldItem(give(rid("cuirass"), 1));
            EXPECT_EQ(enchanting.getMaxEnchantValue(), 30);
            enchanting.setOldItem(MWWorld::Ptr());
            EXPECT_EQ(enchanting.getMaxEnchantValue(), 0);
            EXPECT_TRUE(enchanting.itemEmpty());
        }

        TEST_F(CraftingEnchantingTest, theGemChargeIsTheSoulsValueAndZeroWithoutASoul)
        {
            Enchanting enchanting;
            EXPECT_TRUE(enchanting.soulEmpty());
            EXPECT_EQ(enchanting.getGemCharge(), 0);
            enchanting.setSoulGem(giveGem("gem", "crea_small"));
            EXPECT_FALSE(enchanting.soulEmpty());
            EXPECT_EQ(enchanting.getGemCharge(), 300);
            enchanting.setSoulGem(give(rid("gem"), 1)); // an empty gem (a stack of its own: it has no soul)
            EXPECT_EQ(enchanting.getGemCharge(), 0);
            MWWorld::Ptr ghost = give(rid("gem"), 1);
            ghost.getCellRef().setSoul(rid("no_such_creature"));
            enchanting.setSoulGem(ghost);
            EXPECT_EQ(enchanting.getGemCharge(), 0) << "a soul the store does not know";
        }

        // ---- chance and price ----

        TEST_F(CraftingEnchantingTest, theChanceIsTheSkillMinusThePointsPlusTheAttributesTimesFatigue)
        {
            // x = (skill - points * fEnchantmentChanceMult 3 + 0.2 * intelligence + 0.1 * luck) * 1.25, truncated.
            Enchanting enchanting;
            enchanting.setEnchanter(mPlayer);
            enchanting.setOldItem(give(rid("sword"), 1));
            enchanting.setEffect(oneEffect());
            // (30 - 75.375 + 10 + 5) * 1.25 = -37.97 -> -37
            EXPECT_EQ(enchanting.getEnchantChance(), -37);
            stats().getSkill(ESM::Skill::Enchant).setBase(100.f);
            // (100 - 75.375 + 15) * 1.25 = 49.53 -> 49
            EXPECT_EQ(enchanting.getEnchantChance(), 49);
        }

        TEST_F(CraftingEnchantingTest, aConstantEnchantmentHalvesTheChanceBeforeTruncating)
        {
            Enchanting enchanting;
            enchanting.setEnchanter(mPlayer);
            enchanting.setOldItem(give(rid("cuirass"), 1));
            enchanting.setSoulGem(giveGem("gem", "crea_big"));
            enchanting.setEffect(oneEffect());
            enchanting.nextCastStyle();
            enchanting.nextCastStyle();
            ASSERT_EQ(enchanting.getCastStyle(), ESM::Enchantment::ConstantEffect);
            stats().getSkill(ESM::Skill::Enchant).setBase(100.f);
            // (100 - 250.125 * 3 + 15) * 1.25 = -794.2; times fEnchantmentConstantChanceMult 0.5 = -397.1 -> -397
            EXPECT_EQ(enchanting.getEnchantChance(), -397);
        }

        TEST_F(CraftingEnchantingTest, thePriceGoesThroughTheEnchantersBarter)
        {
            Enchanting enchanting;
            enchanting.setEnchanter(mEnchanter);
            enchanting.setOldItem(give(rid("sword"), 1));
            enchanting.setEffect(twoEffects());
            // The final running total 26.675 times fEnchantmentValueMult 1000 = 26675.
            EXPECT_EQ(enchanting.getEnchantPrice(), 26675);
            ASSERT_EQ(mMechanics.mBarterCalls.size(), 1u);
            EXPECT_EQ(mMechanics.mBarterCalls[0].mBasePrice, 26675);
            EXPECT_TRUE(mMechanics.mBarterCalls[0].mBuying);
            mMechanics.mBarterFactor = 0.5f;
            EXPECT_EQ(enchanting.getEnchantPrice(), 13337);
        }

        TEST_F(CraftingEnchantingTest, aSelfEnchantmentHasNoPriceAndAnEmptyEffectListNeitherCostsNorPoints)
        {
            Enchanting enchanting;
            enchanting.setOldItem(give(rid("sword"), 1));
            enchanting.setEffect(oneEffect());
            EXPECT_EQ(enchanting.getEnchantPrice(), 0) << "no enchanter";
            enchanting.setEnchanter(mEnchanter);
            enchanting.setEffect(ESM::EffectList());
            EXPECT_FLOAT_EQ(enchanting.getEnchantPoints(true), 0.f);
            // no effects: final cost 0 -> base price 0 -> the barter offer of 0, then at least 1
            EXPECT_EQ(enchanting.getEnchantPrice(), 1);
        }

        // ---- the cast style ----

        TEST_F(CraftingEnchantingTest, castStylesOfAnArmorFollowTheSoulSize)
        {
            Enchanting enchanting;
            enchanting.setEnchanter(mPlayer);
            enchanting.setOldItem(give(rid("cuirass"), 1));
            enchanting.setSoulGem(giveGem("gem", "crea_small")); // 300 < 400
            EXPECT_EQ(enchanting.getCastStyle(), ESM::Enchantment::CastOnce);
            std::string styles;
            for (int i = 0; i < 4; ++i)
            {
                enchanting.nextCastStyle();
                styles += std::to_string(enchanting.getCastStyle());
            }
            EXPECT_EQ(styles, "2222") << "when used, and no constant effect with a small soul";

            enchanting.setSoulGem(giveGem("gem", "crea_big"));
            styles.clear();
            for (int i = 0; i < 4; ++i)
            {
                enchanting.nextCastStyle();
                styles += std::to_string(enchanting.getCastStyle());
            }
            EXPECT_EQ(styles, "3232") << "constant effect and back, with a big enough soul";
        }

        TEST_F(CraftingEnchantingTest, castStylesOfWeaponsFollowTheirClass)
        {
            auto styles = [&](const char* weapon, const char* soul) {
                Enchanting enchanting;
                enchanting.setEnchanter(mPlayer);
                enchanting.setOldItem(give(rid(weapon), 1));
                enchanting.setSoulGem(giveGem("gem", soul));
                std::string out;
                for (int i = 0; i < 5; ++i)
                {
                    enchanting.nextCastStyle();
                    out += std::to_string(enchanting.getCastStyle());
                }
                return out;
            };
            // 0 once, 1 strikes, 2 used, 3 constant
            EXPECT_EQ(styles("sword", "crea_small"), "12121");
            EXPECT_EQ(styles("sword", "crea_big"), "12312") << "melee: strikes, used, constant, strikes, used...";
            EXPECT_EQ(styles("bow", "crea_small"), "22222");
            EXPECT_EQ(styles("bow", "crea_big"), "23232");
            EXPECT_EQ(styles("dart", "crea_big"), "11111") << "thrown: strikes only, never constant";
            EXPECT_EQ(styles("arrow", "crea_big"), "11111");
        }

        TEST_F(CraftingEnchantingTest, aScrollIsAlwaysCastOnceAndNothingChangesWithoutAnItem)
        {
            Enchanting enchanting;
            enchanting.setEnchanter(mPlayer);
            enchanting.nextCastStyle();
            EXPECT_EQ(enchanting.getCastStyle(), ESM::Enchantment::CastOnce);
            enchanting.setOldItem(give(rid("scroll"), 1));
            enchanting.nextCastStyle();
            EXPECT_EQ(enchanting.getCastStyle(), ESM::Enchantment::CastOnce);
        }

        TEST_F(CraftingEnchantingTest, settingTheEnchanterResetsTheCastStyle)
        {
            Enchanting enchanting;
            enchanting.setEnchanter(mPlayer);
            enchanting.setOldItem(give(rid("cuirass"), 1));
            enchanting.nextCastStyle();
            ASSERT_EQ(enchanting.getCastStyle(), ESM::Enchantment::WhenUsed);
            enchanting.setEnchanter(mEnchanter);
            EXPECT_EQ(enchanting.getCastStyle(), ESM::Enchantment::CastOnce);
        }

        // ---- create ----

        TEST_F(CraftingEnchantingTest, aServiceEnchantmentMakesTheItemTakesTheGemAndPaysTheEnchanter)
        {
            give(rid("gold_001"), 100000);
            MWWorld::Ptr sword = give(rid("sword"), 1);
            MWWorld::Ptr gem = giveGem("gem", "crea_small");
            MWMechanics::CreatureStats& enchanterStats = mEnchanter.getClass().getCreatureStats(mEnchanter);
            enchanterStats.setGoldPool(10);

            Enchanting enchanting;
            enchanting.setSelfEnchanting(false);
            enchanting.setEnchanter(mEnchanter);
            enchanting.setOldItem(sword);
            enchanting.setSoulGem(gem);
            enchanting.setNewItemName("magic sword");
            enchanting.setEffect(twoEffects());
            seedWorld(5);
            Misc::Rng::Generator sibling(5);

            ASSERT_TRUE(enchanting.create());

            MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
            EXPECT_EQ(mWorld.mPrng, sibling) << "a service enchantment never rolls";
            EXPECT_EQ(store.count(rid("sword")), 0);
            EXPECT_EQ(store.count(rid("gem")), 0);
            EXPECT_EQ(store.count(ESM::RefId::stringRefId("gold_001")), 100000 - 26675);
            EXPECT_EQ(enchanterStats.getGoldPool(), 10 + 26675);
            ASSERT_EQ(mStore.get<ESM::Enchantment>().getDynamicSize(), 1u);
            const ESM::Enchantment* enchantment = nullptr;
            for (auto it = mStore.get<ESM::Enchantment>().begin(); it != mStore.get<ESM::Enchantment>().end(); ++it)
                if (mStore.get<ESM::Enchantment>().isDynamic(it->mId))
                    enchantment = &*it;
            ASSERT_NE(enchantment, nullptr);
            EXPECT_EQ(enchantment->mData.mType, ESM::Enchantment::CastOnce);
            EXPECT_EQ(enchantment->mData.mCost, 51) << "the floored points: 25 + 26";
            EXPECT_EQ(enchantment->mData.mCharge, 300);
            EXPECT_EQ(enchantment->mData.mFlags, 0);
            ASSERT_EQ(enchantment->mEffects.mList.size(), 2u);

            // The new weapon is in the inventory, named, with the enchantment and the charge as its points.
            MWWorld::Ptr created;
            for (auto it = store.begin(MWWorld::ContainerStore::Type_Weapon); it != store.end(); ++it)
                created = *it;
            ASSERT_FALSE(created.isEmpty());
            const ESM::Weapon& record = *created.get<ESM::Weapon>()->mBase;
            EXPECT_EQ(record.mName, "magic sword");
            EXPECT_EQ(record.mEnchant, enchantment->mId);
            EXPECT_EQ(record.mData.mEnchant, 300);
            EXPECT_TRUE(record.mData.mFlags & ESM::Weapon::Magical);
            EXPECT_TRUE(mLua.mSkillUses.empty());
        }

        TEST_F(CraftingEnchantingTest, aConstantEnchantmentHasNoChargeAndNoCastCost)
        {
            give(rid("gold_001"), 1000000);
            Enchanting enchanting;
            enchanting.setEnchanter(mEnchanter);
            enchanting.setOldItem(give(rid("cuirass"), 1));
            enchanting.setSoulGem(giveGem("gem", "crea_big"));
            enchanting.setNewItemName("constant");
            enchanting.setEffect(oneEffect());
            enchanting.nextCastStyle();
            enchanting.nextCastStyle();
            ASSERT_TRUE(enchanting.create());
            const ESM::Enchantment* enchantment = nullptr;
            for (auto it = mStore.get<ESM::Enchantment>().begin(); it != mStore.get<ESM::Enchantment>().end(); ++it)
                if (mStore.get<ESM::Enchantment>().isDynamic(it->mId))
                    enchantment = &*it;
            ASSERT_NE(enchantment, nullptr);
            EXPECT_EQ(enchantment->mData.mType, ESM::Enchantment::ConstantEffect);
            EXPECT_EQ(enchantment->mData.mCost, 0);
            EXPECT_EQ(enchantment->mData.mCharge, 0);
        }

        TEST_F(CraftingEnchantingTest, theSameEnchantmentTwiceReusesTheRecord)
        {
            give(rid("gold_001"), 1000000);
            for (int i = 0; i < 2; ++i)
            {
                Enchanting enchanting;
                enchanting.setEnchanter(mEnchanter);
                enchanting.setOldItem(give(rid("sword"), 1));
                enchanting.setSoulGem(giveGem("gem", "crea_small"));
                enchanting.setNewItemName("twin");
                enchanting.setEffect(oneEffect());
                ASSERT_TRUE(enchanting.create());
            }
            EXPECT_EQ(mStore.get<ESM::Enchantment>().getDynamicSize(), 1u);
            // Unlike potions and enchantments, the enchanted weapon record is not looked up: one new record each time.
            EXPECT_EQ(mStore.get<ESM::Weapon>().getDynamicSize(), 2u);
        }

        TEST_F(CraftingEnchantingTest, anAzurasStarIsGivenBack)
        {
            give(rid("gold_001"), 1000000);
            Enchanting enchanting;
            enchanting.setEnchanter(mEnchanter);
            enchanting.setOldItem(give(rid("sword"), 1));
            enchanting.setSoulGem(giveGem("Misc_SoulGem_Azura", "crea_small"));
            enchanting.setNewItemName("star");
            enchanting.setEffect(oneEffect());
            ASSERT_TRUE(enchanting.create());
            EXPECT_EQ(mPlayer.getClass().getContainerStore(mPlayer).count(rid("Misc_SoulGem_Azura")), 1);
        }

        // Self-enchanting: the player is the enchanter; chance (skill 30): (30 - 75.375 + 15) * 1.25 = -37, so use the
        // skill 100 chance of 49: the attempt fails when the chance is not above the roll.
        TEST_F(CraftingEnchantingTest, selfEnchantingSucceedsWhenTheRollIsBelowTheChance)
        {
            stats().getSkill(ESM::Skill::Enchant).setBase(100.f);
            auto attempt = [&](int roll) {
                Enchanting enchanting;
                enchanting.setSelfEnchanting(true);
                enchanting.setEnchanter(mPlayer);
                enchanting.setOldItem(give(rid("sword"), 1));
                enchanting.setSoulGem(giveGem("gem", "crea_small"));
                enchanting.setNewItemName("self");
                enchanting.setEffect(oneEffect());
                EXPECT_EQ(enchanting.getEnchantChance(), 49);
                seedWorld(seedWithRoll(roll));
                return enchanting.create();
            };
            EXPECT_TRUE(attempt(48));
            EXPECT_EQ(mLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mLua.mSkillUses[0].mSkill, "Enchant");
            EXPECT_EQ(mLua.mSkillUses[0].mUseType, ESM::Skill::Enchant_CreateMagicItem);
            EXPECT_FALSE(attempt(49)) << "a chance equal to the roll fails";
            EXPECT_EQ(mLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mMechanics.mBarterCalls.size(), 0u) << "no payment for a self-enchantment";
        }

        TEST_F(CraftingEnchantingTest, aFailedSelfEnchantmentStillUsesTheGemAndKeepsTheItem)
        {
            Enchanting enchanting;
            enchanting.setSelfEnchanting(true);
            enchanting.setEnchanter(mPlayer);
            enchanting.setOldItem(give(rid("sword"), 1));
            enchanting.setSoulGem(giveGem("gem", "crea_small"));
            enchanting.setNewItemName("self");
            enchanting.setEffect(oneEffect());
            seedWorld(seedWithRoll(10)); // the chance at skill 30 is negative
            EXPECT_FALSE(enchanting.create());
            MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
            EXPECT_EQ(store.count(rid("gem")), 0);
            EXPECT_EQ(store.count(rid("sword")), 1);
            EXPECT_EQ(mStore.get<ESM::Enchantment>().getDynamicSize(), 0u);
        }

        TEST_F(CraftingEnchantingTest, aSelfEnchantmentUsesOneDraw)
        {
            stats().getSkill(ESM::Skill::Enchant).setBase(100.f);
            Enchanting enchanting;
            enchanting.setSelfEnchanting(true);
            enchanting.setEnchanter(mPlayer);
            enchanting.setOldItem(give(rid("sword"), 1));
            enchanting.setSoulGem(giveGem("gem", "crea_small"));
            enchanting.setNewItemName("self");
            enchanting.setEffect(oneEffect());
            const unsigned seed = seedWithRoll(3);
            seedWorld(seed);
            Misc::Rng::Generator sibling(seed);
            Misc::Rng::roll0to99(sibling);
            ASSERT_TRUE(enchanting.create());
            EXPECT_EQ(mWorld.mPrng, sibling);
        }

        // ---- several projectiles at once (the "projectiles enchant multiplier" setting; 0 by default) ----

        TEST_F(CraftingEnchantingTest, projectilesAreEnchantedOneAtATimeByDefault)
        {
            give(rid("gold_001"), 1000000);
            give(rid("arrow"), 50);
            Enchanting enchanting;
            enchanting.setEnchanter(mEnchanter);
            enchanting.setOldItem(give(rid("arrow"), 0 + 1));
            enchanting.setSoulGem(giveGem("gem", "crea_big"));
            enchanting.setNewItemName("arrows");
            enchanting.setEffect(oneEffect());
            ASSERT_TRUE(enchanting.create());
            EXPECT_EQ(mPlayer.getClass().getContainerStore(mPlayer).count(rid("arrow")), 50);
        }

        TEST_F(CraftingEnchantingTest, withAMultiplierManyProjectilesShareOneGemAtAnEighthOfThePrice)
        {
            // The setting is limited to 0 to 1 (a larger value is sanitized to 1).
            Settings::game().mProjectilesEnchantMultiplier.set(1.f);
            give(rid("gold_001"), 1000000);
            MWWorld::Ptr arrows = give(rid("arrow"), 50);
            Enchanting enchanting;
            enchanting.setEnchanter(mEnchanter);
            enchanting.setOldItem(arrows);
            enchanting.setSoulGem(giveGem("gem", "crea_big"));
            enchanting.setNewItemName("arrows");
            enchanting.setEffect(oneEffect());
            // count = clamp(int(500 * 1 / 25.125), 1, 50) = 19; price = int(19 * 0.125) = 2 times the base price 25125
            EXPECT_EQ(enchanting.getEnchantPrice(), 50250);
            ASSERT_TRUE(enchanting.create());
            MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
            EXPECT_EQ(store.count(rid("arrow")), 50 - 19);
            const ESM::Enchantment* enchantment = nullptr;
            for (auto it = mStore.get<ESM::Enchantment>().begin(); it != mStore.get<ESM::Enchantment>().end(); ++it)
                if (mStore.get<ESM::Enchantment>().isDynamic(it->mId))
                    enchantment = &*it;
            ASSERT_NE(enchantment, nullptr);
            EXPECT_EQ(enchantment->mData.mCharge, 500 / 19) << "the soul is shared between the projectiles";
        }
    }
}
