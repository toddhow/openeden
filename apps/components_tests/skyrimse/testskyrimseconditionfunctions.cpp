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
    using game_skyrimse::survey::Ctda;
    using ContentTests::Bytes;

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
        bool inFaction(Subject, std::uint32_t) const override { return false; }
        float globalValue(std::uint32_t) const override { return 0.f; }

        bool isAliasRef(Subject s, std::uint32_t a) const override { return s == mSubject && a == mAlias; }
        bool hasVoiceType(Subject s, std::uint32_t v) const override { return s == mSubject && v == mVoice; }
        float questVariable(std::uint32_t q, const std::string& n) const override
        {
            return q == mQuest && n == mVarName ? mVarValue : -1.f;
        }
        bool inLocation(Subject s, std::uint32_t l) const override { return s == mSubject && l == mLocation; }
        float randomPercent() const override { return mRandom; }
        float itemCount(Subject s, std::uint32_t i) const override { return s == mSubject && i == mItem ? mItems : 0.f; }
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
        float factionRank(Subject s, std::uint32_t f) const override { return s == mSubject && f == mFaction ? mRank : -1.f; }
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
        float attackDesire(Subject s, std::uint32_t t) const override { return s == mSubject && t == 0x14 ? 2.f : 0.f; }
        float actorValuePercent(Subject s, std::uint32_t av) const override { return s == mSubject && av == 24 ? 0.45f : 1.f; }
        bool mIntimidate = true;
        bool playerCanIntimidate(Subject s) const override { return s == mSubject && mIntimidate; }
        bool mTalked = true;
        bool talkedToPlayer(Subject s) const override { return s == mSubject && mTalked; }
        float baseActorValue(Subject s, std::uint32_t av) const override { return s == mSubject && av == 24 ? 100.f : 0.f; }
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
        float distanceTo(Subject s, std::uint32_t r) const override { return s == mSubject && r == 0xE0 ? 512.f : 9000.f; }
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
