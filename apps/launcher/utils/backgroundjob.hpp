#ifndef LAUNCHER_BACKGROUNDJOB_HPP
#define LAUNCHER_BACKGROUNDJOB_HPP

#include <QList>
#include <QObject>
#include <QThread>

#include <memory>
#include <optional>
#include <utility>

namespace Launcher
{
    /// Runs `work` on its own thread and calls `done` with the result on the thread of `owner` (the GUI thread).
    /// The mod organizer and the World LOD page scan folders and read files, which the GUI thread must not do
    /// (tasks 0057 and 0058). `work` must not touch widgets and must own what it reads (pass copies).
    /// Threads are kept in `threads`; call waitForJobs from the owner's destructor.
    template <class Work, class Done>
    void startBackgroundJob(QObject* owner, QList<QThread*>& threads, Work work, Done done)
    {
        using Result = decltype(work());
        auto result = std::make_shared<std::optional<Result>>();
        QThread* thread = QThread::create([result, work = std::move(work)]() mutable { result->emplace(work()); });
        thread->setParent(owner); // deleted with the owner if the finish is never delivered
        threads.append(thread);
        QObject::connect(
            thread, &QThread::finished, owner, [thread, result, done = std::move(done), &threads]() mutable {
                threads.removeOne(thread);
                thread->deleteLater();
                if (result->has_value())
                    done(std::move(**result));
            });
        thread->start();
    }

    inline void waitForJobs(QList<QThread*>& threads)
    {
        for (QThread* thread : threads)
            thread->wait();
        threads.clear();
    }
}

#endif
