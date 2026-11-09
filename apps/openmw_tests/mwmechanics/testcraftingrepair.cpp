// Task 0085, move 2, stage 2b: characterization tests of MWMechanics::Repair. They pass on the current code; stage 2c
// (moving the class) must keep them green and unedited. See craftingfixture.hpp for the fixture and its limits.
//
// The roll (Misc::Rng::roll0to99) is chosen by seed (CraftingRulesTest::seedWithRoll), so the numbers below do not
// depend on how a standard library maps the generator's output to a roll. "Exactly one draw" is checked by comparing
// the world's generator with a sibling that has made one draw.
#include <algorithm>

#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadweap.hpp>

#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/repair.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"

#include "craftingfixture.hpp"

namespace CraftingTest
{
    namespace
    {
        constexpr float kToolQuality = 1.5f;
        constexpr int kToolUses = 3;
        constexpr int kWeaponMaxHealth = 100;

        class CraftingRepairTest : public CraftingRulesTest
        {
        protected:
            void SetUp() override
            {
                CraftingRulesTest::SetUp();

                ESM::Repair tool;
                tool.blank();
                tool.mId = rid("test_prong");
                tool.mName = "test prong";
                tool.mData.mUses = kToolUses;
                tool.mData.mQuality = kToolQuality;
                tool.mData.mWeight = 1.f;
                tool.mData.mValue = 10;
                mStore.insertStatic(tool);

                ESM::Weapon sword;
                sword.blank();
                sword.mId = rid("test_sword");
                sword.mName = "test sword";
                sword.mData.mHealth = kWeaponMaxHealth;
                sword.mData.mWeight = 5.f;
                sword.mData.mValue = 50;
                mStore.insertStatic(sword);
            }

            /// Strength and luck 50 each, so x = (5 + 5 + armorer) * fatigue term (1.25 at full fatigue).
            void setArmorer(float skill) { stats().getSkill(ESM::Skill::Armorer).setBase(skill); }

            int stacksOfWeapons()
            {
                MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
                int stacks = 0;
                for (auto it = store.begin(MWWorld::ContainerStore::Type_Weapon); it != store.end(); ++it)
                    ++stacks;
                return stacks;
            }
        };

        TEST_F(CraftingRepairTest, fixtureStartsWithTheExpectedFatigueTerm)
        {
            // Pins the fixture itself: the other tests' thresholds depend on this number.
            EXPECT_FLOAT_EQ(stats().getFatigueTerm(), 1.25f);
        }

        // Strength 50, luck 50, armorer 100, fatigue term 1.25: x = 137.5, every roll (0 to 99) is a success.
        TEST_F(CraftingRepairTest, successRepairsByTheRollTimesQualityTimesMultiplierAndUsesOneDraw)
        {
            setArmorer(100.f);
            MWWorld::Ptr tool = give(rid("test_prong"), 1);
            MWWorld::Ptr sword = give(rid("test_sword"), 1);
            sword.getCellRef().setCharge(10);

            const unsigned seed = seedWithRoll(40);
            seedWorld(seed);
            Misc::Rng::Generator sibling(seed);
            Misc::Rng::roll0to99(sibling);

            MWMechanics::Repair repair;
            repair.setTool(tool);
            repair.repair(sword);

            // y = int(fRepairAmountMult 3 * quality 1.5 * roll 40) = 180, so 10 + 180 is capped at the 100 maximum.
            EXPECT_EQ(sword.getCellRef().getCharge(), 100);
            EXPECT_EQ(mWorld.mPrng, sibling) << "exactly one draw";
            EXPECT_EQ(mWorld.mBreakInvisibilityCalls, 1);
            EXPECT_EQ(mWindow.mSounds, std::vector<std::string>({ "Repair" }));
            EXPECT_EQ(mWindow.mMessages, std::vector<std::string>({ "#{sRepairSuccess}" }));
            ASSERT_EQ(mLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mLua.mSkillUses[0].mSkill, "Armorer");
            EXPECT_EQ(mLua.mSkillUses[0].mUseType, ESM::Skill::Armorer_Repair);
            EXPECT_FLOAT_EQ(mLua.mSkillUses[0].mScale, 1.f);
            // One use of the tool is spent.
            EXPECT_EQ(tool.getCellRef().getCharge(), kToolUses - 1);
        }

        TEST_F(CraftingRepairTest, successWithASmallRollRepairsByTheRollTimesQualityTimesMultiplier)
        {
            setArmorer(100.f);
            MWWorld::Ptr tool = give(rid("test_prong"), 1);
            MWWorld::Ptr sword = give(rid("test_sword"), 1);
            sword.getCellRef().setCharge(10);

            seedWorld(seedWithRoll(7));
            MWMechanics::Repair repair;
            repair.setTool(tool);
            repair.repair(sword);

            // y = int(3 * 1.5 * 7) = int(31.5) = 31
            EXPECT_EQ(sword.getCellRef().getCharge(), 41);
        }

        TEST_F(CraftingRepairTest, aRollOfZeroStillRepairsByOnePoint)
        {
            setArmorer(100.f);
            MWWorld::Ptr tool = give(rid("test_prong"), 1);
            MWWorld::Ptr sword = give(rid("test_sword"), 1);
            sword.getCellRef().setCharge(10);

            seedWorld(seedWithRoll(0));
            MWMechanics::Repair repair;
            repair.setTool(tool);
            repair.repair(sword);

            EXPECT_EQ(sword.getCellRef().getCharge(), 11);
        }

