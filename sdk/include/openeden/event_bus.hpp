#pragma once

#include <openeden/phases.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>

namespace openeden::sdk
{
    using EventId = std::uint32_t;
    using RawEventCallback = std::function<void(const void* data, std::size_t size)>;

    /**
     * @brief Core typed and raw event bus with immediate and phase-deferred dispatch.
     */
    class EventBus
    {
    public:
        virtual ~EventBus() = default;

        /**
         * @brief Synchronously publishes a typed event to all registered subscribers.
         */
        template <typename T>
        void publishImmediate(const T& event)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                "Event types published via raw memory slice must be trivially copyable");
            publishImmediate(T::kEventId, &event, sizeof(T));
        }

        /**
         * @brief Synchronously publishes a raw binary event.
         */
        virtual void publishImmediate(EventId id, const void* data, std::size_t size) = 0;

        /**
         * @brief Queues a typed event for delivery at the start of targetPhase.
         */
        template <typename T>
        void publishDeferred(const T& event, Phase targetPhase)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                "Event types published via raw memory slice must be trivially copyable");
            publishDeferred(T::kEventId, &event, sizeof(T), targetPhase);
        }

        /**
         * @brief Queues a raw binary event for delivery at targetPhase.
         */
        virtual void publishDeferred(EventId id, const void* data, std::size_t size, Phase targetPhase) = 0;

        /**
         * @brief Flushes and delivers all deferred events queued for target phase.
         */
        virtual void processDeferredEvents(Phase phase) {}

        /**
         * @brief Subscribes a typed callback invoked in plugin load order.
         */
        template <typename T, typename Callback>
        void subscribe(std::string_view pluginId, Callback&& callback)
        {
            subscribe(
                T::kEventId, pluginId, [cb = std::forward<Callback>(callback)](const void* data, std::size_t size) {
                    if (size >= sizeof(T))
                    {
                        cb(*static_cast<const T*>(data));
                    }
                });
        }

        /**
         * @brief Subscribes a raw callback invoked in plugin load order.
         */
        virtual void subscribe(EventId id, std::string_view pluginId, RawEventCallback callback) = 0;

        /**
         * @brief Unsubscribes a plugin from an event.
         */
        virtual void unsubscribe(EventId id, std::string_view pluginId) = 0;
    };
} // namespace openeden::sdk
