#include "taskrunner.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <mutex>

namespace CSMModule
{
    using namespace openeden::editor;

    /// The runner's state. Tasks and the closures posted for them hold it, so a closure that runs after the runner was
    /// destroyed finds it alive (and its task dropped).
    struct TaskRunner::Shared
    {
        std::mutex mMutex;
        std::condition_variable mWake; ///< Work queued or stopping.
        std::condition_variable mIdle; ///< A work function returned.
        std::deque<std::shared_ptr<Task>> mQueue;
        std::map<TaskId, std::shared_ptr<Task>> mTasks;
        TaskId mNextId = 1;
        bool mStopping = false;
    };

    /// One task. The flags are atomics (the work thread reads them through TaskControl); mStarted and mWorkDone are
    /// guarded by the shared mutex, the pending progress by its own.
    struct TaskRunner::Task final : public TaskControl, public std::enable_shared_from_this<Task>
    {
        TaskId mId = 0;
        TaskDesc mDesc;
        TaskRunner::Post mPost;
        std::shared_ptr<TaskRunner::Shared> mShared;

        std::atomic<bool> mCancelled{ false }; ///< cancelTask, cancelAsset, cancelAll
        std::atomic<bool> mDropped{ false }; ///< no callback may reach the UI any more
        bool mStarted = false;
        bool mWorkDone = false;

        std::mutex mProgressMutex;
        double mPendingFraction = 0.0;
        std::string mPendingMessage;
        bool mProgressPosted = false;

        bool cancelled() const override { return mCancelled.load(); }

        void reportProgress(double fraction, std::string_view message) override
        {
            if (mDropped.load() || !mDesc.progress)
                return;
            if (!std::isfinite(fraction))
                fraction = 0.0;
            fraction = std::min(1.0, std::max(0.0, fraction));
            {
                std::lock_guard<std::mutex> lock(mProgressMutex);
                mPendingFraction = fraction;
                mPendingMessage.assign(message);
                if (mProgressPosted)
                    return; // the closure already queued reads the newest value
                mProgressPosted = true;
            }
            mPost([self = shared_from_this()] {
                double value = 0.0;
                std::string text;
                {
                    std::lock_guard<std::mutex> lock(self->mProgressMutex);
                    value = self->mPendingFraction;
                    text = std::move(self->mPendingMessage);
                    self->mProgressPosted = false;
                }
                if (!self->mDropped.load() && self->mDesc.progress)
                    self->mDesc.progress(value, text);
            });
        }
    };

    TaskRunner::TaskRunner(Post post, std::size_t threads)
        : mPost(std::move(post))
        , mShared(std::make_shared<Shared>())
    {
        const std::size_t count = std::max<std::size_t>(1, threads);
        for (std::size_t i = 0; i < count; ++i)
            mThreads.emplace_back([this] { workerLoop(); });
    }

    TaskRunner::~TaskRunner()
    {
        cancelAll();
        {
            std::lock_guard<std::mutex> lock(mShared->mMutex);
            mShared->mStopping = true;
        }
        mShared->mWake.notify_all();
        for (std::thread& thread : mThreads)
            thread.join();
    }

    TaskId TaskRunner::start(TaskDesc desc)
    {
        if (!desc.work)
            return 0;
        auto task = std::make_shared<Task>();
        task->mDesc = std::move(desc);
        task->mPost = mPost;
        task->mShared = mShared;
        {
            std::lock_guard<std::mutex> lock(mShared->mMutex);
            task->mId = mShared->mNextId++;
            mShared->mTasks.emplace(task->mId, task);
            mShared->mQueue.push_back(task);
        }
        mShared->mWake.notify_one();
        return task->mId;
    }

    void TaskRunner::workerLoop()
    {
        const std::shared_ptr<Shared> shared = mShared;
        for (;;)
        {
            std::shared_ptr<Task> task;
            {
                std::unique_lock<std::mutex> lock(shared->mMutex);
                shared->mWake.wait(lock, [&] { return shared->mStopping || !shared->mQueue.empty(); });
                if (shared->mQueue.empty())
                    return; // stopping
                task = std::move(shared->mQueue.front());
                shared->mQueue.pop_front();
                task->mStarted = true;
            }

            TaskOutcome outcome = TaskOutcome::Finished;
            std::string error;
            try
            {
                task->mDesc.work(*task);
            }
            catch (const std::exception& e)
            {
                outcome = TaskOutcome::Failed;
                error = e.what();
            }
            catch (...)
            {
                outcome = TaskOutcome::Failed;
                error = "the task threw an exception that is not a std::exception";
            }
            if (outcome == TaskOutcome::Finished && task->mCancelled.load())
                outcome = TaskOutcome::Cancelled;

            if (task->mDropped.load())
            {
                std::lock_guard<std::mutex> lock(shared->mMutex);
                shared->mTasks.erase(task->mId);
            }
            else
            {
                task->mPost([task, outcome, text = std::move(error)] {
                    if (!task->mDropped.load() && task->mDesc.done)
                        task->mDesc.done(outcome, text);
                    std::lock_guard<std::mutex> lock(task->mShared->mMutex);
                    task->mShared->mTasks.erase(task->mId);
                });
            }
            {
                std::lock_guard<std::mutex> lock(shared->mMutex);
                task->mWorkDone = true;
            }
            shared->mIdle.notify_all();
        }
    }

    void TaskRunner::cancelTask(TaskId id)
    {
        std::lock_guard<std::mutex> lock(mShared->mMutex);
        const auto found = mShared->mTasks.find(id);
        if (found != mShared->mTasks.end())
            found->second->mCancelled = true;
    }

    void TaskRunner::cancelAndWait(const std::vector<std::shared_ptr<Task>>& tasks)
    {
        std::unique_lock<std::mutex> lock(mShared->mMutex);
        for (const std::shared_ptr<Task>& task : tasks)
        {
            task->mCancelled = true;
            task->mDropped = true; // no more callbacks; a closure of it that is still queued does nothing
        }
        // A task that never started is settled here, the others are waited for.
        for (const std::shared_ptr<Task>& task : tasks)
            if (!task->mStarted)
            {
                auto& queue = mShared->mQueue;
                queue.erase(std::remove(queue.begin(), queue.end(), task), queue.end());
                task->mWorkDone = true;
            }
        mShared->mIdle.wait(lock, [&] {
            return std::all_of(
                tasks.begin(), tasks.end(), [](const std::shared_ptr<Task>& task) { return task->mWorkDone; });
        });
        for (const std::shared_ptr<Task>& task : tasks)
            mShared->mTasks.erase(task->mId);
    }

    void TaskRunner::cancelAsset(const std::string& assetId)
    {
        if (assetId.empty())
            return;
        std::vector<std::shared_ptr<Task>> mine;
        {
            std::lock_guard<std::mutex> lock(mShared->mMutex);
            for (const auto& entry : mShared->mTasks)
                if (entry.second->mDesc.assetId == assetId)
                    mine.push_back(entry.second);
        }
        cancelAndWait(mine);
    }

    void TaskRunner::cancelAll()
    {
        std::vector<std::shared_ptr<Task>> all;
        {
            std::lock_guard<std::mutex> lock(mShared->mMutex);
            for (const auto& entry : mShared->mTasks)
                all.push_back(entry.second);
        }
        cancelAndWait(all);
    }

    std::size_t TaskRunner::activeCount() const
    {
        std::lock_guard<std::mutex> lock(mShared->mMutex);
        return mShared->mTasks.size();
    }
}
