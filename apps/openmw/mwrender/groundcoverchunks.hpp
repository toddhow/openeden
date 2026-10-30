#ifndef OPENEDEN_MWRENDER_GROUNDCOVERCHUNKS_H
#define OPENEDEN_MWRENDER_GROUNDCOVERCHUNKS_H

#include <algorithm>
#include <cstddef>
#include <list>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace MWRender
{
    /**
     * @brief Tracks what each groundcover cell's chunk is doing (task 0012, V4g): unknown, pending (being built on a
     * worker), shown, or cached (built, out of range). Renderer-free so it can be unit-tested; MWRender::Groundcover
     * keeps one, with Chunk a scene graph node.
     *
     * Not thread-safe: Groundcover calls it on the main thread only, collecting finished builds there.
     */
    template <class Chunk>
    class GroundcoverChunkTracker
    {
    public:
        using Cell = std::pair<int, int>;

        explicit GroundcoverChunkTracker(std::size_t cacheCapacity)
            : mCacheCapacity(cacheCapacity)
        {
        }

        /// Sets the cells in range (sorted, no duplicates) and returns those that need building, now pending.
        std::vector<Cell> setWanted(const std::vector<Cell>& wanted)
        {
            mWanted = wanted;
            for (auto it = mShown.begin(); it != mShown.end();)
            {
                if (isWanted(it->first))
                {
                    ++it;
                    continue;
                }
                addToCache(it->first, std::move(it->second));
                it = mShown.erase(it);
            }

            std::vector<Cell> toBuild;
            for (const Cell& cell : wanted)
            {
                if (mShown.contains(cell) || mPending.contains(cell))
                    continue;
                if (auto cached = mCached.find(cell); cached != mCached.end())
                {
                    mShown.emplace(cell, std::move(cached->second));
                    mCached.erase(cached);
                    mCacheOrder.remove(cell);
                    continue;
                }
                mPending.insert(cell);
                toBuild.push_back(cell);
            }
            trimCache();
            return toBuild;
        }

        /// Takes a finished build; a cell that is not pending (its build outlived a clear()) is ignored.
        void finish(const Cell& cell, Chunk chunk)
        {
            if (mPending.erase(cell) == 0)
                return;
            if (isWanted(cell))
                mShown.emplace(cell, std::move(chunk));
            else
            {
                addToCache(cell, std::move(chunk));
                trimCache();
            }
        }

        const std::map<Cell, Chunk>& shown() const { return mShown; }
        bool isPending(const Cell& cell) const { return mPending.contains(cell); }
        std::size_t pendingCount() const { return mPending.size(); }
        std::size_t cachedCount() const { return mCached.size(); }

        void clear()
        {
            mWanted.clear();
            mPending.clear();
            mShown.clear();
            mCached.clear();
            mCacheOrder.clear();
        }

    private:
        bool isWanted(const Cell& cell) const { return std::binary_search(mWanted.begin(), mWanted.end(), cell); }

        void addToCache(const Cell& cell, Chunk chunk)
        {
            mCached.emplace(cell, std::move(chunk));
            mCacheOrder.push_back(cell);
        }

        void trimCache()
        {
            while (mCached.size() > mCacheCapacity)
            {
                mCached.erase(mCacheOrder.front());
                mCacheOrder.pop_front();
            }
        }

        std::size_t mCacheCapacity;
        std::vector<Cell> mWanted;
        std::set<Cell> mPending;
        std::map<Cell, Chunk> mShown;
        std::map<Cell, Chunk> mCached;
        std::list<Cell> mCacheOrder; // oldest first
    };
}

#endif
