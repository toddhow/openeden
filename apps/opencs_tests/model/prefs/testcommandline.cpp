#include "apps/opencs/model/prefs/commandline.hpp"

#include <components/files/configurationmanager.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace CSMPrefs
{
    namespace
    {
        // Eden Studio's command line, which OpenMW-CS never parsed (task 0024 fix, 2026-11-04): the editor took argv[1]
        // as the file to open, so `--replace=config` became a file name and the real user folder was used.
        class CSMPrefsCommandLineTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto* testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = std::filesystem::temp_directory_path() / (std::string("eden-studio-cmd-") + testInfo->name());
                std::filesystem::remove_all(mDir);
                std::filesystem::create_directories(mDir);
            }

            void TearDown() override { std::filesystem::remove_all(mDir); }

            CommandLine parse(const std::vector<std::string>& arguments) const
            {
                std::vector<const char*> argv{ "eden-studio" };
                for (const std::string& argument : arguments)
                    argv.push_back(argument.c_str());
                return parseCommandLine(static_cast<int>(argv.size()), argv.data(), makeEditorOptions());
            }

            static bool mentions(const CommandLine& commandLine, const std::string& text)
            {
                for (const std::string& error : commandLine.mErrors)
                {
                    if (error.find(text) != std::string::npos)
                        return true;
                }
                return false;
            }

            std::filesystem::path mDir;
        };

        TEST_F(CSMPrefsCommandLineTest, the_editor_options_include_the_games_configuration_options)
        {
            const boost::program_options::options_description description = makeEditorOptions();
            for (const char* name : { "data", "data-local", "content", "encoding", "extension", "fallback-archive",
                     "fallback", "config", "replace", "user-data", "resources", "help" })
            {
                EXPECT_NE(description.find_nothrow(name, false), nullptr) << name;
            }
        }

        TEST_F(CSMPrefsCommandLineTest, no_arguments_give_the_defaults_and_no_file)
        {
            const CommandLine commandLine = parse({});
            EXPECT_TRUE(commandLine.mErrors.empty());
            EXPECT_FALSE(commandLine.mHelp);
            EXPECT_TRUE(commandLine.mFileToLoad.empty());
            ASSERT_EQ(commandLine.mVariables.count("encoding"), 1u);
            EXPECT_EQ(commandLine.mVariables["encoding"].as<std::string>(), "win1252");
        }

        TEST_F(CSMPrefsCommandLineTest, the_config_options_are_read_and_not_taken_as_a_file)
        {
            const CommandLine commandLine
                = parse({ "--replace=config", "--config", "C:/scratch/config", "--user-data", "C:/scratch/user" });
            EXPECT_TRUE(commandLine.mErrors.empty());
            EXPECT_TRUE(commandLine.mFileToLoad.empty());
            ASSERT_EQ(commandLine.mVariables.count("replace"), 1u);
            EXPECT_EQ(
                commandLine.mVariables["replace"].as<std::vector<std::string>>(), std::vector<std::string>{ "config" });
            const auto configs = commandLine.mVariables["config"].as<Files::MaybeQuotedPathContainer>();
            ASSERT_EQ(configs.size(), 1u);
            EXPECT_EQ(std::filesystem::path(configs[0]), std::filesystem::path("C:/scratch/config"));
            EXPECT_EQ(std::filesystem::path(commandLine.mVariables["user-data"].as<Files::MaybeQuotedPath>()),
                std::filesystem::path("C:/scratch/user"));
        }

        TEST_F(CSMPrefsCommandLineTest, data_and_content_options_add_up)
        {
            const CommandLine commandLine
                = parse({ "--data", "A", "--data", "B", "--content", "a.esm", "--content", "b.esp" });
            EXPECT_TRUE(commandLine.mErrors.empty());
            EXPECT_EQ(commandLine.mVariables["data"].as<Files::MaybeQuotedPathContainer>().size(), 2u);
            EXPECT_EQ(commandLine.mVariables["content"].as<std::vector<std::string>>(),
                (std::vector<std::string>{ "a.esm", "b.esp" }));
        }

        TEST_F(CSMPrefsCommandLineTest, an_existing_file_first_or_after_the_end_of_options_is_the_file_to_load)
        {
            const std::filesystem::path file = mDir / "Test.omwaddon";
            std::ofstream(file, std::ios::binary) << "x";

            const CommandLine first = parse({ file.string(), "--replace=config", "--config", mDir.string() });
            EXPECT_TRUE(first.mErrors.empty());
            EXPECT_EQ(first.mFileToLoad, file);

            const CommandLine afterEnd = parse({ "--replace=config", "--config", mDir.string(), "--", file.string() });
            EXPECT_TRUE(afterEnd.mErrors.empty());
            EXPECT_EQ(afterEnd.mFileToLoad, file);
        }

        // The list options take every following word, as the game's do: a file written after one is refused with a
        // message, never opened as a config folder or read as an option name.
        TEST_F(CSMPrefsCommandLineTest, a_file_swallowed_by_a_list_option_is_refused_with_a_hint)
        {
            const std::filesystem::path file = mDir / "Test.omwaddon";
            std::ofstream(file, std::ios::binary) << "x";

            const CommandLine afterReplace = parse({ "--replace=config", file.string() });
            EXPECT_FALSE(afterReplace.mErrors.empty());
            EXPECT_TRUE(mentions(afterReplace, "Test.omwaddon"));
            EXPECT_TRUE(afterReplace.mFileToLoad.empty());

            const CommandLine afterConfig = parse({ "--config", mDir.string(), file.string() });
            EXPECT_FALSE(afterConfig.mErrors.empty());
            EXPECT_TRUE(mentions(afterConfig, "Test.omwaddon"));
        }

        TEST_F(CSMPrefsCommandLineTest, a_positional_argument_that_is_not_a_file_is_refused)
        {
            const std::filesystem::path missing = mDir / "Missing.esp";
            const CommandLine missingFile = parse({ missing.string() });
            EXPECT_FALSE(missingFile.mErrors.empty());
            EXPECT_TRUE(mentions(missingFile, "Missing.esp"));
            EXPECT_TRUE(missingFile.mFileToLoad.empty());

            const CommandLine folder = parse({ mDir.string() });
            EXPECT_FALSE(folder.mErrors.empty());
            EXPECT_TRUE(folder.mFileToLoad.empty());
        }

        TEST_F(CSMPrefsCommandLineTest, two_positional_files_are_refused)
        {
            const std::filesystem::path first = mDir / "A.esp";
            const std::filesystem::path second = mDir / "B.esp";
            std::ofstream(first, std::ios::binary) << "x";
            std::ofstream(second, std::ios::binary) << "x";
            const CommandLine commandLine = parse({ first.string(), second.string() });
            EXPECT_FALSE(commandLine.mErrors.empty());
            EXPECT_TRUE(commandLine.mFileToLoad.empty());
        }

        TEST_F(CSMPrefsCommandLineTest, an_unknown_option_is_refused_with_its_name)
        {
            const CommandLine commandLine = parse({ "--frobnicate", "3" });
            EXPECT_FALSE(commandLine.mErrors.empty());
            EXPECT_TRUE(mentions(commandLine, "frobnicate"));
        }

        TEST_F(CSMPrefsCommandLineTest, help_is_recognised)
        {
            const CommandLine commandLine = parse({ "--help" });
            EXPECT_TRUE(commandLine.mErrors.empty());
            EXPECT_TRUE(commandLine.mHelp);
        }

        // The configuration manager must then use the folders the command line names, as the game's does.
        TEST_F(CSMPrefsCommandLineTest, the_configuration_manager_uses_the_config_and_user_data_given)
        {
            Files::ConfigurationManager cfgMgr(true);
            if (!std::filesystem::exists(cfgMgr.getLocalPath() / "openmw.cfg"))
                GTEST_SKIP() << "no openmw.cfg beside the test program: the configuration manager reads none";

            const std::filesystem::path config = mDir / "config";
            const std::filesystem::path user = mDir / "user";
            std::filesystem::create_directories(config);
            std::ofstream(config / "openmw.cfg", std::ios::binary) << "encoding=win1251\n";

            CommandLine commandLine
                = parse({ "--replace=config", "--config", config.string(), "--user-data", user.string() });
            ASSERT_TRUE(commandLine.mErrors.empty());
            const boost::program_options::options_description description = makeEditorOptions();
            cfgMgr.readConfiguration(commandLine.mVariables, description, true);

            EXPECT_EQ(cfgMgr.getUserConfigPath(), config);
            EXPECT_EQ(cfgMgr.getUserDataPath(), user);
            EXPECT_EQ(commandLine.mVariables["encoding"].as<std::string>(), "win1251");
        }
    }
}
