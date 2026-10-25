#pragma once

#include <cstdint>
#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief Stable entity handle valid across frames (slot index + generation).
     */
    struct EntityId
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        [[nodiscard]] constexpr bool isValid() const noexcept { return index != 0 && generation != 0; }

        auto operator<=>(const EntityId&) const = default;
    };

    /**
     * @brief Engine entity lifecycle event kinds.
     */
    enum class EntityLifecycleEvent
    {
        Spawned,
        CellLoaded,
        Died,
        Revived,
        CellUnloaded,
        Despawned,
    };

    /**
     * @brief Listener notified of entity lifecycle transitions in plugin load order.
     */
    class EntityLifecycleListener
    {
    public:
        virtual ~EntityLifecycleListener() = default;
        virtual void onEntityLifecycle(EntityLifecycleEvent event, EntityId entityId) = 0;
    };

    using EntityDataDeleter = void (*)(void*);

    /**
     * @brief Core entity manager managing stable IDs, lifecycle events, and per-entity plugin data.
     */
    class EntityManager
    {
    public:
        virtual ~EntityManager() = default;

        /**
         * @brief Allocates a new entity handle with a valid index and generation.
         */
        virtual EntityId createEntity() = 0;

        /**
         * @brief Despawns an entity, invoking attached data deleters and invalidating the handle.
         */
        virtual bool destroyEntity(EntityId id) = 0;

        /**
         * @brief Checks if an entity handle is valid and current.
         */
        [[nodiscard]] virtual bool isValid(EntityId id) const = 0;

        /**
         * @brief Attaches arbitrary data to an entity slot with a custom deleter.
         */
        virtual bool attachData(EntityId id, std::string_view slotKey, void* data, EntityDataDeleter deleter = nullptr)
            = 0;

        /**
         * @brief Typed helper that automatically attaches data with `delete static_cast<T*>(ptr)`.
         */
        template <typename T>
        bool attachData(EntityId id, std::string_view slotKey, T* data)
        {
            return attachData(id, slotKey, static_cast<void*>(data), [](void* ptr) { delete static_cast<T*>(ptr); });
        }

        /**
         * @brief Retrieves data from a named slot, or nullptr if absent or invalid.
         */
        [[nodiscard]] virtual void* getData(EntityId id, std::string_view slotKey) const = 0;

        /**
         * @brief Typed helper to retrieve data from a named slot.
         */
        template <typename T>
        [[nodiscard]] T* getData(EntityId id, std::string_view slotKey) const
        {
            return static_cast<T*>(getData(id, slotKey));
        }

        /**
         * @brief Removes a named slot, calling its deleter immediately.
         */
        virtual bool removeData(EntityId id, std::string_view slotKey) = 0;

        /**
         * @brief Registers an entity lifecycle listener for a plugin.
         */
        virtual void registerLifecycleListener(std::string_view pluginId, EntityLifecycleListener* listener) = 0;

        /**
         * @brief Unregisters an entity lifecycle listener.
         */
        virtual void unregisterLifecycleListener(std::string_view pluginId) = 0;

        /**
         * @brief Dispatches an entity lifecycle event to listeners in plugin load order.
         */
        virtual void dispatchLifecycleEvent(EntityLifecycleEvent event, EntityId entityId) = 0;
    };
} // namespace openeden::sdk
