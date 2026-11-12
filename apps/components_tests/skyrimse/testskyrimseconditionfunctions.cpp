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

    constexpr std::uint8_t kEq = 0, kGe = 3;
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
