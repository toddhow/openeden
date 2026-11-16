// Task 0236: the conditions on the effects of spells, poisons and enchantments are evaluated (magic_conditions.hpp,
// combat_effect_conditions.cpp, the CTDA lists of decodeSpell and decodeMagicEffect). Three groups:
//  - EffectCondEval: the evaluator over two or three generated actors, every condition shaped like a real record's
//    (the shapes are VERIFIED on Skyrim.esm: AlchParalysis, AlchInfluenceConfDown, PerkImpactStaggerPushFFAimed,
//    PerkMasterMindConfDownFFAimed, see the task doc);
//  - EffectCondRecords: the decoders keep the CTDAs with their parameters resolved;
//  - EffectCondCombat: the combat runtime applies an effect whose conditions hold, with no test setting, and does
//    nothing for one whose conditions fail or cannot be answered.
// Every expected value is derived by hand from the lists in the test.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/magic_conditions.hpp>
#include <game_skyrimse/magic_status.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;
    using effectconditions::Participant;
    using effectconditions::Verdict;

    constexpr FormId kKwImmuneParalysis = 0x000F23C5; // VERIFIED ids of Skyrim.esm
    constexpr FormId kKwDragon = 0x00035D59;
    constexpr FormId kKwDaedra = 0x00013797;
    constexpr FormId kKwUndead = effectconditions::kKeywordUndead;
    constexpr FormId kPerkImpact = 0x00058F5E; // any id: only equality matters
    constexpr FormId kPerkTest = 0x00ABC123;

    survey::Ctda cond(std::uint16_t fn, std::uint8_t compare, float value, std::uint32_t p1 = 0, std::uint8_t flags = 0,
        std::uint32_t runOn = 0, std::uint32_t reference = 0)
    {
        survey::Ctda c;
        c.mCompare = compare;
        c.mFlags = flags;
        c.mTypeByte = static_cast<std::uint8_t>((compare << 5) | flags);
        c.mComparison = value;
        std::memcpy(&c.mComparisonBits, &value, sizeof(value));
        c.mFunction = fn;
        c.mParam1 = p1;
        c.mRunOn = runOn;
        c.mReference = reference;
        return c;
    }

    constexpr std::uint8_t kEq = 0, kLt = 4, kGe = 3;
    constexpr std::uint8_t kOr = 1;

    /// The list of AlchParalysis (VERIFIED shape): not immune, not restrained, not a dragon.
    std::vector<survey::Ctda> paralysisList()
    {
        return { cond(560, kEq, 0.f, kKwImmuneParalysis), cond(244, kEq, 0.f), cond(560, kEq, 0.f, kKwDragon) };
    }

    /// The list of AlchInfluenceConfDown (VERIFIED shape).
    std::vector<survey::Ctda> fearList()
    {
        return { cond(700, kEq, 0.f), cond(560, kEq, 0.f, kKwUndead), cond(560, kEq, 0.f, kKwDaedra),
            cond(560, kEq, 0.f, kKwDragon), cond(715, kEq, 0.f) };
    }

    /// An actor with its own lists.
    struct Person
    {
        std::vector<FormId> mKeywords;
        std::vector<FormId> mPerks;
        std::vector<FormId> mFactions;
        std::vector<FormId> mActive;
        std::vector<std::pair<std::uint32_t, float>> mValues;
        Participant mP;
        Participant* get()
        {
            mP.mPerk.mKind = PerkParticipant::Kind::Actor;
            mP.mPerk.mKeywords = &mKeywords;
            mP.mPerk.mPerks = &mPerks;
            mP.mPerk.mFactions = &mFactions;
            mP.mPerk.mValues = &mValues;
            mP.mActiveEffects = mActive;
            return &mP;
        }
    };

    Verdict run(effectconditions::Evaluator& e, const std::vector<survey::Ctda>& list, Person* target,
        Person* caster = nullptr, Person* player = nullptr)
    {
        return e.evaluate(list, target != nullptr ? target->get() : nullptr, caster != nullptr ? caster->get() : nullptr,
            player != nullptr ? player->get() : nullptr);
    }

    // ---- the evaluator ---------------------------------------------------------------------------------------------

    TEST(EffectCondEval, theAnsweredFunctionsAreTheListedOnesAndNoOther)
    {
        for (const int fn : { 560, 448, 715, 72, 71, 69, 70, 80, 14, 277, 640, 597, 682, 699, 214, 289, 46, 77,
                 244, 700, 724 })
        {
            EXPECT_TRUE(effectconditions::functionAnswered(static_cast<std::uint16_t>(fn))) << fn;
        }
        for (const int fn : { 74, 300, 310, 719, 722, 365, 629, 566, 58, 0, 9999 })
        {
            EXPECT_FALSE(effectconditions::functionAnswered(static_cast<std::uint16_t>(fn))) << fn;
        }
    }

    TEST(EffectCondEval, onlySubjectTargetAndThePlayerReferenceCanBeResolved)
    {
        EXPECT_TRUE(effectconditions::runOnAnswered(cond(560, kEq, 0.f, 1, 0, 0)));
        EXPECT_TRUE(effectconditions::runOnAnswered(cond(560, kEq, 0.f, 1, 0, 1)));
        EXPECT_TRUE(effectconditions::runOnAnswered(cond(80, kGe, 1.f, 0, 0, 2, 0x14)));
        EXPECT_FALSE(effectconditions::runOnAnswered(cond(80, kGe, 1.f, 0, 0, 2, 0x1234)));
        for (const std::uint32_t runOn : { 3u, 4u, 5u, 6u, 7u })
        {
            EXPECT_FALSE(effectconditions::runOnAnswered(cond(560, kEq, 0.f, 1, 0, runOn))) << runOn;
        }
    }

    TEST(EffectCondEval, anEmptyListPasses)
    {
        effectconditions::Evaluator e;
        Person t;
        EXPECT_EQ(run(e, {}, &t), Verdict::Pass);
    }

    TEST(EffectCondEval, aParalysisListPassesOnAnOrdinaryActorAndFailsOnImmuneOnesAndDragons)
    {
        effectconditions::Evaluator e;
        const auto list = paralysisList();
        Person nord;
        EXPECT_EQ(run(e, list, &nord), Verdict::Pass);
        Person immune;
        immune.mKeywords = { kKwImmuneParalysis };
        EXPECT_EQ(run(e, list, &immune), Verdict::Fail);
        Person dragon;
        dragon.mKeywords = { 0x1234, kKwDragon };
        EXPECT_EQ(run(e, list, &dragon), Verdict::Fail);
        EXPECT_EQ(e.mPassed, 1u);
        EXPECT_EQ(e.mFailed, 2u);
        EXPECT_EQ(e.mUnanswered, 0u);
    }

    TEST(EffectCondEval, aFearListFailsOnTheUndeadAndTheDaedra)
    {
        effectconditions::Evaluator e;
        const auto list = fearList();
        Person nord;
        EXPECT_EQ(run(e, list, &nord), Verdict::Pass);
        Person draugr;
        draugr.mKeywords = { kKwUndead };
        EXPECT_EQ(run(e, list, &draugr), Verdict::Fail);
        Person daedra;
        daedra.mKeywords = { kKwDaedra };
        EXPECT_EQ(run(e, list, &daedra), Verdict::Fail);
    }

    TEST(EffectCondEval, isUndeadReadsTheUndeadKeywordOnTheSubject)
    {
        effectconditions::Evaluator e;
        const std::vector<survey::Ctda> list = { cond(715, kEq, 1.f) };
        Person living;
        EXPECT_EQ(run(e, list, &living), Verdict::Fail);
        Person dead;
        dead.mKeywords = { kKwUndead };
        EXPECT_EQ(run(e, list, &dead), Verdict::Pass);
    }

    TEST(EffectCondEval, runOnTargetAsksTheCasterNotTheTarget)
    {
        effectconditions::Evaluator e;
        const std::vector<survey::Ctda> list = { cond(448, kEq, 1.f, kPerkImpact, 0, 1) };
        Person target;
        Person caster;
        target.mPerks = { kPerkImpact }; // the target has it, the caster does not: fails
        EXPECT_EQ(run(e, list, &target, &caster), Verdict::Fail);
        target.mPerks.clear();
        caster.mPerks = { kPerkImpact };
        EXPECT_EQ(run(e, list, &target, &caster), Verdict::Pass);
    }

    TEST(EffectCondEval, aConditionOnTheCasterWithNoCasterIsUnanswered)
    {
        effectconditions::Evaluator e;
        Person target;
        const std::vector<survey::Ctda> list = { cond(448, kEq, 1.f, kPerkImpact, 0, 1) };
        EXPECT_EQ(run(e, list, &target), Verdict::Unanswered);
        EXPECT_EQ(e.mUnanswered, 1u);
    }

    TEST(EffectCondEval, aConditionOnTheSubjectWithNoTargetIsUnanswered)
    {
        effectconditions::Evaluator e;
        EXPECT_EQ(run(e, paralysisList(), nullptr), Verdict::Unanswered);
    }

    TEST(EffectCondEval, effectWasDualCastIsNeverTrueSoAnImpactEffectNeverApplies)
    {
        // PerkImpactStaggerPushFFAimed (VERIFIED shape): dual cast AND the Impact perk, both on the caster
        effectconditions::Evaluator e;
        const std::vector<survey::Ctda> list
            = { cond(724, kEq, 1.f, 0, 0, 1), cond(448, kEq, 1.f, kPerkImpact, 0, 1) };
        Person target;
        Person caster;
        caster.mPerks = { kPerkImpact };
        EXPECT_EQ(run(e, list, &target, &caster), Verdict::Fail);
        const std::vector<survey::Ctda> single = { cond(724, kEq, 0.f, 0, 0, 1) };
        EXPECT_EQ(run(e, single, &target, &caster), Verdict::Pass);
    }

    TEST(EffectCondEval, aMasterOfTheMindListNeedsThePerkAndAnOrGroupOfKeywords)
    {
        // PerkMasterMindConfDownFFAimed (VERIFIED shape): HasPerk (caster) AND (Daedra OR Undead OR IsUndead)
        effectconditions::Evaluator e;
        const std::vector<survey::Ctda> list = { cond(448, kEq, 1.f, kPerkImpact, 0, 1),
            cond(560, kEq, 1.f, kKwDaedra, kOr), cond(560, kEq, 1.f, kKwUndead, kOr), cond(715, kEq, 1.f, 0, kOr) };
        Person caster;
        caster.mPerks = { kPerkImpact };
        Person nord;
        EXPECT_EQ(run(e, list, &nord, &caster), Verdict::Fail); // nobody of the groups
        Person draugr;
        draugr.mKeywords = { kKwUndead };
        EXPECT_EQ(run(e, list, &draugr, &caster), Verdict::Pass);
        Person plainCaster;
        EXPECT_EQ(run(e, list, &draugr, &plainCaster), Verdict::Fail); // no perk
    }

    TEST(EffectCondEval, aFunctionTheWorldCannotAnswerFailsClosedAndIsLoggedOnce)
    {
        effectconditions::Evaluator e;
        std::vector<std::string> log;
        e.setLog([&](const std::string& line) { log.push_back(line); });
        Person target;
        const std::vector<survey::Ctda> list = { cond(74, kGe, 1.f, 0x55), cond(560, kEq, 0.f, kKwDragon) };
        EXPECT_EQ(run(e, list, &target), Verdict::Unanswered);
        EXPECT_EQ(run(e, list, &target), Verdict::Unanswered);
        EXPECT_EQ(log.size(), 1u);
        if (!log.empty())
        {
            EXPECT_NE(log[0].find("GetGlobalValue"), std::string::npos) << log[0];
        }
        EXPECT_EQ(e.mUnansweredFunctions.count(74), 1u);
        EXPECT_EQ(e.mUnanswered, 2u);
    }

    TEST(EffectCondEval, theLevelOfThePlayerReferenceNeedsAPlayer)
    {
        effectconditions::Evaluator e;
        const std::vector<survey::Ctda> list = { cond(80, kGe, 5.f, 0, 0, 2, 0x14) };
        Person target;
        Person player;
        player.mP.mPerk.mLevel = 10;
        EXPECT_EQ(run(e, list, &target, nullptr, &player), Verdict::Pass);
        player.mP.mPerk.mLevel = 4;
        EXPECT_EQ(run(e, list, &target, nullptr, &player), Verdict::Fail);
        EXPECT_EQ(run(e, list, &target), Verdict::Unanswered);
        const std::vector<survey::Ctda> other = { cond(80, kGe, 5.f, 0, 0, 2, 0x777) };
        EXPECT_EQ(run(e, other, &target, nullptr, &player), Verdict::Unanswered);
    }

    TEST(EffectCondEval, healthPercentComesFromTheFractionAndOtherValuesReadAsFull)
    {
        effectconditions::Evaluator e;
        const std::vector<survey::Ctda> low = { cond(640, kLt, 0.2f, 24) };
        Person t;
        t.get();
        t.mP.mHealthFraction = 0.1f;
        EXPECT_EQ(run(e, low, &t), Verdict::Pass);
        t.mP.mHealthFraction = 0.5f;
        EXPECT_EQ(run(e, low, &t), Verdict::Fail);
        const std::vector<survey::Ctda> magicka = { cond(640, kLt, 0.5f, 25) };
        t.mP.mMagickaFraction = 0.25f;
        EXPECT_EQ(run(e, magicka, &t), Verdict::Pass);
        const std::vector<survey::Ctda> skill = { cond(640, kEq, 1.f, 6) };
        EXPECT_EQ(run(e, skill, &t), Verdict::Pass);
    }

    TEST(EffectCondEval, aRunningEffectIsFoundByIdAndByKeyword)
    {
        effectconditions::Evaluator e;
        MagicEffectRecord shield;
        shield.mCommon.mId = 0x5000;
        shield.mKeywords = { 0x4242 };
        e.setEffectLookup([&](FormId id) { return id == 0x5000 ? &shield : nullptr; });
        Person t;
        t.mActive = { 0x5000 };
        const std::vector<survey::Ctda> byId = { cond(214, kEq, 1.f, 0x5000) };
        const std::vector<survey::Ctda> byKeyword = { cond(699, kEq, 1.f, 0x4242) };
        const std::vector<survey::Ctda> noKeyword = { cond(699, kEq, 1.f, 0x9999) };
        EXPECT_EQ(run(e, byId, &t), Verdict::Pass);
        EXPECT_EQ(run(e, byKeyword, &t), Verdict::Pass);
        EXPECT_EQ(run(e, noKeyword, &t), Verdict::Fail);
        t.mActive.clear();
        EXPECT_EQ(run(e, byId, &t), Verdict::Fail);
    }

    TEST(EffectCondEval, theStateFunctionsReadTheActorsFacts)
    {
        effectconditions::Evaluator e;
        Person t;
        t.get();
        const std::vector<survey::Ctda> restrained = { cond(244, kEq, 0.f) };
        const std::vector<survey::Ctda> commanded = { cond(700, kEq, 1.f) };
        const std::vector<survey::Ctda> dead = { cond(46, kEq, 1.f) };
        const std::vector<survey::Ctda> fighting = { cond(289, kEq, 1.f, 0) };
        EXPECT_EQ(run(e, restrained, &t), Verdict::Pass);
        EXPECT_EQ(run(e, commanded, &t), Verdict::Fail);
        EXPECT_EQ(run(e, dead, &t), Verdict::Fail);
        EXPECT_EQ(run(e, fighting, &t), Verdict::Fail);
        t.mP.mDead = true;
        t.mP.mInCombat = true;
        EXPECT_EQ(run(e, dead, &t), Verdict::Pass);
        EXPECT_EQ(run(e, fighting, &t), Verdict::Pass);
    }

    TEST(EffectCondEval, raceFactionIdAndLevelAreAnswered)
    {
        effectconditions::Evaluator e;
        Person t;
        t.get();
        t.mFactions = { 0x77 };
        t.mP.mPerk.mRace = 0x13740;
        t.mP.mPerk.mForm = 0x99;
        t.mP.mPerk.mLevel = 7;
        EXPECT_EQ(run(e, { cond(71, kEq, 1.f, 0x77) }, &t), Verdict::Pass);
        EXPECT_EQ(run(e, { cond(71, kEq, 1.f, 0x78) }, &t), Verdict::Fail);
        EXPECT_EQ(run(e, { cond(69, kEq, 1.f, 0x13740) }, &t), Verdict::Pass);
        EXPECT_EQ(run(e, { cond(72, kEq, 1.f, 0x99) }, &t), Verdict::Pass);
        EXPECT_EQ(run(e, { cond(80, kEq, 7.f) }, &t), Verdict::Pass);
        EXPECT_EQ(run(e, { cond(80, kGe, 8.f) }, &t), Verdict::Fail);
    }

    TEST(EffectCondEval, theRandomPercentFollowsTheSeedAndNoClock)
    {
        const std::vector<survey::Ctda> list = { cond(77, kLt, 50.f) };
        Person t;
        std::vector<int> first;
        std::vector<int> second;
        effectconditions::Evaluator a;
        effectconditions::Evaluator b;
        a.setSeed(12345);
        b.setSeed(12345);
        for (int i = 0; i < 40; ++i)
        {
            first.push_back(run(a, list, &t) == Verdict::Pass ? 1 : 0);
            second.push_back(run(b, list, &t) == Verdict::Pass ? 1 : 0);
        }
        EXPECT_EQ(first, second);
        EXPECT_NE(std::count(first.begin(), first.end(), 1), 0);
        EXPECT_NE(std::count(first.begin(), first.end(), 0), 0);
        effectconditions::Evaluator c;
        c.setSeed(99999);
        std::vector<int> third;
        for (int i = 0; i < 40; ++i)
            third.push_back(run(c, list, &t) == Verdict::Pass ? 1 : 0);
        EXPECT_NE(first, third);
    }

    // ---- the decoders ----------------------------------------------------------------------------------------------

    using namespace ContentTests;

    struct RecordFixture
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
            return Content::Plugin::open(
                std::make_unique<std::istringstream>(
                    tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(), std::ios::binary),
                "test.esp", error);
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

    Content::RawRecord raw(const char (&type)[5], std::uint32_t id)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC(type);
        r.mFormId = id;
        r.mOffset = 0x1234;
        return r;
    }

    Bytes ctdaBytes(std::uint16_t fn, std::uint8_t compare, float value, std::uint32_t p1, std::uint32_t runOn = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0);
        b.f32(value).u16(fn).u16(0).u32(p1).u32(0).u32(runOn).u32(0).u32(0);
        return b;
    }

    TEST(EffectCondRecords, aSpellKeepsTheConditionsOfEachEffectWithResolvedParameters)
    {
        RecordFixture f;
        std::string why;
        Bytes spel;
        spel.append(sub4("EDID", Bytes().zstring("TestSpell")));
        Bytes spit;
        spit.u32(10).u32(0).u32(1).u32(0).u32(1).u32(2).f32(0.f).u32(0).u32(0);
        spel.append(sub4("SPIT", spit));
        spel.append(sub4("EFID", Bytes().u32(0x00000555)));
        spel.append(sub4("EFIT", Bytes().f32(5.f).u32(0).u32(10)));
        spel.append(sub4("CTDA", ctdaBytes(560, 1, 1.f, 0x00035D59)));
        spel.append(sub4("CTDA", ctdaBytes(448, 0, 1.f, 0x00000123, 1)));
        spel.append(sub4("EFID", Bytes().u32(0x00000556)));
        spel.append(sub4("EFIT", Bytes().f32(6.f).u32(0).u32(0)));
        SpellRecord s;
        ASSERT_TRUE(decodeSpell(view(spel), raw("SPEL", 0x01000B10), f.context(), s, why)) << why;
        ASSERT_EQ(s.mEffects.size(), 2u);
        EXPECT_EQ(s.mEffects[0].mConditions, 2u);
        ASSERT_EQ(s.mEffects[0].mConditionList.size(), 2u);
        EXPECT_EQ(s.mEffects[0].mConditionList[0].mFunction, 560);
        EXPECT_EQ(s.mEffects[0].mConditionList[0].mCompare, 1);
        EXPECT_EQ(s.mEffects[0].mConditionList[0].mParam1, 0x00035D59u); // Skyrim.esm: master index 0
        EXPECT_EQ(s.mEffects[0].mConditionList[1].mFunction, 448);
        EXPECT_EQ(s.mEffects[0].mConditionList[1].mRunOn, 1u);
        EXPECT_EQ(s.mEffects[0].mConditionList[1].mParam1, *f.mResolver.resolve(0x123)); // this plugin's own id
        EXPECT_EQ(s.mEffects[1].mConditions, 0u);
        EXPECT_TRUE(s.mEffects[1].mConditionList.empty());
    }

    TEST(EffectCondRecords, aDamagedConditionIsCountedButNotListedSoTheEffectCannotBeEvaluated)
    {
        RecordFixture f;
        std::string why;
        Bytes spel;
        spel.append(sub4("EDID", Bytes().zstring("TestSpell2")));
        Bytes spit;
        spit.u32(10).u32(0).u32(1).u32(0).u32(1).u32(2).f32(0.f).u32(0).u32(0);
        spel.append(sub4("SPIT", spit));
        spel.append(sub4("EFID", Bytes().u32(0x00000555)));
        spel.append(sub4("EFIT", Bytes().f32(5.f).u32(0).u32(10)));
        spel.append(sub4("CTDA", Bytes().u32(1).u32(2))); // 8 bytes, not 32
        SpellRecord s;
        ASSERT_TRUE(decodeSpell(view(spel), raw("SPEL", 0x01000B11), f.context(), s, why)) << why;
        ASSERT_EQ(s.mEffects.size(), 1u);
        EXPECT_EQ(s.mEffects[0].mConditions, 1u);
        EXPECT_TRUE(s.mEffects[0].mConditionList.empty());
    }

    TEST(EffectCondRecords, aMagicEffectKeepsItsConditionsAndKeywords)
    {
        RecordFixture f;
        std::string why;
        Bytes mgef;
        mgef.append(sub4("EDID", Bytes().zstring("TestEffect")));
        Bytes data;
        for (int i = 0; i < 38; ++i)
            data.u32(0); // 152 bytes
        mgef.append(sub4("DATA", data));
        mgef.append(sub4("KWDA", Bytes().u32(0x00000777).u32(0x00013796)));
        mgef.append(sub4("CTDA", ctdaBytes(560, 0, 0.f, 0x000F23C5)));
        mgef.append(sub4("CTDA", ctdaBytes(244, 0, 0.f, 0)));
        MagicEffectRecord m;
        ASSERT_TRUE(decodeMagicEffect(view(mgef), raw("MGEF", 0x01000C11), f.context(), m, why)) << why;
        EXPECT_EQ(m.mConditions, 2u);
        ASSERT_EQ(m.mConditionList.size(), 2u);
        EXPECT_EQ(m.mConditionList[0].mParam1, 0x000F23C5u);
        EXPECT_EQ(m.mConditionList[1].mFunction, 244);
        ASSERT_EQ(m.mKeywords.size(), 2u);
        EXPECT_EQ(m.mKeywords[0], *f.mResolver.resolve(0x777));
        EXPECT_EQ(m.mKeywords[1], 0x00013796u);
    }

    // ---- the combat runtime ----------------------------------------------------------------------------------------

    constexpr WorldId kArcherBase = 100, kBrenuinBase = 200, kCedricBase = 300;
    constexpr FormId kParalyze = 0x1000, kFear = 0x1002, kPerkBurn = 0x1003, kGlobalBurn = 0x1004, kMismatch = 0x1005,
                     kPlain = 0x1006, kSpellGated = 0x1007;
    constexpr FormId kParaEffect = 0x2000, kFearEffect = 0x2002, kPerkEffect = 0x2003, kGlobalEffect = 0x2004,
                     kMismatchEffect = 0x2005, kPlainEffect = 0x2006, kGatedEffect = 0x2007;
    constexpr FormId kProjectile = 0x3000;

    class CondData final : public CombatData
    {
    public:
        CondData()
        {
            mProj.mCommon.mId = kProjectile;
            mProj.mCommon.mEditorId = "TestBoltProjectile";
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 2500.f;
            mProj.mGravity = 0.f;
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\TestBolt.nif";

            constexpr std::uint32_t hostile = MagicFlag::Hostile | MagicFlag::Detrimental;
            addEffect(kParaEffect, StatusArchetype::Paralysis, 53, hostile);
            mEffects[kParaEffect].mConditionList = paralysisList();
            mEffects[kParaEffect].mConditions = 3;
            addEffect(kFearEffect, StatusArchetype::Demoralize, 1, MagicFlag::Hostile);
            mEffects[kFearEffect].mConditionList = fearList();
            mEffects[kFearEffect].mConditions = 5;
            addEffect(kPerkEffect, MagicArchetype::ValueModifier, 24, hostile);
            mEffects[kPerkEffect].mConditionList = { cond(448, kEq, 1.f, kPerkTest, 0, 1) };
            mEffects[kPerkEffect].mConditions = 1;
            addEffect(kGlobalEffect, MagicArchetype::ValueModifier, 24, hostile);
            mEffects[kGlobalEffect].mConditionList = { cond(74, kGe, 1.f, 0x55) };
            mEffects[kGlobalEffect].mConditions = 1;
            addEffect(kMismatchEffect, MagicArchetype::ValueModifier, 24, hostile);
            mEffects[kMismatchEffect].mConditions = 2; // counted, never listed: the pack cannot evaluate it
            addEffect(kPlainEffect, MagicArchetype::ValueModifier, 24, hostile);
            addEffect(kGatedEffect, MagicArchetype::ValueModifier, 24, hostile);

            addSpell(kParalyze, "Paralyze", 5, MagicDelivery::Aimed, kParaEffect, 0.f, 6);
            addSpell(kFear, "Fear", 5, MagicDelivery::Aimed, kFearEffect, 8.f, 12);
            addSpell(kPerkBurn, "PerkBurn", 5, MagicDelivery::Aimed, kPerkEffect, 10.f, 0);
            addSpell(kGlobalBurn, "GlobalBurn", 5, MagicDelivery::Aimed, kGlobalEffect, 10.f, 0);
            addSpell(kMismatch, "Mismatch", 5, MagicDelivery::Aimed, kMismatchEffect, 10.f, 0);
            addSpell(kPlain, "Plain", 5, MagicDelivery::Aimed, kPlainEffect, 10.f, 0);
            addSpell(kSpellGated, "SpellGated", 5, MagicDelivery::Aimed, kGatedEffect, 10.f, 0);
            // the SPEL's own condition on its effect: it only lands on a dragon
            mSpells[kSpellGated].mEffects[0].mConditionList = { cond(560, kEq, 1.f, kKwDragon) };
            mSpells[kSpellGated].mEffects[0].mConditions = 1;
            for (const FormId id : { kParaEffect, kFearEffect, kPerkEffect, kGlobalEffect, kMismatchEffect, kPlainEffect,
                     kGatedEffect })
            {
                mEffects[id].mProjectile = kProjectile;
            }

            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;

            FighterProfile npc;
            npc.mHealthMax = 50.f;
            npc.mStaminaMax = 80.f;
            npc.mMagickaMax = 100.f;
            npc.mUnarmedDamage = 4.f;
            npc.mUnarmedReach = 96.f;
            npc.mSkills[0] = 50;
            npc.mAi.mKnown = true;
            npc.mAi.mConfidence = 4;
            npc.mLevel = 3;
            mProfiles[kArcherBase] = npc;
            mProfiles[kBrenuinBase] = npc; // an ordinary actor
            mProfiles[kCedricBase] = npc;
            mProfiles[kCedricBase].mKeywords = { kKwDragon };
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
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
        const SpellRecord* spellByEditorId(std::string_view editorId) override
        {
            for (const auto& entry : mSpells)
            {
                std::string lower = entry.second.mCommon.mEditorId;
                for (char& c : lower)
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + 32);
                if (lower == editorId)
                    return &entry.second;
            }
            return nullptr;
        }
        const SpellRecord* spellById(FormId id) override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        const ProjectileRecord* projectileById(FormId id) override { return id == kProjectile ? &mProj : nullptr; }

        void addEffect(FormId id, std::uint32_t archetype, std::int32_t value, std::uint32_t flags)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            mEffects[id] = m;
        }
        void addSpell(FormId id, const char* name, std::uint32_t cost, std::uint32_t delivery, FormId effect,
            float magnitude, std::uint32_t duration)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mCost = cost;
            s.mCastType = MagicCast::FireAndForget;
            s.mDelivery = delivery;
            SpellEffect se;
            se.mEffect = effect;
            se.mMagnitude = magnitude;
            se.mDuration = duration;
            s.mEffects.push_back(se);
            mSpells[id] = s;
        }

        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        ProjectileRecord mProj;
        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class CondWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (kind != ClipEventKind::SpellFire)
                return false;
            if (clip == "MRh_Release")
                out = 0.167f;
            else if (clip == "MRh_SelfRelease")
                out = 0.367f;
            else
                return false;
            return true;
        }
        bool launchProjectile(CombatSubject, const ProjectileRequest& request) override
        {
            mLaunches.push_back(request.token);
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
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
        std::vector<std::uint32_t> mLaunches;
        std::vector<DamageReport> mDamage;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo infoOf(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct EffectCondCombat : ::testing::Test
    {
        CondData mData;
        SkyrimCombat mCombat;
        CondWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };
        ActorHandle mC{ 30, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void start()
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.setCombatAi(true);
            mCombat.onActorAdded(infoOf(10, kArcherBase, "Archer"));
            mCombat.onActorAdded(infoOf(20, kBrenuinBase, "Brenuin"));
            mCombat.onActorAdded(infoOf(30, kCedricBase, "Cedric"));
            SubjectState a;
            mWriter.mStates[10] = a;
            SubjectState b;
            b.x = 800.0;
            mWriter.mStates[20] = b;
            SubjectState c;
            c.x = 800.0;
            c.y = 150.0;
            mWriter.mStates[30] = c;
            mWriter.mPlayerState.x = 0.0;
            mWriter.mPlayerState.y = 600.0;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n, float seconds = 0.25f)
        {
            for (int i = 0; i < n; ++i)
                update(seconds);
        }
        [[nodiscard]] bool traced(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        [[nodiscard]] std::size_t traceCount(const std::string& text) const
        {
            return static_cast<std::size_t>(std::count_if(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; }));
        }
        SkyrimCombat::FighterView view(ActorHandle who)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::of(who), v));
            return v;
        }
        // The player casts `spell` and the bolt hits `victim`.
        void playerCast(const char* spell, ActorHandle victim)
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell(spell));
            const std::size_t before = mWriter.mLaunches.size();
            mCombat.onPlayerAttackEdge(true);
            updates(3);
            mCombat.onPlayerAttackEdge(false);
            ASSERT_EQ(mWriter.mLaunches.size(), before + 1) << spell;
            ProjectileResult r;
            r.owner = CombatSubject::ofPlayer();
            r.token = mWriter.mLaunches.back();
            r.fate = ProjectileFate::HitActor;
            r.actor = victim;
            mCombat.onProjectileResult(r);
            update();
        }
    };

    TEST_F(EffectCondCombat, aParalysisLandsOnAnOrdinaryActorWithNoTestSetting)
    {
        start();
        playerCast("Paralyze", mB);
        EXPECT_TRUE(mCombat.paralyzed(CombatSubject::of(mB)));
        EXPECT_EQ(mCombat.effectConditionsPassed(), 1u);
        EXPECT_EQ(mCombat.effectConditionsFailed(), 0u);
    }

    TEST_F(EffectCondCombat, aParalysisIsRefusedOnADragon)
    {
        start();
        playerCast("Paralyze", mC); // Cedric has ActorTypeDragon
        EXPECT_FALSE(mCombat.paralyzed(CombatSubject::of(mC)));
        EXPECT_EQ(mCombat.effectConditionsFailed(), 1u);
        EXPECT_EQ(mCombat.statusesApplied(), 0u);
        EXPECT_TRUE(traced("refused"));
    }

    TEST_F(EffectCondCombat, aParalysisIsRefusedOnAnImmuneActor)
    {
        mData.mProfiles[kBrenuinBase].mKeywords = { kKwImmuneParalysis };
        start();
        playerCast("Paralyze", mB);
        EXPECT_FALSE(mCombat.paralyzed(CombatSubject::of(mB)));
        EXPECT_EQ(mCombat.effectConditionsFailed(), 1u);
    }

    TEST_F(EffectCondCombat, theTestSettingStillAppliesEverythingWhateverTheConditionsSay)
    {
        mCombat.setTestSpellConditions(true);
        start();
        playerCast("Paralyze", mC);
        EXPECT_TRUE(mCombat.paralyzed(CombatSubject::of(mC)));
        EXPECT_EQ(mCombat.effectConditionsFailed(), 0u); // nothing was evaluated
    }

    TEST_F(EffectCondCombat, aFearLandsOnAnOrdinaryActorAndIsRefusedOnTheUndead)
    {
        mData.mProfiles[kCedricBase].mKeywords = { kKwUndead };
        start();
        playerCast("Fear", mB);
        EXPECT_TRUE(view(mB).mDemoralized);
        EXPECT_EQ(mCombat.effectConditionsPassed(), 1u);
        playerCast("Fear", mC);
        EXPECT_FALSE(view(mC).mDemoralized);
        EXPECT_EQ(mCombat.effectConditionsFailed(), 1u);
    }

    TEST_F(EffectCondCombat, anEffectGatedOnTheCastersPerkIsRefusedWhenTheCasterLacksIt)
    {
        start();
        playerCast("PerkBurn", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 50.f);
        EXPECT_EQ(mCombat.effectConditionsFailed(), 1u);
    }

    TEST_F(EffectCondCombat, anEffectGatedOnTheCastersPerkAppliesWhenTheCasterHasIt)
    {
        mData.mPlayer.mPerks = { kPerkTest };
        start();
        playerCast("PerkBurn", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 40.f); // 50 - 10
        EXPECT_EQ(mCombat.effectConditionsPassed(), 1u);
    }

    TEST_F(EffectCondCombat, anEffectWithAnUnanswerableConditionDoesNothingAndIsLoggedOnce)
    {
        start();
        playerCast("GlobalBurn", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 50.f);
        EXPECT_EQ(mCombat.effectConditionsUnanswered(), 1u);
        playerCast("GlobalBurn", mB);
        EXPECT_EQ(mCombat.effectConditionsUnanswered(), 2u);
        EXPECT_EQ(traceCount("GetGlobalValue"), 1u);
    }

    TEST_F(EffectCondCombat, anEffectWhoseConditionsWereCountedButNotListedIsStillSkipped)
    {
        start();
        playerCast("Mismatch", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 50.f);
        EXPECT_TRUE(traced("cannot evaluate"));
        EXPECT_EQ(mCombat.effectConditionsPassed() + mCombat.effectConditionsFailed(), 0u); // never evaluated
    }

    TEST_F(EffectCondCombat, anEffectWithNoConditionsAppliesAsBefore)
    {
        start();
        playerCast("Plain", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 40.f);
        EXPECT_EQ(mCombat.effectConditionsPassed() + mCombat.effectConditionsFailed(), 0u);
    }

    TEST_F(EffectCondCombat, theSpellsOwnConditionOnTheEffectIsEvaluatedToo)
    {
        start();
        playerCast("SpellGated", mB); // only on a dragon
        EXPECT_FLOAT_EQ(view(mB).mHealth, 50.f);
        playerCast("SpellGated", mC);
        EXPECT_FLOAT_EQ(view(mC).mHealth, 40.f);
        EXPECT_EQ(mCombat.effectConditionsPassed(), 1u);
        EXPECT_EQ(mCombat.effectConditionsFailed(), 1u);
    }

    std::vector<std::string> scenarioTrace()
    {
        CondData data;
        SkyrimCombat combat;
        CondWriter writer;
        std::vector<std::string> trace;
        combat.setTrace([&](const std::string& line) { trace.push_back(line); });
        combat.setData(&data);
        combat.setTestWeapon("IronSword");
        combat.setCombatAi(true);
        combat.onActorAdded(infoOf(20, kBrenuinBase, "Brenuin"));
        combat.onActorAdded(infoOf(30, kCedricBase, "Cedric"));
        SubjectState b;
        b.x = 800.0;
        writer.mStates[20] = b;
        writer.mStates[30] = b;
        writer.mPlayerState.healthMax = 100.f;
        writer.mPlayerState.health = 100.f;
        const ActorHandle targets[2] = { { 20, 1 }, { 30, 1 } };
        std::size_t next = 0;
        for (const char* spell : { "Paralyze", "Fear", "PerkBurn", "GlobalBurn" })
        {
            if (!combat.setTestPlayerSpell(spell))
                return {};
            combat.onPlayerAttackEdge(true);
            for (int i = 0; i < 3; ++i)
                combat.combatUpdate(0.25f, writer);
            combat.onPlayerAttackEdge(false);
            if (writer.mLaunches.size() <= next)
                return {};
            ProjectileResult r;
            r.owner = CombatSubject::ofPlayer();
            r.token = writer.mLaunches.back();
            r.fate = ProjectileFate::HitActor;
            r.actor = targets[next % 2];
            ++next;
            combat.onProjectileResult(r);
            combat.combatUpdate(0.25f, writer);
        }
        return trace;
    }

    TEST(EffectCondCombatRuns, threeRunsGiveTheSameTrace)
    {
        const std::vector<std::string> first = scenarioTrace();
        EXPECT_FALSE(first.empty());
        EXPECT_EQ(scenarioTrace(), first);
        EXPECT_EQ(scenarioTrace(), first);
        EXPECT_NE(std::find_if(first.begin(), first.end(),
                      [](const std::string& line) { return line.find("effect") != std::string::npos; }),
            first.end());
    }

    // ---- the real masters (gated: OPENEDEN_SKYRIMSE_DATA) ------------------------------------------------------------

    bool loadIndex(CombatIndex& index, std::string& skip)
    {
        namespace fs = std::filesystem;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
        {
            skip = "OPENEDEN_SKYRIMSE_DATA is not set";
            return false;
        }
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
        {
            if (!fs::exists(data / n) || !order.add(n, false, err))
            {
                skip = "the five official masters are not all in the Data folder";
                return false;
            }
        }
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            if (file == nullptr)
            {
                skip = "cannot open " + n;
                return false;
            }
            CombatRecords records;
            (void)parseCombatRecords(*file, order, ExteriorLimits(), records);
            index.add(records);
        }
        return true;
    }

    /// Both lists of a spell's effect `i`, evaluated for a target and a caster.
    Verdict spellEffectVerdict(const CombatIndex& index, const SpellRecord& spell, std::size_t i, Person& target,
        Person& caster)
    {
        effectconditions::Evaluator e;
        const SpellEffect& se = spell.mEffects.at(i);
        const MagicEffectRecord* m = index.magicEffect(se.mEffect);
        EXPECT_NE(m, nullptr);
        if (m == nullptr)
            return Verdict::Unanswered;
        Verdict v = Verdict::Pass;
        for (const std::vector<survey::Ctda>* list : { &se.mConditionList, &m->mConditionList })
        {
            const Verdict one = run(e, *list, &target, &caster);
            if (one == Verdict::Unanswered)
                return one;
            if (one == Verdict::Fail)
                v = one;
        }
        return v;
    }

    TEST(EffectCondRealData, everyEffectListIsCompleteAndTheRealParalysisFearAndFirebolt)
    {
        CombatIndex index;
        std::string skip;
        if (!loadIndex(index, skip))
        {
            GTEST_SKIP() << skip;
        }
        std::uint64_t withConditions = 0, listed = 0, answerable = 0, spellEffects = 0;
        for (const FormId id : index.spellIds())
        {
            const SpellRecord* spell = index.spell(id);
            for (const SpellEffect& se : spell->mEffects)
            {
                const MagicEffectRecord* m = index.magicEffect(se.mEffect);
                if (m == nullptr)
                    continue;
                ++spellEffects;
                if (se.mConditions + m->mConditions == 0)
                    continue;
                ++withConditions;
                if (se.mConditionList.size() != se.mConditions || m->mConditionList.size() != m->mConditions)
                    continue;
                ++listed;
                bool all = true;
                for (const std::vector<survey::Ctda>* list : { &se.mConditionList, &m->mConditionList })
                {
                    for (const survey::Ctda& c : *list)
                        all = all && effectconditions::functionAnswered(c.mFunction) && effectconditions::runOnAnswered(c);
                }
                if (all)
                    ++answerable;
            }
        }
        std::cout << "SKYRIMSE-EFFCOND spell effects " << spellEffects << " with conditions " << withConditions
                  << " fully listed " << listed << " answerable " << answerable << "\n";
        EXPECT_GT(withConditions, 100u);
        EXPECT_EQ(listed, withConditions); // no CTDA of the masters is damaged
        EXPECT_GT(answerable * 2, withConditions); // more than half can be answered

        Person nord;
        Person caster;
        Person dragon;
        dragon.mKeywords = { kKwDragon };
        Person draugr;
        draugr.mKeywords = { kKwUndead };
        const SpellRecord* paralyze = index.spellByEditorId("Paralyze");
        ASSERT_NE(paralyze, nullptr);
        EXPECT_EQ(spellEffectVerdict(index, *paralyze, 0, nord, caster), Verdict::Pass);
        EXPECT_EQ(spellEffectVerdict(index, *paralyze, 0, dragon, caster), Verdict::Fail);
        const SpellRecord* fear = index.spellByEditorId("Fear");
        ASSERT_NE(fear, nullptr);
        EXPECT_EQ(spellEffectVerdict(index, *fear, 0, nord, caster), Verdict::Pass);
        EXPECT_EQ(spellEffectVerdict(index, *fear, 0, draugr, caster), Verdict::Fail);
        // Firebolt: its Stagger (needs a dual cast and the Impact perk) and Demoralize (needs the Intense Flames perk) are refused
        const SpellRecord* firebolt = index.spellByEditorId("Firebolt");
        ASSERT_NE(firebolt, nullptr);
        ASSERT_EQ(firebolt->mEffects.size(), 3u);
        EXPECT_EQ(spellEffectVerdict(index, *firebolt, 0, nord, caster), Verdict::Pass); // the damage: no conditions
        EXPECT_EQ(spellEffectVerdict(index, *firebolt, 1, nord, caster), Verdict::Fail);
        EXPECT_EQ(spellEffectVerdict(index, *firebolt, 2, nord, caster), Verdict::Fail);
    }
}
