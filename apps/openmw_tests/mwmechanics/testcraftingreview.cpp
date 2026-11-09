// Review F3 and F4 of the 2026-11-09 moves review: the frame helper with no crafting rules installed, and the soul of
// a gem whose soul id is empty or names no creature (the contract says nothing throws).
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadweap.hpp>

#include <game_morrowind/crafting_rules.hpp>

#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"
#include "apps/openmw/mwworld/craftingrules.hpp"
#include "apps/openmw/mwworld/itemhandles.hpp"

#include "craftingfixture.hpp"

namespace CraftingTest
{
    namespace
    {
        namespace sdk = openeden::sdk::game;

        class CraftingReviewTest : public CraftingRulesTest
        {
        protected:
            void SetUp() override
            {
                CraftingRulesTest::SetUp();

                ESM::Creature creature;
                creature.blank();
                creature.mId = rid("review_creature");
                creature.mData.mSoul = 300;
                mStore.insertStatic(creature);

                ESM::Miscellaneous gem;
                gem.blank();
                gem.mId = rid("review_gem");
                gem.mName = "review gem";
                gem.mData.mWeight = 1.f;
                gem.mData.mValue = 100;
                mStore.insertStatic(gem);

                ESM::Weapon blade;
                blade.blank();
                blade.mId = rid("review_blade");
                blade.mData.mType = ESM::Weapon::LongBladeOneHand;
                blade.mData.mEnchant = 200;
                blade.mData.mHealth = 100;
                mStore.insertStatic(blade);
            }

            void TearDown() override
            {
                MWWorld::installCraftingRules(nullptr);
                CraftingRulesTest::TearDown();
            }

            MWWorld::ItemHandleScope mScope;
        };

        TEST_F(CraftingReviewTest, aGemSoulIsTheCreaturesSoul)
        {
            auto rules = game_morrowind::makeCraftingRules();
            const MWWorld::Ptr gem = give(rid("review_gem"), 1);
            gem.getCellRef().setSoul(rid("review_creature"));
            EXPECT_EQ(rules->gemSoul(mScope.make(gem)), 300);
        }

        TEST_F(CraftingReviewTest, aGemWithNoSoulHasSoulZeroAndDoesNotThrow)
        {
            auto rules = game_morrowind::makeCraftingRules();
            const MWWorld::Ptr gem = give(rid("review_gem"), 1);
            ASSERT_TRUE(gem.getCellRef().getSoul().empty());
            int soul = -1;
            ASSERT_NO_THROW(soul = rules->gemSoul(mScope.make(gem)));
            EXPECT_EQ(soul, 0);
        }

        TEST_F(CraftingReviewTest, aGemWhoseSoulIsNoCreatureHasSoulZeroAndDoesNotThrow)
        {
            auto rules = game_morrowind::makeCraftingRules();
            const MWWorld::Ptr gem = give(rid("review_gem"), 1);
            gem.getCellRef().setSoul(rid("no_such_creature"));
            int soul = -1;
            ASSERT_NO_THROW(soul = rules->gemSoul(mScope.make(gem)));
            EXPECT_EQ(soul, 0);
        }

        // With nothing installed the per-frame helper does nothing and says so (it is not a throw and not a write).
        TEST_F(CraftingReviewTest, theFrameHelperWithNoRulesInstalledChangesNothing)
        {
            MWWorld::installCraftingRules(nullptr);
            const MWWorld::Ptr blade = give(rid("review_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            bool recharged = true;
            ASSERT_NO_THROW(recharged = MWWorld::rechargeItemOverTime(blade, 100.f, 5.f));
            EXPECT_FALSE(recharged);
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 10.f);
        }
    }
}
