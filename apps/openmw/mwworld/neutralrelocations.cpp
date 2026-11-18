#include "neutralrelocations.hpp"

#include <cmath>

namespace MWWorld
{
    bool NeutralRelocations::note(const std::string& gameId, std::uint32_t placement, const NeutralSaveCellKey& origin,
        const NeutralSaveCellKey& destination, const float (&pos)[3], const float (&rot)[3])
    {
        if (gameId.empty() || origin.mKind == NeutralSaveCellKey::Kind::None
            || destination.mKind == NeutralSaveCellKey::Kind::None)
            return false;
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(pos[i]) || !std::isfinite(rot[i]))
                return false;
        const auto key = std::make_pair(gameId, placement);
        const auto found = mEntries.find(key);
        const NeutralSaveCellKey& from = found != mEntries.end() ? found->second.mOrigin : origin;
        if (from == destination)
        {
            // back where the pack's record has it: nothing to remember
            if (found != mEntries.end())
                mEntries.erase(found);
            return true;
        }
        if (found == mEntries.end() && mEntries.size() >= kNeutralSaveMaxRelocations)
            return false;
        NeutralRelocation entry;
        entry.mGameId = gameId;
        entry.mPlacement = placement;
        entry.mOrigin = from;
        entry.mDestination = destination;
        for (int i = 0; i < 3; ++i)
        {
            entry.mPos[i] = pos[i];
            entry.mRot[i] = rot[i];
        }
        mEntries.insert_or_assign(key, std::move(entry));
        return true;
    }

    const NeutralRelocation* NeutralRelocations::find(const std::string& gameId, std::uint32_t placement) const
    {
        const auto found = mEntries.find(std::make_pair(gameId, placement));
        return found == mEntries.end() ? nullptr : &found->second;
    }

    bool NeutralRelocations::erase(const std::string& gameId, std::uint32_t placement)
    {
        return mEntries.erase(std::make_pair(gameId, placement)) != 0;
    }

    void NeutralRelocations::load(std::span<const NeutralRelocation> entries)
    {
        mEntries.clear();
        for (const NeutralRelocation& entry : entries)
        {
            if (mEntries.size() >= kNeutralSaveMaxRelocations)
                break;
            mEntries.emplace(std::make_pair(entry.mGameId, entry.mPlacement), entry);
        }
    }

    std::vector<NeutralRelocation> NeutralRelocations::collect() const
    {
        std::vector<NeutralRelocation> result;
        result.reserve(mEntries.size());
        for (const auto& [key, entry] : mEntries)
            result.push_back(entry);
        return result;
    }
}
