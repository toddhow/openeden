// Task 0018: the failure report the game shows at the main menu, and the Morrowind compatibility pack's converter
// reached through the registry from a real library.

#include "apps/openmw/pluginhost.hpp"

#include <openeden/phases.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using openeden::sdk::Phase;
    using testing::Contains;
    using testing::HasSubstr;
    using testing::IsEmpty;
    using testing::Not;

    const std::string kA = "openeden_test_plugin_a"; // provides openeden.test.a
    const std::string kB = "openeden_test_plugin_b"; // requires openeden.test.a
    const std::string kC = "openeden_test_plugin_c"; // no dependencies
    const std::string kKf = "openeden_test_plugin_kf"; // requires openeden.compat.kf_converter
    const std::string kPack = "compat_morrowind";

    constexpr std::array kAllPhases
        = { Phase::Input, Phase::PreSimulation, Phase::PostSimulation, Phase::Animation, Phase::PreRender, Phase::Ui };

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
            return OPENEDEN_TEST_PLUGIN_A_PATH;
        if (id == kB)
            return OPENEDEN_TEST_PLUGIN_B_PATH;
        if (id == kKf)
            return OPENEDEN_TEST_PLUGIN_KF_PATH;
        return OPENEDEN_TEST_PLUGIN_C_PATH;
    }

    std::string contractList(const std::string& contract)
    {
        return "[ { \"contract\": \"" + contract + "\", \"version\": [ 1, 0 ] } ]";
    }

    std::string makeManifest(const std::string& id, const std::string& libraryFile)
    {
        std::string provides = "[]";
        std::string requiredContracts = "[]";
        if (id == kA)
            provides = contractList("openeden.test.a");
        if (id == kB)
            requiredContracts = contractList("openeden.test.a");
        if (id == kKf)
            requiredContracts = contractList("openeden.compat.kf_converter");

        std::ostringstream json;
        json << "{\n"
             << "  \"format\": \"openeden.plugin\",\n"
             << "  \"version\": 1,\n"
             << "  \"id\": \"" << id << "\",\n"
             << "  \"name\": \"Test plugin\",\n"
             << "  \"pluginVersion\": \"1.0.0\",\n"
             << "  \"kind\": \"feature\",\n"
             << "  \"sdk\": { \"major\": 2, \"minor\": 0 },\n"
             << "  \"toolchain\": \"\",\n"
             << "  \"library\": { \"windows\": \"" << libraryFile << "\", \"linux\": \"" << libraryFile << "\" },\n"
             << "  \"provides\": " << provides << ",\n"
             << "  \"requires\": " << requiredContracts << ",\n"
             << "  \"optional\": [],\n"
             << "  \"loadAfter\": [],\n"
             << "  \"data\": \"data\",\n"
             << "  \"settings\": [],\n"
             << "  \"licence\": \"GPL-3.0-or-later\"\n"
             << "}\n";
        return json.str();
    }

    void writeTextFile(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    std::vector<std::string> linesOf(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                lines.push_back(line);
        }
        return lines;
    }

    class PluginFailureReportTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto* const info = testing::UnitTest::GetInstance()->current_test_info();
            mRoot = fs::temp_directory_path() / "openeden_pluginfailure_tests"
                / (std::string(info->name()) + "_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            mPluginDir = mRoot / "plugins";
            fs::create_directories(mPluginDir);
            mRecordPath = mRoot / "record.txt";
            setEnvironment("OPENEDEN_TEST_PLUGIN_LOG", mRecordPath.string());
            clearEnvironment("OPENEDEN_TEST_PLUGIN_THROW");
        }

        void TearDown() override
        {
            clearEnvironment("OPENEDEN_TEST_PLUGIN_LOG");
            clearEnvironment("OPENEDEN_TEST_PLUGIN_THROW");
            std::error_code ignored;
            fs::remove_all(mRoot, ignored);
        }

        fs::path install(const std::string& id)
        {
            const fs::path directory = mPluginDir / id;
            fs::create_directories(directory);
            const fs::path library = builtLibrary(id);
            fs::copy_file(library, directory / library.filename(), fs::copy_options::overwrite_existing);
            writeTextFile(directory / "plugin.json", makeManifest(id, library.filename().string()));
            return directory;
        }

        // The real pack: the library the build made and the manifest it ships with.
        void installPack()
        {
            const fs::path library = OPENEDEN_TEST_COMPAT_MORROWIND_PATH;
            const fs::path manifest = OPENEDEN_TEST_COMPAT_MORROWIND_MANIFEST;
            ASSERT_TRUE(fs::exists(library)) << library;
            ASSERT_TRUE(fs::exists(manifest)) << manifest;
            const fs::path directory = mPluginDir / kPack;
            fs::create_directories(directory);
            fs::copy_file(library, directory / library.filename());
            fs::copy_file(manifest, directory / "plugin.json");
        }

        [[nodiscard]] OMW::PluginHost::Config config(std::vector<std::string> enabled) const
        {
            return OMW::PluginHost::Config{ { mPluginDir }, std::move(enabled) };
        }

        // What the KF plugin recorded starting with "kf ", without that prefix or its ID.
        [[nodiscard]] std::vector<std::string> kfEvents() const
        {
            std::ifstream file(mRecordPath, std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            const std::string prefix = kKf + " kf ";
            std::vector<std::string> result;
            for (const std::string& line : linesOf(text.str()))
                if (line.starts_with(prefix))
                    result.push_back(line.substr(prefix.size()));
            return result;
        }

        static void runFrame(OMW::PluginHost& host)
        {
            for (const Phase phase : kAllPhases)
                host.runPhase(phase, 0.016f);
        }

        // One line, naming the plugin and giving a reason besides; the report is then used up.
        static void expectOneLineThenEmpty(OMW::PluginHost& host, const std::string& id, const std::string& reason = {})
        {
            const std::string report = host.takeFailureReport();
            const std::vector<std::string> lines = linesOf(report);
            ASSERT_EQ(lines.size(), 1u) << report;
            EXPECT_THAT(lines[0], HasSubstr(id));
            EXPECT_GT(lines[0].size(), id.size() + 1) << "no reason in: " << lines[0];
            if (!reason.empty())
            {
                EXPECT_THAT(lines[0], HasSubstr(reason));
            }
            EXPECT_THAT(host.takeFailureReport(), IsEmpty());
        }

        fs::path mRoot;
        fs::path mPluginDir;
        fs::path mRecordPath;
    };

    // Nothing to report

    TEST_F(PluginFailureReportTest, emptyWithNoPluginsEnabled)
    {
        install(kC);
        OMW::PluginHost host(config({}));
        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
        host.start();
        runFrame(host);
        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
    }

    TEST_F(PluginFailureReportTest, emptyWhenPluginsRunWell)
    {
        install(kA);
        install(kB);
        install(kC);
        OMW::PluginHost host(config({ kA, kB, kC }));
        host.start();
        runFrame(host);
        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
        host.stop();
        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
    }

    TEST_F(PluginFailureReportTest, brokenManifestOfAPluginThatIsNotEnabledIsNotReported)
    {
        const fs::path directory = install(kA);
        writeTextFile(directory / "plugin.json", "{ this is not json");
        install(kC);

        OMW::PluginHost host(config({ kC }));
        host.start();
        runFrame(host);

        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
    }

    // Each kind of failure

    TEST_F(PluginFailureReportTest, missingLibraryIsNamedOnce)
    {
        const fs::path directory = install(kC);
        fs::remove(directory / builtLibrary(kC).filename());

        OMW::PluginHost host(config({ kC }));
        host.start();

        // Nothing loaded, but the player must still hear about it.
        EXPECT_FALSE(host.hasPlugins());
        expectOneLineThenEmpty(host, kC);
    }

    TEST_F(PluginFailureReportTest, brokenManifestOfAnEnabledPluginIsNamedOnce)
    {
        const fs::path directory = install(kC);
        writeTextFile(directory / "plugin.json", "{ this is not json");

        OMW::PluginHost host(config({ kC }));
        host.start();

        EXPECT_FALSE(host.hasPlugins());
        expectOneLineThenEmpty(host, kC);
    }

    TEST_F(PluginFailureReportTest, throwInStartIsNamedOnceWithItsMessage)
    {
        install(kC);
        setEnvironment("OPENEDEN_TEST_PLUGIN_THROW", kC + ":start");

        OMW::PluginHost host(config({ kC }));
        host.start();

        expectOneLineThenEmpty(host, kC, "test throw " + kC + ":start");
    }

    TEST_F(PluginFailureReportTest, throwInAPhaseIsNamedOnceWithItsMessage)
    {
        install(kC);
        setEnvironment("OPENEDEN_TEST_PLUGIN_THROW", kC + ":phase");

        OMW::PluginHost host(config({ kC }));
        host.start();
        EXPECT_THAT(host.takeFailureReport(), IsEmpty());

        runFrame(host);

        expectOneLineThenEmpty(host, kC, "test throw " + kC + ":phase");
    }

    TEST_F(PluginFailureReportTest, failureBeforeStartIsNotReportedAgainAfterStart)
    {
        const fs::path directory = install(kC);
        fs::remove(directory / builtLibrary(kC).filename());

        OMW::PluginHost host(config({ kC }));
        const std::string beforeStart = host.takeFailureReport();
        host.start();
        const std::string afterStart = host.takeFailureReport();

        EXPECT_EQ(linesOf(beforeStart + afterStart).size(), 1u) << beforeStart << afterStart;
    }

    // Several

    TEST_F(PluginFailureReportTest, aFailedPluginAndItsDependantHaveALineEach)
    {
        install(kA);
        install(kB);
        install(kC);
        setEnvironment("OPENEDEN_TEST_PLUGIN_THROW", kA + ":start");

        OMW::PluginHost host(config({ kA, kB, kC }));
        host.start();

        const std::string report = host.takeFailureReport();
        const std::vector<std::string> lines = linesOf(report);
        ASSERT_EQ(lines.size(), 2u) << report;
        EXPECT_THAT(lines[0], HasSubstr(kA));
        EXPECT_THAT(lines[0], HasSubstr("test throw " + kA + ":start"));
        EXPECT_THAT(lines[1], HasSubstr(kB));
        EXPECT_THAT(report, Not(HasSubstr(kC)));
        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
    }

    TEST_F(PluginFailureReportTest, aLoadFailureAndAStartFailureAreBothReported)
    {
        const fs::path directoryA = install(kA);
        fs::remove(directoryA / builtLibrary(kA).filename());
        install(kC);
        setEnvironment("OPENEDEN_TEST_PLUGIN_THROW", kC + ":start");

        OMW::PluginHost host(config({ kA, kC }));
        host.start();

        const std::string report = host.takeFailureReport();
        EXPECT_EQ(linesOf(report).size(), 2u) << report;
        EXPECT_THAT(report, HasSubstr(kA));
        EXPECT_THAT(report, HasSubstr(kC));
    }

    TEST_F(PluginFailureReportTest, aNewFailureLaterIsReportedThenAndOnlyIt)
    {
        const fs::path directoryA = install(kA);
        fs::remove(directoryA / builtLibrary(kA).filename());
        install(kC);

        OMW::PluginHost host(config({ kA, kC }));
        host.start();
        runFrame(host);
        expectOneLineThenEmpty(host, kA);

        setEnvironment("OPENEDEN_TEST_PLUGIN_THROW", kC + ":phase");
        runFrame(host);

        const std::string report = host.takeFailureReport();
        const std::vector<std::string> lines = linesOf(report);
        ASSERT_EQ(lines.size(), 1u) << report;
        EXPECT_THAT(lines[0], HasSubstr(kC));
        EXPECT_THAT(lines[0], Not(HasSubstr(kA)));
        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
    }

    // The Morrowind pack, loaded from its built library

    TEST_F(PluginFailureReportTest, packLoadsStartsAndStopsInTheHost)
    {
        installPack();
        OMW::PluginHost host(config({ kPack }));
        EXPECT_TRUE(host.hasPlugins());
        host.start();
        runFrame(host);

        EXPECT_TRUE(host.lifecycle().isPluginActive(kPack));
        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
        host.stop();
        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
    }

    TEST_F(PluginFailureReportTest, packConverterIsCallableThroughTheRegistryFromAnotherLibrary)
    {
        installPack();
        install(kKf);

        OMW::PluginHost host(config({ kKf, kPack }));
        host.start();

        EXPECT_THAT(host.takeFailureReport(), IsEmpty());
        EXPECT_TRUE(host.lifecycle().isPluginActive(kPack));
        EXPECT_TRUE(host.lifecycle().isPluginActive(kKf));

        // The consumer's own record of the call, made inside its library.
        const std::vector<std::string> events = kfEvents();
        EXPECT_THAT(events, Contains("contract found"));
        EXPECT_THAT(events, Contains("converted"));
        EXPECT_THAT(events, Contains("glb magic ok"));
        EXPECT_THAT(events, Contains("sidecar names clip idle"));
        EXPECT_THAT(events, Contains("garbage refused"));
        for (const std::string& event : events)
            EXPECT_FALSE(event.starts_with("failed")) << event;
    }

    TEST_F(PluginFailureReportTest, consumerWithoutThePackIsDisabledAndReportedWithoutCallingAnything)
    {
        install(kKf);

        OMW::PluginHost host(config({ kKf }));
        host.start();

        EXPECT_FALSE(host.lifecycle().isPluginActive(kKf));
        EXPECT_THAT(kfEvents(), IsEmpty());
        expectOneLineThenEmpty(host, kKf);
    }
}
