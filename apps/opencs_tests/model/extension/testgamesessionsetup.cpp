// Task 0173 (stage c): what a game pack needs before the editor host starts it, the session's VFS and content list
// (apps/opencs/model/extension/gamesessionsetup.hpp). Written from the header's comments before the code. The pack is
// the real shared library of testgamepack/testgamepack.cpp, which lists the content names it was given in its cell.
#include "apps/opencs/model/extension/gamesessionsetup.hpp"

#include "apps/opencs/model/extension/editorextensionhost.hpp"

#include <components/worldmodel/store.hpp>

#include "apps/components_tests/contentsource/pluginbuilder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace CSMExtension
{
    namespace
    {
        namespace fs = std::filesystem;

        const std::string kPack = EDEN_STUDIO_TEST_GAME_PACK_ID;

        const char* const packManifest = R"({
  "format": "openeden.extension", "version": 2, "id": "%ID%", "name": "Test game pack", "extensionVersion": "1.2.3",
  "kind": "game", "sdk": { "major": 4, "minor": 0 }, "toolchain": "",
  "library": { "windows": "%LIB%", "linux": "%LIB%" },
  "provides": [], "requires": [], "optional": [], "loadAfter": [], "data": "data", "settings": [],
  "licence": "GPL-3.0-or-later"
})";

        class GameSessionSetupTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = fs::temp_directory_path() / (std::string("eden-studio-gss-") + info->name());
                fs::remove_all(mDir);
                fs::create_directories(mDir / "packs");
                fs::create_directories(mDir / "Data");
            }

            void TearDown() override
            {
                std::error_code ec;
                fs::remove_all(mDir, ec);
            }

            void installPack()
            {
                const fs::path library = EDEN_STUDIO_TEST_GAME_PACK_PATH;
                ASSERT_TRUE(fs::exists(library)) << library;
                fs::create_directories(mDir / "packs" / kPack);
                fs::copy_file(
                    library, mDir / "packs" / kPack / library.filename(), fs::copy_options::overwrite_existing);
                std::string text = packManifest;
                const auto replace = [&text](const std::string& from, const std::string& to) {
                    for (std::size_t at; (at = text.find(from)) != std::string::npos;)
                    {
                        text.replace(at, from.size(), to);
                    }
                };
                replace("%ID%", kPack);
                replace("%LIB%", library.filename().string());
                std::ofstream(mDir / "packs" / kPack / "extension.json", std::ios::binary) << text;
            }

            void writeData(const std::string& name, const std::string& bytes)
            {
                std::ofstream(mDir / "Data" / name, std::ios::binary) << bytes;
            }

            EditorExtensionHost::Config config(const std::string& gamePack) const
            {
                EditorExtensionHost::Config result;
                result.mSearchDirs = { mDir / "packs" };
                result.mGamePackId = gamePack;
                return result;
            }

            Files::PathContainer dataDirs() const { return { mDir / "Data" }; }

            fs::path mDir;
        };

        TEST_F(GameSessionSetupTest, withNoGamePackNothingIsBuilt)
        {
            GameSessionSetup setup;
            EditorExtensionHost host(config(""));
            setup.prepare(host, dataDirs(), {}, { "Skyrim.esm" }, ToUTF8::WINDOWS_1252);
            EXPECT_EQ(setup.vfs(), nullptr);
            EXPECT_TRUE(setup.problems().empty());
        }

        TEST_F(GameSessionSetupTest, aBuiltInGamePackBuildsNothing)
        {
            GameSessionSetup setup;
            EditorExtensionHost host(config("morrowind"));
            setup.prepare(host, dataDirs(), {}, { "Morrowind.esm" }, ToUTF8::WINDOWS_1252);
            EXPECT_EQ(setup.vfs(), nullptr);
            EXPECT_TRUE(setup.problems().empty());
            EXPECT_EQ(host.neutralWorldStore(), nullptr);
        }

        TEST_F(GameSessionSetupTest, aPackThatIsNotInstalledBuildsNothing)
        {
            GameSessionSetup setup;
            EditorExtensionHost host(config("skyrimse"));
            ASSERT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Failed);
            setup.prepare(host, dataDirs(), {}, { "Skyrim.esm" }, ToUTF8::WINDOWS_1252);
            EXPECT_EQ(setup.vfs(), nullptr);
        }

        TEST_F(GameSessionSetupTest, aLoadedPackGetsAVfsAndTheContentItsLoaderAccepts)
        {
            installPack();
            writeData("Skyrim.esm", ContentTests::tes4Header(1.7f, 1, "a", "d", 0, {}).str());
            writeData("Update.esm", ContentTests::tes4Header(1.7f, 1, "a", "d", 0, { { "Skyrim.esm", 0 } }).str());
            writeData("Old.esm", ContentTests::tes3Header(1.3f, 1, "a", "d", 0, {}).str());

            // Declared first: the setup owns the VFS and must outlive the host.
            GameSessionSetup setup;
            EditorExtensionHost host(config(kPack));
            ASSERT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Loaded);
            setup.prepare(host, dataDirs(), {}, { "Skyrim.esm", "Old.esm", "Update.esm" }, ToUTF8::WINDOWS_1252);
            EXPECT_NE(setup.vfs(), nullptr);
            EXPECT_TRUE(setup.problems().empty());

            host.start();
            ASSERT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Active) << host.gamePackFailure();
            ASSERT_NE(host.neutralWorldStore(), nullptr);
            std::string error;
            const openeden::sdk::game::WorldCell* const cell = host.neutralWorldStore()->cell(kPack, 1, error);
            ASSERT_NE(cell, nullptr) << error;
            // The Morrowind plugin is read and left out, the order of the list is kept
            EXPECT_EQ(cell->editorId, "Content:Skyrim.esm|Update.esm");
        }

        TEST_F(GameSessionSetupTest, aNameThatIsNotInAnyDataFolderIsReportedAndTheRestGoesOn)
        {
            installPack();
            writeData("Skyrim.esm", ContentTests::tes4Header(1.7f, 1, "a", "d", 0, {}).str());
            writeData("Broken.esp", "not a plugin");

            GameSessionSetup setup;
            EditorExtensionHost host(config(kPack));
            setup.prepare(host, dataDirs(), {}, { "Skyrim.esm", "Missing.esp", "Broken.esp" }, ToUTF8::WINDOWS_1252);
            const auto mentions = [&setup](const std::string& name) {
                return std::any_of(setup.problems().begin(), setup.problems().end(),
                    [&name](const std::string& problem) { return problem.find(name) != std::string::npos; });
            };
            EXPECT_TRUE(mentions("Missing.esp"));
            EXPECT_TRUE(mentions("Broken.esp"));
            EXPECT_FALSE(mentions("Skyrim.esm"));

            host.start();
            ASSERT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Active) << host.gamePackFailure();
            std::string error;
            const openeden::sdk::game::WorldCell* const cell = host.neutralWorldStore()->cell(kPack, 1, error);
            ASSERT_NE(cell, nullptr) << error;
            EXPECT_EQ(cell->editorId, "Content:Skyrim.esm");
        }

        TEST_F(GameSessionSetupTest, aSecondPrepareReplacesTheProblemsOfTheFirst)
        {
            installPack();
            writeData("Skyrim.esm", ContentTests::tes4Header(1.7f, 1, "a", "d", 0, {}).str());
            GameSessionSetup setup;
            EditorExtensionHost host(config(kPack));
            setup.prepare(host, dataDirs(), {}, { "Missing.esp" }, ToUTF8::WINDOWS_1252);
            ASSERT_FALSE(setup.problems().empty());
            setup.prepare(host, dataDirs(), {}, { "Skyrim.esm" }, ToUTF8::WINDOWS_1252);
            EXPECT_TRUE(setup.problems().empty());
        }
    }
}
