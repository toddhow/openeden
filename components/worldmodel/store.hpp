#ifndef OPENEDEN_COMPONENTS_WORLDMODEL_STORE_H
#define OPENEDEN_COMPONENTS_WORLDMODEL_STORE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_source.hpp>

#include "validate.hpp"

// The core's neutral world store (decision 0016, task 0100 stage N1). It holds WorldCell snapshots that game packs hand
// over through the SDK's WorldCellSource, builds them LAZILY (a cell is requested by id, built by its source,
// validated, kept), keeps at most a capped number of cells and objects resident (the least recently requested cell is
// dropped first, by a counter, so the order is deterministic), and refuses a hostile snapshot instead of keeping it.
//
// Nothing in the engine reads this yet (stage N3 will); it carries no engine, renderer, Jolt, Qt or game-record type.
//
// Who owns what: the store owns the snapshots; it does NOT own the sources (a source is registered by reference and
// must outlive its registration; removeSource drops its snapshots). Threading: main thread only
// (WorldCellSource::buildCell is main thread only). Allocation: loading allocates; reading a resident cell does not.

namespace NeutralWorld
{
    struct StoreLimits
    {
        std::size_t mMaxResidentCells = 64;
        std::size_t mMaxResidentObjects = 4u * 1024u * 1024u;
        std::size_t mMaxSources = 8;
        Limits mCell;
    };

    struct StoreStats
    {
        std::size_t mBuilds = 0; // cells built by a source and kept
        std::size_t mHits = 0; // requests answered from a resident cell
        std::size_t mEvictions = 0;
        std::size_t mRefusedByValidation = 0; // snapshots refused as hostile
        std::size_t mSourceFailures = 0; // buildCell said false or threw
        std::size_t mRefusedTooBig = 0; // a cell larger than the object cap
        std::size_t mResidentCells = 0;
        std::size_t mResidentObjects = 0;
    };

    class NeutralWorldStore
    {
    public:
        explicit NeutralWorldStore(StoreLimits limits = {})
            : mLimits(limits)
        {
        }

        /// Registers a source under its game id. False (with `error`) for a duplicate game id or past the source cap.
        [[nodiscard]] bool addSource(openeden::sdk::game::WorldCellSource& source, std::string& error);

        /// Forgets the source and drops its resident cells.
        void removeSource(std::string_view gameId);

        [[nodiscard]] std::size_t sourceCount() const { return mSources.size(); }

        /// The cell, built on the first request. nullptr with `error` when the game is unknown, the source fails, the
        /// snapshot is refused or does not fit the caps. The pointer is valid until the next call of this function,
        /// removeSource or clear (a later request may drop the cell to make room); copy what must outlive that.
        [[nodiscard]] const openeden::sdk::game::WorldCell* cell(
            std::string_view gameId, openeden::sdk::game::WorldId id, std::string& error);

        [[nodiscard]] bool isResident(std::string_view gameId, openeden::sdk::game::WorldId id) const;

        void clear();

        [[nodiscard]] const StoreStats& stats() const { return mStats; }

    private:
        struct Entry
        {
            std::unique_ptr<openeden::sdk::game::WorldCell> mCell;
            std::uint64_t mLastUse = 0;
        };
        using Key = std::pair<std::string, openeden::sdk::game::WorldId>;

        void makeRoom(const Key& keep, std::size_t incomingObjects);
        void updateResidentStats();

        StoreLimits mLimits;
        StoreStats mStats;
        std::uint64_t mClock = 0;
        std::map<std::string, openeden::sdk::game::WorldCellSource*, std::less<>> mSources;
        std::map<Key, Entry> mCells;
    };
}

#endif
