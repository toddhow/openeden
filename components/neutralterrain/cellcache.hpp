#ifndef OPENEDEN_COMPONENTS_NEUTRALTERRAIN_CELLCACHE_H
#define OPENEDEN_COMPONENTS_NEUTRALTERRAIN_CELLCACHE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <openeden/game/world_source.hpp>
#include <openeden/game/world_terrain.hpp>

// The terrain cell cache of the neutral world (task 0120, stage E1 of docs/research/0053). Terrain chunks are built on
// worker threads (Terrain::Paging and Builder), but a WorldCellSource is main thread only; so the main thread fills
// this cache from the source (ensure*), and workers read immutable snapshots out of it (get). A snapshot is a
// std::shared_ptr<const TerrainCell>: once published it never changes, and it stays alive while a worker holds it even
// if the cache drops the cell to stay within its cap.
//
// A cell is looked up by (x, y) in the ONE world the cache was set to (setWorld), following the parent chain for land
// the world borrows (WorldSpaceInfo::parent with TerrainParentUsage::Land): a cell the world has no land for takes the
// parent's, and so on up to maxParentDepth. Whatever was found, or the fact that there is none, is kept under the
// requested (x, y).
//
// Who owns what: the cache owns its snapshots, the world info and the ground texture table; it does NOT own the source,
// which must outlive it. Threading: setWorld, ensure*, and the destructor are for the main thread (the source's
// thread); get, isKnown, world(), textures() and the counters are safe from any thread AFTER setWorld returned (world
// and textures never change afterwards). Hostile input: a source's cell is validated (sizes, finite numbers, caps)
// before it is published; one that fails is counted, logged in errors() and treated as "no land".

namespace NeutralTerrain
{
    struct CacheLimits
    {
        std::size_t mMaxCells = 1024; ///< published cells kept (including "no land" entries); oldest dropped first
        std::uint32_t mMaxParentDepth = 8;
        std::uint32_t mMaxVertsPerSide = 257;
        std::size_t mMaxTextures = 65536;
        std::size_t mMaxErrors = 64;
        std::int64_t mMaxEnsureCells = 4096; ///< one ensure() call's area
    };

    /// Checks a cell a source built against the world it was asked for. Located message in `error` ("cell (x, y): ...").
    /// Pure; exposed for the tests.
    [[nodiscard]] bool validateTerrainCell(const openeden::sdk::game::TerrainCell& cell, std::uint32_t expectedVerts,
        const CacheLimits& limits, std::string& error);

    class CellCache
    {
    public:
        explicit CellCache(openeden::sdk::game::WorldCellSource& source, CacheLimits limits = {});

        /// Selects the world and reads its description, its parents' descriptions and the ground texture table. False
        /// (with `error`) when the source does not know the world or has no terrain. Call before anything else.
        [[nodiscard]] bool setWorld(openeden::sdk::game::WorldId world, std::string& error);

        /// Loads the cell if it is not known. True when the cell is now known (with or without land).
        bool ensureCell(std::int32_t x, std::int32_t y);

        /// Loads the cells x0..x1, y0..y1 (inclusive). Returns how many it asked the source for.
        std::size_t ensure(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1);

        // ---- any thread, after setWorld ----

        /// The cell's land, or null when it has none or is not loaded (isKnown tells which).
        [[nodiscard]] std::shared_ptr<const openeden::sdk::game::TerrainCell> get(std::int32_t x, std::int32_t y) const;
        [[nodiscard]] bool isKnown(std::int32_t x, std::int32_t y) const;

        [[nodiscard]] const openeden::sdk::game::WorldSpaceInfo& world() const { return mWorld; }
        [[nodiscard]] bool hasWorld() const { return mHasWorld; }
        /// Ground textures, the default (id 0) first. Never changes after setWorld.
        [[nodiscard]] const std::vector<openeden::sdk::game::GroundTexture>& textures() const { return mTextures; }
        /// The index into textures() of a texture id; 0 (the default) for an unknown id.
        [[nodiscard]] std::size_t textureIndex(openeden::sdk::game::WorldId id) const;

        /// get() calls for a cell that was not loaded at all (the caller forgot ensure), and cells refused.
        [[nodiscard]] std::uint64_t unloadedReads() const { return mUnloadedReads.load(); }
        [[nodiscard]] std::uint64_t refusedCells() const { return mRefused.load(); }
        [[nodiscard]] std::uint64_t builtCells() const { return mBuilt.load(); }
        [[nodiscard]] std::size_t resident() const;
        [[nodiscard]] std::vector<std::string> errors() const;

    private:
        using Key = std::pair<std::int32_t, std::int32_t>;
        struct Entry
        {
            std::shared_ptr<const openeden::sdk::game::TerrainCell> mCell; ///< null: known to have no land
        };

        void publish(const Key& key, std::shared_ptr<const openeden::sdk::game::TerrainCell> cell);
        void noteError(const std::string& message);

        openeden::sdk::game::WorldCellSource& mSource;
        CacheLimits mLimits;
        bool mHasWorld = false;
        openeden::sdk::game::WorldSpaceInfo mWorld;
        std::map<openeden::sdk::game::WorldId, openeden::sdk::game::WorldSpaceInfo> mInfos; ///< the chain, main thread
        std::vector<openeden::sdk::game::GroundTexture> mTextures;
        std::map<openeden::sdk::game::WorldId, std::size_t> mTextureIndex;

        mutable std::mutex mMutex;
        std::map<Key, Entry> mEntries;
        std::deque<Key> mOrder;
        std::vector<std::string> mErrors;
        mutable std::atomic<std::uint64_t> mUnloadedReads{ 0 };
        std::atomic<std::uint64_t> mRefused{ 0 };
        std::atomic<std::uint64_t> mBuilt{ 0 };
    };
}

#endif
