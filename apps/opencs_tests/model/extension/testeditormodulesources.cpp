// Task 0031, stage A: EditorExtensionHost::editorModuleSources(), written from the spec alone.
//
// Assumptions: editorModuleSources() is a const member returning std::vector<CSMModule::ModuleSource> (declared in
// apps/opencs/model/module/modulehost.hpp, included by editorextensionhost.hpp or here); its members are mId, mLibrary,
// mOrigin. It may be called before or after start().
//
// Limit: an extension is only Active when its library loads and starts, and this test cannot build one (the manifests
// here name libraries that do not exist, like testeditorextensionhost.cpp). So the positive case (active extensions
// that have an editor entry yield a source, in load order, library resolved next to the manifest) is NOT covered
// here; it needs a real extension library and belongs to the check in the acceptance list. What is covered: the empty
// case and every case that must yield nothing.
#include "apps/opencs/model/extension/editorextensionhost.hpp"
#include "apps/opencs/model/module/modulehost.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace CSMExtension
{
    namespace
    {
        const char* const manifestWithEditor = R"({
  "format": "openeden.extension", "version": 1, "id": "%ID%", "name": "Test %ID%", "extensionVersion": "1.2.3",
  "kind": "feature", "sdk": { "major": 4, "minor": 0 }, "toolchain": "",
  "library": { "windows": "nothing-here.dll", "linux": "libnothing-here.so" },
  "provides": [], "requires": [], "optional": [], "loadAfter": [], "data": "data", "settings": [],
  "licence": "GPL-3.0-or-later",
  "editor": { "library": "%ID%_editor.dll", "editorApi": { "major": 1, "minor": 0 } }
})";

        const char* const manifestWithoutEditor = R"({
  "format": "openeden.extension", "version": 1, "id": "%ID%", "name": "Test %ID%", "extensionVersion": "1.2.3",
  "kind": "feature", "sdk": { "major": 4, "minor": 0 }, "toolchain": "",
  "library": { "windows": "nothing-here.dll", "linux": "libnothing-here.so" },
  "provides": [], "requires": [], "optional": [], "loadAfter": [], "data": "data", "settings": [],
  "licence": "GPL-3.0-or-later"
})";

        class CSMExtensionEditorModuleSourcesTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = std::filesystem::temp_directory_path() / (std::string("eden-studio-src-") + info->name());
                std::filesystem::remove_all(mDir);
                std::filesystem::create_directories(mDir);
            }

            void TearDown() override { std::filesystem::remove_all(mDir); }

            void addExtension(const std::string& id, bool withEditor)
            {
                std::filesystem::create_directories(mDir / id);
                std::string text = withEditor ? manifestWithEditor : manifestWithoutEditor;
                for (std::size_t at; (at = text.find("%ID%")) != std::string::npos;)
                    text.replace(at, 4, id);
                std::ofstream(mDir / id / "extension.json") << text;
            }

            EditorExtensionHost::Config config(std::vector<std::string> enabled = {}) const
            {
                return { { mDir }, std::move(enabled) };
            }

            std::filesystem::path mDir;
        };

        TEST_F(CSMExtensionEditorModuleSourcesTest, noExtensionsGivesNoSources)
        {
            EditorExtensionHost host(config());
            host.start();
            EXPECT_TRUE(host.editorModuleSources().empty());
        }

        TEST_F(CSMExtensionEditorModuleSourcesTest, beforeStartGivesNoSources)
        {
            addExtension("alpha", true);
            EditorExtensionHost host(config({ "alpha" }));
            EXPECT_TRUE(host.editorModuleSources().empty());
        }

        TEST_F(CSMExtensionEditorModuleSourcesTest, installedButNotEnabledGivesNoSource)
        {
            addExtension("alpha", true);
            EditorExtensionHost host(config());
            host.start();
            ASSERT_EQ(host.extensions().size(), 1u);
            EXPECT_EQ(host.extensions()[0].mState, ExtensionInfo::State::Installed);
            EXPECT_TRUE(host.editorModuleSources().empty());
        }

        TEST_F(CSMExtensionEditorModuleSourcesTest, enabledButFailedToLoadGivesNoSource)
        {
            // The runtime library "nothing-here" does not exist, so the extension is Failed, not Active.
            addExtension("alpha", true);
            EditorExtensionHost host(config({ "alpha" }));
            host.start();
            ASSERT_EQ(host.extensions().size(), 1u);
            EXPECT_EQ(host.extensions()[0].mState, ExtensionInfo::State::Failed);
            EXPECT_TRUE(host.editorModuleSources().empty());
        }

        TEST_F(CSMExtensionEditorModuleSourcesTest, extensionsWithoutAnEditorEntryGiveNoSource)
        {
            addExtension("alpha", false);
            addExtension("beta", false);
            EditorExtensionHost host(config({ "alpha", "beta" }));
            host.start();
            EXPECT_TRUE(host.editorModuleSources().empty());
        }

        TEST_F(CSMExtensionEditorModuleSourcesTest, stoppingTheHostLeavesNoSources)
        {
            addExtension("alpha", true);
            EditorExtensionHost host(config({ "alpha" }));
            host.start();
            host.stop();
            EXPECT_TRUE(host.editorModuleSources().empty());
        }
    }
}
