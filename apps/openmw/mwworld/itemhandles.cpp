#include "itemhandles.hpp"

#include <cstdint>

namespace MWWorld
{
    using openeden::sdk::game::ItemHandle;
    using openeden::sdk::game::kNoItem;

    namespace
    {
        // A handle is (generation << 32) | (slot index + 1); zero is "no item". A slot's generation changes whenever it is
        // freed, so a handle from before the change no longer matches.
        constexpr std::uint64_t kIndexMask = 0xFFFFFFFFull;

        struct Slot
        {
            Ptr mPtr;
            std::uint32_t mGeneration = 1;
            bool mUsed = false;
        };

        struct Table
        {
            std::vector<Slot> mSlots;
            std::vector<std::uint32_t> mFree; // indices of unused slots
            std::size_t mLive = 0;
        };

        // Main thread only (itemhandles.hpp). A function-local static: no order problem for tests or start-up.
        Table& table()
        {
            static Table instance;
            return instance;
        }

        ItemHandle encode(std::uint32_t index, std::uint32_t generation)
        {
            return ItemHandle{ (static_cast<std::uint64_t>(generation) << 32) | (static_cast<std::uint64_t>(index) + 1) };
        }

        Slot* find(ItemHandle handle)
        {
            const std::uint64_t low = handle.value & kIndexMask;
            if (low == 0)
                return nullptr;
            Table& t = table();
            const std::uint64_t index = low - 1;
            if (index >= t.mSlots.size())
                return nullptr;
            Slot& slot = t.mSlots[static_cast<std::size_t>(index)];
            if (!slot.mUsed || slot.mGeneration != static_cast<std::uint32_t>(handle.value >> 32))
                return nullptr;
            return &slot;
        }

        void free(ItemHandle handle)
        {
            Slot* slot = find(handle);
            if (slot == nullptr)
                return;
            Table& t = table();
            slot->mPtr = Ptr();
            slot->mUsed = false;
            ++slot->mGeneration;
            if (slot->mGeneration == 0) // wrapped: never use generation zero, a handle would then be zero for slot -1
                slot->mGeneration = 1;
            t.mFree.push_back(static_cast<std::uint32_t>((handle.value & kIndexMask) - 1));
            --t.mLive;
        }
    }

    ItemHandleScope::~ItemHandleScope()
    {
        release();
    }

    ItemHandle ItemHandleScope::make(const Ptr& ptr)
    {
        if (ptr.isEmpty())
            return kNoItem;

        for (const ItemHandle existing : mHandles)
        {
            const Slot* slot = find(existing);
            if (slot != nullptr && slot->mPtr == ptr)
                return existing;
        }

        Table& t = table();
        std::uint32_t index;
        if (!t.mFree.empty())
        {
            index = t.mFree.back();
            t.mFree.pop_back();
        }
        else
        {
            index = static_cast<std::uint32_t>(t.mSlots.size());
            t.mSlots.emplace_back();
            // Room for every slot to be free at once: free() runs in ~ItemHandleScope, which cannot throw.
            if (t.mFree.capacity() < t.mSlots.size())
                t.mFree.reserve(t.mSlots.capacity());
        }
        Slot& slot = t.mSlots[index];
        slot.mPtr = ptr;
        slot.mUsed = true;
        ++t.mLive;

        const ItemHandle handle = encode(index, slot.mGeneration);
        mHandles.push_back(handle);
        return handle;
    }

    void ItemHandleScope::release()
    {
        for (const ItemHandle handle : mHandles)
            free(handle);
        mHandles.clear();
    }

    Ptr resolveItem(ItemHandle handle)
    {
        const Slot* slot = find(handle);
        return slot != nullptr ? slot->mPtr : Ptr();
    }

    std::size_t liveItemHandleCount()
    {
        return table().mLive;
    }

    std::size_t itemHandleSlotCount()
    {
        return table().mSlots.size();
    }

    bool itemHandleFreeListReserved()
    {
        const Table& t = table();
        return t.mFree.capacity() >= t.mSlots.size();
    }
}
