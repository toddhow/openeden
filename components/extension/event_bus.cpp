#include "event_bus.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace openeden::extension
{
    namespace
    {
        bool isValidUtf8(std::string_view text)
        {
            std::size_t i = 0;
            while (i < text.size())
            {
                const auto lead = static_cast<unsigned char>(text[i]);
                std::size_t extra = 0;
                std::uint32_t codePoint = 0;
                std::uint32_t minimum = 0;
                if (lead < 0x80)
                {
                    ++i;
                    continue;
                }
                else if ((lead & 0xE0) == 0xC0)
                {
                    extra = 1;
                    codePoint = lead & 0x1F;
                    minimum = 0x80;
                }
                else if ((lead & 0xF0) == 0xE0)
                {
                    extra = 2;
                    codePoint = lead & 0x0F;
                    minimum = 0x800;
                }
                else if ((lead & 0xF8) == 0xF0)
                {
                    extra = 3;
                    codePoint = lead & 0x07;
                    minimum = 0x10000;
                }
                else
                    return false;

                if (extra >= text.size() - i)
                    return false;
                for (std::size_t j = 1; j <= extra; ++j)
                {
                    const auto next = static_cast<unsigned char>(text[i + j]);
                    if ((next & 0xC0) != 0x80)
                        return false;
                    codePoint = (codePoint << 6) | (next & 0x3F);
                }
                if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
                    return false;
                i += extra + 1;
            }
            return true;
        }

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

    bool decodeForwardedEvent(const ForwardedEventRecord& record, const void* data, std::size_t size,
        std::vector<DecodedField>& out, std::string& error)
    {
        using openeden::sdk::ForwardedFieldType;

        if (size < record.payloadSize || (data == nullptr && record.payloadSize != 0))
        {
            error = "payload of " + std::to_string(size) + " bytes is shorter than the "
                + std::to_string(record.payloadSize) + " bytes marked";
            return false;
        }

        const auto* bytes = static_cast<const char*>(data);
        const auto read = [&](const ForwardedFieldRecord& field, auto value) {
            std::memcpy(&value, bytes + field.offset, sizeof(value));
            return value;
        };

        out.clear();
        out.reserve(record.fields.size());
        for (const ForwardedFieldRecord& field : record.fields)
        {
            std::variant<double, std::string, bool> value;
            switch (field.type)
            {
                case ForwardedFieldType::Float32:
                    value = static_cast<double>(read(field, float{}));
                    break;
                case ForwardedFieldType::Float64:
                    value = read(field, double{});
                    break;
                case ForwardedFieldType::Int32:
                    value = static_cast<double>(read(field, std::int32_t{}));
                    break;
                case ForwardedFieldType::UInt32:
                    value = static_cast<double>(read(field, std::uint32_t{}));
                    break;
                case ForwardedFieldType::Boolean:
                    value = read(field, char{}) != 0;
                    break;
                case ForwardedFieldType::String:
                {
                    std::string_view text(bytes + field.offset, field.size);
                    text = text.substr(0, text.find('\0'));
                    if (!isValidUtf8(text))
                    {
                        error = "field '" + field.name + "' is not valid UTF-8";
                        return false;
                    }
                    value = std::string(text);
                    break;
                }
            }
            out.push_back({ field.name, std::move(value) });
        }
        return true;
    }

    EventBusImpl::EventBusImpl(std::size_t capacity)
        : mCapacity(capacity)
    {
        // Pre-allocate fixed circular ring buffer for each phase (the 6 frame phases and the 2 game phases)
        const openeden::sdk::Phase allPhases[] = {
            openeden::sdk::Phase::Input,
            openeden::sdk::Phase::PreSimulation,
            openeden::sdk::Phase::PostSimulation,
            openeden::sdk::Phase::Animation,
            openeden::sdk::Phase::PreRender,
            openeden::sdk::Phase::Ui,
            openeden::sdk::Phase::GameSimulation,
            openeden::sdk::Phase::GameUpdateUi,
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
        // Sort subscribers by extension load order; extensions not in load order are placed at the end stably
        std::stable_sort(subs.begin(), subs.end(), [this](const SubscriberEntry& a, const SubscriberEntry& b) {
            auto itA = std::find(mLoadOrder.begin(), mLoadOrder.end(), a.extensionId);
            auto itB = std::find(mLoadOrder.begin(), mLoadOrder.end(), b.extensionId);

            const std::size_t rankA = (itA != mLoadOrder.end())
                ? static_cast<std::size_t>(std::distance(mLoadOrder.begin(), itA))
                : SIZE_MAX;
            const std::size_t rankB = (itB != mLoadOrder.end())
                ? static_cast<std::size_t>(std::distance(mLoadOrder.begin(), itB))
                : SIZE_MAX;

            return rankA < rankB;
        });
    }

    void EventBusImpl::setExtensionLoadOrder(const std::vector<std::string>& loadOrder)
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
        openeden::sdk::EventId id, std::string_view extensionId, openeden::sdk::RawEventCallback callback)
    {
        auto& subs = mSubscribers[id];
        // Replace existing subscription if present
        for (auto& entry : subs)
        {
            if (entry.extensionId == extensionId)
            {
                entry.callback = std::move(callback);
                return;
            }
        }

        subs.push_back({ std::string(extensionId), std::move(callback) });
        sortSubscribers(subs);
    }

    void EventBusImpl::unsubscribeAll(std::string_view extensionId)
    {
        for (auto& [id, subs] : mSubscribers)
        {
            subs.erase(std::remove_if(subs.begin(), subs.end(),
                           [&](const SubscriberEntry& entry) { return entry.extensionId == extensionId; }),
                subs.end());
        }
    }

    void EventBusImpl::releaseExtensionObjects()
    {
        mSubscribers.clear();
    }

    void EventBusImpl::unsubscribe(openeden::sdk::EventId id, std::string_view extensionId)
    {
        auto it = mSubscribers.find(id);
        if (it != mSubscribers.end())
        {
            auto& subs = it->second;
            subs.erase(std::remove_if(subs.begin(), subs.end(),
                           [&](const SubscriberEntry& entry) { return entry.extensionId == extensionId; }),
                subs.end());
        }
    }

    bool EventBusImpl::markForwardedToLua(const openeden::sdk::ForwardedEventDesc& desc)
    {
        if (desc.extensionId.empty() || desc.name.empty() || mForwardedEvents.contains(desc.id))
            return false;

        // Extension ID and name together become the Lua event's name, so one pair means one payload layout
        for (const auto& [id, other] : mForwardedEvents)
        {
            if (other.extensionId == desc.extensionId && other.name == desc.name)
                return false;
        }

        ForwardedEventRecord record;
        record.extensionId = desc.extensionId;
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
        // With nothing marked, this path costs one check and allocates nothing
        if (mForwardSink && !mForwardedEvents.empty())
        {
            const auto forwarded = mForwardedEvents.find(id);
            if (forwarded != mForwardedEvents.end())
            {
                try
                {
                    mForwardSink(forwarded->second, data, size);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden EventBus: Exception forwarding event " << id << ": " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden EventBus: Unknown exception forwarding event " << id << '\n';
                }
            }
        }

        auto it = mSubscribers.find(id);
        if (it == mSubscribers.end() || it->second.empty())
            return;

        // Subscribers are pre-sorted by extension load order: zero dynamic allocations on hot path
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
                              << " (extension: " << entry.extensionId << "): " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden EventBus: Unknown exception in subscriber for event " << id
                              << " (extension: " << entry.extensionId << ")\n";
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
} // namespace openeden::extension
