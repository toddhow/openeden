// Task 0098, move 5, stage 2: characterization tests of the thief rules (MWMechanics::Security: lock picking and trap
// probing; MWMechanics::Pickpocket). They pass on the current code; the move (stage 3) must keep them green and
// unedited. The rolls are chosen by seed (CraftingRulesTest::seedWithRoll) and the draw counts are compared with a
// sibling generator (RulesTest::drawsSince), so nothing here depends on how a standard library maps the generator to a
// roll. Numbers are chosen so that the arithmetic is exact in float.
#include <algorithm>

#include <components/esm3/loadcont.hpp>

#include "apps/openmw/mwmechanics/npcstats.hpp"
#include "apps/openmw/mwmechanics/pickpocket.hpp"
#include "apps/openmw/mwmechanics/security.hpp"

#include "rulesfixture.hpp"

namespace CraftingTest
{
    namespace
    {
        class RulesThiefTest : public RulesTest
        {
        protected:
            void SetUp() override
            {
                RulesTest::SetUp();
                addLockpick("test_pick", 1.f, 3);
                addProbe("test_probe", 1.f, 2);

                ESM::Spell trap;
                trap.blank();
                trap.mId = rid("test_trap");
                trap.mName = "trap";
                trap.mData.mType = ESM::Spell::ST_Spell;
                trap.mData.mCost = 5;
                trap.mData.mFlags = 0;
                mStore.insertStatic(trap);
            }

            /// Agility 50, luck 50 (from the fixture); the security skill is set here.
            void setSecurity(float skill) { stats().getSkill(ESM::Skill::Security).setBase(skill); }

            std::string_view mMessage;
            std::string_view mSound;
        };

        // Agility 50, luck 50, security 30, quality 1, fatigue term 1.25: x = (10 + 5 + 30) * 1 * 1.25 + (-1) * 6 =
        // 50.25. A roll of 50 or less opens the lock.
        TEST_F(RulesThiefTest, pickLock_succeeds_on_a_roll_up_to_the_chance)
        {
            MWWorld::Ptr pick = give(rid("test_pick"), 1);
            MWWorld::Ptr lock = makeLock(6);
            const unsigned seed = seedWithRoll(50);
            seedWorld(seed);

            MWMechanics::Security(mPlayer).pickLock(lock, pick, mMessage, mSound);

            EXPECT_EQ(lock.getCellRef().getLockLevel(), -6) << "an unlocked ref keeps minus its old level";
            EXPECT_EQ(mMessage, "#{sLockSuccess}");
            EXPECT_EQ(mSound, "Open Lock");
            EXPECT_EQ(drawsSince(seed), 1);
            ASSERT_EQ(mLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mLua.mSkillUses[0].mSkill, ESM::RefId(ESM::Skill::Security).getRefIdString());
            EXPECT_EQ(mLua.mSkillUses[0].mUseType, ESM::Skill::Security_PickLock);
            ASSERT_EQ(mRulesMechanics.mUnlockAttempts.size(), 1u);
            EXPECT_TRUE(mRulesMechanics.mUnlockAttempts[0].mItem == lock);
            EXPECT_EQ(pick.getCellRef().getCharge(), 2);
            EXPECT_EQ(countOf(rid("test_pick")), 1);
        }

        TEST_F(RulesThiefTest, pickLock_fails_on_a_roll_above_the_chance)
        {
            MWWorld::Ptr pick = give(rid("test_pick"), 1);
            MWWorld::Ptr lock = makeLock(6);
            const unsigned seed = seedWithRoll(51);
            seedWorld(seed);

            MWMechanics::Security(mPlayer).pickLock(lock, pick, mMessage, mSound);

            EXPECT_EQ(lock.getCellRef().getLockLevel(), 6);
            EXPECT_EQ(mMessage, "#{sLockFail}");
            EXPECT_EQ(mSound, "Open Lock Fail");
            EXPECT_EQ(drawsSince(seed), 1);
            EXPECT_TRUE(mLua.mSkillUses.empty());
            EXPECT_EQ(mRulesMechanics.mUnlockAttempts.size(), 1u);
            EXPECT_EQ(pick.getCellRef().getCharge(), 2);
        }

