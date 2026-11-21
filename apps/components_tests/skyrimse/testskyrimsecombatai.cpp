// Task 0190 (milestone M-S5, stage S5-9): the combat AI of the Skyrim SE pack. Three groups, none needs game data:
// (1) the CSTY decoder on generated records, (2) the pure decision (combat_ai.hpp) with hand-derived numbers and
// statistics over many seeds, (3) the integration in SkyrimCombat through a fake data source and a recording writer
// (the fixture of testskyrimsecombatblock.cpp: an Iron Sword of damage 7, reach 1.0 = 141 units). The rule that
// combines
// the style numbers is OURS (docs/tasks/0190-skyrim-combat-s5-9-ai.md section 4): these tests pin THIS rule.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_ai.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/combat_style.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;
    using ContentTests::Bytes;

    // ---- (1) the CSTY decoder -----------------------------------------------------------------------------------

    struct StyleFixture
    {
        LoadOrder mOrder = [] {
            LoadOrder order;
            std::string err;
            EXPECT_TRUE(order.add("Skyrim.esm", false, err));
            EXPECT_TRUE(order.add("test.esp", false, err));
            return order;
        }();
        std::unique_ptr<Content::Plugin> mPlugin = [] {
            Content::Error error;
            const std::string header
                = ContentTests::tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str();
            return Content::Plugin::open(
                std::make_unique<std::istringstream>(header, std::ios::binary), "test.esp", error);
        }();
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mLimits, mNotes, false }; }
    };

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord rawCsty()
    {
        Content::RawRecord r;
        r.mType = Content::fourCC("CSTY");
        r.mFormId = 0x01000F00;
        return r;
    }

    Bytes floats(std::initializer_list<float> values, std::size_t bytes = 0)
    {
        Bytes b;
        for (float v : values)
            b.f32(v);
        while (bytes != 0 && b.str().size() < bytes)
            b.u8(0);
        return b;
    }

    TEST(SkyrimCombatStyleDecode, aFullRecordDecodesEveryBlock)
    {
        StyleFixture f;
        Bytes subs;
        subs.append(ContentTests::sub4("EDID", Bytes().zstring("csTestStyle")));
        subs.append(ContentTests::sub4("CSGD", floats({ 0.74f, 0.55f, 0.72f, 1.f, 4.05f, 3.2f, 1.f, 1.f, 1.f, 0.2f })));
        subs.append(ContentTests::sub4("CSME", floats({ 0.68f, 0.69f, 1.1f, 1.2f, 1.3f, 0.64f, 0.65f, 0.1f })));
        subs.append(ContentTests::sub4("CSCR", floats({ 0.73f, 0.41f, 0.25f, 0.3f })));
        subs.append(ContentTests::sub4("CSLR", floats({ 0.2f })));
        subs.append(ContentTests::sub4("DATA", Bytes().u32(5)));
        CombatStyleRecord style;
        std::string why;
        ASSERT_TRUE(decodeCombatStyle(view(subs), rawCsty(), f.context(), style, why)) << why;
        EXPECT_EQ(style.mCommon.mEditorId, "csTestStyle");
        EXPECT_TRUE(style.mHasData);
        EXPECT_EQ(style.mFlags, 5u);
        EXPECT_FLOAT_EQ(style.mOffensive, 0.74f);
        EXPECT_FLOAT_EQ(style.mDefensive, 0.55f);
        EXPECT_FLOAT_EQ(style.mGroupOffensive, 0.72f);
        EXPECT_FLOAT_EQ(style.mMeleeScore, 1.f);
        EXPECT_FLOAT_EQ(style.mMagicScore, 4.05f);
        EXPECT_FLOAT_EQ(style.mRangedScore, 3.2f);
        EXPECT_FLOAT_EQ(style.mAvoidThreatChance, 0.2f);
        EXPECT_FLOAT_EQ(style.mAttackStaggered, 0.68f);
        EXPECT_FLOAT_EQ(style.mPowerAttackStaggered, 0.69f);
        EXPECT_FLOAT_EQ(style.mPowerAttackBlocking, 1.1f);
        EXPECT_FLOAT_EQ(style.mBash, 1.2f);
        EXPECT_FLOAT_EQ(style.mBashRecoil, 1.3f);
        EXPECT_FLOAT_EQ(style.mBashAttack, 0.64f);
        EXPECT_FLOAT_EQ(style.mBashPowerAttack, 0.65f);
        EXPECT_FLOAT_EQ(style.mSpecialAttack, 0.1f);
        EXPECT_FLOAT_EQ(style.mCircle, 0.73f);
        EXPECT_FLOAT_EQ(style.mFallback, 0.41f);
        EXPECT_FLOAT_EQ(style.mFlankDistance, 0.25f);
        EXPECT_FLOAT_EQ(style.mStalkTime, 0.3f);
        EXPECT_EQ(f.mNotes.mOddSubrecords, 0u);
    }

    TEST(SkyrimCombatStyleDecode, theShortVariantsKeepTheDefaultsForWhatTheyLack)
    {
        StyleFixture f;
        // CSGD of 32 bytes (the avoid threat chance is missing), CSME of 28 (no special attack), CSCR of 8.
        Bytes subs;
        subs.append(ContentTests::sub4("CSGD", floats({ 0.5f, 0.4f, 0.3f, 1.f, 2.f, 1.f, 1.f, 1.f })));
        subs.append(ContentTests::sub4("CSME", floats({ 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f })));
        subs.append(ContentTests::sub4("CSCR", floats({ 0.6f, 0.7f })));
        CombatStyleRecord style;
        std::string why;
        ASSERT_TRUE(decodeCombatStyle(view(subs), rawCsty(), f.context(), style, why)) << why;
        EXPECT_FLOAT_EQ(style.mOffensive, 0.5f);
        EXPECT_FLOAT_EQ(style.mStaffScore, 1.f);
        EXPECT_FLOAT_EQ(style.mAvoidThreatChance, 0.2f); // the default
        EXPECT_FLOAT_EQ(style.mBashPowerAttack, 1.f);
        EXPECT_FLOAT_EQ(style.mSpecialAttack, 0.1f); // the default
        EXPECT_FLOAT_EQ(style.mCircle, 0.6f);
        EXPECT_FLOAT_EQ(style.mFallback, 0.7f);
        EXPECT_FLOAT_EQ(style.mFlankDistance, 0.2f); // the default
        EXPECT_EQ(style.mFlags, CombatStyleFlag::Dueling); // the default with no DATA
        EXPECT_EQ(f.mNotes.mOddSubrecords, 0u);
    }

    TEST(SkyrimCombatStyleDecode, aWrongSizedOrNonFiniteBlockIsOddAndLeavesTheDefaults)
    {
        StyleFixture f;
        Bytes subs;
        subs.append(ContentTests::sub4("CSGD", floats({ 0.9f, 0.9f, 0.9f }))); // 12 bytes: a short variant, read
        subs.append(ContentTests::sub4("CSME", floats({ 1.f, 1.f, 1.f }))); // 12 bytes: not 28 or 32
        subs.append(ContentTests::sub4("CSCR", floats({ std::numeric_limits<float>::quiet_NaN(), 0.5f })));
        subs.append(ContentTests::sub4("DATA", Bytes().u8(1))); // 1 byte
        CombatStyleRecord style;
        std::string why;
        ASSERT_TRUE(decodeCombatStyle(view(subs), rawCsty(), f.context(), style, why)) << why;
        // CSME, CSCR (NaN) and DATA are odd; the 12 byte CSGD is a short variant that reads three floats.
        EXPECT_EQ(f.mNotes.mOddSubrecords, 3u);
        EXPECT_FLOAT_EQ(style.mCircle, 0.22f);
        EXPECT_FLOAT_EQ(style.mAttackStaggered, 0.38f);
        EXPECT_FLOAT_EQ(style.mOffensive, 0.9f);
    }

    TEST(SkyrimCombatStyleDecode, aTruncatedSubrecordFailsWithAMessage)
    {
        StyleFixture f;
        Bytes subs;
        subs.append(ContentTests::sub4("CSGD", floats({ 1.f, 1.f })));
        std::string text = subs.str();
        text.resize(text.size() - 3); // cut into the last subrecord
        const std::span<const std::byte> cut{ reinterpret_cast<const std::byte*>(text.data()), text.size() };
        CombatStyleRecord style;
        std::string why;
        EXPECT_FALSE(decodeCombatStyle(cut, rawCsty(), f.context(), style, why));
        EXPECT_FALSE(why.empty());
    }

    // ---- (2) the pure decision -------------------------------------------------------------------------------------

    AiProfile profileOf(std::uint8_t confidence, float offensive, float defensive, float circle = 0.f,
        float fallback = 0.f, float bash = 0.76f)
    {
        AiProfile p;
        p.mKnown = true;
        p.mConfidence = confidence;
        p.mStyle.mOffensive = offensive;
        p.mStyle.mDefensive = defensive;
        p.mStyle.mCircle = circle;
        p.mStyle.mFallback = fallback;
        p.mStyle.mBash = bash;
        return p;
    }

    AiView inReach(float now)
    {
        AiView v;
        v.mNow = now;
        v.mDistance = 100.f;
        v.mReach = 150.f;
        return v;
    }

    TEST(SkyrimCombatAiPure, theFleeFractionFallsWithConfidence)
    {
        EXPECT_FLOAT_EQ(fleeHealthFraction(0), 1.0f);
        EXPECT_FLOAT_EQ(fleeHealthFraction(1), 0.5f);
        EXPECT_FLOAT_EQ(fleeHealthFraction(2), 0.25f);
        EXPECT_FLOAT_EQ(fleeHealthFraction(3), 0.1f);
        EXPECT_FLOAT_EQ(fleeHealthFraction(4), 0.f);
        EXPECT_FLOAT_EQ(fleeHealthFraction(200), 0.25f); // unknown counts as average
    }

    TEST(SkyrimCombatAiPure, theRollsAreSeededInTheUnitIntervalAndRepeat)
    {
        AiActorState a, b, c;
        a.mSeed = b.mSeed = aiSeed(77, 1);
        c.mSeed = aiSeed(78, 1);
        std::set<float> seen;
        bool differs = false;
        for (int i = 0; i < 200; ++i)
        {
            const float x = aiRoll(a);
            EXPECT_EQ(x, aiRoll(b));
            EXPECT_GE(x, 0.f);
            EXPECT_LT(x, 1.f);
            if (aiRoll(c) != x)
                differs = true;
            seen.insert(x);
        }
        EXPECT_TRUE(differs);
        EXPECT_GT(seen.size(), 190u);
        EXPECT_NE(aiSeed(77, 1), aiSeed(77, 2));
        EXPECT_NE(aiSeed(77, 1), aiSeed(78, 1));
        EXPECT_EQ(a.mRolls, 200u);
    }

    TEST(SkyrimCombatAiPure, theRollsAreRoughlyUniform)
    {
        AiActorState s;
        s.mSeed = aiSeed(5, 1);
        int below = 0;
        for (int i = 0; i < 4000; ++i)
            if (aiRoll(s) < 0.25f)
                ++below;
        EXPECT_GT(below, 900);
        EXPECT_LT(below, 1100);
    }

    TEST(SkyrimCombatAiPure, aCalmActorDoesNothing)
    {
        AiActorState s;
        const AiOutput out = aiThink(s, inReach(1.f), profileOf(2, 1.f, 0.f));
        EXPECT_EQ(out.mMove, AiMove::None);
        EXPECT_EQ(out.mAction, AiAction::None);
        EXPECT_FALSE(out.mLost);
        EXPECT_EQ(s.mMode, AiMode::Calm);
    }

    TEST(SkyrimCombatAiPure, engagingStartsAnAdvanceWithADecisionDue)
    {
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 4.f);
        EXPECT_EQ(s.mMode, AiMode::Advance);
        EXPECT_FLOAT_EQ(s.mNextDecision, 4.f);
        EXPECT_TRUE(s.mCircleDir == 1 || s.mCircleDir == -1);
        EXPECT_FALSE(s.mGuardUp);
        EXPECT_FALSE(s.mCornered);
    }

    TEST(SkyrimCombatAiPure, aDeadTargetEndsTheEngagementAndDropsTheGuard)
    {
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        s.mGuardUp = true;
        AiView v = inReach(1.f);
        v.mTargetAlive = false;
        const AiOutput out = aiThink(s, v, profileOf(2, 1.f, 0.f));
        EXPECT_TRUE(out.mLost);
        EXPECT_EQ(out.mAction, AiAction::LowerGuard);
        EXPECT_EQ(s.mMode, AiMode::Calm);
        EXPECT_FALSE(s.mGuardUp);
    }

    TEST(SkyrimCombatAiPure, outOfReachItApproachesAndInReachItDuels)
    {
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        AiView v = inReach(0.f);
        v.mDistance = 600.f;
        AiOutput out = aiThink(s, v, profileOf(2, 1.f, 0.f));
        EXPECT_EQ(out.mMove, AiMove::Approach);
        EXPECT_EQ(s.mMode, AiMode::Advance);
        v.mDistance = 100.f;
        out = aiThink(s, v, profileOf(2, 1.f, 0.f));
        EXPECT_TRUE(out.mModeChanged);
        EXPECT_EQ(s.mMode, AiMode::Duel);
        // Arriving makes the decision at once: offensive 1.0 with nothing else but the wait weight (0.15) gives an
        // attack for a roll below 1.0 / 1.15; the first few seeds below are all decided one way or the other.
        EXPECT_GE(s.mDecisions, 1u);
    }

    TEST(SkyrimCombatAiPure, aBusyFighterTakesNoNewAction)
    {
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        AiView v = inReach(0.f);
        (void)aiThink(s, v, profileOf(2, 1.f, 0.f)); // arrives
        s.mNextDecision = 0.f;
        v.mBusy = true;
        const std::uint32_t before = s.mDecisions;
        const AiOutput out = aiThink(s, v, profileOf(2, 1.f, 0.f));
        EXPECT_EQ(out.mAction, AiAction::None);
        EXPECT_EQ(out.mMove, AiMove::None);
        EXPECT_EQ(s.mDecisions, before);
    }

    // Counts the actions of `seeds` fresh engagements' first duel decision.
    std::map<AiAction, int> firstDecisions(const AiProfile& profile, const AiView& view, int seeds)
    {
        std::map<AiAction, int> counts;
        for (int i = 0; i < seeds; ++i)
        {
            AiActorState s;
            aiEngage(s, aiSeed(1000 + i, 1), view.mNow);
            const AiOutput out = aiThink(s, view, profile);
            ++counts[out.mAction];
            if (out.mMove == AiMove::Circle)
                ++counts[AiAction::Cast]; // abused as a counter for "circle": see the test
        }
        return counts;
    }

    TEST(SkyrimCombatAiPure, withNoDefenceAndNoCircleItNeverBlocksOrCircles)
    {
        const auto counts = firstDecisions(profileOf(2, 1.f, 0.f, 0.f, 0.f), inReach(0.f), 400);
        EXPECT_EQ(counts.count(AiAction::RaiseGuard), 0u);
        EXPECT_EQ(counts.count(AiAction::Cast), 0u);
        // Attack weight 1.0 against the wait weight 0.15: 1.0 / 1.15 = 87 percent attack.
        const int attacks = (counts.count(AiAction::Attack) ? counts.at(AiAction::Attack) : 0)
            + (counts.count(AiAction::PowerAttack) ? counts.at(AiAction::PowerAttack) : 0)
            + (counts.count(AiAction::Bash) ? counts.at(AiAction::Bash) : 0);
        EXPECT_GT(attacks, 400 * 0.80);
        EXPECT_LT(attacks, 400 * 0.94);
    }

    TEST(SkyrimCombatAiPure, aTargetThatIsSwingingMakesABlockFarLikelier)
    {
        const AiProfile profile = profileOf(2, 0.24f, 0.8f);
        AiView calm = inReach(0.f);
        AiView swinging = calm;
        swinging.mTargetSwinging = true;
        const auto a = firstDecisions(profile, calm, 600);
        const auto b = firstDecisions(profile, swinging, 600);
        const int blocksCalm = a.count(AiAction::RaiseGuard) ? a.at(AiAction::RaiseGuard) : 0;
        const int blocksSwing = b.count(AiAction::RaiseGuard) ? b.at(AiAction::RaiseGuard) : 0;
        // Weights: attack 0.24, block 0.8 x 0.5 = 0.4 or 0.8 x 2 = 1.6, wait 0.15: 0.4 / 0.79 = 51 percent against
        // 1.6 / 1.99 = 80 percent.
        EXPECT_GT(blocksSwing, blocksCalm);
        EXPECT_GT(blocksCalm, 600 * 0.40);
        EXPECT_LT(blocksCalm, 600 * 0.62);
        EXPECT_GT(blocksSwing, 600 * 0.72);
    }

    TEST(SkyrimCombatAiPure, aBlockerWithNoStaminaDoesNotBlock)
    {
        AiView v = inReach(0.f);
        v.mStaminaFraction = 0.05f;
        const auto counts = firstDecisions(profileOf(2, 0.24f, 1.f), v, 300);
        EXPECT_EQ(counts.count(AiAction::RaiseGuard), 0u);
    }

    TEST(SkyrimCombatAiPure, anAnimalStyleNeverBashes)
    {
        AiView v = inReach(0.f);
        v.mTargetBlocking = true;
        const auto counts = firstDecisions(profileOf(2, 1.f, 0.f, 0.f, 0.f, 0.f), v, 400);
        EXPECT_EQ(counts.count(AiAction::Bash), 0u);
    }

    TEST(SkyrimCombatAiPure, aBlockingTargetDrawsBashesAndPowerAttacks)
    {
        AiView v = inReach(0.f);
        v.mTargetBlocking = true;
        // Bash chance min(0.9, 0.5 x 1.5) = 0.75 of the attacks.
        const AiProfile profile = profileOf(2, 1.f, 0.f, 0.f, 0.f, 1.5f);
        const auto counts = firstDecisions(profile, v, 800);
        const int bashes = counts.count(AiAction::Bash) ? counts.at(AiAction::Bash) : 0;
        const int attacks = bashes + (counts.count(AiAction::Attack) ? counts.at(AiAction::Attack) : 0)
            + (counts.count(AiAction::PowerAttack) ? counts.at(AiAction::PowerAttack) : 0);
        ASSERT_GT(attacks, 600);
        EXPECT_GT(bashes, attacks * 0.65);
        EXPECT_LT(bashes, attacks * 0.85);
    }

    TEST(SkyrimCombatAiPure, theMixOfAttacksIncludesPowerAttacksWithStamina)
    {
        AiView v = inReach(0.f);
        const auto counts = firstDecisions(profileOf(2, 1.f, 0.f), v, 1500);
        const int power = counts.count(AiAction::PowerAttack) ? counts.at(AiAction::PowerAttack) : 0;
        const int normal = counts.count(AiAction::Attack) ? counts.at(AiAction::Attack) : 0;
        const int bash = counts.count(AiAction::Bash) ? counts.at(AiAction::Bash) : 0;
        EXPECT_GT(power, 0);
        EXPECT_GT(normal, power);
        EXPECT_GT(bash, 0); // the unblocked bash chance min(0.5, 0.1 x 0.76) = 0.076
        // Tired: the power chance is 0.05 instead of 0.25.
        v.mStaminaFraction = 0.4f;
        const auto tired = firstDecisions(profileOf(2, 1.f, 0.f), v, 1500);
        const int tiredPower = tired.count(AiAction::PowerAttack) ? tired.at(AiAction::PowerAttack) : 0;
        EXPECT_LT(tiredPower, power);
    }

    TEST(SkyrimCombatAiPure, aRaisedGuardHoldsThenDropsAndDecidesAgain)
    {
        AiProfile profile = profileOf(2, 0.05f, 1.f);
        AiView v = inReach(0.f);
        v.mTargetSwinging = true;
        AiActorState s;
        bool raised = false;
        for (int seed = 0; seed < 50 && !raised; ++seed)
        {
            aiEngage(s, aiSeed(seed, 1), 0.f);
            const AiOutput out = aiThink(s, v, profile);
            raised = out.mAction == AiAction::RaiseGuard;
            if (raised)
            {
                EXPECT_GE(out.mGuardHold, kAiBlockHoldMin);
                EXPECT_LE(out.mGuardHold, kAiBlockHoldMax);
                EXPECT_TRUE(s.mGuardUp);
                EXPECT_EQ(s.mMode, AiMode::Block);
                EXPECT_FLOAT_EQ(s.mUntil, out.mGuardHold);
            }
        }
        ASSERT_TRUE(raised);
        // Before the end of the hold: nothing else happens.
        AiView during = inReach(s.mUntil - 0.1f);
        AiOutput out = aiThink(s, during, profile);
        EXPECT_EQ(out.mAction, AiAction::None);
        EXPECT_EQ(out.mMove, AiMove::None);
        EXPECT_EQ(s.mMode, AiMode::Block);
        AiView after = inReach(s.mUntil);
        out = aiThink(s, after, profile);
        // At the end: the guard comes down and a new decision is taken at once (which may raise it again).
        EXPECT_EQ(s.mGuardUp, out.mAction == AiAction::RaiseGuard);
        if (!s.mGuardUp)
        {
            EXPECT_NE(s.mMode, AiMode::Block);
        }
    }

    TEST(SkyrimCombatAiPure, targetOutOfReachWhileGuardedLowersTheGuardAndAdvances)
    {
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        s.mMode = AiMode::Block;
        s.mGuardUp = true;
        s.mUntil = 10.f;
        AiView v = inReach(1.f);
        v.mDistance = 500.f;
        const AiOutput out = aiThink(s, v, profileOf(2, 1.f, 1.f));
        EXPECT_EQ(out.mAction, AiAction::LowerGuard);
        EXPECT_EQ(out.mMove, AiMove::Approach);
        EXPECT_EQ(s.mMode, AiMode::Advance);
    }

    TEST(SkyrimCombatAiPure, aSwingOfTheTargetForcesADecisionBeforeItIsDue)
    {
        const AiProfile profile = profileOf(2, 0.05f, 1.f);
        int forced = 0;
        for (int seed = 0; seed < 40; ++seed)
        {
            AiActorState s;
            aiEngage(s, aiSeed(seed, 1), 0.f);
            AiView v = inReach(0.f);
            (void)aiThink(s, v, profile); // the arrival decision
            s.mMode = AiMode::Duel;
            s.mGuardUp = false;
            s.mNextDecision = 100.f; // nothing due for a long time
            v.mNow = 1.f;
            EXPECT_EQ(aiThink(s, v, profile).mAction, AiAction::None);
            v.mTargetSwinging = true; // the rising edge
            const std::uint32_t before = s.mDecisions;
            (void)aiThink(s, v, profile);
            if (s.mDecisions == before + 1)
                ++forced;
        }
        EXPECT_EQ(forced, 40);
    }

    TEST(SkyrimCombatAiPure, circlingMovesAlongTheCircleForTheHoldThenDecidesAgain)
    {
        const AiProfile profile = profileOf(2, 0.05f, 0.f, 1.f, 0.f);
        AiActorState s;
        bool circling = false;
        for (int seed = 0; seed < 80 && !circling; ++seed)
        {
            aiEngage(s, aiSeed(seed, 1), 0.f);
            const AiOutput out = aiThink(s, inReach(0.f), profile);
            circling = out.mMove == AiMove::Circle;
        }
        ASSERT_TRUE(circling);
        EXPECT_EQ(s.mMode, AiMode::Circle);
        EXPECT_GE(s.mUntil, kAiCircleHoldMin);
        EXPECT_LE(s.mUntil, kAiCircleHoldMax);
        EXPECT_EQ(aiThink(s, inReach(s.mUntil - 0.05f), profile).mMove, AiMove::Circle);
        (void)aiThink(s, inReach(s.mUntil), profile);
        EXPECT_NE(s.mMode, AiMode::Advance);
    }

    TEST(SkyrimCombatAiPure, aFallbackStepsAwayAndIsOnlyForADuelingStyle)
    {
        AiProfile dueling = profileOf(2, 0.05f, 0.f, 0.f, 1.f);
        dueling.mStyle.mFlags = CombatStyleFlag::Dueling;
        AiView tired = inReach(0.f);
        tired.mStaminaFraction = 0.2f;
        bool away = false;
        for (int seed = 0; seed < 80 && !away; ++seed)
        {
            AiActorState s;
            aiEngage(s, aiSeed(seed, 1), 0.f);
            const AiOutput out = aiThink(s, tired, dueling);
            away = out.mMove == AiMove::Away;
            if (away)
            {
                EXPECT_EQ(s.mMode, AiMode::Fallback);
                EXPECT_FLOAT_EQ(s.mUntil, kAiFallbackHold);
            }
        }
        EXPECT_TRUE(away);
        AiProfile notDueling = dueling;
        notDueling.mStyle.mFlags = 0;
        for (int seed = 0; seed < 200; ++seed)
        {
            AiActorState s;
            aiEngage(s, aiSeed(seed, 1), 0.f);
            EXPECT_NE(aiThink(s, tired, notDueling).mMove, AiMove::Away);
        }
    }

    TEST(SkyrimCombatAiPure, aCowardFleesAtOnceAndABraveOneAtTheEdgeOfDeath)
    {
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        AiOutput out = aiThink(s, inReach(0.f), profileOf(0, 1.f, 0.f));
        EXPECT_EQ(out.mMove, AiMove::Flee);
        EXPECT_EQ(s.mMode, AiMode::Retreat);
        // Average (2): flees at 25 percent, not above.
        AiActorState t;
        aiEngage(t, aiSeed(2, 1), 0.f);
        AiView v = inReach(0.f);
        v.mHealthFraction = 0.26f;
        (void)aiThink(t, v, profileOf(2, 1.f, 0.f));
        EXPECT_NE(t.mMode, AiMode::Retreat);
        v.mHealthFraction = 0.25f;
        out = aiThink(t, v, profileOf(2, 1.f, 0.f));
        EXPECT_EQ(t.mMode, AiMode::Retreat);
        EXPECT_EQ(out.mMove, AiMove::Flee);
        // Foolhardy (4) never.
        AiActorState u;
        aiEngage(u, aiSeed(3, 1), 0.f);
        v.mHealthFraction = 0.01f;
        (void)aiThink(u, v, profileOf(4, 1.f, 0.f));
        EXPECT_NE(u.mMode, AiMode::Retreat);
    }

    TEST(SkyrimCombatAiPure, aFleeingActorDropsItsGuardAndIsLostAtTheFleeDistance)
    {
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        s.mGuardUp = true;
        AiView v = inReach(0.f);
        v.mHealthFraction = 0.1f;
        AiOutput out = aiThink(s, v, profileOf(2, 1.f, 1.f));
        EXPECT_EQ(out.mAction, AiAction::LowerGuard);
        EXPECT_EQ(out.mMove, AiMove::Flee);
        v.mDistance = 2999.f;
        EXPECT_FALSE(aiThink(s, v, profileOf(2, 1.f, 1.f)).mLost);
        v.mDistance = 3000.f;
        out = aiThink(s, v, profileOf(2, 1.f, 1.f));
        EXPECT_TRUE(out.mLost);
        EXPECT_EQ(s.mMode, AiMode::Calm);
    }

    TEST(SkyrimCombatAiPure, aCorneredActorFightsBackWhateverItsHealth)
    {
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        AiView v = inReach(0.f);
        v.mHealthFraction = 0.05f;
        (void)aiThink(s, v, profileOf(1, 1.f, 0.f));
        ASSERT_EQ(s.mMode, AiMode::Retreat);
        s.mCornered = true;
        const AiOutput out = aiThink(s, v, profileOf(1, 1.f, 0.f));
        EXPECT_EQ(s.mMode, AiMode::Duel);
        EXPECT_NE(out.mMove, AiMove::Flee);
        EXPECT_NE(aiThink(s, v, profileOf(1, 1.f, 0.f)).mMove, AiMove::Flee);
    }

    // Added after the first in-game run (docs/tasks/0190 stage log): a flee that began below
    // iAICombatRestoreHealthPercentage (60) ends when the health is back to it; a flee that ran out of ways holds
    // still while the player is far and fights only when it comes near.
    TEST(SkyrimCombatAiPure, aRetreatEndsWhenTheHealthIsBackAtTheRestoreFraction)
    {
        const AiProfile profile = profileOf(2, 1.f, 0.f);
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        AiView v = inReach(0.f);
        v.mDistance = 800.f;
        v.mHealthFraction = 0.2f;
        EXPECT_EQ(aiThink(s, v, profile).mMove, AiMove::Flee);
        ASSERT_EQ(s.mMode, AiMode::Retreat);
        v.mHealthFraction = 0.59f;
        EXPECT_EQ(aiThink(s, v, profile).mMove, AiMove::Flee); // not yet
        v.mHealthFraction = 0.6f;
        const AiOutput out = aiThink(s, v, profile);
        EXPECT_NE(out.mMove, AiMove::Flee);
        EXPECT_NE(s.mMode, AiMode::Retreat);
        // 0.6 is above the flee fraction of confidence 2 (0.25): it does not flee again.
        v.mHealthFraction = 0.55f;
        (void)aiThink(s, v, profile);
        EXPECT_NE(s.mMode, AiMode::Retreat);
    }

    TEST(SkyrimCombatAiPure, aCowardWhoFledAtFullHealthDoesNotStopAtTheRestoreFraction)
    {
        const AiProfile profile = profileOf(0, 1.f, 0.f);
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        AiView v = inReach(0.f);
        v.mDistance = 800.f;
        v.mHealthFraction = 1.0f;
        EXPECT_EQ(aiThink(s, v, profile).mMove, AiMove::Flee);
        for (int i = 0; i < 5; ++i)
            EXPECT_EQ(aiThink(s, v, profile).mMove, AiMove::Flee);
        EXPECT_EQ(s.mMode, AiMode::Retreat);
    }

    TEST(SkyrimCombatAiPure, aCowardThatRecoveredFleesAgainOnlyAfterMoreDamage)
    {
        const AiProfile profile = profileOf(0, 1.f, 0.f); // flees at or below 100 percent
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        AiView v = inReach(0.f);
        v.mDistance = 800.f;
        v.mHealthFraction = 0.5f;
        (void)aiThink(s, v, profile);
        ASSERT_EQ(s.mMode, AiMode::Retreat);
        v.mHealthFraction = 0.6f; // recovered
        (void)aiThink(s, v, profile);
        ASSERT_NE(s.mMode, AiMode::Retreat);
        v.mHealthFraction = 0.55f; // hurt a little: not 0.1 below the recovery
        (void)aiThink(s, v, profile);
        EXPECT_NE(s.mMode, AiMode::Retreat);
        v.mHealthFraction = 0.45f; // more than 0.1 below
        EXPECT_EQ(aiThink(s, v, profile).mMove, AiMove::Flee);
        EXPECT_EQ(s.mMode, AiMode::Retreat);
    }

    TEST(SkyrimCombatAiPure, aFleeWithNoWayOutHoldsWhileThePlayerIsFarAndFightsWhenItComesNear)
    {
        const AiProfile profile = profileOf(1, 1.f, 0.f);
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        AiView v = inReach(0.f);
        v.mHealthFraction = 0.2f;
        v.mDistance = 1200.f;
        (void)aiThink(s, v, profile);
        ASSERT_EQ(s.mMode, AiMode::Retreat);
        s.mFleeStuck = true; // set when every flee goal failed
        AiOutput out = aiThink(s, v, profile);
        EXPECT_EQ(out.mMove, AiMove::None);
        EXPECT_EQ(s.mMode, AiMode::Retreat);
        EXPECT_FALSE(s.mCornered);
        v.mDistance = kAiCorneredDistance + 1.f;
        EXPECT_EQ(aiThink(s, v, profile).mMove, AiMove::None);
        v.mDistance = kAiCorneredDistance;
        out = aiThink(s, v, profile);
        EXPECT_TRUE(out.mBecameCornered);
        EXPECT_TRUE(s.mCornered);
        EXPECT_NE(s.mMode, AiMode::Retreat);
    }

    TEST(SkyrimCombatAiPure, aRangedFighterHoldsAtItsRangeAndStepsBackWhenClosedIn)
    {
        const AiProfile profile = profileOf(2, 1.f, 0.f);
        AiActorState s;
        aiEngage(s, aiSeed(1, 1), 0.f);
        AiView v;
        v.mNow = 0.f;
        v.mRange = 2000.f;
        v.mReach = 150.f;
        v.mDistance = 1700.f; // beyond 0.8 x 2000
        EXPECT_EQ(aiThink(s, v, profile).mMove, AiMove::Approach);
        v.mDistance = 1500.f;
        const AiOutput out = aiThink(s, v, profile);
        EXPECT_EQ(out.mMove, AiMove::None);
        EXPECT_NE(out.mAction, AiAction::Bash); // a ranged fighter has no bash or power attack
        EXPECT_NE(out.mAction, AiAction::PowerAttack);
        v.mNow = 1.f;
        v.mDistance = 200.f; // inside the minimum
        EXPECT_EQ(aiThink(s, v, profile).mMove, AiMove::Away);
    }

    TEST(SkyrimCombatAiPure, aCasterCastsOutsideMeleeReachOnlyWhenItCan)
    {
        AiProfile mage = profileOf(2, 0.5f, 0.f);
        mage.mStyle.mMagicScore = 4.05f; // chance min(0.5, 0.15 x 4.05) = 0.5
        AiView v;
        v.mNow = 0.f;
        v.mDistance = 800.f;
        v.mReach = 150.f;
        int casts = 0;
        for (int seed = 0; seed < 400; ++seed)
        {
            AiActorState s;
            aiEngage(s, aiSeed(seed, 1), 0.f);
            v.mCanCast = true;
            if (aiThink(s, v, mage).mAction == AiAction::Cast)
                ++casts;
        }
        EXPECT_GT(casts, 400 * 0.42);
        EXPECT_LT(casts, 400 * 0.58);
        v.mCanCast = false;
        for (int seed = 0; seed < 100; ++seed)
        {
            AiActorState s;
            aiEngage(s, aiSeed(seed, 1), 0.f);
            EXPECT_NE(aiThink(s, v, mage).mAction, AiAction::Cast);
        }
        // Not inside the minimum casting distance (150 + 200).
        v.mCanCast = true;
        v.mDistance = 300.f;
        for (int seed = 0; seed < 100; ++seed)
        {
            AiActorState s;
            aiEngage(s, aiSeed(seed, 1), 0.f);
            EXPECT_NE(aiThink(s, v, mage).mAction, AiAction::Cast);
        }
    }

    TEST(SkyrimCombatAiPure, theSameSeedAndViewsGiveTheSameOutputs)
    {
        const AiProfile profile = profileOf(2, 0.5f, 0.5f, 0.5f, 0.3f);
        auto run = [&] {
            AiActorState s;
            aiEngage(s, aiSeed(42, 3), 0.f);
            std::vector<int> trace;
            for (int i = 0; i < 400; ++i)
            {
                AiView v = inReach(i * 0.1f);
                v.mTargetSwinging = (i % 17) < 4;
                v.mBusy = (i % 11) == 0;
                v.mDistance = 100.f + static_cast<float>((i * 37) % 400);
                v.mStaminaFraction = 1.f - static_cast<float>(i % 10) * 0.1f;
                const AiOutput out = aiThink(s, v, profile);
                trace.push_back(
                    static_cast<int>(out.mMove) * 100 + static_cast<int>(out.mAction) * 10 + static_cast<int>(s.mMode));
            }
            return trace;
        };
        EXPECT_EQ(run(), run());
    }

    TEST(SkyrimCombatAiPure, nonFiniteStyleNumbersDoNotBreakTheDecision)
    {
        AiProfile profile;
        profile.mKnown = true;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        profile.mStyle.mOffensive = nan;
        profile.mStyle.mDefensive = nan;
        profile.mStyle.mCircle = nan;
        profile.mStyle.mFallback = nan;
        profile.mStyle.mBash = nan;
        profile.mStyle.mPowerAttackBlocking = nan;
        for (int seed = 0; seed < 100; ++seed)
        {
            AiActorState s;
            aiEngage(s, aiSeed(seed, 1), 0.f);
            AiView v = inReach(0.f);
            v.mStaminaFraction = nan;
            v.mTargetBlocking = true;
            (void)aiThink(s, v, profile);
            EXPECT_TRUE(std::isfinite(s.mUntil));
            EXPECT_TRUE(std::isfinite(s.mNextDecision));
        }
    }

    // ---- geometry --------------------------------------------------------------------------------------------------

    TEST(SkyrimCombatAiGeometry, theApproachPointStopsAtTheStandOffOnTheLineToTheTarget)
    {
        const AiPoint p = aiApproachPoint({ 1000.0, 0.0 }, { 0.0, 0.0 }, 90.0);
        EXPECT_NEAR(p.x, 90.0, 1e-9);
        EXPECT_NEAR(p.y, 0.0, 1e-9);
        // Already nearer: where it is.
        const AiPoint q = aiApproachPoint({ 30.0, 40.0 }, { 0.0, 0.0 }, 90.0);
        EXPECT_DOUBLE_EQ(q.x, 30.0);
        EXPECT_DOUBLE_EQ(q.y, 40.0);
        // On the target: where it is (no division by zero).
        const AiPoint r = aiApproachPoint({ 5.0, 5.0 }, { 5.0, 5.0 }, 90.0);
        EXPECT_DOUBLE_EQ(r.x, 5.0);
    }

    TEST(SkyrimCombatAiGeometry, theCirclePointKeepsTheRadiusAndTurnsByTheArc)
    {
        const AiPoint p = aiCirclePoint({ 100.0, 0.0 }, { 0.0, 0.0 }, 100.0, 1, 100.0); // one radian counter-clockwise
        EXPECT_NEAR(p.x, std::cos(1.0) * 100.0, 1e-9);
        EXPECT_NEAR(p.y, std::sin(1.0) * 100.0, 1e-9);
        const AiPoint q = aiCirclePoint({ 100.0, 0.0 }, { 0.0, 0.0 }, 100.0, -1, 100.0);
        EXPECT_NEAR(q.y, -std::sin(1.0) * 100.0, 1e-9);
        // Radius from a different distance: it ends ON the circle of the radius asked for.
        const AiPoint r = aiCirclePoint({ 300.0, 400.0 }, { 0.0, 0.0 }, 120.0, 1, 50.0);
        EXPECT_NEAR(std::hypot(r.x, r.y), 120.0, 1e-9);
        EXPECT_NEAR(aiCirclePoint({ 1.0, 1.0 }, { 0.0, 0.0 }, 0.0, 1, 5.0).x, 1.0, 1e-12);
    }

    TEST(SkyrimCombatAiGeometry, theAwayPointsGoStraightAwayThenTurnThroughTheAttempts)
    {
        const AiPoint a0 = aiAwayPoint({ 100.0, 0.0 }, { 0.0, 0.0 }, 900.0, 0);
        EXPECT_NEAR(a0.x, 1000.0, 1e-9);
        EXPECT_NEAR(a0.y, 0.0, 1e-9);
        const AiPoint a1 = aiAwayPoint({ 100.0, 0.0 }, { 0.0, 0.0 }, 900.0, 1); // +45 degrees
        EXPECT_NEAR(a1.x, 100.0 + 900.0 * std::cos(std::acos(-1.0) / 4), 1e-6);
        EXPECT_NEAR(a1.y, 900.0 * std::sin(std::acos(-1.0) / 4), 1e-6);
        const AiPoint a7 = aiAwayPoint({ 100.0, 0.0 }, { 0.0, 0.0 }, 900.0, 7); // 180: back through the target
        EXPECT_NEAR(a7.x, 100.0 - 900.0, 1e-6);
        // An index out of range is the last attempt; all eight are different.
        EXPECT_NEAR(aiAwayPoint({ 100.0, 0.0 }, { 0.0, 0.0 }, 900.0, 99).x, a7.x, 1e-6);
        std::set<std::pair<long long, long long>> seen;
        for (int i = 0; i < kAiFleeAttempts; ++i)
        {
            const AiPoint p = aiAwayPoint({ 100.0, 0.0 }, { 0.0, 0.0 }, 900.0, i);
            seen.insert({ std::llround(p.x), std::llround(p.y) });
        }
        EXPECT_EQ(seen.size(), static_cast<std::size_t>(kAiFleeAttempts));
        // On the target itself it still gives a point away from it.
        EXPECT_NEAR(aiAwayPoint({ 0.0, 0.0 }, { 0.0, 0.0 }, 10.0, 0).x, 10.0, 1e-9);
    }

    // ---- (3) the integration ---------------------------------------------------------------------------------------

    constexpr WorldId kBase = 100;
    constexpr WorldId kOtherBase = 200;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;
            FighterProfile npc;
            npc.mHealthMax = 100.f;
            npc.mStaminaMax = 100.f;
            npc.mSkills[0] = 50;
            npc.mUnarmedDamage = 4.f;
            npc.mUnarmedReach = 96.f;
            npc.mAi.mKnown = true;
            npc.mAi.mConfidence = 4; // foolhardy by default: no test flees unless it says so
            mProfiles[kBase] = npc;
            mProfiles[kOtherBase] = npc;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view id) override
        {
            return id == "ironsword" ? &mSword : nullptr;
        }
        CombatSettings settings() override { return CombatSettings(); }

        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        struct Clip
        {
            CombatSubject mSubject;
            ClipSlot mSlot;
            std::string mName;
            std::uint32_t mToken;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot, std::string(request.clip), request.token });
            return true;
        }
        bool stopClip(CombatSubject subject) override
        {
            ++mStops;
            (void)subject;
            return true;
        }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            mSweeps.push_back({ subject, request });
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            return true;
        }
        bool reportDeath(ActorHandle actor) override
        {
            mDeaths.push_back(actor);
            return true;
        }
        bool faceHeading(CombatSubject subject, float heading) override
        {
            mFacings.push_back({ subject, heading });
            return true;
        }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayerState;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        std::size_t clipsOf(ClipSlot slot) const
        {
            std::size_t n = 0;
            for (const Clip& c : mClips)
                if (c.mSlot == slot)
                    ++n;
            return n;
        }

        std::vector<Clip> mClips;
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::vector<std::pair<CombatSubject, float>> mFacings;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        std::size_t mStops = 0;
    };

    ActorInfo infoOf(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct CombatAi : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        std::vector<std::string> mTrace;
        std::vector<SkyrimCombat::AiMoveIntent> mMoves; ///< everything drained so far

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.setCombatAi(true);
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
            mWriter.mStates[10] = { 100.0, 0.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 100.0, 600.0, 5.0, 0.f, 90.f, 100.f };
        }
        // Runs `n` updates of `dt`, draining the moves after each.
        void run(int n, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
            {
                mCombat.combatUpdate(dt, mWriter);
                mCombat.drainAiMoves(mMoves);
            }
        }
        std::size_t movesOf(SkyrimCombat::AiMoveIntent::Kind kind) const
        {
            std::size_t n = 0;
            for (const auto& m : mMoves)
                if (m.mKind == kind && m.mActor == mA)
                    ++n;
            return n;
        }
        const SkyrimCombat::AiMoveIntent* lastGoTo() const
        {
            for (auto it = mMoves.rbegin(); it != mMoves.rend(); ++it)
                if (it->mKind == SkyrimCombat::AiMoveIntent::Kind::GoTo)
                    return &*it;
            return nullptr;
        }
        bool traced(const std::string& text) const
        {
            for (const std::string& line : mTrace)
                if (line.find(text) != std::string::npos)
                    return true;
            return false;
        }
    };

    using Kind = SkyrimCombat::AiMoveIntent::Kind;

    TEST_F(CombatAi, withTheAiOffNothingMovesAndNothingEngages)
    {
        mCombat.setCombatAi(false);
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(40);
        EXPECT_TRUE(mMoves.empty());
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
    }

    TEST_F(CombatAi, aNamedHostileEngagesSuspendsThePackageBrainAndRunsAtThePlayer)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(3);
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
        EXPECT_EQ(mCombat.aiModeOf(mA), AiMode::Advance);
        EXPECT_EQ(movesOf(Kind::Suspend), 1u);
        ASSERT_GE(movesOf(Kind::GoTo), 1u);
        const auto* go = lastGoTo();
        ASSERT_NE(go, nullptr);
        // 600 units away: it runs, towards a point near the player: the stand-off is 0.6 x reach (the Iron Sword's 141
        // units) = 84.6 units short of the player's centre, on the line to the actor (x = 100 everywhere).
        EXPECT_EQ(go->mSpeed, ActorSpeed::Run);
        EXPECT_NEAR(go->mX, 100.0, 1e-3);
        EXPECT_NEAR(go->mY, 600.0 - 0.6 * 141.0, 0.5);
        EXPECT_TRUE(traced("ai engage guard"));
    }

    TEST_F(CombatAi, aNearTargetIsWalkedTo)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mWriter.mPlayerState.y = 400.0; // within 500: a walk
        run(2);
        const auto* go = lastGoTo();
        ASSERT_NE(go, nullptr);
        EXPECT_EQ(go->mSpeed, ActorSpeed::Walk);
    }

    TEST_F(CombatAi, aGoalIsNotReissuedWhileTheTargetStandsStillButFollowsAMovingOne)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(30, 0.1f); // 3 s, the player does not move
        EXPECT_EQ(movesOf(Kind::GoTo), 1u);
        mWriter.mPlayerState.x = 400.0; // moved 300 units
        run(5, 0.1f);
        EXPECT_EQ(movesOf(Kind::GoTo), 2u);
        EXPECT_NEAR(lastGoTo()->mX, 400.0 - 0.6 * 141.0 * 300.0 / std::hypot(300.0, 600.0), 5.0);
    }

    TEST_F(CombatAi, inReachItStopsFacesThePlayerAndAttacks)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mData.mProfiles[kBase].mAi.mStyle.mOffensive = 1.f;
        mData.mProfiles[kBase].mAi.mStyle.mDefensive = 0.f;
        mData.mProfiles[kBase].mAi.mStyle.mCircle = 0.f;
        mData.mProfiles[kBase].mAi.mStyle.mFallback = 0.f;
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mWriter.mPlayerState.y = 100.0; // in reach (141 + 22 - 4)
        run(30);
        EXPECT_EQ(movesOf(Kind::GoTo), 0u); // already in reach: no goal at all
        EXPECT_GE(mWriter.clipsOf(ClipSlot::Attack) + mWriter.clipsOf(ClipSlot::PowerAttack)
                + mWriter.clipsOf(ClipSlot::Bash),
            1u);
        bool facedPlayer = false;
        for (const auto& f : mWriter.mFacings)
            if (f.first == CombatSubject::of(mA) && std::abs(f.second - 0.f) < 1e-3f) // atan2(0, 100) = 0: +Y
                facedPlayer = true;
        EXPECT_TRUE(facedPlayer);
    }

    TEST_F(CombatAi, aHostileOnSightEngagesWithinSightAndNotBeyondOrOnAnotherFloor)
    {
        mData.mProfiles[kBase].mAi.mHostileOnSight = true;
        mData.mProfiles[kBase].mAi.mAggression = 2;
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mWriter.mPlayerState.y = kAiSightDistance + 200.f; // too far
        run(20);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        mWriter.mPlayerState = { 100.0, 600.0, 5.0 + kAiSightHeight + 50.0, 0.f, 90.f, 100.f }; // another floor
        run(20);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        mWriter.mPlayerState = { 100.0, 600.0, 5.0, 0.f, 90.f, 100.f };
        run(20);
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
        EXPECT_TRUE(traced("(on sight)"));
    }

    TEST_F(CombatAi, anActorThatIsNotHostileOnSightNeverEngagesByItself)
    {
        mCombat.onActorAdded(infoOf(10, kBase, "Guard")); // defaults: not hostile on sight
        mWriter.mPlayerState.y = 200.0;
        run(60);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_TRUE(mMoves.empty());
    }

    // The player hits the actor at `y` = 100: runs the player's timer swing and delivers a sweep that hits it.
    void playerHits(CombatAi& t, ActorHandle target)
    {
        t.mCombat.onPlayerAttack(false);
        for (int i = 0; i < 6; ++i)
            t.run(1, 0.1f);
        ASSERT_FALSE(t.mWriter.mSweeps.empty());
        SweepHit hit;
        hit.target = SweepTarget::Actor;
        hit.actor = target;
        hit.distance = 20.f;
        t.mCombat.onSweepResult(CombatSubject::ofPlayer(), t.mWriter.mSweeps.back().second.token, &hit, 1);
        t.run(1, 0.1f);
    }

    TEST_F(CombatAi, anUnaggressiveActorFightsBackWhenThePlayerHurtsIt)
    {
        mWriter.mPlayerState.y = 100.0;
        run(5);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        playerHits(*this, mA);
        ASSERT_FALSE(mWriter.mDamage.empty());
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
        EXPECT_TRUE(traced("(provoked)"));
        EXPECT_EQ(movesOf(Kind::Suspend), 1u);
    }

    TEST_F(CombatAi, aCowardHurtByThePlayerFlees)
    {
        mData.mProfiles[kBase].mAi.mConfidence = 0;
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mWriter.mPlayerState.y = 100.0;
        playerHits(*this, mA);
        run(3);
        ASSERT_EQ(mCombat.aiEngaged(), 1u);
        EXPECT_EQ(mCombat.aiModeOf(mA), AiMode::Retreat);
        const auto* go = lastGoTo();
        ASSERT_NE(go, nullptr);
        EXPECT_EQ(go->mSpeed, ActorSpeed::Run);
        // Straight away from the player (the player is north, +Y): the goal is 900 units south of the actor.
        EXPECT_NEAR(go->mX, 100.0, 1e-3);
        EXPECT_NEAR(go->mY, 0.0 - kAiFleeStep, 1e-3);
        EXPECT_EQ(mWriter.clipsOf(ClipSlot::Attack), 0u);
    }

    // Brings the actor to a low health and engaged: a named hostile of confidence 2 at 10 percent health.
    void makeFleeing(CombatAi& t)
    {
        t.mData.mProfiles[kBase].mAi.mConfidence = 2;
        t.mData.mProfiles[kBase].mAi.mStyle.mOffensive = 1.f; // what it does once cornered: attack
        t.mData.mProfiles[kBase].mAi.mStyle.mDefensive = 0.f;
        t.mData.mProfiles[kBase].mAi.mStyle.mCircle = 0.f;
        t.mData.mProfiles[kBase].mAi.mStyle.mFallback = 0.f;
        ASSERT_TRUE(t.mCombat.setTestHostile("guard"));
        t.mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        t.mWriter.mPlayerState.y = 100.0;
        t.run(3);
        ASSERT_EQ(t.mCombat.aiEngaged(), 1u);
        ASSERT_TRUE(t.mCombat.setFighterHealth(CombatSubject::of(t.mA), 10.f));
        t.run(2);
    }

    TEST_F(CombatAi, anActorBelowItsFleeHealthRunsAwayAndTriesTheNextWayWhenOneIsBlocked)
    {
        makeFleeing(*this);
        ASSERT_EQ(mCombat.aiModeOf(mA), AiMode::Retreat);
        const auto first = *lastGoTo();
        EXPECT_NEAR(first.mY, -kAiFleeStep, 1e-3);
        mCombat.aiGoalFinished(mA, GoalResult::NoPath);
        run(2, 0.5f); // a failed goal waits for the retarget period before the next
        const auto second = *lastGoTo();
        EXPECT_GT(std::hypot(second.mX - first.mX, second.mY - first.mY), 100.0); // a different direction
        EXPECT_EQ(mCombat.aiModeOf(mA), AiMode::Retreat);
        mCombat.aiGoalFinished(mA, GoalResult::Arrived); // got there: keeps running from the new place
        run(2, 0.5f);
        EXPECT_EQ(mCombat.aiModeOf(mA), AiMode::Retreat);
    }

    TEST_F(CombatAi, whenEveryWayIsBlockedItIsCorneredAndFightsBack)
    {
        makeFleeing(*this);
        for (int i = 0; i < kAiFleeAttempts + 1; ++i)
        {
            mCombat.aiGoalFinished(mA, GoalResult::Blocked);
            run(2, 0.5f);
        }
        EXPECT_NE(mCombat.aiModeOf(mA), AiMode::Retreat);
        EXPECT_TRUE(traced("cornered"));
        mWriter.mPlayerState.y = 100.0; // at its side: it attacks
        run(30);
        EXPECT_GE(mWriter.clipsOf(ClipSlot::Attack) + mWriter.clipsOf(ClipSlot::PowerAttack)
                + mWriter.clipsOf(ClipSlot::Bash),
            1u);
    }

    TEST_F(CombatAi, aFleeEndsWhenThePlayerIsFarAndThePackageBrainTakesOverAgain)
    {
        makeFleeing(*this);
        mWriter.mPlayerState.y = -3500.0; // 3500 units from the actor at y = 0: past the flee distance of 3000
        run(2);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_EQ(movesOf(Kind::Resume), 1u);
        EXPECT_TRUE(traced("ai lost guard"));
    }

    // Added after the first in-game run: a hostile that cannot get to the player (every goal ends NoPath or Blocked)
    // gives up after kAiMaxGoalFails in a row and does not engage again for kAiGiveUpCooldown seconds on its own.
    TEST_F(CombatAi, anActorWhoseGoalsKeepFailingGivesUpAndRestsBeforeEngagingAgain)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(3);
        ASSERT_EQ(mCombat.aiEngaged(), 1u);
        for (int i = 0; i < kAiMaxGoalFails; ++i)
        {
            ASSERT_EQ(mCombat.aiEngaged(), 1u) << "gave up after only " << i << " failures";
            mCombat.aiGoalFinished(mA, i % 2 == 0 ? GoalResult::NoPath : GoalResult::Blocked);
            run(1, 0.5f);
        }
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_EQ(movesOf(Kind::Resume), 1u);
        EXPECT_TRUE(traced("no way to the target"));
        run(20, 0.5f); // 10 s: still resting
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        run(20, 0.5f); // 20 s in all: past the cooldown, it engages again
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
    }

    TEST_F(CombatAi, anArrivalClearsTheCountOfFailedGoals)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(3);
        for (int round = 0; round < 3; ++round)
        {
            for (int i = 0; i < kAiMaxGoalFails - 1; ++i)
            {
                mCombat.aiGoalFinished(mA, GoalResult::NoPath);
                run(1, 0.5f);
            }
            mCombat.aiGoalFinished(mA, GoalResult::Arrived);
            run(1, 0.5f);
        }
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
    }

    TEST_F(CombatAi, aProvokedActorEngagesEvenDuringTheRest)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(3);
        for (int i = 0; i < kAiMaxGoalFails; ++i)
        {
            mCombat.aiGoalFinished(mA, GoalResult::NoPath);
            run(1, 0.5f);
        }
        ASSERT_EQ(mCombat.aiEngaged(), 0u);
        mWriter.mPlayerState.y = 100.0; // in reach now
        playerHits(*this, mA);
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
        EXPECT_TRUE(traced("(provoked)"));
    }

    TEST_F(CombatAi, aFleeThatRanOutOfWaysFarFromThePlayerWaitsAndFightsWhenItComesNear)
    {
        makeFleeing(*this);
        mWriter.mPlayerState.y = -1200.0; // 1200 away
        run(2);
        for (int i = 0; i < kAiFleeAttempts + 1; ++i)
        {
            mCombat.aiGoalFinished(mA, GoalResult::Blocked);
            run(2, 0.5f);
        }
        EXPECT_EQ(mCombat.aiModeOf(mA), AiMode::Retreat) << "far from the player it waits, it does not turn and fight";
        const std::size_t goals = movesOf(Kind::GoTo);
        run(10, 0.5f);
        EXPECT_EQ(movesOf(Kind::GoTo), goals); // no more goals while it waits
        mWriter.mPlayerState.y = -300.0; // the player comes near
        run(2);
        EXPECT_NE(mCombat.aiModeOf(mA), AiMode::Retreat);
        EXPECT_TRUE(traced("cornered"));
    }

    TEST_F(CombatAi, afterAFailedGoalTheNextOneWaitsForTheRetargetPeriod)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(2);
        const std::size_t before = movesOf(Kind::GoTo);
        ASSERT_EQ(before, 1u);
        mCombat.aiGoalFinished(mA, GoalResult::NoPath);
        run(3, 0.1f); // 0.3 s
        EXPECT_EQ(movesOf(Kind::GoTo), before) << "a path search every frame is what a failed goal must not cause";
        run(5, 0.1f); // 0.8 s
        EXPECT_EQ(movesOf(Kind::GoTo), before + 1);
    }

    TEST_F(CombatAi, damageThePlayerDoesToAnActorByAnyMeansProvokesIt)
    {
        DamageReport report;
        report.attackerIsPlayer = true;
        report.victim = mA;
        report.amount = 5.f;
        report.healthAfter = 50.f;
        mCombat.aiNoteDamage(report);
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
        EXPECT_TRUE(traced("(provoked)"));
        mCombat.aiNoteDamage(report); // again: still one
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
    }

    TEST_F(CombatAi, onlyDamagePlayerDealtThatLeavesTheVictimAliveProvokes)
    {
        DamageReport report;
        report.attackerIsPlayer = true;
        report.victim = mA;
        report.amount = 5.f;
        report.healthAfter = 0.f; // the blow killed it
        mCombat.aiNoteDamage(report);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        report.healthAfter = 20.f;
        report.attackerIsPlayer = false; // an NPC's blow
        report.attacker = ActorHandle{ 99, 1 };
        mCombat.aiNoteDamage(report);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        report.attackerIsPlayer = true;
        report.victimIsPlayer = true; // the player's own damage (a heal report is amount 0)
        mCombat.aiNoteDamage(report);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        report.victimIsPlayer = false;
        report.amount = 0.f; // nothing taken off
        mCombat.aiNoteDamage(report);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        mCombat.setCombatAi(false);
        report.amount = 3.f;
        mCombat.aiNoteDamage(report);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
    }

    TEST_F(CombatAi, theDeathOfTheActorOrThePlayerReleasesIt)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(3);
        ASSERT_EQ(mCombat.aiEngaged(), 1u);
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::ofPlayer(), 0.f));
        run(2);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_EQ(movesOf(Kind::Resume), 1u);
    }

    TEST_F(CombatAi, aRemovedActorIsForgottenWithNothingLeftToTell)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(3);
        ASSERT_EQ(mCombat.aiEngaged(), 1u);
        mCombat.onActorRemoved(mA);
        const std::size_t before = mMoves.size();
        run(10);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_EQ(mMoves.size(), before);
    }

    TEST_F(CombatAi, aHostileInReachMixesNormalPowerAndBashAttacksOverAFight)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mData.mProfiles[kBase].mAi.mStyle.mOffensive = 1.f;
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mWriter.mPlayerState.y = 100.0;
        std::size_t normal = 0, power = 0, bash = 0;
        for (int i = 0; i < 2400; ++i) // two minutes
        {
            mCombat.combatUpdate(0.05f, mWriter);
            mCombat.drainAiMoves(mMoves);
            for (const auto& c : mWriter.mClips)
            {
                normal += c.mSlot == ClipSlot::Attack;
                power += c.mSlot == ClipSlot::PowerAttack;
                bash += c.mSlot == ClipSlot::Bash;
                mCombat.onClipFinished(c.mSubject, c.mToken, ClipEnd::Completed);
            }
            mWriter.mClips.clear();
        }
        EXPECT_GT(normal, 20u);
        EXPECT_GT(power, 0u);
        EXPECT_GT(bash, 0u);
        EXPECT_GT(normal, power);
        EXPECT_GT(normal, bash);
    }

    TEST_F(CombatAi, theEveryNTestSettingsOverrideTheChoice)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mCombat.setTestPowerEvery(2);
        mCombat.setTestBashEvery(3);
        mData.mProfiles[kBase].mAi.mStyle.mOffensive = 1.f;
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mWriter.mPlayerState.y = 100.0;
        std::vector<ClipSlot> slots;
        for (int i = 0; i < 2400 && slots.size() < 6; ++i)
        {
            mCombat.combatUpdate(0.05f, mWriter);
            mCombat.drainAiMoves(mMoves);
            // Every clip the pack asked for ends at once, as the engine would tell it.
            for (const auto& c : mWriter.mClips)
            {
                if (c.mSlot == ClipSlot::Attack || c.mSlot == ClipSlot::PowerAttack || c.mSlot == ClipSlot::Bash)
                    slots.push_back(c.mSlot);
                mCombat.onClipFinished(c.mSubject, c.mToken, ClipEnd::Completed);
            }
            mWriter.mClips.clear();
        }
        // Attacks 1..6 with power every 2 and bash every 3: normal, power, bash, power, normal, power-or-bash (6 is
        // both:
        // bash wins).
        ASSERT_GE(slots.size(), 6u);
        EXPECT_EQ(slots[0], ClipSlot::Attack);
        EXPECT_EQ(slots[1], ClipSlot::PowerAttack);
        EXPECT_EQ(slots[2], ClipSlot::Bash);
        EXPECT_EQ(slots[3], ClipSlot::PowerAttack);
        EXPECT_EQ(slots[4], ClipSlot::Attack);
        EXPECT_EQ(slots[5], ClipSlot::Bash);
    }

    TEST_F(CombatAi, theTestBlockerStillBlocksAlwaysWithTheAiOn)
    {
        ASSERT_TRUE(mCombat.setTestBlock("guard"));
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        run(2);
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), v));
        EXPECT_TRUE(v.mBlocking);
    }

    TEST_F(CombatAi, aGuardRaisedByTheAiPlaysTheGuardClipAndIsTheBlockTheRulesUse)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mData.mProfiles[kBase].mAi.mStyle.mDefensive = 5.f; // almost always blocks when the player swings
        mData.mProfiles[kBase].mAi.mStyle.mOffensive = 0.f;
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mWriter.mPlayerState.y = 100.0;
        run(4);
        mCombat.setTestPlayerSwing(0.5f);
        bool blocked = false;
        for (int i = 0; i < 200 && !blocked; ++i)
        {
            run(1, 0.05f);
            SkyrimCombat::FighterView v;
            if (mCombat.fighterView(CombatSubject::of(mA), v))
                blocked = v.mBlocking;
        }
        ASSERT_TRUE(blocked);
        EXPECT_GE(mWriter.clipsOf(ClipSlot::BlockStart), 1u);
        bool guardClip = false;
        for (const auto& c : mWriter.mClips)
            if (c.mSlot == ClipSlot::BlockStart && c.mName == "1HM_BlockIdle")
                guardClip = true;
        EXPECT_TRUE(guardClip);
    }

    TEST_F(CombatAi, aCasterCastsAtARangeWhenItHasASpell)
    {
        // Needs the magic data: the fake gives none, so the AI never casts and never fails.
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mData.mProfiles[kBase].mAi.mStyle.mMagicScore = 4.05f;
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mWriter.mPlayerState.y = 900.0;
        run(100);
        EXPECT_EQ(mWriter.clipsOf(ClipSlot::Cast), 0u);
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
    }

    TEST_F(CombatAi, aCircleTurnsTheNpcAlongACircleAroundThePlayer)
    {
        ASSERT_TRUE(mCombat.setTestHostile("guard"));
        mData.mProfiles[kBase].mAi.mStyle.mCircle = 20.f; // circling dominates
        mData.mProfiles[kBase].mAi.mStyle.mOffensive = 0.f;
        mData.mProfiles[kBase].mAi.mStyle.mDefensive = 0.f;
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mWriter.mPlayerState.y = 100.0; // in reach
        bool circled = false;
        for (int i = 0; i < 100 && !circled; ++i)
        {
            run(1, 0.1f);
            circled = mCombat.aiModeOf(mA) == AiMode::Circle;
        }
        ASSERT_TRUE(circled);
        run(2, 0.1f);
        const auto* go = lastGoTo();
        ASSERT_NE(go, nullptr);
        // The goal is on the circle round the player of the radius the NPC stands at (100 here, at least 90).
        EXPECT_NEAR(std::hypot(go->mX - 100.0, go->mY - 100.0), 100.0, 1.0);
        EXPECT_GT(std::abs(go->mX - 100.0), 20.0); // it moved sideways
    }

    // Two runs of the same scenario give the same trace and the same moves, line for line.
    std::pair<std::vector<std::string>, std::vector<int>> scenario()
    {
        FakeData data;
        SkyrimCombat combat;
        FakeWriter writer;
        std::vector<std::string> trace;
        std::vector<SkyrimCombat::AiMoveIntent> moves;
        combat.setData(&data);
        combat.setTestWeapon("IronSword");
        combat.setCombatAi(true);
        combat.setTestHostile("guard");
        combat.setTrace([&](const std::string& line) { trace.push_back(line); });
        data.mProfiles[kBase].mAi.mConfidence = 1;
        data.mProfiles[kBase].mAi.mStyle.mOffensive = 0.6f;
        data.mProfiles[kBase].mAi.mStyle.mDefensive = 0.5f;
        data.mProfiles[kBase].mAi.mStyle.mCircle = 0.5f;
        data.mProfiles[kBase].mAi.mStyle.mFallback = 0.3f;
        combat.onActorAdded(infoOf(10, kBase, "Guard"));
        writer.mStates[10] = { 100.0, 0.0, 5.0, 0.f, 0.f, 0.f };
        writer.mPlayerState = { 100.0, 300.0, 5.0, 0.f, 90.f, 100.f };
        combat.setTestPlayerSwing(1.7f);
        for (int i = 0; i < 1500; ++i)
        {
            combat.combatUpdate(0.02f, writer);
            combat.drainAiMoves(moves);
            for (const auto& c : writer.mClips)
                combat.onClipFinished(c.mSubject, c.mToken, ClipEnd::Completed);
            writer.mClips.clear();
            if (!writer.mSweeps.empty())
            {
                SweepHit hit;
                hit.target = SweepTarget::Player;
                hit.distance = 20.f;
                for (const auto& s : writer.mSweeps)
                    combat.onSweepResult(s.first, s.second.token, &hit, 1);
                writer.mSweeps.clear();
            }
            writer.mStates[10].y = std::min(writer.mStates[10].y + 4.0, 200.0); // it walks up to the player
        }
        std::vector<int> ints;
        for (const auto& m : moves)
            ints.push_back(static_cast<int>(m.mKind) * 100000 + static_cast<int>(m.mX) % 1000);
        return { trace, ints };
    }

    // What the AI costs per update: prints microseconds per engaged actor (approaching a player who does not move, so
    // goals are issued once and then only thought about) and per watcher (a hostile-on-sight actor that never sees the
    // player). The fake writer's maps are part of the number, so it is an upper bound of the pack's own work.
    TEST(CombatAiCost, printsTheMicrosecondsAnUpdateTakesPerEngagedActorAndPerWatcher)
    {
        constexpr int kEngaged = 100;
        constexpr int kWatchers = 400;
        constexpr int kUpdates = 2000;
        FakeData data;
        SkyrimCombat combat;
        FakeWriter writer;
        std::vector<SkyrimCombat::AiMoveIntent> moves;
        combat.setData(&data);
        combat.setTestWeapon("IronSword");
        combat.setCombatAi(true);
        data.mProfiles[kBase].mAi.mHostileOnSight = false;
        data.mProfiles[kOtherBase].mAi.mHostileOnSight = true;
        data.mProfiles[kOtherBase].mAi.mAggression = 2;
        writer.mPlayerState = { 0.0, 0.0, 0.0, 0.f, 90.f, 100.f };
        for (int i = 0; i < kEngaged; ++i)
        {
            const WorldId ref = 1000 + i;
            combat.onActorAdded(infoOf(ref, kBase, "Fighter" + std::to_string(i)));
            writer.mStates[ref] = { 600.0 + i, 0.0, 0.0, 0.f, 0.f, 0.f };
        }
        for (int i = 0; i < kWatchers; ++i)
        {
            const WorldId ref = 5000 + i;
            combat.onActorAdded(infoOf(ref, kOtherBase, "Watcher" + std::to_string(i)));
            writer.mStates[ref] = { 9000.0 + i, 9000.0, 0.0, 0.f, 0.f, 0.f }; // far from the player
        }
        for (int i = 0; i < 200; ++i)
            combat.combatUpdate(0.05f, writer); // the scan reads every profile (4 a frame)
        for (int i = 0; i < kEngaged; ++i)
        {
            DamageReport r;
            r.attackerIsPlayer = true;
            r.victim = ActorHandle{ static_cast<WorldId>(1000 + i), 1 };
            r.amount = 1.f;
            r.healthAfter = 99.f;
            combat.aiNoteDamage(r);
        }
        EXPECT_EQ(combat.aiEngaged(), static_cast<std::size_t>(kEngaged));
        for (int i = 0; i < 20; ++i)
        {
            combat.combatUpdate(0.05f, writer);
            moves.clear();
            combat.drainAiMoves(moves);
        }
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kUpdates; ++i)
        {
            combat.combatUpdate(0.05f, writer);
            moves.clear();
            combat.drainAiMoves(moves);
        }
        const double micros
            = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        // The watchers alone, same number of updates.
        SkyrimCombat quiet;
        quiet.setData(&data);
        quiet.setTestWeapon("IronSword");
        quiet.setCombatAi(true);
        for (int i = 0; i < kWatchers; ++i)
            quiet.onActorAdded(infoOf(5000 + i, kOtherBase, "Watcher" + std::to_string(i)));
        for (int i = 0; i < 200; ++i)
            quiet.combatUpdate(0.05f, writer);
        const auto start2 = std::chrono::steady_clock::now();
        for (int i = 0; i < kUpdates; ++i)
            quiet.combatUpdate(0.05f, writer);
        const double micros2
            = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start2).count();
        std::cout << "COMBATAI-COST engaged " << kEngaged << " watchers " << kWatchers << " updates " << kUpdates
                  << " us-per-update-with-engaged-and-watchers " << micros / kUpdates << " us-per-engaged-actor-update "
                  << (micros / kUpdates - micros2 / kUpdates) / kEngaged << " us-per-update-watchers-only "
                  << micros2 / kUpdates << " us-per-watcher-update " << micros2 / kUpdates / kWatchers << "\n";
        EXPECT_LT(micros / kUpdates, 5000.0); // far below a frame
    }

    TEST(CombatAiDeterminism, twoRunsOfAFightAreIdentical)
    {
        const auto a = scenario();
        const auto b = scenario();
        ASSERT_FALSE(a.first.empty());
        EXPECT_EQ(a.first, b.first);
        EXPECT_EQ(a.second, b.second);
    }

    // Task 0324: an ally of the player (a faction a quest script made the player's ally: the Greybeards) takes the
    // player's blow as an accident and does not fight back; the hook is off by default and only the player's blows
    // count.
    TEST_F(CombatAi, aVictimAlliedToThePlayerDoesNotFightBackWhenTheHookSaysSo)
    {
        mData.mProfiles[kBase].mFactions = { 0x500 };
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mCombat.setAllyOfPlayerHook([](const std::vector<FormId>& factions) {
            return std::find(factions.begin(), factions.end(), FormId(0x500)) != factions.end();
        });
        mWriter.mPlayerState.y = 100.0;
        run(5); // the fighter's profile is read at its first update
        DamageReport report;
        report.attackerIsPlayer = true;
        report.victim = mA;
        report.amount = 5.f;
        report.healthAfter = 50.f;
        mCombat.aiNoteDamage(report);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_FALSE(traced("(provoked)"));
    }

    TEST_F(CombatAi, aVictimThatIsNotTheHooksAllyStillFightsBack)
    {
        mData.mProfiles[kBase].mFactions = { 0x501 };
        mCombat.onActorAdded(infoOf(10, kBase, "Guard"));
        mCombat.setAllyOfPlayerHook([](const std::vector<FormId>& factions) {
            return std::find(factions.begin(), factions.end(), FormId(0x500)) != factions.end();
        });
        mWriter.mPlayerState.y = 100.0;
        run(5); // the fighter's profile is read at its first update
        DamageReport report;
        report.attackerIsPlayer = true;
        report.victim = mA;
        report.amount = 5.f;
        report.healthAfter = 50.f;
        mCombat.aiNoteDamage(report);
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
    }
}
