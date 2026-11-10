#include "store.hpp"

#include <exception>

namespace NeutralWorld
{
    using namespace openeden::sdk::game;

    bool NeutralWorldStore::addSource(WorldCellSource& source, std::string& error)
    {
        const std::string game(source.gameId());
        if (game.empty())
        {
            error = "a source with an empty game id";
            return false;
        }
        if (mSources.contains(game))
        {
            error = "a source for '" + game + "' is already registered";
            return false;
        }
        if (mSources.size() >= mLimits.mMaxSources)
        {
            error = "too many sources";
            return false;
        }
        mSources.emplace(game, &source);
        return true;
    }

    void NeutralWorldStore::removeSource(std::string_view gameId)
    {
        const auto found = mSources.find(gameId);
        if (found == mSources.end())
            return;
        mSources.erase(found);
        for (auto it = mCells.begin(); it != mCells.end();)
        {
            if (it->first.first == gameId)
                it = mCells.erase(it);
            else
                ++it;
        }
        updateResidentStats();
    }

    void NeutralWorldStore::updateResidentStats()
    {
        mStats.mResidentCells = mCells.size();
        std::size_t objects = 0;
        for (const auto& [key, entry] : mCells)
            objects += entry.mCell->objects.size();
        mStats.mResidentObjects = objects;
    }

    void NeutralWorldStore::makeRoom(const Key& keep, std::size_t incomingObjects)
    {
        for (;;)
        {
            std::size_t objects = incomingObjects;
            for (const auto& [key, entry] : mCells)
                objects += entry.mCell->objects.size();
            if (mCells.size() + 1 <= mLimits.mMaxResidentCells && objects <= mLimits.mMaxResidentObjects)
                return;
            // The least recently requested cell other than the one being kept. The clock never repeats, so there are no
            // ties.
            auto oldest = mCells.end();
            for (auto it = mCells.begin(); it != mCells.end(); ++it)
            {
                if (it->first == keep)
                    continue;
                if (oldest == mCells.end() || it->second.mLastUse < oldest->second.mLastUse)
                    oldest = it;
            }
            if (oldest == mCells.end())
                return;
            mCells.erase(oldest);
            ++mStats.mEvictions;
        }
    }

    const WorldCell* NeutralWorldStore::cell(std::string_view gameId, WorldId id, std::string& error)
    {
        const auto source = mSources.find(gameId);
        if (source == mSources.end())
        {
            error = "no source for '" + std::string(gameId) + "'";
            return nullptr;
        }
        Key key(std::string(gameId), id);
        const auto resident = mCells.find(key);
        if (resident != mCells.end())
        {
            resident->second.mLastUse = ++mClock;
            ++mStats.mHits;
            return resident->second.mCell.get();
        }

        auto built = std::make_unique<WorldCell>();
        bool ok = false;
        std::string why;
        try
        {
            ok = source->second->buildCell(id, *built, why);
        }
        catch (const std::exception& e)
        {
            ok = false;
            why = std::string("the source threw: ") + e.what();
        }
        catch (...)
        {
            ok = false;
            why = "the source threw";
        }
        if (!ok)
        {
            ++mStats.mSourceFailures;
            error = std::string(gameId) + ": " + why;
            return nullptr;
        }
        if (!validateCell(*built, mLimits.mCell, why))
        {
            ++mStats.mRefusedByValidation;
            error = std::string(gameId) + ": snapshot refused: " + why;
            return nullptr;
        }
        if (built->id != id)
        {
            ++mStats.mRefusedByValidation;
            error = std::string(gameId) + ": snapshot refused: the source answered cell " + std::to_string(built->id)
                + " for a request for " + std::to_string(id);
            return nullptr;
        }
        if (built->objects.size() > mLimits.mMaxResidentObjects)
        {
            ++mStats.mRefusedTooBig;
            error = std::string(gameId) + ": cell " + std::to_string(id) + " has more objects than the store can hold";
            return nullptr;
        }

        makeRoom(key, built->objects.size());
        Entry entry;
        entry.mCell = std::move(built);
        entry.mLastUse = ++mClock;
        const WorldCell* result = entry.mCell.get();
        mCells.emplace(std::move(key), std::move(entry));
        ++mStats.mBuilds;
        updateResidentStats();
        return result;
    }

    bool NeutralWorldStore::isResident(std::string_view gameId, WorldId id) const
    {
        return mCells.contains(Key(std::string(gameId), id));
    }

    void NeutralWorldStore::clear()
    {
        mCells.clear();
        updateResidentStats();
    }
}
