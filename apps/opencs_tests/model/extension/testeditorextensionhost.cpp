#include "apps/opencs/model/extension/editorextensionhost.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace CSMExtension
{
    namespace
    {
        const char* const validManifest = R"({
  "format": "openeden.extension", "version": 1, "id": "%ID%", "name": "Test %ID%", "extensionVersion": "1.2.3",
  "kind": "feature", "sdk": { "major": 4, "minor": 0 }, "toolchain": "",
  "library": { "windows": "nothing-here.dll", "linux": "libnothing-here.so" },
  "provides": [], "requires": [], "optional": [], "loadAfter": [], "data": "data", "settings": [],
  "licence": "GPL-3.0-or-later"
})";

        class CSMExtensionEditorHostTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = std::filesystem::temp_directory_path() / (std::string("eden-studio-ext-") + info->name());
                std::filesystem::remove_all(mDir);
                std::filesystem::create_directories(mDir);
            }

            void TearDown() override { std::filesystem::remove_all(mDir); }

            void addExtension(const std::string& folder, const std::string& id)
            {
                std::filesystem::create_directories(mDir / folder);
                std::string text = validManifest;
                for (std::size_t at; (at = text.find("%ID%")) != std::string::npos;)
                    text.replace(at, 4, id);
                std::ofstream(mDir / folder / "extension.json") << text;
            }

            void addBrokenFolder(const std::string& folder)
            {
                std::filesystem::create_directories(mDir / folder);
                std::ofstream(mDir / folder / "extension.json") << "{ this is not json";
            }

            EditorExtensionHost::Config config(std::vector<std::string> enabled = {}) const
            {
                return { { mDir }, std::move(enabled) };
            }

            std::filesystem::path mDir;
        };

        TEST_F(CSMExtensionEditorHostTest, noExtensionsAtAll)
        {
            EditorExtensionHost host(config());
            host.start();
            EXPECT_TRUE(host.extensions().empty());
            EXPECT_FALSE(host.isRunning());
            EXPECT_EQ(host.summary(), "0 of 0 extensions active");
        }

        TEST_F(CSMExtensionEditorHostTest, installedButNotEnabledIsListedAndNotLoaded)
        {
            addExtension("alpha", "alpha");
            EditorExtensionHost host(config());
            host.start();

            const auto list = host.extensions();
            ASSERT_EQ(list.size(), 1u);
            EXPECT_EQ(list[0].mId, "alpha");
            EXPECT_EQ(list[0].mName, "Test alpha");
            EXPECT_EQ(list[0].mVersion, "1.2.3");
            EXPECT_FALSE(list[0].mEnabled);
            EXPECT_EQ(list[0].mState, ExtensionInfo::State::Installed);
        }

        TEST_F(CSMExtensionEditorHostTest, enabledWithoutItsLibraryFailsWithAReason)
        {
            addExtension("alpha", "alpha");
            addExtension("beta", "beta");
            EditorExtensionHost host(config({ "alpha" }));
            host.start();

            const auto list = host.extensions();
            ASSERT_EQ(list.size(), 2u);
            for (const ExtensionInfo& info : list)
            {
                if (info.mId == "alpha")
                {
                    EXPECT_TRUE(info.mEnabled);
                    EXPECT_EQ(info.mState, ExtensionInfo::State::Failed);
                    EXPECT_FALSE(info.mMessage.empty());
                }
                else
                {
                    EXPECT_EQ(info.mState, ExtensionInfo::State::Installed); // beta was not asked for
                }
            }
            EXPECT_EQ(host.summary(), "0 of 2 extensions active, 1 failed");
        }

        TEST_F(CSMExtensionEditorHostTest, enabledButNotInstalledFails)
        {
            EditorExtensionHost host(config({ "ghost" }));
            const auto list = host.extensions();
            ASSERT_EQ(list.size(), 1u);
            EXPECT_EQ(list[0].mId, "ghost");
            EXPECT_EQ(list[0].mState, ExtensionInfo::State::Failed);
            EXPECT_NE(list[0].mMessage.find("installed"), std::string::npos);
        }

        TEST_F(CSMExtensionEditorHostTest, aBrokenManifestIsReportedAndDoesNotStopTheOthers)
        {
            addBrokenFolder("broken");
            addExtension("alpha", "alpha");
            EditorExtensionHost host(config());

            const auto list = host.extensions();
            ASSERT_EQ(list.size(), 2u);
            // Not asked for: listed with its reason, but not counted as a failure.
            int installed = 0;
            for (const ExtensionInfo& info : list)
            {
                installed += info.mState == ExtensionInfo::State::Installed;
                if (info.mId != "alpha")
                {
                    EXPECT_FALSE(info.mMessage.empty());
                }
            }
            EXPECT_EQ(installed, 2);

            // Asked for: a failure.
            EditorExtensionHost asked(config({ "broken" }));
            int failed = 0;
            for (const ExtensionInfo& info : asked.extensions())
                failed += info.mState == ExtensionInfo::State::Failed;
            EXPECT_EQ(failed, 1);
        }

        TEST_F(CSMExtensionEditorHostTest, stopAndStartAreSafeToRepeat)
        {
            EditorExtensionHost host(config());
            host.stop();
            host.start();
            host.start();
            host.stop();
            host.stop();
            SUCCEED();
        }
    }
}
