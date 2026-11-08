// Task 0085, move 2, stage 2b: characterization tests of MWMechanics::rechargeItem (over time, and with a soul gem).
// They pass on the current code; stage 2c must keep them green and unedited. See craftingfixture.hpp.
//
// fMagicItemRechargePerSecond is cached by the code in a function-local static on its first call, so the fixture's
// value (0.05) is the same for the whole process; no test may change it.
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadweap.hpp>

#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/recharge.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"

#include "craftingfixture.hpp"

namespace CraftingTest
{
    namespace
    {
        constexpr int kEnchantmentCharge = 500;
        constexpr int kSoul = 300;

        class CraftingRechargeTest : public CraftingRulesTest
        {
        protected:
            void SetUp() override
            {
                CraftingRulesTest::SetUp();

                ESM::Enchantment enchantment;
                enchantment.blank();
                enchantment.mId = rid("test_enchantment");
                enchantment.mData.mType = ESM::Enchantment::CastOnce;
                enchantment.mData.mCost = 10;
                enchantment.mData.mCharge = kEnchantmentCharge;
                enchantment.mData.mFlags = 0;
                mStore.insertStatic(enchantment);

                ESM::Weapon blade;
                blade.blank();
                blade.mId = rid("test_blade");
                blade.mName = "test blade";
                blade.mEnchant = rid("test_enchantment");
                blade.mData.mHealth = 100;
                blade.mData.mWeight = 5.f;
                blade.mData.mValue = 50;
                mStore.insertStatic(blade);

                ESM::Creature creature;
                creature.blank();
                creature.mId = rid("test_creature");
                creature.mData.mSoul = kSoul;
                mStore.insertStatic(creature);

                for (const char* name : { "test_gem", "Misc_SoulGem_Azura" })
                {
                    ESM::Miscellaneous gem;
                    gem.blank();
                    gem.mId = rid(name);
                    gem.mName = name;
                    gem.mData.mWeight = 1.f;
                    gem.mData.mValue = 100;
                    mStore.insertStatic(gem);
                }
            }

            MWWorld::Ptr giveGem(const char* id, int count)
            {
                MWWorld::Ptr gem = give(rid(id), count);
                gem.getCellRef().setSoul(rid("test_creature"));
                return gem;
            }

            void setEnchant(float skill) { stats().getSkill(ESM::Skill::Enchant).setBase(skill); }
        };

        // ---- over time: rechargeItem(item, maxCharge, duration) ----

        TEST_F(CraftingRechargeTest, overTimeAddsTheRatePerSecondTimesTheDuration)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);

