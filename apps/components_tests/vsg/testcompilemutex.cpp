// Task 0104: the viewer's compile and a loader thread's compile must not overlap (components/vsgutil/compilecontext.hpp).
// The race itself needs a device; these tests pin the rule the fix is built on: loader compiles share the lock, the
// viewer's compile excludes them, and there is one lock for the process.
#include <components/vsgutil/compilecontext.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace
{
    TEST(VsgCompileMutex, there_is_one_lock_for_the_process)
    {
        EXPECT_EQ(&vsgUtil::viewerCompileMutex(), &vsgUtil::viewerCompileMutex());
    }

    TEST(VsgCompileMutex, loader_compiles_may_overlap_each_other)
    {
        std::shared_lock first(vsgUtil::viewerCompileMutex());
        std::atomic<bool> second{ false };
        std::thread other([&] { second = vsgUtil::viewerCompileMutex().try_lock_shared(); if (second) vsgUtil::viewerCompileMutex().unlock_shared(); });
        other.join();
        EXPECT_TRUE(second);
    }

    TEST(VsgCompileMutex, the_viewers_compile_cannot_start_while_a_loader_compile_runs)
    {
        std::shared_lock loader(vsgUtil::viewerCompileMutex());
        std::atomic<bool> got{ true };
        std::thread viewer([&] { got = vsgUtil::viewerCompileMutex().try_lock(); if (got) vsgUtil::viewerCompileMutex().unlock(); });
        viewer.join();
        EXPECT_FALSE(got);
    }

    TEST(VsgCompileMutex, a_loader_compile_cannot_start_while_the_viewers_compile_runs)
    {
        std::unique_lock viewer(vsgUtil::viewerCompileMutex());
        std::atomic<bool> got{ true };
        std::thread loader([&] { got = vsgUtil::viewerCompileMutex().try_lock_shared(); if (got) vsgUtil::viewerCompileMutex().unlock_shared(); });
        loader.join();
        EXPECT_FALSE(got);
    }
}
