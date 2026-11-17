#ifndef OPENMW_MWPHYSICS_NEUTRALREMOVEDREFS_H
#define OPENMW_MWPHYSICS_NEUTRALREMOVEDREFS_H

// Task 0273b: the neutral actors whose cell unloaded. The physics system notes the key (the reference pointer, opaque
// here) of every neutral actor it removes; the world takes the list once a frame and forgets everything it kept for
// those actors: the scene speech queue, the speaker's `Ptr`, its mouth driver and its corpse rest record. Those are
// keyed by the raw pointer of a `LiveCellRef` that is freed with its cell, so a key that outlives its object would be
// used after free, or would be reused by an unrelated new object at the same address.
//
// Main thread only. Bounded: beyond `kMax` keys the oldest are dropped and counted (a cell holds far fewer actors).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace MWPhysics
{
    class NeutralRemovedRefs
    {
    public:
        static constexpr std::size_t kMax = 4096;

        void note(const void* key)
        {
            if (key == nullptr || std::find(mKeys.begin(), mKeys.end(), key) != mKeys.end())
                return;
            if (mKeys.size() >= kMax)
            {
                mKeys.erase(mKeys.begin());
                ++mDropped;
            }
            mKeys.push_back(key);
        }
        /// The noted keys in order, and forgets them.
        [[nodiscard]] std::vector<const void*> take() { return std::exchange(mKeys, {}); }
        [[nodiscard]] std::size_t size() const { return mKeys.size(); }
        [[nodiscard]] std::uint64_t dropped() const { return mDropped; }

    private:
        std::vector<const void*> mKeys;
        std::uint64_t mDropped = 0;
    };
}

#endif
