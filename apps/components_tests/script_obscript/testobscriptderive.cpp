// Task 0092 item 2: deriving the ObScript function table from script text and compiled bytes.
//
// In plain words: given the text of a script and its compiled bytes, the derivation pairs each command name with the
// compiled command number, the number of arguments and what kind each argument was. These tests build the compiled
// bytes by hand (derivehelpers.hpp, written from the format description in derive.hpp) for invented commands, and check
// the table that comes out: names, opcodes, shapes, counts, the opcodes that stay unnamed, scripts that are skipped,
// determinism, the text format, hand-written seed entries, and hostile input.

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include <components/script_obscript/derive.hpp>
#include <components/script_obscript/functiontable.hpp>

#include "derivehelpers.hpp"

namespace
{
    using namespace DeriveTest;
    using ScriptObscript::DeriveEnvironment;
    using ScriptObscript::DeriveStats;
    using ScriptObscript::FunctionTable;
    using ScriptObscript::SourceUnit;
    using ScriptObscript::TableEntry;

    struct Unit
    {
        std::string mSource;
        Bytes mCompiled;
    };

    DeriveEnvironment environment()
    {
        DeriveEnvironment env;
        env.mForms = { "player", "something", "otherthing" };
        env.mVariables = { "stage", "counter" };
        return env;
    }

    FunctionTable derive(const std::vector<Unit>& units, DeriveStats* stats = nullptr)
    {
        std::vector<SourceUnit> views;
        for (const Unit& unit : units)
        {
            views.push_back({ unit.mSource, unit.mCompiled });
        }
        return ScriptObscript::deriveFunctionTable(views, environment(), stats);
    }

    // A whole script from its body lines (text) and statements (bytes).
    Unit script(const std::string& bodyText, const Bytes& bodyBytes, const std::string& locals = {})
    {
        Unit unit;
        unit.mSource = "ScriptName T\n" + locals + "Begin GameMode\n" + bodyText + "End\n";
        unit.mCompiled = scriptName() + begin() + bodyBytes + end();
        return unit;
    }

    // Object references are numbered from 1: 1 is the player, 2 is Something, 3 is OtherThing.

    constexpr unsigned kFrob = 0x2001;
    constexpr unsigned kGreet = 0x2002;
    constexpr unsigned kScale = 0x2003;
    constexpr unsigned kMode = 0x2004;
    constexpr unsigned kCount = 0x2005;
    constexpr unsigned kDist = 0x2006;

    Unit frob(const std::string& text, const Bytes& bytes)
    {
        return script(text, bytes);
    }

