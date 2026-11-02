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
#include <memory>
#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <components/lod/tilebake.hpp>
#include <components/lod/tileselection.hpp>
#include <components/terrain/coverage.hpp>
#include <components/terrain/layermaps.hpp>
#include <components/terrain/storage.hpp>
#include <components/vsgutil/compilecontext.hpp>

namespace vsg
{
    class Options;
    class Sampler;
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
        };

        /// `live` is the game's terrain storage (it must outlive this object and be ready to read). `landCells` are
        /// the cells with land, for the staleness check.
        LodTerrain(std::filesystem::path pack, float threshold, Terrain::Storage& live,
            std::vector<std::pair<std::int32_t, std::int32_t>> landCells, vsg::ref_ptr<vsgUtil::CompileContext> compile,
            vsg::ref_ptr<const vsg::Options> imageOptions, vsg::ref_ptr<const vsg::Options> shaderOptions,
            vsg::ref_ptr<vsg::Sampler> sampler, Terrain::TerrainMapSettings mapSettings);
        ~LodTerrain();

        /// The group to put in the scene beside the live terrain.
        vsg::ref_ptr<vsg::Group> node() const;

        /// Main thread, once a frame. Returns what Terrain::Paging::updateView should leave out, or nullptr while the
        /// worker is not done or when the pack is unusable (the live terrain then draws everything).
        const Terrain::Coverage* update(const Camera& camera);

        /// Counts for the log and the viewer.
        struct Stats
        {
            bool mReady = false;
            bool mUsable = false;
            std::size_t mTiles = 0; // in the pack
            std::size_t mDrawn = 0; // chosen and ready: in the coverage
            std::size_t mLive = 0; // visible, live terrain
            std::size_t mSkipped = 0; // outside the view
        };
        Stats stats() const;

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
}

#endif
