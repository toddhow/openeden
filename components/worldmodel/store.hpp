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
#include <vector>

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

        /// The registered source of a game, or nullptr (task 0121: the engine asks it for optional interfaces, such as
        /// openeden::sdk::game::ActorAppearanceSource, by dynamic_cast). Not owned.
        [[nodiscard]] openeden::sdk::game::WorldCellSource* source(std::string_view gameId) const
        {
            const auto it = mSources.find(gameId);
            return it == mSources.end() ? nullptr : it->second;
        }

        /// The game ids of the registered sources, in the order of the map (alphabetical: deterministic).
        [[nodiscard]] std::vector<std::string> gameIds() const
        {
            std::vector<std::string> ids;
            ids.reserve(mSources.size());
            for (const auto& entry : mSources)
                ids.push_back(entry.first);
            return ids;
        }

        /// The cell, built on the first request. nullptr with `error` when the game is unknown, the source fails, the
        /// snapshot is refused or does not fit the caps. The pointer is valid until the next call of this function,
        /// removeSource or clear (a later request may drop the cell to make room); copy what must outlive that.
        [[nodiscard]] const openeden::sdk::game::WorldCell* cell(
            std::string_view gameId, openeden::sdk::game::WorldId id, std::string& error);

        [[nodiscard]] bool isResident(std::string_view gameId, openeden::sdk::game::WorldId id) const;

        // The lookups of the source interface (world_source.hpp 1.0), forwarded to the game's source. They build no
        // cell. A game the store does not know, a source that cannot say, or one that throws answers none (kNoWorldId;
        // 0.0 for the unit).
        [[nodiscard]] openeden::sdk::game::WorldId findCellByEditorId(
            std::string_view gameId, std::string_view editorId) const;
        [[nodiscard]] openeden::sdk::game::WorldId findCellByGrid(
            std::string_view gameId, openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y) const;
        [[nodiscard]] openeden::sdk::game::WorldId findWorldByEditorId(
            std::string_view gameId, std::string_view editorId) const;
        [[nodiscard]] double unitsPerMetre(std::string_view gameId) const;

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