    TEST(ObScriptDerive, a_command_statement_gives_its_name_opcode_and_shape)
    {
        DeriveStats stats;
        const FunctionTable table = derive(
            { frob("Frobnicate Something 5\n", statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }))) }, &stats);
        const TableEntry* entry = table.find("frobnicate");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mOpcode, kFrob);
        EXPECT_EQ(entry->mSignature, "rl/");
        EXPECT_EQ(entry->mReturns, 'l');
        EXPECT_EQ(entry->mOccurrences, 1u);
        EXPECT_EQ(entry->mOpcodeVotes, 1u);
        EXPECT_EQ(entry->mShapeVotes, 1u);
        EXPECT_EQ(entry->mExplicitUses, 0u);
        EXPECT_FALSE(entry->mSeeded);
        EXPECT_EQ(stats.mUnits, 1u);
        EXPECT_EQ(stats.mAligned, 1u);
        EXPECT_EQ(stats.mPairs, 1u);
        EXPECT_EQ(stats.mOpcodesSeen, 1u);
        EXPECT_EQ(stats.mOpcodesNamed, 1u);
    }

    TEST(ObScriptDerive, names_are_lower_cased_and_the_text_may_have_comments_blank_lines_and_declarations)
    {
        Unit unit;
        unit.mSource = "scn T\n; a comment\nshort x\n\nBEGIN GameMode ; why\n  FROBNICATE Something 5 ; later\nEND\n";
        unit.mCompiled = scriptName() + begin() + statement(kFrob, payload({ Arg::ref(2), Arg::number(5) })) + end();
        const FunctionTable table = derive({ unit });
        ASSERT_NE(table.find("frobnicate"), nullptr);
        EXPECT_EQ(table.find("FROBNICATE"), nullptr); // lookups are by lower-case name
    }

    TEST(ObScriptDerive, a_call_on_a_reference_is_counted_as_an_explicit_use)
    {
        const FunctionTable table = derive({ frob("player.Frobnicate Something 5\n",
            referencePrefix(1) + statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }))) });
        const TableEntry* entry = table.find("frobnicate");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mOpcode, kFrob);
        EXPECT_EQ(entry->mExplicitUses, 1u);
        EXPECT_EQ(entry->mOccurrences, 1u);
    }

    TEST(ObScriptDerive, the_arrow_form_of_a_reference_call_is_read_too)
    {
        const FunctionTable table = derive({ frob("Something->Frobnicate OtherThing 5\n",
            referencePrefix(2) + statement(kFrob, payload({ Arg::ref(3), Arg::number(5) }))) });
        ASSERT_NE(table.find("frobnicate"), nullptr);
        EXPECT_EQ(table.find("frobnicate")->mExplicitUses, 1u);
    }

    TEST(ObScriptDerive, counts_add_up_and_the_most_used_opcode_wins_with_the_conflict_visible)
    {
        const Bytes ok = statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }));
        const Bytes odd = statement(kGreet, payload({ Arg::ref(2), Arg::number(5) }));
        const FunctionTable table = derive({ frob("Frobnicate Something 5\n", ok), frob("Frobnicate Something 5\n", ok),
            frob("Frobnicate Something 5\n", odd) });
        const TableEntry* entry = table.find("frobnicate");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mOpcode, kFrob);
        EXPECT_EQ(entry->mOccurrences, 3u);
        EXPECT_EQ(entry->mOpcodeVotes, 2u);
        EXPECT_NEAR(entry->opcodeConfidence(), 2.0 / 3.0, 1e-9);
    }

    TEST(ObScriptDerive, a_tie_between_opcodes_goes_to_the_lower_number)
    {
        const FunctionTable table
            = derive({ frob("Frobnicate Something 5\n", statement(0x2010, payload({ Arg::ref(2), Arg::number(5) }))),
                frob("Frobnicate Something 5\n", statement(0x2008, payload({ Arg::ref(2), Arg::number(5) }))) });
        ASSERT_NE(table.find("frobnicate"), nullptr);
        EXPECT_EQ(table.find("frobnicate")->mOpcode, 0x2008u);
    }

    TEST(ObScriptDerive, two_names_for_one_opcode_are_both_listed_and_the_opcode_prints_the_more_used_name)
    {
        const Bytes call = statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }));
        DeriveStats stats;
        const FunctionTable table
            = derive({ frob("Frobnicate Something 5\n", call), frob("Frobnicate Something 5\n", call),
                         frob("Frob Something 5\n", call) },
                &stats);
        ASSERT_NE(table.find("frobnicate"), nullptr);
        ASSERT_NE(table.find("frob"), nullptr);
        EXPECT_EQ(table.find("frob")->mOpcode, kFrob);
        EXPECT_EQ(table.nameOfOpcode(kFrob), "frobnicate");
        EXPECT_EQ(stats.mOpcodesWithSeveralNames, 1u);
    }

    TEST(ObScriptDerive, arguments_that_are_sometimes_missing_make_the_rest_optional)
    {
        const FunctionTable table = derive({ frob("Greet 5\n", statement(kGreet, payload({ Arg::number(5) }))),
            frob("Greet 5 6\n", statement(kGreet, payload({ Arg::number(5), Arg::number(6) }))) });
        const TableEntry* entry = table.find("greet");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mSignature, "l/l");
        EXPECT_EQ(entry->mShapeVotes, 2u);
    }

    TEST(ObScriptDerive, a_float_argument_is_a_float_even_when_it_is_written_as_a_whole_number)
    {
        const FunctionTable table
            = derive({ frob("Scale Something 2\n", statement(kScale, payload({ Arg::ref(2), Arg::real(2.0) }))),
                frob("Scale Something 3.5\n", statement(kScale, payload({ Arg::ref(2), Arg::real(3.5) }))) });
        const TableEntry* entry = table.find("scale");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mSignature, "rf/");
        EXPECT_EQ(entry->mShapeVotes, 2u);
    }

    TEST(ObScriptDerive, a_variable_in_an_argument_takes_the_kind_of_the_variable)
    {
        Unit unit = script("Scale Something fValue\n", statement(kScale, payload({ Arg::ref(2), Arg::variable(0) })),
            "float fValue\n");
        const FunctionTable table = derive({ unit });
        ASSERT_NE(table.find("scale"), nullptr);
        EXPECT_EQ(table.find("scale")->mSignature, "rl/");
    }

    TEST(ObScriptDerive, a_word_the_engine_knows_is_a_bare_word_argument)
    {
        const FunctionTable table
            = derive({ frob("SetMode Sneaking\n", statement(kMode, payload({ Arg::untagged(3) }))) });
        const TableEntry* entry = table.find("setmode");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mSignature, "c/");
    }

    TEST(ObScriptDerive, a_quoted_string_is_a_string_argument)
    {
        const FunctionTable table
            = derive({ frob("SetName \"hello\"\n", statement(kMode + 10, payload({ Arg::untagged(5) }))) });
        ASSERT_NE(table.find("setname"), nullptr);
        EXPECT_EQ(table.find("setname")->mSignature, "S/");
    }

    TEST(ObScriptDerive, a_command_with_no_arguments_has_the_empty_shape)
    {
        const FunctionTable table = derive({ frob("Disable\n", statement(0x2020, payload({}))) });
        ASSERT_NE(table.find("disable"), nullptr);
        EXPECT_EQ(table.find("disable")->mSignature, "/");
    }

    TEST(ObScriptDerive, a_command_in_a_condition_is_paired)
    {
        const Bytes condition = Expr::call(kCount, payload({ Arg::ref(2) })) + Expr::number("0") + Expr::op(">");
        const FunctionTable table
            = derive({ frob("If GetThingCount Something > 0\nEndIf\n", ifStatement(condition) + endIf()) });
        const TableEntry* entry = table.find("getthingcount");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mOpcode, kCount);
        EXPECT_EQ(entry->mSignature, "r/");
        EXPECT_EQ(entry->mExplicitUses, 0u);
    }

    TEST(ObScriptDerive, a_command_on_a_reference_in_a_condition_is_an_explicit_use)
    {
        const Bytes condition
            = Expr::ref(1) + Expr::call(kCount, payload({ Arg::ref(2) }), true) + Expr::number("0") + Expr::op(">");
        const FunctionTable table
            = derive({ frob("If player.GetThingCount Something > 0\nEndIf\n", ifStatement(condition) + endIf()) });
        ASSERT_NE(table.find("getthingcount"), nullptr);
        EXPECT_EQ(table.find("getthingcount")->mExplicitUses, 1u);
    }

    TEST(ObScriptDerive, a_command_with_no_arguments_inside_an_expression_has_an_empty_payload)
    {
        const Bytes condition = Expr::call(kDist, Expr::noArguments()) + Expr::number("100") + Expr::op("<");
        const FunctionTable table
            = derive({ frob("If GetCloseness < 100\nEndIf\n", ifStatement(condition) + endIf()) });
        ASSERT_NE(table.find("getcloseness"), nullptr);
        EXPECT_EQ(table.find("getcloseness")->mSignature, "/");
    }

    TEST(ObScriptDerive, two_commands_in_one_condition_are_paired_in_order)
    {
        const Bytes condition = Expr::call(kCount, payload({ Arg::ref(2) })) + Expr::number("0") + Expr::op(">")
            + Expr::call(kDist, Expr::noArguments()) + Expr::number("5") + Expr::op("<") + Expr::op("&&");
        const FunctionTable table = derive(
            { frob("If GetThingCount Something > 0 && GetCloseness < 5\nEndIf\n", ifStatement(condition) + endIf()) });
        ASSERT_NE(table.find("getthingcount"), nullptr);
        ASSERT_NE(table.find("getcloseness"), nullptr);
        EXPECT_EQ(table.find("getthingcount")->mOpcode, kCount);
        EXPECT_EQ(table.find("getcloseness")->mOpcode, kDist);
    }

    TEST(ObScriptDerive, else_and_elseif_lines_agree_with_their_statements)
    {
        const Bytes condition = Expr::call(kDist, Expr::noArguments()) + Expr::number("5") + Expr::op("<");
        const FunctionTable table = derive({ frob("If GetCloseness < 5\nElseIf GetCloseness < 5\nElse\nEndIf\n",
            ifStatement(condition) + ifStatement(condition, 0x18) + statement(0x17) + endIf()) });
        ASSERT_NE(table.find("getcloseness"), nullptr);
        EXPECT_EQ(table.find("getcloseness")->mOccurrences, 2u);
    }

    TEST(ObScriptDerive, a_command_in_a_set_statement_is_paired_and_a_float_variable_makes_it_a_float)
    {
        const Bytes target = "f" + std::string("\x00\x00", 2);
        const Bytes expression = Expr::call(kDist, payload({ Arg::ref(2) }));
        Unit unit
            = script("Set fValue to GetCloseness Something\n", setStatement(target, expression), "float fValue\n");
        const FunctionTable table = derive({ unit });
        const TableEntry* entry = table.find("getcloseness");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mOpcode, kDist);
        EXPECT_EQ(entry->mReturns, 'f');
    }

    TEST(ObScriptDerive, a_command_compared_with_a_float_number_returns_a_float)
    {
        const Bytes condition = Expr::call(kDist, payload({ Arg::ref(2) })) + Expr::number("1.5") + Expr::op("<");
        const FunctionTable table
            = derive({ frob("If GetCloseness Something < 1.5\nEndIf\n", ifStatement(condition) + endIf()) });
        ASSERT_NE(table.find("getcloseness"), nullptr);
        EXPECT_EQ(table.find("getcloseness")->mReturns, 'f');
    }

    TEST(ObScriptDerive, a_command_assigned_to_an_integer_variable_returns_an_integer)
    {
        const Bytes target = "s" + std::string("\x00\x00", 2);
        const Bytes expression = Expr::call(kCount, payload({ Arg::ref(2) }));
        Unit unit
            = script("Set iValue to GetThingCount Something\n", setStatement(target, expression), "long iValue\n");
        const FunctionTable table = derive({ unit });
        ASSERT_NE(table.find("getthingcount"), nullptr);
        EXPECT_EQ(table.find("getthingcount")->mReturns, 'l');
    }

    TEST(ObScriptDerive, a_variable_of_another_script_is_not_taken_for_a_command)
    {
        // "Something.Stage" is a variable: the compiled set has a reference token and a variable token, no call.
        const Bytes target = Arg::ref(2) + Arg::variable(4);
        const Bytes expression = Expr::number("1");
        DeriveStats stats;
        const FunctionTable table
            = derive({ frob("Set Something.Stage to 1\n", setStatement(target, expression)) }, &stats);
        EXPECT_EQ(stats.mAligned, 1u);
        EXPECT_EQ(stats.mPairs, 0u);
        EXPECT_TRUE(table.mEntries.empty());
    }

    TEST(ObScriptDerive, a_fragment_without_begin_and_end_is_read)
    {
        Unit unit;
        unit.mSource = "Frobnicate Something 5\n";
        unit.mCompiled = statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }));
        const FunctionTable table = derive({ unit });
        ASSERT_NE(table.find("frobnicate"), nullptr);
    }

    TEST(ObScriptDerive, an_opcode_that_is_never_named_is_listed_and_prints_as_fn_0x)
    {
        // The second unit's statements do not match its lines, so it is skipped; its opcode is still seen.
        Unit skipped;
        skipped.mSource = "ScriptName T\nBegin GameMode\nFrobnicate Something 5\nFrobnicate Something 6\nEnd\n";
        skipped.mCompiled
            = scriptName() + begin() + statement(0x3333, payload({ Arg::ref(2), Arg::number(5) })) + end();
        DeriveStats stats;
        const FunctionTable table = derive({ skipped }, &stats);
        EXPECT_TRUE(table.mEntries.empty());
        EXPECT_EQ(stats.mSkippedLineCount, 1u);
        EXPECT_EQ(stats.mOpcodesSeen, 1u);
        EXPECT_EQ(stats.mOpcodesNamed, 0u);
        ASSERT_EQ(table.mOpcodes.size(), 1u);
        EXPECT_EQ(table.mOpcodes[0].mOpcode, 0x3333u);
        EXPECT_EQ(table.nameOfOpcode(0x3333), "fn_0x3333");
        EXPECT_EQ(ScriptObscript::unnamedOpcodeName(0x1022), "fn_0x1022");
    }

    TEST(ObScriptDerive, a_script_whose_lines_and_statements_disagree_is_skipped_and_counted)
    {
        DeriveStats stats;
        Unit wrongKind;
        wrongKind.mSource = "ScriptName T\nBegin GameMode\nIf 1\nEndIf\nEnd\n";
        wrongKind.mCompiled = scriptName() + begin() + statement(kFrob, payload({})) + endIf() + end();
        const FunctionTable table = derive({ wrongKind }, &stats);
        EXPECT_EQ(stats.mSkippedKind, 1u);
        EXPECT_EQ(stats.mAligned, 0u);
        EXPECT_TRUE(table.mEntries.empty());
    }

    TEST(ObScriptDerive, compiled_bytes_that_do_not_end_where_they_should_are_skipped_not_guessed)
    {
        Unit unit = frob("Frobnicate Something 5\n", statement(kFrob, payload({ Arg::ref(2), Arg::number(5) })));
        unit.mCompiled.resize(unit.mCompiled.size() - 2); // cut off in the middle of End
        DeriveStats stats;
        derive({ unit }, &stats);
        EXPECT_EQ(stats.mSkippedMalformed, 1u);
        EXPECT_EQ(stats.mPairs, 0u);
    }

    TEST(ObScriptDerive, an_expression_that_cannot_be_read_skips_the_script)
    {
        const Bytes broken = Bytes(" ?") + Expr::number("0");
        DeriveStats stats;
        derive({ frob("If GetThingCount Something > 0\nEndIf\n", ifStatement(broken) + endIf()) }, &stats);
        EXPECT_EQ(stats.mSkippedExpression, 1u);
        EXPECT_EQ(stats.mPairs, 0u);
    }

    TEST(ObScriptDerive, a_command_whose_argument_count_often_differs_from_the_text_has_the_skip_shape)
    {
        // The text always shows two words, the bytes sometimes one, sometimes three: not a shape to trust.
        std::vector<Unit> units;
        for (int i = 0; i < 5; ++i)
        {
            units.push_back(frob("Spread Something 5\n",
                statement(0x2030,
                    payload(i % 2 == 0 ? std::vector<Bytes>{ Arg::ref(2) }
                                       : std::vector<Bytes>{ Arg::ref(2), Arg::number(5), Arg::number(6) }))));
        }
        DeriveStats stats;
        const FunctionTable table = derive(units, &stats);
        ASSERT_NE(table.find("spread"), nullptr);
        EXPECT_EQ(table.find("spread")->mSignature, "*");
        EXPECT_LT(table.find("spread")->shapeConfidence(), 0.8);
        EXPECT_EQ(stats.mVariableArity, 1u);
        EXPECT_GT(stats.mLinesWithOtherCount, 0u);
    }

    TEST(ObScriptDerive, a_command_with_a_form_in_one_place_and_a_number_in_another_has_the_skip_shape)
    {
        const FunctionTable table = derive({ frob("Mixed Something\n", statement(0x2040, payload({ Arg::ref(2) }))),
            frob("Mixed 5\n", statement(0x2040, payload({ Arg::number(5) }))),
            frob("Mixed 6\n", statement(0x2040, payload({ Arg::number(6) }))) });
        ASSERT_NE(table.find("mixed"), nullptr);
        EXPECT_LT(table.find("mixed")->shapeConfidence(), 1.0);
    }

    TEST(ObScriptDerive, the_result_does_not_depend_on_the_order_of_the_units)
    {
        std::vector<Unit> units;
        units.push_back(frob("Frobnicate Something 5\n", statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }))));
        units.push_back(frob("Frob Something 5\n", statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }))));
        units.push_back(frob("Greet 5\n", statement(kGreet, payload({ Arg::number(5) }))));
        units.push_back(frob("Greet 5 6\n", statement(kGreet, payload({ Arg::number(5), Arg::number(6) }))));
        units.push_back(frob("Disable\n", statement(0x2020, payload({}))));
        const std::string first = ScriptObscript::serialiseTable(derive(units));
        std::mt19937 random(7);
        for (int i = 0; i < 20; ++i)
        {
            std::shuffle(units.begin(), units.end(), random);
            EXPECT_EQ(ScriptObscript::serialiseTable(derive(units)), first);
        }
        EXPECT_FALSE(first.empty());
    }

    TEST(ObScriptDerive, hostile_compiled_bytes_never_crash_the_derivation)
    {
        std::mt19937 random(11);
        const Unit base = frob("Frobnicate Something 5\nIf GetThingCount Something > 0\nEndIf\n",
            statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }))
                + ifStatement(Expr::call(kCount, payload({ Arg::ref(2) })) + Expr::number("0") + Expr::op(">"))
                + endIf());
        std::vector<Unit> units;
        for (int i = 0; i < 3000; ++i)
        {
            Unit unit = base;
            const int edits = 1 + static_cast<int>(random() % 6);
            for (int e = 0; e < edits; ++e)
            {
                switch (random() % 3)
                {
                    case 0:
                        unit.mCompiled[random() % unit.mCompiled.size()] = static_cast<char>(random() & 0xFF);
                        break;
                    case 1:
                        unit.mCompiled.resize(random() % (unit.mCompiled.size() + 1));
                        break;
                    default:
                        unit.mCompiled.insert(random() % (unit.mCompiled.size() + 1), 1, static_cast<char>(random()));
                        break;
                }
                if (unit.mCompiled.empty())
                {
                    unit.mCompiled = "x";
                }
            }
            units.push_back(std::move(unit));
        }
        DeriveStats stats;
        const FunctionTable table = derive(units, &stats);
        EXPECT_EQ(stats.mUnits, units.size());
        EXPECT_LE(table.mEntries.size(), 100u);
    }

    TEST(ObScriptDerive, huge_inputs_are_bounded_by_the_caller_but_a_long_line_is_fine)
    {
        std::string line = "Frobnicate Something 5";
        for (int i = 0; i < 1000; ++i)
        {
            line += " 5";
        }
        line += "\n";
        DeriveStats stats;
        derive({ frob(line, statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }))) }, &stats);
        EXPECT_EQ(stats.mUnits, 1u);
        EXPECT_EQ(stats.mLinesWithOtherCount, 1u);
    }

    // ---- the table and its text -------------------------------------------------------------------------------

    FunctionTable sampleTable()
    {
        return derive({ frob("Frobnicate Something 5\n", statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }))),
            frob("Frob Something 5\n", statement(kFrob, payload({ Arg::ref(2), Arg::number(5) }))),
            frob("Greet 5\n", statement(kGreet, payload({ Arg::number(5) }))),
            frob("player.Disable\n", referencePrefix(1) + statement(0x2020, payload({}))) });
    }

    TEST(ObScriptTable, the_text_form_is_canonical_and_reads_back_to_the_same_bytes)
    {
        const FunctionTable table = sampleTable();
        const std::string text = ScriptObscript::serialiseTable(table);
        ASSERT_EQ(text.rfind("openeden.obscript.functions 1\n", 0), 0u);
        const auto parsed = ScriptObscript::parseTable(text, "table");
        ASSERT_TRUE(parsed.ok) << (parsed.errors.empty() ? "" : parsed.errors.front().format());
        EXPECT_EQ(ScriptObscript::serialiseTable(parsed.table), text);
        ASSERT_NE(parsed.table.find("greet"), nullptr);
        EXPECT_EQ(parsed.table.find("greet")->mSignature, "l/");
        EXPECT_EQ(parsed.table.find("greet")->mOpcode, kGreet);
    }

    TEST(ObScriptTable, the_text_has_the_records_in_sorted_order)
    {
        const std::string text = ScriptObscript::serialiseTable(sampleTable());
        const std::size_t disable = text.find("function disable ");
        const std::size_t frob = text.find("function frob ");
        const std::size_t frobnicate = text.find("function frobnicate ");
        const std::size_t greet = text.find("function greet ");
        ASSERT_NE(disable, std::string::npos);
        EXPECT_LT(disable, frob);
        EXPECT_LT(frob, frobnicate);
        EXPECT_LT(frobnicate, greet);
        EXPECT_NE(text.find("opcode 2001 "), std::string::npos);
    }

    TEST(ObScriptTable, confidence_is_the_share_of_places_that_agree)
    {
        TableEntry entry;
        entry.mOccurrences = 4;
        entry.mOpcodeVotes = 3;
        entry.mShapeVotes = 2;
        EXPECT_DOUBLE_EQ(entry.opcodeConfidence(), 0.75);
        EXPECT_DOUBLE_EQ(entry.shapeConfidence(), 0.5);
        TableEntry seeded;
        seeded.mSeeded = true;
        EXPECT_DOUBLE_EQ(seeded.opcodeConfidence(), 1.0);
        EXPECT_DOUBLE_EQ(seeded.shapeConfidence(), 1.0);
        EXPECT_DOUBLE_EQ(TableEntry().opcodeConfidence(), 0.0);
    }

    struct BadText
    {
        const char* mName;
        std::string mText;
        unsigned mLine;
        const char* mMessage;
    };

    TEST(ObScriptTable, a_malformed_table_is_refused_with_a_located_error)
    {
        const std::string header = "openeden.obscript.functions 1\n";
        const std::string good = "function frob 2001 rl/ l 3 3 3 0 derived\n";
        const std::vector<BadText> cases = {
            { "no header", "function frob 2001 rl/ l 3 3 3 0 derived\n", 1, "header" },
            { "wrong version", "openeden.obscript.functions 2\n", 1, "version" },
            { "unknown record", header + "thing a b\n", 2, "record" },
            { "too few fields", header + "function frob 2001 rl/ l 3\n", 2, "fields" },
            { "bad opcode", header + "function frob zz rl/ l 3 3 3 0 derived\n", 2, "opcode" },
            { "bad signature", header + "function frob 2001 r!/ l 3 3 3 0 derived\n", 2, "signature" },
            { "signature without a slash", header + "function frob 2001 rl l 3 3 3 0 derived\n", 2, "signature" },
            { "bad return", header + "function frob 2001 rl/ x 3 3 3 0 derived\n", 2, "returns" },
            { "votes above occurrences", header + "function frob 2001 rl/ l 3 4 3 0 derived\n", 2, "votes" },
            { "bad origin", header + "function frob 2001 rl/ l 3 3 3 0 elsewhere\n", 2, "derived or seed" },
            { "upper case name", header + "function Frob 2001 rl/ l 3 3 3 0 derived\n", 2, "name" },
            { "duplicate name", header + good + good, 3, "duplicate" },
            { "overlong name", header + "function " + std::string(200, 'a') + " 2001 rl/ l 3 3 3 0 derived\n", 2,
                "name" },
            { "overlong line", header + "# " + std::string(ScriptObscript::TableLimits::kMaxLineBytes + 1, 'x') + "\n",
                2, "too long" },
            { "bad opcode record", header + "opcode 2001 x 0 0\n", 2, "occurrences" },
            { "arguments reversed", header + "opcode 2001 4 3 1\n", 2, "arguments" },
        };
        for (const BadText& item : cases)
        {
            SCOPED_TRACE(item.mName);
            const auto parsed = ScriptObscript::parseTable(item.mText, "t.table");
            EXPECT_FALSE(parsed.ok);
            ASSERT_FALSE(parsed.errors.empty());
            EXPECT_EQ(parsed.errors.front().script, "t.table");
            EXPECT_EQ(parsed.errors.front().line, item.mLine);
            EXPECT_NE(parsed.errors.front().message.find(item.mMessage), std::string::npos)
                << parsed.errors.front().format();
            EXPECT_TRUE(parsed.table.mEntries.empty());
        }
    }

    TEST(ObScriptTable, comments_and_blank_lines_are_allowed)
    {
        const auto parsed = ScriptObscript::parseTable(
            "openeden.obscript.functions 1\n# made here\n\nfunction frob 2001 rl/ l 3 3 3 0 derived\n", "t");
        EXPECT_TRUE(parsed.ok);
        EXPECT_NE(parsed.table.find("frob"), nullptr);
    }

    TEST(ObScriptTable, too_large_a_table_is_refused_before_it_is_read)
    {
        const std::string big(ScriptObscript::TableLimits::kMaxBytes + 1, 'x');
        EXPECT_FALSE(ScriptObscript::parseTable(big, "t").ok);
    }

    TEST(ObScriptTable, too_many_entries_are_refused)
    {
        std::string text = "openeden.obscript.functions 1\n";
        for (std::size_t i = 0; i <= ScriptObscript::TableLimits::kMaxEntries; ++i)
        {
            text += "function f" + std::to_string(i) + " 2001 / l 1 1 1 0 derived\n";
        }
        const auto parsed = ScriptObscript::parseTable(text, "t");
        EXPECT_FALSE(parsed.ok);
        ASSERT_FALSE(parsed.errors.empty());
        EXPECT_NE(parsed.errors.front().message.find("too many"), std::string::npos);
    }

    TEST(ObScriptTable, a_deterministic_fuzz_of_the_text_never_crashes)
    {
        const std::string text = ScriptObscript::serialiseTable(sampleTable());
        std::mt19937 random(5);
        for (int i = 0; i < 20000; ++i)
        {
            std::string mutated = text;
            const int edits = 1 + static_cast<int>(random() % 4);
            for (int e = 0; e < edits; ++e)
            {
                const std::size_t at = random() % mutated.size();
                switch (random() % 3)
                {
                    case 0:
                        mutated[at] = static_cast<char>(random() & 0xFF);
                        break;
                    case 1:
                        mutated.erase(at, 1 + random() % 5);
                        break;
                    default:
                        mutated.insert(at, 1, static_cast<char>(' ' + random() % 90));
                        break;
                }
                if (mutated.empty())
                {
                    mutated = "x";
                }
            }
            const auto parsed = ScriptObscript::parseTable(mutated, "fuzz");
            if (parsed.ok)
            {
                // whatever is accepted must write and read back the same
                const std::string again = ScriptObscript::serialiseTable(parsed.table);
                const auto second = ScriptObscript::parseTable(again, "fuzz");
                ASSERT_TRUE(second.ok);
                EXPECT_EQ(ScriptObscript::serialiseTable(second.table), again);
            }
        }
    }

    // ---- hand-written entries -----------------------------------------------------------------------------------

    TEST(ObScriptSeed, a_seed_entry_with_a_source_is_added_when_the_table_lacks_the_name)
    {
        FunctionTable table = sampleTable();
        std::vector<openeden::script::ErrorReport> errors;
        const std::size_t added = ScriptObscript::mergeSeed(table,
            "function wibble 0 rl/ l 0 0 0 0 seed RECALLED-VERIFY written from memory, check against the game's help\n"
            "function frob 0 / l 0 0 0 0 seed READ a page that was opened\n",
            "seed.txt", errors);
        EXPECT_EQ(added, 1u); // "frob" is already derived: the derived entry wins
        EXPECT_TRUE(errors.empty());
        const TableEntry* wibble = table.find("wibble");
        ASSERT_NE(wibble, nullptr);
        EXPECT_TRUE(wibble->mSeeded);
        EXPECT_EQ(wibble->mSignature, "rl/");
        EXPECT_EQ(wibble->mSource.rfind("RECALLED-VERIFY", 0), 0u);
        EXPECT_EQ(table.find("frob")->mSeeded, false);
    }

    TEST(ObScriptSeed, a_seed_entry_without_a_source_or_not_marked_seed_is_refused)
    {
        FunctionTable table;
        std::vector<openeden::script::ErrorReport> errors;
        EXPECT_EQ(ScriptObscript::mergeSeed(table, "function wibble 0 rl/ l 0 0 0 0 seed\n", "seed.txt", errors), 0u);
        ASSERT_EQ(errors.size(), 1u);
        EXPECT_EQ(errors[0].line, 1u);
        EXPECT_NE(errors[0].message.find("source"), std::string::npos);
        errors.clear();
        EXPECT_EQ(
            ScriptObscript::mergeSeed(table, "function wibble 0 rl/ l 0 0 0 0 derived\n", "seed.txt", errors), 0u);
        EXPECT_EQ(errors.size(), 1u);
        errors.clear();
        EXPECT_EQ(ScriptObscript::mergeSeed(
                      table, "function wibble 0 rl/ l 0 0 0 0 seed from the manual\n", "seed.txt", errors),
            0u);
        EXPECT_EQ(errors.size(), 1u); // the source must start with READ or RECALLED-VERIFY
        EXPECT_TRUE(table.mEntries.empty());
    }

    TEST(ObScriptSeed, seed_entries_given_out_of_order_are_all_found_and_derived_names_still_win)
    {
        FunctionTable table = sampleTable();
        std::string seed;
        for (const char* name : { "zeta", "alpha", "frobnicate", "mu", "beta", "greet", "omega" })
        {
            seed += std::string("function ") + name + " 0 / l 0 0 0 0 seed READ a test page" + "\n";
        }
        std::vector<openeden::script::ErrorReport> errors;
        EXPECT_EQ(ScriptObscript::mergeSeed(table, seed, "seed.txt", errors), 5u);
        EXPECT_TRUE(errors.empty());
        for (const char* name : { "zeta", "alpha", "mu", "beta", "omega", "disable", "frob", "frobnicate", "greet" })
        {
            EXPECT_NE(table.find(name), nullptr) << name;
        }
        EXPECT_FALSE(table.find("frobnicate")->mSeeded);
        EXPECT_FALSE(table.find("greet")->mSeeded);
        EXPECT_TRUE(std::is_sorted(table.mEntries.begin(), table.mEntries.end(),
            [](const TableEntry& left, const TableEntry& right) { return left.mName < right.mName; }));
    }

    TEST(ObScriptSeed, seed_entries_survive_the_text_round_trip)
    {
        FunctionTable table;
        std::vector<openeden::script::ErrorReport> errors;
        ScriptObscript::mergeSeed(table, "function wibble 0 rl/ l 0 0 0 0 seed READ the manual page\n", "s", errors);
        const std::string text = ScriptObscript::serialiseTable(table);
        const auto parsed = ScriptObscript::parseTable(text, "t");
        ASSERT_TRUE(parsed.ok) << (parsed.errors.empty() ? "" : parsed.errors.front().format());
        ASSERT_NE(parsed.table.find("wibble"), nullptr);
        EXPECT_TRUE(parsed.table.find("wibble")->mSeeded);
        EXPECT_EQ(parsed.table.find("wibble")->mSource, "READ the manual page");
    }
}
