#include "game_skyrimse/follower_state.hpp"

#include <algorithm>
#include <bit>

namespace game_skyrimse
{
    namespace
    {
        constexpr std::int32_t kMinRank = -4; // Archnemesis
        constexpr std::int32_t kMaxRank = 4; // Lover
        constexpr float kWaitingThreshold = 0.5f; // WaitingForPlayer is 0 or 1; above one half is waiting

        template <typename Map, typename Key>
        bool mayAdd(const Map& map, const Key& key)
        {
            return map.count(key) != 0 || map.size() < kMaxFollowerStateEntries;
        }

        bool withinCaps(const FollowerStateData& d)
        {
            if (d.mFactions.size() > kMaxFollowerStateEntries || d.mRanks.size() > kMaxFollowerStateEntries
                || d.mTeammates.size() > kMaxFollowerStateEntries || d.mGlobals.size() > kMaxFollowerStateEntries)
                return false;
            for (const auto& [key, rank] : d.mFactions)
            {
                (void)rank;
                if (key.first == 0 || key.second == 0)
                    return false;
            }
            for (const auto& [key, rank] : d.mRanks)
            {
                (void)rank;
                if (key.first == 0 || key.second == 0)
                    return false;
            }
            if (!d.mTeammates.empty() && *d.mTeammates.begin() == 0)
                return false;
            if (!d.mGlobals.empty() && d.mGlobals.begin()->first == 0)
                return false;
            return true;
        }
    }

    bool FollowerState::addToFaction(FormId reference, FormId faction, std::int32_t rank)
    {
        if (reference == 0 || faction == 0)
            return false;
        const auto key = std::make_pair(reference, faction);
        if (!mayAdd(mData.mFactions, key))
            return false;
        mData.mFactions[key] = std::max<std::int32_t>(rank, 0);
        return true;
    }

    bool FollowerState::removeFromFaction(FormId reference, FormId faction)
    {
        if (reference == 0 || faction == 0)
            return false;
        const auto key = std::make_pair(reference, faction);
        if (!mayAdd(mData.mFactions, key))
            return false;
        mData.mFactions[key] = -1;
        return true;
    }

    bool FollowerState::setRelationshipRank(FormId reference, FormId target, std::int32_t rank)
    {
        if (reference == 0 || target == 0)
            return false;
        const auto key = std::make_pair(reference, target);
        if (!mayAdd(mData.mRanks, key))
            return false;
        mData.mRanks[key] = std::clamp(rank, kMinRank, kMaxRank);
        return true;
    }

    bool FollowerState::setTeammate(FormId reference, bool teammate)
    {
        if (reference == 0)
            return false;
        if (!teammate)
        {
            mData.mTeammates.erase(reference);
            return true;
        }
        if (mData.mTeammates.count(reference) == 0 && mData.mTeammates.size() >= kMaxFollowerStateEntries)
            return false;
        mData.mTeammates.insert(reference);
        return true;
    }

    bool FollowerState::setGlobal(FormId global, float value)
    {
        if (global == 0 || !mayAdd(mData.mGlobals, global))
            return false;
        mData.mGlobals[global] = value;
        return true;
    }

    bool FollowerState::setFactionRelation(FormId a, FormId b, std::uint8_t relation)
    {
        if (a == 0 || b == 0)
            return false;
        const std::pair<FormId, FormId> key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
        if (mRelations.count(key) == 0 && mRelations.size() >= kMaxFollowerStateEntries)
            return false;
        mRelations[key] = relation;
        return true;
    }