            EXPECT_TRUE(MWMechanics::rechargeItem(blade, 100.f, 20.f));
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 11.f) << "0.05 * 20 = 1";
        }

        TEST_F(CraftingRechargeTest, overTimeStopsAtTheMaximum)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(99.5f);

            EXPECT_TRUE(MWMechanics::rechargeItem(blade, 100.f, 20.f));
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 100.f);
        }

        TEST_F(CraftingRechargeTest, overTimeDoesNothingForAnUnusedOrFullItem)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            // A new item has the charge -1 ("not yet touched"): left alone.
            ASSERT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), -1.f);
            EXPECT_FALSE(MWMechanics::rechargeItem(blade, 100.f, 20.f));
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), -1.f);

            blade.getCellRef().setEnchantmentCharge(100.f);
            EXPECT_FALSE(MWMechanics::rechargeItem(blade, 100.f, 20.f));
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 100.f);
        }

        TEST_F(CraftingRechargeTest, overTimeWithZeroDurationReportsARechargeAndChangesNothing)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            EXPECT_TRUE(MWMechanics::rechargeItem(blade, 100.f, 0.f));
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 10.f);
        }

        TEST_F(CraftingRechargeTest, overTimeUsesNoRandomNumbers)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            seedWorld(5);
            Misc::Rng::Generator sibling(5);
            MWMechanics::rechargeItem(blade, 100.f, 20.f);
            EXPECT_EQ(mWorld.mPrng, sibling);
        }

        // ---- with a soul gem: rechargeItem(item, gem) ----
        // Enchant skill 30 (fixture), intelligence 50, luck 50, fatigue term 1.25:
        // intelligence term 0.2 * 50 = 10, luck term 0.1 * 50 = 5, x = (30 + 10 + 5) * 1.25 = 56.25.

        TEST_F(CraftingRechargeTest, gemSuccessRestoresTheSoulTimesRollOverXAndUsesOneDraw)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            MWWorld::Ptr gem = giveGem("test_gem", 1);

            const unsigned seed = seedWithRoll(40);
            seedWorld(seed);
            Misc::Rng::Generator sibling(seed);
            Misc::Rng::roll0to99(sibling);

            EXPECT_TRUE(MWMechanics::rechargeItem(blade, gem));

            // restored = 300 * (40 / 56.25) = 213.3333; 10 + 213.3333 is below the maximum of 500.
            EXPECT_NEAR(blade.getCellRef().getEnchantmentCharge(), 223.3333, 1e-3);
            EXPECT_EQ(mWorld.mPrng, sibling) << "exactly one draw";
            EXPECT_EQ(mWorld.mBreakInvisibilityCalls, 1);
            EXPECT_EQ(mWindow.mSounds, std::vector<std::string>({ "Enchant Success" }));
            ASSERT_EQ(mLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mLua.mSkillUses[0].mSkill, "Enchant");
            EXPECT_EQ(mLua.mSkillUses[0].mUseType, ESM::Skill::Enchant_Recharge);
            EXPECT_EQ(mPlayer.getClass().getContainerStore(mPlayer).count(rid("test_gem")), 0);
        }

        TEST_F(CraftingRechargeTest, gemSuccessIsCappedAtTheEnchantmentsCharge)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(400.f);
            MWWorld::Ptr gem = giveGem("test_gem", 1);

            seedWorld(seedWithRoll(55));
            EXPECT_TRUE(MWMechanics::rechargeItem(blade, gem));

            // 300 * 55 / 56.25 = 293.33; 400 + 293.33 is capped at 500.
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), static_cast<float>(kEnchantmentCharge));
        }

        TEST_F(CraftingRechargeTest, gemRollBelowXSucceedsAndEqualOrAboveFails)
        {
            // x = 56.25: roll 56 < x, roll 57 is not.
            {
                MWWorld::Ptr blade = give(rid("test_blade"), 1);
                blade.getCellRef().setEnchantmentCharge(10.f);
                MWWorld::Ptr gem = giveGem("test_gem", 1);
                seedWorld(seedWithRoll(56));
                MWMechanics::rechargeItem(blade, gem);
                EXPECT_EQ(mWindow.mSounds.back(), "Enchant Success");
                // The charge is a float; compared to the value the same expression gives.
                EXPECT_NEAR(blade.getCellRef().getEnchantmentCharge(), 10.f + 300.f * (56.f / 56.25f), 1e-3);
            }
            {
                MWWorld::Ptr blade = give(rid("test_blade"), 1);
                blade.getCellRef().setEnchantmentCharge(10.f);
                MWWorld::Ptr gem = giveGem("test_gem", 1);
                seedWorld(seedWithRoll(57));
                MWMechanics::rechargeItem(blade, gem);
                EXPECT_EQ(mWindow.mSounds.back(), "Enchant Fail");
                EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 10.f);
            }
        }

        TEST_F(CraftingRechargeTest, aRollEqualToAWholeThresholdFails)
        {
            // Enchant 25: x = (25 + 10 + 5) * 1.25 = 50 exactly; the test is "roll < x".
            setEnchant(25.f);
            {
                MWWorld::Ptr blade = give(rid("test_blade"), 1);
                blade.getCellRef().setEnchantmentCharge(10.f);
                MWWorld::Ptr gem = giveGem("test_gem", 1);
                seedWorld(seedWithRoll(49));
                MWMechanics::rechargeItem(blade, gem);
                EXPECT_EQ(mWindow.mSounds.back(), "Enchant Success");
            }
            {
                MWWorld::Ptr blade = give(rid("test_blade"), 1);
                blade.getCellRef().setEnchantmentCharge(10.f);
                MWWorld::Ptr gem = giveGem("test_gem", 1);
                seedWorld(seedWithRoll(50));
                MWMechanics::rechargeItem(blade, gem);
                EXPECT_EQ(mWindow.mSounds.back(), "Enchant Fail");
            }
        }

        TEST_F(CraftingRechargeTest, gemFailureStillUsesTheGemAndCountsAsSkillUse)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            MWWorld::Ptr gem = giveGem("test_gem", 1);

            seedWorld(seedWithRoll(90));
            EXPECT_TRUE(MWMechanics::rechargeItem(blade, gem));

            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 10.f);
            EXPECT_EQ(mWindow.mSounds, std::vector<std::string>({ "Enchant Fail" }));
            EXPECT_EQ(mPlayer.getClass().getContainerStore(mPlayer).count(rid("test_gem")), 0);
            ASSERT_EQ(mLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mLua.mSkillUses[0].mUseType, ESM::Skill::Enchant_Recharge);
        }

        TEST_F(CraftingRechargeTest, aUsedUpGemIsNamedInAMessage)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            MWWorld::Ptr gem = giveGem("test_gem", 1);

            seedWorld(seedWithRoll(90));
            MWMechanics::rechargeItem(blade, gem);
            EXPECT_EQ(mWindow.mMessages, std::vector<std::string>({ "Your test_gem is used up." }));
        }

        TEST_F(CraftingRechargeTest, aGemStackKeepsTheRestAndSaysNothing)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            MWWorld::Ptr gem = giveGem("test_gem", 3);

            seedWorld(seedWithRoll(90));
            MWMechanics::rechargeItem(blade, gem);
            EXPECT_EQ(mPlayer.getClass().getContainerStore(mPlayer).count(rid("test_gem")), 2);
            EXPECT_TRUE(mWindow.mMessages.empty());
        }

        TEST_F(CraftingRechargeTest, aUsedUpAzurasStarComesBack)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            MWWorld::Ptr gem = giveGem("Misc_SoulGem_Azura", 1);

            seedWorld(seedWithRoll(90));
            MWMechanics::rechargeItem(blade, gem);
            MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
            EXPECT_EQ(store.count(rid("Misc_SoulGem_Azura")), 1) << "re-added by the special case";
            ASSERT_EQ(mWindow.mMessages.size(), 1u);
            EXPECT_EQ(mWindow.mMessages[0], "Your Misc_SoulGem_Azura is used up.");
        }

        TEST_F(CraftingRechargeTest, aGemWithNoCountIsRefusedWithoutADraw)
        {
            MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            MWWorld::Ptr gem = giveGem("test_gem", 1);
            gem.getCellRef().setCount(0);

            seedWorld(5);
            Misc::Rng::Generator sibling(5);
            EXPECT_FALSE(MWMechanics::rechargeItem(blade, gem));
            EXPECT_EQ(mWorld.mPrng, sibling);
            EXPECT_EQ(mWorld.mBreakInvisibilityCalls, 0);
            EXPECT_TRUE(mWindow.mSounds.empty());
            EXPECT_TRUE(mLua.mSkillUses.empty());
        }

        TEST_F(CraftingRechargeTest, theLuckAndIntelligenceTermsAreClamped)
        {
            // luck 5: 0.1 * 5 = 0.5 < 1, so the luck term is 1: x = (30 + 10 + 1) * 1.25 = 51.25.
            stats().setAttribute(ESM::Attribute::Luck, 5.f);
            {
                MWWorld::Ptr blade = give(rid("test_blade"), 1);
                blade.getCellRef().setEnchantmentCharge(10.f);
                MWWorld::Ptr gem = giveGem("test_gem", 1);
                seedWorld(seedWithRoll(51));
                MWMechanics::rechargeItem(blade, gem);
                EXPECT_EQ(mWindow.mSounds.back(), "Enchant Success");
            }
            {
                MWWorld::Ptr blade = give(rid("test_blade"), 1);
                blade.getCellRef().setEnchantmentCharge(10.f);
                MWWorld::Ptr gem = giveGem("test_gem", 1);
                seedWorld(seedWithRoll(52));
                MWMechanics::rechargeItem(blade, gem);
                EXPECT_EQ(mWindow.mSounds.back(), "Enchant Fail");
            }
            // luck 150: 15 > 10, the luck term is 1 as well; intelligence 200: 0.2 * 200 = 40 > 20, the term is 20:
            // x = (30 + 20 + 1) * 1.25 = 63.75.
            stats().setAttribute(ESM::Attribute::Luck, 150.f);
            stats().setAttribute(ESM::Attribute::Intelligence, 200.f);
            {
                MWWorld::Ptr blade = give(rid("test_blade"), 1);
                blade.getCellRef().setEnchantmentCharge(10.f);
                MWWorld::Ptr gem = giveGem("test_gem", 1);
                seedWorld(seedWithRoll(63));
                MWMechanics::rechargeItem(blade, gem);
                EXPECT_EQ(mWindow.mSounds.back(), "Enchant Success");
            }
            {
                MWWorld::Ptr blade = give(rid("test_blade"), 1);
                blade.getCellRef().setEnchantmentCharge(10.f);
                MWWorld::Ptr gem = giveGem("test_gem", 1);
                seedWorld(seedWithRoll(64));
                MWMechanics::rechargeItem(blade, gem);
                EXPECT_EQ(mWindow.mSounds.back(), "Enchant Fail");
            }
        }
    }
}
