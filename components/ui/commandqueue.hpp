#ifndef OPENEDEN_COMPONENTS_UI_COMMANDQUEUE_H
#define OPENEDEN_COMPONENTS_UI_COMMANDQUEUE_H

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "elementprotocol.hpp"

namespace Ui
{
    /// The bridge between the thread that runs the scripts and the thread that owns the UI engine (task 0162). Scripts
    /// write through a `Backend` session (batched, committed once a frame); the engine thread calls `drain` to apply
    /// the committed batches, in order, to its `ElementProtocol`, and `pushEvent` to send an event back to the session
    /// that owns the target element.
    ///
    /// Threads: `Backend` calls from the session's own thread; `drain` and `pushEvent` from the engine thread; the
    /// queue is the only shared state and holds one mutex, taken briefly (to hand over a batch or an inbox, never while
    /// a command is applied). A commit of an empty batch takes no lock and allocates nothing. Owner: shared; a session
    /// keeps the queue alive. A session destroyed with documents still open destroys them (one batch).
    class CommandQueue : public std::enable_shared_from_this<CommandQueue>
    {
    public:
        /// Open sessions at most (the id carries 8 bits of session; 0 is not used).
        static constexpr unsigned kMaxSessions = 255;

        CommandQueue();
        ~CommandQueue();
        CommandQueue(const CommandQueue&) = delete;
        CommandQueue& operator=(const CommandQueue&) = delete;

        /// A new session. Throws std::runtime_error when kMaxSessions are open.
        std::unique_ptr<Backend> openSession();

        /// Engine thread: applies every committed batch to `target`, oldest first. Returns the number of commands.
        std::size_t drain(ElementProtocol& target);

        /// Engine thread: routes an event to the session that allocated `event.mTarget`; dropped if that session is
        /// gone or its inbox holds kMaxInboxEvents events.
        void pushEvent(Event event);

        static constexpr std::size_t kMaxInboxEvents = 4096;

        /// Committed batches not yet drained (tests).
        std::size_t pendingBatches() const;

    private:
        friend class QueueBackend;
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
