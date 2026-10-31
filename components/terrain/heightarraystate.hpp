#ifndef VSGOPENMW_TERRAIN_HEIGHTARRAYSTATE_H
#define VSGOPENMW_TERRAIN_HEIGHTARRAYSTATE_H

#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/DescriptorSet.h>

#include <components/vsgutil/arraystate.hpp>

namespace Terrain
{
    /*
     * Reproduces vertex shader for intersection testing on the CPU.
     */
    class HeightArrayState : public vsgUtil::ArrayState<HeightArrayState>
    {
        const vsg::floatArray2D* mHeights = nullptr;
    public:
        vsg::Object* parent{};
        float vertexScale = 0;
        vsg::vec2 origin;
        uint32_t heightBinding = 0;
        using vsg::ConstVisitor::apply;

        void apply(const vsg::BindDescriptorSet& bds) override
        {
            apply(*bds.descriptorSet);
        }
        void apply(const vsg::BindVertexBuffers&) override {}
        void apply(const vsg::DescriptorSet& ds) override
        {
            for (auto& descriptor : ds.descriptors)
            {
                if (descriptor->dstBinding == heightBinding)
                {
                    descriptor->accept(*this);
                    break;
                }
            }
        }
        void apply(const vsg::DescriptorImage& di) override
        {
            di.imageInfoList[0]->imageView->image->data->accept(*this);
        }
        void apply(const vsg::floatArray2D& in_heights) override
        {
            mHeights = &in_heights;
        }
        vsg::ref_ptr<const vsg::vec3Array> vertexArray(uint32_t instance) override
        {
            if (!mHeights)
                return {};
            uint8_t lod = instance & 0xff;
            uint32_t shiftSize = (instance >> 8) & 0xff;
            float chunkScale = 1.f / (1 << shiftSize);
            vsg::ivec2 gridPos = vsg::ivec2((instance >> 16) & 0xff, (instance >> 24) & 0xff);
            vsg::vec2 chunkOffset = vsg::vec2(gridPos) * chunkScale;

            auto w = mHeights->width();
            auto h = mHeights->height();
            uint32_t increment = 1 << lod;
            auto chunkW = (w-1) * chunkScale/ increment + 1;
            auto chunkH = (h-1) * chunkScale/ increment + 1;
            vsg::ref_ptr<vsg::vec3Array> proxy (getOrCreateProxyVertices(parent, chunkW * chunkH));
            uint32_t x0 = (w-1) * chunkOffset.x;
            uint32_t y0 = (h-1) * chunkOffset.y;
            for (uint32_t x = 0; x < chunkW; ++x)
            {
                for (uint32_t y = 0; y < chunkH; ++y)
                {
                    auto srcX = x0 + x * increment;
                    auto srcY = y0 + y * increment;
                    proxy->at(x * chunkH + y) = {
                        srcX / float(w-1) * vertexScale + origin.x,
                        srcY / float(h-1) * vertexScale + origin.y,
                        mHeights->at(srcX, srcY) };
                }
            }
            return proxy;
        }
    };
}

#endif
