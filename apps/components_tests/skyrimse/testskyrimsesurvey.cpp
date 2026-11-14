// Task 0116: the Skyrim SE data survey (conditions, Papyrus events, NIF block types) on generated records. No game
// data.
#include <game_skyrimse/survey.hpp>
#include <game_skyrimse/survey_nif.hpp>
#include <game_skyrimse/survey_scripts.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace game_skyrimse::survey;
    using ContentTests::Bytes;

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    constexpr std::uint32_t cc(const char (&n)[5])
    {
        return Content::fourCC(n);
    }

    // A CTDA of the layout the survey decodes: type byte, 3 unused, comparison, function, 2 unused, p1, p2, run-on,
    // reference, p3.
    Bytes ctda(std::uint8_t type, float comparison, std::uint16_t function, std::uint32_t p1 = 0, std::uint32_t p2 = 0,
        std::uint32_t runOn = 0, std::uint32_t ref = 0, std::uint32_t p3 = 0xFFFFFFFFu)
    {
        Bytes b;
        b.u8(type).u8(0).u8(0).u8(0).f32(comparison).u16(function).u16(0).u32(p1).u32(p2).u32(runOn).u32(ref).u32(p3);
        return b;
    }

    Bytes record(std::initializer_list<Bytes> subs)
    {
        Bytes b;
        for (const Bytes& s : subs)
            b.append(s);
        return b;
    }

    Bytes sub(const char (&n)[5], const Bytes& d)
    {
        return ContentTests::sub4(n, d);
    }

    TEST(SkyrimSurveyCtda, decodes_every_field)
    {
        const Bytes b = ctda(0x41, 2.5f, 0x1234, 7, 9, 3, 0x01000800, 5);
        Ctda c;
        ASSERT_TRUE(decodeCtda(view(b), c));
        EXPECT_EQ(c.mTypeByte, 0x41);
        EXPECT_EQ(c.mCompare, 2);
        EXPECT_EQ(c.mFlags, 1);
        EXPECT_FLOAT_EQ(c.mComparison, 2.5f);
        EXPECT_EQ(c.mFunction, 0x1234);
        EXPECT_EQ(c.mParam1, 7u);
        EXPECT_EQ(c.mParam2, 9u);
        EXPECT_EQ(c.mRunOn, 3u);
        EXPECT_EQ(c.mReference, 0x01000800u);
        EXPECT_EQ(c.mParam3, 5u);
        EXPECT_FALSE(c.mPaddingNonZero);
    }

    TEST(SkyrimSurveyCtda, refuses_other_sizes)
    {
        Bytes b = ctda(0, 0, 1);
        Ctda c;
        EXPECT_FALSE(decodeCtda(view(Bytes().text(b.str().substr(0, 31))), c));
        EXPECT_FALSE(decodeCtda(view(Bytes().text(b.str() + "x")), c));
        EXPECT_FALSE(decodeCtda({}, c));
    }

    TEST(SkyrimSurveyCtda, notes_padding_garbage)
    {
        Bytes b = ctda(0, 0, 1);
        b.str()[2] = 7;
        Ctda c;
        ASSERT_TRUE(decodeCtda(view(b), c));
        EXPECT_TRUE(c.mPaddingNonZero);
        Bytes d = ctda(0, 0, 1);
        d.str()[11] = 1;
        ASSERT_TRUE(decodeCtda(view(d), c));
        EXPECT_TRUE(c.mPaddingNonZero);
    }

    TEST(SkyrimSurveyCtda, function_names_are_fn_index_only)
    {
        EXPECT_EQ(functionName(0), "fn_0");
        EXPECT_EQ(functionName(72), "fn_72");
        EXPECT_EQ(functionName(65535), "fn_65535");
    }

    TEST(SkyrimSurveyNames, upstream_names_for_known_indices_and_none_otherwise)
    {
        EXPECT_EQ(upstreamFunctionName(72), "GetIsID");
        EXPECT_EQ(upstreamFunctionName(58), "GetStage");
        EXPECT_EQ(upstreamFunctionName(310), "GetInWorldspace");
        EXPECT_TRUE(upstreamFunctionName(0).empty());
        EXPECT_TRUE(upstreamFunctionName(426).empty()); // not in the table
        EXPECT_TRUE(upstreamFunctionName(362).empty()); // above 330 the numbering is not trusted
        EXPECT_TRUE(upstreamFunctionName(65535).empty());
        EXPECT_EQ(describeFunction(72), "fn_72 (GetIsID?)");
        EXPECT_EQ(describeFunction(426), "fn_426");
    }

    TEST(SkyrimSurveyNames, report_function_lines_carry_the_name)
    {
        ConditionSurvey s;
        s.addRecord(cc("INFO"), view(record({ sub("CTDA", ctda(0, 0, 72)), sub("CTDA", ctda(0, 0, 426)) })));
        std::ostringstream out;
        writeConditionReport(s, out, 5);
        EXPECT_NE(out.str().find("function #1 fn_72 (GetIsID?) uses 1"), std::string::npos);
        EXPECT_NE(out.str().find("function #2 fn_426 uses 1"), std::string::npos);
    }

    TEST(SkyrimSurveySubrecords, walks_in_order_with_xxxx)
    {
        Bytes data = record({ sub("EDID", Bytes().zstring("a")), sub("CTDA", ctda(0, 0, 5)) });
        // XXXX: the next subrecord's size is 40, its own size field is 0
        data.fourCC("XXXX").u16(4).u32(40);
        data.fourCC("DATA").u16(0).text(std::string(40, 'z'));
        data.append(sub("ZZZZ", Bytes()));
        std::vector<std::pair<std::uint32_t, std::size_t>> seen;
        EXPECT_TRUE(
            forEachSubrecord(view(data), [&](const SubrecordView& s) { seen.emplace_back(s.mType, s.mData.size()); }));
        ASSERT_EQ(seen.size(), 4u);
        EXPECT_EQ(seen[0], std::make_pair(cc("EDID"), std::size_t(2)));
        EXPECT_EQ(seen[1], std::make_pair(cc("CTDA"), std::size_t(32)));
        EXPECT_EQ(seen[2], std::make_pair(cc("DATA"), std::size_t(40)));
        EXPECT_EQ(seen[3], std::make_pair(cc("ZZZZ"), std::size_t(0)));
    }

    TEST(SkyrimSurveySubrecords, a_size_past_the_end_is_malformed)
    {
        Bytes data = sub("EDID", Bytes().zstring("a"));
        data.fourCC("CTDA").u16(100).text("short");
        int n = 0;
        EXPECT_FALSE(forEachSubrecord(view(data), [&](const SubrecordView&) { ++n; }));
        EXPECT_EQ(n, 1);
        // a truncated header
        Bytes h;
        h.text("AB");
        EXPECT_FALSE(forEachSubrecord(view(h), [](const SubrecordView&) {}));
    }

    TEST(SkyrimSurveyCoverage, smallest_number_of_items_reaching_each_fraction)
    {
        const std::vector<double> f = { 0.5, 0.8, 0.9, 1.0 };
        // total 100: 50, 30, 10, 5, 5 -> 1 item for 50%, 2 for 80%, 3 for 90%, 5 for 100%
        const auto c = coverageCurve({ 5, 50, 10, 30, 5 }, f);
        ASSERT_EQ(c.size(), 4u);
        EXPECT_EQ(c[0].mItems, 1u);
        EXPECT_EQ(c[1].mItems, 2u);
        EXPECT_EQ(c[2].mItems, 3u);
        EXPECT_EQ(c[3].mItems, 5u);
        EXPECT_DOUBLE_EQ(c[1].mFraction, 0.8);
    }

    TEST(SkyrimSurveyCoverage, empty_and_zero_totals_give_zero_and_fractions_clamp)
    {
        const std::vector<double> f = { 0.5, 2.0, -1.0 };
        EXPECT_EQ(coverageCurve({}, f)[0].mItems, 0u);
        EXPECT_EQ(coverageCurve({ 0, 0 }, f)[0].mItems, 0u);
        const auto c = coverageCurve({ 3, 1 }, f);
        EXPECT_EQ(c[1].mItems, 2u);
        EXPECT_EQ(c[2].mItems, 0u);
    }

    TEST(SkyrimSurveyCoverage, standard_fractions_are_the_five_of_the_note)
    {
        const auto s = standardFractions();
        ASSERT_EQ(s.size(), 5u);
        EXPECT_DOUBLE_EQ(s[0], 0.5);
        EXPECT_DOUBLE_EQ(s[4], 0.99);
    }

    TEST(SkyrimSurveyConditions, counts_functions_by_record_type_and_orders_them)
    {
        ConditionSurvey s;
        s.addRecord(cc("INFO"),
            view(record({ sub("CTDA", ctda(0x00, 1.0f, 72)), sub("CTDA", ctda(0x20, 0.0f, 72)),
                sub("CTDA", ctda(0x61, 5.0f, 10, 0, 0, 1)) })));
        s.addRecord(cc("PACK"), view(record({ sub("CTDA", ctda(0x00, 1.0f, 72)) })));
        s.addRecord(cc("MISC"), view(record({ sub("EDID", Bytes().zstring("x")) })));
        EXPECT_EQ(s.mRecords, 3u);
        EXPECT_EQ(s.mConditions, 4u);
        EXPECT_EQ(s.mRecordsByType[cc("MISC")], 1u);
        EXPECT_EQ(s.mRecordsWithConditions[cc("INFO")], 1u);
        EXPECT_EQ(s.mRecordsWithConditions.count(cc("MISC")), 0u);
        EXPECT_EQ(s.mConditionsByRecordType[cc("INFO")], 3u);
        EXPECT_EQ(s.mFunctions[72].mUses, 3u);
        EXPECT_EQ(s.mFunctions[72].mByRecordType[cc("INFO")], 2u);
        EXPECT_EQ(s.mFunctions[72].mByRecordType[cc("PACK")], 1u);
        EXPECT_EQ(s.mFunctions[10].mRunOn[1], 1u);
        EXPECT_EQ(s.mFunctions[10].mCompare[3], 1u); // 0x61 >> 5
        EXPECT_EQ(s.mFunctions[10].mOrFlag, 1u);
        EXPECT_EQ(s.mCompare[0], 2u);
        EXPECT_EQ(s.mCompare[1], 1u);
        EXPECT_EQ(s.mRunOn[0], 3u);
        EXPECT_EQ(s.mCtdaSizes[32], 4u);
        const auto order = s.functionsByUses();
        ASSERT_EQ(order.size(), 2u);
        EXPECT_EQ(order[0], std::make_pair(std::uint16_t(72), std::uint64_t(3)));
        EXPECT_EQ(order[1], std::make_pair(std::uint16_t(10), std::uint64_t(1)));
        const auto cov = s.functionCoverage(standardFractions());
        ASSERT_EQ(cov.size(), 5u);
        EXPECT_EQ(cov[0].mItems, 1u); // 3 of 4 uses is already at 75 percent
        EXPECT_EQ(cov[2].mItems, 2u);
    }

    TEST(SkyrimSurveyConditions, ties_are_ordered_by_index)
    {
        ConditionSurvey s;
        s.addRecord(cc("QUST"), view(record({ sub("CTDA", ctda(0, 0, 9)), sub("CTDA", ctda(0, 0, 3)) })));
        const auto order = s.functionsByUses();
        ASSERT_EQ(order.size(), 2u);
        EXPECT_EQ(order[0].first, 3);
        EXPECT_EQ(order[1].first, 9);
    }

    TEST(SkyrimSurveyConditions, odd_sizes_and_other_subrecords_are_counted_not_decoded)
    {
        ConditionSurvey s;
        s.addRecord(cc("INFO"),
            view(record({ sub("CTDA", Bytes().text(std::string(28, '\0'))), sub("CIS1", Bytes().zstring("v")),
                sub("CIS2", Bytes().zstring("w")), sub("CTDT", Bytes().text(std::string(20, '\0'))),
                sub("CTDA", ctda(0, 0, 1)) })));
        EXPECT_EQ(s.mConditions, 1u);
        EXPECT_EQ(s.mConditionsOddSize, 1u);
        EXPECT_EQ(s.mCtdaSizes[28], 1u);
        EXPECT_EQ(s.mStringParams, 2u);
        EXPECT_EQ(s.mCtdtSubrecords, 1u);
        EXPECT_EQ(s.mRecordsWithConditions[cc("INFO")], 1u);
    }

    TEST(SkyrimSurveyConditions, padding_and_nonfinite_comparisons_are_counted)
    {
        ConditionSurvey s;
        Bytes padded = ctda(0, 0, 1);
        padded.str()[1] = 1;
        Bytes nan = ctda(0, std::nanf(""), 2);
        s.addRecord(cc("INFO"), view(record({ sub("CTDA", padded), sub("CTDA", nan), sub("CTDA", ctda(0, 1, 3)) })));
        EXPECT_EQ(s.mPaddingNonZero, 1u);
        EXPECT_EQ(s.mNonFiniteComparison, 1u);
        EXPECT_EQ(s.mConditions, 3u);
    }

    TEST(SkyrimSurveyConditions, a_malformed_record_keeps_what_came_before)
    {
        ConditionSurvey s;
        Bytes data = sub("CTDA", ctda(0, 0, 4));
        data.fourCC("CTDA").u16(500).text("xx");
        s.addRecord(cc("INFO"), view(data));
        EXPECT_EQ(s.mRecordsMalformed, 1u);
        EXPECT_EQ(s.mConditions, 1u);
        EXPECT_EQ(s.mRecords, 1u);
    }

    TEST(SkyrimSurveyConditions, parameter_kinds_come_from_the_lookup)
    {
        ConditionSurvey s;
        s.setFormTypeLookup([](std::uint32_t id) -> std::optional<std::uint32_t> {
            if (id == 0x00012345u)
                return Content::fourCC("QUST");
            return std::nullopt;
        });
        s.addRecord(cc("INFO"),
            view(record({ sub("CTDA", ctda(0, 0, 50, 0x00012345, 0)), sub("CTDA", ctda(0, 0, 50, 0x00012345, 7)),
                sub("CTDA", ctda(0, 0, 50, 0x3F800000, 70000, 2, 0x00012345)) })));
        const FunctionStats& f = s.mFunctions[50];
        EXPECT_EQ(f.mParam1.mResolved, 2u);
        EXPECT_EQ(f.mParam1.mResolvedType.at(cc("QUST")), 2u);
        EXPECT_EQ(f.mParam1.mOther, 1u);
        EXPECT_EQ(f.mParam2.mZero, 1u);
        EXPECT_EQ(f.mParam2.mSmall, 1u);
        EXPECT_EQ(f.mParam2.mOther, 1u);
        EXPECT_EQ(f.mParam3.mOther, 3u); // 0xFFFFFFFF each time, not a known form
        EXPECT_EQ(f.mReference.mResolved, 1u);
        EXPECT_EQ(f.mReference.mZero, 2u);
        EXPECT_EQ(f.mParam1.total(), 3u);
    }

    TEST(SkyrimSurveyConditions, without_a_lookup_nothing_is_resolved)
    {
        ConditionSurvey s;
        s.addRecord(cc("INFO"), view(record({ sub("CTDA", ctda(0, 0, 50, 0x00012345, 5)) })));
        EXPECT_EQ(s.mFunctions[50].mParam1.mResolved, 0u);
        EXPECT_EQ(s.mFunctions[50].mParam1.mOther, 1u);
        EXPECT_EQ(s.mFunctions[50].mParam2.mSmall, 1u);
    }

    TEST(SkyrimSurveyConditions, use_global_flag_is_counted)
    {
        ConditionSurvey s;
        s.addRecord(cc("INFO"), view(record({ sub("CTDA", ctda(0x04, 0, 8)), sub("CTDA", ctda(0x00, 0, 8)) })));
        EXPECT_EQ(s.mFunctions[8].mUseGlobal, 1u);
        EXPECT_EQ(s.mFlags[4], 1u);
        EXPECT_EQ(s.mFlags[0], 1u);
    }

    TEST(SkyrimSurveyConditions, report_lines_are_aggregates_with_fn_names)
    {
        ConditionSurvey s;
        s.addRecord(cc("INFO"),
            view(record({ sub("CTDA", ctda(0, 0, 72)), sub("CTDA", ctda(0, 0, 72)), sub("CTDA", ctda(0, 0, 5)) })));
        std::ostringstream out;
        writeConditionReport(s, out, 1);
        const std::string text = out.str();
        EXPECT_NE(text.find("SKYRIMSE-SURVEY conditions: "), std::string::npos);
        EXPECT_NE(text.find("fn_72"), std::string::npos);
        EXPECT_EQ(text.find("function #2 "), std::string::npos); // topFunctions = 1
        EXPECT_NE(text.find("function #1 fn_72 "), std::string::npos);
        EXPECT_NE(text.find("record type INFO functions: distinct 2"), std::string::npos);
        EXPECT_NE(text.find("INFO"), std::string::npos);
        EXPECT_EQ(fourCCText(cc("INFO")), "INFO");
        // every line starts with the tag
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
            EXPECT_EQ(line.rfind("SKYRIMSE-SURVEY conditions:", 0), 0u) << line;
    }

    TEST(SkyrimSurveyConditions, small_values_are_kept_and_padding_is_split_by_place)
    {
        ConditionSurvey s;
        Bytes a = ctda(0, 0, 9, 0, 0, 2, 20);
        a.str()[1] = 1; // after the type byte
        Bytes b = ctda(0, 0, 9, 0, 0, 2, 20);
        b.str()[10] = 1; // after the function index
        s.addRecord(
            cc("INFO"), view(record({ sub("CTDA", a), sub("CTDA", b), sub("CTDA", ctda(0, 0, 9, 0, 0, 2, 21)) })));
        EXPECT_EQ(s.mPaddingNonZero, 2u);
        EXPECT_EQ(s.mPaddingAfterTypeNonZero, 1u);
        EXPECT_EQ(s.mPaddingAfterFunctionNonZero, 1u);
        const ParamStats& r = s.mFunctions[9].mReference;
        EXPECT_EQ(r.mSmallValues.at(20), 2u);
        EXPECT_EQ(r.mSmallValues.at(21), 1u);
    }

    TEST(SkyrimSurveyConditions, distinct_small_values_are_capped)
    {
        ConditionSurvey s;
        Bytes all;
        for (std::uint32_t v = 1; v <= 200; ++v)
            all.append(sub("CTDA", ctda(0, 0, 9, v)));
        s.addRecord(cc("INFO"), view(all));
        const ParamStats& p = s.mFunctions[9].mParam1;
        EXPECT_EQ(p.mSmall, 200u);
        EXPECT_LE(p.mSmallValues.size(), 64u);
    }

    // ---- events ----

    using ScriptPapyrus::Pex::File;
    using ScriptPapyrus::Pex::Function;
    using ScriptPapyrus::Pex::Object;
    using ScriptPapyrus::Pex::State;

    std::uint16_t str(File& f, const std::string& s)
    {
        f.mStrings.push_back(s);
        return static_cast<std::uint16_t>(f.mStrings.size() - 1);
    }

    Function fn(File& f, const std::string& name, bool native = false)
    {
        Function x;
        x.mName = str(f, name);
        x.mFlags = native ? Function::kNative : 0;
        return x;
    }

    TEST(SkyrimSurveyEvents, counts_handlers_scripts_states_and_declared_natives)
    {
        File base;
        {
            Object o;
            o.mName = str(base, "ObjectReference");
            State s;
            s.mName = str(base, "");
            s.mFunctions.push_back(fn(base, "OnActivate", true));
            s.mFunctions.push_back(fn(base, "OnInit", true));
            s.mFunctions.push_back(fn(base, "GetPositionX", true));
            o.mStates.push_back(s);
            base.mObjects.push_back(o);
        }
        File a;
        {
            Object o;
            o.mName = str(a, "MyScript");
            State s;
            s.mName = str(a, "");
            s.mFunctions.push_back(fn(a, "OnActivate"));
            s.mFunctions.push_back(fn(a, "OnInit"));
            s.mFunctions.push_back(fn(a, "OnCustomThing"));
            s.mFunctions.push_back(fn(a, "Online")); // not an event: lower case after On
            s.mFunctions.push_back(fn(a, "DoIt"));
            State busy;
            busy.mName = str(a, "busy");
            busy.mFunctions.push_back(fn(a, "OnActivate"));
            o.mStates.push_back(s);
            o.mStates.push_back(busy);
            a.mObjects.push_back(o);
        }
        File b;
        {
            Object o;
            o.mName = str(b, "Other");
            State s;
            s.mName = str(b, "");
            s.mFunctions.push_back(fn(b, "OnInit"));
            o.mStates.push_back(s);
            b.mObjects.push_back(o);
        }
        EventSurvey s;
        s.addScript(base);
        s.addScript(a);
        s.addScript(b);
        EXPECT_EQ(s.mObjects, 3u);
        EXPECT_EQ(s.mObjectsWithHandlers, 2u);
        EXPECT_EQ(s.mNativeFunctions, 3u);
        EXPECT_EQ(s.declaredNativeEventCount(), 2u);
        const auto rows = s.events();
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(rows[0].mName, "oninit");
        EXPECT_EQ(rows[0].mScripts, 2u);
        EXPECT_EQ(rows[0].mHandlers, 2u);
        EXPECT_TRUE(rows[0].mDeclaredNative);
        EXPECT_EQ(rows[1].mName, "onactivate");
        EXPECT_EQ(rows[1].mScripts, 1u);
        EXPECT_EQ(rows[1].mHandlers, 2u);
        EXPECT_EQ(rows[1].mInStates, 1u);
        EXPECT_TRUE(rows[1].mDeclaredNative);
        EXPECT_EQ(rows[2].mName, "oncustomthing");
        EXPECT_FALSE(rows[2].mDeclaredNative);
    }

    TEST(SkyrimSurveyEvents, declaration_order_does_not_matter)
    {
        File a;
        Object o;
        o.mName = str(a, "S");
        State s;
        s.mName = str(a, "");
        s.mFunctions.push_back(fn(a, "OnHit"));
        o.mStates.push_back(s);
        a.mObjects.push_back(o);
        File base;
        Object p;
        p.mName = str(base, "Actor");
        State t;
        t.mName = str(base, "");
        t.mFunctions.push_back(fn(base, "OnHit", true));
        p.mStates.push_back(t);
        base.mObjects.push_back(p);
        EventSurvey s1;
        s1.addScript(a);
        s1.addScript(base);
        EventSurvey s2;
        s2.addScript(base);
        s2.addScript(a);
        ASSERT_EQ(s1.events().size(), 1u);
        EXPECT_TRUE(s1.events()[0].mDeclaredNative);
        EXPECT_TRUE(s2.events()[0].mDeclaredNative);
    }

    TEST(SkyrimSurveyEvents, nameless_functions_are_ignored)
    {
        File a;
        Object o;
        o.mName = str(a, "S");
        State s;
        s.mName = str(a, "");
        Function f;
        f.mName = Function::kNoName;
        s.mFunctions.push_back(f);
        o.mStates.push_back(s);
        a.mObjects.push_back(o);
        EventSurvey sv;
        sv.addScript(a);
        EXPECT_TRUE(sv.events().empty());
        EXPECT_EQ(sv.mFunctions, 1u);
    }

    // ---- NIF header ----

    Bytes nifHeader(const std::vector<std::string>& types, const std::vector<std::uint16_t>& index,
        std::uint32_t user = 12, std::uint32_t stream = 100)
    {
        Bytes b;
        b.text("Gamebryo File Format, Version 20.2.0.7\n");
        b.u32(0x14020007).u8(1).u32(user).u32(static_cast<std::uint32_t>(index.size()));
        if (user >= 3)
        {
            b.u32(stream);
            b.u8(3).text(std::string("ab\0", 3)); // author
            b.u8(1).u8(0); // process script (stream < 131)
            b.u8(1).u8(0); // export script
            if (stream >= 103)
                b.u8(1).u8(0); // max file path
        }
        b.u16(static_cast<std::uint16_t>(types.size()));
        for (const std::string& t : types)
            b.u32(static_cast<std::uint32_t>(t.size())).text(t);
        for (std::uint16_t i : index)
            b.u16(i);
        return b;
    }

    TEST(SkyrimSurveyNif, reads_the_block_type_table_and_indices)
    {
        const Bytes b = nifHeader({ "NiNode", "BSTriShape" }, { 0, 1, 1, 1 });
        const NifHeader h = readNifHeader(view(b));
        ASSERT_TRUE(h.mOk) << h.mError;
        EXPECT_EQ(h.mVersion, 0x14020007u);
        EXPECT_EQ(h.mUserVersion, 12u);
        EXPECT_EQ(h.mStreamVersion, 100u);
        ASSERT_EQ(h.mTypeNames.size(), 2u);
        EXPECT_EQ(h.mTypeNames[1], "BSTriShape");
        ASSERT_EQ(h.mBlockTypes.size(), 4u);
        EXPECT_EQ(h.mBlockTypes[0], 0);
        EXPECT_EQ(h.mBlockTypes[3], 1);
    }

    TEST(SkyrimSurveyNif, stream_130_has_a_max_file_path)
    {
        const Bytes b = nifHeader({ "NiNode" }, { 0, 0 }, 12, 130);
        const NifHeader h = readNifHeader(view(b));
        ASSERT_TRUE(h.mOk) << h.mError;
        EXPECT_EQ(h.mBlockTypes.size(), 2u);
    }

    TEST(SkyrimSurveyNif, hostile_headers_are_refused_with_a_message)
    {
        Bytes good = nifHeader({ "NiNode" }, { 0 });
        // every truncation fails cleanly
        for (std::size_t n = 0; n + 1 < good.size(); ++n)
        {
            const Bytes cut = Bytes().text(good.str().substr(0, n));
            const NifHeader h = readNifHeader(view(cut));
            EXPECT_FALSE(h.mOk) << n;
            EXPECT_FALSE(h.mError.empty()) << n;
        }
        // a type index past the table
        const NifHeader bad = readNifHeader(view(nifHeader({ "NiNode" }, { 3 })));
        EXPECT_FALSE(bad.mOk);
        // not a NIF
        EXPECT_FALSE(readNifHeader(view(Bytes().text("hello world, not a nif at all\n"))).mOk);
        // a huge block count with no bytes behind it
        Bytes big;
        big.text("Gamebryo File Format, Version 20.2.0.7\n");
        big.u32(0x14020007).u8(1).u32(12).u32(0xFFFFFFFFu).u32(100);
        EXPECT_FALSE(readNifHeader(view(big)).mOk);
    }

    TEST(SkyrimSurveyNif, survey_counts_files_blocks_and_versions)
    {
        NifSurvey s;
        s.add(readNifHeader(view(nifHeader({ "NiNode", "BSTriShape" }, { 0, 1, 1 }))));
        s.add(readNifHeader(view(nifHeader({ "NiNode" }, { 0 }))));
        s.add(readNifHeader(view(Bytes().text("junk"))));
        EXPECT_EQ(s.mFiles, 3u);
        EXPECT_EQ(s.mHeaderFailures, 1u);
        EXPECT_EQ(s.mBlocks, 4u);
        EXPECT_EQ(s.mBlocksByType["NiNode"], 2u);
        EXPECT_EQ(s.mBlocksByType["BSTriShape"], 2u);
        EXPECT_EQ(s.mFilesByType["NiNode"], 2u);
        EXPECT_EQ(s.mFilesByType["BSTriShape"], 1u);
        EXPECT_EQ(s.mVersions["20.2.0.7 user 12 stream 100"], 2u);
        EXPECT_EQ(s.mHeaderErrors.size(), 1u);
    }

    TEST(SkyrimSurveyNif, reader_failures_name_unsupported_types)
    {
        NifSurvey s;
        s.noteReaderFailure("Unknown record type BSFooBar (file x.nif)");
        s.noteReaderFailure("Unknown record type BSFooBar");
        s.noteReaderFailure("Unknown record type NiOther");
        s.noteReaderFailure("Something else entirely");
        EXPECT_EQ(s.mReaderFailures, 4u);
        EXPECT_EQ(s.mUnsupportedTypes.size(), 2u);
        EXPECT_TRUE(s.mUnsupportedTypes.count("BSFooBar"));
        EXPECT_TRUE(s.mUnsupportedTypes.count("NiOther"));
        EXPECT_EQ(s.mReaderFailureKinds.size(), 3u);
    }
}
