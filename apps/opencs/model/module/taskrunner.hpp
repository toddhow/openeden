#ifndef CSM_MODULE_TASKRUNNER_H
#define CSM_MODULE_TASKRUNNER_H

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <openeden/editor_module/registrar.hpp>

namespace CSMModule
{
    /// Background tasks for editor modules (API 1.2, task 0091 stage e): a module's work function runs on a small pool
    /// of Studio's own threads with progress and cancel, and its result is delivered on the UI thread.
    ///
    /// Threads: start(), cancel*() and the posted callbacks belong to the UI thread; only the work function and the
    /// TaskControl it is given are called from a pool thread. The runner never touches Qt: how a closure reaches the UI
    /// thread is the Post function it is made with (Studio queues it to the main thread's event loop; tests collect
    /// the closures and run them by hand).
    ///
    /// Delivery: a task's `progress` callbacks come in order and coalesce (the newest wins when the UI is slow), then
    /// `done` once, with Finished (the work returned and nobody cancelled), Cancelled (cancelTask / cancelAll) or
    /// Failed (the work threw: the text goes with it). A task cancelled because its ASSET was closed or replaced
    /// (cancelAsset) or because the runner shuts down gets no `done` at all, and no progress after the cancel: the
    /// thing it would report to is gone.
    ///
    /// cancelAsset() and cancelAll() cancel AND WAIT for the work functions of those tasks to return, so that no work
    /// function runs on after the object it belongs to (a module's code, a document) is destroyed. A work function
    /// should check TaskControl::cancelled() often.
    ///
    /// Ownership: owns its threads and the tasks; joins in the destructor.
    class TaskRunner
    {
    public:
        using Post = std::function<void(std::function<void()>)>;

        /// \param threads the pool size (at least 1).
        explicit TaskRunner(Post post, std::size_t threads = 2);
        ~TaskRunner();

        TaskRunner(const TaskRunner&) = delete;
        TaskRunner& operator=(const TaskRunner&) = delete;

        /// \return the task's id (never 0), or 0 if the description has no work function.
        openeden::editor::TaskId start(openeden::editor::TaskDesc task);

        /// Asks the task to stop; its `done` then reports Cancelled (unless it had finished). Safe for an unknown id.
        void cancelTask(openeden::editor::TaskId id);
        /// Cancels every task of the asset, waits for their work functions, and drops their callbacks.
        void cancelAsset(const std::string& assetId);
        /// The same for all tasks; the runner stays usable.
        void cancelAll();

        /// Tasks started and not yet finished or dropped (waiting, running, or finished with `done` still queued).
        std::size_t activeCount() const;

    private:
        struct Task;
        /// The state that queued callbacks may still touch after the runner is gone: they hold it by shared_ptr.
        struct Shared;

        void workerLoop();
        void cancelAndWait(const std::vector<std::shared_ptr<Task>>& tasks);

        Post mPost;
        std::shared_ptr<Shared> mShared;
        std::vector<std::thread> mThreads;
    };
}

#endif
