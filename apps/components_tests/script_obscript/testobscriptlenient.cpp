// Task 0092 item 3: the last grammar habits of the Fallout 3 and New Vegas scripts, in the ObScript front end.
//
// In plain words, five things the shipped scripts do that the first slice refused:
//  - a member of a quest or object that begins with digits: "Set MyQuest.2ndStage to 1";
//  - an argument that is a dotted name given to a command we know no shape for: "Set x to Frob MyQuest.stage";
//  - a command whose name is also the editor id of some record ("Crate 1" when a crate is called Crate);
//  - a member the world does not know ("Set OtherQuest.stage to 1"): a warning, not a reason to refuse the script;
//  - decoration after the last End of a script (a row of "=" signs, a stray word).
// Each is accepted in tolerant mode (play) and refused in strict mode (tests), except the first, which is only a
// matter of reading the name and is accepted in both. Generated scripts and the invented stub world only.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::InstanceId;
    using openeden::script::ScriptId;
    using openeden::script::ScriptKind;
    using openeden::script::ScriptSource;
    using openeden::script::Severity;
    using openeden::script::UnknownFunctionPolicy;
    using openeden::script::Value;

    std::string wrap(const std::string& body)
    {
        return "ScriptName Lenient\nref target\nshort flag\nlong number\nfloat real\nBegin GameMode\n" + body
            + "\nEnd\n";
    }

    struct Compiled
    {
        explicit Compiled(const std::string& text, UnknownFunctionPolicy policy = UnknownFunctionPolicy::Tolerant,
            ScriptKind kind = ScriptKind::Script)
            : mFixture(policy)
        {
            mFixture.mWorld.mVariables[{ sQuest, "2ndstage" }] = Value::makeInteger(4);
            mFixture.mWorld.mVariables[{ sQuest, "crate" }] = Value::makeInteger(9);
            ScriptSource source;
            source.name = "Lenient";
            source.text = text;
            source.kind = kind;
            const auto result = mFixture.mRuntime->compile(source);
            mOk = result.ok;
            mScript = result.script;
            for (const auto& report : result.reports)
            {
                mReports.push_back(report.format());
                mErrors += report.severity == Severity::Error ? 1 : 0;
                mWarnings += report.severity == Severity::Warning ? 1 : 0;
            }
        }

        std::string reports() const
        {
            std::string all;
            for (const std::string& report : mReports)
            {
                all += report + "; ";
            }
            return all;
        }

        bool run(const char* event = "GameMode")
        {
            mInstance = mFixture.mRuntime->createInstance(mScript, sCrate);
            return mFixture.mRuntime->run(mInstance, event).ok;
        }

        std::int32_t integer(const char* name) const { return mFixture.mRuntime->getLocal(mInstance, name).integer; }

        std::int32_t worldVariable(openeden::script::FormId owner, const char* name)
        {
            return mFixture.mWorld.mVariables[std::make_pair(owner, std::string(name))].integer;
        }

        std::size_t worldVariableCount(openeden::script::FormId owner, const char* name)
        {
            return mFixture.mWorld.mVariables.count(std::make_pair(owner, std::string(name)));
        }

        RuntimeFixture mFixture;
        bool mOk = false;
        ScriptId mScript = 0;
        InstanceId mInstance = 0;
        std::vector<std::string> mReports;
        int mErrors = 0;
        int mWarnings = 0;
    };

    // ---- members that begin with digits --------------------------------------------------------------------

    TEST(ObScriptLenient, a_member_that_begins_with_digits_is_read_and_written)
    {
        for (const auto policy : { UnknownFunctionPolicy::Tolerant, UnknownFunctionPolicy::Strict })
        {
            Compiled compiled(
                wrap("Set number to MyQuest.2ndStage\nSet MyQuest.2ndStage to 11\nSet flag to MyQuest.2ndStage"),
                policy);
            ASSERT_TRUE(compiled.mOk) << compiled.reports();
            ASSERT_TRUE(compiled.run());
            EXPECT_EQ(compiled.integer("number"), 4);
            EXPECT_EQ(compiled.integer("flag"), 11);
            EXPECT_EQ(compiled.worldVariable(sQuest, "2ndstage"), 11);
        }
    }

    TEST(ObScriptLenient, a_member_that_begins_with_digits_works_in_conditions)
    {
        Compiled compiled(wrap("If ( MyQuest.2ndStage == 4 ) && ( MyQuest.stage == 3 )\n Set flag to 1\nEndIf"),
            UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("flag"), 1);
    }

    TEST(ObScriptLenient, a_floating_point_literal_is_still_a_number)
    {
        Compiled compiled(wrap("Set real to .5\nSet real to real + 1.25"), UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_FLOAT_EQ(compiled.mFixture.mRuntime->getLocal(compiled.mInstance, "real").number, 1.75f);
    }

    // ---- dotted arguments of commands without a shape -------------------------------------------------------

    TEST(ObScriptLenient, a_dotted_name_is_an_argument_of_an_unknown_command_in_a_set)
    {
        Compiled compiled(wrap("Set flag to Frob MyQuest.stage\nSet number to 5"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("flag"), 0);
        EXPECT_EQ(compiled.integer("number"), 5);
    }

    TEST(ObScriptLenient, a_dotted_name_is_an_argument_of_an_unknown_command_in_a_condition)
    {
        Compiled compiled(
            wrap("If ( flag == 0 ) && ( Frob MyQuest.stage > 1 ) || ( MyQuest.stage )\n Set number to 1\nEndIf\n"
                 "If Frob2 MyQuest.stage MyQuest.2ndStage == 0\n Set flag to 2\nEndIf"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("number"), 1);
        EXPECT_EQ(compiled.integer("flag"), 2);
    }

    TEST(ObScriptLenient, a_command_name_is_an_argument_of_another_unknown_command)
    {
        Compiled compiled(wrap("Set flag to Frob Frob2 1\nSet number to Frob3\nSet real to Frob2"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
    }

    TEST(ObScriptLenient, a_dotted_argument_does_not_swallow_the_next_line)
    {
        Compiled compiled(wrap("Set flag to Frob MyQuest.stage\nSet number to 7\nFrob MyQuest.stage\nSet real to 2.5"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("number"), 7);
        EXPECT_FLOAT_EQ(compiled.mFixture.mRuntime->getLocal(compiled.mInstance, "real").number, 2.5f);
    }

    TEST(ObScriptLenient, strict_mode_still_names_the_unknown_command)
    {
        Compiled compiled(wrap("Set flag to Frob MyQuest.stage"), UnknownFunctionPolicy::Strict);
        EXPECT_FALSE(compiled.mOk);
        EXPECT_NE(compiled.reports().find("unknown function 'Frob'"), std::string::npos) << compiled.reports();
    }

    TEST(ObScriptLenient, a_declared_command_still_takes_a_form_argument_in_strict_mode)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(fixture.registerStub("CountOf", "r", openeden::script::ValueKind::Integer));
        const ScriptId script = fixture.compileOk("Declared", wrap("Set number to CountOf Crate"));
        EXPECT_NE(script, 0u);
    }

    // ---- a command named like a record --------------------------------------------------------------------

    TEST(ObScriptLenient, a_command_spelled_like_an_editor_id_is_a_command_at_the_start_of_a_line)
    {
        Compiled compiled(wrap("Crate 1\nGold001\nSet number to 3"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("number"), 3);
    }

    TEST(ObScriptLenient, a_command_spelled_like_an_editor_id_followed_by_a_form_argument)
    {
        Compiled compiled(wrap("MyQuest Crate\nSet number to 4"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("number"), 4);
    }

    TEST(ObScriptLenient, a_form_before_a_dot_or_an_arrow_is_still_a_reference)
    {
        Compiled compiled(
            wrap("Crate.AddItem Gold001 5\nCrate->AddItem Gold001 2\nSet number to Crate.GetItemCount Gold001"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("number"), 7);
    }

    TEST(ObScriptLenient, a_form_used_as_a_value_is_still_a_value)
    {
        Compiled compiled(
            wrap("Set target to Crate\nIf ( target == Crate )\n Set flag to 1\nEndIf"), UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("flag"), 1);
    }

    TEST(ObScriptLenient, a_form_with_a_trailing_command_on_the_right_of_to_is_a_command)
    {
        Compiled compiled(wrap("Set number to Crate 5"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("number"), 0);
    }

    TEST(ObScriptLenient, in_strict_mode_a_command_spelled_like_an_editor_id_is_unknown)
    {
        Compiled compiled(wrap("Crate 1"), UnknownFunctionPolicy::Strict);
        EXPECT_FALSE(compiled.mOk);
    }

    // ---- a member after a dot that is not a variable of the owner ------------------------------------------

    TEST(ObScriptLenient, a_member_spelled_like_an_editor_id_is_a_variable_when_the_owner_has_it)
    {
        Compiled compiled(wrap("Set number to MyQuest.Crate\nSet MyQuest.Crate to 12"), UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("number"), 9);
        EXPECT_EQ(compiled.worldVariable(sQuest, "crate"), 12);
    }

    TEST(ObScriptLenient, a_member_spelled_like_an_editor_id_that_the_owner_lacks_is_a_command)
    {
        // Gold001 is a form, and MyQuest has no such variable: "MyQuest.Gold001" is read as a call on MyQuest.
        Compiled compiled(wrap("Set number to MyQuest.Gold001\nSet flag to 3"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("flag"), 3);
    }

    // ---- members the world does not know ------------------------------------------------------------------

    TEST(ObScriptLenient, writing_a_member_the_world_does_not_know_is_a_warning_in_tolerant_mode)
    {
        Compiled compiled(wrap("Set MyQuest.nothing to 1\nSet Crate.stage to 2 + number\nSet flag to 6"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        EXPECT_GE(compiled.mWarnings, 2);
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("flag"), 6);
        EXPECT_EQ(compiled.worldVariableCount(sQuest, "nothing"), 0u);
    }

    TEST(ObScriptLenient, writing_a_member_spelled_like_a_command_is_a_warning_too)
    {
        Compiled compiled(wrap("AddItem Gold001 1\nSet MyQuest.AddItem to 1\nSet MyQuest.end to 2\nSet flag to 6"));
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("flag"), 6);
    }

    TEST(ObScriptLenient, writing_a_member_the_world_does_not_know_is_an_error_in_strict_mode)
    {
        Compiled compiled(wrap("Set MyQuest.nothing to 1"), UnknownFunctionPolicy::Strict);
        EXPECT_FALSE(compiled.mOk);
        EXPECT_NE(compiled.reports().find("Unknown variable"), std::string::npos) << compiled.reports();
    }

    TEST(ObScriptLenient, a_member_the_world_knows_is_written_in_both_modes)
    {
        for (const auto policy : { UnknownFunctionPolicy::Tolerant, UnknownFunctionPolicy::Strict })
        {
            Compiled compiled(wrap("Set MyQuest.stage to 8"), policy);
            ASSERT_TRUE(compiled.mOk) << compiled.reports();
            EXPECT_EQ(compiled.mWarnings, 0);
            ASSERT_TRUE(compiled.run());
            EXPECT_EQ(compiled.worldVariable(sQuest, "stage"), 8);
        }
    }

    // ---- text after the last End ----------------------------------------------------------------------------

    TEST(ObScriptLenient, decoration_after_the_last_end_is_a_warning_in_tolerant_mode)
    {
        const std::string text
            = wrap("Set flag to 2") + "\n;====\n==================\n-----------------\nstray words\n`\n";
        Compiled compiled(text);
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        EXPECT_GE(compiled.mWarnings, 1);
        ASSERT_TRUE(compiled.run());
        EXPECT_EQ(compiled.integer("flag"), 2);
    }

    TEST(ObScriptLenient, text_after_the_last_end_is_an_error_in_strict_mode)
    {
        const std::string text = wrap("Set flag to 2") + "stray words\n";
        Compiled compiled(text, UnknownFunctionPolicy::Strict);
        EXPECT_FALSE(compiled.mOk);
        EXPECT_NE(compiled.reports().find("after the last End"), std::string::npos) << compiled.reports();
    }

    TEST(ObScriptLenient, a_row_of_punctuation_between_or_before_blocks_is_decoration)
    {
        const std::string text
            = "ScriptName Two\nshort x\n==========\nBegin GameMode\nSet x to 1\nEnd\n-----------\n"
              "` `\nBegin MenuMode\nSet x to 2\nEnd\n";
        Compiled compiled(text);
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        EXPECT_GE(compiled.mWarnings, 1);
        ASSERT_TRUE(compiled.run("MenuMode"));
        EXPECT_EQ(compiled.integer("x"), 2);
        ASSERT_TRUE(compiled.run("GameMode"));
        EXPECT_EQ(compiled.integer("x"), 1);
    }

    TEST(ObScriptLenient, a_row_of_punctuation_outside_the_blocks_is_an_error_in_strict_mode)
    {
        Compiled compiled("ScriptName One\nshort x\nBegin GameMode\nSet x to 1\nEnd\n=========\nBegin MenuMode\nEnd\n",
            UnknownFunctionPolicy::Strict);
        EXPECT_FALSE(compiled.mOk);
        EXPECT_NE(compiled.reports().find("decoration"), std::string::npos) << compiled.reports();
    }

    TEST(ObScriptLenient, punctuation_with_a_word_between_blocks_is_still_an_error)
    {
        Compiled compiled(
            "ScriptName One\nshort x\nBegin GameMode\nSet x to 1\nEnd\n==== note ====\nBegin MenuMode\nEnd\n");
        EXPECT_FALSE(compiled.mOk);
    }

    TEST(ObScriptLenient, text_between_blocks_is_still_an_error)
    {
        const std::string text
            = "ScriptName Two\nshort x\nBegin GameMode\nSet x to 1\nEnd\nstray words\nBegin MenuMode\nSet x to "
              "2\nEnd\n";
        Compiled compiled(text);
        EXPECT_FALSE(compiled.mOk);
        EXPECT_NE(compiled.reports().find("outside a Begin"), std::string::npos) << compiled.reports();
    }

    TEST(ObScriptLenient, text_before_the_first_block_is_still_an_error)
    {
        Compiled compiled("ScriptName One\nshort x\nstray words\nBegin GameMode\nSet x to 1\nEnd\n");
        EXPECT_FALSE(compiled.mOk);
    }

    TEST(ObScriptLenient, a_begin_without_an_end_is_still_an_error_even_after_a_block)
    {
        Compiled compiled(
            "ScriptName One\nshort x\nBegin GameMode\nSet x to 1\nEnd\n====\nBegin MenuMode\nSet x to 2\n");
        EXPECT_FALSE(compiled.mOk);
    }

    TEST(ObScriptLenient, a_script_with_only_decoration_after_its_blocks_keeps_the_block_structure)
    {
        const std::string text
            = "ScriptName Two\nshort x\nBegin GameMode\nSet x to 1\nEnd\nBegin MenuMode\nSet x to 2\nEnd\n"
              "================\n";
        Compiled compiled(text);
        ASSERT_TRUE(compiled.mOk) << compiled.reports();
        ASSERT_TRUE(compiled.run("MenuMode"));
        EXPECT_EQ(compiled.integer("x"), 2);
    }
}
