// Task 0065, stage B: the sample editor module uses the addition of API 1.3 (the simulation sandbox), as the reference
// for module authors and as a test. Written from the spec alone, before the sample changed.
//
// Contract pinned here (sdk/examples/sample_editor_module, id "sample"):
//  - A command "sample.sandboxProbe" (label "Probe Sandbox", menu Sample/Probe Sandbox) asks the context for its
//    simulation sandbox and writes ONE log line:
//      - no sandbox (a host without one): "Sandbox: this host has none",
//      - a sandbox with nothing built: "Sandbox: nothing built",
//      - a built sandbox: "Sandbox: <N> bodies, <M> pushes waiting" (N from bodyNames().size(), M from
//        pendingPushes()).
//    It changes nothing (no build, no push, no reset).
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

extern "C" const openeden::editor::EditorModuleDescriptor* openeden_editor_module_descriptor();

namespace CSMModule
{
    namespace
    {
        using namespace openeden::editor;

        class ProbeSandbox final : public SimulationSandbox
        {
        public:
            bool build(
                const std::filesystem::path&, const std::filesystem::path&, const Vec3&, double, std::string&) override
            {
                ++mCalls;
                return false;
            }
            bool built() const override { return mBuilt; }
            std::vector<std::string> bodyNames() const override
            {
                return mBuilt ? std::vector<std::string>{ "a", "b", "c" } : std::vector<std::string>();
            }
            bool push(std::string_view, const Vec3&, const Vec3&) override
            {
                ++mCalls;
                return false;
            }
            std::size_t pendingPushes() const override { return mBuilt ? 2 : 0; }
            bool bodyPose(std::string_view, SandboxBodyPose&) const override { return false; }
            bool reset(std::string&) override
            {
                ++mCalls;
                return false;
            }
            void release() override { ++mCalls; }

            bool mBuilt = false;
            int mCalls = 0;
        };

        class ProbeContext final : public EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<Command>) override {}
            void markDirty(std::string_view) override {}
            void log(LogLevel, std::string_view message) override { mLog.emplace_back(message); }
            void requestRedraw() override {}
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return nullptr; }
            SimulationSandbox* simulationSandbox() override { return mSandbox; }

            ProbeSandbox* mSandbox = nullptr;
            std::vector<std::string> mLog;
        };

        class Sample13Test : public ::testing::Test
        {
        protected:
            void load(ProbeSandbox* sandbox)
            {
                mContext.mSandbox = sandbox;
                mHost = std::make_unique<ModuleHost>(mRegistry, mContext);
                mHost->loadStatic("sample", *openeden_editor_module_descriptor(), ModuleSource::Origin::Extension);
                ASSERT_EQ(mHost->modules().at(0).mState, ModuleInfo::State::Loaded) << mHost->modules().at(0).mMessage;
            }
            void TearDown() override { mHost.reset(); }

            std::string probe()
            {
                mContext.mLog.clear();
                EXPECT_TRUE(mRegistry.runCommand("sample.sandboxProbe"));
                return mContext.mLog.size() == 1
                    ? mContext.mLog[0]
                    : std::string("<") + std::to_string(mContext.mLog.size()) + " log lines>";
            }

            ModuleRegistry mRegistry;
            ProbeContext mContext;
            std::unique_ptr<ModuleHost> mHost;
        };

        TEST_F(Sample13Test, theProbeCommandIsRegisteredInTheSampleMenu)
        {
            load(nullptr);
            bool found = false;
            for (const auto& entry : mRegistry.commands())
                if (entry.mDesc.id == "sample.sandboxProbe")
                {
                    found = true;
                    EXPECT_EQ(entry.mDesc.label, "Probe Sandbox");
                    EXPECT_EQ(entry.mDesc.menuPath, "Sample/Probe Sandbox");
                }
            EXPECT_TRUE(found);
        }

        TEST_F(Sample13Test, aHostWithoutASandboxIsReported)
        {
            load(nullptr);
            EXPECT_EQ(probe(), "Sandbox: this host has none");
        }

        TEST_F(Sample13Test, aSandboxWithNothingBuiltIsReported)
        {
            ProbeSandbox sandbox;
            load(&sandbox);
            EXPECT_EQ(probe(), "Sandbox: nothing built");
            EXPECT_EQ(sandbox.mCalls, 0);
        }

        TEST_F(Sample13Test, aBuiltSandboxReportsItsBodiesAndWaitingPushesAndChangesNothing)
        {
            ProbeSandbox sandbox;
            sandbox.mBuilt = true;
            load(&sandbox);
            EXPECT_EQ(probe(), "Sandbox: 3 bodies, 2 pushes waiting");
            EXPECT_EQ(sandbox.mCalls, 0);
        }
    }
}
