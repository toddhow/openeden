#include "game_skyrimse/follower.hpp"

#include <algorithm>

namespace game_skyrimse
{
    void FollowerChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        std::vector<FollowerEntry> entries = mGet ? mGet() : std::vector<FollowerEntry>();
        std::sort(entries.begin(), entries.end(),
            [](const FollowerEntry& a, const FollowerEntry& b) { return a.mId < b.mId; });
        entries.erase(std::unique(entries.begin(), entries.end(),
                          [](const FollowerEntry& a, const FollowerEntry& b) { return a.mId == b.mId; }),
            entries.end());
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                          [](const FollowerEntry& e) { return e.mId == openeden::sdk::game::kNoWorldId; }),
            entries.end());
        if (entries.size() > kMaxFollowers)
            entries.resize(kMaxFollowers);
        writer.writeValue(static_cast<std::uint32_t>(entries.size()));
        for (const FollowerEntry& e : entries)
        {
            writer.writeValue(static_cast<std::uint32_t>(e.mId));
            writer.writeValue(static_cast<std::uint32_t>(e.mHome));
        }
    }

    bool FollowerChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        if (version != kVersion)
        {
            mRefusal = "followers chunk: version " + std::to_string(version) + " is not understood";
            return false;
        }
        std::uint32_t count = 0;
        if (!reader.readValue(count) || count > kMaxFollowers
            || reader.remaining() < static_cast<std::size_t>(count) * 8u)
        {
            mRefusal = "followers chunk: bad count";
            return false;
        }
        std::vector<FollowerEntry> ids;
        ids.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t id = 0;
            std::uint32_t home = 0;
            if (!reader.readValue(id) || !reader.readValue(home) || id == 0 || (!ids.empty() && id <= ids.back().mId))
            {
                mRefusal = "followers chunk: ids are not strictly ascending and non-zero";
                return false;
            }
            ids.push_back({ id, home });
        }
        if (reader.remaining() != 0)
        {
            mRefusal = "followers chunk: trailing bytes";
            return false;
        }
        if (mSet && !mSet(ids))
        {
            mRefusal = "followers chunk: the pack's actor rules are not running";
            return false;
        }
        return true;
    }

    FollowerAliasChange followerAliasChange(std::uint32_t previous, std::uint32_t current)
    {
        FollowerAliasChange change;
        if (previous == current)
            return change;
        change.mDismiss = previous;
        change.mRecruit = current;
        return change;
    }
}
