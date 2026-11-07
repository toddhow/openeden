#include "apps/openmw/extensionhost.hpp"

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>

#include <openeden/logging.hpp>
#include <openeden/phases.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using openeden::sdk::Phase;
    using testing::ElementsAre;
    using testing::HasSubstr;
    using testing::IsEmpty;

    const std::string kA = "openeden_test_extension_a"; // provides openeden.test.a
    const std::string kB = "openeden_test_extension_b"; // requires openeden.test.a
    const std::string kC = "openeden_test_extension_c"; // no dependencies

    constexpr std::array kAllPhases
        = { Phase::Input, Phase::PreSimulation, Phase::PostSimulation, Phase::Animation, Phase::PreRender, Phase::Ui };
    constexpr int kPhasesPerFrame = static_cast<int>(kAllPhases.size());

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

    fs::path builtLibrary(const std::string& id)
    {
        if (id == kA)
            return OPENEDEN_TEST_EXTENSION_A_PATH;
        if (id == kB)
            return OPENEDEN_TEST_EXTENSION_B_PATH;
        return OPENEDEN_TEST_EXTENSION_C_PATH;
    }

    struct ManifestOptions
    {
        std::string mId;
        std::string mLibraryFile;
        int mSdkMajor = 4;
        bool mProvidesA = false;
        bool mRequiresA = false;
    };

    std::string makeManifest(const ManifestOptions& options)
    {
        const std::string contractA = R"([ { "contract": "openeden.test.a", "version": [ 1, 0 ] } ])";
        std::ostringstream json;
        json << "{\n"
             << R"(  "format": "openeden.extension",)"
             << "\n"
             << R"(  "version": 1,)"
             << "\n"
             << R"(  "id": ")" << options.mId << "\",\n"
             << R"(  "name": "Test extension",)"
             << "\n"
             << R"(  "extensionVersion": "1.0.0",)"
             << "\n"
             << R"(  "kind": "feature",)"
             << "\n"
             << R"(  "sdk": { "major": )" << options.mSdkMajor << R"(, "minor": 0 },)"
             << "\n"
             << R"(  "toolchain": "",)"
             << "\n"
             << R"(  "library": { "windows": ")" << options.mLibraryFile << R"(", "linux": ")" << options.mLibraryFile
             << "\" },\n"
             << R"(  "provides": )" << (options.mProvidesA ? contractA : "[]") << ",\n"
             << R"(  "requires": )" << (options.mRequiresA ? contractA : "[]") << ",\n"
             << R"(  "optional": [],)"
             << "\n"
             << R"(  "loadAfter": [],)"
             << "\n"
             << R"(  "data": "data",)"
             << "\n"
             << R"(  "settings": [],)"
             << "\n"
             << R"(  "licence": "GPL-3.0-or-later")"
             << "\n"
             << "}\n";
        return json.str();
    }

    void writeTextFile(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    // Everything the engine writes through Log goes to std::cout, and anything else to std::cerr, in this test binary:
    // main() does not call Debug::setupLogging. Log::sMinDebugLevel is raised so that no level is filtered out.
    class EngineLogCapture
    {
    public:
        EngineLogCapture()
            : mOldCout(std::cout.rdbuf(mText.rdbuf()))
            , mOldCerr(std::cerr.rdbuf(mText.rdbuf()))
            , mOldLevel(Log::sMinDebugLevel)
        {
            Log::sMinDebugLevel = Debug::All;
        }

        ~EngineLogCapture()
        {
            std::cout.rdbuf(mOldCout);
            std::cerr.rdbuf(mOldCerr);
            Log::sMinDebugLevel = mOldLevel;
        }

        EngineLogCapture(const EngineLogCapture&) = delete;
        EngineLogCapture& operator=(const EngineLogCapture&) = delete;

        [[nodiscard]] std::string text() const { return mText.str(); }

    private:
        std::stringstream mText;
        std::streambuf* mOldCout;
        std::streambuf* mOldCerr;
        Debug::Level mOldLevel;
    };

    class ExtensionHostTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto* const info = testing::UnitTest::GetInstance()->current_test_info();
            mRoot = fs::temp_directory_path() / "openeden_extensionhost_tests"
                / (std::string(info->name()) + "_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mRoot);
            mExtensionDir = mRoot / "extensions";
            fs::create_directories(mExtensionDir);
            mRecordPath = mRoot / "record.txt";

            for (const std::string& id : { kA, kB, kC })
                ASSERT_TRUE(fs::exists(builtLibrary(id))) << builtLibrary(id);

            setEnvironment("OPENEDEN_TEST_EXTENSION_LOG", mRecordPath.string());
            clearEnvironment("OPENEDEN_TEST_EXTENSION_THROW");
            mEngineLog.emplace();
        }

        void TearDown() override
        {
            mEngineLog.reset();
            clearEnvironment("OPENEDEN_TEST_EXTENSION_LOG");
            clearEnvironment("OPENEDEN_TEST_EXTENSION_THROW");
            std::error_code ignored;
            fs::remove_all(mRoot, ignored);
        }

        // Copies the built library into <searchDir>/<folder> and writes the manifest the ID needs.
        fs::path install(const std::string& id, const fs::path& searchDir, const std::string& folder)
        {
            const fs::path directory = searchDir / folder;
            fs::create_directories(directory);
            const fs::path library = builtLibrary(id);
            fs::copy_file(library, directory / library.filename(), fs::copy_options::overwrite_existing);

            ManifestOptions options;
            options.mId = id;
            options.mLibraryFile = library.filename().string();
            options.mProvidesA = id == kA;
            options.mRequiresA = id == kB;
            writeTextFile(directory / "extension.json", makeManifest(options));
            return directory;
        }

        fs::path install(const std::string& id) { return install(id, mExtensionDir, id); }

        void installAll()
        {
            install(kA);
            install(kB);
            install(kC);
        }

        [[nodiscard]] OMW::ExtensionHost::Config config(std::vector<std::string> enabled) const
        {
            return OMW::ExtensionHost::Config{ { mExtensionDir }, std::move(enabled) };
        }

        [[nodiscard]] std::vector<std::string> recorded() const
        {
            std::vector<std::string> lines;
            std::ifstream file(mRecordPath, std::ios::binary);
            std::string line;
            while (std::getline(file, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                lines.push_back(line);
            }
            return lines;
        }

        // What the extension recorded, in order, without its sink's lines and without its ID.
        [[nodiscard]] std::vector<std::string> events(const std::string& id) const
        {
            const std::string prefix = id + " ";
            std::vector<std::string> result;
            for (const std::string& line : recorded())
                if (line.starts_with(prefix) && !line.starts_with(prefix + "sink "))
                    result.push_back(line.substr(prefix.size()));
            return result;
        }

        [[nodiscard]] int count(const std::string& id, const std::string& event) const
        {
            const std::vector<std::string> all = events(id);
            return static_cast<int>(std::count(all.begin(), all.end(), event));
        }

        [[nodiscard]] int phaseCount(const std::string& id) const
        {
            const std::vector<std::string> all = events(id);
            return static_cast<int>(
                std::count_if(all.begin(), all.end(), [](const std::string& e) { return e.starts_with("phase "); }));
        }

        [[nodiscard]] int sinkLinesContaining(const std::string& id, const std::string& text) const
        {
            const std::string prefix = id + " sink ";
            int result = 0;
            for (const std::string& line : recorded())
                if (line.starts_with(prefix) && line.find(text) != std::string::npos)
                    ++result;
            return result;
        }

        // Position of an exact recorded line, or -1.
        [[nodiscard]] int position(const std::string& line) const
        {
            const std::vector<std::string> all = recorded();
            const auto it = std::find(all.begin(), all.end(), line);
            return it == all.end() ? -1 : static_cast<int>(it - all.begin());
        }

        [[nodiscard]] std::string engineLog() const { return mEngineLog->text(); }

        static void runFrame(OMW::ExtensionHost& host)
        {
            for (const Phase phase : kAllPhases)
                host.runPhase(phase, 0.016f);
        }

        // The state every failure case must end in: A and B are out, C goes on with a full frame of phases.
        void expectOnlyCRuns(OMW::ExtensionHost& host)
        {
            runFrame(host);
            EXPECT_TRUE(host.hasExtensions());
            EXPECT_TRUE(host.lifecycle().isExtensionActive(kC));
            EXPECT_FALSE(host.lifecycle().isExtensionActive(kA));
            EXPECT_FALSE(host.lifecycle().isExtensionActive(kB));
            EXPECT_EQ(count(kC, "start"), 1);
            EXPECT_EQ(phaseCount(kC), kPhasesPerFrame);
            EXPECT_EQ(phaseCount(kA), 0);
            EXPECT_EQ(phaseCount(kB), 0);
            EXPECT_EQ(count(kB, "start"), 0);
        }

        fs::path mRoot;
        fs::path mExtensionDir;
        fs::path mRecordPath;
        std::optional<EngineLogCapture> mEngineLog;
    };

    // Nothing to load

    TEST_F(ExtensionHostTest, emptyEnabledListLoadsNothingAndEveryCallIsNoOp)
    {
        installAll();
        OMW::ExtensionHost host(config({}));
        EXPECT_FALSE(host.hasExtensions());

        host.start();
        runFrame(host);
        host.stop();
        host.stop();

        EXPECT_FALSE(host.hasExtensions());
        EXPECT_EQ(host.lifecycle().getActiveExtensionCount(), 0u);
        EXPECT_THAT(host.lifecycle().getLoadOrder(), IsEmpty());
        EXPECT_THAT(recorded(), IsEmpty());
    }

    TEST_F(ExtensionHostTest, noSearchDirectoriesAreGiven)
    {
        OMW::ExtensionHost host(OMW::ExtensionHost::Config{ {}, { kA } });
        host.start();
        runFrame(host);
        EXPECT_FALSE(host.hasExtensions());
        EXPECT_THAT(recorded(), IsEmpty());
    }

    TEST_F(ExtensionHostTest, searchDirectoriesDoNotExist)
    {
        OMW::ExtensionHost host(OMW::ExtensionHost::Config{ { mRoot / "missing1", mRoot / "missing2" }, { kA } });
        host.start();
        runFrame(host);
        host.stop();
        EXPECT_FALSE(host.hasExtensions());
        EXPECT_THAT(recorded(), IsEmpty());
    }

    TEST_F(ExtensionHostTest, installedButNotEnabledIsNotLoadedAndIsListedInTheLog)
    {
        install(kA);
        install(kC);
        OMW::ExtensionHost host(config({ kC }));
        host.start();
        runFrame(host);

        EXPECT_THAT(events(kA), IsEmpty());
        EXPECT_FALSE(host.lifecycle().isExtensionActive(kA));
        EXPECT_EQ(count(kC, "start"), 1);
        EXPECT_THAT(engineLog(), HasSubstr(kA));
    }

    TEST_F(ExtensionHostTest, installedExtensionsAreListedEvenWhenNoneIsEnabled)
    {
        install(kA);
        OMW::ExtensionHost host(config({}));
        EXPECT_THAT(recorded(), IsEmpty());
        EXPECT_THAT(engineLog(), HasSubstr(kA));
    }

    TEST_F(ExtensionHostTest, enablingAnUninstalledIdDoesNotCrashAndOthersRun)
    {
        install(kC);
        OMW::ExtensionHost host(config({ "openeden_test_extension_not_installed", kC }));
        host.start();
        runFrame(host);

        EXPECT_TRUE(host.lifecycle().isExtensionActive(kC));
        EXPECT_EQ(phaseCount(kC), kPhasesPerFrame);
        EXPECT_THAT(engineLog(), HasSubstr("openeden_test_extension_not_installed"));
    }

    TEST_F(ExtensionHostTest, enablingOnlyAnUninstalledIdLoadsNothing)
    {
        OMW::ExtensionHost host(config({ "openeden_test_extension_not_installed" }));
        host.start();
        runFrame(host);
        host.stop();
        EXPECT_FALSE(host.hasExtensions());
    }

    // The normal lifecycle

    TEST_F(ExtensionHostTest, constructorRunsNoLifecycleCode)
    {
        install(kC);
        OMW::ExtensionHost host(config({ kC }));
        EXPECT_TRUE(host.hasExtensions());
        EXPECT_THAT(events(kC), IsEmpty());
    }

    TEST_F(ExtensionHostTest, phasesBeforeStartAreNotDelivered)
    {
        install(kC);
        OMW::ExtensionHost host(config({ kC }));
        runFrame(host);
        EXPECT_EQ(phaseCount(kC), 0);
    }

    TEST_F(ExtensionHostTest, enabledExtensionRunsTheWholeLifecycleInOrder)
    {
        install(kC);
        {
            OMW::ExtensionHost host(config({ kC }));
            host.start();
            EXPECT_TRUE(host.lifecycle().isExtensionActive(kC));
            runFrame(host);
            host.stop();
        }

        EXPECT_THAT(events(kC),
            ElementsAre("register", "resolve", "start", "phase Input", "phase PreSimulation", "phase PostSimulation",
                "phase Animation", "phase PreRender", "phase Ui", "stop", "destroy"));
    }

    TEST_F(ExtensionHostTest, phasesAreDeliveredInTheOrderCalled)
    {
        install(kC);
        OMW::ExtensionHost host(config({ kC }));
        host.start();
        host.runPhase(Phase::Ui, 0.016f);
        host.runPhase(Phase::Input, 0.016f);
        host.runPhase(Phase::Animation, 0.016f);

        EXPECT_THAT(
            events(kC), ElementsAre("register", "resolve", "start", "phase Ui", "phase Input", "phase Animation"));
    }

    TEST_F(ExtensionHostTest, stopIsIdempotentAndEndsPhases)
    {
        install(kC);
        OMW::ExtensionHost host(config({ kC }));
        host.start();
        host.stop();
        host.stop();
        runFrame(host);

        EXPECT_EQ(count(kC, "stop"), 1);
        EXPECT_EQ(phaseCount(kC), 0);
        EXPECT_FALSE(host.lifecycle().isExtensionActive(kC));
    }

    TEST_F(ExtensionHostTest, stopBeforeStartIsSafeAndNothingStartsAfterward)
    {
        install(kC);
        {
            OMW::ExtensionHost host(config({ kC }));
            host.stop();
            runFrame(host);
        }
        EXPECT_EQ(count(kC, "start"), 0);
        EXPECT_EQ(phaseCount(kC), 0);
    }

    TEST_F(ExtensionHostTest, destructorStopsARunningExtension)
    {
        install(kC);
        {
            OMW::ExtensionHost host(config({ kC }));
            host.start();
        }
        EXPECT_THAT(events(kC), ElementsAre("register", "resolve", "start", "stop", "destroy"));
    }

    TEST_F(ExtensionHostTest, destructorAfterStopDoesNotStopTwice)
    {
        install(kC);
        {
            OMW::ExtensionHost host(config({ kC }));
            host.start();
            host.stop();
        }
        EXPECT_EQ(count(kC, "stop"), 1);
        EXPECT_EQ(count(kC, "destroy"), 1);
    }

    TEST_F(ExtensionHostTest, dependencyOrderStartsProviderFirstAndStopsItLast)
    {
        installAll();
        {
            OMW::ExtensionHost host(config({ kC, kB, kA }));
            host.start();
            EXPECT_THAT(host.lifecycle().getLoadOrder(), ElementsAre(kA, kB, kC));
            EXPECT_EQ(host.lifecycle().getActiveExtensionCount(), 3u);
            host.stop();
        }

        EXPECT_LT(position(kA + " register"), position(kB + " register"));
        EXPECT_LT(position(kB + " register"), position(kC + " register"));
        EXPECT_LT(position(kA + " start"), position(kB + " start"));
        EXPECT_LT(position(kB + " start"), position(kC + " start"));
        EXPECT_LT(position(kC + " stop"), position(kB + " stop"));
        EXPECT_LT(position(kB + " stop"), position(kA + " stop"));
        EXPECT_LT(position(kC + " destroy"), position(kB + " destroy"));
        EXPECT_LT(position(kB + " destroy"), position(kA + " destroy"));
        // Every extension stops before any extension is destroyed.
        EXPECT_LT(position(kA + " stop"), position(kC + " destroy"));
    }

    TEST_F(ExtensionHostTest, phaseCallbacksRunInLoadOrder)
    {
        installAll();
        OMW::ExtensionHost host(config({ kA, kB, kC }));
        host.start();
        host.runPhase(Phase::Input, 0.016f);

        EXPECT_LT(position(kA + " phase Input"), position(kB + " phase Input"));
        EXPECT_LT(position(kB + " phase Input"), position(kC + " phase Input"));
    }

    // Failures disable the extension and its dependants, and nothing else

    TEST_F(ExtensionHostTest, brokenManifestDisablesOnlyThatExtensionAndItsDependant)
    {
        const fs::path directoryA = install(kA);
        install(kB);
        install(kC);
        writeTextFile(directoryA / "extension.json", "{ this is not json");

        OMW::ExtensionHost host(config({ kA, kB, kC }));
        host.start();

        expectOnlyCRuns(host);
        EXPECT_THAT(engineLog(), HasSubstr("extension.json"));
        EXPECT_THAT(engineLog(), HasSubstr(kB));
    }

    TEST_F(ExtensionHostTest, brokenManifestAloneLoadsNothing)
    {
        const fs::path directoryC = install(kC);
        writeTextFile(directoryC / "extension.json", "{ this is not json");

        OMW::ExtensionHost host(config({ kC }));
        host.start();
        runFrame(host);
        host.stop();
        EXPECT_FALSE(host.hasExtensions());
        EXPECT_THAT(recorded(), IsEmpty());
    }

    TEST_F(ExtensionHostTest, missingLibraryDisablesOnlyThatExtensionAndItsDependant)
    {
        const fs::path directoryA = install(kA);
        install(kB);
        install(kC);
        fs::remove(directoryA / builtLibrary(kA).filename());

        OMW::ExtensionHost host(config({ kA, kB, kC }));
        host.start();

        expectOnlyCRuns(host);
        EXPECT_THAT(engineLog(), HasSubstr(kA));
        EXPECT_THAT(engineLog(), HasSubstr(kB));
    }

    TEST_F(ExtensionHostTest, libraryThatIsNotALibraryDisablesOnlyThatExtensionAndItsDependant)
    {
        const fs::path directoryA = install(kA);
        install(kB);
        install(kC);
        writeTextFile(directoryA / builtLibrary(kA).filename(), "this is not a shared library");

        OMW::ExtensionHost host(config({ kA, kB, kC }));
        host.start();

        expectOnlyCRuns(host);
        EXPECT_THAT(engineLog(), HasSubstr(kA));
    }

    TEST_F(ExtensionHostTest, wrongSdkMajorDisablesOnlyThatExtensionAndItsDependant)
    {
        const fs::path directoryA = install(kA);
        install(kB);
        install(kC);
        ManifestOptions options;
        options.mId = kA;
        options.mLibraryFile = builtLibrary(kA).filename().string();
        options.mSdkMajor = 1;
        options.mProvidesA = true;
        writeTextFile(directoryA / "extension.json", makeManifest(options));

        OMW::ExtensionHost host(config({ kA, kB, kC }));
        host.start();

        expectOnlyCRuns(host);
        EXPECT_THAT(engineLog(), HasSubstr(kA));
        EXPECT_EQ(count(kA, "register"), 0);
    }

    TEST_F(ExtensionHostTest, sdk2ManifestIsRefusedAndDisablesOnlyThatExtensionAndItsDependant)
    {
        const fs::path directoryA = install(kA);
        install(kB);
        install(kC);
        ManifestOptions options;
        options.mId = kA;
        options.mLibraryFile = builtLibrary(kA).filename().string();
        options.mSdkMajor = 2; // SDK 3.0 removed the editor hooks from Host (task 0031, stage C)
        options.mProvidesA = true;
        writeTextFile(directoryA / "extension.json", makeManifest(options));

        OMW::ExtensionHost host(config({ kA, kB, kC }));
        host.start();

        expectOnlyCRuns(host);
        EXPECT_THAT(engineLog(), HasSubstr("built for SDK 2.0; this host provides 4.0"));
        EXPECT_EQ(count(kA, "register"), 0);
    }

    TEST_F(ExtensionHostTest, failedHandshakeDisablesOnlyThatExtension)
    {
        // The library says it is extension A, and the manifest says otherwise.
        const std::string liar = "openeden_test_extension_liar";
        const fs::path directory = mExtensionDir / liar;
        fs::create_directories(directory);
        const fs::path library = builtLibrary(kA);
        fs::copy_file(library, directory / library.filename());
        ManifestOptions options;
        options.mId = liar;
        options.mLibraryFile = library.filename().string();
        writeTextFile(directory / "extension.json", makeManifest(options));
        install(kC);

        OMW::ExtensionHost host(config({ liar, kC }));
        host.start();
        runFrame(host);

        EXPECT_FALSE(host.lifecycle().isExtensionActive(liar));
        EXPECT_THAT(events(kA), IsEmpty());
        EXPECT_TRUE(host.lifecycle().isExtensionActive(kC));
        EXPECT_EQ(phaseCount(kC), kPhasesPerFrame);
        EXPECT_THAT(engineLog(), HasSubstr(liar));
    }

    TEST_F(ExtensionHostTest, dependantWithoutItsProviderIsDisabledAndOthersRun)
    {
        install(kB);
        install(kC);

        OMW::ExtensionHost host(config({ kB, kC }));
        host.start();

        expectOnlyCRuns(host);
        EXPECT_THAT(engineLog(), HasSubstr(kB));
    }

    TEST_F(ExtensionHostTest, exceptionInStartDisablesOnlyThatExtensionAndItsDependant)
    {
        installAll();
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kA + ":start");

        OMW::ExtensionHost host(config({ kA, kB, kC }));
        EXPECT_NO_THROW(host.start());

        expectOnlyCRuns(host);
        EXPECT_THAT(engineLog(), HasSubstr(kA));
        EXPECT_THAT(engineLog(), HasSubstr("test throw " + kA + ":start"));
        EXPECT_THAT(engineLog(), HasSubstr(kB));
    }

    TEST_F(ExtensionHostTest, exceptionInDependantStartLeavesItsProviderRunning)
    {
        installAll();
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kB + ":start");

        OMW::ExtensionHost host(config({ kA, kB, kC }));
        EXPECT_NO_THROW(host.start());
        runFrame(host);

        EXPECT_FALSE(host.lifecycle().isExtensionActive(kB));
        EXPECT_TRUE(host.lifecycle().isExtensionActive(kA));
        EXPECT_TRUE(host.lifecycle().isExtensionActive(kC));
        EXPECT_EQ(phaseCount(kB), 0);
        EXPECT_EQ(phaseCount(kA), kPhasesPerFrame);
        EXPECT_EQ(phaseCount(kC), kPhasesPerFrame);
    }

    TEST_F(ExtensionHostTest, exceptionInPhaseDisablesThatExtensionAndItsDependantForGood)
    {
        installAll();
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kA + ":phase");

        OMW::ExtensionHost host(config({ kA, kB, kC }));
        host.start();
        EXPECT_NO_THROW(host.runPhase(Phase::Input, 0.016f));

        // The fault is known once the phase has been called. B, later in load order, may have seen that one phase.
        EXPECT_EQ(phaseCount(kA), 1);
        const int dependantPhases = phaseCount(kB);
        EXPECT_LE(dependantPhases, 1);

        EXPECT_TRUE(host.lifecycle().isExtensionFaulted(kA));
        EXPECT_FALSE(host.lifecycle().isExtensionActive(kA));
        EXPECT_FALSE(host.lifecycle().isExtensionActive(kB));
        EXPECT_TRUE(host.lifecycle().isExtensionActive(kC));
        EXPECT_THAT(engineLog(), HasSubstr("test throw " + kA + ":phase"));

        const int independentPhases = phaseCount(kC);
        EXPECT_NO_THROW(runFrame(host));
        EXPECT_NO_THROW(runFrame(host));

        EXPECT_EQ(phaseCount(kA), 1);
        EXPECT_EQ(phaseCount(kB), dependantPhases);
        EXPECT_EQ(phaseCount(kC), independentPhases + 2 * kPhasesPerFrame);
    }

    TEST_F(ExtensionHostTest, exceptionInDependantPhaseLeavesItsProviderRunning)
    {
        installAll();
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kB + ":phase");

        OMW::ExtensionHost host(config({ kA, kB, kC }));
        host.start();
        EXPECT_NO_THROW(runFrame(host));
        EXPECT_NO_THROW(runFrame(host));

        EXPECT_EQ(phaseCount(kB), 1);
        EXPECT_TRUE(host.lifecycle().isExtensionFaulted(kB));
        EXPECT_EQ(phaseCount(kA), 2 * kPhasesPerFrame);
        EXPECT_EQ(phaseCount(kC), 2 * kPhasesPerFrame);
    }

    TEST_F(ExtensionHostTest, shutdownAfterAPhaseFaultStopsTheOthersOnceAndDestroysEveryExtensionOnce)
    {
        installAll();
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kA + ":phase");
        {
            OMW::ExtensionHost host(config({ kA, kB, kC }));
            host.start();
            runFrame(host);
            host.stop();
        }

        EXPECT_EQ(count(kC, "stop"), 1);
        EXPECT_LE(count(kA, "stop"), 1);
        EXPECT_LE(count(kB, "stop"), 1);
        EXPECT_EQ(count(kA, "destroy"), 1);
        EXPECT_EQ(count(kB, "destroy"), 1);
        EXPECT_EQ(count(kC, "destroy"), 1);
    }

    TEST_F(ExtensionHostTest, loggingAfterAFaultedExtensionIsUnloadedDoesNotCallItsSink)
    {
        // The extension added a log sink in start and removes it in stop. If a fault skips stop and the library then
        // unloads, the logging service would call into unmapped code here.
        install(kC);
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kC + ":phase");

        OMW::ExtensionHost host(config({ kC }));
        host.start();
        runFrame(host);
        host.stop();

        host.host().loggingService().dispatch("after_unload", openeden::sdk::LogLevel::Info, "line after unload");
        EXPECT_EQ(sinkLinesContaining(kC, "line after unload"), 0);
    }

    // Added after review: an extension whose start throws never runs stop, so its sink is still registered at unload.
    TEST_F(ExtensionHostTest, sinkLeftByAExtensionWhoseStartThrewIsDroppedBeforeItsLibraryUnloads)
    {
        install(kC);
        setEnvironment("OPENEDEN_TEST_EXTENSION_THROW", kC + ":start-after-sink");

        {
            OMW::ExtensionHost host(config({ kC }));
            host.start();
            EXPECT_FALSE(host.lifecycle().isExtensionActive(kC));
            host.stop();

            host.host().loggingService().dispatch("after_unload", openeden::sdk::LogLevel::Info, "line after unload");
            EXPECT_EQ(sinkLinesContaining(kC, "line after unload"), 0);
        }
    }

    // Duplicate IDs and discovery order

    TEST_F(ExtensionHostTest, earlierSearchDirectoryWinsOverALaterOneThatWouldFail)
    {
        const fs::path first = mRoot / "first";
        const fs::path second = mRoot / "second";
        install(kC, first, kC);
        const fs::path later = install(kC, second, kC);
        fs::remove(later / builtLibrary(kC).filename());

        OMW::ExtensionHost host(OMW::ExtensionHost::Config{ { first, second }, { kC } });
        host.start();

        EXPECT_TRUE(host.lifecycle().isExtensionActive(kC));
        EXPECT_EQ(count(kC, "start"), 1);
    }

    TEST_F(ExtensionHostTest, earlierSearchDirectoryWinsEvenWhenOnlyTheLaterOneWouldWork)
    {
        const fs::path first = mRoot / "first";
        const fs::path second = mRoot / "second";
        const fs::path earlier = install(kC, first, kC);
        install(kC, second, kC);
        fs::remove(earlier / builtLibrary(kC).filename());

        OMW::ExtensionHost host(OMW::ExtensionHost::Config{ { first, second }, { kC } });
        host.start();

        EXPECT_FALSE(host.lifecycle().isExtensionActive(kC));
        EXPECT_EQ(count(kC, "start"), 0);
    }

    TEST_F(ExtensionHostTest, duplicateIdIsLoadedOnceAndNotReportedAsADuplicateProvider)
    {
        const fs::path first = mRoot / "first";
        const fs::path second = mRoot / "second";
        install(kA, first, kA);
        install(kA, second, kA);

        OMW::ExtensionHost host(OMW::ExtensionHost::Config{ { first, second }, { kA } });
        host.start();

        EXPECT_TRUE(host.lifecycle().isExtensionActive(kA));
        EXPECT_THAT(host.lifecycle().getLoadOrder(), ElementsAre(kA));
        EXPECT_EQ(count(kA, "register"), 1);
    }

    TEST_F(ExtensionHostTest, loadOrderDoesNotDependOnDiscoveryOrder)
    {
        // Folder names decide the order a directory lists them in, so these two sets are discovered in opposite orders.
        const fs::path forward = mRoot / "forward";
        install(kA, forward, "1_folder");
        install(kB, forward, "2_folder");
        install(kC, forward, "3_folder");
        const fs::path backward = mRoot / "backward";
        install(kC, backward, "1_folder");
        install(kB, backward, "2_folder");
        install(kA, backward, "3_folder");

        std::vector<std::string> orderForward;
        {
            OMW::ExtensionHost host(OMW::ExtensionHost::Config{ { forward }, { kA, kB, kC } });
            host.start();
            orderForward = host.lifecycle().getLoadOrder();
        }
        std::vector<std::string> orderBackward;
        {
            OMW::ExtensionHost host(OMW::ExtensionHost::Config{ { backward }, { kC, kB, kA } });
            host.start();
            orderBackward = host.lifecycle().getLoadOrder();
        }

        EXPECT_THAT(orderForward, ElementsAre(kA, kB, kC));
        EXPECT_EQ(orderForward, orderBackward);
    }

    TEST_F(ExtensionHostTest, loadOrderDoesNotDependOnWhichSearchDirectoryHoldsWhat)
    {
        const fs::path one = mRoot / "one";
        const fs::path two = mRoot / "two";
        install(kB, one, kB);
        install(kA, two, kA);
        install(kC, two, kC);

        OMW::ExtensionHost host(OMW::ExtensionHost::Config{ { one, two }, { kA, kB, kC } });
        host.start();

        EXPECT_THAT(host.lifecycle().getLoadOrder(), ElementsAre(kA, kB, kC));
    }

    // Logging

    TEST_F(ExtensionHostTest, extensionLogLineReachesTheEngineLogWithItsPrefix)
    {
        install(kC);
        OMW::ExtensionHost host(config({ kC }));
        host.start();

        std::istringstream lines(engineLog());
        std::string line;
        bool found = false;
        while (std::getline(lines, line))
            if (line.find("[" + kC + "]") != std::string::npos
                && line.find("hello from extension") != std::string::npos)
                found = true;
        EXPECT_TRUE(found) << engineLog();
    }

    TEST_F(ExtensionHostTest, extensionSinkReceivesLinesFromTheLoggingServiceUntilStop)
    {
        install(kC);
        OMW::ExtensionHost host(config({ kC }));
        host.start();
        EXPECT_EQ(sinkLinesContaining(kC, "hello from extension"), 1);

        host.host().loggingService().dispatch("direct", openeden::sdk::LogLevel::Info, "line while running");
        EXPECT_EQ(sinkLinesContaining(kC, "line while running"), 1);

        host.stop();
        host.host().loggingService().dispatch("direct", openeden::sdk::LogLevel::Info, "line after stop");
        EXPECT_EQ(sinkLinesContaining(kC, "line after stop"), 0);
    }

    // The engine's log listeners fire only inside the stream buffers Debug::wrapApplication installs, and main() of
    // this binary does not use it. So this one test runs its body inside wrapApplication. It can run once per process:
    // wrapApplication opens static stream buffers, and opening them a second time throws.
    std::function<void()> sWrappedBody;

    int runWrappedBody(int /*argc*/, char* /*argv*/[])
    {
        sWrappedBody();
        return 0;
    }

    TEST_F(ExtensionHostTest, engineLogLinesReachExtensionSinksAndTheExistingListenerStillWorks)
    {
        static bool alreadyRan = false;
        if (alreadyRan)
            GTEST_SKIP() << "Debug::wrapApplication can run once per process";
        alreadyRan = true;

        // wrapApplication keeps the current std::cout buffer for later, so it must be the real one.
        mEngineLog.reset();
        const Debug::Level oldLevel = Log::sMinDebugLevel;

        install(kC);
        std::vector<std::string> listenerLines;
        // The slot the in-game log viewer uses.
        Debug::setLogListener([&listenerLines](Debug::Level, std::string_view, std::string_view message) {
            listenerLines.emplace_back(message);
        });
        const auto listenerSaw = [&listenerLines](const std::string& text) {
            return std::any_of(listenerLines.begin(), listenerLines.end(),
                [&text](const std::string& line) { return line.find(text) != std::string::npos; });
        };

        sWrappedBody = [&] {
            Log::sMinDebugLevel = Debug::All;
            Log::sWriteLevel = true;
            {
                OMW::ExtensionHost host(config({ kC }));
                host.start();

                // An extension's own line goes engine log -> listeners, and must not come back to the sink a second
                // time.
                EXPECT_TRUE(listenerSaw("[" + kC + "]")) << "extension line missing from the engine log";
                EXPECT_TRUE(listenerSaw("hello from extension"));
                EXPECT_EQ(sinkLinesContaining(kC, "hello from extension"), 1);

                Log(Debug::Info) << "engine line while running";
                EXPECT_TRUE(listenerSaw("engine line while running"));
                EXPECT_EQ(sinkLinesContaining(kC, "engine line while running"), 1);

                host.stop();
                Log(Debug::Info) << "engine line after stop";
                EXPECT_TRUE(listenerSaw("engine line after stop"));
                EXPECT_EQ(sinkLinesContaining(kC, "engine line after stop"), 0);
            }
            // The bridge is gone with the host: logging must neither reach it nor lose the listener.
            Log(Debug::Info) << "engine line after destruction";
            EXPECT_TRUE(listenerSaw("engine line after destruction"));
        };

        setEnvironment("OPENMW_DISABLE_CRASH_CATCHER", "1");
        char programName[] = "openmw-tests";
        char* arguments[] = { programName, nullptr };
        const int result = Debug::wrapApplication(runWrappedBody, 1, arguments, "openmw-tests");
        clearEnvironment("OPENMW_DISABLE_CRASH_CATCHER");

        sWrappedBody = nullptr;
        Debug::setLogListener(nullptr);
        Log::sMinDebugLevel = oldLevel; // wrapApplication resets it, and Log::sWriteLevel, on the way out
        EXPECT_EQ(result, 0);
    }
}
