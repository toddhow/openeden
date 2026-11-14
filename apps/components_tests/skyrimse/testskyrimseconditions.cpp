// Task 0122: the Skyrim SE conditions engine v1 on generated conditions and a fake world. No game data.
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

    constexpr std::uint8_t kEq = 0, kNe = 1, kGt = 2, kGe = 3, kLt = 4, kLe = 5;
    constexpr std::uint8_t kOr = 0x01, kUseGlobal = 0x04;

    Ctda cond(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0, std::uint32_t p2 = 0,
        std::uint8_t flags = 0, std::uint32_t runOn = 0, std::uint32_t reference = 0)
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
        return c;
    }

    // fn_77 style helper conditions on the five known functions.
    Ctda isId(std::uint32_t form, bool expected = true)
    {
        return cond(72, kEq, expected ? 1.f : 0.f, form);
    }

    class FakeWorld final : public ConditionWorld
    {
    public:
        Subject mSubject = 1;
        std::uint32_t mBaseId = 0x100;
        float mStage = 0;
        std::uint32_t mStageDoneFor = 0; // quest 7: stage 20 is done when this is 20
        std::uint32_t mFaction = 0x200;
        float mGlobal = 0;
        mutable std::uint32_t mLastRunOn = 99;
        mutable std::uint32_t mLastReference = 99;

        Subject resolveRunOn(std::uint32_t runOn, std::uint32_t reference) const override
        {
            mLastRunOn = runOn;
            mLastReference = reference;
            return mSubject;
        }
        bool hasBaseId(Subject s, std::uint32_t f) const override { return s == mSubject && f == mBaseId; }
        float questStage(std::uint32_t quest) const override { return quest == 7 ? mStage : 0.f; }
        bool questStageDone(std::uint32_t quest, std::uint32_t stage) const override
        {
            return quest == 7 && stage == mStageDoneFor;
        }
        bool inFaction(Subject s, std::uint32_t f) const override { return s == mSubject && f == mFaction; }
        float globalValue(std::uint32_t g) const override { return g == 5 ? mGlobal : 0.f; }
    };

    TEST(SkyrimConditionsDecode, reads_each_ctda_in_order_and_ignores_other_subrecords)
    {
        auto raw = [](std::uint16_t fn) {
            Bytes b;
            b.u8(0).u8(0).u8(0).u8(0).f32(1.f).u16(fn).u16(0).u32(0).u32(0).u32(0).u32(0).u32(0);
            return b;
        };
        Bytes rec;
        rec.append(ContentTests::sub4("EDID", Bytes().u8(1).u8(0)));
        rec.append(ContentTests::sub4("CTDA", raw(72)));
        rec.append(ContentTests::sub4("CIS1", Bytes().u8(65).u8(0)));
        rec.append(ContentTests::sub4("CTDA", raw(58)));
        std::vector<Ctda> out;
        ASSERT_TRUE(decodeConditions({ reinterpret_cast<const std::byte*>(rec.str().data()), rec.str().size() }, out));
        ASSERT_EQ(out.size(), 2u);
        EXPECT_EQ(out[0].mFunction, 72);
        EXPECT_EQ(out[1].mFunction, 58);
    }

    TEST(SkyrimConditionsDecode, no_conditions_is_an_empty_good_list)
    {
        std::vector<Ctda> out;
        EXPECT_TRUE(decodeConditions({}, out));
        EXPECT_TRUE(out.empty());
    }

    TEST(SkyrimConditionsDecode, a_ctda_of_the_wrong_size_is_malformed)
    {
        Bytes rec;
        rec.append(ContentTests::sub4("CTDA", Bytes().u32(1).u32(2)));
        std::vector<Ctda> out;
        EXPECT_FALSE(decodeConditions({ reinterpret_cast<const std::byte*>(rec.str().data()), rec.str().size() }, out));
    }

    TEST(SkyrimConditionsDecode, a_broken_subrecord_framing_is_malformed)
    {
        const std::byte junk[3] = { std::byte{ 'C' }, std::byte{ 'T' }, std::byte{ 'D' } };
        std::vector<Ctda> out;
        EXPECT_FALSE(decodeConditions(junk, out));
    }

    TEST(SkyrimConditionsCompare, the_six_operators)
    {
        bool valid = false;
        EXPECT_TRUE(compareValues(kEq, 2, 2, valid));
        EXPECT_TRUE(valid);
        EXPECT_FALSE(compareValues(kEq, 2, 3, valid));
        EXPECT_TRUE(compareValues(kNe, 2, 3, valid));
        EXPECT_FALSE(compareValues(kNe, 2, 2, valid));
        EXPECT_TRUE(compareValues(kGt, 3, 2, valid));
        EXPECT_FALSE(compareValues(kGt, 2, 2, valid));
        EXPECT_TRUE(compareValues(kGe, 2, 2, valid));
        EXPECT_FALSE(compareValues(kGe, 1, 2, valid));
        EXPECT_TRUE(compareValues(kLt, 1, 2, valid));
        EXPECT_FALSE(compareValues(kLt, 2, 2, valid));
        EXPECT_TRUE(compareValues(kLe, 2, 2, valid));
        EXPECT_FALSE(compareValues(kLe, 3, 2, valid));
        EXPECT_TRUE(valid);
    }

    TEST(SkyrimConditionsCompare, a_code_above_five_is_invalid_and_false)
    {
        bool valid = true;
        EXPECT_FALSE(compareValues(6, 1, 1, valid));
        EXPECT_FALSE(valid);
        valid = true;
        EXPECT_FALSE(compareValues(7, 1, 1, valid));
        EXPECT_FALSE(valid);
    }

    TEST(SkyrimConditionsFunctions, the_five_named_functions_are_known_and_four_indices_outside_the_table_are_not)
    {
        for (std::uint16_t fn : { 72, 58, 71, 59, 74 })
            EXPECT_NE(findFunction(fn), nullptr) << fn;
        for (std::uint16_t fn : { 9001, 9002, 9003, 9004, 0, 9999 })
            EXPECT_EQ(findFunction(fn), nullptr) << fn;
    }

    TEST(SkyrimConditionsFunctions, GetIsID_asks_the_subjects_base_id)
    {
        FakeWorld w;
        const auto f = findFunction(72);
        EXPECT_EQ(f(isId(0x100), 1, w), 1.f);
        EXPECT_EQ(f(isId(0x101), 1, w), 0.f);
    }

    TEST(SkyrimConditionsFunctions, GetIsID_and_GetInFaction_are_zero_without_a_subject)
    {
        FakeWorld w;
        w.mSubject = 0; // the fake would answer true for subject 0 with these ids
        EXPECT_EQ(findFunction(72)(isId(0x100), 0, w), 0.f);
        EXPECT_EQ(findFunction(71)(cond(71, kEq, 1, 0x200), 0, w), 0.f);
    }

    TEST(SkyrimConditionsFunctions, GetStage_is_the_quests_stage)
    {
        FakeWorld w;
        w.mStage = 40;
        EXPECT_EQ(findFunction(58)(cond(58, kEq, 40, 7), 1, w), 40.f);
        EXPECT_EQ(findFunction(58)(cond(58, kEq, 40, 8), 1, w), 0.f);
    }

    TEST(SkyrimConditionsFunctions, GetStageDone_uses_the_second_parameter_as_the_stage)
    {
        FakeWorld w;
        w.mStageDoneFor = 20;
        EXPECT_EQ(findFunction(59)(cond(59, kEq, 1, 7, 20), 1, w), 1.f);
        EXPECT_EQ(findFunction(59)(cond(59, kEq, 1, 7, 30), 1, w), 0.f);
    }

    TEST(SkyrimConditionsFunctions, GetInFaction_and_GetGlobalValue)
    {
        FakeWorld w;
        w.mGlobal = 3.5f;
        EXPECT_EQ(findFunction(71)(cond(71, kEq, 1, 0x200), 1, w), 1.f);
        EXPECT_EQ(findFunction(71)(cond(71, kEq, 1, 0x201), 1, w), 0.f);
        EXPECT_EQ(findFunction(74)(cond(74, kEq, 0, 5), 1, w), 3.5f);
    }

    TEST(SkyrimConditionsEvaluate, an_empty_list_passes)
    {
        FakeWorld w;
        Evaluator e;
        EXPECT_TRUE(e.evaluate({}, w));
        EXPECT_EQ(e.mLists, 1u);
        EXPECT_EQ(e.mListsFailedClosed, 0u);
    }

    TEST(SkyrimConditionsEvaluate, a_condition_compares_the_function_value_with_the_constant)
    {
        FakeWorld w;
        w.mStage = 40;
        Evaluator e;
        const Ctda ge[] = { cond(58, kGe, 30, 7) };
        const Ctda lt[] = { cond(58, kLt, 30, 7) };
        EXPECT_TRUE(e.evaluate(ge, w));
        EXPECT_FALSE(e.evaluate(lt, w));
        EXPECT_EQ(e.mConditions, 2u);
    }

    TEST(SkyrimConditionsEvaluate, the_use_global_flag_takes_the_comparison_from_a_global)
    {
        FakeWorld w;
        w.mStage = 40;
        w.mGlobal = 40;
        std::uint32_t globalForm = 5;
        float asFloat;
        std::memcpy(&asFloat, &globalForm, 4);
        Ctda c = cond(58, kEq, 0, 7, 0, kUseGlobal);
        c.mComparisonBits = globalForm;
        c.mComparison = asFloat;
        Evaluator e;
        const Ctda list[] = { c };
        EXPECT_TRUE(e.evaluate(list, w));
        w.mGlobal = 41;
        EXPECT_FALSE(e.evaluate(list, w));
    }

    TEST(SkyrimConditionsEvaluate, and_is_the_default_between_conditions)
    {
        FakeWorld w;
        Evaluator e;
        const Ctda bothTrue[] = { isId(0x100), isId(0x100) };
        const Ctda oneFalse[] = { isId(0x100), isId(0x999) };
        EXPECT_TRUE(e.evaluate(bothTrue, w));
        EXPECT_FALSE(e.evaluate(oneFalse, w));
    }

    TEST(SkyrimConditionsEvaluate, the_or_flag_joins_a_condition_with_the_next)
    {
        FakeWorld w;
        Evaluator e;
        Ctda t = isId(0x100);
        Ctda f = isId(0x999);
        t.mFlags = kOr;
        f.mFlags = kOr;
        const Ctda trueOrFalse[] = { t, isId(0x999) };
        const Ctda falseOrTrue[] = { f, isId(0x100) };
        const Ctda falseOrFalse[] = { f, isId(0x999) };
        EXPECT_TRUE(e.evaluate(trueOrFalse, w));
        EXPECT_TRUE(e.evaluate(falseOrTrue, w));
        EXPECT_FALSE(e.evaluate(falseOrFalse, w));
    }

    TEST(SkyrimConditionsEvaluate, or_binds_tighter_than_and)
    {
        // A OR B AND C with A true, B false, C false: (A or B) and C is false; A or (B and C) would be true.
        FakeWorld w;
        Evaluator e;
        Ctda a = isId(0x100);
        Ctda b = isId(0x999);
        a.mFlags = kOr;
        const Ctda list[] = { a, b, isId(0x999) };
        EXPECT_FALSE(e.evaluate(list, w));
        // and with C true the whole list is true
        const Ctda list2[] = { a, b, isId(0x100) };
        EXPECT_TRUE(e.evaluate(list2, w));
    }

    TEST(SkyrimConditionsEvaluate, a_last_condition_with_the_or_flag_ends_its_clause_at_the_list_end)
    {
        FakeWorld w;
        Evaluator e;
        Ctda t = isId(0x100);
        Ctda f = isId(0x999);
        t.mFlags = kOr;
        f.mFlags = kOr;
        const Ctda trueDangling[] = { t };
        const Ctda falseDangling[] = { f };
        EXPECT_TRUE(e.evaluate(trueDangling, w));
        EXPECT_FALSE(e.evaluate(falseDangling, w));
    }

    TEST(SkyrimConditionsEvaluate, the_run_on_and_reference_reach_the_world)
    {
        FakeWorld w;
        Evaluator e;
        const Ctda list[] = { cond(72, kEq, 1, 0x100, 0, 0, 5, 0xABCD) };
        EXPECT_TRUE(e.evaluate(list, w));
        EXPECT_EQ(w.mLastRunOn, 5u);
        EXPECT_EQ(w.mLastReference, 0xABCDu);
    }

    TEST(SkyrimConditionsEvaluate, an_unknown_function_fails_the_whole_list_closed_even_in_an_or)
    {
        FakeWorld w;
        Evaluator e;
        Ctda t = isId(0x100);
        t.mFlags = kOr;
        // known true OR unknown: an open policy would pass; closed does not.
        const Ctda list[] = { t, cond(9001, kEq, 1) };
        EXPECT_FALSE(e.evaluate(list, w));
        EXPECT_EQ(e.mListsFailedClosed, 1u);
        EXPECT_EQ(e.mConditionsUnknown, 1u);
        EXPECT_EQ(e.mUnknownFunctions.at(9001), 1u);
    }

    TEST(SkyrimConditionsEvaluate, every_unknown_condition_is_counted_and_logged_once_per_index)
    {
        FakeWorld w;
        Evaluator e;
        std::vector<std::string> log;
        e.setLog([&](const std::string& s) { log.push_back(s); });
        const Ctda list[] = { cond(9002, kEq, 1), cond(9002, kEq, 1), cond(9003, kEq, 1), isId(0x100) };
        EXPECT_FALSE(e.evaluate(list, w));
        EXPECT_FALSE(e.evaluate(list, w));
        EXPECT_EQ(e.mConditionsUnknown, 6u);
        EXPECT_EQ(e.mUnknownFunctions.at(9002), 4u);
        EXPECT_EQ(e.mUnknownFunctions.at(9003), 2u);
        EXPECT_EQ(e.mListsFailedClosed, 2u);
        ASSERT_EQ(log.size(), 2u);
        EXPECT_NE(log[0].find("fn_9002"), std::string::npos);
        EXPECT_NE(log[1].find("fn_9003"), std::string::npos);
    }

    TEST(SkyrimConditionsEvaluate, an_unknown_function_without_a_log_sink_is_fine)
    {
        FakeWorld w;
        Evaluator e;
        const Ctda list[] = { cond(9004, kEq, 1) };
        EXPECT_FALSE(e.evaluate(list, w));
        EXPECT_EQ(e.mUnknownFunctions.at(9004), 1u);
    }

    TEST(SkyrimConditionsEvaluate, a_compare_code_above_five_fails_the_list_closed_and_is_counted)
    {
        FakeWorld w;
        Evaluator e;
        const Ctda list[] = { cond(72, 6, 1, 0x100) };
        EXPECT_FALSE(e.evaluate(list, w));
        EXPECT_EQ(e.mConditionsBadCompare, 1u);
        EXPECT_EQ(e.mListsFailedClosed, 1u);
        EXPECT_EQ(e.mConditionsUnknown, 0u);
    }
}
