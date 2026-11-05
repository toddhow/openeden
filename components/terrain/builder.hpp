#ifndef VSGOPENMW_TERRAIN_BUILDER_H
#define VSGOPENMW_TERRAIN_BUILDER_H

#include <vsg/commands/DrawIndexed.h>
#include <vsg/core/Array2D.h>
#include <vsg/nodes/StateGroup.h>

#include <components/vsgutil/cache.hpp>

#include "bounds.hpp"
#include "chunkfade.hpp"
#include "indexbuffer.hpp"
#include "layermaps.hpp"
#include "storage.hpp"

namespace vsg
{
    class BindGraphicsPipeline;
}
namespace Terrain
{
    /*
     * Assembles a complete subgraph capable of drawing terrain.
     */
    struct Builder
    {
        Builder(vsg::ref_ptr<const vsg::Options> in_imageOptions, vsg::ref_ptr<const vsg::Options> in_shaderOptions,
            vsg::ref_ptr<vsg::Sampler> in_samplerOptions, TerrainMapSettings in_mapSettings = {});
        ~Builder();

        void pruneCache() const;
        void clearCache() const;

        void setStorage(Storage* in_storage);

        using IndexKey = std::pair<uint32_t /*numVerts*/, uint32_t /*flags*/>;
        vsgUtil::RefCache<IndexKey, vsg::BindIndexBuffer> indexCache;

        using Key = std::pair<Bounds, uint8_t /*lod*/>;
        struct BatchData
        {
            vsg::StateCommands stateCommands;
            vsg::ref_ptr<vsg::vec2Array2D> heightRanges;
            /// The batch's uniform (a vsg::Value of Pipeline::Data::Batch), for Builder::setBatchFade; task 0087.
            vsg::ref_ptr<vsg::Data> batchUniform;
        };
        vsgUtil::Cache<Key, BatchData> dataCache;

        /// Task 0087: makes the batches built from now on upload their uniform again when it changes, so that
        /// setBatchFade works on them. Off by default (the live terrain never changes it). Call before the first batch.
        void setDynamicBatchData(bool dynamic) { mDynamicBatchData = dynamic; }

        /// Task 0087: sets the share of the dither the batch's draws keep (clamped to 0..1) and marks the uniform for
        /// upload when it changed. False when the data has no uniform. Main thread; the batch must have been built
        /// after setDynamicBatchData(true) for the change to reach the GPU.
        static bool setBatchFade(const BatchData& data, float fade);

        /// Task 0088: sets the uniform to the fade code of the complementary dither: `fade` (0 to 1) when
        /// `complementOf` is below 0, else -1 minus `complementOf` (0 to 1). Both are rounded to 1/1024.
        static bool setBatchShare(const BatchData& data, float fade, float complementOf);

        BatchData create(const Key& key) const;
        BatchData getBatchData(const Bounds& bounds, uint8_t lod = 0) const;

        struct Batch
        {
            Bounds bounds;
            uint8_t lod = 0;
            BatchData data;
            vsg::ref_ptr<vsg::Node> root;
            vsg::ref_ptr<vsg::Group> leafGroup;
        };
        Batch createBatch(const Bounds& bounds, uint8_t lod = 0) const;

        /// OpenEden (task 0088): a chunk's draw command and the firstInstance it has without a fade, so that the live
        /// terrain can change its fade each frame (`draw->firstInstance = encodeChunkFade(baseInstance, fade)`).
        struct DrawHandle
        {
            vsg::ref_ptr<vsg::DrawIndexed> draw;
            uint32_t baseInstance = 0;
        };

        /// `share` (task 0088) is the chunk's share of the dither (1: all of it, the old value of firstInstance), or the
        /// complement of an incoming chunk's;
        /// `handle`, when given, receives the draw command.
        vsg::ref_ptr<vsg::Node> createDraw(const Batch& batch, const Bounds& bounds, uint8_t lod = 0,
            uint32_t lodFlags = 0, ChunkShare share = {}, DrawHandle* handle = nullptr) const;

        // OpenEden (task 0073): the lowest and highest terrain height under `bounds`, a part of the batch
        // `batchBounds`, from the batch data's height ranges (the values the cull node of a smaller draw is made from).
        vsg::vec2 heightRange(const BatchData& data, const Bounds& batchBounds, const Bounds& bounds) const;

    private:
        // openeden-vsg: the layer arrays with normal and specular maps, and each layer's flags for the shader
        // (files/shaders/terrain/main.frag): the normal map index in the low 16 bits, then parallax and specular.
        static constexpr uint32_t sNoNormalMap = 0xffff;
        static constexpr uint32_t sParallaxFlag = 1u << 16;
        static constexpr uint32_t sSpecularFlag = 1u << 17;
        void setLayersWithMaps(const std::vector<LayerInfo>& layers);

        vsg::ref_ptr<vsg::Node> createCullNode(vsg::ref_ptr<vsg::Node> child, const Bounds& batchBounds,
            const BatchData& data, const Bounds& bounds) const;

        vsg::ref_ptr<vsg::BindGraphicsPipeline> mPipeline;
        Storage* mStorage = nullptr;
        vsg::ref_ptr<const vsg::Options> mImageOptions;
        vsg::ref_ptr<vsg::Sampler> mSamplerOptions;
        vsg::ref_ptr<vsg::Descriptor> mLayerDescriptor;
        // openeden-vsg: terrain normal and specular maps (task 0012, V4i); descriptors are null with the maps off.
        TerrainMapSettings mMapSettings;
        bool mDynamicBatchData = false;
        vsg::ref_ptr<vsg::Descriptor> mNormalLayerDescriptor;
        vsg::ref_ptr<vsg::Descriptor> mLayerFlagsDescriptor;
        // Task 0044: the material map array (binding 8) and the per-layer scales (binding 9), with `pbr`.
        vsg::ref_ptr<vsg::Descriptor> mMaterialLayerDescriptor;
        vsg::ref_ptr<vsg::Descriptor> mLayerScalesDescriptor;
        // Task 0045: the height array (binding 10) and per layer its index and depth (binding 11), with parallax.
        vsg::ref_ptr<vsg::Descriptor> mHeightLayerDescriptor;
        vsg::ref_ptr<vsg::Descriptor> mLayerHeightsDescriptor;
    };
}

#endif
