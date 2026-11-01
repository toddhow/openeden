#include "apps/opencs/model/prefs/configmigration.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace CSMPrefs
{
    namespace
    {
        // A scratch folder that is removed with the test.
        class CSMPrefsConfigMigrationTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = std::filesystem::temp_directory_path() / (std::string("eden-studio-test-") + info->name());
                std::filesystem::remove_all(mDir);
                std::filesystem::create_directories(mDir);
            }

            void TearDown() override { std::filesystem::remove_all(mDir); }

            void write(const char* name, const std::string& text) const
            {
                std::ofstream(mDir / name, std::ios::binary) << text;
            }

            std::string read(const char* name) const
            {
                std::ifstream in(mDir / name, std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                return ss.str();
            }

            bool exists(const char* name) const { return std::filesystem::exists(mDir / name); }

            std::filesystem::path mDir;
        };

        TEST_F(CSMPrefsConfigMigrationTest, namesAreTheDocumentedOnes)
        {
            EXPECT_STREQ(configFileName, "eden-studio.cfg");
            EXPECT_STREQ(legacyConfigFileName, "openmw-cs.cfg");
        }

        TEST_F(CSMPrefsConfigMigrationTest, copiesLegacyFileWhenNewOneIsAbsent)
        {
            write("openmw-cs.cfg", "[Windows]\nmainwindow-scrollbar = 1\n");

            EXPECT_TRUE(migrateLegacyConfig(mDir));
            ASSERT_TRUE(exists("eden-studio.cfg"));
            EXPECT_EQ(read("eden-studio.cfg"), "[Windows]\nmainwindow-scrollbar = 1\n");
        }

        TEST_F(CSMPrefsConfigMigrationTest, neverChangesTheLegacyFile)
        {
            write("openmw-cs.cfg", "[Windows]\nmainwindow-scrollbar = 1\n");
            const auto before = std::filesystem::last_write_time(mDir / "openmw-cs.cfg");

            migrateLegacyConfig(mDir);
            // Later saves go to the new file only.
            write("eden-studio.cfg", "[Windows]\nmainwindow-scrollbar = 0\n");
            migrateLegacyConfig(mDir);

            EXPECT_EQ(read("openmw-cs.cfg"), "[Windows]\nmainwindow-scrollbar = 1\n");
            EXPECT_EQ(std::filesystem::last_write_time(mDir / "openmw-cs.cfg"), before);
        }

        TEST_F(CSMPrefsConfigMigrationTest, readsTheLegacyFileOnlyOnce)
        {
            write("openmw-cs.cfg", "old\n");
            EXPECT_TRUE(migrateLegacyConfig(mDir));
            write("eden-studio.cfg", "changed by the user\n");

            EXPECT_FALSE(migrateLegacyConfig(mDir));
            EXPECT_EQ(read("eden-studio.cfg"), "changed by the user\n");
        }

        TEST_F(CSMPrefsConfigMigrationTest, leavesAnExistingNewFileAlone)
        {
            write("openmw-cs.cfg", "old\n");
            write("eden-studio.cfg", "new\n");

            EXPECT_FALSE(migrateLegacyConfig(mDir));
            EXPECT_EQ(read("eden-studio.cfg"), "new\n");
        }

        TEST_F(CSMPrefsConfigMigrationTest, doesNothingWhenNeitherFileExists)
        {
            EXPECT_FALSE(migrateLegacyConfig(mDir));
            EXPECT_FALSE(exists("eden-studio.cfg"));
            EXPECT_FALSE(exists("openmw-cs.cfg"));
        }

        TEST_F(CSMPrefsConfigMigrationTest, doesNothingWhenTheFolderIsMissing)
        {
            EXPECT_FALSE(migrateLegacyConfig(mDir / "no-such-folder"));
        }
    }
}
