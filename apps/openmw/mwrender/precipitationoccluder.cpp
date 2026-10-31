#include "precipitationoccluder.hpp"

#include <vsg/app/Camera.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/vk/Context.h>

#include <components/pipeline/bindings.hpp>
#include <components/render/depth.hpp>
#include <components/view/shadow.hpp>
#include <components/vsgutil/image.hpp>

namespace MWRender
{
    namespace
    {
        // The depth map's size in each direction (OpenMW's rttSize).
        constexpr uint32_t sMapSize = 256;
    }

    PrecipitationOccluder::PrecipitationOccluder(vsg::Context& context)
    {
        const auto format = VK_FORMAT_D32_SFLOAT;
        auto image = vsgUtil::createImage(format, VkExtent2D{ sMapSize, sMapSize },
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        image->compile(context);
        auto imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_DEPTH_BIT);
        imageView->compile(context.device);

        mRenderGraph = vsg::RenderGraph::create();
        mRenderGraph->renderArea.offset = {};
        mRenderGraph->renderArea.extent = { sMapSize, sMapSize };
        mRenderGraph->framebuffer = vsg::Framebuffer::create(View::createDepthRenderPass(context.device, format),
            vsg::ImageViews{ imageView }, sMapSize, sMapSize, 1);
        mRenderGraph->clearValues = { { .depthStencil = { Render::farDepth, 0 } } };

        mOrtho = vsg::Orthographic::create();
        mLookAt = vsg::LookAt::create();
        mView = vsg::View::create();
        mView->viewDependentState = nullptr;
        mView->overridePipelineStates = View::depthOnlyPipelineStates(false, VK_CULL_MODE_NONE);
        mView->camera
            = vsg::Camera::create(mOrtho, mLookAt, vsg::ViewportState::create(0, 0, sMapSize, sMapSize));

        // Particles outside the box see the far depth, so nothing occludes them (OpenMW's border colour).
        auto sampler = vsg::Sampler::create();
        sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler->borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK; // reversed depth
        sampler->minFilter = sampler->magFilter = VK_FILTER_LINEAR;
        mMap = vsg::DescriptorImage::create(
            vsg::ImageInfo::create(sampler, imageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
            Pipeline::Descriptors::OCCLUSION_UNIT, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }

    PrecipitationOccluder::~PrecipitationOccluder() {}

    void PrecipitationOccluder::update(const vsg::vec3& cameraPosition, const vsg::vec3& range)
    {
        const OcclusionBox box = makeOcclusionBox(cameraPosition, range);
        mBoxSize = box.size.x;
        mBoxHeight = box.zRange;
        mOrtho->left = -box.size.x / 2;
        mOrtho->right = box.size.x / 2;
        mOrtho->bottom = -box.size.y / 2;
        mOrtho->top = box.size.y / 2;
        mOrtho->nearDistance = 0;
        mOrtho->farDistance = box.zRange;
        mLookAt->eye = vsg::dvec3(box.centre.x, box.centre.y, box.zMax);
        mLookAt->center = vsg::dvec3(box.centre.x, box.centre.y, box.zMax - box.zRange);
        mLookAt->up = vsg::dvec3(0, 1, 0);
    }
}
