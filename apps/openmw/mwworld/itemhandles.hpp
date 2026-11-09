#ifndef OPENMW_MWWORLD_ITEMHANDLES_H
#define OPENMW_MWWORLD_ITEMHANDLES_H

#include <cstddef>
#include <vector>

#include <openeden/game/crafting.hpp>

#include "ptr.hpp"

namespace MWWorld
{
    /// Maps the items of the game (a Ptr: a stack in an inventory or an object in the world) to the opaque
    /// openeden::sdk::game::ItemHandle that crosses the crafting contract (task 0085 move 2, stage 2c).
    ///
    /// A handle belongs to one ItemHandleScope and dies with it; a handle never names another item afterwards, even if
    /// its table slot is reused (each slot has a generation). Main thread only: the contract is for the main thread, and
    /// the table takes no lock. Allocation: the table's vector grows to the most handles alive at once and is reused.
    ///
    /// Lifetime of the Ptr itself is the caller's, exactly as before the contract: a handle keeps a copy of the Ptr, not
    /// the item alive. A caller that removes a stack from its container drops the scope (or asks for a new handle).
    class ItemHandleScope
    {
    public:
        ItemHandleScope() = default;
        ~ItemHandleScope();

        ItemHandleScope(const ItemHandleScope&) = delete;
        ItemHandleScope& operator=(const ItemHandleScope&) = delete;

        /// An empty Ptr gives kNoItem. The same Ptr (the same LiveCellRef) made twice in one scope gives the same
        /// handle; a different Ptr gives a different one, so an item that was replaced by another stack is a new handle.
        [[nodiscard]] openeden::sdk::game::ItemHandle make(const Ptr& ptr);

        /// Ends every handle of this scope. The scope can be used again afterwards.
        void release();

        [[nodiscard]] std::size_t size() const { return mHandles.size(); }

    private:
        std::vector<openeden::sdk::game::ItemHandle> mHandles;
    };

    /// The item a handle names, or an empty Ptr for kNoItem, a handle that was never made, or one whose scope has ended.
    [[nodiscard]] Ptr resolveItem(openeden::sdk::game::ItemHandle handle);

    /// Handles alive now, and the table's slots (for tests: the table must not grow without bound).
    [[nodiscard]] std::size_t liveItemHandleCount();
    [[nodiscard]] std::size_t itemHandleSlotCount();
    /// True if the free list has room for every slot, so that ending a scope never allocates.
    [[nodiscard]] bool itemHandleFreeListReserved();
}

#endif
