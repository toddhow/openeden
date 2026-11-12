#ifndef OPENMW_MWRENDER_LODTERRAIN_H
#define OPENMW_MWRENDER_LODTERRAIN_H

// OpenEden world LOD (task 0055, stage S1d): baked terrain tiles drawn for the far field, behind `[WorldLod] enabled`.
// The tile layout is version 1 (components/lod), approved 2026-11-02.
//
// What it does, in plain words. At start it asks a worker thread to open <user data>/openeden/lod/terrain.edenpak,
// to check that the pack was made from the land the game has loaded (the pack's content digest against a digest of
// the live land), and to read every tile (each is a few KiB). Each frame, on the main thread, it chooses for every
// tile whether the tile or the live terrain is drawn (Lod::selectTiles, by pixel error), builds the draw of newly
// chosen tiles a few at a time with the same Terrain::Builder the live terrain uses (fed the tile's baked heights and
// normals, and the live terrain's colours and blend maps), and hands Terrain::Paging a Coverage of the tiles that are
// ready, so that the live chunks under them are left out. A tile whose draw is not ready is not in the coverage, so
// there is never a hole.
//
// Off means unchanged: RenderManager creates this only when the setting is on, and nothing here runs otherwise.
//
// Threads: the worker thread touches only the pack and the land sampler (both safe to read from a thread, like the
// terrain preload thread does). Everything else, including the scene graph, runs on the main thread.

#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <openeden/game/world_source.hpp>

#include <components/lod/albedobake.hpp>
#include <components/lod/lodworld.hpp>
#include <components/lod/residency.hpp>
#include <components/lod/tilebake.hpp>
#include <components/lod/tileselection.hpp>
#include <components/terrain/coverage.hpp>
#include <components/terrain/layermaps.hpp>
#include <components/terrain/storage.hpp>
#include <components/vsgutil/compilecontext.hpp>

namespace Terrain
{
    struct Builder;
}

namespace vsg
{
    class Options;
    class Sampler;
}
namespace VFS
{
    class Manager;
}

namespace MWRender
{
    /// The land of the game as the live terrain reads it, for Lod::LandSource (the baker and the staleness check).
    class StorageLandSource final : public Lod::LandSource
    {
    public:
        explicit StorageLandSource(Terrain::Storage& storage)
            : mStorage(storage)
        {
        }

        void sample(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells, std::uint32_t lod,
            std::vector<float>& heights, std::vector<float>& normals) const override;

    private:
        Terrain::Storage& mStorage;
    };

    /// The ground colour inputs of the game: blend maps and vertex colours from the live terrain storage, the layer
    /// pictures read the way Terrain::Builder reads them, and a digest of each layer's file. One user at a time (the
    /// caches are not locked): the baker on the main thread, or the staleness check on its worker thread, each with
    /// its own object. `layers` is the layer list of the live storage, taken once on the main thread.
    class StorageAlbedoSource final : public Lod::AlbedoSource
    {
    public:
        StorageAlbedoSource(Terrain::Storage& storage, std::vector<Terrain::LayerInfo> layers,
            vsg::ref_ptr<const vsg::Options> imageOptions, const VFS::Manager* vfs);
        ~StorageAlbedoSource() override;

        std::size_t layerCount() const override;
        /// Reads and decodes every layer on the first call (the pictures are the ones the live terrain draws with).
        const Lod::Rgba8Image* layerImage(std::size_t index) const override;
        EdenArchive::Hash layerDigest(std::size_t index) const override;
        /// For the message of a stale pack: the layer's name, the path the image options resolve it to, whether the
        /// virtual file system has that file, and the digest. One line.
        std::string describeLayer(std::size_t index) const;
        void blendMap(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
            std::vector<std::uint8_t>& indices) const override;
        void vertexColours(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
            std::vector<std::uint8_t>& rgb) const override;

    private:
        struct Caches;
        Terrain::Storage& mStorage;
        std::vector<Terrain::LayerInfo> mLayers;
        vsg::ref_ptr<const vsg::Options> mImageOptions;
        const VFS::Manager* mVfs;
        std::unique_ptr<Caches> mCaches;
    };

    /// The levels and sizes the baker and the runtime agree on (Lod::TerrainBakePlan's defaults).
    /// The ground picture's size follows the cell size (Lod::albedoSizeForCell): 256 for Morrowind, as always.
    /// A neutral world's plan has a ground picture on every level (task 0133): level 1 would otherwise take its blend
    /// map and vertex colours from the live neutral storage, which holds only the cells round the camera, and show the
    /// default ground where a far tile lies outside that window.
    Lod::TerrainBakePlan terrainBakePlan(float cellSizeUnits = 8192.f, bool neutralWorld = false);
    /// The settings the baker and the runtime agree on, for a storage with this cell size.
    /// `verticesPerCell` is the storage's Terrain::Storage::cellVertices (65 for Morrowind, 33 for Skyrim SE: task 0133).
    Lod::TileBakeSettings terrainBakeSettings(std::uint32_t cellSize, std::uint32_t verticesPerCell = 65);

    /// Task 0133: the facts a neutral world's pack is keyed and checked by (Lod::neutralFingerprint), from its source
    /// and the world description the terrain cache holds. Main thread (it asks the source).
    Lod::NeutralWorldFacts neutralWorldFacts(
        const openeden::sdk::game::WorldCellSource& source, const openeden::sdk::game::WorldSpaceInfo& world);

