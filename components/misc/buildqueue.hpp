#ifndef OPENEDEN_MISC_BUILDQUEUE_H
#define OPENEDEN_MISC_BUILDQUEUE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Misc
{
    /*
     * Task 0060 (LOD S6), stage g: the work queue behind [Cells] incremental cell build.
     *
     * When the player crosses a cell border the game loads a row of new cells. Building their 400 to 550 objects in
     * one frame costs 25 to 100 ms. With incremental building the cells are loaded as before, but their objects are
     * put in this queue and built a few milliseconds at a time over the next frames.
     *
     * The queue knows nothing about the game: an item is a group (a cell), a key (an object), a rank, a phase and a
     * distance, plus a payload the caller needs to build it (the game uses a Ptr). Items come out in this order:
     * rank (nearer cells first), phase (the plain objects of a cell, then its actors, then the navigator pass, so an
     * actor never stands where the floor has not been built yet), distance (nearer objects first), then the order in
     * which they were added. The order never depends on a clock or an address, so it is the same on every run; only
     * how many items fit in one frame depends on the time they take.
     *
     * Main thread only. Not thread safe.
     */
    enum class BuildPhase : int
    {
        Object = 0,
        Actor = 1,
        Navigator = 2
    };

    template <class Payload>
    class BuildQueue
    {
    public:
        struct Item
        {
            const void* mGroup = nullptr; // the cell
            const void* mKey = nullptr; // the object; it may be queued once per phase
            int mGroupRank = 0; // lower is built first
            BuildPhase mPhase = BuildPhase::Object;
            float mDistance = 0.f; // lower is built first
            Payload mPayload{};
        };

        void add(Item item)
        {
            // A distance that is not a number (a corrupt position) would break the sort: put it last.
            if (!(item.mDistance == item.mDistance))
                item.mDistance = std::numeric_limits<float>::max();
            ++mGroupCounts[item.mGroup];
            ++mKeyCounts[item.mKey];
            mEntries.push_back(Entry{ std::move(item), mNextSequence++ });
            mSorted = false;
        }

        bool empty() const { return mNext >= mEntries.size(); }

        std::size_t size() const { return mEntries.size() - mNext; }

        bool contains(const void* key) const { return mKeyCounts.find(key) != mKeyCounts.end(); }

        std::size_t pendingInGroup(const void* group) const
        {
            const auto found = mGroupCounts.find(group);
            return found == mGroupCounts.end() ? 0 : found->second;
        }

        std::size_t cancelKey(const void* key)
        {
            return cancelIf([key](const Item& item) { return item.mKey == key; });
        }

        std::size_t cancelGroup(const void* group)
        {
            return cancelIf([group](const Item& item) { return item.mGroup == group; });
        }

        std::size_t replaceKey(const void* oldKey, const void* newKey, const Payload& newPayload)
        {
            std::size_t changed = 0;
            for (std::size_t i = mNext; i < mEntries.size(); ++i)
            {
                Item& item = mEntries[i].mItem;
                if (item.mKey == oldKey)
                {
                    releaseKey(oldKey);
                    item.mKey = newKey;
                    ++mKeyCounts[newKey];
                    item.mPayload = newPayload;
                    ++changed;
                }
            }
            return changed;
        }

        // Like replaceKey, and the items also move to newGroup (the object moved to a cell that is not the one it
        // was queued for). Returns how many items changed.
        std::size_t replaceKeyAndGroup(
            const void* oldKey, const void* newKey, const void* newGroup, const Payload& newPayload)
        {
            std::size_t changed = 0;
            for (std::size_t i = mNext; i < mEntries.size(); ++i)
            {
                Item& item = mEntries[i].mItem;
                if (item.mKey == oldKey)
                {
                    releaseCounts(item);
                    item.mKey = newKey;
                    item.mGroup = newGroup;
                    item.mPayload = newPayload;
                    ++mGroupCounts[newGroup];
                    ++mKeyCounts[newKey];
                    ++changed;
                }
            }
            return changed;
        }

        void clear()
        {
            mEntries.clear();
            mGroupCounts.clear();
            mKeyCounts.clear();
            mNext = 0;
            mSorted = true;
        }

        // Builds queued items in order until the budget is used. At least max(minItems, 1) items are built when
        // that many are queued, so the queue always drains even when the frames are slow.
        template <class Clock, class Build>
        std::size_t run(double budgetMs, std::size_t minItems, Clock&& nowMs, Build&& build)
        {
            if (empty())
                return 0;
            const double start = nowMs();
            std::size_t built = 0;
            while (!empty())
            {
                if (built >= std::max<std::size_t>(minItems, 1) && nowMs() - start >= budgetMs)
                    break;
                build(pop());
                ++built;
            }
            return built;
        }

        // Builds every queued item of the group now, whatever the budget.
        template <class Build>
        std::size_t runGroup(const void* group, Build&& build)
        {
            std::size_t built = 0;
            try
            {
                while (pendingInGroup(group) > 0)
                {
                    sortIfNeeded();
                    // Bring the group's items to the front once (the rest keeps its order), then take them from there.
                    if (mEntries[mNext].mItem.mGroup != group)
                    {
                        std::stable_partition(mEntries.begin() + static_cast<std::ptrdiff_t>(mNext), mEntries.end(),
                            [group](const Entry& entry) { return entry.mItem.mGroup == group; });
                    }
                    Item item = popFront();
                    build(item);
                    ++built;
                }
            }
            catch (...)
            {
                mSorted = false; // the partition left the queue out of order
                throw;
            }
            return built;
        }

    private:
        struct Entry
        {
            Item mItem;
            std::uint64_t mSequence;
        };

        static bool before(const Entry& lhs, const Entry& rhs)
        {
            if (lhs.mItem.mGroupRank != rhs.mItem.mGroupRank)
                return lhs.mItem.mGroupRank < rhs.mItem.mGroupRank;
            if (lhs.mItem.mPhase != rhs.mItem.mPhase)
                return static_cast<int>(lhs.mItem.mPhase) < static_cast<int>(rhs.mItem.mPhase);
            if (lhs.mItem.mDistance != rhs.mItem.mDistance)
                return lhs.mItem.mDistance < rhs.mItem.mDistance;
            return lhs.mSequence < rhs.mSequence;
        }

        void sortIfNeeded()
        {
            if (mSorted)
                return;
            // Drop what was already built, then order the rest.
            mEntries.erase(mEntries.begin(), mEntries.begin() + static_cast<std::ptrdiff_t>(mNext));
            mNext = 0;
            std::sort(mEntries.begin(), mEntries.end(), before);
            mSorted = true;
        }

        void releaseKey(const void* key)
        {
            const auto found = mKeyCounts.find(key);
            if (found != mKeyCounts.end() && --found->second == 0)
                mKeyCounts.erase(found);
        }

        void releaseCounts(const Item& item)
        {
            const auto found = mGroupCounts.find(item.mGroup);
            if (found != mGroupCounts.end() && --found->second == 0)
                mGroupCounts.erase(found);
            releaseKey(item.mKey);
        }

        Item pop()
        {
            sortIfNeeded();
            return popFront();
        }

        // Takes the first queued item without looking at the order.
        Item popFront()
        {
            Item item = std::move(mEntries[mNext].mItem);
            ++mNext;
            releaseCounts(item);
            if (mNext >= mEntries.size())
            {
                mEntries.clear();
                mNext = 0;
            }
            return item;
        }

        template <class Predicate>
        std::size_t cancelIf(Predicate&& predicate)
        {
            std::size_t removed = 0;
            std::size_t kept = mNext;
            for (std::size_t i = mNext; i < mEntries.size(); ++i)
            {
                if (predicate(mEntries[i].mItem))
                {
                    releaseCounts(mEntries[i].mItem);
                    ++removed;
                }
                else
                {
                    if (kept != i)
                        mEntries[kept] = std::move(mEntries[i]);
                    ++kept;
                }
            }
            mEntries.erase(mEntries.begin() + static_cast<std::ptrdiff_t>(kept), mEntries.end());
            if (mNext >= mEntries.size())
            {
                mEntries.clear();
                mNext = 0;
            }
            return removed;
        }

        std::vector<Entry> mEntries; // [mNext, end) is what is still queued
        std::size_t mNext = 0;
        bool mSorted = true;
        std::uint64_t mNextSequence = 0;
        std::map<const void*, std::size_t> mGroupCounts;
        std::unordered_map<const void*, std::size_t> mKeyCounts; // only looked up, never iterated
    };
}

#endif