        TEST_F(CraftingRepairTest, aRollEqualToTheThresholdSucceedsAndOneAboveFails)
        {
            // armorer 6: x = (5 + 5 + 6) * 1.25 = 20 exactly
            // CROSS-PLATFORM ITEM (review of 2026-11-08): the boundary needs x to be exactly 20.0f in float arithmetic
            // ((0.1f * 50 + 0.1f * 50 + 6) * 1.25f). It rounds to 20 with and without FMA by reading, but it is a
            // one-ulp boundary: confirm on the Linux gate, and on aarch64 or clang if they are ever built.
            setArmorer(6.f);
            {
                MWWorld::Ptr tool = give(rid("test_prong"), 1);
                MWWorld::Ptr sword = give(rid("test_sword"), 1);
                sword.getCellRef().setCharge(10);
                seedWorld(seedWithRoll(20));
                MWMechanics::Repair repair;
                repair.setTool(tool);
                repair.repair(sword);
                EXPECT_EQ(sword.getCellRef().getCharge(), 10 + 90) << "roll 20 <= x 20 repairs int(3 * 1.5 * 20) = 90";
                EXPECT_EQ(mWindow.mMessages.back(), "#{sRepairSuccess}");
            }
            {
                MWWorld::Ptr sword = give(rid("test_sword"), 1);
                sword.getCellRef().setCharge(10);
                MWWorld::Ptr tool = give(rid("test_prong"), 1);
                seedWorld(seedWithRoll(21));
                MWMechanics::Repair repair;
                repair.setTool(tool);
                repair.repair(sword);
                EXPECT_EQ(sword.getCellRef().getCharge(), 10);
                EXPECT_EQ(mWindow.mMessages.back(), "#{sRepairFailed}");
            }
        }

        TEST_F(CraftingRepairTest, failureSpendsTheToolPlaysTheFailSoundAndGivesNoSkillUse)
        {
            setArmorer(0.f); // x = (5 + 5) * 1.25 = 12.5
            MWWorld::Ptr tool = give(rid("test_prong"), 1);
            MWWorld::Ptr sword = give(rid("test_sword"), 1);
            sword.getCellRef().setCharge(10);

            seedWorld(seedWithRoll(60));
            MWMechanics::Repair repair;
            repair.setTool(tool);
            repair.repair(sword);

            EXPECT_EQ(sword.getCellRef().getCharge(), 10);
            EXPECT_EQ(tool.getCellRef().getCharge(), kToolUses - 1);
            EXPECT_EQ(mWindow.mSounds, std::vector<std::string>({ "Repair Fail" }));
            EXPECT_EQ(mWindow.mMessages, std::vector<std::string>({ "#{sRepairFailed}" }));
            EXPECT_TRUE(mLua.mSkillUses.empty());
            EXPECT_EQ(mWorld.mBreakInvisibilityCalls, 1);
        }

        TEST_F(CraftingRepairTest, aToolWithOneUseLeftIsRemovedAndTheMessageNamesIt)
        {
            setArmorer(100.f);
            MWWorld::Ptr tool = give(rid("test_prong"), 1);
            tool.getCellRef().setCharge(1);
            MWWorld::Ptr sword = give(rid("test_sword"), 1);
            sword.getCellRef().setCharge(10);

            seedWorld(seedWithRoll(5));
            MWMechanics::Repair repair;
            repair.setTool(tool);
            repair.repair(sword);

            MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
            EXPECT_EQ(store.count(rid("test_prong")), 0);
            EXPECT_EQ(mWindow.mSounds, std::vector<std::string>({ "Repair" }));
            ASSERT_EQ(mWindow.mMessages.size(), 2u);
            EXPECT_EQ(mWindow.mMessages[0], "#{sRepairSuccess}");
            EXPECT_EQ(mWindow.mMessages[1], "Your test prong is used up.");
            EXPECT_EQ(sword.getCellRef().getCharge(), 10 + 22) << "int(3 * 1.5 * 5) = 22";
        }

        TEST_F(CraftingRepairTest, aSpareToolOfTheSameIdIsPickedUpWhenTheToolIsUsedUp)
        {
            setArmorer(100.f);
            MWWorld::Ptr tool = give(rid("test_prong"), 1);
            tool.getCellRef().setCharge(1);
            MWWorld::Ptr spare = give(rid("test_prong"), 1);
            ASSERT_NE(tool, spare) << "a used tool does not stack with a full one";
            MWWorld::Ptr sword = give(rid("test_sword"), 1);
            sword.getCellRef().setCharge(10);

            seedWorld(seedWithRoll(5));
            MWMechanics::Repair repair;
            repair.setTool(tool);
            repair.repair(sword);

            EXPECT_EQ(repair.getTool(), spare);
            EXPECT_EQ(mWindow.mSounds, std::vector<std::string>({ "Repair", "Item Repair Up" }));
            MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
            EXPECT_EQ(store.count(rid("test_prong")), 1);
        }

        TEST_F(CraftingRepairTest, aFullyRepairedItemIsRestackedWithItsTwin)
        {
            setArmorer(100.f);
            MWWorld::Ptr tool = give(rid("test_prong"), 1);
            MWWorld::Ptr swordA = give(rid("test_sword"), 1);
            swordA.getCellRef().setCharge(10);
            give(rid("test_sword"), 1);
            // Both stacks exist: a damaged sword does not stack.
            EXPECT_EQ(stacksOfWeapons(), 2);

            seedWorld(seedWithRoll(99));
            MWMechanics::Repair repair;
            repair.setTool(tool);
            repair.repair(swordA);

            EXPECT_EQ(stacksOfWeapons(), 1) << "fully repaired, it stacks with the other sword again";
            MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
            EXPECT_EQ(store.count(rid("test_sword")), 2);
        }
    }
}
