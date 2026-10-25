#include "entity_manager.hpp"

#include <algorithm>
#include <iostream>

namespace openeden::plugin
{
    EntityManagerImpl::EntityManagerImpl()
    {
        // Slot 0 is reserved as invalid
        mSlots.emplace_back();
    }

    EntityManagerImpl::~EntityManagerImpl()
    {
        // Clean up any remaining alive entity data slots
        for (std::size_t i = 1; i < mSlots.size(); ++i)
        {
            auto& slot = mSlots[i];
            if (slot.alive)
            {
                auto dataToClean = std::move(slot.dataSlots);
                slot.dataSlots.clear();
                slot.alive = false;

                for (auto& [key, data] : dataToClean)
                {
                    if (data.deleter && data.ptr)
                    {
                        try
                        {
                            data.deleter(data.ptr);
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }
        }
    }

    void EntityManagerImpl::sortListeners()
    {
        std::stable_sort(mListeners.begin(), mListeners.end(), [this](const ListenerEntry& a, const ListenerEntry& b) {
            auto itA = std::find(mLoadOrder.begin(), mLoadOrder.end(), a.pluginId);
            auto itB = std::find(mLoadOrder.begin(), mLoadOrder.end(), b.pluginId);

            const std::size_t rankA = (itA != mLoadOrder.end())
                ? static_cast<std::size_t>(std::distance(mLoadOrder.begin(), itA))
                : SIZE_MAX;
            const std::size_t rankB = (itB != mLoadOrder.end())
                ? static_cast<std::size_t>(std::distance(mLoadOrder.begin(), itB))
                : SIZE_MAX;

            return rankA < rankB;
        });
    }

    void EntityManagerImpl::setPluginLoadOrder(const std::vector<std::string>& loadOrder)
    {
        mLoadOrder = loadOrder;
        sortListeners();
    }

    openeden::sdk::EntityId EntityManagerImpl::createEntity()
    {
        std::uint32_t index = 0;
        if (!mFreeIndices.empty())
        {
            index = mFreeIndices.back();
            mFreeIndices.pop_back();
        }
        else
        {
            index = static_cast<std::uint32_t>(mSlots.size());
            mSlots.emplace_back();
        }

        auto& slot = mSlots[index];
        slot.alive = true;
        slot.dataSlots.clear();

        openeden::sdk::EntityId id;
        id.index = index;
        id.generation = slot.generation;
        return id;
    }

    bool EntityManagerImpl::destroyEntity(openeden::sdk::EntityId id)
    {
        if (!isValid(id))
            return false;

        auto& slot = mSlots[id.index];

        // 1. Immediately invalidate entity handle and alive state to prevent re-entrancy issues
        slot.alive = false;
        ++slot.generation;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }

        // 2. Dispatch Despawned lifecycle event in load order
        dispatchLifecycleEvent(openeden::sdk::EntityLifecycleEvent::Despawned, id);

        // 3. Move out and invoke deleters for all attached plugin data slots
        auto dataToClean = std::move(slot.dataSlots);
        slot.dataSlots.clear();

        for (auto& [key, entry] : dataToClean)
        {
            if (entry.deleter && entry.ptr)
            {
                try
                {
                    entry.deleter(entry.ptr);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden EntityManager: Exception in data deleter for slot '" << key
                              << "': " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden EntityManager: Unknown exception in data deleter for slot '" << key << "'\n";
                }
            }
        }

        mFreeIndices.push_back(id.index);
        return true;
    }

    bool EntityManagerImpl::isValid(openeden::sdk::EntityId id) const
    {
        if (!id.isValid() || id.index >= mSlots.size())
            return false;

        const auto& slot = mSlots[id.index];
        return slot.alive && slot.generation == id.generation;
    }

    bool EntityManagerImpl::attachData(
        openeden::sdk::EntityId id, std::string_view slotKey, void* data, openeden::sdk::EntityDataDeleter deleter)
    {
        if (!isValid(id))
            return false;

        auto& slot = mSlots[id.index];
        auto it = slot.dataSlots.find(slotKey);
        if (it != slot.dataSlots.end())
        {
            // Replace existing: call previous deleter first
            if (it->second.deleter && it->second.ptr)
            {
                try
                {
                    it->second.deleter(it->second.ptr);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden EntityManager: Exception in replaced deleter for slot '" << slotKey
                              << "': " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden EntityManager: Unknown exception in replaced deleter for slot '" << slotKey
                              << "'\n";
                }
            }
            it->second = { data, deleter };
        }
        else
        {
            slot.dataSlots.emplace(std::string(slotKey), DataEntry{ data, deleter });
        }
        return true;
    }

    void* EntityManagerImpl::getData(openeden::sdk::EntityId id, std::string_view slotKey) const
    {
        if (!isValid(id))
            return nullptr;

        const auto& slot = mSlots[id.index];
        // Transparent comparator: zero allocation string_view lookup
        auto it = slot.dataSlots.find(slotKey);
        if (it != slot.dataSlots.end())
        {
            return it->second.ptr;
        }
        return nullptr;
    }

    bool EntityManagerImpl::removeData(openeden::sdk::EntityId id, std::string_view slotKey)
    {
        if (!isValid(id))
            return false;

        auto& slot = mSlots[id.index];
        auto it = slot.dataSlots.find(slotKey);
        if (it == slot.dataSlots.end())
            return false;

        if (it->second.deleter && it->second.ptr)
        {
            try
            {
                it->second.deleter(it->second.ptr);
            }
            catch (const std::exception& e)
            {
                std::cerr << "OpenEden EntityManager: Exception in data deleter on remove for slot '" << slotKey
                          << "': " << e.what() << '\n';
            }
            catch (...)
            {
                std::cerr << "OpenEden EntityManager: Unknown exception in data deleter on remove for slot '" << slotKey
                          << "'\n";
            }
        }
        slot.dataSlots.erase(it);
        return true;
    }

    void EntityManagerImpl::registerLifecycleListener(
        std::string_view pluginId, openeden::sdk::EntityLifecycleListener* listener)
    {
        for (auto& entry : mListeners)
        {
            if (entry.pluginId == pluginId)
            {
                entry.listener = listener;
                sortListeners();
                return;
            }
        }
        mListeners.push_back({ std::string(pluginId), listener });
        sortListeners();
    }

    void EntityManagerImpl::unregisterLifecycleListener(std::string_view pluginId)
    {
        mListeners.erase(std::remove_if(mListeners.begin(), mListeners.end(),
                             [&](const ListenerEntry& e) { return e.pluginId == pluginId; }),
            mListeners.end());
    }

    void EntityManagerImpl::dispatchLifecycleEvent(
        openeden::sdk::EntityLifecycleEvent event, openeden::sdk::EntityId entityId)
    {
        // Listeners are pre-sorted by plugin load order: zero dynamic allocations on hot path
        for (const auto& entry : mListeners)
        {
            if (entry.listener)
            {
                try
                {
                    entry.listener->onEntityLifecycle(event, entityId);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden EntityManager: Exception in lifecycle listener for plugin '"
                              << entry.pluginId << "': " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden EntityManager: Unknown exception in lifecycle listener for plugin '"
                              << entry.pluginId << "'\n";
                }
            }
        }
    }
} // namespace openeden::plugin