        TEST_F(RulesThiefTest, pickLock_with_a_hopeless_chance_draws_nothing_and_still_wears_the_pick)
        {
            MWWorld::Ptr pick = give(rid("test_pick"), 1);
            MWWorld::Ptr lock = makeLock(100); // x = 56.25 - 100 < 0
            const unsigned seed = seedWithRoll(0);
            seedWorld(seed);

            MWMechanics::Security(mPlayer).pickLock(lock, pick, mMessage, mSound);

            EXPECT_EQ(mMessage, "#{sLockImpossible}");
            EXPECT_EQ(mSound, "Open Lock Fail");
            EXPECT_EQ(drawsSince(seed), 0);
            EXPECT_EQ(lock.getCellRef().getLockLevel(), 100);
            EXPECT_EQ(mRulesMechanics.mUnlockAttempts.size(), 1u);
            EXPECT_EQ(pick.getCellRef().getCharge(), 2);
        }

        TEST_F(RulesThiefTest, the_last_use_removes_the_pick_from_the_inventory)
        {
            MWWorld::Ptr pick = give(rid("test_pick"), 1);
            pick.getCellRef().setCharge(1);
            MWWorld::Ptr lock = makeLock(6);
            seedWorld(seedWithRoll(99));

            MWMechanics::Security(mPlayer).pickLock(lock, pick, mMessage, mSound);

            EXPECT_EQ(mMessage, "#{sLockFail}");
            EXPECT_EQ(countOf(rid("test_pick")), 0);
        }

        TEST_F(RulesThiefTest, an_unlocked_ref_or_a_spent_pick_does_nothing_at_all)
        {
            MWWorld::Ptr pick = give(rid("test_pick"), 1);
            const unsigned seed = seedWithRoll(10);
            seedWorld(seed);

            MWWorld::Ptr open = makeLock(0);
            MWMechanics::Security(mPlayer).pickLock(open, pick, mMessage, mSound);
            EXPECT_TRUE(mMessage.empty());
            EXPECT_TRUE(mSound.empty());

            MWWorld::Ptr lock = makeLock(6);
            pick.getCellRef().setCharge(0);
            MWMechanics::Security(mPlayer).pickLock(lock, pick, mMessage, mSound);
            EXPECT_TRUE(mMessage.empty());
            EXPECT_TRUE(mSound.empty());

            EXPECT_EQ(drawsSince(seed), 0);
            EXPECT_TRUE(mRulesMechanics.mUnlockAttempts.empty());
            EXPECT_EQ(pick.getCellRef().getCharge(), 0);
        }

        TEST_F(RulesThiefTest, a_better_skill_and_a_better_pick_raise_the_chance)
        {
            // security 100, quality 1.5: x = (15 + 100) * 1.5 * 1.25 - 6 = 209.6: every roll opens the lock.
            addLockpick("test_master_pick", 1.5f, 3);
            setSecurity(100.f);
            MWWorld::Ptr pick = give(rid("test_master_pick"), 1);
            MWWorld::Ptr lock = makeLock(6);
            seedWorld(seedWithRoll(99));

            MWMechanics::Security(mPlayer).pickLock(lock, pick, mMessage, mSound);
            EXPECT_EQ(mMessage, "#{sLockSuccess}");
        }

        TEST_F(RulesThiefTest, tiredness_lowers_the_chance)
        {
            // Half the fatigue: the term is 1.25 - 0.5 * 0.5 = 1.0, so x = 45 - 6 = 39.
            stats().setFatigue(MWMechanics::DynamicStat<float>(200.f, 0.f, 100.f));
            ASSERT_FLOAT_EQ(stats().getFatigueTerm(), 1.f);
            MWWorld::Ptr pick = give(rid("test_pick"), 1);
            MWWorld::Ptr lock = makeLock(6);
            seedWorld(seedWithRoll(39));
            MWMechanics::Security(mPlayer).pickLock(lock, pick, mMessage, mSound);
            EXPECT_EQ(mMessage, "#{sLockSuccess}");

            MWWorld::Ptr lock2 = makeLock(6);
            seedWorld(seedWithRoll(40));
            MWMechanics::Security(mPlayer).pickLock(lock2, pick, mMessage, mSound);
            EXPECT_EQ(mMessage, "#{sLockFail}");
        }

