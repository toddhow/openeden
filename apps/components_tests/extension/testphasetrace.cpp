#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/misc/loadtrace.hpp>
#include <openeden/extension.hpp>
#include <openeden/host.hpp>
#include <openeden/phases.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{
    // Task 0323: every frame phase the lifecycle manager runs is one load trace scope named by
    // LifecycleManager::phaseScopeName, so the cost of the extensions and of a game pack (its callback runs in
    // GameSimulation) shows in the trace beside the engine's own scopes. Off, a scope records nothing.
    using openeden::extension::ExtensionManifest;
    using openeden::extension::LifecycleManager;
    using openeden::sdk::Extension;
    using openeden::sdk::Host;
    using openeden::sdk::HostInfo;
    using openeden::sdk::Phase;
    using openeden::sdk::Registrar;
    using openeden::sdk::ServiceLocator;

    class TraceHost : public Host
    {
    public:
        HostInfo mHostInfo{ openeden::sdk::kSdkVersion, "OpenEdenTraceHost" };
        const HostInfo& getHostInfo() const override { return mHostInfo; }
    };

    class PhaseExtension : public Extension
    {
    public:
        void registerServices(Registrar& registrar) override
        {
            registrar.registerPhaseCallback(Phase::GameSimulation, [](float) {});
            registrar.registerPhaseCallback(Phase::Input, [](float) {});
        }
        bool resolve(const ServiceLocator&) override { return true; }
        void start(Host&) override {}
        void stop() override {}
    };

    ExtensionManifest manifest()
    {
        ExtensionManifest m;
        m.id = "tracer";
        m.name = m.id;
        m.extensionVersion = "1.0.0";
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "test-toolchain";
        return m;
    }

    struct PhaseTrace : testing::Test
    {
        std::filesystem::path mPath = std::filesystem::temp_directory_path() / "openeden_phasetrace.jsonl";
        void SetUp() override { std::filesystem::remove(mPath); }
        void TearDown() override
        {
            Misc::LoadTrace::stop();
            std::filesystem::remove(mPath);
        }
        std::string readAll() const
        {
            std::ifstream in(mPath, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
    };

    TEST_F(PhaseTrace, aPhaseIsOneScopeNamedLikeItsProfilerScope)
    {
        TraceHost host;
        LifecycleManager manager(host);
        manager.addExtension(manifest(), std::make_unique<PhaseExtension>());
        ASSERT_TRUE(manager.initialize());
        Misc::LoadTrace::start(mPath.string());
        manager.runPhase(Phase::GameSimulation, 0.016f);
        Misc::LoadTrace::stop();
        const std::string text = readAll();
        EXPECT_NE(text.find("\"n\":\"phase.GameSimulation\""), std::string::npos) << text;
        EXPECT_EQ(text.find("phase.Input"), std::string::npos) << text;
        manager.shutdown();
    }

    TEST_F(PhaseTrace, offRecordsNothing)
    {
        TraceHost host;
        LifecycleManager manager(host);
        manager.addExtension(manifest(), std::make_unique<PhaseExtension>());
        ASSERT_TRUE(manager.initialize());
        manager.runPhase(Phase::GameSimulation, 0.016f);
        EXPECT_FALSE(Misc::LoadTrace::enabled());
        EXPECT_FALSE(std::filesystem::exists(mPath));
        manager.shutdown();
    }
}
