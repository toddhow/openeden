// Task 0065, stage B: the shapes of Editor Module API 1.3 (the simulation sandbox) and what hosts of other versions do
// with them. Written from the spec alone, before the host changed.
//
// What this pins:
//  - kApiVersion is {1, 3}.
//  - A context written for 1.0 (only the pure virtual functions) still compiles, and its simulationSandbox() answers
//    null: a module must cope with a host that has no sandbox.
//  - A module that declares 1.3 is refused by a host that provides 1.2, 1.1 or 1.0 with a message naming both versions;
//    modules that declare 1.0, 1.1, 1.2 or 1.3 load on a 1.3 host; and a module BUILT against the frozen 1.2 headers
//    (a separate shared library) loads and registers on today's host.
//  - The pose and sandbox types are plain data with nothing set by default (a unit quaternion at the origin).
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
    using namespace openeden::editor;

    class Context10 final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command>) override {}
        void markDirty(std::string_view) override {}
        void log(LogLevel, std::string_view) override {}
        void requestRedraw() override {}
        AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
        AssetDocument* activeAsset() override { return nullptr; }
    };

    class EmptyModule final : public EditorModule
    {
    public:
        void registerUi(EditorRegistrar&) override {}
        void shutdown() override {}
    };

    EditorModule* createEmpty()
    {
        return new EmptyModule();
    }

    void destroyEmpty(EditorModule* module)
    {
        delete module;
    }

    EditorModuleDescriptor descriptorFor(openeden::sdk::Version version)
    {
        return EditorModuleDescriptor{ version, "empty", &createEmpty, &destroyEmpty };
    }

    /// A sandbox that only answers "nothing", to show the interface can be implemented by a host or a test.
    class NullSandbox final : public SimulationSandbox
    {
    public:
        bool build(const std::filesystem::path&, const std::filesystem::path&, const Vec3&, double,
            std::string& error) override
        {
            error = "no";
            return false;
        }
        bool built() const override { return false; }
        std::vector<std::string> bodyNames() const override { return {}; }
        bool push(std::string_view, const Vec3&, const Vec3&) override { return false; }
        std::size_t pendingPushes() const override { return 0; }
        bool bodyPose(std::string_view, SandboxBodyPose&) const override { return false; }
        bool reset(std::string& error) override
        {
            error = "no";
            return false;
        }
        void release() override {}
    };

    class ContextWithSandbox final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command>) override {}
        void markDirty(std::string_view) override {}
        void log(LogLevel, std::string_view) override {}
        void requestRedraw() override {}
        AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
        AssetDocument* activeAsset() override { return nullptr; }
        SimulationSandbox* simulationSandbox() override { return &mSandbox; }

    private:
        NullSandbox mSandbox;
    };
}

TEST(EditorModuleApi13, theVersionIsOneDotThree)
{
    EXPECT_EQ(kApiVersion, (openeden::sdk::Version{ 1, 3 }));
}

TEST(EditorModuleApi13, aContextWrittenFor10HasNoSandbox)
{
    Context10 context;
    EXPECT_EQ(context.simulationSandbox(), nullptr);
}

TEST(EditorModuleApi13, aHostThatHasASandboxHandsItOut)
{
    ContextWithSandbox context;
    EditorContext& base = context;
    ASSERT_NE(base.simulationSandbox(), nullptr);
    std::string error;
    EXPECT_FALSE(base.simulationSandbox()->reset(error));
    EXPECT_FALSE(base.simulationSandbox()->built());
}

TEST(EditorModuleApi13, aPoseIsPlainDataAtTheOriginWithTheIdentityRotation)
{
    const SandboxBodyPose pose;
    EXPECT_EQ(pose.positionM.x, 0.0);
    EXPECT_EQ(pose.positionM.y, 0.0);
    EXPECT_EQ(pose.positionM.z, 0.0);
    EXPECT_EQ(pose.qx, 0.0);
    EXPECT_EQ(pose.qy, 0.0);
    EXPECT_EQ(pose.qz, 0.0);
    EXPECT_EQ(pose.qw, 1.0);
}

TEST(EditorModuleApi13, aModuleThatDeclaresOneThreeIsRefusedByOlderHostsAndNamesBothVersions)
{
    for (const std::uint16_t hostMinor : { std::uint16_t(0), std::uint16_t(1), std::uint16_t(2) })
    {
        CSMModule::ModuleRegistry registry;
        Context10 context;
        CSMModule::ModuleHost host(registry, context);
        host.setProvidedApiVersion({ 1, hostMinor });
        const EditorModuleDescriptor descriptor = descriptorFor(kApiVersion);
        host.loadStatic("empty", descriptor, CSMModule::ModuleSource::Origin::Extension);

        const auto modules = host.modules();
        ASSERT_EQ(modules.size(), 1u) << hostMinor;
        EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Failed) << hostMinor;
        EXPECT_NE(modules[0].mMessage.find("1.3"), std::string::npos) << modules[0].mMessage;
        EXPECT_NE(modules[0].mMessage.find(std::string("1.") + std::to_string(hostMinor)), std::string::npos)
            << modules[0].mMessage;
    }
}

TEST(EditorModuleApi13, modulesThatDeclareAnyEarlierMinorLoadOnAnOneThreeHost)
{
    for (const std::uint16_t minor : { std::uint16_t(0), std::uint16_t(1), std::uint16_t(2), std::uint16_t(3) })
    {
        CSMModule::ModuleRegistry registry;
        Context10 context;
        CSMModule::ModuleHost host(registry, context);
        const EditorModuleDescriptor descriptor = descriptorFor({ 1, minor });
        host.loadStatic("empty", descriptor, CSMModule::ModuleSource::Origin::Extension);

        const auto modules = host.modules();
        ASSERT_EQ(modules.size(), 1u) << "minor " << minor;
        EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Loaded) << modules[0].mMessage;
    }
}

TEST(EditorModuleApi13, aModuleBuiltAgainstTheFrozenOneTwoHeadersStillLoadsAndRegisters)
{
    CSMModule::ModuleRegistry registry;
    Context10 context;
    CSMModule::ModuleHost host(registry, context);
    host.load({ { "sample_old", OLD_API_1_2_MODULE_PATH, CSMModule::ModuleSource::Origin::Extension } });
    const auto modules = host.modules();
    ASSERT_EQ(modules.size(), 1u);
    EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Loaded) << modules[0].mMessage;
    EXPECT_EQ(registry.panels().size(), 1u);
    EXPECT_EQ(registry.gizmoProviders().size(), 1u);
}

TEST(EditorModuleApi13, aOneTwoModuleIsRefusedByAOneOneHostAndNamesBothVersions)
{
    CSMModule::ModuleRegistry registry;
    Context10 context;
    CSMModule::ModuleHost host(registry, context);
    host.setProvidedApiVersion({ 1, 1 });
    host.load({ { "sample_old", OLD_API_1_2_MODULE_PATH, CSMModule::ModuleSource::Origin::Extension } });
    const auto modules = host.modules();
    ASSERT_EQ(modules.size(), 1u);
    EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Failed);
    EXPECT_NE(modules[0].mMessage.find("1.2"), std::string::npos) << modules[0].mMessage;
    EXPECT_NE(modules[0].mMessage.find("1.1"), std::string::npos) << modules[0].mMessage;
}
