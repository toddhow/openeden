#include "neutralstore.hpp"

#include <algorithm>
#include <stdexcept>

namespace MWWorld
{
    namespace
    {
        using openeden::sdk::game::WorldObject;

        bool gameIdUsable(std::string_view gameId)
        {
            if (gameId.empty())
                return false;
            return std::none_of(gameId.begin(), gameId.end(),
                [](char c) { return c == ':' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f; });
        }

        std::string describe(std::string_view gameId, const NeutralBase& base)
        {
            return std::string(gameId) + " base " + std::to_string(base.mBase) + " (placement "
                + std::to_string(base.mPlacement) + ")";
        }
    }

    ESM::RefId neutralBaseId(std::string_view gameId, const WorldObject& object, std::string& error)
    {
        if (!gameIdUsable(gameId))
        {
            error = "neutral base: the game id \"" + std::string(gameId)
                + "\" is empty or holds a ':' or a control character";
            return ESM::RefId();
        }
        std::string key(gameId);
        if (object.base != openeden::sdk::game::kNoWorldId)
        {
            key += ":b";
            key += std::to_string(object.base);
        }
        else if (object.id != openeden::sdk::game::kNoWorldId)
        {
            key += ":o";
            key += std::to_string(object.id);
        }
        else
        {
            error
                = "neutral base: the object of game " + std::string(gameId) + " has neither a base nor a placement id";
            return ESM::RefId();
        }
        return ESM::RefId::stringRefId(key);
    }

    Store<NeutralBase>::AddResult Store<NeutralBase>::addFromObject(std::string_view gameId, const WorldObject& object)
    {
        AddResult result;
        const ESM::RefId id = neutralBaseId(gameId, object, result.mError);
        if (id.empty())
            return result;

        NeutralBase facts;
        facts.mId = id;
        facts.mGameId = std::string(gameId);
        facts.mBase = object.base;
        facts.mPlacement = object.base == openeden::sdk::game::kNoWorldId ? object.id : openeden::sdk::game::kNoWorldId;
        facts.mKind = object.kind;
        facts.mEditorId = object.baseEditorId;
        facts.mModel = object.modelPath;
        facts.mCollision = object.collision;
        facts.mPayload = object.payload;

        const auto found = mRecords.find(id);
        if (found != mRecords.end())
        {
            result.mBase = &found->second;
            if (found->second == facts)
            {
                result.mStatus = Status::Existing;
                return result;
            }
            ++mConflicts;
            result.mStatus = Status::Conflict;
            result.mError = "neutral base: " + describe(gameId, found->second)
                + " arrived with other facts than the record already held; the first one is kept";
            return result;
        }

        if (mOrder.size() >= mMaxBases)
        {
            result.mError = "neutral base: the store holds its limit of " + std::to_string(mMaxBases) + " records; "
                + describe(gameId, facts) + " was not stored";
            return result;
        }

        const auto inserted = mRecords.emplace(id, std::move(facts));
        result.mBase = &inserted.first->second;
        mOrder.push_back(result.mBase);
        result.mStatus = Status::Added;
        return result;
    }

    const NeutralBase* Store<NeutralBase>::search(const ESM::RefId& id) const
    {
        const auto it = mRecords.find(id);
        return it == mRecords.end() ? nullptr : &it->second;
    }

    const NeutralBase* Store<NeutralBase>::find(const ESM::RefId& id) const
    {
        const NeutralBase* base = search(id);
        if (base == nullptr)
            throw std::runtime_error("Neutral base " + id.toDebugString() + " not found");
        return base;
    }

    void Store<NeutralBase>::removeGame(std::string_view gameId)
    {
        std::erase_if(mOrder, [&](const NeutralBase* base) { return base->mGameId == gameId; });
        std::erase_if(mRecords, [&](const auto& entry) { return entry.second.mGameId == gameId; });
    }

    void Store<NeutralBase>::clear()
    {
        mOrder.clear();
        mRecords.clear();
    }
}
