#ifndef OPENEDEN_COMPONENTS_NEUTRALTERRAIN_STORAGE_H
#define OPENEDEN_COMPONENTS_NEUTRALTERRAIN_STORAGE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <components/terrain/storage.hpp>

#include "cellcache.hpp"

// The terrain storage of the neutral world (task 0120, stage E1 of docs/research/0053): a Terrain::Storage that serves
// the land a game pack described through the SDK (world_terrain.hpp), for ONE world at a time. It is the neutral
// counterpart of ESMTerrain::Storage and does not touch it: Morrowind's terrain is unchanged by construction.
//
// The cell size and the vertices per cell come from the world's description (Skyrim SE: 4096 and 33, so a vertex every
// 128 units), passed to the Terrain::Storage constructor, which is the only place Terrain::Builder and Terrain::Paging
// read them.
//
// Threading: Terrain::Storage's const interface must be thread safe, because chunks are built on worker threads.
// Nothing here calls the source: every read goes through a CellCache snapshot (an immutable shared_ptr), and the main
// thread fills the cache ahead of use (CellCache::ensure). A cell that was never loaded reads as "no land" (the world's
// default height, counted by CellCache::unloadedReads()).
//
// Ownership: the storage shares the cache (shared_ptr) so it cannot outlive its data.
//
// E1 draws the BASE ground texture of each quadrant (a blend map whose four corner indices are the quadrant's base
// layer): the four-layer weight splat is stage E2/E3 and needs a shader mode.
//
// A chunk also reads the cells to its east and north (their local 0 is its last row and column), so the caller
// ensures one cell more than the chunks it asks for on those sides.
//
// Seams: a vertex on a cell edge is taken from the cell to its north-east (the one whose local index is 0), as every
// chunk does, so two chunks that share an edge get the identical vertex and the terrain has no cracks even where the
// two cells' stored edges differ (0.27 percent of the Skyrim SE pairs). Where that cell has no land the vertex comes
// from the neighbour that has.

namespace NeutralTerrain
{
    class Storage final : public Terrain::Storage
    {
    public:
        explicit Storage(std::shared_ptr<CellCache> cache);

        VertexData getVertexData(int lodLevel, const Terrain::Bounds& bounds) override;
        vsg::ref_ptr<vsg::Data> getBlendmap(const Terrain::Bounds& bounds) override;
        std::vector<Terrain::LayerInfo> getLayers() override;
        float getHeightAt(const vsg::vec3& worldPos, ESM::RefId worldspace) override;

        /// The height of one vertex as getVertexData serves it: `gx`, `gy` are global fine vertex indices (cell *
        /// (verts
        /// - 1) + local). Exposed for the tests.
        [[nodiscard]] float vertexHeight(std::int64_t gx, std::int64_t gy) const;

        /// Terrain texels per cell side in the blend map (the Morrowind scheme: 16).
        static constexpr int kBlendTexelsPerCell = 16;

        [[nodiscard]] const CellCache& cache() const { return *mCache; }

        /// Task 0149: build the blend map as a splat map (splat.hpp) that carries the quadrants' alpha layers.
        void setLayerSplat(bool on) { mLayerSplat = on; }
        [[nodiscard]] bool layerSplat() const { return mLayerSplat; }

    private:
        std::shared_ptr<CellCache> mCache;
        bool mLayerSplat = false;
    };
}

#endif
