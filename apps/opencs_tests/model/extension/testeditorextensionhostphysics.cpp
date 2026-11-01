// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include "apps/opencs/model/extension/editorextensionhost.hpp"
#include "apps/opencs/model/physics/studioworld.hpp"

#include <openeden/physics.hpp>

#include <gtest/gtest.h>

#include <filesystem>

// A real extension library is not built into eden-studio-tests, and the host cannot take a statically linked
// extension, so the PreSimulation, PostSimulation, Ui order is not tested here. What is tested is the host's physics
// attachment, and that stepping a host with no running extension does nothing and does not throw.
namespace CSMExtension
{
    namespace
    {
        class CSMExtensionEditorHostPhysicsTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                mDir = std::filesystem::temp_directory_path() / "eden-studio-ext-physics";
                std::filesystem::remove_all(mDir);
                std::filesystem::create_directories(mDir);
            }

            void TearDown() override { std::filesystem::remove_all(mDir); }

            EditorExtensionHost::Config config() const { return { { mDir }, {} }; }

            std::filesystem::path mDir;
        };

        TEST_F(CSMExtensionEditorHostPhysicsTest, withoutAttachingThereIsNoPhysicsService)
        {
            EditorExtensionHost host(config());
            host.start();
            EXPECT_EQ(host.getPhysicsQueryService(), nullptr);
        }

        TEST_F(CSMExtensionEditorHostPhysicsTest, attachingGivesAServiceBeforeStartWithThePublishedMasks)
        {
            CSMPhysics::StudioWorld world;
            EditorExtensionHost host(config());
            host.attachPhysicsWorld(world.system(), CSMPhysics::StudioWorld::sUnitsPerMeter);

            openeden::sdk::PhysicsQueryService* service = host.getPhysicsQueryService();
            ASSERT_NE(service, nullptr);
            EXPECT_EQ(service->queryMask("world"), CSMPhysics::StudioWorld::sLayerWorld);
            EXPECT_EQ(service->queryMask("dynamic_world"), CSMPhysics::StudioWorld::sLayerDynamicWorld);
            EXPECT_EQ(service->queryMask("no_such_mask"), openeden::sdk::kQueryMaskNone);

            host.start();
            EXPECT_NE(host.getPhysicsQueryService(), nullptr);
            host.stop();
        }

        TEST_F(CSMExtensionEditorHostPhysicsTest, steppingWithNoRunningExtensionDoesNothingAndDoesNotThrow)
        {
            EditorExtensionHost notStarted(config());
            EXPECT_NO_THROW(notStarted.beforeStep(1.0f / 60.0f));
            EXPECT_NO_THROW(notStarted.afterStep(1.0f / 60.0f));

            CSMPhysics::StudioWorld world;
            EditorExtensionHost host(config());
            host.attachPhysicsWorld(world.system(), CSMPhysics::StudioWorld::sUnitsPerMeter);
            host.start();
            EXPECT_FALSE(host.isRunning());
            EXPECT_NO_THROW(host.beforeStep(1.0f / 60.0f));
            EXPECT_NO_THROW(host.afterStep(1.0f / 60.0f));
            host.stop();
        }
    }
}