        // x = ((10 + 5 + 30) + (-1) * 5) * 1 * 1.25 = 50
        TEST_F(RulesThiefTest, probeTrap_succeeds_on_a_roll_up_to_the_chance)
        {
            MWWorld::Ptr probe = give(rid("test_probe"), 1);
            MWWorld::Ptr chest = makeLock(0, rid("test_trap"));
            const unsigned seed = seedWithRoll(50);
            seedWorld(seed);

            MWMechanics::Security(mPlayer).probeTrap(chest, probe, mMessage, mSound);

            EXPECT_TRUE(chest.getCellRef().getTrap().empty());
            EXPECT_EQ(mMessage, "#{sTrapSuccess}");
            EXPECT_EQ(mSound, "Disarm Trap");
            EXPECT_EQ(drawsSince(seed), 1);
            ASSERT_EQ(mLua.mSkillUses.size(), 1u);
            EXPECT_EQ(mLua.mSkillUses[0].mUseType, ESM::Skill::Security_DisarmTrap);
            EXPECT_EQ(mRulesMechanics.mUnlockAttempts.size(), 1u);
            EXPECT_EQ(probe.getCellRef().getCharge(), 1);
        }

        TEST_F(RulesThiefTest, probeTrap_fails_on_a_roll_above_the_chance)
        {
            MWWorld::Ptr probe = give(rid("test_probe"), 1);
            MWWorld::Ptr chest = makeLock(0, rid("test_trap"));
            const unsigned seed = seedWithRoll(51);
            seedWorld(seed);

            MWMechanics::Security(mPlayer).probeTrap(chest, probe, mMessage, mSound);

            EXPECT_EQ(chest.getCellRef().getTrap(), rid("test_trap"));
            EXPECT_EQ(mMessage, "#{sTrapFail}");
            EXPECT_EQ(mSound, "Disarm Trap Fail");
            EXPECT_EQ(drawsSince(seed), 1);
            EXPECT_EQ(probe.getCellRef().getCharge(), 1);
        }

        TEST_F(RulesThiefTest, probeTrap_without_a_trap_or_uses_does_nothing)
        {
            MWWorld::Ptr probe = give(rid("test_probe"), 1);
            const unsigned seed = seedWithRoll(3);
            seedWorld(seed);

            MWWorld::Ptr clean = makeLock(0);
            MWMechanics::Security(mPlayer).probeTrap(clean, probe, mMessage, mSound);
            EXPECT_TRUE(mMessage.empty());

            MWWorld::Ptr chest = makeLock(0, rid("test_trap"));
            probe.getCellRef().setCharge(0);
            MWMechanics::Security(mPlayer).probeTrap(chest, probe, mMessage, mSound);
            EXPECT_TRUE(mMessage.empty());
            EXPECT_EQ(chest.getCellRef().getTrap(), rid("test_trap"));
            EXPECT_EQ(drawsSince(seed), 0);
        }

        TEST_F(RulesThiefTest, the_last_probe_use_removes_the_probe)
        {
            MWWorld::Ptr probe = give(rid("test_probe"), 1);
            probe.getCellRef().setCharge(1);
            MWWorld::Ptr chest = makeLock(0, rid("test_trap"));
            seedWorld(seedWithRoll(99));
            MWMechanics::Security(mPlayer).probeTrap(chest, probe, mMessage, mSound);
            EXPECT_EQ(countOf(rid("test_probe")), 0);
        }

        TEST_F(RulesThiefTest, a_trap_too_strong_for_the_skill_is_impossible_and_draws_nothing)
        {
            // security 0, agility and luck 0: x = (-1 * 5) * ... is negative.
            setSecurity(0.f);
            stats().setAttribute(ESM::Attribute::Agility, 0.f);
            stats().setAttribute(ESM::Attribute::Luck, 0.f);
            MWWorld::Ptr probe = give(rid("test_probe"), 1);
            MWWorld::Ptr chest = makeLock(0, rid("test_trap"));
            const unsigned seed = seedWithRoll(0);
            seedWorld(seed);
            MWMechanics::Security(mPlayer).probeTrap(chest, probe, mMessage, mSound);
            EXPECT_EQ(mMessage, "#{sTrapImpossible}");
            EXPECT_EQ(drawsSince(seed), 0);
        }

        class RulesPickpocketTest : public RulesTest
        {
        protected:
            void SetUp() override
            {
                RulesTest::SetUp();
                mVictim = makeNpc("test_victim");
                addMisc("test_gem", 4);
                mThiefSneak = 20.f;
                stats().getSkill(ESM::Skill::Sneak).setBase(mThiefSneak);
            }

            /// The victim has agility and luck 50 and this sneak skill.
            void setVictimSneak(float sneak)
            {
                mVictim.getClass().getNpcStats(mVictim).getSkill(ESM::Skill::Sneak).setBase(sneak);
            }

