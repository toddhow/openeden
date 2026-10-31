#pragma once

#include <openeden/phases.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>

namespace openeden::sdk
{
    using EventId = std::uint32_t;
    using RawEventCallback = std::function<void(const void* data, std::size_t size)>;

    /**
     * @brief How the core reads one field of a forwarded event's payload.
     */
    enum class ForwardedFieldType : std::uint8_t
    {
        Float32,
        Float64,
        Int32,
        UInt32,
        Boolean,
        String, ///< A fixed char array in the event; ends at its first NUL or at its size.
    };

    /**
     * @brief One flat payload field of a forwarded event. Fill it with forwardedField().
     */
    struct ForwardedField
    {
        std::string_view name;
        ForwardedFieldType type = ForwardedFieldType::Float32;
        std::size_t offset = 0;
        std::size_t size = 0;
    };

    /**
     * @brief Describes an event type the core forwards to Lua scripts as a Lua event.
     *
     * Plugins have no Lua API (decision 0003): no plugin code runs in Lua, the core copies the
     * payload. The payload is flat, only numbers, strings and booleans. Use
     * EventBus::markForwardedToLua<T>(name, fields) rather than filling this by hand.
     */
    struct ForwardedEventDesc
    {
        EventId id = 0;
        std::string_view pluginId;
        std::string_view name;
        std::size_t payloadSize = 0;
        std::span<const ForwardedField> fields;
    };

    /**
     * @brief Describes the member of event type T that a pointer to member refers to.
     *
     * Example: forwardedField("damage", &ScoreEvent::damage, offsetof(ScoreEvent, damage)).
     */
    template <typename T, typename Member>
    constexpr ForwardedField forwardedField(std::string_view name, Member T::*, std::size_t offset) noexcept
    {
        ForwardedFieldType type = ForwardedFieldType::String;
        if constexpr (std::is_same_v<Member, float>)
            type = ForwardedFieldType::Float32;
        else if constexpr (std::is_same_v<Member, double>)
            type = ForwardedFieldType::Float64;
        else if constexpr (std::is_same_v<Member, std::int32_t>)
            type = ForwardedFieldType::Int32;
        else if constexpr (std::is_same_v<Member, std::uint32_t>)
            type = ForwardedFieldType::UInt32;
        else if constexpr (std::is_same_v<Member, bool>)
            type = ForwardedFieldType::Boolean;
        else
            static_assert(std::is_array_v<Member> && std::is_same_v<std::remove_extent_t<Member>, char>,
                "Forwarded fields must be float, double, int32, uint32, bool or a char array");
        return ForwardedField{ name, type, offset, sizeof(Member) };
    }

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

        /**
         * @brief Marks typed event T as forwarded to Lua. Call it from registerServices() or start().
         */
        template <typename T>
        bool markForwardedToLua(
            std::string_view pluginId, std::string_view name, std::span<const ForwardedField> fields)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                "Event types published via raw memory slice must be trivially copyable");
            return markForwardedToLua(ForwardedEventDesc{ T::kEventId, pluginId, name, sizeof(T), fields });
        }

        /**
         * @brief Marks an event type as forwarded to Lua.
         *
         * Returns false, changing nothing, if the plugin ID or name is empty, the plugin already used
         * that name, a field has an empty or repeated name, a size wrong for its type or does not fit
         * inside the payload, or the event is already marked.
         */
        virtual bool markForwardedToLua(const ForwardedEventDesc& desc) = 0;
    };
} // namespace openeden::sdk
