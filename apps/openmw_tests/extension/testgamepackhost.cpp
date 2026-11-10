// Task 0084: a game pack loaded from a shared library by the game's extension host (the dynamic route), chosen by
// game=, handed the game services, run in the game phases, and ended with the session when it fails.
#include "apps/openmw/extensionhost.hpp"

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>

#include <components/extension/save_chunk_store.hpp>

#include <openeden/phases.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using openeden::sdk::Phase;
    using testing::HasSubstr;

    const std::string kPack = "openeden_test_game_pack";

    void setEnvironment(const char* name, const std::string& value)
    {
#ifdef _WIN32
        _putenv_s(name, value.c_str());
#else
        setenv(name, value.c_str(), 1);
#endif
    }

    void clearEnvironment(const char* name)
    {
#ifdef _WIN32
        _putenv_s(name, "");
#else
        unsetenv(name);
#endif
    }

    class GamePackHostTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto* const info = testing::UnitTest::GetInstance()->current_test_info();
            mRoot = fs::temp_directory_path() / "openeden_gamepack_tests"
                / (std::string(info->name()) + "_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            mPackDir = mRoot / "game_packs";
            fs::create_directories(mPackDir / kPack);
            mRecordPath = mRoot / "record.txt";

            const fs::path library = OPENEDEN_TEST_GAME_PACK_PATH;
            ASSERT_TRUE(fs::exists(library)) << library;
            fs::copy_file(library, mPackDir / kPack / library.filename(), fs::copy_options::overwrite_existing);
            std::ofstream manifest(mPackDir / kPack / "extension.json", std::ios::binary);
            manifest
                << "{\n  \"format\": \"openeden.extension\",\n  \"version\": 2,\n  \"id\": \"" << kPack
                << "\",\n  \"name\": \"Test game pack\",\n  \"extensionVersion\": \"1.2.0\",\n  \"kind\": \"game\",\n"
                   "  \"sdk\": { \"major\": 4, \"minor\": 0 },\n  \"toolchain\": \"\",\n  \"library\": { "
                   "\"windows\": \""
                << library.filename().string() << "\", \"linux\": \"" << library.filename().string()
                << "\" },\n  \"provides\": [],\n  \"requires\": [],\n  \"optional\": [],\n  \"loadAfter\": [],\n  "
                   "\"data\": \"data\",\n  \"settings\": [],\n  \"licence\": \"GPL-3.0-or-later\"\n}\n";
            manifest.close();

            setEnvironment("OPENEDEN_TEST_EXTENSION_LOG", mRecordPath.string());
            clearEnvironment("OPENEDEN_TEST_EXTENSION_THROW");
            mOldCout = std::cout.rdbuf(mLogText.rdbuf());
            mOldCerr = std::cerr.rdbuf(mLogText.rdbuf());
            mOldLevel = Log::sMinDebugLevel;
            Log::sMinDebugLevel = Debug::All;
        }

        void TearDown() override
        {
            std::cout.rdbuf(mOldCout);
            std::cerr.rdbuf(mOldCerr);
            Log::sMinDebugLevel = mOldLevel;
            clearEnvironment("OPENEDEN_TEST_EXTENSION_LOG");
            clearEnvironment("OPENEDEN_TEST_EXTENSION_THROW");
            std::error_code ignored;
            fs::remove_all(mRoot, ignored);
        }

        // enableIt: the pack is also in the extension= list, as a player might have left it
        [[nodiscard]] OMW::ExtensionHost::Config config(const std::string& gamePackId, bool enableIt = false) const
        {
            return OMW::ExtensionHost::Config{ { mPackDir },
                enableIt ? std::vector<std::string>{ kPack } : std::vector<std::string>{}, gamePackId };
        }

        [[nodiscard]] std::vector<std::string> events() const
        {
            std::vector<std::string> lines;
            std::ifstream file(mRecordPath, std::ios::binary);
            std::string line;
            const std::string prefix = kPack + " ";
            while (std::getline(file, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.starts_with(prefix))
                    lines.push_back(line.substr(prefix.size()));
            }
            return lines;
        }

        fs::path mRoot;
        fs::path mPackDir;
        fs::path mRecordPath;
        std::stringstream mLogText;
        std::streambuf* mOldCout = nullptr;
        std::streambuf* mOldCerr = nullptr;
        Debug::Level mOldLevel = Debug::All;
    };

    TEST_F(GamePackHostTest, theChosenGamePackLoadsStartsAndRunsItsPhasesFirst)
    {
        OMW::ExtensionHost host(config(kPack));
        EXPECT_TRUE(host.hasExtensions());
        host.start();
        EXPECT_EQ(host.gamePackId(), kPack);
        EXPECT_EQ(host.gamePackVersion(), "1.2.0");

        host.runPhase(Phase::GameSimulation, 0.016f);
        host.runPhase(Phase::PostSimulation, 0.016f);
        host.runPhase(Phase::GameUpdateUi, 0.016f);
        host.stop();

        EXPECT_EQ(events(),
            (std::vector<std::string>{ "resolve", "start with every game service", "phase GameSimulation",
                "phase PostSimulation", "phase GameUpdateUi", "stop" }));
        EXPECT_TRUE(host.sessionError().empty());
        EXPECT_TRUE(host.takeFailureReport().empty());
    }

    TEST_F(GamePackHostTest, aGamePackThatIsNotChosenIsNotLoadedAndTheReportNamesBothPacks)
    {
        OMW::ExtensionHost host(config("some_other_game", true));
        EXPECT_FALSE(host.hasExtensions());
        host.start();
        const std::string report = host.takeFailureReport();
        EXPECT_THAT(report, HasSubstr(kPack));
        EXPECT_THAT(report, HasSubstr("some_other_game"));
        EXPECT_TRUE(events().empty());
    }

    TEST_F(GamePackHostTest, withNoGameLineNoGamePackLoads)
    {
        OMW::ExtensionHost host(config("", true));
        EXPECT_FALSE(host.hasExtensions());
        host.start();
        EXPECT_THAT(host.takeFailureReport(), HasSubstr("no game= line"));
        EXPECT_TRUE(host.gamePackId().empty());
        EXPECT_TRUE(events().empty());
    }

    TEST_F(GamePackHostTest, aChosenGamePackThatIsNotInstalledIsReported)
    {
        fs::remove_all(mPackDir / kPack);
        OMW::ExtensionHost host(config(kPack));
        const std::string report = host.takeFailureReport();
        EXPECT_THAT(report, HasSubstr(kPack));
        EXPECT_THAT(report, HasSubstr("not installed"));
    }

    TEST_F(GamePackHostTest, aGamePackThatThrowsInAPhaseEndsTheSessionAndSavingIsRefused)
    {
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kPack + ":phase");
        OMW::ExtensionHost host(config(kPack));
        host.start();
        EXPECT_TRUE(host.sessionError().empty());
        EXPECT_NO_THROW(host.runPhase(Phase::GameSimulation, 0.016f));
        EXPECT_THAT(host.sessionError(), HasSubstr(kPack));
        EXPECT_THAT(host.sessionError(), HasSubstr("saving is refused"));
        EXPECT_THAT(host.takeFailureReport(), HasSubstr(kPack));
        host.stop();
    }

    // Task 0084 leftovers, lane I stage I2: a game pack that fails while the game runs ends the session. The engine
    // asks the host once per frame; the answer is the located message exactly once, so the game shows one error and
    // returns to the main menu (apps/openmw/game.hpp, MWState::StateManager::error).
    TEST_F(GamePackHostTest, aHealthyGamePackHasNoSessionFault)
    {
        OMW::ExtensionHost host(config(kPack));
        host.start();
        host.runPhase(Phase::GameSimulation, 0.016f);
        EXPECT_TRUE(host.takeSessionFault().empty());
        host.stop();
    }

    TEST_F(GamePackHostTest, aFailedGamePackReportsItsSessionFaultOnceWithItsIdAndTheSavingRefusal)
    {
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kPack + ":phase");
        OMW::ExtensionHost host(config(kPack));
        host.start();
        EXPECT_TRUE(host.takeSessionFault().empty()) << "nothing has failed yet";
        host.runPhase(Phase::GameSimulation, 0.016f);

        const std::string fault = host.takeSessionFault();
        EXPECT_THAT(fault, HasSubstr(kPack));
        EXPECT_THAT(fault, HasSubstr("saving is refused"));
        EXPECT_EQ(fault, host.sessionError()) << "the message is the one that refuses saving";
        EXPECT_TRUE(host.takeSessionFault().empty()) << "reported once";
        EXPECT_FALSE(host.sessionError().empty()) << "saving stays refused after the report";
        host.stop();
    }

    TEST_F(GamePackHostTest, aFaultInAnotherFrameStillEndsOnlyOnce)
    {
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kPack + ":phase");
        OMW::ExtensionHost host(config(kPack));
        host.start();
        host.runPhase(Phase::GameSimulation, 0.016f);
        EXPECT_FALSE(host.takeSessionFault().empty());
        for (int frame = 0; frame < 3; ++frame)
        {
            host.runPhase(Phase::GameSimulation, 0.016f);
            host.runPhase(Phase::GameUpdateUi, 0.016f);
            EXPECT_TRUE(host.takeSessionFault().empty()) << "frame " << frame;
        }
        host.stop();
    }

    TEST_F(GamePackHostTest, aSessionWithNoGamePackNeverEndsForAPackFault)
    {
        OMW::ExtensionHost host(config(""));
        host.start();
        host.runPhase(Phase::GameSimulation, 0.016f);
        EXPECT_TRUE(host.takeSessionFault().empty());
        host.stop();
    }

    // Task 0084 leftovers, lane I stage I4: the host's side of the pack's chunks in the save. The engine calls these
    // on the main thread (collect just before the save's worker starts, restore when a load has finished reading).
    TEST_F(GamePackHostTest, theSavedChunksOfAGamePackComeOutAsOneBlobAndGoBackIn)
    {
        std::vector<std::byte> blob;
        {
            OMW::ExtensionHost host(config(kPack));
            host.start();
            std::string error;
            ASSERT_TRUE(host.collectGamePackSave(blob, error)) << error;
            EXPECT_FALSE(blob.empty());
            std::vector<openeden::extension::SavedChunk> chunks;
            ASSERT_TRUE(openeden::extension::decodeChunks(blob, chunks, error)) << error;
            ASSERT_EQ(chunks.size(), 1u);
            EXPECT_EQ(chunks[0].packId, kPack);
            EXPECT_EQ(chunks[0].name, "counter");
            host.stop();
        }
        OMW::ExtensionHost second(config(kPack));
        second.start();
        std::string error;
        EXPECT_TRUE(second.restoreGamePackSave(blob, error)) << error;
        second.stop();
    }

    TEST_F(GamePackHostTest, aBlobThatIsNotChunksIsRefusedWithAMessage)
    {
        OMW::ExtensionHost host(config(kPack));
        host.start();
        const std::vector<std::byte> garbage(7, std::byte{ 0xAB });
        std::string error;
        EXPECT_FALSE(host.restoreGamePackSave(garbage, error));
        EXPECT_FALSE(error.empty());
        host.stop();
    }

    TEST_F(GamePackHostTest, aSessionWithNoGamePackCollectsNothingAndRestoresNothing)
    {
        OMW::ExtensionHost host(config(""));
        host.start();
        std::vector<std::byte> blob{ std::byte{ 1 } };
        std::string error;
        EXPECT_TRUE(host.collectGamePackSave(blob, error));
        EXPECT_TRUE(blob.empty()) << "no record is written, so a Morrowind save has the bytes it always had";
        EXPECT_TRUE(host.restoreGamePackSave({}, error));
        host.stop();
    }

    TEST_F(GamePackHostTest, aFailedGamePackWritesNoChunks)
    {
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kPack + ":phase");
        OMW::ExtensionHost host(config(kPack));
        host.start();
        host.runPhase(Phase::GameSimulation, 0.016f);
        std::vector<std::byte> blob;
        std::string error;
        EXPECT_FALSE(host.collectGamePackSave(blob, error));
        EXPECT_THAT(error, HasSubstr(kPack));
        EXPECT_TRUE(blob.empty());
        host.stop();
    }

    // Task 0084 leftovers, lane I part 2, stage I1b: the first start phase with a pack loaded from a library.
    TEST_F(GamePackHostTest, theGamePackStartsInTheFirstPhaseAndStartDoesNotStartItAgain)
    {
        OMW::ExtensionHost host(config(kPack));
        EXPECT_TRUE(host.startGamePack());
        EXPECT_EQ(events(), (std::vector<std::string>{ "resolve", "start with every game service" }));
        EXPECT_EQ(host.gamePackId(), kPack);

        host.start();
        EXPECT_EQ(events(), (std::vector<std::string>{ "resolve", "start with every game service" }))
            << "the second phase does not start the pack again";
        host.runPhase(Phase::GameSimulation, 0.016f);
        EXPECT_EQ(events().back(), "phase GameSimulation");
        host.stop();
        EXPECT_EQ(events().back(), "stop");
    }

    TEST_F(GamePackHostTest, withoutAGamePackTheFirstPhaseIsNotTaken)
    {
        OMW::ExtensionHost host(config(""));
        EXPECT_FALSE(host.startGamePack());
        EXPECT_TRUE(events().empty());
        host.start();
        EXPECT_TRUE(events().empty());
    }
}
