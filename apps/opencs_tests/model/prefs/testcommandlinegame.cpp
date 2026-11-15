// Task 0173 (stage a): Eden Studio's `game` option, declared as the game declares it (apps/openmw/options.cpp: a string
// option with an empty default). Before it, `--game skyrimse` made the editor exit with code 2 and a `game=` line of
// the config files was silently dropped. Written from the spec before the code.
#include "apps/opencs/model/prefs/commandline.hpp"

#include <components/files/configurationmanager.hpp>

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace CSMPrefs
{
    namespace
    {
        CommandLine parse(const std::vector<std::string>& arguments)
        {
            std::vector<const char*> argv{ "eden-studio" };
            for (const std::string& argument : arguments)
            {
                argv.push_back(argument.c_str());
            }
            return parseCommandLine(static_cast<int>(argv.size()), argv.data(), makeEditorOptions());
        }

        TEST(CSMPrefsGameOption, the_editor_declares_a_game_option)
        {
            const boost::program_options::options_description description = makeEditorOptions();
            EXPECT_NE(description.find_nothrow("game", false), nullptr);
        }

        TEST(CSMPrefsGameOption, the_game_defaults_to_empty)
        {
            const CommandLine commandLine = parse({});
            EXPECT_TRUE(commandLine.mErrors.empty());
            ASSERT_EQ(commandLine.mVariables.count("game"), 1u);
            EXPECT_EQ(commandLine.mVariables["game"].as<std::string>(), "");
        }

        TEST(CSMPrefsGameOption, the_game_is_read_from_the_command_line_with_a_space_or_an_equals_sign)
        {
            const CommandLine spaced = parse({ "--game", "skyrimse" });
            EXPECT_TRUE(spaced.mErrors.empty());
            EXPECT_TRUE(spaced.mFileToLoad.empty());
            ASSERT_EQ(spaced.mVariables.count("game"), 1u);
            EXPECT_EQ(spaced.mVariables["game"].as<std::string>(), "skyrimse");

            const CommandLine joined = parse({ "--game=fallout4" });
            EXPECT_TRUE(joined.mErrors.empty());
            EXPECT_EQ(joined.mVariables["game"].as<std::string>(), "fallout4");
        }

        TEST(CSMPrefsGameOption, the_game_does_not_disturb_the_other_options)
        {
            const CommandLine commandLine = parse({ "--game", "skyrimse", "--content", "Skyrim.esm", "--data", "D" });
            EXPECT_TRUE(commandLine.mErrors.empty());
            EXPECT_EQ(commandLine.mVariables["game"].as<std::string>(), "skyrimse");
            EXPECT_EQ(commandLine.mVariables["content"].as<std::vector<std::string>>(),
                std::vector<std::string>{ "Skyrim.esm" });
            EXPECT_EQ(commandLine.mVariables["data"].as<Files::MaybeQuotedPathContainer>().size(), 1u);
        }

        TEST(CSMPrefsGameOption, a_game_line_of_a_config_file_is_no_longer_dropped)
        {
            boost::program_options::variables_map variables;
            std::istringstream config("encoding=win1252\ngame=skyrimse\ncontent=Skyrim.esm\n");
            Files::parseConfig(config, variables, makeEditorOptions());
            ASSERT_EQ(variables.count("game"), 1u);
            EXPECT_EQ(variables["game"].as<std::string>(), "skyrimse");
            EXPECT_EQ(variables["content"].as<std::vector<std::string>>(), std::vector<std::string>{ "Skyrim.esm" });
        }

        TEST(CSMPrefsGameOption, an_option_that_is_still_unknown_is_still_refused)
        {
            const CommandLine commandLine = parse({ "--no-such-option" });
            EXPECT_FALSE(commandLine.mErrors.empty());
        }
    }
}
