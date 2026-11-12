#ifndef GAME_MWWORLD_WORLDMODEL_H
#define GAME_MWWORLD_WORLDMODEL_H

#include <deque>
#include <list>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

#include <components/esmbridge/exteriorcelllocation.hpp>
#include <components/misc/algorithm.hpp>

#include "cellstore.hpp"
#include "ptr.hpp"
#include "ptrregistry.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;
    class ReadersCache;
    struct Cell;
}

namespace ESM4
{
    struct Cell;
}

namespace MWState
{
    class Loading;
}

namespace NeutralWorld
{
    class NeutralWorldStore;
}

namespace MWWorld
{
    class ESMStore;

    /// Task 0115: the answer of WorldModel::findNeutralCell. Found: `mStore` is the cell (new or reused). Exterior:
    /// the pack's cell is an exterior, which the neutral model cannot enter yet. Failed: unknown id or game, or the
    /// source could not build the cell (`mError` says why).
    enum class NeutralCellStatus
    {
        Found,
        Exterior,
        Failed,
    };

    struct NeutralCellLookup
    {
        NeutralCellStatus mStatus = NeutralCellStatus::Failed;
        CellStore* mStore = nullptr;
        bool mReused = false; ///< the store was built earlier and is returned as it is
        std::string mError;
        // Task 0125 (stage E2): for status Exterior, when the game pack can describe the cell's worldspace (its
        // WorldCellSource::worldInfo says so) the exterior can be entered: the worldspace it is known by in the engine
        // and the cell's grid square (when the pack's cell has one; a persistent cell has none).
        bool mWorldSupported = false;
        ESM::RefId mWorldspace;
        bool mHasGrid = false;
        std::int32_t mGridX = 0;
        std::int32_t mGridY = 0;
    };

    /// Task 0125: the pack and world a neutral worldspace of the engine stands for.
    struct NeutralWorldspace
    {
        std::string mGameId;
        openeden::sdk::game::WorldId mWorld = openeden::sdk::game::kNoWorldId;
    };

    /// Task 0140: what finding a neutral world by its editor id says.
    struct NeutralWorldLookup
    {
        std::string mGameId;
        openeden::sdk::game::WorldId mWorld = openeden::sdk::game::kNoWorldId;
        ESM::RefId mWorldspace; ///< empty unless the world was found and registered
        std::string mError;
    };

    /// \brief Cell container
    class WorldModel
    {
    public:
        explicit WorldModel(ESMStore& store, ESM::ReadersCache& reader);

        WorldModel(const WorldModel&) = delete;
        WorldModel& operator=(const WorldModel&) = delete;

        void clear();

        /// Task 0115 (neutral world, stage N4): the interior CellStore of a game pack's neutral cell by its pack id,
        /// built on first use exactly as findInterior builds one by editor id, and registered under its editor id so
        /// that the scene's by-name lookup after a teleport finds the same store. A cell built earlier (by either
        /// route) is returned as it is. An exterior is refused and nothing is added. Main thread. The second form
        /// uses the extension host's store; the first is for tests.
        NeutralCellLookup findNeutralCell(
            NeutralWorld::NeutralWorldStore& neutral, std::string_view gameId, openeden::sdk::game::WorldId id) const;
        NeutralCellLookup findNeutralCell(std::string_view gameId, openeden::sdk::game::WorldId id) const;

        /// Task 0140: finds a world of any known neutral game by its editor id (the first game that has it, in the
        /// store's order) and registers it as a neutral worldspace, so that getExterior works for it. The error says
        /// why not (no such world, or the game pack cannot describe it).
        NeutralWorldLookup findNeutralWorld(NeutralWorld::NeutralWorldStore& neutral, std::string_view editorId) const;

        CellStore& getExterior(ESM::ExteriorCellLocation location, bool forceLoad = true) const;

        /// Task 0125 (stage E2): the neutral worldspaces the engine knows (registered when a door or a start finds an
        /// exterior of a supported world); null for any other worldspace, Morrowind's included.
        const NeutralWorldspace* neutralWorldspace(const ESM::RefId& worldspace) const;

        /// The exterior cell of a registered neutral worldspace at a grid square, built from the pack's cell (or an empty
        /// cell when the pack has none there) and kept; null when the worldspace is not a registered neutral one. The
        /// first form takes the store explicitly (tests), getExterior uses the extension host's.
        CellStore* findNeutralExterior(
            NeutralWorld::NeutralWorldStore& neutral, const ESM::ExteriorCellLocation& location) const;

