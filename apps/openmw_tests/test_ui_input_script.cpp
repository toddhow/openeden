// Task 0111: the parser of the synthetic input script (a test aid, apps/openmw/uiinputscript.hpp). Pure.
#include "apps/openmw/uiinputscript.hpp"

#include <gtest/gtest.h>

namespace
{
    using OMW::UiInputScript;

    TEST(UiInputScriptParse, commands_with_frames_and_arguments)
    {
        const auto commands = UiInputScript::parse("# a comment\n\n10 mouse 40 50\n12 down L 40 50\n13 up L 40 50\n");
        ASSERT_EQ(commands.size(), 3u);
        EXPECT_EQ(commands[0].mFrame, 10u);
        EXPECT_EQ(commands[0].mName, "mouse");
        ASSERT_EQ(commands[0].mArguments.size(), 2u);
        EXPECT_EQ(commands[0].mArguments[1], "50");
        EXPECT_EQ(commands[1].mName, "down");
        EXPECT_EQ(commands[1].mArguments[0], "L");
    }

    TEST(UiInputScriptParse, the_text_command_keeps_the_rest_of_the_line)
    {
        const auto commands = UiInputScript::parse("5 text hello world\r\n");
        ASSERT_EQ(commands.size(), 1u);
        ASSERT_EQ(commands[0].mArguments.size(), 1u);
        EXPECT_EQ(commands[0].mArguments[0], "hello world");
    }

    TEST(UiInputScriptParse, edit_takes_the_last_two_words_as_numbers)
    {
        const auto commands = UiInputScript::parse("7 edit ni hao 2 0\n");
        ASSERT_EQ(commands.size(), 1u);
        ASSERT_EQ(commands[0].mArguments.size(), 3u);
        EXPECT_EQ(commands[0].mArguments[0], "ni hao");
        EXPECT_EQ(commands[0].mArguments[1], "2");
        EXPECT_EQ(commands[0].mArguments[2], "0");
    }

    TEST(UiInputScriptParse, an_edit_with_no_text_ends_a_composition)
    {
        const auto commands = UiInputScript::parse("7 edit 0 0\n");
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0].mArguments[0], "");
    }

    TEST(UiInputScriptParse, commands_are_ordered_by_frame_keeping_the_file_order_of_equal_frames)
    {
        const auto commands = UiInputScript::parse("20 log b\n10 log a\n20 log c\n");
        ASSERT_EQ(commands.size(), 3u);
        EXPECT_EQ(commands[0].mArguments[0], "a");
        EXPECT_EQ(commands[1].mArguments[0], "b");
        EXPECT_EQ(commands[2].mArguments[0], "c");
    }

    TEST(UiInputScriptParse, bad_lines_are_skipped_and_named)
    {
        std::vector<std::string> errors;
        const auto commands = UiInputScript::parse("x mouse 1 2\n3\n4 mouse 1 2\n5 edit onlyone\n", &errors);
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0].mFrame, 4u);
        EXPECT_EQ(errors.size(), 3u);
    }

    TEST(UiInputScriptParse, a_frame_with_trailing_junk_is_refused)
    {
        std::vector<std::string> errors;
        EXPECT_TRUE(UiInputScript::parse("12abc mouse 1 2\n", &errors).empty());
        EXPECT_EQ(errors.size(), 1u);
    }
}
