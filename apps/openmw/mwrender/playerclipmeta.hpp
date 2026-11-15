#ifndef OPENMW_MWRENDER_PLAYERCLIPMETA_HPP
#define OPENMW_MWRENDER_PLAYERCLIPMETA_HPP

// Task 0171 (review fixes): the meta (events and length) of the clips the player's hidden swing clock needs, read once
// each on a worker. The lookup allocates nothing (names are compared as views, the answer is a shared_ptr copy), a
// meta that is known is returned without asking its future again, and a read that failed is tried once more after
// kRetrySeconds instead of being remembered as a failure for ever; the failure is reported once per clip name.
//
// Threads: main thread only (Scene::neutralClipMeta); the futures are made by workers. Not thread safe.

#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <components/havokpack/clipevents.hpp>

namespace MWRender
{
    class PlayerClipMetaCache
    {
    public:
        using Future = std::shared_future<std::shared_ptr<const HavokPack::ClipMeta>>;
        /// Seconds before a failed read is tried again, and how many reads a clip gets in all.
        static constexpr double kRetrySeconds = 2.0;
        static constexpr int kMaxAttempts = 2;
        /// Clips the cache remembers (the player has two attack clips: this is generous).
        static constexpr std::size_t kMaxEntries = 16;

        struct Answer
        {
            std::shared_ptr<const HavokPack::ClipMeta> mMeta; ///< null while unknown
            bool mStart = false; ///< the caller must start a read now and hand its future to begin()
            bool mWarn = false; ///< a read failed for the first time for this clip: say so once
        };

        PlayerClipMetaCache() { mEntries.reserve(kMaxEntries); }

        /// `now` is any steady clock in seconds. Allocation free.
        [[nodiscard]] Answer lookup(std::string_view clip, bool perSex, double now);
        /// Hands over the future of the read lookup() asked for (allocates the first time a clip is seen).
        void begin(std::string_view clip, bool perSex, Future future);
        [[nodiscard]] std::size_t size() const { return mEntries.size(); }

    private:
        struct Entry
        {
            std::string mClip;
            bool mPerSex = false;
            Future mFuture;
            std::shared_ptr<const HavokPack::ClipMeta> mMeta;
            int mAttempts = 0; ///< reads that came back empty
            bool mWarned = false;
            bool mGaveUp = false;
            double mRetryAt = 0.0;
        };

        Entry* find(std::string_view clip, bool perSex)
        {
            for (Entry& e : mEntries)
                if (e.mPerSex == perSex && e.mClip == clip)
                    return &e;
            return nullptr;
        }

        std::vector<Entry> mEntries;
    };

    // Stub: the behaviour is written after the tests have been shown red.
    inline PlayerClipMetaCache::Answer PlayerClipMetaCache::lookup(std::string_view, bool, double)
    {
        return {};
    }
    inline void PlayerClipMetaCache::begin(std::string_view, bool, Future) {}
}

#endif