        CellStore* findCell(ESM::RefId id, bool forceLoad = true) const;

        CellStore& getCell(ESM::RefId id, bool forceLoad = true) const;

        // Returns a special cell that is never active. Can be used for creating objects
        // without adding them to the scene.
        CellStore& getDraftCell() const;

        CellStore* findInterior(std::string_view name, bool forceLoad = true) const;

        CellStore& getInterior(std::string_view name, bool forceLoad = true) const;

        CellStore* findCell(std::string_view name, bool forceLoad = true) const;

        CellStore& getCell(std::string_view name, bool forceLoad = true) const;

        Ptr getPtrByRefId(const ESM::RefId& name);

        Ptr getPtr(ESM::RefNum refNum) const { return mPtrRegistry.getOrEmpty(refNum); }

        PtrRegistryView getPtrRegistryView() const { return PtrRegistryView(mPtrRegistry); }

        ESM::RefNum getLastGeneratedRefNum() const { return mPtrRegistry.getLastGenerated(); }

        void setLastGeneratedRefNum(ESM::RefNum v) { mPtrRegistry.setLastGenerated(v); }

        std::size_t getPtrRegistryRevision() const { return mPtrRegistry.getRevision(); }

        void registerPtr(const Ptr& ptr);

        void deregisterLiveCellRef(LiveCellRefBase& ref) noexcept;

        void assignSaveFileRefNum(ESM::CellRef& ref) { mPtrRegistry.assign(ref); }

        template <typename Fn>
        void forEachLoadedCellStore(Fn&& fn)
        {
            for (auto& [_, store] : mCells)
                fn(store);
        }

        /// Get all Ptrs referencing \a name in exterior cells
        /// @note Due to the current implementation of getPtr this only supports one Ptr per cell.
        /// @note name must be lower case
        void getExteriorPtrs(const ESM::RefId& name, std::vector<MWWorld::Ptr>& out);

        std::vector<MWWorld::Ptr> getAll(const ESM::RefId& id);

        size_t countSavedGameRecords() const;

        void write(ESM::ESMWriter& writer, MWState::Loading& progress) const;

        bool readRecord(ESM::ESMReader& reader, uint32_t type);

    private:
        struct GetCellStoreCallback;

        PtrRegistry mPtrRegistry; // defined before mCells because during destruction it should be the last

        MWWorld::ESMStore& mStore;
        ESM::ReadersCache& mReaders;
        mutable std::unordered_map<ESM::RefId, CellStore> mCells;
        mutable std::map<std::string, CellStore*, Misc::StringUtils::CiComp> mInteriors;
        mutable std::map<ESM::ExteriorCellLocation, CellStore*> mExteriors;
        ESM::Cell mDraftCell;
        // The synthetic ESM::Cell records of the neutral cells (task 0109). A deque, so that the Cell of a CellStore
        // can keep a pointer into it for as long as the model lives.
        mutable std::deque<ESM::Cell> mNeutralCells;
        // Task 0125: neutral worldspaces by their RefId in the engine (see neutralWorldspaceId).
        mutable std::map<ESM::RefId, NeutralWorldspace> mNeutralWorldspaces;
        std::vector<std::pair<ESM::RefId, CellStore*>> mIdCache;
        std::size_t mIdCacheIndex = 0;

        CellStore& getOrInsertCellStore(const ESM::Cell& cell);

        /// Task 0109 (neutral world, stage N3a-3): the interior cell of this editor id from a game pack's neutral
        /// world, made into a CellStore (its base records go into the ESMStore's Store<NeutralBase>). Null when
        /// `[Game] neutral world` is off, no pack has such a cell, or the cell cannot be used (exteriors are not
        /// supported yet: design document 10.4, R7).
        CellStore* emplaceNeutralInteriorCellStore(std::string_view name) const;

        /// The shared builder of the two routes above (by name, by pack id): the bases of the cell's objects into the
        /// ESMStore and the CellStore into mCells; the store that already exists for this cell when there is one.
        CellStore* emplaceNeutralCellStore(std::string_view gameId, const openeden::sdk::game::WorldCell& cell,
            std::string_view name, bool& reused) const;

        CellStore& insertCellStore(const ESM::Cell& cell);

        Ptr getPtrAndCache(const ESM::RefId& name, CellStore& cellStore);

        void writeCell(ESM::ESMWriter& writer, CellStore& cell) const;
    };
}

#endif
