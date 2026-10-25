#pragma once

#include <openeden/entity.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Engine implementation of the entity manager.
     */
    class EntityManagerImpl : public openeden::sdk::EntityManager
    {
    public:
        EntityManagerImpl();
        ~EntityManagerImpl() override;

        // Non-copyable
        EntityManagerImpl(const EntityManagerImpl&) = delete;
        EntityManagerImpl& operator=(const EntityManagerImpl&) = delete;

        // Expose base class template overloads
        using openeden::sdk::EntityManager::attachData;
        using openeden::sdk::EntityManager::getData;

        openeden::sdk::EntityId createEntity() override;
        bool destroyEntity(openeden::sdk::EntityId id) override;
        [[nodiscard]] bool isValid(openeden::sdk::EntityId id) const override;

        bool attachData(openeden::sdk::EntityId id, std::string_view slotKey, void* data,
            openeden::sdk::EntityDataDeleter deleter = nullptr) override;

        [[nodiscard]] void* getData(openeden::sdk::EntityId id, std::string_view slotKey) const override;

        bool removeData(openeden::sdk::EntityId id, std::string_view slotKey) override;

        void registerLifecycleListener(
            std::string_view pluginId, openeden::sdk::EntityLifecycleListener* listener) override;

        void unregisterLifecycleListener(std::string_view pluginId) override;

        void dispatchLifecycleEvent(
            openeden::sdk::EntityLifecycleEvent event, openeden::sdk::EntityId entityId) override;

        /**
         * @brief Sets deterministic plugin load order for lifecycle dispatching.
         */
        void setPluginLoadOrder(const std::vector<std::string>& loadOrder);

    private:
        struct DataEntry
        {
            void* ptr = nullptr;
            openeden::sdk::EntityDataDeleter deleter = nullptr;
        };

        struct EntitySlot
        {
            std::uint32_t generation = 1;
            bool alive = false;
            std::map<std::string, DataEntry, std::less<>> dataSlots;
        };

        struct ListenerEntry
        {
            std::string pluginId;
            openeden::sdk::EntityLifecycleListener* listener = nullptr;
        };

        void sortListeners();

        std::vector<EntitySlot> mSlots;
        std::vector<std::uint32_t> mFreeIndices;
        std::vector<std::string> mLoadOrder;
        std::vector<ListenerEntry> mListeners;
    };
} // namespace openeden::plugin