    class LodTerrain
    {
    public:
        struct Camera
        {
            vsg::dvec3 mEye;
            vsg::dvec3 mForward;
            double mFovYDegrees = 60;
            double mAspect = 16.0 / 9.0;
            double mViewportHeight = 720;
            double mViewDistance = 7168;
            /// How far the live terrain pages (the live view distance): a tile wholly inside it can give its square
            /// back to the live terrain, one reaching past it cannot (task 0060).
            double mPagingRange = 7168;
            /// Seconds since the last call (the frame time), for the fade of tiles that change (task 0087). A call with
            /// 0 (a cell grid change between frames) lets the fades stand.
            float mDeltaSeconds = 0;
        };

        struct Setup
        {
            std::filesystem::path mPack;
            /// Task 0133: a game pack's neutral world. The name its pack's manifest must carry
            /// (Lod::neutralWorldspaceName); with it set the pack is checked against that name, the cell size and the
            /// grid size instead of re-reading the land (the neutral cells are loaded a window at a time by the main
            /// thread, which a worker cannot do), and mLandCells stays empty. Empty: Morrowind's check, as before.
            std::string mNeutralWorldspaceName;
            float mThreshold = 8;
            /// The highest tile level read and drawn (`[WorldLod] far levels`); 0: every level of the pack.
            std::uint32_t mMaxLevel = 0;
            /// What the drawn tiles may hold in memory, in bytes (the tier's budget, counted from what each built draw
            /// holds); 0: no budget. A tile the budget leaves out is drawn by the live terrain.
            std::uint64_t mResidentBudgetBytes = 0;
            /// The game's terrain storage: must outlive the LodTerrain and be ready to read.
            Terrain::Storage* mLive = nullptr;
            /// The cells with land and the live layer list (taken once, on the main thread), for the staleness check.
            std::vector<std::pair<std::int32_t, std::int32_t>> mLandCells;
            std::vector<Terrain::LayerInfo> mLayers;
            vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
            vsg::ref_ptr<const vsg::Options> mImageOptions;
            vsg::ref_ptr<const vsg::Options> mShaderOptions;
            vsg::ref_ptr<vsg::Sampler> mSampler;
            Terrain::TerrainMapSettings mMapSettings;
            /// Task 0080: the live terrain's builder. When its layer arrays equal the ones this tile builder would
            /// make, they are used instead of a second set. Only read when the tiles are adopted (main thread); the
            /// builder outlives it (RenderManager's terrain is made first and destroyed last). May be null.
            /// Lifetime order: RenderManager declares mTerrain before mLodTerrain, so members destroy the LodTerrain
            /// first; whoever resets or replaces mTerrain must reset mLodTerrain first. Adoption is deferred
            /// (Terrain::mayAdoptTiles) until this builder's layers are ready, so it never blocks the main thread.
            const Terrain::Builder* mShareLayersFrom = nullptr;
            const VFS::Manager* mVfs = nullptr;
            /// Task 0087: seconds a tile takes to dither in or out when it appears, is replaced or is handed to the
            /// live terrain (`[WorldLod] terrain fade time`); 0 or less: the old instant swap, no fade code runs.
            float mFadeSeconds = 0;
        };
        explicit LodTerrain(Setup setup);
        ~LodTerrain();

        /// The group to put in the scene beside the live terrain.
        vsg::ref_ptr<vsg::Group> node() const;

        /// Main thread, once a frame. Returns what Terrain::Paging::updateView should leave out, or nullptr while the
        /// worker is not done or when the pack is unusable (the live terrain then draws everything).
        const Terrain::Coverage* update(const Camera& camera);

        /// Task 0080: the memory budget of the drawn tiles, changed while running (main thread, between updates); 0: no
        /// budget.
        void setResidentBudget(std::uint64_t bytes);
        std::uint64_t residentBudget() const;

        /// Task 0080 (texture mips): the tile builder and the tiles' own copies of their binds follow a swap of the
        /// layer arrays (see Terrain::Builder::replaceArrayDescriptor and replaceBinds).
        void replaceArrayDescriptor(const vsg::Descriptor* old, const vsg::ref_ptr<vsg::Descriptor>& replacement);
        void replaceBinds(
            const std::map<const vsg::BindDescriptorSet*, vsg::ref_ptr<vsg::BindDescriptorSet>>& replaced);
        /// Drops the tile builder's cached batches that nothing draws with (before and after an array swap).
        void pruneCache();
        /// Whether the tile builder uses the live terrain's layer arrays (valid once the pack is adopted).
        bool sharesLayerArrays() const;

        /// The squares of the tiles that are drawn now, in world units, for the cluster client's ground test (task
        /// 0060, far cluster eviction: Lod::groundDrawnOver). Rebuilt together with the coverage; the version changes
        /// with it. Main thread.
        const std::vector<Lod::GroundSquare>& drawnGround() const;
        std::uint64_t groundVersion() const;

        /// Task 0133: the box of land cells the pack's tiles cover, once the pack is adopted and usable (empty before). For
        /// the view limits of a neutral world, which has no ESM land to measure. Main thread.
        Lod::LandExtent landExtent() const;

        /// Counts for the log and the viewer.
        struct Stats
        {
            bool mReady = false;
            bool mUsable = false;
            std::size_t mTiles = 0; // in the pack
            std::size_t mDrawn = 0; // chosen and ready: in the coverage
            std::size_t mFading = 0; // drawn and dithering in or out (task 0087), not in the coverage
            std::size_t mLive = 0; // visible, live terrain
            std::size_t mSkipped = 0; // outside the view
            std::size_t mBlocked = 0; // wanted tiles the memory budget did not admit this frame
            std::size_t mCooling = 0; // wanted tiles a cooldown held back this frame (third review, finding 1)
            std::uint64_t mHeldBytes = 0; // what the drawn tiles hold (their images and height ranges)
            std::uint64_t mTileDataBytes = 0; // what the pack's tiles hold once read (heights, normals, pictures)
        };
        Stats stats() const;

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
}

#endif
