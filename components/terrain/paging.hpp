#ifndef VSGOPENMW_TERRAIN_PAGING_H
#define VSGOPENMW_TERRAIN_PAGING_H

#include <map>
#include <memory>

#include <vsg/io/Options.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/Sampler.h>

#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/compileop.hpp>

#include "coverage.hpp"
#include "layermaps.hpp"
#include "view.hpp"

namespace Terrain
{
    class Storage;
    struct Builder;
    class ViewData;

    /// OpenEden (task 0088): the live terrain's chunk fade, for the player's view only. `fadeSeconds` at or below 0 is
    /// off (the old code, nothing remembered); `deltaSeconds` is the time since the last call.
    struct ChunkFadeParams
    {
        float fadeSeconds = 0.f;
        float deltaSeconds = 0.f;
    };

    /**
     * @brief Terrain implementation that draws cells in batches, with geometry LOD
     * @note The const implementation must be thread safe.
     */
    class Paging
    {
    public:
        Paging(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> imageOptions,
            vsg::ref_ptr<const vsg::Options> shaderOptions, vsg::ref_ptr<vsg::Sampler> samplerOptions,
            TerrainMapSettings mapSettings = {});
        ~Paging();

        float lodFactor = 1;
        int vertexLodMod = 0;

        /// @param storage Storage instance to get terrain data from (heights, normals, colors, textures..)
        void setStorage(Storage* storage);
        /// Task 0080: the builder, for the LOD terrain to share its layer arrays (Builder::setStorageSharingLayers).
        const Builder& builder() const { return *mBuilder; }

        /// Task 0089: whether the terrain's layer arrays are built (see Builder::layersReady).
        bool layersReady() const;

        /// Task 0080 (texture mips): see Builder::replaceArrayDescriptor and Builder::replaceBinds.
        bool replaceArrayDescriptor(const vsg::Descriptor* old, const vsg::ref_ptr<vsg::Descriptor>& replacement);
        void replaceBinds(
            const std::map<const vsg::BindDescriptorSet*, vsg::ref_ptr<vsg::BindDescriptorSet>>& replaced);

        std::unique_ptr<View> createView() const;
        // Fill in view with just the specified cell without LOD, reusing batch data created for referenceViewDistance.
        // Return a CompileOp that can be run straight away, or added to an OperationThread if care is taken to avoid
        // data races. With `heightRange`, the cell's lowest and highest terrain height are written there
        // (task 0073).
        vsg::ref_ptr<vsgUtil::CompileOp> updateViewForCell(
            View& v, const vsg::ivec2& cell, float referenceViewDistance, vsg::vec2* heightRange = nullptr) const;
        // Fill in view with LOD for specified view point and distance.
        // Return a CompileOp that can be run straight away, or added to an OperationThread if care is taken to avoid
        // data races.
        // OpenEden (task 0055): `coverage`, when given, lists squares of cells that something else draws. Chunks
        // inside them are left out, chunks that only partly overlap them are split, and chunks next to them stitch
        // their edges to the covering draw's vertex LOD. Only the main thread's call passes it, and it must not change
        // during the call. A view built for another coverage version is rebuilt even if the view point did not move.
        // OpenEden (task 0088): with `fade` the chunks that appear dither in, the ones that leave stay drawn and dither
        // out (or stay until what replaces them is solid); the view is rebuilt when one is dropped. Main thread only.
        vsg::ref_ptr<vsgUtil::CompileOp> updateView(View& view, const vsg::dvec3& viewPoint, float viewDistance,
            bool preload = false, const Coverage* coverage = nullptr, const ChunkFadeParams* fade = nullptr) const;
        void clearView(View& view) const;

        void pruneCache() const;
        /// Forgets batch data, for a storage whose land changed. The editor's: the game's land does not.
        void clearCache() const;

    protected:
        Storage* mStorage;
        std::unique_ptr<Builder> mBuilder;
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        const int mMaxBatchSize = 16;
        int getBatchSize(float cellViewDist) const;
    };
}

#endif