    std::optional<std::uint8_t> FollowerState::factionRelation(FormId a, FormId b) const
    {
        const auto found = mRelations.find(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
        if (found == mRelations.end())
            return std::nullopt;
        return found->second;
    }

    bool FollowerState::waiting(FormId reference) const
    {
        const std::optional<float> value = actorValue(reference, kWaitingForPlayerIndex);
        return value && *value > kWaitingThreshold;
    }

    std::optional<float> FollowerState::factionRank(FormId reference, FormId faction) const
    {
        const auto it = mData.mFactions.find({ reference, faction });
        if (it == mData.mFactions.end())
            return std::nullopt;
        return static_cast<float>(it->second);
    }

    std::optional<float> FollowerState::actorValue(FormId reference, std::uint32_t index) const
    {
        if (!mActorValues)
            return std::nullopt;
        if (index == kWaitingForPlayerIndex)
            return mActorValues(reference, kWaitingForPlayerKey);
        if (index == kVariable01Index) // task 0302: the ambush draugr's sleep package tests it
            return mActorValues(reference, kVariable01Key);
        return std::nullopt;
    }

    std::optional<float> FollowerState::globalValue(FormId global) const
    {
        const auto it = mData.mGlobals.find(global);
        if (it == mData.mGlobals.end())
            return std::nullopt;
        return it->second;
    }

    std::optional<float> FollowerState::relationshipRank(FormId reference, FormId target) const
    {
        const auto it = mData.mRanks.find({ reference, target });
        if (it == mData.mRanks.end())
            return std::nullopt;
        return static_cast<float>(it->second);
    }

    bool FollowerState::isPlayerTeammate(FormId reference) const
    {
        return mData.mTeammates.count(reference) != 0;
    }

    bool FollowerState::restore(const FollowerStateData& data)
    {
        if (!withinCaps(data))
            return false;
        mData = data;
        for (auto& [key, rank] : mData.mFactions)
            rank = std::clamp<std::int32_t>(rank, -1, 255);
        for (auto& [key, rank] : mData.mRanks)
            rank = std::clamp(rank, kMinRank, kMaxRank);
        return true;
    }

    void FollowerStateChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        const FollowerStateData data = mGet ? mGet() : FollowerStateData();
        const auto count = [](std::size_t n) {
            return static_cast<std::uint32_t>(std::min<std::size_t>(n, kMaxFollowerStateEntries));
        };
        const auto writePairs = [&](const std::map<std::pair<FormId, FormId>, std::int32_t>& map) {
            writer.writeValue(count(map.size()));
            std::size_t written = 0;
            for (const auto& [key, rank] : map)
            {
                if (written++ >= kMaxFollowerStateEntries)
                    break;
                writer.writeValue(static_cast<std::uint32_t>(key.first));
                writer.writeValue(static_cast<std::uint32_t>(key.second));
                writer.writeValue(static_cast<std::int32_t>(rank));
            }
        };
        writePairs(data.mFactions);
        writePairs(data.mRanks);
        writer.writeValue(count(data.mTeammates.size()));
        std::size_t written = 0;
        for (const FormId id : data.mTeammates)
        {
            if (written++ >= kMaxFollowerStateEntries)
                break;
            writer.writeValue(static_cast<std::uint32_t>(id));
        }
        writer.writeValue(count(data.mGlobals.size()));
        written = 0;
        for (const auto& [id, value] : data.mGlobals)
        {
            if (written++ >= kMaxFollowerStateEntries)
                break;
            writer.writeValue(static_cast<std::uint32_t>(id));
            writer.writeValue(std::bit_cast<std::uint32_t>(value));
        }
    }

    bool FollowerStateChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        if (version != kVersion)
        {
            mRefusal = "followerstate chunk: version " + std::to_string(version) + " is not understood";
            return false;
        }
        FollowerStateData data;
        const auto refuse = [&](const char* why) {
            mRefusal = std::string("followerstate chunk: ") + why;
            return false;
        };
        const auto readPairs = [&](std::map<std::pair<FormId, FormId>, std::int32_t>& map) {
            std::uint32_t n = 0;
            if (!reader.readValue(n) || n > kMaxFollowerStateEntries
                || reader.remaining() < static_cast<std::size_t>(n) * 12u)
                return false;
            std::pair<FormId, FormId> previous{ 0, 0 };
            for (std::uint32_t i = 0; i < n; ++i)
            {
                std::uint32_t a = 0, b = 0;
                std::int32_t rank = 0;
                if (!reader.readValue(a) || !reader.readValue(b) || !reader.readValue(rank) || a == 0 || b == 0)
                    return false;
                const std::pair<FormId, FormId> key{ a, b };
                if (i > 0 && !(previous < key))
                    return false; // strictly ascending: no duplicate, no other order
                previous = key;
                map[key] = rank;
            }
            return true;
        };
        if (!readPairs(data.mFactions))
            return refuse("bad faction list");
        if (!readPairs(data.mRanks))
            return refuse("bad relationship list");
        std::uint32_t n = 0;
        if (!reader.readValue(n) || n > kMaxFollowerStateEntries
            || reader.remaining() < static_cast<std::size_t>(n) * 4u)
            return refuse("bad teammate list");
        FormId previous = 0;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            std::uint32_t id = 0;
            if (!reader.readValue(id) || id == 0 || (i > 0 && id <= previous))
                return refuse("teammates are not strictly ascending and non-zero");
            previous = id;
            data.mTeammates.insert(id);
        }
        if (!reader.readValue(n) || n > kMaxFollowerStateEntries
            || reader.remaining() < static_cast<std::size_t>(n) * 8u)
            return refuse("bad global list");
        previous = 0;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            std::uint32_t id = 0, bits = 0;
            if (!reader.readValue(id) || !reader.readValue(bits) || id == 0 || (i > 0 && id <= previous))
                return refuse("globals are not strictly ascending and non-zero");
            previous = id;
            data.mGlobals[id] = std::bit_cast<float>(bits);
        }
        if (reader.remaining() != 0)
            return refuse("trailing bytes");
        if (mSet && !mSet(data))
            return refuse("the pack's follower rules are not running");
        return true;
    }
}
