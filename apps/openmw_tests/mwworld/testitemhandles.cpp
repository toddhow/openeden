// Task 0085 move 2, stage 2c: the table that maps items (Ptr) to the opaque handles of the crafting contract. Written
// from the decisions of 2026-11-08 (items cross the contract as an opaque handle the engine maps back to a Ptr) before
// the implementation.
#include "apps/openmw/mwclass/static.hpp"
#include "apps/openmw/mwworld/itemhandles.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/ptr.hpp"

#include <components/esm3/cellref.hpp>
#include <components/esm3/loadstat.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace MWWorld
{
    namespace
    {
        using openeden::sdk::game::ItemHandle;
        using openeden::sdk::game::kNoItem;

        struct Item
        {
            ESM::Static mRecord;
            LiveCellRef<ESM::Static> mRef;

            explicit Item(std::uint32_t index)
                : mRef(makeCellRef(index), &mRecord)
            {
            }

            static ESM::CellRef makeCellRef(std::uint32_t index)
            {
                // A LiveCellRef looks its class up when it is made, before this constructor's body would run.
                static const bool registered = (MWClass::Static::registerSelf(), true);
                (void)registered;
                ESM::CellRef cellRef;
                cellRef.blank();
                cellRef.mRefNum = ESM::RefNum{ .mIndex = index, .mContentFile = 1 };
                return cellRef;
            }

            Ptr ptr() { return Ptr(&mRef); }
        };

        TEST(MWWorldItemHandles, a_handle_resolves_to_the_item_it_was_made_from)
        {
            Item item(1);
            ItemHandleScope scope;
            const ItemHandle handle = scope.make(item.ptr());
            EXPECT_FALSE(handle.empty());
            EXPECT_EQ(resolveItem(handle), item.ptr());
            EXPECT_FALSE(resolveItem(handle).isEmpty());
        }

        TEST(MWWorldItemHandles, an_empty_ptr_is_no_item_and_no_item_is_an_empty_ptr)
        {
            ItemHandleScope scope;
            EXPECT_EQ(scope.make(Ptr()), kNoItem);
            EXPECT_TRUE(resolveItem(kNoItem).isEmpty());
            EXPECT_EQ(scope.size(), 0u);
        }

        TEST(MWWorldItemHandles, a_handle_nobody_made_resolves_to_nothing)
        {
            for (std::uint64_t value : { std::uint64_t{ 1 }, std::uint64_t{ 2 }, std::uint64_t{ 0xFFFFFFFFull },
                     std::uint64_t{ 0x100000001ull }, ~std::uint64_t{ 0 } })
                EXPECT_TRUE(resolveItem(ItemHandle{ value }).isEmpty()) << value;
        }

        TEST(MWWorldItemHandles, the_same_item_made_twice_in_a_scope_is_one_handle)
        {
            Item item(1);
            ItemHandleScope scope;
            const ItemHandle first = scope.make(item.ptr());
            const ItemHandle second = scope.make(item.ptr());
            EXPECT_EQ(first, second);
            EXPECT_EQ(scope.size(), 1u);
            EXPECT_EQ(liveItemHandleCount(), 1u);
        }

        TEST(MWWorldItemHandles, a_different_item_is_a_different_handle)
        {
            Item a(1), b(2);
            ItemHandleScope scope;
            const ItemHandle ha = scope.make(a.ptr());
            const ItemHandle hb = scope.make(b.ptr());
            EXPECT_NE(ha, hb);
            EXPECT_EQ(resolveItem(ha), a.ptr());
            EXPECT_EQ(resolveItem(hb), b.ptr());
            EXPECT_EQ(scope.size(), 2u);
        }

        // The Repair case: a tool that is used up is replaced by another stack of the same kind. The new stack is another
        // LiveCellRef, so it must get a NEW handle, and the old handle must keep naming the old stack.
        TEST(MWWorldItemHandles, a_replaced_stack_gets_a_new_handle_and_the_old_one_keeps_its_item)
        {
            Item oldTool(1), newTool(2);
            ItemHandleScope scope;
            const ItemHandle before = scope.make(oldTool.ptr());
            const ItemHandle after = scope.make(newTool.ptr());
            EXPECT_NE(before, after);
            EXPECT_EQ(resolveItem(before), oldTool.ptr());
            EXPECT_EQ(resolveItem(after), newTool.ptr());
        }

        TEST(MWWorldItemHandles, handles_die_with_their_scope)
        {
            Item item(1);
            ItemHandle handle;
            {
                ItemHandleScope scope;
                handle = scope.make(item.ptr());
                ASSERT_EQ(resolveItem(handle), item.ptr());
                EXPECT_EQ(liveItemHandleCount(), 1u);
            }
            EXPECT_TRUE(resolveItem(handle).isEmpty());
            EXPECT_EQ(liveItemHandleCount(), 0u);
        }

        TEST(MWWorldItemHandles, release_ends_the_handles_and_the_scope_can_be_used_again)
        {
            Item item(1);
            ItemHandleScope scope;
            const ItemHandle first = scope.make(item.ptr());
            scope.release();
            EXPECT_TRUE(resolveItem(first).isEmpty());
            EXPECT_EQ(scope.size(), 0u);
            scope.release();
            const ItemHandle second = scope.make(item.ptr());
            EXPECT_EQ(resolveItem(second), item.ptr());
            EXPECT_TRUE(resolveItem(first).isEmpty());
        }

        TEST(MWWorldItemHandles, a_reused_slot_never_brings_an_old_handle_back_to_life)
        {
            Item a(1), b(2);
            ItemHandle stale;
            {
                ItemHandleScope scope;
                stale = scope.make(a.ptr());
            }
            ItemHandleScope scope;
            const ItemHandle fresh = scope.make(b.ptr());
            EXPECT_NE(stale, fresh);
            EXPECT_TRUE(resolveItem(stale).isEmpty());
            EXPECT_EQ(resolveItem(fresh), b.ptr());
        }

        TEST(MWWorldItemHandles, scopes_are_independent)
        {
            Item item(1);
            ItemHandleScope one, two;
            const ItemHandle h1 = one.make(item.ptr());
            const ItemHandle h2 = two.make(item.ptr());
            EXPECT_NE(h1, h2);
            one.release();
            EXPECT_TRUE(resolveItem(h1).isEmpty());
            EXPECT_EQ(resolveItem(h2), item.ptr());
        }

        // Review 2c, L1/L2: the slot of a freed handle is reused with a new generation; the old handle stays dead
        // through many reuses, and freeing never throws (the free list is reserved when a slot is made).
        TEST(MWWorldItemHandles, one_slot_reused_many_times_never_revives_any_earlier_handle)
        {
            Item a(1), b(2);
            std::vector<ItemHandle> dead;
            for (int round = 0; round < 50; ++round)
            {
                ItemHandleScope scope;
                const ItemHandle handle = scope.make((round % 2 == 0 ? a : b).ptr());
                for (const ItemHandle old : dead)
                {
                    EXPECT_NE(old, handle);
                    EXPECT_TRUE(resolveItem(old).isEmpty());
                }
                EXPECT_EQ(resolveItem(handle), (round % 2 == 0 ? a : b).ptr());
                dead.push_back(handle);
            }
            EXPECT_EQ(liveItemHandleCount(), 0u);
        }

        TEST(MWWorldItemHandles, the_free_list_has_room_for_every_slot_so_ending_a_scope_cannot_allocate)
        {
            std::vector<Item> items;
            items.reserve(40);
            for (std::uint32_t i = 0; i < 40; ++i)
                items.emplace_back(i + 1);
            {
                ItemHandleScope scope;
                for (Item& item : items)
                    (void)scope.make(item.ptr());
                EXPECT_GE(liveItemHandleCount(), 40u);
            }
            EXPECT_EQ(liveItemHandleCount(), 0u);
            EXPECT_TRUE(itemHandleFreeListReserved())
                << "~ItemHandleScope is noexcept: its push_back must not allocate";
        }

        TEST(MWWorldItemHandles, the_table_does_not_grow_without_bound)
        {
            std::vector<Item> items;
            items.reserve(8);
            for (std::uint32_t i = 0; i < 8; ++i)
                items.emplace_back(i + 1);
            for (int round = 0; round < 1000; ++round)
            {
                ItemHandleScope scope;
                for (Item& item : items)
                    (void)scope.make(item.ptr());
            }
            EXPECT_EQ(liveItemHandleCount(), 0u);
            EXPECT_LE(itemHandleSlotCount(), 64u);
        }
    }
}
