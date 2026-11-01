#pragma once

#include <openeden/event_bus.hpp>
#include <openeden/phases.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace openeden::extension
{
    struct ForwardedFieldRecord
    {
        std::string name;
        openeden::sdk::ForwardedFieldType type = openeden::sdk::ForwardedFieldType::Float32;
        std::size_t offset = 0;
        std::size_t size = 0;
    };

    /**
     * @brief The bus's own copy of a ForwardedEventDesc, which only holds views.
     */
    struct ForwardedEventRecord
    {
        std::string extensionId;
        std::string name;
        std::size_t payloadSize = 0;
        std::vector<ForwardedFieldRecord> fields;
    };

    /**
     * @brief One payload field of a forwarded event, ready for the engine to turn into a Lua value.
     */
    struct DecodedField
    {
        std::string name;
        std::variant<double, std::string, bool> value;
    };

    /**
     * @brief Reads a marked event's payload by its record.
     *
     * @return False, with the reason in error, if the payload is shorter than the record says or a string is not
     * valid UTF-8. A String field ends at its first NUL or at its size.
     */
    [[nodiscard]] bool decodeForwardedEvent(const ForwardedEventRecord& record, const void* data, std::size_t size,
        std::vector<DecodedField>& out, std::string& error);

    /**
     * @brief Called for each published event that is marked, in publish order, on the publishing thread. Extensions
     * must publish marked events on the main thread, so the game's sink needs no lock.
     */
    using ForwardSink = std::function<void(const ForwardedEventRecord& record, const void* data, std::size_t size)>;

    /**
     * @brief Engine implementation of the OpenEden event bus.
     * Provides zero-allocation hot paths for immediate and deferred event publishing.
     */
    class EventBusImpl : public openeden::sdk::EventBus
    {
    public:
        explicit EventBusImpl(std::size_t capacity = 64);
        ~EventBusImpl() override = default;

        // Non-copyable
        EventBusImpl(const EventBusImpl&) = delete;
        EventBusImpl& operator=(const EventBusImpl&) = delete;

        // Movable
        EventBusImpl(EventBusImpl&&) noexcept = default;
        EventBusImpl& operator=(EventBusImpl&&) noexcept = default;

        // Expose base class template overloads
        using openeden::sdk::EventBus::markForwardedToLua;
        using openeden::sdk::EventBus::publishDeferred;
        using openeden::sdk::EventBus::publishImmediate;
        using openeden::sdk::EventBus::subscribe;

        void publishImmediate(openeden::sdk::EventId id, const void* data, std::size_t size) override;

        void publishDeferred(
            openeden::sdk::EventId id, const void* data, std::size_t size, openeden::sdk::Phase targetPhase) override;

        void subscribe(
            openeden::sdk::EventId id, std::string_view extensionId, openeden::sdk::RawEventCallback callback) override;

        void unsubscribe(openeden::sdk::EventId id, std::string_view extensionId) override;

        /// Removes all of one extension's subscriptions. Not to be called while events are being delivered.
        void unsubscribeAll(std::string_view extensionId);

        /// Drops every object an extension left here, while its library is still loaded.
        void releaseExtensionObjects();

        /**
         * @brief Records an event type as forwarded to Lua. The engine reads the records in task 0016.
         */
        bool markForwardedToLua(const openeden::sdk::ForwardedEventDesc& desc) override;

        [[nodiscard]] bool isForwardedToLua(openeden::sdk::EventId id) const;

        /**
         * @brief Sets who receives marked events; the game forwards them to Lua. A deferred event arrives when it
         * is delivered, like a subscriber's. Unmarked events never reach it. Null stops it.
         */
        void setForwardSink(ForwardSink sink) { mForwardSink = std::move(sink); }

        /**
         * @brief Returns the recorded forwarded events, sorted by event ID.
         */
        [[nodiscard]] const std::map<openeden::sdk::EventId, ForwardedEventRecord>& forwardedEvents() const noexcept
        {
            return mForwardedEvents;
        }

        /**
         * @brief Sets the deterministic load order of extensions used for dispatching subscribers.
         */
        void setExtensionLoadOrder(const std::vector<std::string>& loadOrder);

        /**
         * @brief Flushes and delivers all deferred events queued for target phase in publish order.
         */
        void processDeferredEvents(openeden::sdk::Phase phase) override;

        /**
         * @brief Returns the total count of events dropped due to queue capacity saturation.
         */
        [[nodiscard]] std::size_t droppedCount() const noexcept;

    private:
        static constexpr std::size_t kInlinePayloadCapacity = 256;

        struct QueuedEvent
        {
            openeden::sdk::EventId id = 0;
            std::size_t size = 0;
            std::array<std::uint8_t, kInlinePayloadCapacity> inlinePayload{};
            std::vector<std::uint8_t> heapPayload;

            [[nodiscard]] const void* data() const noexcept
            {
                return size <= kInlinePayloadCapacity ? inlinePayload.data() : heapPayload.data();
            }
        };

        struct PhaseQueue
        {
            std::vector<QueuedEvent> ring;
            std::size_t head = 0;
            std::size_t tail = 0;
            std::size_t count = 0;
        };

        struct SubscriberEntry
        {
            std::string extensionId;
            openeden::sdk::RawEventCallback callback;
        };

        void sortSubscribers(std::vector<SubscriberEntry>& subs);

        std::size_t mCapacity;
        std::size_t mDroppedCount = 0;
        std::vector<std::string> mLoadOrder;

        // 6 phases from openeden::sdk::Phase
        std::map<openeden::sdk::Phase, PhaseQueue> mPhaseQueues;

        // EventId -> list of subscribers pre-sorted by load order
        std::map<openeden::sdk::EventId, std::vector<SubscriberEntry>> mSubscribers;

        std::map<openeden::sdk::EventId, ForwardedEventRecord> mForwardedEvents;
        ForwardSink mForwardSink;
    };
} // namespace openeden::extension
