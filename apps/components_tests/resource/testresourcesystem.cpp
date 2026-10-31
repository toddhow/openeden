#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>
#include <components/vsgutil/readnode.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vsg/nodes/Node.h>
#include <vsg/state/Sampler.h>

#include <atomic>
#include <thread>
#include <vector>

namespace
{
    using namespace testing;

    // Was scenemanager_getinstance_should_be_thread_safe. SceneManager::getInstance became vsgUtil::readNode with the
    // resource system's nodeOptions, and like getInstance it gives a stand-in for a file that does not exist
    // (Resource::Fallback, createNodeOptions in resourcesystem.cpp).
    TEST(ResourceResourceSystem, read_node_should_be_thread_safe)
    {
        const VFS::Manager vfsManager;
        Resource::ResourceSystem resourceSystem(&vfsManager, "", vsg::Sampler::create(), false, 0, 0);

        std::atomic<int> loaded{ 0 };
        std::vector<std::thread> threads;

        for (int i = 0; i < 50; ++i)
        {
            threads.emplace_back([&]() {
                if (vsgUtil::readNode("meshes/whatever.nif", resourceSystem.nodeOptions))
                    ++loaded;
            });
        }
        for (std::thread& thread : threads)
            thread.join();

        EXPECT_EQ(loaded, 50);
    }
}
