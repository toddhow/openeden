#include "event_bus.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace openeden::plugin
{
    namespace
    {
        // The core reads a field by its type, so the size must be the one that type needs
        bool hasValidSize(const openeden::sdk::ForwardedField& field)
        {
            using openeden::sdk::ForwardedFieldType;
            switch (field.type)
            {
                case ForwardedFieldType::Float32:
                    return field.size == sizeof(float);
                case ForwardedFieldType::Float64:
                    return field.size == sizeof(double);
                case ForwardedFieldType::Int32:
                case ForwardedFieldType::UInt32:
                    return field.size == sizeof(std::int32_t);
                case ForwardedFieldType::Boolean:
                    return field.size == sizeof(bool);
                case ForwardedFieldType::String:
                    return field.size > 0;
            }
            return false;
        }
    }

    EventBusImpl::EventBusImpl(std::size_t capacity)
        : mCapacity(capacity)
    {
        // Pre-allocate fixed circular ring buffer for each of the 6 phases
        const openeden::sdk::Phase allPhases[] = {
            openeden::sdk::Phase::Input,
            openeden::sdk::Phase::PreSimulation,
            openeden::sdk::Phase::PostSimulation,
            openeden::sdk::Phase::Animation,
            openeden::sdk::Phase::PreRender,
            openeden::sdk::Phase::Ui,
        };

        for (const auto phase : allPhases)
        {
            auto& q = mPhaseQueues[phase];
            q.ring.resize(mCapacity);
            q.head = 0;
            q.tail = 0;
            q.count = 0;
        }
    }

    void EventBusImpl::sortSubscribers(std::vector<SubscriberEntry>& subs)
    {
        // Sort subscribers by plugin load order; plugins not in load order are placed at the end stably
        std::stable_sort(subs.begin(), subs.end(), [this](const SubscriberEntry& a, const SubscriberEntry& b) {
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

    void EventBusImpl::setPluginLoadOrder(const std::vector<std::string>& loadOrder)
    {
        mLoadOrder = loadOrder;
        // Re-sort existing subscriptions in place
        for (auto& [id, subs] : mSubscribers)
        {
            sortSubscribers(subs);
        }
    }

    std::size_t EventBusImpl::droppedCount() const noexcept
    {
        return mDroppedCount;
    }

    void EventBusImpl::subscribe(
        openeden::sdk::EventId id, std::string_view pluginId, openeden::sdk::RawEventCallback callback)
    {
        auto& subs = mSubscribers[id];
        // Replace existing subscription if present
        for (auto& entry : subs)
        {
            if (entry.pluginId == pluginId)
            {
                entry.callback = std::move(callback);
                return;
            }
        }

        subs.push_back({ std::string(pluginId), std::move(callback) });
        sortSubscribers(subs);
    }

    void EventBusImpl::unsubscribeAll(std::string_view pluginId)
    {
        for (auto& [id, subs] : mSubscribers)
        {
            subs.erase(std::remove_if(subs.begin(), subs.end(),
                           [&](const SubscriberEntry& entry) { return entry.pluginId == pluginId; }),
                subs.end());
        }
    }

    void EventBusImpl::releasePluginObjects()
    {
        mSubscribers.clear();
    }

    void EventBusImpl::unsubscribe(openeden::sdk::EventId id, std::string_view pluginId)
    {
        auto it = mSubscribers.find(id);
        if (it != mSubscribers.end())
        {
            auto& subs = it->second;
            subs.erase(std::remove_if(subs.begin(), subs.end(),
                           [&](const SubscriberEntry& entry) { return entry.pluginId == pluginId; }),
                subs.end());
        }
    }

    bool EventBusImpl::markForwardedToLua(const openeden::sdk::ForwardedEventDesc& desc)
    {
        if (desc.pluginId.empty() || desc.name.empty() || mForwardedEvents.contains(desc.id))
            return false;

        // Plugin ID and name together become the Lua event's name, so one pair means one payload layout
        for (const auto& [id, other] : mForwardedEvents)
        {
            if (other.pluginId == desc.pluginId && other.name == desc.name)
                return false;
        }

        ForwardedEventRecord record;
        record.pluginId = desc.pluginId;
        record.name = desc.name;
        record.payloadSize = desc.payloadSize;
        record.fields.reserve(desc.fields.size());
        for (const auto& field : desc.fields)
        {
            // Written so that a huge offset cannot overflow past the check
            const bool fits = field.size <= desc.payloadSize && field.offset <= desc.payloadSize - field.size;
            const bool duplicate = std::any_of(record.fields.begin(), record.fields.end(),
                [&](const ForwardedFieldRecord& other) { return other.name == field.name; });
            if (field.name.empty() || !fits || duplicate || !hasValidSize(field))
                return false;

            record.fields.push_back({ std::string(field.name), field.type, field.offset, field.size });
        }

        mForwardedEvents.emplace(desc.id, std::move(record));
        return true;
    }

    bool EventBusImpl::isForwardedToLua(openeden::sdk::EventId id) const
    {
        return mForwardedEvents.contains(id);
    }

    void EventBusImpl::publishImmediate(openeden::sdk::EventId id, const void* data, std::size_t size)
    {
        auto it = mSubscribers.find(id);
        if (it == mSubscribers.end() || it->second.empty())
            return;

        // Subscribers are pre-sorted by plugin load order: zero dynamic allocations on hot path
        const auto& subs = it->second;
        for (const auto& entry : subs)
        {
            if (entry.callback)
            {
                try
                {
                    entry.callback(data, size);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden EventBus: Exception in subscriber for event " << id
                              << " (plugin: " << entry.pluginId << "): " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden EventBus: Unknown exception in subscriber for event " << id
                              << " (plugin: " << entry.pluginId << ")\n";
                }
            }
        }
    }

    void EventBusImpl::publishDeferred(
        openeden::sdk::EventId id, const void* data, std::size_t size, openeden::sdk::Phase targetPhase)
    {
        auto it = mPhaseQueues.find(targetPhase);
        if (it == mPhaseQueues.end())
            return;

        auto& queue = it->second;
        if (queue.count >= queue.ring.size())
        {
            ++mDroppedCount;
            std::cerr << "OpenEden EventBus: WARNING: Deferred queue capacity (" << queue.ring.size()
                      << ") saturated for phase " << static_cast<int>(targetPhase) << ". Dropping event " << id
                      << ". Total dropped: " << mDroppedCount << '\n';
            return;
        }

        // Write directly to pre-allocated circular ring buffer slot (zero heap allocation for size <= 256)
        auto& slot = queue.ring[queue.tail];
        slot.id = id;
        slot.size = size;

        if (size <= kInlinePayloadCapacity)
        {
            if (data && size > 0)
            {
                std::memcpy(slot.inlinePayload.data(), data, size);
            }
        }
        else
        {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            slot.heapPayload.assign(bytes, bytes + size);
        }

        queue.tail = (queue.tail + 1) % queue.ring.size();
        ++queue.count;
    }

    void EventBusImpl::processDeferredEvents(openeden::sdk::Phase phase)
    {
        auto it = mPhaseQueues.find(phase);
        if (it == mPhaseQueues.end() || it->second.count == 0)
            return;

        auto& queue = it->second;
        const std::size_t countToProcess = queue.count;

        // Process existing queued events in strict FIFO order without reallocating ring buffer
        for (std::size_t i = 0; i < countToProcess; ++i)
        {
            const auto& slot = queue.ring[queue.head];
            publishImmediate(slot.id, slot.data(), slot.size);

            queue.head = (queue.head + 1) % queue.ring.size();
            --queue.count;
        }
    }
} // namespace openeden::plugin
