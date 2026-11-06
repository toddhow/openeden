// Task 0091, stage e: background tasks for editor modules. Written from the spec alone, against a stub.
//
// Pinned (see taskrunner.hpp): the work function runs off the calling thread; progress (clamped, coalesced, in order)
// and done (once) are delivered only by running the closures given to Post, on the thread that does; cancelTask gives
// done(Cancelled); a throwing work function gives done(Failed) with the text; cancelAsset and cancelAll wait for the
// work functions and drop their callbacks (a queued task never starts); the destructor cancels and joins.
#include "apps/opencs/model/module/taskrunner.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace CSMModule;
    using namespace openeden::editor;
    using namespace std::chrono_literals;

    /// What a UI thread's event loop is, for the tests: closures wait in a queue until pump() runs them here.
    class FakeUi
    {
    public:
        TaskRunner::Post post()
        {
            return [this](std::function<void()> closure) {
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    mQueue.push_back(std::move(closure));
                }
                mWake.notify_all();
            };
        }

        /// Runs what is queued now.
        std::size_t pump()
        {
            std::vector<std::function<void()>> closures;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                closures.swap(mQueue);
            }
            for (auto& closure : closures)
                closure();
            return closures.size();
        }

        /// Waits (at most 5 s) until at least \a count closures are queued.
        bool waitQueued(std::size_t count)
        {
            std::unique_lock<std::mutex> lock(mMutex);
            return mWake.wait_for(lock, 5s, [&] { return mQueue.size() >= count; });
        }

        std::size_t queued()
        {
            std::lock_guard<std::mutex> lock(mMutex);
            return mQueue.size();
        }

    private:
        std::mutex mMutex;
        std::condition_variable mWake;
        std::vector<std::function<void()>> mQueue;
    };

    /// A latch a work function can block on until the test lets it go.
    class Gate
    {
    public:
        void open()
        {
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mOpen = true;
            }
            mWake.notify_all();
        }
        bool wait()
        {
            std::unique_lock<std::mutex> lock(mMutex);
            return mWake.wait_for(lock, 5s, [&] { return mOpen; });
        }

    private:
        std::mutex mMutex;
        std::condition_variable mWake;
        bool mOpen = false;
    };

    struct Result
    {
        std::vector<double> mProgress;
        std::vector<TaskOutcome> mOutcomes;
        std::string mError;
        std::thread::id mDoneThread;
        std::atomic<bool> mWorkReturned{ false };
        std::atomic<bool> mSawCancel{ false };
        std::thread::id mWorkThread;
    };

    TaskDesc task(Result& result, std::function<void(TaskControl&)> work, std::string assetId = {})
    {
        TaskDesc desc;
        desc.label = "test";
        desc.assetId = std::move(assetId);
        desc.work = [&result, work = std::move(work)](TaskControl& control) {
            result.mWorkThread = std::this_thread::get_id();
            work(control);
            result.mWorkReturned = true;
        };
        desc.progress = [&result](double fraction, const std::string&) { result.mProgress.push_back(fraction); };
        desc.done = [&result](TaskOutcome outcome, const std::string& error) {
            result.mOutcomes.push_back(outcome);
            result.mError = error;
            result.mDoneThread = std::this_thread::get_id();
        };
        return desc;
    }

    TEST(CSMTaskRunner, aTaskWithoutAWorkFunctionIsRefusedAndIdsAreNonZeroAndDistinct)
    {
        FakeUi ui;
        TaskRunner runner(ui.post());
        EXPECT_EQ(runner.start(TaskDesc()), 0u);
        Result a;
        Result b;
        const TaskId first = runner.start(task(a, [](TaskControl&) {}));
        const TaskId second = runner.start(task(b, [](TaskControl&) {}));
        EXPECT_NE(first, 0u);
        EXPECT_NE(second, 0u);
        EXPECT_NE(first, second);
        ASSERT_TRUE(ui.waitQueued(2));
        ui.pump();
    }

    TEST(CSMTaskRunner, theWorkRunsOffThisThreadAndDoneArrivesOnlyWhenTheUiPumpsOnThatThread)
    {
        FakeUi ui;
        TaskRunner runner(ui.post());
        Result result;
        runner.start(task(result, [](TaskControl&) {}));
        ASSERT_TRUE(ui.waitQueued(1));
        EXPECT_TRUE(result.mOutcomes.empty()); // queued, not delivered
        EXPECT_NE(result.mWorkThread, std::this_thread::get_id());
        EXPECT_EQ(runner.activeCount(), 1u);

        ui.pump();
        ASSERT_EQ(result.mOutcomes.size(), 1u);
        EXPECT_EQ(result.mOutcomes[0], TaskOutcome::Finished);
        EXPECT_EQ(result.mDoneThread, std::this_thread::get_id());
        EXPECT_EQ(runner.activeCount(), 0u);
        EXPECT_EQ(ui.pump(), 0u); // done once
    }

    TEST(CSMTaskRunner, progressIsClampedCoalescedAndComesBeforeDone)
    {
        FakeUi ui;
        TaskRunner runner(ui.post());
        Result result;
        runner.start(task(result, [](TaskControl& control) {
            control.reportProgress(-1.0, "low");
            for (int i = 1; i <= 9; ++i)
                control.reportProgress(i / 10.0, "step");
            control.reportProgress(7.0, "high");
        }));
        ASSERT_TRUE(ui.waitQueued(2)); // at least one progress closure and the done closure
        // Let the work finish before the UI looks: the progress is coalesced to the newest value.
        while (!result.mWorkReturned)
            std::this_thread::sleep_for(1ms);
        ui.pump();
        ASSERT_FALSE(result.mProgress.empty());
        EXPECT_LE(result.mProgress.size(), 11u);
        for (const double value : result.mProgress)
        {
            EXPECT_GE(value, 0.0);
            EXPECT_LE(value, 1.0);
        }
        EXPECT_EQ(result.mProgress.back(), 1.0); // the newest, clamped
        ASSERT_EQ(result.mOutcomes.size(), 1u);
    }

    TEST(CSMTaskRunner, aNonFiniteProgressValueIsTreatedAsZero)
    {
        FakeUi ui;
        TaskRunner runner(ui.post());
        Result result;
        runner.start(task(result, [](TaskControl& control) { control.reportProgress(std::nan(""), "x"); }));
        ASSERT_TRUE(ui.waitQueued(2));
        ui.pump();
        ASSERT_EQ(result.mProgress.size(), 1u);
        EXPECT_EQ(result.mProgress[0], 0.0);
    }

    TEST(CSMTaskRunner, cancelTaskReachesTheWorkAndDoneReportsCancelled)
    {
        FakeUi ui;
        TaskRunner runner(ui.post());
        Result result;
        Gate started;
        const TaskId id = runner.start(task(result, [&](TaskControl& control) {
            started.open();
            while (!control.cancelled())
                std::this_thread::sleep_for(1ms);
            result.mSawCancel = true;
        }));
        ASSERT_TRUE(started.wait());
        runner.cancelTask(id);
        ASSERT_TRUE(ui.waitQueued(1));
        ui.pump();
        EXPECT_TRUE(result.mSawCancel);
        ASSERT_EQ(result.mOutcomes.size(), 1u);
        EXPECT_EQ(result.mOutcomes[0], TaskOutcome::Cancelled);
        runner.cancelTask(id); // done already: safe
        runner.cancelTask(12345); // unknown: safe
    }

    TEST(CSMTaskRunner, aThrowingWorkFunctionIsFailedWithItsTextAndNothingEscapes)
    {
        FakeUi ui;
        TaskRunner runner(ui.post());
        Result named;
        Result unnamed;
        runner.start(task(named, [](TaskControl&) { throw std::runtime_error("boom"); }));
        runner.start(task(unnamed, [](TaskControl&) { throw 7; }));
        ASSERT_TRUE(ui.waitQueued(2));
        ui.pump();
        ASSERT_EQ(named.mOutcomes.size(), 1u);
        EXPECT_EQ(named.mOutcomes[0], TaskOutcome::Failed);
        EXPECT_EQ(named.mError, "boom");
        ASSERT_EQ(unnamed.mOutcomes.size(), 1u);
        EXPECT_EQ(unnamed.mOutcomes[0], TaskOutcome::Failed);
        EXPECT_FALSE(unnamed.mError.empty());
    }

    TEST(CSMTaskRunner, cancelAssetWaitsForItsWorkDropsItsCallbacksAndLeavesOtherAssetsAlone)
    {
        FakeUi ui;
        TaskRunner runner(ui.post());
        Result mine;
        Result other;
        Gate started;
        runner.start(task(
            mine,
            [&](TaskControl& control) {
                started.open();
                while (!control.cancelled())
                    std::this_thread::sleep_for(1ms);
                control.reportProgress(0.5, "after cancel");
            },
            "asset-a"));
        runner.start(task(
            other, [](TaskControl&) {}, "asset-b"));
        ASSERT_TRUE(started.wait());

        runner.cancelAsset("asset-a");
        EXPECT_TRUE(mine.mWorkReturned); // it waited
        ui.pump();
        EXPECT_TRUE(mine.mOutcomes.empty()); // no done
        EXPECT_TRUE(mine.mProgress.empty()); // and no progress after the cancel
        ASSERT_EQ(other.mOutcomes.size(), 1u);
        EXPECT_EQ(other.mOutcomes[0], TaskOutcome::Finished);
        EXPECT_EQ(runner.activeCount(), 0u);
    }

    TEST(CSMTaskRunner, cancelAssetAfterTheWorkFinishedButBeforeTheUiPumpedDropsDone)
    {
        FakeUi ui;
        TaskRunner runner(ui.post());
        Result result;
        runner.start(task(
            result, [](TaskControl&) {}, "asset-a"));
        ASSERT_TRUE(ui.waitQueued(1));
        runner.cancelAsset("asset-a");
        ui.pump();
        EXPECT_TRUE(result.mOutcomes.empty());
        EXPECT_EQ(runner.activeCount(), 0u);
    }

    TEST(CSMTaskRunner, cancelAllNeverStartsAQueuedTaskAndWaitsForTheRunningOne)
    {
        FakeUi ui;
        TaskRunner runner(ui.post(), 1); // one thread: the second task waits in the queue
        Result running;
        Result queued;
        Gate started;
        runner.start(task(running, [&](TaskControl& control) {
            started.open();
            while (!control.cancelled())
                std::this_thread::sleep_for(1ms);
        }));
        runner.start(task(queued, [](TaskControl&) {}));
        ASSERT_TRUE(started.wait());

        runner.cancelAll();
        EXPECT_TRUE(running.mWorkReturned);
        EXPECT_FALSE(queued.mWorkReturned); // never started
        ui.pump();
        EXPECT_TRUE(running.mOutcomes.empty());
        EXPECT_TRUE(queued.mOutcomes.empty());
        EXPECT_EQ(runner.activeCount(), 0u);

        // The runner is usable again.
        Result again;
        runner.start(task(again, [](TaskControl&) {}));
        ASSERT_TRUE(ui.waitQueued(1));
        ui.pump();
        ASSERT_EQ(again.mOutcomes.size(), 1u);
        EXPECT_EQ(again.mOutcomes[0], TaskOutcome::Finished);
    }

    TEST(CSMTaskRunner, theDestructorCancelsAndJoinsARunningTask)
    {
        FakeUi ui;
        Result result;
        Gate started;
        {
            TaskRunner runner(ui.post());
            runner.start(task(result, [&](TaskControl& control) {
                started.open();
                while (!control.cancelled())
                    std::this_thread::sleep_for(1ms);
            }));
            ASSERT_TRUE(started.wait());
        }
        EXPECT_TRUE(result.mWorkReturned);
        ui.pump(); // whatever it queued is dropped, and nothing dangles
        EXPECT_TRUE(result.mOutcomes.empty());
    }

    TEST(CSMTaskRunner, manyTasksOnAPoolAllFinishExactlyOnce)
    {
        FakeUi ui;
        TaskRunner runner(ui.post(), 3);
        constexpr int count = 40;
        std::vector<std::unique_ptr<Result>> results;
        for (int i = 0; i < count; ++i)
        {
            results.push_back(std::make_unique<Result>());
            runner.start(task(*results.back(), [](TaskControl& control) { control.reportProgress(0.5, "half"); }));
        }
        std::size_t delivered = 0;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (delivered < count && std::chrono::steady_clock::now() < deadline)
        {
            ui.pump();
            delivered = 0;
            for (const auto& result : results)
                delivered += result->mOutcomes.size();
            std::this_thread::sleep_for(1ms);
        }
        for (const auto& result : results)
            EXPECT_EQ(result->mOutcomes.size(), 1u);
        EXPECT_EQ(runner.activeCount(), 0u);
    }
}
