#include "mutate_queue.hpp"

#include "bindings_store.hpp"

#include <components/debug/debuglog.hpp>

#include <cstring>

namespace openeden::extension
{
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    namespace
    {
        constexpr std::size_t kDroppedFunctionsReserved = 1024;

        // The bytes and array slots a call needs in the arena
        void measure(std::span<const BindingValue> args, std::size_t& bytes, std::size_t& values)
        {
            bytes = 0;
            values = 0;
            for (const BindingValue& arg : args)
            {
                if (arg.type == BindingType::String)
                    bytes += arg.text.size();
                if (arg.count > 0 && arg.elements != nullptr)
                {
                    values += arg.count;
                    if (arg.type == BindingType::String)
                    {
                        for (std::size_t i = 0; i < arg.count; ++i)
                            bytes += arg.elements[i].text.size();
                    }
                }
            }
        }
    }

    MutateQueue::MutateQueue(std::size_t maxCalls, std::size_t arenaBytes, std::size_t arenaValues)
        : mMaxCalls(maxCalls)
        , mArenaBytes(arenaBytes)
        , mArenaValues(arenaValues)
    {
    }

    MutateQueue::~MutateQueue() = default;

    void MutateQueue::reserve()
    {
        const std::lock_guard lock(mMutex);
        if (mReserved)
            return;
        for (Batch& batch : mBatches)
        {
            batch.entries = std::make_unique<Entry[]>(mMaxCalls);
            batch.bytes = std::make_unique<char[]>(mArenaBytes);
            batch.values = std::make_unique<BindingValue[]>(mArenaValues);
            batch.clear();
        }
        mDropped.reserve(kDroppedFunctionsReserved);
        mReserved = true;
    }

    MutateQueue::PushResult MutateQueue::push(BoundFunction& function, std::span<const BindingValue> args)
    {
        const std::lock_guard lock(mMutex);

        std::size_t bytes = 0;
        std::size_t values = 0;
        measure(args, bytes, values);

        Batch& batch = mBatches[mActive];
        const bool fits = mReserved && args.size() <= openeden::sdk::kMaxBindingParameters
            && batch.entryCount < mMaxCalls && batch.byteCount + bytes <= mArenaBytes
            && batch.valueCount + values <= mArenaValues;
        if (!fits)
        {
            ++mDroppedTotal;
            ++function.droppedTotal;
            if (function.droppedSinceDrain++ == 0 && mDropped.size() < mDropped.capacity())
                mDropped.push_back(&function);
            return PushResult::Dropped;
        }

        Entry& entry = batch.entries[batch.entryCount++];
        entry.function = &function;
        entry.argc = static_cast<std::uint8_t>(args.size());
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            BindingValue& out = entry.args[i];
            out = args[i];
            if (out.type == BindingType::String && !out.text.empty())
            {
                char* where = batch.bytes.get() + batch.byteCount;
                std::memcpy(where, out.text.data(), out.text.size());
                batch.byteCount += out.text.size();
                out.text = std::string_view(where, out.text.size());
            }
            if (out.count > 0 && out.elements != nullptr)
            {
                BindingValue* slots = batch.values.get() + batch.valueCount;
                batch.valueCount += out.count;
                for (std::size_t k = 0; k < out.count; ++k)
                {
                    slots[k] = out.elements[k];
                    if (slots[k].type == BindingType::String && !slots[k].text.empty())
                    {
                        char* where = batch.bytes.get() + batch.byteCount;
                        std::memcpy(where, slots[k].text.data(), slots[k].text.size());
                        batch.byteCount += slots[k].text.size();
                        slots[k].text = std::string_view(where, slots[k].text.size());
                    }
                }
                out.elements = slots;
            }
        }
        return PushResult::Queued;
    }

    MutateQueue::DrainResult MutateQueue::drain()
    {
        DrainResult result;
        int index = 0;
        {
            const std::lock_guard lock(mMutex);
            if (!mReserved)
                return result;
            index = mActive;
            mActive ^= 1;
            for (BoundFunction* function : mDropped)
            {
                Log(Debug::Warning) << "core." << function->qualifiedName << ": " << function->droppedSinceDrain
                                    << " script call(s) were dropped because the queue of calls that change the game "
                                       "was full";
                function->droppedSinceDrain = 0;
                ++result.functionsWithDrops;
            }
            mDropped.clear();
        }

        Batch& batch = mBatches[index];
        for (std::size_t i = 0; i < batch.entryCount; ++i)
        {
            Entry& entry = batch.entries[i];
            BoundFunction& function = *entry.function;
            if (function.handler == nullptr)
            {
                ++result.retired;
                continue;
            }
            const Invoked invoked
                = BindingsStore::invoke(function, std::span<const BindingValue>(entry.args, entry.argc));
            ++result.ran;
            if (invoked.firstEscapedException)
                Log(Debug::Error) << "core." << function.qualifiedName
                                  << ": an exception escaped the game pack's handler (reported once per function)";
            if (invoked.result.status != BindingStatus::Ok)
            {
                ++result.failed;
                if (function.failedMutations++ == 0)
                    Log(Debug::Warning) << "core." << function.qualifiedName
                                        << ": a queued call failed: " << static_cast<int>(invoked.result.status) << ": "
                                        << invoked.result.message << " (reported once per function)";
            }
        }

        const std::lock_guard lock(mMutex);
        batch.clear();
        return result;
    }

    std::size_t MutateQueue::pending() const
    {
        const std::lock_guard lock(mMutex);
        return mReserved ? mBatches[mActive].entryCount : 0;
    }

    std::uint64_t MutateQueue::droppedTotal() const
    {
        const std::lock_guard lock(mMutex);
        return mDroppedTotal;
    }
}
