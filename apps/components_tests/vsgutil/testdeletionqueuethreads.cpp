#include <components/vsgutil/deletionqueue.hpp>

#include <vsg/core/Inherit.h>
#include <vsg/core/Object.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace
{
    // Counts how many of its kind have been destroyed, so that a lost or twice-released object shows up.
    std::atomic<std::size_t> sDestroyed{ 0 };

    class Counted : public vsg::Inherit<vsg::Object, Counted>
    {
    public:
        ~Counted() override { sDestroyed.fetch_add(1); }
    };

    // The game detaches nodes from the main thread and from the terrain preload thread
    // (Terrain::Preload::setPositions, through Paging::updateView), while the main thread advances the queue once a
    // frame. Every object that is added must be released exactly once, and nothing may crash.
    TEST(DeletionQueueThreads, objectsAddedFromWorkerThreadsAreEachReleasedOnce)
    {
        constexpr std::size_t numWorkers = 4;
        constexpr std::size_t perWorker = 20000;
        constexpr std::size_t numFrames = 4;
        sDestroyed = 0;

        {
            vsgUtil::DeletionQueue queue{ numFrames };
            std::atomic<bool> go{ false };
            std::atomic<std::size_t> finished{ 0 };

            std::vector<std::thread> workers;
            for (std::size_t w = 0; w < numWorkers; ++w)
                workers.emplace_back([&] {
                    while (!go.load())
                        std::this_thread::yield();
                    for (std::size_t i = 0; i < perWorker; ++i)
                        queue.add(Counted::create());
                    finished.fetch_add(1);
                });

            go = true;
            // The main thread's frames run while the workers add.
            while (finished.load() < numWorkers)
                queue.advanceFrame();
            for (auto& worker : workers)
                worker.join();

            // Flushing: after as many advances as the queue is long, nothing is held any more.
            for (std::size_t i = 0; i < numFrames; ++i)
                queue.advanceFrame();
            EXPECT_EQ(sDestroyed.load(), numWorkers * perWorker);
        }
    }

    // An object whose destructor adds to the queue again (a node tree that detaches its children) must not deadlock
    // the queue.
    class Reentrant : public vsg::Inherit<vsg::Object, Reentrant>
    {
    public:
        Reentrant(vsgUtil::DeletionQueue& queue)
            : mQueue(queue)
        {
        }
        ~Reentrant() override { mQueue.add(Counted::create()); }

    private:
        vsgUtil::DeletionQueue& mQueue;
    };

    TEST(DeletionQueueThreads, addingFromADestructorDuringAdvanceDoesNotDeadlock)
    {
        sDestroyed = 0;
        vsgUtil::DeletionQueue queue{ 2 };
        queue.add(Reentrant::create(queue));
        queue.advanceFrame();
        queue.advanceFrame();
        queue.advanceFrame();
        queue.advanceFrame();
        EXPECT_EQ(sDestroyed.load(), 1u);
    }
}
