// The condition functions added by the "condnames" follow-up (fn_566, 426, 629, 359, 77, 47, 67, 14, 131, 46, 70, 249,
// 579, 543, 560, 73, 562), the string parameters (CIS2), the third parameter of run-on 5 and the swap flag, on
// generated conditions and a fake world. No game data.
#include <game_skyrimse/conditions.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace game_skyrimse::conditions;
    using ContentTests::Bytes;
    using game_skyrimse::survey::Ctda;

    constexpr std::uint8_t kEq = 0, kGe = 3, kLt = 4;
    constexpr std::uint8_t kSwap = 0x10;

    Ctda cond(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0, std::uint32_t p2 = 0,
        std::uint8_t flags = 0, std::uint32_t runOn = 0, std::uint32_t reference = 0, std::uint32_t p3 = 0)
    {
        Ctda c;
        c.mCompare = compare;
        c.mFlags = flags;
        c.mTypeByte = static_cast<std::uint8_t>((compare << 5) | flags);
        c.mComparison = value;
        std::memcpy(&c.mComparisonBits, &value, 4);
        c.mFunction = function;
        c.mParam1 = p1;
        c.mParam2 = p2;
        c.mRunOn = runOn;
        c.mReference = reference;
        c.mParam3 = p3;
        return c;
    }

    class World final : public ConditionWorld
    {
    public:
        Subject mSubject = 7;
        // answers
        std::uint32_t mAlias = 3;
        std::uint32_t mVoice = 0x40;
        std::uint32_t mLocation = 0x50;
        std::uint32_t mCell = 0x60;
        std::uint32_t mItem = 0x70;
        std::uint32_t mKeyword = 0x80;
        std::uint32_t mShout = 0x90;
        std::uint32_t mFaction = 0xA0;
        std::uint32_t mQuest = 9;
        std::string mVarName = "counter";
        float mVarValue = 4.f;
        float mRandom = 42.f;
        float mItems = 5.f;
        float mAv = 33.f;
        float mRank = 2.f;
        int mSex = 1;
        bool mDead = true;
        bool mTalking = true;
        bool mCompleted = true;
        Subject mPlayer = 1;
        // what the evaluator asked
        mutable std::uint32_t mAskedRunOn = 99, mAskedReference = 99, mAskedParam3 = 99;
        mutable std::uint32_t mAskedAv = 99;

        Subject resolveRunOn(std::uint32_t, std::uint32_t) const override { return mSubject; }
        Subject resolveSubject(const Ctda& c) const override
        {
            mAskedRunOn = c.mRunOn;
            mAskedReference = c.mReference;
            mAskedParam3 = c.mParam3;
            return c.mRunOn == 3 ? mPlayer : mSubject;
        }
        bool hasBaseId(Subject, std::uint32_t) const override { return false; }
        float questStage(std::uint32_t) const override { return 0.f; }
        bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
        bool inFaction(Subject s, std::uint32_t f) const override { return s == mSubject && f == mFaction; }
        float globalValue(std::uint32_t) const override { return 0.f; }

        bool isAliasRef(Subject s, std::uint32_t a) const override { return s == mSubject && a == mAlias; }
        bool hasVoiceType(Subject s, std::uint32_t v) const override { return s == mSubject && v == mVoice; }
        float questVariable(std::uint32_t q, const std::string& n) const override
        {
            return q == mQuest && n == mVarName ? mVarValue : -1.f;
        }
        bool inLocation(Subject s, std::uint32_t l) const override { return s == mSubject && l == mLocation; }
        float randomPercent() const override { return mRandom; }
        float itemCount(Subject s, std::uint32_t i) const override
        {
            return s == mSubject && i == mItem ? mItems : 0.f;
        }
        bool inCell(Subject s, std::uint32_t c) const override { return s == mSubject && c == mCell; }
        float actorValue(Subject s, std::uint32_t av) const override
        {
            mAskedAv = av;
            return s == mSubject ? mAv : 0.f;
        }
        Subject playerSubject() const override { return mPlayer; }
        int sexOf(Subject s) const override { return s == mSubject ? mSex : (s == mPlayer ? 0 : -1); }
        bool isDead(Subject s) const override { return s == mSubject && mDead; }
        bool inDialogueWithPlayer(Subject s) const override { return s == mSubject && mTalking; }
        bool hasEquippedShout(Subject s, std::uint32_t x) const override { return s == mSubject && x == mShout; }
        bool questCompleted(std::uint32_t q) const override { return q == mQuest && mCompleted; }
        bool hasKeyword(Subject s, std::uint32_t k) const override { return s == mSubject && k == mKeyword; }
        float factionRank(Subject s, std::uint32_t f) const override
        {
            return s == mSubject && f == mFaction ? mRank : -1.f;
        }
        bool locationHasKeyword(Subject s, std::uint32_t k) const override { return s == mSubject && k == mKeyword; }
        float keywordDataForLocation(std::uint32_t l, std::uint32_t k) const override
        {
            return l == mLocation && k == mKeyword ? mKeywordData : 0.f;
        }
        float mKeywordData = 2.5f;
        float relationshipRank(Subject s, std::uint32_t t) const override
        {
            return s == mSubject && t == mTarget ? mRank403 : 0.f;
        }
        std::uint32_t mTarget = 0x14;
        float mRank403 = 3.f;
        std::uint32_t mRace = 0xB0;
        bool mDisabled = true;
        bool mSneaking = true;
        bool mRunning = true;
        std::uint32_t mWorldspace = 0xC0;
        std::uint32_t mEquipped = 0xD0;
        bool mServices = true;
        bool mInterior = true;
        bool hasMagicEffectKeyword(Subject s, std::uint32_t k) const override { return s == mSubject && k == 0x80; }
        bool locationCleared(std::uint32_t l) const override { return l == 0x50; }
        bool locationLoaded(std::uint32_t l) const override { return l == 0x51; }
        bool mIsCastingFlag = true;
        bool isCasting(Subject s) const override { return s == mSubject && mIsCastingFlag; }
        bool mIsUnderArrestFlag = true;
        bool isUnderArrest(Subject s) const override { return s == mSubject && mIsUnderArrestFlag; }
        bool mHasLoaded3DFlag = true;
        bool hasLoaded3D(Subject s) const override { return s == mSubject && mHasLoaded3DFlag; }
        bool mIsRestrainedFlag = true;
        bool isRestrained(Subject s) const override { return s == mSubject && mIsRestrainedFlag; }
        float weaponAnimType(Subject s) const override { return s == mSubject ? 108.f : 0.f; }
        float flyingState(Subject s) const override { return s == mSubject ? 633.f : 0.f; }
        float clothingValue(Subject s) const override { return s == mSubject ? 41.f : 0.f; }
        float equippedItemType(Subject s, std::uint32_t p) const override
        {
            return s == mSubject && p == 1 ? 9.f : 0.f;
        }
        bool wasKilledBy(Subject s, std::uint32_t p) const override { return s == mSubject && p == 0x14; }
        float refTypeAliveCount(std::uint32_t a, std::uint32_t b) const override
        {
            return a == 0x50 && b == 0x200 ? 6.f : 0.f;
        }
        bool mHasDiseaseFlag = true;
        bool hasDisease(Subject s) const override { return s == mSubject && mHasDiseaseFlag; }
        bool mIsChildFlag = true;
        bool isChild(Subject s) const override { return s == mSubject && mIsChildFlag; }
        float crimeGoldNonviolent(Subject s, std::uint32_t f) const override
        {
            return f == 0xA0 ? 20.f : (f == 0 && s == mSubject ? 5.f : 0.f);
        }
        bool detects(Subject s, std::uint32_t t) const override { return s == mSubject && t == 0x14; }
        float raininess() const override { return 1.f; }
        bool hasRefType(Subject s, std::uint32_t r) const override { return s == mSubject && r == 0x200; }
        float weaponOutState(Subject s) const override { return s == mSubject ? 1.f : 0.f; }
        float keywordDataForCurrentLocation(Subject s, std::uint32_t k) const override
        {
            return s == mSubject && k == 0x80 ? 10.f : 0.f;
        }
        bool wornHasKeyword(Subject s, std::uint32_t k) const override { return s == mSubject && k == 0x81; }
        float snowiness() const override { return 0.25f; }
        bool mIsArrestingActorFlag = true;
        bool isArrestingActor(Subject s) const override { return s == mSubject && mIsArrestingActorFlag; }
        bool mIsLockedFlag = true;
        bool isLocked(Subject s) const override { return s == mSubject && mIsLockedFlag; }
        float weatherPleasantness() const override { return 0.5f; }
        bool mIsInSceneFlag = true;
        bool isInScene(Subject s) const override { return s == mSubject && mIsInSceneFlag; }
        float crimeGoldViolent(Subject s, std::uint32_t f) const override
        {
            return f == 0xA0 ? 30.f : (f == 0 && s == mSubject ? 10.f : 0.f);
        }
        bool baseInList(Subject s, std::uint32_t l) const override { return s == mSubject && l == 0x1F0; }
        bool weaponInList(Subject s, std::uint32_t l) const override { return s == mSubject && l == 0x1F1; }
        bool locationInList(Subject s, std::uint32_t l) const override { return s == mSubject && l == 0x1F2; }
        bool inSameCell(Subject s, std::uint32_t r) const override { return s == mSubject && r == 0x14; }
        bool mIsBribedByPlayerFlag = true;
        bool isBribedByPlayer(Subject s) const override { return s == mSubject && mIsBribedByPlayerFlag; }
        bool mIsCommandedActorFlag = true;
        bool isCommandedActor(Subject s) const override { return s == mSubject && mIsCommandedActorFlag; }
        bool mIsAllowedToFlyFlag = true;
        bool isAllowedToFly(Subject s) const override { return s == mSubject && mIsAllowedToFlyFlag; }
        float daysInJail(Subject s) const override { return s == mSubject ? 4.f : 0.f; }
        bool canPayCrimeGold(Subject s, std::uint32_t f) const override
        {
            return s == mSubject && (f == 0 || f == 0xA0);
        }
        bool hasCombatTarget(Subject s, std::uint32_t t) const override { return s == mSubject && t == 0x14; }
        bool combatTargetHasKeyword(Subject s, std::uint32_t k) const override { return s == mSubject && k == 0x80; }
        bool isObjectType(Subject s, std::uint32_t c) const override { return s == mSubject && c == 4; }
        bool hasPerk(Subject s, std::uint32_t p) const override { return s == mSubject && p == 0xC3680; }
        float healthFraction(Subject s) const override { return s == mSubject ? 0.4f : 0.f; }
        float mapMarkerState(Subject s) const override { return s == mSubject ? 2.f : 0.f; }
        bool isParentOf(Subject s, std::uint32_t c) const override { return s == mSubject && c == 0x1E0; }
        float factionRelation(Subject s, std::uint32_t t) const override
        {
            return s == mSubject && t == 0x14 ? 3.f : 0.f;
        }
        float moveDirection(Subject s) const override { return s == mSubject ? 3.f : 0.f; }
        bool mInFriendStateWithPlayerFlag = true;
        bool inFriendStateWithPlayer(Subject s) const override { return s == mSubject && mInFriendStateWithPlayerFlag; }
        bool isSpellTarget(Subject s, std::uint32_t m) const override { return s == mSubject && m == 0x1D0; }
        bool mIsSmallBumpFlag = true;
        bool isSmallBump(Subject s) const override { return s == mSubject && mIsSmallBumpFlag; }
        bool mIsGuardClassFlag = true;
        bool isGuardClass(Subject s) const override { return s == mSubject && mIsGuardClassFlag; }
        bool mIsFlyingFlag = true;
        bool isFlying(Subject s) const override { return s == mSubject && mIsFlyingFlag; }
        float combatGroupSize(Subject s) const override { return s == mSubject ? 3.f : 0.f; }
        float friendHits(Subject s, std::uint32_t t) const override { return s == mSubject && t == 0x14 ? 2.f : 0.f; }
        bool locAliasHasKeyword(std::uint32_t a, std::uint32_t k) const override { return a == 5 && k == 0x1C0; }
        float gameHour() const override { return 19.75f; }
        bool mInFavorStateFlag = true;
        bool inFavorState(Subject s) const override { return s == mSubject && mInFavorStateFlag; }
        bool mAlarmed = true;
        bool hasAssociation(Subject s, std::uint32_t o, std::uint32_t a) const override
        {
            return s == mSubject && o == 0x14 && a == 0x143CA;
        }
        bool mGhost = true;
        bool isGhost(Subject s) const override { return s == mSubject && mGhost; }
        float scriptVariable(std::uint32_t o, const std::string& n) const override
        {
            return o == 0x1B0 && n == "::MyScript.MyInt" ? 7.f : 0.f;
        }
        float sitState(Subject s) const override { return s == mSubject ? 13.f : 0.f; }
        bool isReference(Subject s, std::uint32_t r) const override { return s == mSubject && r == 0x1A0; }
        bool isAlarmed(Subject s) const override { return s == mSubject && mAlarmed; }
        bool inContainer(Subject s, std::uint32_t c) const override { return s == mSubject && c == 0x190; }
        bool mBleeding = true;
        bool isBleedingOut(Subject s) const override { return s == mSubject && mBleeding; }
        bool mDrawn = true;
        bool weaponOrMagicOut(Subject s) const override { return s == mSubject && mDrawn; }
        float levelOf(Subject s) const override { return s == mSubject ? 12.f : 0.f; }
        bool hasMagicEffect(Subject s, std::uint32_t e) const override { return s == mSubject && e == 0xB729D; }
        bool scenePlaying(std::uint32_t sc) const override { return sc == 0x180; }
        float trespassWarnings(Subject s) const override { return s == mSubject ? 2.f : 0.f; }
        bool knowsSpell(Subject s, std::uint32_t sp) const override { return s == mSubject && sp == 0x170; }
        float sleepState(Subject s) const override { return s == mSubject ? 3.f : 0.f; }
        float crimeGold(Subject s, std::uint32_t f) const override
        {
            return f == 0xA0 ? 40.f : (f == 0 && s == mSubject ? 15.f : 0.f);
        }
        bool mIgnoring = false;
        bool inCombat(Subject s, bool notIgnoring) const override
        {
            return s == mSubject && !(notIgnoring && mIgnoring);
        }
        float attackDesire(Subject s, std::uint32_t t) const override { return s == mSubject && t == 0x14 ? 2.f : 0.f; }
        float actorValuePercent(Subject s, std::uint32_t av) const override
        {
            return s == mSubject && av == 24 ? 0.45f : 1.f;
        }
        bool mIntimidate = true;
        bool mBribe = true;
        bool playerCanBribe(Subject s) const override { return s == mSubject && mBribe; }
        bool playerCanIntimidate(Subject s) const override { return s == mSubject && mIntimidate; }
        bool mTalked = true;
        bool talkedToPlayer(Subject s) const override { return s == mSubject && mTalked; }
        float baseActorValue(Subject s, std::uint32_t av) const override
        {
            return s == mSubject && av == 24 ? 100.f : 0.f;
        }
        bool locAliasIs(std::uint32_t a, std::uint32_t l) const override { return a == 5 && l == 0x150; }
        bool inLocationAlias(Subject s, std::uint32_t a) const override { return s == mSubject && a == 6; }
        bool isPlayerTeammate(Subject s) const override { return s == mSubject; }
        bool spellHasKeyword(Subject s, std::uint32_t src, std::uint32_t k) const override
        {
            return s == mSubject && src == 0 && k == 0x160;
        }
        float goldOf(Subject s) const override { return s == mSubject ? 250.f : 0.f; }
        bool mTrespassing = true;
        bool isTrespassing(Subject s) const override { return s == mSubject && mTrespassing; }
        Subject mVictim = 7;
        Subject crimeVictim() const override { return mVictim; }
        bool inInterior(Subject s) const override { return s == mSubject && mInterior; }
        bool isCurrentWeather(std::uint32_t x) const override { return x == 0x101; }
        bool offersServicesNow(Subject s) const override { return s == mSubject && mServices; }
        bool runsPackage(Subject s, std::uint32_t p) const override { return s == mSubject && p == 0xF0; }
        float deadCount(std::uint32_t b) const override { return b == 0xF8 ? 6.f : 0.f; }
        float distanceTo(Subject s, std::uint32_t r) const override
        {
            return s == mSubject && r == 0xE0 ? 512.f : 9000.f;
        }
        bool isEquipped(Subject s, std::uint32_t o) const override { return s == mSubject && o == mEquipped; }
        bool inWorldspace(Subject s, std::uint32_t w) const override { return s == mSubject && w == mWorldspace; }
        bool isRace(Subject s, std::uint32_t r) const override
        {
            return (s == mSubject && r == mRace) || (s == mPlayer && r == mPlayerRace);
        }
        std::uint32_t mPlayerRace = 0xB2;
        bool isDisabled(Subject s) const override { return s == mSubject && mDisabled; }
        bool isSneaking(Subject s) const override { return s == mSubject && mSneaking; }
        bool questRunning(std::uint32_t q) const override { return q == mQuest && mRunning; }
    };

    bool run(const Ctda& c, const World& w, Evaluator* out = nullptr)
    {
        Evaluator e;
        const Ctda list[] = { c };
        const bool r = e.evaluate(list, w);
        if (out)
            *out = e;
        return r;
    }

    TEST(SkyrimConditionFunctions, fn_566_asks_whether_the_subject_fills_the_alias)
    {
        World w;
        EXPECT_TRUE(run(cond(566, kEq, 1.f, 3), w));
        EXPECT_FALSE(run(cond(566, kEq, 1.f, 4), w));
        EXPECT_TRUE(run(cond(566, kEq, 0.f, 4), w));
    }

    TEST(SkyrimConditionFunctions, fn_426_asks_the_voice_type_or_list)
    {
        World w;
        EXPECT_TRUE(run(cond(426, kEq, 1.f, 0x40), w));
        EXPECT_FALSE(run(cond(426, kEq, 1.f, 0x41), w));
    }

    TEST(SkyrimConditionFunctions, fn_359_asks_the_current_location)
    {
        World w;
        EXPECT_TRUE(run(cond(359, kEq, 1.f, 0x50), w));
        EXPECT_FALSE(run(cond(359, kEq, 1.f, 0x51), w));
    }

    TEST(SkyrimConditionFunctions, fn_77_compares_the_number_the_world_draws)
    {
        World w;
        EXPECT_TRUE(run(cond(77, kGe, 40.f), w));
        w.mRandom = 10.f;
        EXPECT_FALSE(run(cond(77, kGe, 40.f), w));
    }

    // fn_606 GetKeywordDataForLocation(location, keyword): the keyword's data value on that location, 0 when unset
    // (toddhow, 2026-11-11). It names its location, so it needs no subject.
    TEST(SkyrimConditionFunctions, fn_606_is_the_keyword_data_of_the_named_location)
    {
        World w;
        EXPECT_TRUE(run(cond(606, kEq, 2.5f, 0x50, 0x80), w));
        EXPECT_TRUE(run(cond(606, kEq, 0.f, 0x51, 0x80), w));
        EXPECT_TRUE(run(cond(606, kEq, 0.f, 0x50, 0x81), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(606, kEq, 2.5f, 0x50, 0x80), w));
    }

    // fn_403 GetRelationshipRank(target): the subject's relationship rank with the target actor, -4 to 4, 0 neutral
    // (toddhow, 2026-11-11). Nobody as subject is neutral, without asking the world.
    TEST(SkyrimConditionFunctions, fn_403_is_the_relationship_rank_with_the_target)
    {
        World w;
        EXPECT_TRUE(run(cond(403, kGe, 3.f, 0x14), w));
        EXPECT_TRUE(run(cond(403, kEq, 0.f, 0x15), w));
        w.mRank403 = -2.f;
        EXPECT_FALSE(run(cond(403, kGe, 0.f, 0x14), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(403, kEq, 0.f, 0x14), w));
    }

    // fn_69 GetIsRace(race), fn_35 GetDisabled, fn_286 IsSneaking, fn_56 GetQuestRunning(quest): meanings from
    // toddhow's descriptions (2026-11-11). Nobody as subject is false, without asking the world.
    TEST(SkyrimConditionFunctions, fn_69_35_286_56_race_disabled_sneaking_quest_running)
    {
        World w;
        EXPECT_TRUE(run(cond(69, kEq, 1.f, 0xB0), w));
        EXPECT_TRUE(run(cond(69, kEq, 0.f, 0xB1), w));
        EXPECT_TRUE(run(cond(35, kEq, 1.f), w));
        EXPECT_TRUE(run(cond(286, kEq, 1.f), w));
        EXPECT_TRUE(run(cond(56, kEq, 1.f, 9), w));
        EXPECT_TRUE(run(cond(56, kEq, 0.f, 10), w));
        w.mDisabled = false;
        w.mSneaking = false;
        w.mRunning = false;
        EXPECT_TRUE(run(cond(35, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(286, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(56, kEq, 0.f, 9), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(69, kEq, 0.f, 0xB0), w));
    }

    // fn_310 GetInWorldspace(worldspace): 1 in an exterior cell of that worldspace, 0 in an interior (CK wiki page
    // shown by toddhow, 2026-11-11; the world decides "interior"). Nobody as subject is false.
    TEST(SkyrimConditionFunctions, fn_310_is_in_an_exterior_of_the_worldspace)
    {
        World w;
        EXPECT_TRUE(run(cond(310, kEq, 1.f, 0xC0), w));
        EXPECT_TRUE(run(cond(310, kEq, 0.f, 0xC1), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(310, kEq, 0.f, 0xC0), w));
    }

    // fn_130 GetPCIsRace(race): GetIsRace of the player, whatever the condition runs on; fn_182 GetEquipped(object)
    // (CK wiki pages shown by toddhow, 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_130_is_the_player_race_and_182_is_equipped)
    {
        World w;
        EXPECT_TRUE(run(cond(130, kEq, 1.f, 0xB2), w));
        EXPECT_TRUE(run(cond(130, kEq, 0.f, 0xB0), w));
        EXPECT_TRUE(run(cond(182, kEq, 1.f, 0xD0), w));
        EXPECT_TRUE(run(cond(182, kEq, 0.f, 0xD1), w));
        w.mPlayer = 0;
        EXPECT_TRUE(run(cond(130, kEq, 0.f, 0xB2), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(182, kEq, 0.f, 0xD0), w));
    }

    // fn_1 GetDistance(reference): units from the subject to the reference (CK wiki, toddhow 2026-11-11). Nobody as
    // subject is infinitely far, so a "closer than" test fails.
    TEST(SkyrimConditionFunctions, fn_1_is_the_distance_to_the_reference)
    {
        World w;
        EXPECT_TRUE(run(cond(1, kEq, 512.f, 0xE0), w));
        EXPECT_TRUE(run(cond(1, kGe, 9000.f, 0xE1), w));
        w.mSubject = 0;
        EXPECT_FALSE(run(cond(1, kLt, 1.0e30f, 0xE0), w));
    }

    // fn_161 GetIsCurrentPackage(package) and fn_84 GetDeadCount(actor base) (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_161_is_the_current_package_and_84_the_dead_count)
    {
        World w;
        EXPECT_TRUE(run(cond(161, kEq, 1.f, 0xF0), w));
        EXPECT_TRUE(run(cond(161, kEq, 0.f, 0xF1), w));
        EXPECT_TRUE(run(cond(84, kEq, 6.f, 0xF8), w));
        EXPECT_TRUE(run(cond(84, kEq, 0.f, 0xF9), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(161, kEq, 0.f, 0xF0), w));
        EXPECT_TRUE(run(cond(84, kEq, 6.f, 0xF8), w));
    }

    // fn_255 GetOffersServicesNow: the subject's current package offers services (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_255_is_offering_services_now)
    {
        World w;
        EXPECT_TRUE(run(cond(255, kEq, 1.f), w));
        w.mServices = false;
        EXPECT_TRUE(run(cond(255, kEq, 0.f), w));
        w.mServices = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(255, kEq, 0.f), w));
    }

    // fn_149 GetIsCurrentWeather(weather): true from the start of a transition into it (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_149_is_the_current_weather)
    {
        World w;
        EXPECT_TRUE(run(cond(149, kEq, 1.f, 0x101), w));
        EXPECT_TRUE(run(cond(149, kEq, 0.f, 0x102), w));
    }

    // fn_300 IsInInterior: the subject is in an interior cell (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_300_is_in_an_interior)
    {
        World w;
        EXPECT_TRUE(run(cond(300, kEq, 1.f), w));
        w.mInterior = false;
        EXPECT_TRUE(run(cond(300, kEq, 0.f), w));
        w.mInterior = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(300, kEq, 0.f), w));
    }

    // fn_314 IsActorAVictim: the subject is the current crime victim, one actor at most (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_314_is_the_current_crime_victim)
    {
        World w;
        EXPECT_TRUE(run(cond(314, kEq, 1.f), w));
        w.mVictim = 8;
        EXPECT_TRUE(run(cond(314, kEq, 0.f), w));
        w.mVictim = 0;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(314, kEq, 0.f), w));
    }

    // fn_145 IsTrespassing: the subject is trespassing (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_145_is_trespassing)
    {
        World w;
        EXPECT_TRUE(run(cond(145, kEq, 1.f), w));
        w.mTrespassing = false;
        EXPECT_TRUE(run(cond(145, kEq, 0.f), w));
        w.mTrespassing = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(145, kEq, 0.f), w));
    }

    // fn_48 GetGold: the subject's gold (CK wiki, toddhow 2026-11-11). Nobody has none.
    TEST(SkyrimConditionFunctions, fn_48_is_the_gold)
    {
        World w;
        EXPECT_TRUE(run(cond(48, kEq, 250.f), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(48, kEq, 0.f), w));
    }

    // fn_605 LocAliasIsLocation(alias, location), fn_360 GetInCurrentLocAlias(alias), fn_453 GetPlayerTeammate,
    // fn_596 SpellHasKeyword(casting source, keyword) (toddhow's descriptions, 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_605_360_453_596_location_aliases_teammate_spell_keyword)
    {
        World w;
        EXPECT_TRUE(run(cond(605, kEq, 1.f, 5, 0x150), w));
        EXPECT_TRUE(run(cond(605, kEq, 0.f, 5, 0x151), w));
        EXPECT_TRUE(run(cond(360, kEq, 1.f, 6), w));
        EXPECT_TRUE(run(cond(360, kEq, 0.f, 7), w));
        EXPECT_TRUE(run(cond(453, kEq, 1.f), w));
        EXPECT_TRUE(run(cond(596, kEq, 1.f, 0, 0x160), w));
        EXPECT_TRUE(run(cond(596, kEq, 0.f, 1, 0x160), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(605, kEq, 1.f, 5, 0x150), w));
        EXPECT_TRUE(run(cond(360, kEq, 0.f, 6), w));
        EXPECT_TRUE(run(cond(453, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(596, kEq, 0.f, 0, 0x160), w));
    }

    // fn_277 GetBaseActorValue(actor value): the base value, not the current one (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_277_is_the_base_actor_value)
    {
        World w;
        EXPECT_TRUE(run(cond(277, kEq, 100.f, 24), w));
        EXPECT_TRUE(run(cond(277, kEq, 0.f, 25), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(277, kEq, 0.f, 24), w));
    }

    // fn_50 GetTalkedToPC: the subject has ever spoken to the player (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_50_has_talked_to_the_player)
    {
        World w;
        EXPECT_TRUE(run(cond(50, kEq, 1.f), w));
        w.mTalked = false;
        EXPECT_TRUE(run(cond(50, kEq, 0.f), w));
        w.mTalked = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(50, kEq, 0.f), w));
    }

    // fn_655 GetIntimidateSuccess: the player can intimidate the subject (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_655_is_intimidation_success)
    {
        World w;
        EXPECT_TRUE(run(cond(655, kEq, 1.f), w));
        w.mIntimidate = false;
        EXPECT_TRUE(run(cond(655, kEq, 0.f), w));
        w.mIntimidate = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(655, kEq, 0.f), w));
    }

    // fn_66 GetShouldAttack(target): the subject's desire to attack, above 0 means it will; fn_640
    // GetActorValuePercent(actor value): current over maximum, 0 to 1 (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_66_is_attack_desire_and_640_actor_value_percent)
    {
        World w;
        EXPECT_TRUE(run(cond(66, kEq, 2.f, 0x14), w));
        EXPECT_TRUE(run(cond(66, kEq, 0.f, 0x15), w));
        EXPECT_TRUE(run(cond(640, kEq, 0.45f, 24), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(66, kEq, 0.f, 0x14), w));
        EXPECT_TRUE(run(cond(640, kEq, 0.f, 24), w));
    }

    // fn_459 GetCrimeGold(faction, optional): the player's crime gold with the faction, or with the subject's crime
    // faction when none is named; fn_289 IsInCombat(flag): a non-zero flag makes an actor ignoring combat count as not
    // in combat (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_459_is_crime_gold_and_289_in_combat)
    {
        World w;
        EXPECT_TRUE(run(cond(459, kEq, 40.f, 0xA0), w));
        EXPECT_TRUE(run(cond(459, kEq, 15.f, 0), w));
        EXPECT_TRUE(run(cond(289, kEq, 1.f, 0), w));
        w.mIgnoring = true;
        EXPECT_TRUE(run(cond(289, kEq, 1.f, 0), w));
        EXPECT_TRUE(run(cond(289, kEq, 0.f, 1), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(459, kEq, 40.f, 0xA0), w));
        EXPECT_TRUE(run(cond(459, kEq, 0.f, 0), w));
        EXPECT_TRUE(run(cond(289, kEq, 0.f, 0), w));
    }

    // fn_49 GetSleeping: the sleep state 0 to 4 (CK wiki, toddhow 2026-11-11). Nobody is not sleeping.
    TEST(SkyrimConditionFunctions, fn_49_is_the_sleep_state)
    {
        World w;
        EXPECT_TRUE(run(cond(49, kEq, 3.f), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(49, kEq, 0.f), w));
    }

    // fn_264 HasSpell(spell or shout): the subject knows it (Papyrus Actor.HasSpell page, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_264_knows_the_spell)
    {
        World w;
        EXPECT_TRUE(run(cond(264, kEq, 1.f, 0x170), w));
        EXPECT_TRUE(run(cond(264, kEq, 0.f, 0x171), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(264, kEq, 0.f, 0x170), w));
    }

    // fn_654 GetBribeSuccess: no parameters, runs on the subject (CK condition dialog, toddhow 2026-11-11); meaning
    // inferred from GetIntimidateSuccess.
    TEST(SkyrimConditionFunctions, fn_654_is_bribe_success)
    {
        World w;
        EXPECT_TRUE(run(cond(654, kEq, 1.f), w));
        w.mBribe = false;
        EXPECT_TRUE(run(cond(654, kEq, 0.f), w));
        w.mBribe = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(654, kEq, 0.f), w));
    }

    // fn_144 GetTrespassWarningLevel: warnings the subject has given (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_144_is_the_warnings_given)
    {
        World w;
        EXPECT_TRUE(run(cond(144, kEq, 2.f), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(144, kEq, 0.f), w));
    }

    // fn_248 IsScenePlaying(scene) (CK wiki, toddhow 2026-11-11). Names its scene, so it needs no subject.
    TEST(SkyrimConditionFunctions, fn_248_is_the_scene_playing)
    {
        World w;
        EXPECT_TRUE(run(cond(248, kEq, 1.f, 0x180), w));
        EXPECT_TRUE(run(cond(248, kEq, 0.f, 0x181), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(248, kEq, 1.f, 0x180), w));
    }

    // fn_101 IsWeaponMagicOut, fn_80 GetLevel, fn_214 HasMagicEffect(effect) (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_101_weapon_out_80_level_214_magic_effect)
    {
        World w;
        EXPECT_TRUE(run(cond(101, kEq, 1.f), w));
        EXPECT_TRUE(run(cond(80, kEq, 12.f), w));
        EXPECT_TRUE(run(cond(214, kEq, 1.f, 0xB729D), w));
        EXPECT_TRUE(run(cond(214, kEq, 0.f, 0xB729E), w));
        w.mDrawn = false;
        EXPECT_TRUE(run(cond(101, kEq, 0.f), w));
        w.mDrawn = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(101, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(80, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(214, kEq, 0.f, 0xB729D), w));
    }

    // fn_580 IsBleedingOut (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_580_is_bleeding_out)
    {
        World w;
        EXPECT_TRUE(run(cond(580, kEq, 1.f), w));
        w.mBleeding = false;
        EXPECT_TRUE(run(cond(580, kEq, 0.f), w));
        w.mBleeding = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(580, kEq, 0.f), w));
    }

    // fn_624 GetInContainer(reference): one reference parameter, runs on the subject (CK condition dialog, toddhow
    // 2026-11-11); meaning inferred from the name.
    TEST(SkyrimConditionFunctions, fn_624_is_in_the_container)
    {
        World w;
        EXPECT_TRUE(run(cond(624, kEq, 1.f, 0x190), w));
        EXPECT_TRUE(run(cond(624, kEq, 0.f, 0x191), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(624, kEq, 0.f, 0x190), w));
    }

    // fn_61 GetAlarmed (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_61_is_alarmed)
    {
        World w;
        EXPECT_TRUE(run(cond(61, kEq, 1.f), w));
        w.mAlarmed = false;
        EXPECT_TRUE(run(cond(61, kEq, 0.f), w));
        w.mAlarmed = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(61, kEq, 0.f), w));
    }

    // fn_136 GetIsReference(reference): the subject is that placed reference, not a base form (CK wiki, toddhow
    // 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_136_is_the_reference)
    {
        World w;
        EXPECT_TRUE(run(cond(136, kEq, 1.f, 0x1A0), w));
        EXPECT_TRUE(run(cond(136, kEq, 0.f, 0x1A1), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(136, kEq, 0.f, 0x1A0), w));
    }

    // fn_159 GetSitting: the sitting state, 0 to 4 and 11 to 14 for horses (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_159_is_the_sitting_state)
    {
        World w;
        EXPECT_TRUE(run(cond(159, kEq, 13.f), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(159, kEq, 0.f), w));
    }

    // fn_237 GetIsGhost; fn_630 GetVMScriptVariable(object, "variable") (CK wiki, toddhow 2026-11-11; the
    // parameter shape is fn_629's, unchecked).
    TEST(SkyrimConditionFunctions, fn_237_is_ghost_and_630_script_variable)
    {
        World w;
        EXPECT_TRUE(run(cond(237, kEq, 1.f), w));
        Ctda v = cond(630, kEq, 7.f, 0x1B0);
        v.mString2 = "::MyScript.MyInt";
        EXPECT_TRUE(run(v, w));
        v.mString2 = "::MyScript.Other";
        EXPECT_FALSE(run(v, w));
        w.mGhost = false;
        EXPECT_TRUE(run(cond(237, kEq, 0.f), w));
        w.mGhost = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(237, kEq, 0.f), w));
    }

    // fn_258 HasAssociationType(actor, association type) (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_258_has_the_association)
    {
        World w;
        EXPECT_TRUE(run(cond(258, kEq, 1.f, 0x14, 0x143CA), w));
        EXPECT_TRUE(run(cond(258, kEq, 0.f, 0x14, 0x143CB), w));
        EXPECT_TRUE(run(cond(258, kEq, 0.f, 0x15, 0x143CA), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(258, kEq, 0.f, 0x14, 0x143CA), w));
    }

    // fn_635 IsInFavorState (CK wiki, toddhow 2026-11-11: doing something the player asked).
    TEST(SkyrimConditionFunctions, fn_635_isInFavorState)
    {
        World w;
        EXPECT_TRUE(run(cond(635, kEq, 1.f), w));
        w.mInFavorStateFlag = false;
        EXPECT_TRUE(run(cond(635, kEq, 0.f), w));
        w.mInFavorStateFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(635, kEq, 0.f), w));
    }

    // fn_18 GetCurrentTime: the game hour as a decimal, 7:45 pm = 19.75; no subject (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_18_is_the_game_hour)
    {
        World w;
        EXPECT_TRUE(run(cond(18, kEq, 19.75f), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(18, kEq, 19.75f), w));
    }

    // fn_610 LocAliasHasKeyword(location alias, keyword): parameters from the CK dialog toddhow showed (2026-11-11),
    // meaning inferred from the name and fn_605. Names its alias, so no subject.
    TEST(SkyrimConditionFunctions, fn_610_location_alias_has_keyword)
    {
        World w;
        EXPECT_TRUE(run(cond(610, kEq, 1.f, 5, 0x1C0), w));
        EXPECT_TRUE(run(cond(610, kEq, 0.f, 5, 0x1C1), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(610, kEq, 1.f, 5, 0x1C0), w));
    }

    // fn_288 GetFriendHit(target actor): friendly hits on the subject from the target (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_288_is_friendly_hits_from_the_target)
    {
        World w;
        EXPECT_TRUE(run(cond(288, kEq, 2.f, 0x14), w));
        EXPECT_TRUE(run(cond(288, kEq, 0.f, 0x15), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(288, kEq, 0.f, 0x14), w));
    }

    // fn_415 GetGroupMemberCount: same as GetCombatGroupMemberCount, actors in the subject's combat group, 0 out of
    // combat (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_415_is_the_combat_group_size)
    {
        World w;
        EXPECT_TRUE(run(cond(415, kEq, 3.f), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(415, kEq, 0.f), w));
    }

    // fn_594 GetIsFlying (CK wiki, toddhow 2026-11-11: flying, 0 once the landing animation ends).
    TEST(SkyrimConditionFunctions, fn_594_getIsFlying)
    {
        World w;
        EXPECT_TRUE(run(cond(594, kEq, 1.f), w));
        w.mIsFlyingFlag = false;
        EXPECT_TRUE(run(cond(594, kEq, 0.f), w));
        w.mIsFlyingFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(594, kEq, 0.f), w));
    }

    // fn_125 IsGuard (CK wiki, toddhow 2026-11-11: the NPC's class is marked as a Guard class).
    TEST(SkyrimConditionFunctions, fn_125_isGuard)
    {
        World w;
        EXPECT_TRUE(run(cond(125, kEq, 1.f), w));
        w.mIsGuardClassFlag = false;
        EXPECT_TRUE(run(cond(125, kEq, 0.f), w));
        w.mIsGuardClassFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(125, kEq, 0.f), w));
    }

    // fn_274 IsSmallBump (CK wiki, toddhow 2026-11-11: an actor walked into the subject; false if they ran in and
    // caused a stagger).
    TEST(SkyrimConditionFunctions, fn_274_isSmallBump)
    {
        World w;
        EXPECT_TRUE(run(cond(274, kEq, 1.f), w));
        w.mIsSmallBumpFlag = false;
        EXPECT_TRUE(run(cond(274, kEq, 0.f), w));
        w.mIsSmallBumpFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(274, kEq, 0.f), w));
    }

    // fn_223 IsSpellTarget(spell, enchantment, ingredient or potion): the subject is being affected by it (CK wiki,
    // toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_223_is_the_spell_target)
    {
        World w;
        EXPECT_TRUE(run(cond(223, kEq, 1.f, 0x1D0), w));
        EXPECT_TRUE(run(cond(223, kEq, 0.f, 0x1D1), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(223, kEq, 0.f, 0x1D0), w));
    }

    // fn_638 IsInFriendStateWithPlayer (CK wiki, toddhow 2026-11-11: an actor that is bribed by the player, intimidated
    // by the player, or whose relationship rank with the player is at least the iRelationshipFriendValue game setting).
    TEST(SkyrimConditionFunctions, fn_638_isInFriendStateWithPlayer)
    {
        World w;
        EXPECT_TRUE(run(cond(638, kEq, 1.f), w));
        w.mInFriendStateWithPlayerFlag = false;
        EXPECT_TRUE(run(cond(638, kEq, 0.f), w));
        w.mInFriendStateWithPlayerFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(638, kEq, 0.f), w));
    }

    // fn_25 IsMoving: requested move direction, 0 none, 1 forward, 2 back, 3 left, 4 right (CK wiki, toddhow
    // 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_25_is_the_move_direction)
    {
        World w;
        EXPECT_TRUE(run(cond(25, kEq, 3.f), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(25, kEq, 0.f), w));
    }

    // fn_449 GetFactionRelation(target actor): 0 neutral, 1 enemy, 2 ally, 3 friend; fn_79 GetQuestVariable is
    // deprecated and does not work, so it is 0 (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_449_is_the_faction_relation_and_79_is_zero)
    {
        World w;
        EXPECT_TRUE(run(cond(449, kEq, 3.f, 0x14), w));
        EXPECT_TRUE(run(cond(449, kEq, 0.f, 0x15), w));
        EXPECT_TRUE(run(cond(79, kEq, 0.f, 9, 1), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(449, kEq, 0.f, 0x14), w));
    }

    // fn_448 HasPerk(perk), fn_430 GetHealthPercentage (0 to 1), fn_491 GetMapMarkerVisible (0 to 2), fn_261
    // HasParentRelationship(actor): the subject is the parent (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_448_perk_430_health_491_map_marker_261_parent)
    {
        World w;
        EXPECT_TRUE(run(cond(448, kEq, 1.f, 0xC3680), w));
        EXPECT_TRUE(run(cond(448, kEq, 0.f, 0xC3681), w));
        EXPECT_TRUE(run(cond(430, kEq, 0.4f), w));
        EXPECT_TRUE(run(cond(491, kEq, 2.f), w));
        EXPECT_TRUE(run(cond(261, kEq, 1.f, 0x1E0), w));
        EXPECT_TRUE(run(cond(261, kEq, 0.f, 0x1E1), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(448, kEq, 0.f, 0xC3680), w));
        EXPECT_TRUE(run(cond(430, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(491, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(261, kEq, 0.f, 0x1E0), w));
    }

    // fn_432 GetIsObjectType(object class): the subject's base form is of that class (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_432_is_the_object_type)
    {
        World w;
        EXPECT_TRUE(run(cond(432, kEq, 1.f, 4), w));
        EXPECT_TRUE(run(cond(432, kEq, 0.f, 5), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(432, kEq, 0.f, 4), w));
    }

    // fn_497 CanPayCrimeGold(faction, optional), fn_513 IsCombatTarget(actor), fn_707
    // GetCombatTargetHasKeyword(keyword) (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_497_pay_crime_gold_513_combat_target_707_target_keyword)
    {
        World w;
        EXPECT_TRUE(run(cond(497, kEq, 1.f, 0), w));
        EXPECT_TRUE(run(cond(497, kEq, 0.f, 0xA1), w));
        EXPECT_TRUE(run(cond(513, kEq, 1.f, 0x14), w));
        EXPECT_TRUE(run(cond(513, kEq, 0.f, 0x15), w));
        EXPECT_TRUE(run(cond(707, kEq, 1.f, 0x80), w));
        EXPECT_TRUE(run(cond(707, kEq, 0.f, 0x81), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(497, kEq, 0.f, 0), w));
        EXPECT_TRUE(run(cond(513, kEq, 0.f, 0x14), w));
        EXPECT_TRUE(run(cond(707, kEq, 0.f, 0x80), w));
    }

    // fn_499 GetDaysInJail: days the player will serve, 0 unless in jail (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_499_is_the_days_in_jail)
    {
        World w;
        EXPECT_TRUE(run(cond(499, kEq, 4.f), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(499, kEq, 0.f), w));
    }

    // fn_698 IsAllowedToFly (CK wiki, toddhow 2026-11-11: false if not an actor, its race cannot fly, the Injured graph
    // variable is set, allow-flying is cleared, flight is blocked, or health is exactly 0).
    TEST(SkyrimConditionFunctions, fn_698_isAllowedToFly)
    {
        World w;
        EXPECT_TRUE(run(cond(698, kEq, 1.f), w));
        w.mIsAllowedToFlyFlag = false;
        EXPECT_TRUE(run(cond(698, kEq, 0.f), w));
        w.mIsAllowedToFlyFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(698, kEq, 0.f), w));
    }

    // fn_700 IsCommandedActor (CK wiki, toddhow 2026-11-11: raised as a zombie or summoned by a summoning spell).
    TEST(SkyrimConditionFunctions, fn_700_isCommandedActor)
    {
        World w;
        EXPECT_TRUE(run(cond(700, kEq, 1.f), w));
        w.mIsCommandedActorFlag = false;
        EXPECT_TRUE(run(cond(700, kEq, 0.f), w));
        w.mIsCommandedActorFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(700, kEq, 0.f), w));
    }

    // fn_402 IsBribedbyPlayer (CK wiki, toddhow 2026-11-11: bribed by the player; the original compares a date stamp
    // with the current date).
    TEST(SkyrimConditionFunctions, fn_402_isBribedbyPlayer)
    {
        World w;
        EXPECT_TRUE(run(cond(402, kEq, 1.f), w));
        w.mIsBribedByPlayerFlag = false;
        EXPECT_TRUE(run(cond(402, kEq, 0.f), w));
        w.mIsBribedByPlayerFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(402, kEq, 0.f), w));
    }

    // fn_32 GetInSameCell(reference) (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_32_is_in_the_same_cell)
    {
        World w;
        EXPECT_TRUE(run(cond(32, kEq, 1.f, 0x14), w));
        EXPECT_TRUE(run(cond(32, kEq, 0.f, 0x15), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(32, kEq, 0.f, 0x14), w));
    }

    // fn_375 GetCrimeGoldViolent(faction, optional), fn_132 GetPCInFaction(faction), fn_372 IsInList(form list),
    // fn_398 IsWeaponInList(form list), fn_444 GetInCurrentLocFormList(form list) (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_375_132_372_398_444_crime_faction_and_lists)
    {
        World w;
        EXPECT_TRUE(run(cond(375, kEq, 30.f, 0xA0), w));
        EXPECT_TRUE(run(cond(375, kEq, 10.f, 0), w));
        EXPECT_TRUE(run(cond(132, kEq, 0.f, 0xA0), w)); // the player (1) is in no faction in this world
        EXPECT_TRUE(run(cond(372, kEq, 1.f, 0x1F0), w));
        EXPECT_TRUE(run(cond(398, kEq, 1.f, 0x1F1), w));
        EXPECT_TRUE(run(cond(444, kEq, 1.f, 0x1F2), w));
        EXPECT_TRUE(run(cond(372, kEq, 0.f, 0x1F1), w));
        w.mPlayer = w.mSubject;
        EXPECT_TRUE(run(cond(132, kEq, 1.f, 0xA0), w));
        w.mSubject = 0;
        w.mPlayer = 0;
        EXPECT_TRUE(run(cond(375, kEq, 30.f, 0xA0), w));
        EXPECT_TRUE(run(cond(375, kEq, 0.f, 0), w));
        EXPECT_TRUE(run(cond(132, kEq, 0.f, 0xA0), w));
        EXPECT_TRUE(run(cond(372, kEq, 0.f, 0x1F0), w));
        EXPECT_TRUE(run(cond(398, kEq, 0.f, 0x1F1), w));
        EXPECT_TRUE(run(cond(444, kEq, 0.f, 0x1F2), w));
    }

    // fn_590 IsInScene (CK condition dialog, toddhow 2026-11-11: no parameters, runs on the subject; meaning INFERRED
    // from the name: the subject is taking part in a scene).
    TEST(SkyrimConditionFunctions, fn_590_isInScene)
    {
        World w;
        EXPECT_TRUE(run(cond(590, kEq, 1.f), w));
        w.mIsInSceneFlag = false;
        EXPECT_TRUE(run(cond(590, kEq, 0.f), w));
        w.mIsInSceneFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(590, kEq, 0.f), w));
    }

    // fn_266 IsPleasant: 0 not pleasant, between 0 and 1 changing to pleasant, 1 pleasant; no subject (CK wiki,
    // toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_266_is_the_weather_pleasantness)
    {
        World w;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(266, kEq, 0.5f), w));
    }

    // fn_5 GetLocked (CK wiki, toddhow 2026-11-11: the object is locked).
    TEST(SkyrimConditionFunctions, fn_5_getLocked)
    {
        World w;
        EXPECT_TRUE(run(cond(5, kEq, 1.f), w));
        w.mIsLockedFlag = false;
        EXPECT_TRUE(run(cond(5, kEq, 0.f), w));
        w.mIsLockedFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(5, kEq, 0.f), w));
    }

    // fn_657 GetArrestingActor (CK condition dialog, toddhow 2026-11-11: no parameters, runs on the subject; meaning
    // INFERRED from the name: the subject is the actor making the arrest).
    TEST(SkyrimConditionFunctions, fn_657_getArrestingActor)
    {
        World w;
        EXPECT_TRUE(run(cond(657, kEq, 1.f), w));
        w.mIsArrestingActorFlag = false;
        EXPECT_TRUE(run(cond(657, kEq, 0.f), w));
        w.mIsArrestingActorFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(657, kEq, 0.f), w));
    }

    // fn_263 IsWeaponOut (0 to 2), fn_651 GetKeywordDataForCurrentLocation(keyword), fn_682 WornHasKeyword(keyword),
    // fn_75 IsSnowing (0 to 1, no subject) (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_263_weapon_out_651_location_data_682_worn_keyword_75_snow)
    {
        World w;
        EXPECT_TRUE(run(cond(263, kEq, 1.f), w));
        EXPECT_TRUE(run(cond(651, kEq, 10.f, 0x80), w));
        EXPECT_TRUE(run(cond(682, kEq, 1.f, 0x81), w));
        EXPECT_TRUE(run(cond(682, kEq, 0.f, 0x80), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(263, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(651, kEq, 0.f, 0x80), w));
        EXPECT_TRUE(run(cond(682, kEq, 0.f, 0x81), w));
        EXPECT_TRUE(run(cond(75, kEq, 0.25f), w));
    }

    // fn_376 GetCrimeGoldNonviolent(faction, optional), fn_45 GetDetected(target), fn_62 IsRaining (0 to 1, no
    // subject), fn_561 HasRefType(location ref type) (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_376_nonviolent_45_detected_62_rain_561_ref_type)
    {
        World w;
        EXPECT_TRUE(run(cond(376, kEq, 20.f, 0xA0), w));
        EXPECT_TRUE(run(cond(376, kEq, 5.f, 0), w));
        EXPECT_TRUE(run(cond(45, kEq, 1.f, 0x14), w));
        EXPECT_TRUE(run(cond(45, kEq, 0.f, 0x15), w));
        EXPECT_TRUE(run(cond(561, kEq, 1.f, 0x200), w));
        EXPECT_TRUE(run(cond(561, kEq, 0.f, 0x201), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(376, kEq, 20.f, 0xA0), w));
        EXPECT_TRUE(run(cond(376, kEq, 0.f, 0), w));
        EXPECT_TRUE(run(cond(45, kEq, 0.f, 0x14), w));
        EXPECT_TRUE(run(cond(62, kEq, 1.f), w));
        EXPECT_TRUE(run(cond(561, kEq, 0.f, 0x200), w));
    }

    // fn_365 IsChild (CK wiki, toddhow 2026-11-11: the actor is a child).
    TEST(SkyrimConditionFunctions, fn_365_isChild)
    {
        World w;
        EXPECT_TRUE(run(cond(365, kEq, 1.f), w));
        w.mIsChildFlag = false;
        EXPECT_TRUE(run(cond(365, kEq, 0.f), w));
        w.mIsChildFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(365, kEq, 0.f), w));
    }

    // fn_39 GetDisease (CK wiki, toddhow 2026-11-11: any of the actor's active magic effects comes from a spell of type
    // Disease).
    TEST(SkyrimConditionFunctions, fn_39_getDisease)
    {
        World w;
        EXPECT_TRUE(run(cond(39, kEq, 1.f), w));
        w.mHasDiseaseFlag = false;
        EXPECT_TRUE(run(cond(39, kEq, 0.f), w));
        w.mHasDiseaseFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(39, kEq, 0.f), w));
    }

    // fn_108 GetWeaponAnimType, fn_633 GetFlyingState, fn_41 GetClothingValue, fn_597 GetEquippedItemType(hand),
    // fn_408 IsKiller(actor), fn_592 GetRefTypeAliveCount(location, ref type) (CK wiki, toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, the_last_tail_of_functions)
    {
        World w;
        EXPECT_TRUE(run(cond(108, kEq, 108.f), w));
        EXPECT_TRUE(run(cond(633, kEq, 633.f), w));
        EXPECT_TRUE(run(cond(41, kEq, 41.f), w));
        EXPECT_TRUE(run(cond(597, kEq, 9.f, 1), w));
        EXPECT_TRUE(run(cond(597, kEq, 0.f, 0), w));
        EXPECT_TRUE(run(cond(408, kEq, 1.f, 0x14), w));
        EXPECT_TRUE(run(cond(408, kEq, 0.f, 0x15), w));
        EXPECT_TRUE(run(cond(592, kEq, 6.f, 0x50, 0x200), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(108, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(633, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(41, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(597, kEq, 0.f, 1), w));
        EXPECT_TRUE(run(cond(408, kEq, 0.f, 0x14), w));
        EXPECT_TRUE(run(cond(592, kEq, 6.f, 0x50, 0x200), w));
    }

    // fn_244 GetRestrained (CK wiki, toddhow 2026-11-11: the actor has been set to a restrained state).
    TEST(SkyrimConditionFunctions, fn_244_getRestrained)
    {
        World w;
        EXPECT_TRUE(run(cond(244, kEq, 1.f), w));
        w.mIsRestrainedFlag = false;
        EXPECT_TRUE(run(cond(244, kEq, 0.f), w));
        w.mIsRestrainedFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(244, kEq, 0.f), w));
    }

    // fn_555 HasLoaded3D (CK wiki, toddhow 2026-11-11: the reference has its 3D loaded).
    TEST(SkyrimConditionFunctions, fn_555_hasLoaded3D)
    {
        World w;
        EXPECT_TRUE(run(cond(555, kEq, 1.f), w));
        w.mHasLoaded3DFlag = false;
        EXPECT_TRUE(run(cond(555, kEq, 0.f), w));
        w.mHasLoaded3DFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(555, kEq, 0.f), w));
    }

    // fn_656 GetArrestedState (CK wiki, toddhow 2026-11-11: the actor is under arrest; for the player only while a
    // guard approaches).
    TEST(SkyrimConditionFunctions, fn_656_getArrestedState)
    {
        World w;
        EXPECT_TRUE(run(cond(656, kEq, 1.f), w));
        w.mIsUnderArrestFlag = false;
        EXPECT_TRUE(run(cond(656, kEq, 0.f), w));
        w.mIsUnderArrestFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(656, kEq, 0.f), w));
    }

    // fn_632 IsCasting (CK condition dialog, toddhow 2026-11-11: no parameters, runs on the subject; meaning INFERRED
    // from the name: the subject is casting).
    TEST(SkyrimConditionFunctions, fn_632_isCasting)
    {
        World w;
        EXPECT_TRUE(run(cond(632, kEq, 1.f), w));
        w.mIsCastingFlag = false;
        EXPECT_TRUE(run(cond(632, kEq, 0.f), w));
        w.mIsCastingFlag = true;
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(632, kEq, 0.f), w));
    }

    // fn_699 HasMagicEffectKeyword(keyword) (CK wiki), fn_250 GetLocationCleared(location) and fn_625
    // IsLocationLoaded(location) (CK dialog, meaning inferred) (toddhow 2026-11-11).
    TEST(SkyrimConditionFunctions, fn_699_effect_keyword_250_cleared_625_loaded)
    {
        World w;
        EXPECT_TRUE(run(cond(699, kEq, 1.f, 0x80), w));
        EXPECT_TRUE(run(cond(699, kEq, 0.f, 0x81), w));
        EXPECT_TRUE(run(cond(250, kEq, 1.f, 0x50), w));
        EXPECT_TRUE(run(cond(250, kEq, 0.f, 0x51), w));
        EXPECT_TRUE(run(cond(625, kEq, 1.f, 0x51), w));
        w.mSubject = 0;
        EXPECT_TRUE(run(cond(699, kEq, 0.f, 0x80), w));
        EXPECT_TRUE(run(cond(250, kEq, 1.f, 0x50), w));
        EXPECT_TRUE(run(cond(625, kEq, 1.f, 0x51), w));
    }

    TEST(SkyrimConditionFunctions, fn_47_is_the_item_count)
    {
        World w;
        EXPECT_TRUE(run(cond(47, kEq, 5.f, 0x70), w));
        EXPECT_TRUE(run(cond(47, kEq, 0.f, 0x71), w));
    }

    TEST(SkyrimConditionFunctions, fn_67_and_fn_14_and_fn_73)
    {
        World w;
        EXPECT_TRUE(run(cond(67, kEq, 1.f, 0x60), w));
        EXPECT_FALSE(run(cond(67, kEq, 1.f, 0x61), w));
        EXPECT_TRUE(run(cond(14, kEq, 33.f, 8), w));
        EXPECT_EQ(w.mAskedAv, 8u);
        EXPECT_TRUE(run(cond(73, kEq, 2.f, 0xA0), w));
        EXPECT_TRUE(run(cond(73, kEq, -1.f, 0xA1), w));
    }

    TEST(SkyrimConditionFunctions, sex_functions_take_zero_male_one_female)
    {
        World w;
        EXPECT_TRUE(run(cond(70, kEq, 1.f, 1), w));
        EXPECT_FALSE(run(cond(70, kEq, 1.f, 0), w));
        // GetPCIsSex asks about the player, whatever the subject is
        EXPECT_TRUE(run(cond(131, kEq, 1.f, 0), w));
        EXPECT_FALSE(run(cond(131, kEq, 1.f, 1), w));
    }

    TEST(SkyrimConditionFunctions, state_questions)
    {
        World w;
        EXPECT_TRUE(run(cond(46, kEq, 1.f), w));
        w.mDead = false;
        EXPECT_FALSE(run(cond(46, kEq, 1.f), w));
        EXPECT_TRUE(run(cond(249, kEq, 1.f), w));
        EXPECT_TRUE(run(cond(579, kEq, 1.f, 0x90), w));
        EXPECT_FALSE(run(cond(579, kEq, 1.f, 0x91), w));
        EXPECT_TRUE(run(cond(543, kEq, 1.f, 9), w));
        EXPECT_FALSE(run(cond(543, kEq, 1.f, 10), w));
        EXPECT_TRUE(run(cond(560, kEq, 1.f, 0x80), w));
        EXPECT_FALSE(run(cond(560, kEq, 1.f, 0x81), w));
        EXPECT_TRUE(run(cond(562, kEq, 1.f, 0x80), w));
    }

    TEST(SkyrimConditionFunctions, nobody_as_subject_gives_the_absent_value_without_asking_the_world)
    {
        World w;
        w.mSubject = 0; // resolves to nobody
        EXPECT_TRUE(run(cond(46, kEq, 0.f), w));
        EXPECT_TRUE(run(cond(47, kEq, 0.f, 0x70), w));
        EXPECT_TRUE(run(cond(73, kEq, -1.f, 0xA0), w));
        EXPECT_TRUE(run(cond(560, kEq, 0.f, 0x80), w));
    }

    TEST(SkyrimConditionFunctions, none_of_them_is_unknown_any_more)
    {
        for (std::uint16_t f : { 566, 426, 629, 359, 77, 47, 67, 14, 131, 46, 70, 249, 579, 543, 560, 73, 562 })
            EXPECT_NE(findFunction(f), nullptr) << f;
    }

    // fn_629: the variable name is the CIS2 string that follows the CTDA.
    TEST(SkyrimConditionFunctions, cis_strings_follow_their_ctda)
    {
        auto ctda = [](std::uint16_t fn) {
            Bytes b;
            b.u8(0).u8(0).u8(0).u8(0).f32(1.f).u16(fn).u16(0).u32(9).u32(0).u32(0).u32(0).u32(0);
            return b;
        };
        Bytes rec;
        rec.append(ContentTests::sub4("CTDA", ctda(629)));
        rec.append(ContentTests::sub4("CIS2", Bytes().u8('c').u8('o').u8('u').u8('n').u8('t').u8('e').u8('r').u8(0)));
        rec.append(ContentTests::sub4("CTDA", ctda(72)));
        std::vector<Ctda> out;
        ASSERT_TRUE(decodeConditions({ reinterpret_cast<const std::byte*>(rec.str().data()), rec.str().size() }, out));
        ASSERT_EQ(out.size(), 2u);
        EXPECT_EQ(out[0].mString2, "counter");
        EXPECT_TRUE(out[1].mString2.empty());
        EXPECT_TRUE(out[0].mString1.empty());
    }

    TEST(SkyrimConditionFunctions, fn_629_reads_the_quest_variable_named_by_cis2)
    {
        World w;
        Ctda c = cond(629, kEq, 4.f, 9);
        c.mString2 = "counter";
        EXPECT_TRUE(run(c, w));
        c.mString2 = "other";
        EXPECT_FALSE(run(c, w)); // the world answers -1 for a name it does not know
    }

    TEST(SkyrimConditionFunctions, the_subject_is_resolved_with_the_whole_condition_so_run_on_5_gets_its_alias)
    {
        World w;
        Ctda c = cond(46, kEq, 1.f, 0, 0, 0, 5, 0, 12);
        EXPECT_TRUE(run(c, w));
        EXPECT_EQ(w.mAskedRunOn, 5u);
        EXPECT_EQ(w.mAskedParam3, 12u);
    }

    TEST(SkyrimConditionFunctions, the_swap_flag_fails_the_list_closed_and_is_counted)
    {
        World w;
        Evaluator e;
        EXPECT_TRUE(run(cond(46, kEq, 1.f), w));
        EXPECT_FALSE(run(cond(46, kEq, 1.f, 0, 0, kSwap), w, &e));
        EXPECT_EQ(e.mConditionsSwapped, 1u);
        EXPECT_EQ(e.mListsFailedClosed, 1u);
    }

    // The world is asked once per condition; a world that only knows the first five functions (the older tests'
    // worlds) answers "no" to every new question rather than failing to compile.
    class OldWorld final : public ConditionWorld
    {
    public:
        Subject resolveRunOn(std::uint32_t, std::uint32_t) const override { return 5; }
        bool hasBaseId(Subject, std::uint32_t) const override { return false; }
        float questStage(std::uint32_t) const override { return 0.f; }
        bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
        bool inFaction(Subject, std::uint32_t) const override { return false; }
        float globalValue(std::uint32_t) const override { return 0.f; }
    };

    TEST(SkyrimConditionFunctions, an_old_world_answers_no_and_minus_one_by_default)
    {
        OldWorld w;
        Evaluator e;
        const Ctda list[] = { cond(46, kEq, 0.f), cond(73, kEq, -1.f, 1), cond(47, kEq, 0.f, 1) };
        EXPECT_TRUE(e.evaluate(list, w));
    }
}
