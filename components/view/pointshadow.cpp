#include "pointshadow.hpp"

#include <vsg/app/RenderGraph.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/vk/Context.h>

#include <components/pipeline/viewbindings.hpp>
#include <components/render/depth.hpp>
#include <components/render/pointshadowgpu.hpp>
#include <components/render/pointshadowmath.hpp>
#include <components/vsgutil/image.hpp>

#include "shadow.hpp"

namespace View
{
    vsg::dmat4 PointShadowProjection::transform() const
    {
        return Render::PointShadow::faceProjectionMatrix(tanHalf, far);
    }

    struct PointShadow::Face
    {
        vsg::ref_ptr<vsg::RenderGraph> rendergraph;
        vsg::ref_ptr<vsg::View> view;
        vsg::ref_ptr<vsg::LookAt> lookAt = vsg::LookAt::create();
        vsg::ref_ptr<PointShadowProjection> projection = PointShadowProjection::create();
    };

    PointShadow::PointShadow(vsg::Context& ctx, int in_slotCount, std::uint32_t res, std::size_t maxLights)
        : slotCount(std::clamp(in_slotCount, 1, Render::PointShadow::maxSlots))
        , resolution(res)
        , tanHalf(Render::PointShadow::tanHalfFov(Render::PointShadow::defaultGuardTexels, res))
    {
        // Both sides are drawn and nothing is clamped to the near plane (research note 0039, section 5).
        mBaseView = vsg::View::create();
        mBaseView->viewDependentState = nullptr;
        mBaseView->overridePipelineStates = depthOnlyPipelineStates(false, VK_CULL_MODE_NONE);

        const auto format = VK_FORMAT_D32_SFLOAT;
        const int layers = slotCount * Render::PointShadow::facesPerLight;
        auto image = vsgUtil::createImage(
            format, VkExtent2D{ res, res }, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        image->arrayLayers = static_cast<std::uint32_t>(layers);
        image->compile(ctx);

        auto imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_DEPTH_BIT);
        imageView->viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;

        auto sampler = vsg::Sampler::create();
        sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler->minFilter = sampler->magFilter = VK_FILTER_LINEAR;
        sampler->maxAnisotropy = 1.0f;
        sampler->minLod = 0.0f;
        sampler->maxLod = 1.0f;
        sampler->borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        sampler->compareEnable = VK_TRUE;
        sampler->compareOp = VK_COMPARE_OP_GREATER; // reversed depth, as the sun's maps

        mShadowMap = vsg::DescriptorImage::create(vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>(), imageView,
                                                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
            Pipeline::Descriptors::VIEW_POINT_SHADOW_MAP_BINDING, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        mShadowSampler = vsg::DescriptorImage::create(vsg::ImageInfo::create(sampler, vsg::ref_ptr<vsg::ImageView>()),
            Pipeline::Descriptors::VIEW_POINT_SHADOW_SAMPLER_BINDING, 0, VK_DESCRIPTOR_TYPE_SAMPLER);

        mData = vsg::vec4Array::create(Render::PointShadow::bufferVec4s(maxLights));
        for (auto& v : *mData)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        mData->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        mDataDescriptor = vsg::DescriptorBuffer::create(
            mData, Pipeline::Descriptors::VIEW_POINT_SHADOW_DATA_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        auto renderPass = createDepthRenderPass(ctx.device, format);
        for (int layer = 0; layer < layers; ++layer)
        {
            auto layerView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_DEPTH_BIT);
            layerView->subresourceRange.layerCount = 1;
            layerView->subresourceRange.baseArrayLayer = static_cast<std::uint32_t>(layer);
            layerView->viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            layerView->compile(ctx.device);

            auto face = std::make_unique<Face>();
            face->rendergraph = vsg::RenderGraph::create();
            face->rendergraph->renderArea.offset = {};
            face->rendergraph->renderArea.extent = { res, res };
            face->rendergraph->framebuffer
                = vsg::Framebuffer::create(renderPass, vsg::ImageViews{ layerView }, res, res, 1);
            face->rendergraph->clearValues = { { .depthStencil = { Render::farDepth, 0 } } };
            face->view = createShadowView(*mBaseView);
            face->view->camera
                = vsg::Camera::create(face->projection, face->lookAt, vsg::ViewportState::create(0, 0, res, res));
            face->projection->tanHalf = tanHalf;
            mFaces.push_back(std::move(face));
        }
    }

    PointShadow::~PointShadow() {}

    vsg::ref_ptr<vsg::RenderGraph> PointShadow::renderGraph(int slot, int face) const
    {
        return mFaces[static_cast<std::size_t>(slot * Render::PointShadow::facesPerLight + face)]->rendergraph;
    }

    vsg::ref_ptr<vsg::View> PointShadow::faceView(int slot, int face) const
    {
        return mFaces[static_cast<std::size_t>(slot * Render::PointShadow::facesPerLight + face)]->view;
    }

    void PointShadow::setSlotLight(int slot, const vsg::dvec3& position, double far)
    {
        for (int f = 0; f < Render::PointShadow::facesPerLight; ++f)
        {
            Face& face = *mFaces[static_cast<std::size_t>(slot * Render::PointShadow::facesPerLight + f)];
            const Render::PointShadow::FaceAxes axes = Render::PointShadow::faceAxes(f);
            face.lookAt->eye = position;
            face.lookAt->center = position + axes.forward;
            face.lookAt->up = axes.up;
            face.projection->far = far;
        }
    }
}
