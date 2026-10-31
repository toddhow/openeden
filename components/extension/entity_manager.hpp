#pragma once

#include <openeden/entity.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::extension
{
    /**
     * @brief What the game knows about entities that a test host does not. Implemented by the host.
     */
    class EntityWorld
    {
    public:
        virtual ~EntityWorld() = default;
        [[nodiscard]] virtual openeden::sdk::EntityId playerEntity() const = 0;
        [[nodiscard]] virtual std::string recordId(openeden::sdk::EntityId id) const = 0;
    };

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

        /**
         * @brief Sets who answers playerEntity and recordId. Null, the default, answers nothing.
         */
        void setWorld(EntityWorld* world) noexcept { mWorld = world; }

        [[nodiscard]] openeden::sdk::EntityId playerEntity() const override;
        [[nodiscard]] std::string recordId(openeden::sdk::EntityId id) const override;

        bool attachData(openeden::sdk::EntityId id, std::string_view slotKey, void* data,
            openeden::sdk::EntityDataDeleter deleter = nullptr) override;

        [[nodiscard]] void* getData(openeden::sdk::EntityId id, std::string_view slotKey) const override;

        bool removeData(openeden::sdk::EntityId id, std::string_view slotKey) override;

        void registerLifecycleListener(
            std::string_view extensionId, openeden::sdk::EntityLifecycleListener* listener) override;

        void unregisterLifecycleListener(std::string_view extensionId) override;

        void dispatchLifecycleEvent(
            openeden::sdk::EntityLifecycleEvent event, openeden::sdk::EntityId entityId) override;

        /**
         * @brief Sets deterministic extension load order for lifecycle dispatching.
         */
        void setExtensionLoadOrder(const std::vector<std::string>& loadOrder);

        /// Runs the deleters of all entity data and forgets listeners, while extension libraries are still loaded.
        void releaseExtensionObjects();

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
            std::string extensionId;
            openeden::sdk::EntityLifecycleListener* listener = nullptr;
        };

        void sortListeners();

        std::vector<EntitySlot> mSlots;
        std::vector<std::uint32_t> mFreeIndices;
        std::vector<std::string> mLoadOrder;
        std::vector<ListenerEntry> mListeners;
        EntityWorld* mWorld = nullptr;
    };
} // namespace openeden::extension