            void setVictimAttributes(float agility, float luck)
            {
                MWMechanics::NpcStats& victim = mVictim.getClass().getNpcStats(mVictim);
                victim.setAttribute(ESM::Attribute::Agility, agility);
                victim.setAttribute(ESM::Attribute::Luck, luck);
            }

            bool finishWithRoll(int roll, int* draws = nullptr)
            {
                const unsigned seed = seedWithRoll(roll);
                seedWorld(seed);
                const bool detected = MWMechanics::Pickpocket(mPlayer, mVictim).finish();
                if (draws)
                    *draws = drawsSince(seed);
                return detected;
            }

            MWWorld::Ptr mVictim;
            float mThiefSneak = 0.f;
        };

        // Thief: (10 + 5 + 20) * 1.25 = 43.75. Victim with nothing: y = 0, t = 87.5, capped to iPickMaxChance 75.
        TEST_F(RulesPickpocketTest, finish_against_a_helpless_victim_is_capped_by_the_maximum_chance)
        {
            setVictimSneak(0.f);
            setVictimAttributes(0.f, 0.f);
            int draws = -2;
            EXPECT_FALSE(finishWithRoll(75, &draws));
            EXPECT_EQ(draws, 1);
            EXPECT_TRUE(finishWithRoll(76));
        }

        // Victim attributes 50, sneak 15: y = (10 + 5 + 15) * 1.25 = 37.5, t = 87.5 - 37.5 = 50.
        TEST_F(RulesPickpocketTest, finish_detects_on_a_roll_above_the_difference)
        {
            setVictimSneak(15.f);
            EXPECT_FALSE(finishWithRoll(50));
            EXPECT_TRUE(finishWithRoll(51));
        }

        // Victim sneak 100: y = (15 + 100) * 1.25 = 143.75, t < 0, below pcSneak / iPickMinChance = 4: the chance falls
        // back to int(20 / 5) = 4.
        TEST_F(RulesPickpocketTest, a_hopeless_theft_still_slips_by_on_the_lowest_rolls)
        {
            setVictimSneak(100.f);
            EXPECT_FALSE(finishWithRoll(4));
            EXPECT_TRUE(finishWithRoll(5));
        }

        // The item raises the victim's side by 10 * fPickPocketMod * value * count: 20 for one gem (value 4), 40 for
        // two. One gem: y = (20 + 10 + 5 + 15) * 1.25 = 62.5, t = 25.
        TEST_F(RulesPickpocketTest, pick_adds_the_value_of_the_stack_to_the_victims_side)
        {
            setVictimSneak(15.f);
            MWWorld::Ptr gems = give(rid("test_gem"), 2);

            seedWorld(seedWithRoll(25));
            EXPECT_FALSE(MWMechanics::Pickpocket(mPlayer, mVictim).pick(gems, 1));
            seedWorld(seedWithRoll(26));
            EXPECT_TRUE(MWMechanics::Pickpocket(mPlayer, mVictim).pick(gems, 1));

            // Two gems: y = (40 + 30) * 1.25 = 87.5, t = 0 < 4: detected above int(4).
            seedWorld(seedWithRoll(4));
            EXPECT_FALSE(MWMechanics::Pickpocket(mPlayer, mVictim).pick(gems, 2));
            seedWorld(seedWithRoll(5));
            EXPECT_TRUE(MWMechanics::Pickpocket(mPlayer, mVictim).pick(gems, 2));
        }

        TEST_F(RulesPickpocketTest, every_attempt_makes_exactly_one_draw)
        {
            setVictimSneak(15.f);
            MWWorld::Ptr gems = give(rid("test_gem"), 1);
            const unsigned seed = seedWithRoll(30);
            seedWorld(seed);
            MWMechanics::Pickpocket(mPlayer, mVictim).pick(gems, 1);
            EXPECT_EQ(drawsSince(seed), 1);
            MWMechanics::Pickpocket(mPlayer, mVictim).finish();
            EXPECT_EQ(drawsSince(seed), 2);
        }

        TEST_F(RulesPickpocketTest, a_better_sneak_skill_lowers_the_detection_chance)
        {
            setVictimSneak(15.f);
            stats().getSkill(ESM::Skill::Sneak).setBase(40.f);
            // x = (15 + 40) * 1.25 = 68.75; y = 37.5; t = 100 capped to 75.
            EXPECT_FALSE(finishWithRoll(75));
            EXPECT_TRUE(finishWithRoll(76));
        }
    }
}
