// Task 0154 (stage S7 of task 0135): the Skyrim SE pack's actor numbers and relations on generated records. No game
// data. Expected values are derived by hand from the rules in game_skyrimse/actor_mechanics.hpp.
#include <game_skyrimse/actor_mechanics.hpp>
#include <game_skyrimse/actors.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdkgame = openeden::sdk::game;

    constexpr FormId kRace = 0x200;
    constexpr FormId kClassMelee = 0x300; // weights 3 0 2: gains 11 0 4
    constexpr FormId kClassCitizen = 0x301; // weights 1 1 1: gains 8.33 3.33 3.33
    constexpr FormId kClassNone = 0x302; // no weights

    ActorNpc npc(FormId id, std::uint32_t flags, std::uint16_t level, std::uint16_t calcMin, std::uint16_t calcMax,
        std::uint16_t health, std::uint16_t magicka, std::uint16_t stamina, FormId classId = kClassMelee)
    {
        ActorNpc n;
        n.mCommon.mId = id;
        n.mCommon.mEditorId = "npc";
        n.mRace = kRace;
        n.mClass = classId;
        n.mHasConfig = true;
        n.mFlags = flags;
        n.mLevel = level;
        n.mCalcMin = calcMin;
        n.mCalcMax = calcMax;
        n.mHasStats = true;
        n.mHealth = health;
        n.mMagicka = magicka;
        n.mStamina = stamina;
        for (std::size_t i = 0; i < n.mSkills.size(); ++i)
            n.mSkills[i] = static_cast<std::uint8_t>(15 + i);
        return n;
    }

    ClassRecord makeClass(FormId id, std::uint8_t h, std::uint8_t m, std::uint8_t s)
    {
        ClassRecord c;
        c.mCommon.mId = id;
        c.mHasData = true;
        c.mHealthWeight = h;
        c.mMagickaWeight = m;
        c.mStaminaWeight = s;
        return c;
    }

    FactionRecord makeFaction(FormId id, std::vector<FactionRelation> relations = {})
    {
        FactionRecord f;
        f.mCommon.mId = id;
        f.mRelations = std::move(relations);
        return f;
    }

    struct Fixture
    {
        Fixture()
        {
            RaceRecord race;
            race.mCommon.mId = kRace;
            race.mHasData = true;
            race.mStartHealth = 50.f;
            race.mStartMagicka = 60.f;
            race.mStartStamina = 70.f;
            mRecords.mRaces.push_back(race);
            mRecords.mClasses.push_back(makeClass(kClassMelee, 3, 0, 2));
            mRecords.mClasses.push_back(makeClass(kClassCitizen, 1, 1, 1));
            ClassRecord none;
            none.mCommon.mId = kClassNone;
            mRecords.mClasses.push_back(none);
        }
        void finish() { mIndex.add(mRecords); }
        ActorRecords mRecords;
        ActorIndex mIndex;
    };

    // ---- the class gain ------------------------------------------------------------------------------------------

    TEST(SkyrimActorMechanicsGain, followsTheClassWeights)
    {
        const ClassRecord meleeClass = makeClass(1, 3, 0, 2);
        const ClassRecord mageClass = makeClass(1, 2, 4, 0);
        const ClassRecord citizenClass = makeClass(1, 1, 1, 1);
        const LevelGain melee = levelGainOf(&meleeClass);
        EXPECT_FLOAT_EQ(melee.mHealth, 11.f);
        EXPECT_FLOAT_EQ(melee.mMagicka, 0.f);
        EXPECT_FLOAT_EQ(melee.mStamina, 4.f);
        const LevelGain mage = levelGainOf(&mageClass);
        EXPECT_NEAR(mage.mHealth, 8.3333f, 1e-3);
        EXPECT_NEAR(mage.mMagicka, 6.6667f, 1e-3);
        EXPECT_FLOAT_EQ(mage.mStamina, 0.f);
        const LevelGain citizen = levelGainOf(&citizenClass);
        EXPECT_NEAR(citizen.mHealth, 8.3333f, 1e-3);
        EXPECT_NEAR(citizen.mMagicka, 3.3333f, 1e-3);
        EXPECT_NEAR(citizen.mStamina, 3.3333f, 1e-3);
    }

    TEST(SkyrimActorMechanicsGain, isZeroWithoutAClassOrWeights)
    {
        EXPECT_FLOAT_EQ(levelGainOf(nullptr).mHealth, 0.f);
        ClassRecord noData;
        EXPECT_FLOAT_EQ(levelGainOf(&noData).mHealth, 0.f);
        const ClassRecord zeroWeights = makeClass(1, 0, 0, 0);
        EXPECT_FLOAT_EQ(levelGainOf(&zeroWeights).mHealth, 0.f);
    }

    // ---- stats ---------------------------------------------------------------------------------------------------

    TEST(SkyrimActorMechanicsStats, aFixedLevelNpcKeepsItsStoredTotals)
    {
        Fixture f;
        f.mRecords.mNpcs.push_back(
            npc(0x100, NpcFlags::AutoCalcStats | NpcFlags::Unique, 4, 0, 0, 75, 60, 60, kClassCitizen));
        f.finish();
        sdkgame::ActorStats out;
        std::string error;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 1, out, error)) << error;
        EXPECT_EQ(out.level, 4);
        EXPECT_FLOAT_EQ(out.health, 75.f);
        EXPECT_FLOAT_EQ(out.magicka, 60.f);
        EXPECT_FLOAT_EQ(out.stamina, 60.f);
        EXPECT_FALSE(out.scalesWithPlayer);
        EXPECT_EQ(out.skills[0], 15);
        EXPECT_EQ(out.skills[17], 32);
        EXPECT_TRUE(out.unique);
        EXPECT_FALSE(out.essential);
        // The player's level does not matter to a fixed-level actor.
        sdkgame::ActorStats other;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 40, other, error));
        EXPECT_EQ(other.level, 4);
        EXPECT_FLOAT_EQ(other.health, 75.f);
    }

    TEST(SkyrimActorMechanicsStats, anActorThatScalesWithThePlayerFollowsTheLevelBetweenItsBounds)
    {
        Fixture f;
        // level field 1000 = multiplier 1.0; the stored totals are those of calc min 5 (hand-built: 90 / 50 / 70).
        f.mRecords.mNpcs.push_back(
            npc(0x100, NpcFlags::AutoCalcStats | NpcFlags::PlayerLevelMult, 1000, 5, 12, 90, 50, 70));
        f.finish();
        sdkgame::ActorStats out;
        std::string error;
        // Player level 1: below calc min, so level 5 and the stored totals.
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 1, out, error)) << error;
        EXPECT_EQ(out.level, 5);
        EXPECT_TRUE(out.scalesWithPlayer);
        EXPECT_FLOAT_EQ(out.health, 90.f);
        EXPECT_FLOAT_EQ(out.stamina, 70.f);
        // Player level 8: three levels above calc min: health +3 * 11, stamina +3 * 4, magicka +0.
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 8, out, error));
        EXPECT_EQ(out.level, 8);
        EXPECT_FLOAT_EQ(out.health, 123.f);
        EXPECT_FLOAT_EQ(out.magicka, 50.f);
        EXPECT_FLOAT_EQ(out.stamina, 82.f);
        // Player level 30: capped at calc max 12, seven levels above calc min.
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 30, out, error));
        EXPECT_EQ(out.level, 12);
        EXPECT_FLOAT_EQ(out.health, 90.f + 7 * 11.f);
        // 0 means the stand-in level (1).
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 0, out, error));
        EXPECT_EQ(out.level, 5);
    }

    TEST(SkyrimActorMechanicsStats, theMultiplierScalesTheLevelAndNoCalcMaxMeansNoCap)
    {
        Fixture f;
        f.mRecords.mNpcs.push_back(
            npc(0x100, NpcFlags::AutoCalcStats | NpcFlags::PlayerLevelMult, 500, 1, 0, 60, 50, 60));
        f.mRecords.mNpcs.push_back(
            npc(0x101, NpcFlags::AutoCalcStats | NpcFlags::PlayerLevelMult, 1000, 0, 0, 60, 50, 60));
        f.finish();
        sdkgame::ActorStats out;
        std::string error;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 20, out, error));
        EXPECT_EQ(out.level, 10); // 20 * 0.5
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x101, 0, 40, out, error));
        EXPECT_EQ(out.level, 40); // no calc max
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x101, 0, 1, out, error));
        EXPECT_EQ(out.level, 1); // calc min 0 reads as 1
    }

    TEST(SkyrimActorMechanicsStats, aStoredZeroFallsBackToTheRaceStartPlusTheOffset)
    {
        Fixture f;
        ActorNpc n = npc(0x100, 0, 1, 0, 0, 0, 0, 0);
        n.mHealthOffset = 10;
        n.mMagickaOffset = -5;
        n.mStaminaOffset = 0;
        f.mRecords.mNpcs.push_back(n);
        f.finish();
        sdkgame::ActorStats out;
        std::string error;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 1, out, error));
        EXPECT_FLOAT_EQ(out.health, 60.f);
        EXPECT_FLOAT_EQ(out.magicka, 55.f);
        EXPECT_FLOAT_EQ(out.stamina, 70.f);
    }

    TEST(SkyrimActorMechanicsStats, neverGoesBelowZero)
    {
        Fixture f;
        ActorNpc n = npc(0x100, 0, 1, 0, 0, 0, 0, 0);
        n.mHealthOffset = -500;
        f.mRecords.mNpcs.push_back(n);
        f.finish();
        sdkgame::ActorStats out;
        std::string error;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 1, out, error));
        EXPECT_FLOAT_EQ(out.health, 0.f);
    }

    TEST(SkyrimActorMechanicsStats, aNegativeStoredTotalIsZeroNotAHugeNumber)
    {
        // A race with no magicka and an ACBS offset of -25 stores -25, which is 65511 read as unsigned (the installed
        // dunTransmogrifyHare and EncChicken). It is 0, and is not replaced by race start + offset (the same negative).
        Fixture f;
        ActorNpc n = npc(0x100, 0, 1, 0, 0, 65530, 65511, 32768);
        n.mMagickaOffset = -25;
        f.mRecords.mNpcs.push_back(n);
        ActorNpc bigNegativeLevelGain = npc(0x101, NpcFlags::PlayerLevelMult, 1000, 2, 0, 65511, 40, 40);
        f.mRecords.mNpcs.push_back(bigNegativeLevelGain);
        f.finish();
        sdkgame::ActorStats out;
        std::string error;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 1, out, error)) << error;
        EXPECT_FLOAT_EQ(out.health, 0.f); // -6
        EXPECT_FLOAT_EQ(out.magicka, 0.f); // -25
        EXPECT_FLOAT_EQ(out.stamina, 0.f); // -32768
        // A scaling actor whose stored total is negative starts from 0 and gains from there.
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x101, 0, 4, out, error));
        EXPECT_EQ(out.level, 4);
        EXPECT_FLOAT_EQ(out.health, 2.f * 11.f); // two levels above calc min 2, gain 11 (class 3 0 2), from 0
    }

    TEST(SkyrimActorMechanicsStats, aLeveledTemplateIsPickedAtTheAppearanceLevel)
    {
        // The drawn actor and its numbers must come from the same leveled entry: the pick uses kAppearanceLevel (30),
        // so the entry that needs level 30 wins over the one for level 1 (entries above the level are not eligible,
        // and only the highest eligible level is used).
        Fixture f;
        f.mRecords.mNpcs.push_back(npc(0x200, 0, 5, 0, 0, 100, 50, 50));
        f.mRecords.mNpcs.push_back(npc(0x201, 0, 25, 0, 0, 500, 50, 50));
        ActorNpc child = npc(0x100, 0, 1, 0, 0, 1, 1, 1);
        child.mTemplate = 0x300;
        child.mTemplateFlags = TemplateFlags::Stats;
        f.mRecords.mNpcs.push_back(child);
        LeveledList list;
        list.mCommon.mId = 0x300;
        list.mIsNpcList = true;
        LeveledEntry low;
        low.mLevel = 1;
        low.mReference = 0x200;
        low.mCount = 1;
        LeveledEntry high;
        high.mLevel = 30;
        high.mReference = 0x201;
        high.mCount = 1;
        list.mEntries = { low, high };
        f.finish();
        f.mIndex.addLeveledLists({ list });
        sdkgame::ActorStats out;
        std::string error;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 1, out, error)) << error;
        EXPECT_EQ(out.level, 25);
        EXPECT_FLOAT_EQ(out.health, 500.f);
    }

    TEST(SkyrimActorMechanicsStats, theFlagsComeFromTheRecord)
    {
        Fixture f;
        f.mRecords.mNpcs.push_back(
            npc(0x100, NpcFlags::Female | NpcFlags::Essential | NpcFlags::Protected | NpcFlags::Invulnerable, 2, 0, 0,
                50, 50, 50));
        f.finish();
        sdkgame::ActorStats out;
        std::string error;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 1, out, error));
        EXPECT_TRUE(out.female);
        EXPECT_TRUE(out.essential);
        EXPECT_TRUE(out.protectedActor);
        EXPECT_TRUE(out.invulnerable);
        EXPECT_FALSE(out.unique);
    }

    TEST(SkyrimActorMechanicsStats, aTemplateWithTheStatsFlagLendsItsNumbersAndLevel)
    {
        Fixture f;
        f.mRecords.mNpcs.push_back(npc(0x200, NpcFlags::AutoCalcStats, 20, 0, 0, 300, 50, 150));
        ActorNpc child = npc(0x100, 0, 1, 0, 0, 10, 10, 10);
        child.mTemplate = 0x200;
        child.mTemplateFlags = TemplateFlags::Stats;
        f.mRecords.mNpcs.push_back(child);
        f.finish();
        sdkgame::ActorStats out;
        std::string error;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 1, out, error)) << error;
        EXPECT_EQ(out.level, 20);
        EXPECT_FLOAT_EQ(out.health, 300.f);
        EXPECT_FLOAT_EQ(out.stamina, 150.f);
    }

    TEST(SkyrimActorMechanicsStats, anUnknownBaseIsRefusedWithAReason)
    {
        Fixture f;
        f.finish();
        sdkgame::ActorStats out;
        out.level = 99;
        std::string error;
        EXPECT_FALSE(computeActorStats(f.mIndex, 0x999, 0, 1, out, error));
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(out.level, 1); // reset
    }

    TEST(SkyrimActorMechanicsStats, aMissingClassMeansNoLevelGain)
    {
        Fixture f;
        f.mRecords.mNpcs.push_back(npc(0x100, NpcFlags::PlayerLevelMult, 1000, 2, 0, 80, 50, 50, kClassNone));
        f.finish();
        sdkgame::ActorStats out;
        std::string error;
        ASSERT_TRUE(computeActorStats(f.mIndex, 0x100, 0, 10, out, error));
        EXPECT_EQ(out.level, 10);
        EXPECT_FLOAT_EQ(out.health, 80.f);
    }

    // ---- relations -----------------------------------------------------------------------------------------------

    constexpr FormId kPlayer = kPlayerFactionId;

    std::vector<FactionMembership> members(std::initializer_list<FormId> ids)
    {
        std::vector<FactionMembership> v;
        for (FormId id : ids)
            v.push_back({ id, 0 });
        return v;
    }

    TEST(SkyrimActorMechanicsRelation, anEnemyEntryInEitherDirectionMakesEnemies)
    {
        Fixture f;
        f.mRecords.mFactionRecords.push_back(makeFaction(0x500, { { kPlayer, 0, CombatReaction::Enemy } }));
        f.mRecords.mFactionRecords.push_back(makeFaction(kPlayer, { { 0x501, 0, CombatReaction::Enemy } }));
        f.finish();
        EXPECT_EQ(
            relationOfFactions(f.mIndex, members({ 0x500 }), members({ kPlayer })), sdkgame::ActorRelation::Enemy);
        EXPECT_EQ(
            relationOfFactions(f.mIndex, members({ kPlayer }), members({ 0x500 })), sdkgame::ActorRelation::Enemy);
        // Stated only by the player faction's record:
        EXPECT_EQ(
            relationOfFactions(f.mIndex, members({ 0x501 }), members({ kPlayer })), sdkgame::ActorRelation::Enemy);
    }

    TEST(SkyrimActorMechanicsRelation, enemyBeatsAllyBeatsFriendBeatsNeutral)
    {
        Fixture f;
        f.mRecords.mFactionRecords.push_back(makeFaction(0x500, { { kPlayer, 0, CombatReaction::Friend } }));
        f.mRecords.mFactionRecords.push_back(makeFaction(0x501, { { kPlayer, 0, CombatReaction::Ally } }));
        f.mRecords.mFactionRecords.push_back(makeFaction(0x502, { { kPlayer, 0, CombatReaction::Enemy } }));
        f.mRecords.mFactionRecords.push_back(makeFaction(0x503, { { kPlayer, 0, CombatReaction::Neutral } }));
        f.finish();
        const auto rel = [&](std::initializer_list<FormId> a) {
            return relationOfFactions(f.mIndex, members(a), members({ kPlayer }));
        };
        EXPECT_EQ(rel({ 0x503 }), sdkgame::ActorRelation::Neutral);
        EXPECT_EQ(rel({ 0x500 }), sdkgame::ActorRelation::Friend);
        EXPECT_EQ(rel({ 0x500, 0x501 }), sdkgame::ActorRelation::Ally);
        EXPECT_EQ(rel({ 0x500, 0x501, 0x502 }), sdkgame::ActorRelation::Enemy);
        EXPECT_EQ(rel({ 0x500, 0x501, 0x502, 0x503 }), sdkgame::ActorRelation::Enemy);
        EXPECT_EQ(rel({}), sdkgame::ActorRelation::Neutral);
        EXPECT_EQ(rel({ 0x999 }), sdkgame::ActorRelation::Neutral); // a faction with no record says nothing
    }

    TEST(SkyrimActorMechanicsRelation, aSharedFactionIsAlly)
    {
        Fixture f;
        f.finish();
        EXPECT_EQ(
            relationOfFactions(f.mIndex, members({ 0x700, 0x701 }), members({ 0x701 })), sdkgame::ActorRelation::Ally);
    }

    TEST(SkyrimActorMechanicsRelation, nullAndUnresolvedFactionsAreIgnored)
    {
        Fixture f;
        f.mRecords.mFactionRecords.push_back(makeFaction(0x500, { { kPlayer, 0, CombatReaction::Enemy } }));
        f.finish();
        EXPECT_EQ(relationOfFactions(f.mIndex, members({ kNullFormId, kUnresolvedFormId }),
                      members({ kNullFormId, kUnresolvedFormId })),
            sdkgame::ActorRelation::Neutral);
    }

    TEST(SkyrimActorMechanicsRelation, aLaterEntryForTheSameFactionWins)
    {
        Fixture f;
        f.mRecords.mFactionRecords.push_back(
            makeFaction(0x500, { { kPlayer, 0, CombatReaction::Enemy }, { kPlayer, 0, CombatReaction::Friend } }));
        f.finish();
        EXPECT_EQ(
            relationOfFactions(f.mIndex, members({ 0x500 }), members({ kPlayer })), sdkgame::ActorRelation::Friend);
    }

    TEST(SkyrimActorMechanicsRelation, aGroupCombatReactionAboveThreeIsNeutral)
    {
        Fixture f;
        f.mRecords.mFactionRecords.push_back(makeFaction(0x500, { { kPlayer, 0, 77 } }));
        f.finish();
        EXPECT_EQ(
            relationOfFactions(f.mIndex, members({ 0x500 }), members({ kPlayer })), sdkgame::ActorRelation::Neutral);
    }

    // ---- the attack rule and the disposition -------------------------------------------------------------------

    TEST(SkyrimActorMechanicsDisposition, theAttackRuleTable)
    {
        using A = sdkgame::ActorAggression;
        using R = sdkgame::ActorRelation;
        const R relations[] = { R::Neutral, R::Enemy, R::Ally, R::Friend };
        // Expected per aggression, in the order Neutral, Enemy, Ally, Friend.
        const bool unaggressive[] = { false, false, false, false };
        const bool aggressive[] = { false, true, false, false };
        const bool very[] = { true, true, false, false };
        const bool frenzied[] = { true, true, true, true };
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_EQ(wouldAttackOnSight(A::Unaggressive, relations[i]), unaggressive[i]) << i;
            EXPECT_EQ(wouldAttackOnSight(A::Aggressive, relations[i]), aggressive[i]) << i;
            EXPECT_EQ(wouldAttackOnSight(A::VeryAggressive, relations[i]), very[i]) << i;
            EXPECT_EQ(wouldAttackOnSight(A::Frenzied, relations[i]), frenzied[i]) << i;
        }
    }

    ActorNpc withAi(ActorNpc n, std::uint8_t aggression, std::vector<FormId> factions)
    {
        AiData ai;
        ai.mAggression = aggression;
        n.mAi = ai;
        for (FormId id : factions)
            n.mFactions.push_back({ id, 0 });
        return n;
    }

    TEST(SkyrimActorMechanicsDisposition, aBanditIsHostileATownsmanIsNot)
    {
        Fixture f;
        f.mRecords.mFactionRecords.push_back(makeFaction(0x500, { { kPlayer, 0, CombatReaction::Enemy } })); // bandits
        f.mRecords.mFactionRecords.push_back(makeFaction(0x501, { { kPlayer, 0, CombatReaction::Neutral } })); // town
        f.mRecords.mNpcs.push_back(withAi(npc(0x100, 0, 5, 0, 0, 100, 50, 100), 2, { 0x500 }));
        f.mRecords.mNpcs.push_back(withAi(npc(0x101, 0, 4, 0, 0, 75, 60, 60), 0, { 0x501 }));
        f.mRecords.mNpcs.push_back(withAi(npc(0x102, 0, 4, 0, 0, 75, 60, 60), 1, { 0x501 })); // aggressive, not enemies
        f.finish();
        sdkgame::ActorDisposition out;
        std::string error;
        ASSERT_TRUE(computeActorDisposition(f.mIndex, 0x100, 0, MechanicsOptions(), out, error)) << error;
        EXPECT_EQ(out.relationToPlayer, sdkgame::ActorRelation::Enemy);
        EXPECT_EQ(out.aggression, sdkgame::ActorAggression::VeryAggressive);
        EXPECT_TRUE(out.hostileOnSight);
        ASSERT_TRUE(computeActorDisposition(f.mIndex, 0x101, 0, MechanicsOptions(), out, error));
        EXPECT_EQ(out.relationToPlayer, sdkgame::ActorRelation::Neutral);
        EXPECT_EQ(out.aggression, sdkgame::ActorAggression::Unaggressive);
        EXPECT_FALSE(out.hostileOnSight);
        ASSERT_TRUE(computeActorDisposition(f.mIndex, 0x102, 0, MechanicsOptions(), out, error));
        EXPECT_FALSE(out.hostileOnSight);
    }

    TEST(SkyrimActorMechanicsDisposition, theAggressionComesFromTheAiTemplateAndIsCapped)
    {
        Fixture f;
        f.mRecords.mNpcs.push_back(withAi(npc(0x200, 0, 5, 0, 0, 100, 50, 100), 200, {}));
        ActorNpc child = npc(0x100, 0, 1, 0, 0, 1, 1, 1);
        child.mTemplate = 0x200;
        child.mTemplateFlags = TemplateFlags::AiData;
        f.mRecords.mNpcs.push_back(child);
        f.finish();
        sdkgame::ActorDisposition out;
        std::string error;
        ASSERT_TRUE(computeActorDisposition(f.mIndex, 0x100, 0, MechanicsOptions(), out, error)) << error;
        EXPECT_EQ(out.aggression, sdkgame::ActorAggression::Frenzied);
    }

    TEST(SkyrimActorMechanicsDisposition, thePlayersFactionsAreAnOption)
    {
        Fixture f;
        f.mRecords.mFactionRecords.push_back(makeFaction(0x500, { { 0x900, 0, CombatReaction::Enemy } }));
        f.mRecords.mNpcs.push_back(withAi(npc(0x100, 0, 5, 0, 0, 100, 50, 100), 1, { 0x500 }));
        f.finish();
        sdkgame::ActorDisposition out;
        std::string error;
        ASSERT_TRUE(computeActorDisposition(f.mIndex, 0x100, 0, MechanicsOptions(), out, error));
        EXPECT_EQ(out.relationToPlayer, sdkgame::ActorRelation::Neutral);
        MechanicsOptions joined;
        joined.mPlayerFactions.push_back(0x900);
        ASSERT_TRUE(computeActorDisposition(f.mIndex, 0x100, 0, joined, out, error));
        EXPECT_EQ(out.relationToPlayer, sdkgame::ActorRelation::Enemy);
        EXPECT_TRUE(out.hostileOnSight);
    }

    TEST(SkyrimActorMechanicsDisposition, anUnknownBaseIsRefused)
    {
        Fixture f;
        f.finish();
        sdkgame::ActorDisposition out;
        std::string error;
        EXPECT_FALSE(computeActorDisposition(f.mIndex, 0x999, 0, MechanicsOptions(), out, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(SkyrimActorMechanicsDisposition, relationBetweenTwoActors)
    {
        Fixture f;
        f.mRecords.mFactionRecords.push_back(makeFaction(0x500, { { 0x501, 0, CombatReaction::Enemy } }));
        f.mRecords.mNpcs.push_back(withAi(npc(0x100, 0, 5, 0, 0, 100, 50, 100), 0, { 0x500 }));
        f.mRecords.mNpcs.push_back(withAi(npc(0x101, 0, 5, 0, 0, 100, 50, 100), 0, { 0x501 }));
        f.mRecords.mNpcs.push_back(withAi(npc(0x102, 0, 5, 0, 0, 100, 50, 100), 0, { 0x502 }));
        f.finish();
        EXPECT_EQ(relationBetweenActors(f.mIndex, 0x100, 0, 0x101, 0), sdkgame::ActorRelation::Enemy);
        EXPECT_EQ(relationBetweenActors(f.mIndex, 0x101, 0, 0x100, 0), sdkgame::ActorRelation::Enemy);
        EXPECT_EQ(relationBetweenActors(f.mIndex, 0x100, 0, 0x102, 0), sdkgame::ActorRelation::Neutral);
        EXPECT_EQ(relationBetweenActors(f.mIndex, 0x100, 0, 0x100, 0), sdkgame::ActorRelation::Ally); // itself
        EXPECT_EQ(relationBetweenActors(f.mIndex, 0x100, 0, 0x999, 0), sdkgame::ActorRelation::Neutral);
    }

    // ---- the new record fields, from generated plugin bytes ---------------------------------------------------

    LoadOrder defaultOrder()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    Bytes header()
    {
        return tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
    }

    ActorRecords parseBytes(const Bytes& bytes, bool* ok = nullptr)
    {
        ActorRecords records;
        Content::Error error;
        const bool parsed
            = parseActorRecordsBytes(bytes.str(), "test.esp", defaultOrder(), ActorLimits(), records, error);
        if (ok != nullptr)
            *ok = parsed;
        return records;
    }

    Bytes plugin(const char (&type)[5], const Bytes& records)
    {
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC(type), 0, records));
        return bytes;
    }

    Bytes xnam(std::uint32_t faction, std::int32_t modifier, std::uint32_t reaction)
    {
        return sub4("XNAM", Bytes().u32(faction).u32(static_cast<std::uint32_t>(modifier)).u32(reaction));
    }

    TEST(SkyrimActorMechanicsParse, aFactionKeepsItsRelationsAndFlags)
    {
        Bytes f;
        f.append(sub4("EDID", Bytes().zstring("TestFaction")));
        f.append(xnam(0x00000DB1, 0, CombatReaction::Enemy));
        f.append(xnam(0x01000500, 0, CombatReaction::Friend));
        f.append(sub4("DATA", Bytes().u32(0x41)));
        bool ok = false;
        const ActorRecords records = parseBytes(plugin("FACT", record4("FACT", 0, 0x01000400, f)), &ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(records.mFactionRecords.size(), 1u);
        const FactionRecord& r = records.mFactionRecords[0];
        EXPECT_EQ(r.mCommon.mEditorId, "TestFaction");
        EXPECT_EQ(r.mFlags, 0x41u);
        ASSERT_EQ(r.mRelations.size(), 2u);
        EXPECT_EQ(r.mRelations[0].mOther, 0x00000DB1u);
        EXPECT_EQ(r.mRelations[0].mReaction, CombatReaction::Enemy);
        EXPECT_EQ(r.mRelations[1].mOther, 0x01000500u);
        EXPECT_EQ(r.mRelations[1].mReaction, CombatReaction::Friend);
        EXPECT_EQ(records.mCounts.at(Content::fourCC("FACT")).mParsed, 1u);
    }

    TEST(SkyrimActorMechanicsParse, aRelationOfTheWrongSizeIsOddAndDecodesToNothing)
    {
        Bytes f;
        f.append(sub4("EDID", Bytes().zstring("Odd")));
        f.append(sub4("XNAM", Bytes().u32(1).u32(2)));
        f.append(xnam(0x00000DB1, -5, CombatReaction::Ally));
        const ActorRecords records = parseBytes(plugin("FACT", record4("FACT", 0, 0x01000400, f)));
        ASSERT_EQ(records.mFactionRecords.size(), 1u);
        EXPECT_EQ(records.mFactionRecords[0].mRelations.size(), 1u);
        EXPECT_EQ(records.mFactionRecords[0].mRelations[0].mModifier, -5);
        EXPECT_EQ(records.mOddBy.at("FACT.XNAM"), 1u);
    }

    TEST(SkyrimActorMechanicsParse, tooManyRelationsFailTheRecordNotTheWalk)
    {
        Bytes f;
        f.append(sub4("EDID", Bytes().zstring("Big")));
        for (int i = 0; i < 5; ++i)
            f.append(xnam(0x01000500 + i, 0, 0));
        Bytes bytes = plugin("FACT", record4("FACT", 0, 0x01000400, f));
        ActorLimits limits;
        limits.mMaxEntriesPerList = 4;
        ActorRecords records;
        Content::Error error;
        ASSERT_TRUE(parseActorRecordsBytes(bytes.str(), "test.esp", defaultOrder(), limits, records, error));
        EXPECT_EQ(records.mCounts.at(Content::fourCC("FACT")).mFailed, 1u);
        EXPECT_TRUE(records.mFactionRecords.empty());
    }

    TEST(SkyrimActorMechanicsParse, anNpcKeepsItsThreeOffsets)
    {
        Bytes npcBytes;
        npcBytes.append(sub4("EDID", Bytes().zstring("N")));
        Bytes acbs;
        // flags, magicka offset, stamina offset, level, calc min, calc max, speed, disposition, template flags,
        // health offset, bleedout
        acbs.u32(0)
            .u16(static_cast<std::uint16_t>(-25))
            .u16(20)
            .u16(3)
            .u16(1)
            .u16(9)
            .u16(100)
            .u16(0)
            .u16(0)
            .u16(static_cast<std::uint16_t>(-15))
            .u16(0);
        npcBytes.append(sub4("ACBS", acbs));
        const ActorRecords records = parseBytes(plugin("NPC_", record4("NPC_", 0, 0x01000100, npcBytes)));
        ASSERT_EQ(records.mNpcs.size(), 1u);
        EXPECT_EQ(records.mNpcs[0].mMagickaOffset, -25);
        EXPECT_EQ(records.mNpcs[0].mStaminaOffset, 20);
        EXPECT_EQ(records.mNpcs[0].mHealthOffset, -15);
        EXPECT_EQ(records.mNpcs[0].mLevel, 3);
    }

    TEST(SkyrimActorMechanicsParse, aRaceKeepsItsStartingValues)
    {
        Bytes r;
        r.append(sub4("EDID", Bytes().zstring("R")));
        Bytes data;
        for (int i = 0; i < 14; ++i)
            data.u8(0);
        data.u16(0).f32(1.f).f32(1.f).f32(1.f).f32(1.f).u32(0).f32(100.f).f32(150.f).f32(75.f).f32(300.f);
        while (data.size() < 164)
            data.u8(0);
        r.append(sub4("DATA", data));
        const ActorRecords records = parseBytes(plugin("RACE", record4("RACE", 0, 0x01000100, r)));
        ASSERT_EQ(records.mRaces.size(), 1u);
        EXPECT_FLOAT_EQ(records.mRaces[0].mStartHealth, 100.f);
        EXPECT_FLOAT_EQ(records.mRaces[0].mStartMagicka, 150.f);
        EXPECT_FLOAT_EQ(records.mRaces[0].mStartStamina, 75.f);
    }

    TEST(SkyrimActorMechanicsParse, aNonFiniteOrNegativeStartIsZero)
    {
        Bytes r;
        r.append(sub4("EDID", Bytes().zstring("R")));
        Bytes data;
        for (int i = 0; i < 14; ++i)
            data.u8(0);
        data.u16(0).f32(1.f).f32(1.f).f32(1.f).f32(1.f).u32(0).f32(-4.f).u32(0x7FC00000u).f32(75.f);
        while (data.size() < 164)
            data.u8(0);
        r.append(sub4("DATA", data));
        const ActorRecords records = parseBytes(plugin("RACE", record4("RACE", 0, 0x01000100, r)));
        ASSERT_EQ(records.mRaces.size(), 1u);
        EXPECT_FLOAT_EQ(records.mRaces[0].mStartHealth, 0.f);
        EXPECT_FLOAT_EQ(records.mRaces[0].mStartMagicka, 0.f);
        EXPECT_FLOAT_EQ(records.mRaces[0].mStartStamina, 75.f);
    }

    TEST(SkyrimActorMechanicsParse, aLaterPluginsFactionReplacesAnEarlierOne)
    {
        ActorRecords first;
        first.mFactionRecords.push_back(makeFaction(0x500, { { kPlayer, 0, CombatReaction::Enemy } }));
        ActorRecords second;
        second.mFactionRecords.push_back(makeFaction(0x500, {}));
        ActorIndex index;
        index.add(first);
        ASSERT_NE(index.faction(0x500), nullptr);
        EXPECT_EQ(index.faction(0x500)->mRelations.size(), 1u);
        index.add(second);
        EXPECT_TRUE(index.faction(0x500)->mRelations.empty());
        EXPECT_EQ(index.faction(0x999), nullptr);
    }
}
