#ifndef CSM_MODULE_EVENTBUS_H
#define CSM_MODULE_EVENTBUS_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

namespace CSMModule
{
    /// How many events may wait while a dispatch is running. An event published from a handler waits for the current
    /// dispatch to finish (deterministic, first in first out); past this many the newest are dropped and counted, so a
    /// handler that publishes from every event cannot run away.
    inline constexpr std::size_t kMaxQueuedEvents = 256;

    /// Studio's editor event bus (task 0091 stage b, plan section 3.10): selection changed, asset opened, simulation
    /// stepped, parameter changed. Single threaded: publish and subscribe are called on the UI thread only, and
    /// handlers run on it in the order they were subscribed (across owners, so in module load order).
    ///
    /// Failure isolation: a handler that throws is removed, and the report callback is called once with the owner (the
    /// module id) and the text, which the registry turns into the module's withdrawal. Nothing the handler does to the
    /// bus (publishing, subscribing, unsubscribing, removing its owner) disturbs the dispatch that is running: the
    /// handlers present when it began are visited, those removed meanwhile are skipped.
    ///
    /// Ownership: owns the handlers (copies). No Qt, no allocation beyond the containers; main thread only.
    class EventBus
    {
    public:
        using Handler = openeden::editor::EventHandler;
        using ThrowReport = std::function<void(const std::string& owner, const std::string& what)>;

        /// \return the subscription's id (never 0), in registration order.
        std::uint64_t subscribe(std::string owner, Handler handler);
        /// Does nothing for an unknown id.
        void unsubscribe(std::uint64_t id);
        /// Removes every subscription of the owner.
        void removeOwner(const std::string& owner);

        /// Delivers to every subscriber, or queues behind the dispatch that is running.
        void publish(const openeden::editor::EditorEvent& event);

        void setThrowReport(ThrowReport report) { mReport = std::move(report); }

        std::size_t subscriberCount() const { return mSubscribers.size(); }
        /// Events dropped because the queue was full.
        std::size_t droppedCount() const { return mDropped; }

    private:
        struct Subscriber
        {
            std::uint64_t mId;
            std::string mOwner;
            Handler mHandler;
        };

        void deliver(const openeden::editor::EditorEvent& event);

        std::vector<Subscriber> mSubscribers;
        std::deque<openeden::editor::EditorEvent> mQueue;
        ThrowReport mReport;
        std::uint64_t mNextId = 1;
        std::size_t mDropped = 0;
        bool mDispatching = false;
    };

    /// The selection value Studio owns (task 0091 stage b). Changing it publishes SelectionChanged on the bus.
    ///
    /// Ownership: the bus is not owned and must outlive it.
    class SelectionModel
    {
    public:
        explicit SelectionModel(EventBus& bus)
            : mBus(bus)
        {
        }

        const openeden::editor::Selection& current() const { return mCurrent; }

        /// Publishes only when the value changes. An empty selection (kind empty) clears; its id is dropped.
        void select(const openeden::editor::Selection& selection);
        void clear() { select({}); }

    private:
        EventBus& mBus;
        openeden::editor::Selection mCurrent;
    };
}

#endif
