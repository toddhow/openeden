// Task 0173 (stage c): Eden Studio's extension host gets a game pack slot. It loads the session's game pack the way the
// game does (GameSession::classify, one pack per session, a built-in pack standing for Morrowind) and, for a pack that
// is not built in, enables world source reading and hands it the session's content list. Written from the spec and the
// header's comments before the code. The pack under test is a real shared library (testgamepack/testgamepack.cpp).
#include "apps/opencs/model/extension/editorextensionhost.hpp"

#include <components/vfs/manager.hpp>
#include <components/worldmodel/store.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace CSMExtension
{
    namespace
    {
        namespace fs = std::filesystem;

        const std::string kPack = EDEN_STUDIO_TEST_GAME_PACK_ID;

        void setEnvironment(const char* name, const char* value)
        {
#ifdef _WIN32
            _putenv_s(name, value);
#else
            setenv(name, value, 1);
#endif
        }

        const char* const featureManifest = R"({
  "format": "openeden.extension", "version": 2, "id": "%ID%", "name": "Test %ID%", "extensionVersion": "1.2.3",
  "kind": "%KIND%", "sdk": { "major": 4, "minor": 0 }, "toolchain": "",
  "library": { "windows": "%LIB%", "linux": "%LIB%" },
  "provides": [], "requires": [], "optional": [], "loadAfter": [], "data": "data", "settings": [],
  "licence": "GPL-3.0-or-later"
})";

        class EditorHostGamePackTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = fs::temp_directory_path() / (std::string("eden-studio-gp-") + info->name());
                fs::remove_all(mDir);
                fs::create_directories(mDir);
                setEnvironment("OPENEDEN_TEST_PACK_THROW", "0");
            }

            void TearDown() override
            {
                setEnvironment("OPENEDEN_TEST_PACK_THROW", "0");
                std::error_code ec;
                fs::remove_all(mDir, ec);
            }

            void writeManifest(const std::string& folder, const std::string& id, const std::string& kind,
                const std::string& library, const std::string& name = {})
            {
                fs::create_directories(mDir / folder);
                std::string text = featureManifest;
                const auto replace = [&text](const std::string& from, const std::string& to) {
                    for (std::size_t at; (at = text.find(from)) != std::string::npos;)
                    {
                        text.replace(at, from.size(), to);
                    }
                };
                replace("%ID%", id);
                replace("%KIND%", kind);
                replace("%LIB%", library);
                if (!name.empty())
                {
                    replace("Test " + id, name);
                }
                std::ofstream(mDir / folder / "extension.json", std::ios::binary) << text;
            }

            // The real test pack, copied next to a manifest of kind "game".
            void installPack()
            {
                const fs::path library = EDEN_STUDIO_TEST_GAME_PACK_PATH;
                ASSERT_TRUE(fs::exists(library)) << library;
                fs::create_directories(mDir / kPack);
                fs::copy_file(library, mDir / kPack / library.filename(), fs::copy_options::overwrite_existing);
                writeManifest(kPack, kPack, "game", library.filename().string(), "Test game pack");
            }

            // A game pack of another id whose library does not exist: a refusal never gets as far as loading it.
            void installOtherGamePack(const std::string& id = "othergame")
            {
                writeManifest(id, id, "game", "nothing-here.dll");
            }

            void installFeature(const std::string& id) { writeManifest(id, id, "feature", "nothing-here.dll"); }

            EditorExtensionHost::Config config(const std::string& gamePack, std::vector<std::string> enabled = {}) const
            {
                EditorExtensionHost::Config result;
                result.mSearchDirs = { mDir };
                result.mEnabled = std::move(enabled);
                result.mGamePackId = gamePack;
                return result;
            }

            static const ExtensionInfo* find(const std::vector<ExtensionInfo>& list, const std::string& id)
            {
                for (const ExtensionInfo& info : list)
                {
                    if (info.mId == id)
                    {
                        return &info;
                    }
                }
                return nullptr;
            }

            static std::vector<openeden::extension::SessionContentEntry> contentList()
            {
                return { { "Skyrim.esm", "/data/Skyrim.esm", "TES4", 44 }, { "Old.esm", "/data/Old.esm", "TES3", 0 },
                    { "Update.esm", "/data/Update.esm", "TES4", 44 } };
            }

            // The editor id of the test pack's one cell, which lists the content names the pack was given.
            static std::string cellEditorId(EditorExtensionHost& host)
            {
                NeutralWorld::NeutralWorldStore* const store = host.neutralWorldStore();
                if (store == nullptr)
                {
                    return "<no store>";
                }
                std::string error;
                const openeden::sdk::game::WorldCell* const cell = store->cell(kPack, 1, error);
                return cell == nullptr ? "<no cell: " + error + ">" : cell->editorId;
            }

            fs::path mDir;
        };

        // ---- off means unchanged ----

        TEST_F(EditorHostGamePackTest, withNoGamePackNothingChanges)
        {
            installFeature("alpha");
            EditorExtensionHost host(config(""));
            host.start();
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::None);
            EXPECT_EQ(host.neutralWorldStore(), nullptr);
            EXPECT_TRUE(host.gamePackId().empty());
            EXPECT_TRUE(host.gamePackFailure().empty());
            EXPECT_EQ(host.summary(), "0 of 1 extensions active");
            const auto list = host.extensions();
            ASSERT_EQ(list.size(), 1u);
            EXPECT_EQ(list[0].mId, "alpha");
            EXPECT_FALSE(list[0].mGamePack);
        }

        TEST_F(EditorHostGamePackTest, withNothingAtAllTheSummaryIsAsBefore)
        {
            EditorExtensionHost host(config(""));
            host.start();
            EXPECT_EQ(host.summary(), "0 of 0 extensions active");
            EXPECT_TRUE(host.extensions().empty());
        }

        TEST_F(EditorHostGamePackTest, morrowindIsBuiltInAndLoadsAndStartsNothing)
        {
            installFeature("alpha");
            EditorExtensionHost plain(config(""));
            plain.start();

            EditorExtensionHost host(config("morrowind"));
            host.start();
            EXPECT_EQ(host.gamePackId(), "morrowind");
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::BuiltIn);
            EXPECT_EQ(host.gamePackName(), "Morrowind");
            EXPECT_TRUE(host.gamePackFailure().empty());
            EXPECT_EQ(host.neutralWorldStore(), nullptr);
            EXPECT_EQ(host.summary(), plain.summary());
            EXPECT_EQ(host.extensions().size(), plain.extensions().size());
        }

        TEST_F(EditorHostGamePackTest, morrowindWithNoExtensionsHasTheEmptySummary)
        {
            EditorExtensionHost host(config("morrowind"));
            host.start();
            EXPECT_EQ(host.summary(), "0 of 0 extensions active");
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::BuiltIn);
        }

        TEST_F(EditorHostGamePackTest, contentHandedToAMorrowindHostIsIgnored)
        {
            // Morrowind is built in (Config::mBuiltInGamePacks): nothing consumes a content list.
            EditorExtensionHost host(config("morrowind"));
            host.setSessionContent(contentList(), nullptr);
            host.start();
            EXPECT_EQ(host.neutralWorldStore(), nullptr);
        }

        // ---- a pack that is not installed ----

        TEST_F(EditorHostGamePackTest, aChosenPackThatIsNotInstalledFailsWithAReasonAndTheHostGoesOn)
        {
            installFeature("alpha");
            EditorExtensionHost host(config("skyrimse"));
            EXPECT_EQ(host.gamePackId(), "skyrimse");
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Failed);
            EXPECT_NE(host.gamePackFailure().find("skyrimse"), std::string::npos) << host.gamePackFailure();
            EXPECT_NE(host.gamePackFailure().find("is not installed"), std::string::npos) << host.gamePackFailure();
            EXPECT_EQ(host.gamePackName(), "skyrimse");
            EXPECT_EQ(host.neutralWorldStore(), nullptr);

            EXPECT_NO_THROW(host.start());
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Failed);

            const auto list = host.extensions();
            const ExtensionInfo* const alpha = find(list, "alpha");
            ASSERT_NE(alpha, nullptr);
            EXPECT_EQ(alpha->mState, ExtensionInfo::State::Installed);
            const ExtensionInfo* const row = find(list, "skyrimse");
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->mState, ExtensionInfo::State::Failed);
            EXPECT_NE(row->mMessage.find("installed"), std::string::npos) << row->mMessage;
        }

        // ---- other game packs are refused ----

        TEST_F(EditorHostGamePackTest, anotherGamePackThatIsEnabledIsRefusedWithTheSessionsReason)
        {
            installOtherGamePack();
            EditorExtensionHost host(config("skyrimse", { "othergame" }));
            const auto list = host.extensions();
            const ExtensionInfo* const row = find(list, "othergame");
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->mState, ExtensionInfo::State::Failed);
            EXPECT_NE(row->mMessage.find("this session's game pack is 'skyrimse'"), std::string::npos) << row->mMessage;
        }

        TEST_F(EditorHostGamePackTest, withNoGameLineAnEnabledGamePackIsRefusedForThat)
        {
            installOtherGamePack();
            EditorExtensionHost host(config("", { "othergame" }));
            const auto list = host.extensions();
            const ExtensionInfo* const row = find(list, "othergame");
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->mState, ExtensionInfo::State::Failed);
            EXPECT_NE(row->mMessage.find("no game= line chooses a game pack"), std::string::npos) << row->mMessage;
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::None);
        }

        TEST_F(EditorHostGamePackTest, aGamePackThatIsNotEnabledIsJustInstalled)
        {
            installOtherGamePack();
            EditorExtensionHost host(config("skyrimse"));
            const auto list = host.extensions();
            const ExtensionInfo* const row = find(list, "othergame");
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->mState, ExtensionInfo::State::Installed);
        }

        // ---- the chosen pack, installed ----

        TEST_F(EditorHostGamePackTest, theChosenPackLoadsAtConstructionAndIsListedFirst)
        {
            installPack();
            installFeature("alpha");
            EditorExtensionHost host(config(kPack));
            EXPECT_EQ(host.gamePackId(), kPack);
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Loaded);
            EXPECT_EQ(host.gamePackName(), "Test game pack");
            EXPECT_TRUE(host.gamePackFailure().empty()) << host.gamePackFailure();
            ASSERT_NE(host.neutralWorldStore(), nullptr);
            EXPECT_EQ(host.neutralWorldStore()->sourceCount(), 0u) << "the pack has not started yet";

            const auto list = host.extensions();
            ASSERT_EQ(list.size(), 2u);
            EXPECT_EQ(list[0].mId, kPack);
            EXPECT_TRUE(list[0].mGamePack);
            EXPECT_EQ(list[1].mId, "alpha");
            EXPECT_FALSE(list[1].mGamePack);
        }

        TEST_F(EditorHostGamePackTest, thePackIsEnabledWithoutAnExtensionLine)
        {
            installPack();
            EditorExtensionHost host(config(kPack, {}));
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Loaded);
            const auto rows = host.extensions();
            const ExtensionInfo* const row = find(rows, kPack);
            ASSERT_NE(row, nullptr);
            EXPECT_TRUE(row->mGamePack);
        }

        TEST_F(EditorHostGamePackTest, startingTheHostStartsThePackAndItsWorldSourceIsInTheStore)
        {
            installPack();
            EditorExtensionHost host(config(kPack));
            VFS::Manager vfs;
            host.setSessionContent(contentList(), &vfs);
            host.start();
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Active);
            EXPECT_TRUE(host.gamePackFailure().empty()) << host.gamePackFailure();
            ASSERT_NE(host.neutralWorldStore(), nullptr);
            EXPECT_EQ(host.neutralWorldStore()->sourceCount(), 1u);
            EXPECT_EQ(host.neutralWorldStore()->gameIds(), (std::vector<std::string>{ kPack }));
            EXPECT_DOUBLE_EQ(host.neutralWorldStore()->unitsPerMetre(kPack), 70.0);

            const auto rows = host.extensions();
            const ExtensionInfo* const row = find(rows, kPack);
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->mState, ExtensionInfo::State::Active);
            EXPECT_TRUE(row->mGamePack);
        }

        TEST_F(EditorHostGamePackTest, thePackIsOnlyGivenTheContentItsLoaderAccepts)
        {
            installPack();
            EditorExtensionHost host(config(kPack));
            VFS::Manager vfs;
            host.setSessionContent(contentList(), &vfs);
            host.start();
            // The pack's loader takes TES4 headers only: Old.esm (TES3) never reaches it, and the order is kept.
            EXPECT_EQ(cellEditorId(host), "Content:Skyrim.esm|Update.esm");
        }

        TEST_F(EditorHostGamePackTest, withNoContentListThePackStartsWithAnEmptyOne)
        {
            installPack();
            EditorExtensionHost host(config(kPack));
            host.start();
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Active);
            EXPECT_EQ(cellEditorId(host), "Content:");
        }

        TEST_F(EditorHostGamePackTest, anotherGamePackIsStillRefusedWhileTheChosenOneRuns)
        {
            installPack();
            installOtherGamePack();
            EditorExtensionHost host(config(kPack, { "othergame" }));
            host.start();
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Active);
            const auto rows = host.extensions();
            const ExtensionInfo* const row = find(rows, "othergame");
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->mState, ExtensionInfo::State::Failed);
            EXPECT_NE(row->mMessage.find("this session's game pack is '" + kPack + "'"), std::string::npos)
                << row->mMessage;
        }

        TEST_F(EditorHostGamePackTest, startingTwiceChangesNothing)
        {
            installPack();
            EditorExtensionHost host(config(kPack));
            host.start();
            host.start();
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Active);
            EXPECT_EQ(host.neutralWorldStore()->sourceCount(), 1u);
        }

        TEST_F(EditorHostGamePackTest, stoppingTheHostTakesTheSourceBackOutOfTheStore)
        {
            installPack();
            EditorExtensionHost host(config(kPack));
            host.start();
            NeutralWorld::NeutralWorldStore* const store = host.neutralWorldStore();
            ASSERT_NE(store, nullptr);
            ASSERT_EQ(store->sourceCount(), 1u);
            host.stop();
            EXPECT_EQ(store->sourceCount(), 0u);
        }

        TEST_F(EditorHostGamePackTest, aPackWhoseStartFailsIsFailedWithItsReasonAndTheEditorGoesOn)
        {
            installPack();
            installFeature("alpha");
            setEnvironment("OPENEDEN_TEST_PACK_THROW", "1");
            EditorExtensionHost host(config(kPack));
            EXPECT_NO_THROW(host.start());
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::Failed);
            EXPECT_NE(host.gamePackFailure().find("test game pack asked to fail"), std::string::npos)
                << host.gamePackFailure();
            const auto rows = host.extensions();
            const ExtensionInfo* const row = find(rows, kPack);
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->mState, ExtensionInfo::State::Failed);
            EXPECT_NE(row->mMessage.find("test game pack asked to fail"), std::string::npos) << row->mMessage;
        }

        // ---- setSessionContent without a pack ----

        TEST_F(EditorHostGamePackTest, handingContentToAHostWithoutAGamePackDoesNothing)
        {
            EditorExtensionHost host(config(""));
            VFS::Manager vfs;
            EXPECT_NO_THROW(host.setSessionContent(contentList(), &vfs));
            host.start();
            EXPECT_EQ(host.neutralWorldStore(), nullptr);
            EXPECT_EQ(host.gamePackState(), EditorExtensionHost::GamePackState::None);
        }
    }
}
