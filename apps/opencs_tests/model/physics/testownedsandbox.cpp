// Tests for CSMPhysics::OwnedSandbox and the registry's withdrawn handler (review of 2026-11-09, item C5): when an
// editor module is withdrawn (one of its callbacks threw, or it was unloaded) the ragdoll and ground it built in the
// simulation sandbox must be released, but never those of another module.
//
// In plain words: the sandbox is one shared object. Studio notes which module built what is in it; if that module goes,
// the sandbox is released; if some other module goes, it is left alone.

#include "apps/opencs/model/module/moduleregistry.hpp"
#include "apps/opencs/model/physics/ownedsandbox.hpp"

#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace CSMPhysics
{
    namespace
    {
        using namespace openeden::editor;

        // A sandbox that only counts what it is asked, so the tests need no physics world.
        class FakeSandbox final : public SimulationSandbox
        {
        public:
            bool build(const std::filesystem::path&, const std::filesystem::path&, const Vec3&, double,
                std::string& error) override
            {
                ++builds;
                if (failNextBuild)
                {
                    failNextBuild = false;
                    // A failed build leaves nothing (the interface's contract), the earlier ragdoll included.
                    isBuilt = false;
                    error = "refused on purpose";
                    return false;
                }
                isBuilt = true;
                return true;
            }
            bool built() const override { return isBuilt; }
            std::vector<std::string> bodyNames() const override
            {
                return isBuilt ? std::vector<std::string>{ "A" } : std::vector<std::string>();
            }
            bool push(std::string_view, const Vec3&, const Vec3&) override { return isBuilt; }
            std::size_t pendingPushes() const override { return 0; }
            bool bodyPose(std::string_view, SandboxBodyPose&) const override { return isBuilt; }
            bool reset(std::string&) override { return isBuilt; }
            void release() override
            {
                ++releases;
                isBuilt = false;
            }

            bool isBuilt = false;
            bool failNextBuild = false;
            int builds = 0;
            int releases = 0;
        };

        bool buildNow(OwnedSandbox& sandbox)
        {
            std::string error;
            return sandbox.build("rig", "profile", Vec3{}, 0.0, error);
        }

        CommandDesc throwingCommand(const std::string& id)
        {
            CommandDesc desc;
            desc.id = id;
            desc.label = "Fails";
            desc.menuPath = "Test/Fails";
            desc.handler = [](EditorContext&) { throw std::runtime_error("boom"); };
            return desc;
        }

        TEST(CSMPhysicsOwnedSandbox, remembersTheModuleThatBuiltWhatIsInTheSandbox)
        {
            FakeSandbox inner;
            std::string who = "creativemotion";
            OwnedSandbox sandbox(inner, [&] { return who; });
            ASSERT_TRUE(buildNow(sandbox));
            EXPECT_EQ(sandbox.owner(), "creativemotion");
            EXPECT_TRUE(sandbox.built());
        }

        TEST(CSMPhysicsOwnedSandbox, releasingForTheOwnerReleasesAndAnotherModuleDoesNothing)
        {
            FakeSandbox inner;
            OwnedSandbox sandbox(inner, [] { return std::string("mod.a"); });
            ASSERT_TRUE(buildNow(sandbox));
            sandbox.releaseIfOwnedBy("mod.b");
            EXPECT_TRUE(inner.isBuilt) << "another module going must not take this one's ragdoll";
            EXPECT_EQ(inner.releases, 0);
            sandbox.releaseIfOwnedBy("");
            EXPECT_TRUE(inner.isBuilt);
            sandbox.releaseIfOwnedBy("mod.a");
            EXPECT_FALSE(inner.isBuilt);
            EXPECT_EQ(inner.releases, 1);
            EXPECT_TRUE(sandbox.owner().empty());
        }

        TEST(CSMPhysicsOwnedSandbox, aNewBuildByAnotherModuleChangesTheOwner)
        {
            FakeSandbox inner;
            std::string who = "mod.a";
            OwnedSandbox sandbox(inner, [&] { return who; });
            ASSERT_TRUE(buildNow(sandbox));
            who = "mod.b";
            ASSERT_TRUE(buildNow(sandbox));
            sandbox.releaseIfOwnedBy("mod.a");
            EXPECT_TRUE(inner.isBuilt) << "mod.a no longer owns it";
            sandbox.releaseIfOwnedBy("mod.b");
            EXPECT_FALSE(inner.isBuilt);
        }

        TEST(CSMPhysicsOwnedSandbox, aFailedBuildLeavesNoOwner)
        {
            FakeSandbox inner;
            OwnedSandbox sandbox(inner, [] { return std::string("mod.a"); });
            ASSERT_TRUE(buildNow(sandbox));
            inner.failNextBuild = true;
            EXPECT_FALSE(buildNow(sandbox));
            EXPECT_TRUE(sandbox.owner().empty()) << "the old ragdoll went with the failed build";
        }

        TEST(CSMPhysicsOwnedSandbox, anUnknownOwnerIsOnlyReleasedByAnExplicitRelease)
        {
            FakeSandbox inner;
            OwnedSandbox sandbox(inner, [] { return std::string(); });
            ASSERT_TRUE(buildNow(sandbox));
            sandbox.releaseIfOwnedBy("mod.a");
            EXPECT_TRUE(inner.isBuilt);
            sandbox.release();
            EXPECT_FALSE(inner.isBuilt);
        }

        TEST(CSMPhysicsOwnedSandbox, releaseClearsTheOwner)
        {
            FakeSandbox inner;
            OwnedSandbox sandbox(inner, [] { return std::string("mod.a"); });
            ASSERT_TRUE(buildNow(sandbox));
            sandbox.release();
            EXPECT_TRUE(sandbox.owner().empty());
            sandbox.releaseIfOwnedBy("mod.a");
            EXPECT_EQ(inner.releases, 1) << "nothing left to release";
        }

        TEST(CSMPhysicsOwnedSandbox, theRegistryTellsTheHandlerWhenAModuleFailsOrIsRemoved)
        {
            CSMModule::ModuleRegistry registry;
            std::vector<std::string> withdrawn;
            registry.setWithdrawnHandler([&](const std::string& id) { withdrawn.push_back(id); });

            registry.beginModule("mod.fails");
            ASSERT_TRUE(registry.registerCommand(throwingCommand("mod.fails.cmd")).ok);
            registry.endModule();
            EXPECT_FALSE(registry.runCommand("mod.fails.cmd"));
            ASSERT_EQ(withdrawn.size(), 1u);
            EXPECT_EQ(withdrawn[0], "mod.fails");

            registry.removeModule("mod.unloaded");
            ASSERT_EQ(withdrawn.size(), 2u);
            EXPECT_EQ(withdrawn[1], "mod.unloaded");
        }

        TEST(CSMPhysicsOwnedSandbox, aFailingModuleLosesItsRagdollThroughTheRegistryAndOthersKeepTheirs)
        {
            FakeSandbox inner;
            OwnedSandbox sandbox(inner, [] { return std::string("mod.fails"); });
            CSMModule::ModuleRegistry registry;
            registry.setWithdrawnHandler([&](const std::string& id) { sandbox.releaseIfOwnedBy(id); });

            registry.beginModule("mod.fails");
            ASSERT_TRUE(registry.registerCommand(throwingCommand("mod.fails.cmd")).ok);
            registry.endModule();

            ASSERT_TRUE(buildNow(sandbox));
            registry.removeModule("mod.other");
            EXPECT_TRUE(inner.isBuilt) << "another module going must not release it";
            EXPECT_FALSE(registry.runCommand("mod.fails.cmd"));
            EXPECT_FALSE(inner.isBuilt) << "the module that built the ragdoll failed: it must be released";
        }
    }
}
