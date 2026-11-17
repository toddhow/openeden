// Task 0238: more effect conditions (GetGlobalValue, IsInInterior, GetInWorldspace, IsChild, IsHostileToActor,
// WornApparelHasKeywordCount), race powers whose conditions are evaluated, and the regeneration rate effects (actor
// values 27 to 29 and 155 to 157). Groups:
//  - EffectCond2Eval: the evaluator with its new sources (extended flag, place, globals, participant facts);
//  - EffectCond2Pure: the regeneration arithmetic;
//  - EffectCond2Combat: the combat runtime (a place source, a global source, child races, hostility, worn pieces);
//  - EffectCond2Regen: a boost spell changes the regeneration of the caster, ends on time, reaches the engine's health;
//  - EffectCond2Powers: a power's effects are refused or applied by their conditions, Battle Cry's real shape;
//  - EffectCond2Real (gated): the five masters, answered shares before and after.
// Every expected number is derived by hand from the lists and rates in the test.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/magic_conditions.hpp>
#include <game_skyrimse/magic_status.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;
    using effectconditions::Participant;
    using effectconditions::Place;
    using effectconditions::Verdict;

    constexpr FormId kKwUndead = effectconditions::kKeywordUndead;
    constexpr FormId kKwDaedra = 0x00013797;
    constexpr FormId kKwDwarven = 0x00013798;
    constexpr FormId kKwHeavy = 0x0006BBD3; // any id: only equality matters
    constexpr FormId kWorldWhiterun = 0x0001A26F;
    constexpr FormId kWorldOther = 0x0000003C;
    constexpr FormId kGlobalA = 0x0000055A;
    constexpr FormId kGlobalMissing = 0x0000055B;

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

    constexpr std::uint8_t kEq = 0, kNe = 1, kGe = 3;

    struct Person
    {
        std::vector<FormId> mKeywords;
        std::vector<FormId> mWorn;
        Participant mP;
        Participant* get()
        {
            mP.mPerk.mKind = PerkParticipant::Kind::Actor;
            mP.mPerk.mKeywords = &mKeywords;
            mP.mPerk.mWornKeywords = &mWorn;
            return &mP;
        }
    };

    Verdict run(
        effectconditions::Evaluator& e, const std::vector<survey::Ctda>& list, Person* target, Person* caster = nullptr)
    {
        return e.evaluate(
            list, target != nullptr ? target->get() : nullptr, caster != nullptr ? caster->get() : nullptr, nullptr);
    }

    effectconditions::Evaluator extended()
    {
        effectconditions::Evaluator e;
        e.setExtended(true);
        e.setGlobalLookup([](FormId id, float& value) {
            if (id == kGlobalA)
            {
                value = 3.f;
                return true;
            }
            return false;
        });
        Place place;
        place.mKnown = true;
        place.mInterior = false;
        place.mWorld = kWorldWhiterun;
        e.setPlace(place);
        return e;
    }

    // ---- the evaluator ---------------------------------------------------------------------------------------------

    TEST(EffectCond2Eval, theBaseListIsUnchangedAndTheSixAreAnsweredOnlyByAnExtendedEvaluator)
    {
        for (const int fn : { 74, 300, 310, 365, 719, 722 })
        {
            EXPECT_FALSE(effectconditions::functionAnswered(static_cast<std::uint16_t>(fn))) << fn;
            EXPECT_TRUE(effectconditions::functionAnsweredExtended(static_cast<std::uint16_t>(fn))) << fn;
        }
        for (const int fn : { 560, 448, 724, 244 })
            EXPECT_TRUE(effectconditions::functionAnsweredExtended(static_cast<std::uint16_t>(fn))) << fn;
        for (const int fn : { 629, 566, 58, 0, 9999 })
            EXPECT_FALSE(effectconditions::functionAnsweredExtended(static_cast<std::uint16_t>(fn))) << fn;
        // not extended: nothing new is answered, whatever sources are given
        effectconditions::Evaluator plain;
        Place place;
        place.mKnown = true;
        plain.setPlace(place);
        plain.setGlobalLookup([](FormId, float& v) {
            v = 1.f;
            return true;
        });
        Person t;
        EXPECT_EQ(run(plain, { cond(300, kEq, 0.f) }, &t), Verdict::Unanswered);
        EXPECT_EQ(run(plain, { cond(74, kGe, 1.f, kGlobalA) }, &t), Verdict::Unanswered);
        EXPECT_EQ(run(plain, { cond(365, kEq, 0.f) }, &t), Verdict::Unanswered);
    }

    TEST(EffectCond2Eval, aGlobalIsReadFromTheSourceAndAnUnknownOneLeavesTheListUnanswered)
    {
        effectconditions::Evaluator e = extended();
        Person t;
        EXPECT_EQ(run(e, { cond(74, kGe, 3.f, kGlobalA) }, &t), Verdict::Pass);
        EXPECT_EQ(run(e, { cond(74, kGe, 4.f, kGlobalA) }, &t), Verdict::Fail);
        EXPECT_EQ(run(e, { cond(74, kEq, 0.f, kGlobalMissing) }, &t), Verdict::Unanswered);
        // without a source at all
        effectconditions::Evaluator bare;
        bare.setExtended(true);
        EXPECT_EQ(run(bare, { cond(74, kEq, 0.f, kGlobalA) }, &t), Verdict::Unanswered);
    }

    TEST(EffectCond2Eval, aComparisonWithAGlobalNeedsThatGlobalToBeKnown)
    {
        effectconditions::Evaluator e = extended();
        Person t;
        survey::Ctda known = cond(80, kGe, 0.f, 0, 0x04);
        known.mComparisonBits = kGlobalA; // the use-global flag: the comparison value is the global's form id
        survey::Ctda unknown = known;
        unknown.mComparisonBits = kGlobalMissing;
        t.get()->mPerk.mLevel = 3;
        EXPECT_EQ(run(e, { known }, &t), Verdict::Pass); // level 3 >= 3
        EXPECT_EQ(run(e, { unknown }, &t), Verdict::Unanswered);
    }

    TEST(EffectCond2Eval, thePlaceDecidesInteriorAndWorldspaceAndWithoutOneBothAreUnanswered)
    {
        Person t;
        effectconditions::Evaluator e = extended(); // an exterior of Whiterun
        EXPECT_EQ(run(e, { cond(300, kEq, 0.f) }, &t), Verdict::Pass);
        EXPECT_EQ(run(e, { cond(300, kEq, 1.f) }, &t), Verdict::Fail);
        EXPECT_EQ(run(e, { cond(310, kEq, 1.f, kWorldWhiterun) }, &t), Verdict::Pass);
        EXPECT_EQ(run(e, { cond(310, kEq, 1.f, kWorldOther) }, &t), Verdict::Fail);
        Place interior;
        interior.mKnown = true;
        interior.mInterior = true;
        e.setPlace(interior);
        EXPECT_EQ(run(e, { cond(300, kEq, 1.f) }, &t), Verdict::Pass);
        // in an interior the answer to "in this worldspace" is no (the VampireSunDamage shape: not in Sovngarde)
        EXPECT_EQ(run(e, { cond(310, kEq, 0.f, kWorldWhiterun) }, &t), Verdict::Pass);
        e.setPlace(Place());
        EXPECT_EQ(run(e, { cond(300, kEq, 0.f) }, &t), Verdict::Unanswered);
        EXPECT_EQ(run(e, { cond(310, kEq, 0.f, kWorldWhiterun) }, &t), Verdict::Unanswered);
    }

    TEST(EffectCond2Eval, aChildIsAsTheParticipantSays)
    {
        effectconditions::Evaluator e = extended();
        Person adult, child;
        child.get()->mChild = true;
        const std::vector<survey::Ctda> noChildren = { cond(365, kEq, 0.f) }; // the Frenzy shape
        EXPECT_EQ(run(e, noChildren, &adult), Verdict::Pass);
        EXPECT_EQ(run(e, noChildren, &child), Verdict::Fail);
        EXPECT_EQ(run(e, { cond(365, kEq, 1.f) }, &child), Verdict::Pass); // the trap that only hurts children
    }

    TEST(EffectCond2Eval, hostilityIsToTheCasterForNoParameterAndToThePlayerForItsReference)
    {
        effectconditions::Evaluator e = extended();
        Person foe;
        foe.get()->mHostileToCaster = true;
        Person friendly;
        EXPECT_EQ(run(e, { cond(719, kEq, 1.f, 0) }, &foe), Verdict::Pass);
        EXPECT_EQ(run(e, { cond(719, kEq, 1.f, 0) }, &friendly), Verdict::Fail);
        EXPECT_EQ(run(e, { cond(719, kNe, 0.f, 0) }, &foe), Verdict::Pass); // the Detect Life shape: hostile != 0
        EXPECT_EQ(
            run(e, { cond(719, kEq, 1.f, 0x14) }, &foe), Verdict::Fail); // hostile to the caster, not to the player
        foe.get()->mHostileToPlayer = true;
        EXPECT_EQ(run(e, { cond(719, kEq, 1.f, 0x14) }, &foe), Verdict::Pass);
        // any other actor reference cannot be answered
        EXPECT_EQ(run(e, { cond(719, kEq, 1.f, 0x1234) }, &foe), Verdict::Unanswered);
    }

    TEST(EffectCond2Eval, wornApparelCountsThePiecesThatCarryTheKeyword)
    {
        effectconditions::Evaluator e = extended();
        Person four, three;
        four.mWorn = { kKwHeavy, 7, kKwHeavy, kKwHeavy, 9, kKwHeavy };
        three.mWorn = { kKwHeavy, kKwHeavy, kKwHeavy, 9 };
        const std::vector<survey::Ctda> set = { cond(722, kGe, 4.f, kKwHeavy) }; // PerkMatchingSetHeavy: 4 pieces
        EXPECT_EQ(run(e, set, &four), Verdict::Pass);
        EXPECT_EQ(run(e, set, &three), Verdict::Fail);
        Person none;
        EXPECT_EQ(run(e, set, &none), Verdict::Fail);
    }

    // ---- the regeneration arithmetic -------------------------------------------------------------------------------

    TEST(EffectCond2Pure, theRegenerationValuesAreTheSixAndTheirVital)
    {
        EXPECT_EQ(regenVital(27), 0);
        EXPECT_EQ(regenVital(28), 1);
        EXPECT_EQ(regenVital(29), 2);
        EXPECT_EQ(regenVital(155), 0);
        EXPECT_EQ(regenVital(156), 1);
        EXPECT_EQ(regenVital(157), 2);
        for (const int v : { 24, 25, 26, 30, 39, 83, 134, 154, 158, -1 })
            EXPECT_EQ(regenVital(v), -1) << v;
        EXPECT_FALSE(isRegenMultValue(29));
        EXPECT_TRUE(isRegenMultValue(155));
        EXPECT_TRUE(isRegenMultValue(157));
        EXPECT_TRUE(isRegenRateValue(28));
        EXPECT_FALSE(isRegenRateValue(24));
    }

    TEST(EffectCond2Pure, theRateIsBasePlusTheAddedRatesTimesOnePlusThePercent)
    {
        EXPECT_FLOAT_EQ(effectiveRegenRate(5.f, 0.f, 0.f), 5.f);
        EXPECT_FLOAT_EQ(effectiveRegenRate(5.f, 50.f, 0.f), 55.f); // Adrenaline Rush: 11 times the base
        EXPECT_FLOAT_EQ(effectiveRegenRate(3.f, 25.f, 0.f), 28.f); // Highborn
        EXPECT_FLOAT_EQ(effectiveRegenRate(0.7f, 10.f, 0.f), 10.7f); // Histskin
        EXPECT_FLOAT_EQ(effectiveRegenRate(3.f, 0.f, 100.f), 6.f); // a Mult effect of 100 percent doubles it
        EXPECT_FLOAT_EQ(effectiveRegenRate(3.f, 1.f, 50.f), 6.f); // (3 + 1) x 1.5
        EXPECT_FLOAT_EQ(effectiveRegenRate(3.f, -5.f, 0.f), 0.f); // never below 0
        EXPECT_FLOAT_EQ(effectiveRegenRate(3.f, 0.f, -100.f), 0.f);
        EXPECT_FLOAT_EQ(effectiveRegenRate(3.f, 0.f, -500.f), 0.f); // cut at -100 percent
        EXPECT_FLOAT_EQ(effectiveRegenRate(NAN, 1.f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(effectiveRegenRate(3.f, INFINITY, 1.f), 0.f);
        EXPECT_FLOAT_EQ(effectiveRegenRate(3.f, 1.0e9f, 0.f), 1003.f); // the sum is cut at kMaxRegenRate
    }

    // ---- the combat runtime ----------------------------------------------------------------------------------------

    constexpr WorldId kOrdinaryBase = 200, kChildBase = 300, kFoeBase = 400, kArmouredBase = 500, kUndeadBase = 600;
    constexpr FormId kChildRace = 0x13749;
    constexpr FormId kInteriorBurn = 0x1001, kWorldBurn = 0x1002, kChildSafe = 0x1003, kFoeBurn = 0x1004,
                     kArmourBurn = 0x1005, kGlobalBurn = 0x1006, kBoost = 0x1010, kSpend = 0x1011, kMultBoost = 0x1012,
                     kSlow = 0x1013, kHistskin = 0x1014, kBoostShort = 0x1015, kBoostCheap = 0x1016, kCry = 0x1020;
    constexpr FormId kEffInterior = 0x2001, kEffWorld = 0x2002, kEffChild = 0x2003, kEffFoe = 0x2004,
                     kEffArmour = 0x2005, kEffGlobal = 0x2006, kEffBoost = 0x2010, kEffSpend = 0x2011,
                     kEffMult = 0x2012, kEffSlow = 0x2013, kEffHist = 0x2014, kEffCry = 0x2020, kEffStagger = 0x2021;
    constexpr FormId kProjectile = 0x3000;

    class RegenData final : public CombatData
    {
    public:
        RegenData()
        {
            mProj.mCommon.mId = kProjectile;
            mProj.mCommon.mEditorId = "TestBoltProjectile";
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 2500.f;
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\TestBolt.nif";

            constexpr std::uint32_t hostile = MagicFlag::Hostile | MagicFlag::Detrimental;
            addEffect(kEffInterior, MagicArchetype::ValueModifier, 24, hostile);
            mEffects[kEffInterior].mConditionList = { cond(300, kEq, 1.f) };
            addEffect(kEffWorld, MagicArchetype::ValueModifier, 24, hostile);
            mEffects[kEffWorld].mConditionList = { cond(310, kEq, 1.f, kWorldWhiterun) };
            addEffect(kEffChild, MagicArchetype::ValueModifier, 24, hostile);
            mEffects[kEffChild].mConditionList = { cond(365, kEq, 0.f) };
            addEffect(kEffFoe, MagicArchetype::ValueModifier, 24, hostile);
            mEffects[kEffFoe].mConditionList = { cond(719, kEq, 1.f, 0x14) };
            addEffect(kEffArmour, MagicArchetype::ValueModifier, 24, hostile);
            mEffects[kEffArmour].mConditionList = { cond(722, kGe, 4.f, kKwHeavy) };
            addEffect(kEffGlobal, MagicArchetype::ValueModifier, 24, hostile);
            mEffects[kEffGlobal].mConditionList = { cond(74, kGe, 2.f, kGlobalA) };
            for (const FormId id : { kEffInterior, kEffWorld, kEffChild, kEffFoe, kEffArmour, kEffGlobal })
            {
                mEffects[id].mConditions = 1;
                mEffects[id].mProjectile = kProjectile;
            }
            addSpell(kInteriorBurn, "InteriorBurn", MagicDelivery::Aimed, kEffInterior, 10.f, 0, 5);
            addSpell(kWorldBurn, "WorldBurn", MagicDelivery::Aimed, kEffWorld, 10.f, 0, 5);
            addSpell(kChildSafe, "ChildSafe", MagicDelivery::Aimed, kEffChild, 10.f, 0, 5);
            addSpell(kFoeBurn, "FoeBurn", MagicDelivery::Aimed, kEffFoe, 10.f, 0, 5);
            addSpell(kArmourBurn, "ArmourBurn", MagicDelivery::Aimed, kEffArmour, 10.f, 0, 5);
            addSpell(kGlobalBurn, "GlobalBurn", MagicDelivery::Aimed, kEffGlobal, 10.f, 0, 5);

            // the regeneration rate effects: Peak modifiers on actor values 27 to 29 and 155 to 157 (the shapes of
            // RaceHighElfFortifyMagickaRate, AlchFortifyHealRate and AlchDamageMagickaRate, VERIFIED)
            addEffect(kEffBoost, MagicArchetype::PeakValueModifier, RegenValue::MagickaRate, 0x200802);
            addEffect(
                kEffSpend, MagicArchetype::PeakValueModifier, 83, 0x200802); // a modifier that changes nothing here
            addEffect(kEffMult, MagicArchetype::PeakValueModifier, RegenValue::MagickaRateMult, 0x200802);
            addEffect(kEffSlow, MagicArchetype::PeakValueModifier, RegenValue::MagickaRate, 0x400a07); // damage rate
            addEffect(kEffHist, MagicArchetype::PeakValueModifier, RegenValue::HealthRate, 0x201802);
            addSpell(kBoost, "Boost", MagicDelivery::Self, kEffBoost, 25.f, 60, 60);
            addSpell(kBoostShort, "BoostShort", MagicDelivery::Self, kEffBoost, 25.f, 2, 60);
            addSpell(kBoostCheap, "BoostCheap", MagicDelivery::Self, kEffBoost, 25.f, 60, 5);
            addSpell(kSpend, "Spend", MagicDelivery::Self, kEffSpend, 1.f, 60, 60);
            addSpell(kMultBoost, "MultBoost", MagicDelivery::Self, kEffMult, 100.f, 60, 10);
            addSpell(kSlow, "Slow", MagicDelivery::Self, kEffSlow, 2.f, 60, 10);
            addSpell(kHistskin, "Histskin", MagicDelivery::Self, kEffHist, 10.f, 60, 5);

            // Battle Cry's shape: Demoralize with three keyword exclusions and IsUndead, no hostility test
            addEffect(kEffCry, StatusArchetype::Demoralize, 1, 0x409903);
            mEffects[kEffCry].mConditionList = { cond(560, kNe, 1.f, kKwUndead), cond(560, kNe, 1.f, kKwDaedra),
                cond(560, kNe, 1.f, kKwDwarven), cond(715, kEq, 0.f) };
            mEffects[kEffCry].mConditions = 4;
            addEffect(kEffStagger, MagicArchetype::Stagger, -1, 0);
            addSpell(kCry, "Cry", MagicDelivery::Self, kEffCry, 99.f, 30, 0);
            mSpells[kCry].mType = 2; // a power
            mSpells[kCry].mEffects[0].mArea = 30;

            FighterProfile npc;
            npc.mHealthMax = 50.f;
            npc.mStaminaMax = 50.f;
            npc.mMagickaMax = 50.f;
            npc.mLevel = 3;
            npc.mAi.mKnown = true;
            mProfiles[kOrdinaryBase] = npc;
            mProfiles[kChildBase] = npc;
            mProfiles[kChildBase].mRace = kChildRace;
            mProfiles[kFoeBase] = npc;
            mProfiles[kFoeBase].mAi.mRelationToPlayer = 1; // an enemy of the player
            mProfiles[kArmouredBase] = npc;
            for (int i = 0; i < 4; ++i)
            {
                ArmourPiece piece;
                piece.mKeywords = { kKwHeavy };
                mProfiles[kArmouredBase].mPieces.push_back(piece);
            }
            mProfiles[kUndeadBase] = npc;
            mProfiles[kUndeadBase].mKeywords = { kKwUndead };
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
            mPlayer.mHealthRegenPercent = 0.7f;
            mPlayer.mMagickaRegenPercent = 3.f;
            mPlayer.mStaminaRegenPercent = 5.f;
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
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        bool raceIsChild(FormId race) override { return race == kChildRace; }
        bool globalDefault(FormId global, float& value) override
        {
            if (global != kGlobalA)
                return false;
            value = mGlobalValue;
            return true;
        }
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
        void addSpell(FormId id, const char* name, std::uint32_t delivery, FormId effect, float magnitude,
            std::uint32_t duration, std::uint32_t cost)
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
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
        float mGlobalValue = 3.f;
    };

    class RegenWriter final : public CombatRequestWriter
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

    struct EffectCond2Combat : ::testing::Test
    {
        RegenData mData;
        SkyrimCombat mCombat;
        RegenWriter mWriter;
        std::vector<std::string> mTrace;
        ActorHandle mOrdinary{ 10, 1 };
        ActorHandle mChild{ 20, 1 };
        ActorHandle mFoe{ 30, 1 };
        ActorHandle mArmoured{ 40, 1 };

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void start()
        {
            mCombat.setData(&mData);
            mCombat.onActorAdded(infoOf(10, kOrdinaryBase, "Ordinary"));
            mCombat.onActorAdded(infoOf(20, kChildBase, "Child"));
            mCombat.onActorAdded(infoOf(30, kFoeBase, "Foe"));
            mCombat.onActorAdded(infoOf(40, kArmouredBase, "Armoured"));
            for (const WorldId id : { 10, 20, 30, 40 })
            {
                SubjectState s;
                s.x = 100.0 + static_cast<double>(id);
                mWriter.mStates[id] = s;
            }
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
            update(); // the profiles are made on the first update
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
        float healthOf(ActorHandle who)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::of(who), v));
            return v.mHealth;
        }
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
        void placeIs(bool interior, FormId world)
        {
            mCombat.setPlaceSource([interior, world](Place& out) {
                out.mInterior = interior;
                out.mWorld = world;
                return true;
            });
        }
    };

    TEST_F(EffectCond2Combat, anInteriorEffectNeedsAnInteriorAndWithoutAPlaceItIsUnanswered)
    {
        start();
        playerCast("InteriorBurn", mOrdinary); // no place source yet
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 50.f);
        EXPECT_EQ(mCombat.effectConditionsUnanswered(), 1u);
        placeIs(false, kWorldWhiterun);
        playerCast("InteriorBurn", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 50.f);
        EXPECT_EQ(mCombat.effectConditionsFailed(), 1u);
        placeIs(true, kNullFormId);
        playerCast("InteriorBurn", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 40.f);
        EXPECT_EQ(mCombat.effectConditionsPassed(), 1u);
    }

    TEST_F(EffectCond2Combat, aWorldspaceEffectNeedsThatExterior)
    {
        start();
        placeIs(false, kWorldWhiterun);
        playerCast("WorldBurn", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 40.f);
        placeIs(false, kWorldOther);
        playerCast("WorldBurn", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 40.f); // refused
        placeIs(true, kNullFormId);
        playerCast("WorldBurn", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 40.f); // an interior is in no worldspace
    }

    TEST_F(EffectCond2Combat, anEffectThatSparesChildrenIsRefusedOnAChildRace)
    {
        start();
        playerCast("ChildSafe", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 40.f);
        playerCast("ChildSafe", mChild);
        EXPECT_FLOAT_EQ(healthOf(mChild), 50.f);
        EXPECT_EQ(mCombat.effectConditionsFailed(), 1u);
    }

    TEST_F(EffectCond2Combat, anEffectOnTheHostileToThePlayerLandsOnAnEnemyOnly)
    {
        start();
        playerCast("FoeBurn", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 50.f);
        playerCast("FoeBurn", mFoe);
        EXPECT_FLOAT_EQ(healthOf(mFoe), 40.f);
    }

    TEST_F(EffectCond2Combat, aSetBonusNeedsFourWornPiecesWithTheKeyword)
    {
        start();
        playerCast("ArmourBurn", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 50.f);
        playerCast("ArmourBurn", mArmoured);
        EXPECT_FLOAT_EQ(healthOf(mArmoured), 40.f);
    }

    TEST_F(EffectCond2Combat, aGlobalGateReadsTheSourcesInOrderAndTheDataLast)
    {
        start();
        playerCast("GlobalBurn", mOrdinary); // the data's value 3 >= 2
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 40.f);
        mData.mGlobalValue = 1.f;
        playerCast("GlobalBurn", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 40.f); // 1 < 2: refused
        mCombat.setGlobalSource([](FormId global, float& value) {
            if (global != kGlobalA)
                return false;
            value = 5.f; // a script set it
            return true;
        });
        playerCast("GlobalBurn", mOrdinary);
        EXPECT_FLOAT_EQ(healthOf(mOrdinary), 30.f);
    }

    // ---- regeneration -----------------------------------------------------------------------------------------------

    struct EffectCond2Regen : EffectCond2Combat
    {
        // The player casts a self spell (the clip's fire event comes 0.367 s into it).
        void selfCast(const char* spell)
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell(spell));
            mCombat.onPlayerAttackEdge(true);
            updates(3);
            mCombat.onPlayerAttackEdge(false);
            updates(2);
        }
        float magickaOfPlayer()
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
            return v.mMagicka;
        }
    };

    TEST_F(EffectCond2Regen, aFortifyRateEffectRaisesTheRateAndItsEndLowersItAgain)
    {
        start();
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 3.f);
        selfCast("BoostShort"); // +25 for 2 s
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 28.f);
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 2), 5.f); // the other vitals are alone
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 0), 0.7f);
        updates(12); // 3 s of clock: the effect ran 2 s
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 3.f);
        EXPECT_LT(mCombat.regenRatePercent(CombatSubject::of(ActorHandle{ 99, 1 }), 1), 0.f); // an unknown fighter
    }

    TEST_F(EffectCond2Regen, theSameSpellCastAgainReplacesItsEarlierEffectInsteadOfStackingIt)
    {
        start();
        selfCast("BoostCheap");
        selfCast("BoostCheap");
        selfCast("BoostCheap");
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 28.f) << "not 78 or 103";
    }

    TEST_F(EffectCond2Regen, aBoostMakesMagickaComeBackFasterThanTheSameSpendWithoutIt)
    {
        start();
        selfCast("Boost"); // costs 60 of 100, rate 3 + 25
        EXPECT_LT(magickaOfPlayer(), 60.f); // 60 of 100 were spent
        updates(16); // 4 s
        const float boosted = magickaOfPlayer();

        RegenData controlData;
        SkyrimCombat control;
        RegenWriter controlWriter;
        control.setData(&controlData);
        controlWriter.mPlayerState.healthMax = 100.f;
        controlWriter.mPlayerState.health = 100.f;
        ASSERT_TRUE(control.setTestPlayerSpell("Spend"));
        control.onPlayerAttackEdge(true);
        for (int i = 0; i < 3; ++i)
            control.combatUpdate(0.25f, controlWriter);
        control.onPlayerAttackEdge(false);
        for (int i = 0; i < 2 + 16; ++i)
            control.combatUpdate(0.25f, controlWriter);
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(control.fighterView(CombatSubject::ofPlayer(), v));
        EXPECT_FLOAT_EQ(control.regenRatePercent(CombatSubject::ofPlayer(), 1), 3.f);
        EXPECT_GT(boosted, v.mMagicka + 20.f) << "boosted " << boosted << " control " << v.mMagicka;
        EXPECT_GT(boosted, 99.f); // 28 a second refills 60 in about 2.2 s once the delay is over
        EXPECT_LT(v.mMagicka, 60.f); // 3 a second: about 6 in 2 s
    }

    TEST_F(EffectCond2Regen, aMultEffectScalesAndADamageRateEffectLowersTheRate)
    {
        start();
        selfCast("MultBoost"); // value 156, 100 percent: (3 + 0) x (1 + 100 / 100)
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 6.f);
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 2), 5.f); // another vital: alone
        selfCast("Slow"); // value 28, a Detrimental 2 a second off: (3 - 2) x 2
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 2.f);
    }

    TEST_F(EffectCond2Regen, histskinGivesThePlayerHealthThroughAZeroDamageReport)
    {
        start();
        mWriter.mPlayerState.health = 50.f; // half
        selfCast("Histskin"); // +10 percent of 100 a second, for 60 s
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 0), 10.7f);
        mWriter.mDamage.clear();
        updates(8); // 2 s
        const auto it = std::find_if(mWriter.mDamage.begin(), mWriter.mDamage.end(),
            [](const DamageReport& r) { return r.amount == 0.f && r.victimIsPlayer && r.healthAfter > 50.f; });
        ASSERT_NE(it, mWriter.mDamage.end());
        float total = 0.f;
        for (const DamageReport& r : mWriter.mDamage)
            if (r.amount == 0.f && r.victimIsPlayer)
                total = std::max(total, r.healthAfter - 50.f);
        EXPECT_GT(total, 0.f);
        // the bonus is what the effect adds to the engine's own rate: (10.7 - 0.7) percent of 100 a second
        EXPECT_NEAR(total, 10.f * 0.25f, 0.01f) << "one flush of one step";
    }

    TEST_F(EffectCond2Regen, aRunOfTheSameSequenceGivesTheSameTrace)
    {
        const auto once = [] {
            std::vector<std::string> trace;
            RegenData data;
            SkyrimCombat combat;
            RegenWriter writer;
            combat.setTrace([&](const std::string& line) { trace.push_back(line); });
            combat.setData(&data);
            writer.mPlayerState.healthMax = 100.f;
            writer.mPlayerState.health = 80.f;
            (void)combat.setTestPlayerSpell("Boost");
            combat.onPlayerAttackEdge(true);
            for (int i = 0; i < 3; ++i)
                combat.combatUpdate(0.25f, writer);
            combat.onPlayerAttackEdge(false);
            for (int i = 0; i < 30; ++i)
                combat.combatUpdate(0.25f, writer);
            return trace;
        };
        const std::vector<std::string> first = once();
        EXPECT_FALSE(first.empty());
        EXPECT_EQ(once(), first);
        EXPECT_EQ(once(), first);
        EXPECT_NE(std::find_if(first.begin(), first.end(),
                      [](const std::string& l) { return l.find("COMBAT regen player rates") != std::string::npos; }),
            first.end());
    }

    // ---- race powers
    // -------------------------------------------------------------------------------------------------

    struct EffectCond2Powers : EffectCond2Combat
    {
        void startCrowd()
        {
            start();
            mCombat.onActorAdded(infoOf(50, kUndeadBase, "Undead"));
            SubjectState s;
            s.x = 150.0;
            mWriter.mStates[50] = s;
        }
        bool demoralized(ActorHandle who)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::of(who), v));
            return v.mDemoralized;
        }
    };

    TEST_F(EffectCond2Powers, theRealBattleCryShapeHitsEveryoneButTheUndeadAndHostilityIsNotAsked)
    {
        startCrowd();
        ASSERT_TRUE(mCombat.castPlayerPower(kCry));
        updates(8);
        EXPECT_TRUE(demoralized(mOrdinary)); // peaceful: the record does not ask for hostility
        EXPECT_TRUE(demoralized(mFoe));
        EXPECT_FALSE(demoralized(ActorHandle{ 50, 1 })) << "the undead are excluded by the record's own conditions";
        EXPECT_TRUE(traced("conditions hold on actor 10"));
        EXPECT_TRUE(traced("refused: its conditions fail on actor 50"));
        EXPECT_EQ(mCombat.effectConditionsFailed(), 1u);
    }

    TEST_F(EffectCond2Powers, anUnlistedPowerConditionIsStillTakenAsMetAndRecorded)
    {
        mData.mEffects[kEffCry].mConditionList.clear(); // counted (4), never listed: a hand-built record
        startCrowd();
        ASSERT_TRUE(mCombat.castPlayerPower(kCry));
        updates(8);
        EXPECT_TRUE(demoralized(mOrdinary));
        EXPECT_TRUE(demoralized(ActorHandle{ 50, 1 })); // nothing to evaluate: as in task 0228
        EXPECT_TRUE(traced("taken as met"));
    }

    // ---- the real masters (gated: OPENEDEN_SKYRIMSE_DATA) -----------------------------------------------------------

    TEST(EffectCond2Real, theAnsweredSharesBeforeAndAfterAndTheRealPowers)
    {
        namespace fs = std::filesystem;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
        {
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        }
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            if (!fs::exists(data / n) || !order.add(n, false, err))
            {
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
            }
        CombatIndex index;
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(file, nullptr) << n;
            CombatRecords records;
            (void)parseCombatRecords(*file, order, ExteriorLimits(), records);
            index.add(records);
        }
        std::uint64_t conditions = 0, before = 0, after = 0, effects = 0, effectsWith = 0, effectsBefore = 0,
                      effectsAfter = 0;
        std::map<std::uint16_t, std::uint64_t> stillUnanswered;
        const auto answeredBefore = [](const survey::Ctda& c) {
            return effectconditions::functionAnswered(c.mFunction) && effectconditions::runOnAnswered(c);
        };
        const auto answeredAfter = [](const survey::Ctda& c) {
            return effectconditions::functionAnsweredExtended(c.mFunction) && effectconditions::runOnAnswered(c)
                && (c.mFunction != 719 || c.mParam1 == 0 || c.mParam1 == effectconditions::kPlayerReference);
        };
        for (const FormId id : index.spellIds())
        {
            const SpellRecord* spell = index.spell(id);
            for (const SpellEffect& se : spell->mEffects)
            {
                const MagicEffectRecord* m = index.magicEffect(se.mEffect);
                if (m == nullptr)
                    continue;
                ++effects;
                bool allBefore = true, allAfter = true;
                bool any = false;
                for (const std::vector<survey::Ctda>* list : { &se.mConditionList, &m->mConditionList })
                    for (const survey::Ctda& c : *list)
                    {
                        any = true;
                        ++conditions;
                        const bool b = answeredBefore(c), a = answeredAfter(c);
                        before += b;
                        after += a;
                        allBefore = allBefore && b;
                        allAfter = allAfter && a;
                        if (!a)
                            ++stillUnanswered[c.mFunction];
                    }
                if (any)
                {
                    ++effectsWith;
                    effectsBefore += allBefore;
                    effectsAfter += allAfter;
                }
            }
        }
        std::cout << "SKYRIMSE-EFFCOND2 spell effects " << effects << " with conditions " << effectsWith
                  << " answerable before " << effectsBefore << " after " << effectsAfter << "; conditions "
                  << conditions << " answered before " << before << " after " << after
                  << "; still unanswered by function:";
        for (const auto& entry : stillUnanswered)
            std::cout << " fn" << entry.first << "=" << entry.second;
        std::cout << "\n";
        EXPECT_GT(after, before);
        EXPECT_GE(effectsAfter, effectsBefore);

        // the four regeneration powers and Battle Cry, by their real records
        std::uint64_t regenPowers = 0;
        for (const char* name : { "PowerArgonianHistskin", "PowerRedguardStaminaRegen", "PowerHighElfMagickaRegen",
                 "PowerBretonAbsorbSpell" })
        {
            const SpellRecord* spell = index.spellByEditorId(name);
            ASSERT_NE(spell, nullptr) << name;
            ASSERT_EQ(spell->mEffects.size(), 1u) << name;
            const MagicEffectRecord* m = index.magicEffect(spell->mEffects[0].mEffect);
            ASSERT_NE(m, nullptr) << name;
            std::cout << "SKYRIMSE-EFFCOND2 power " << name << " value " << m->mPrimaryValue << " archetype "
                      << m->mArchetype << " magnitude " << spell->mEffects[0].mMagnitude << " duration "
                      << spell->mEffects[0].mDuration << "\n";
            regenPowers += isRegenRateValue(m->mPrimaryValue) ? 1 : 0;
        }
        EXPECT_EQ(regenPowers, 3u); // Dragonskin is on actor value 83, not a rate
        const SpellRecord* redguard = index.spellByEditorId("PowerRedguardStaminaRegen");
        ASSERT_NE(redguard, nullptr);
        EXPECT_EQ(index.magicEffect(redguard->mEffects[0].mEffect)->mPrimaryValue, RegenValue::StaminaRate);
        EXPECT_FLOAT_EQ(redguard->mEffects[0].mMagnitude, 50.f);
        const SpellRecord* cry = index.spellByEditorId("PowerNordBattleCry");
        ASSERT_NE(cry, nullptr);
        const MagicEffectRecord* cryEffect = index.magicEffect(cry->mEffects[0].mEffect);
        ASSERT_NE(cryEffect, nullptr);
        EXPECT_EQ(cryEffect->mConditionList.size(), 4u);
        // evaluate Battle Cry's list for an ordinary actor, an undead one and a Daedra with the real ids
        effectconditions::Evaluator e;
        e.setExtended(true);
        Person ordinary, undead, daedra;
        undead.mKeywords = { kKwUndead };
        daedra.mKeywords = { kKwDaedra };
        EXPECT_EQ(run(e, cryEffect->mConditionList, &ordinary), Verdict::Pass);
        EXPECT_EQ(run(e, cryEffect->mConditionList, &undead), Verdict::Fail);
        EXPECT_EQ(run(e, cryEffect->mConditionList, &daedra), Verdict::Fail);
    }
}
