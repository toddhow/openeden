#include "bloom.hpp"

#include <algorithm>

#include <vsg/commands/Draw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/PushConstants.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/state/ViewportState.h>
#include <vsg/vk/Context.h>

#include <components/debug/debuglog.hpp>
#include <components/vsgutil/image.hpp>
#include <components/vsgutil/passstages.hpp>
#include <components/vsgutil/recordwithviewid.hpp>
#include <components/vsgutil/setviewportstate.hpp>

#include "hdrtarget.hpp"

namespace Render
{
    namespace
    {
        // One colour attachment of the HDR format. `load`: the up passes add to what the down passes left in the image.
        vsg::ref_ptr<vsg::RenderPass> createPass(vsg::Device* device, bool load)
        {
            vsg::AttachmentDescription attachment = {};
            attachment.format = HdrTarget::colorFormat;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = load ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            vsg::SubpassDescription subpass = {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachments.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

            // Every pass writes an image that an earlier pass (or the previous frame's tonemap) has read, and is read
            // by the next one.
            vsg::SubpassDependency before = { VK_SUBPASS_EXTERNAL, 0,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0 };
            vsg::SubpassDependency after = { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                0 };
            return vsg::RenderPass::create(device, vsg::RenderPass::Attachments{ attachment },
                vsg::RenderPass::Subpasses{ subpass }, vsg::RenderPass::Dependencies{ before, after });
        }

        struct Pass
        {
            vsg::ref_ptr<vsg::ImageView> source;
            vsg::ref_ptr<vsg::Framebuffer> target;
            VkExtent2D sourceExtent;
            VkExtent2D targetExtent;
            bool up;
        };
    }

    uint32_t Bloom::levelsFor(const VkExtent2D& sceneExtent)
    {
        uint32_t levels = 0;
        VkExtent2D extent = sceneExtent;
        while (levels < maxLevels)
        {
            extent = { std::max(1u, extent.width / 2), std::max(1u, extent.height / 2) };
            if (std::min(extent.width, extent.height) < minSide)
                break;
            ++levels;
        }
        return levels;
    }

    VkExtent2D Bloom::levelExtent(const VkExtent2D& sceneExtent, uint32_t level)
    {
        VkExtent2D extent = sceneExtent;
        for (uint32_t i = 0; i <= level; ++i)
            extent = { std::max(1u, extent.width / 2), std::max(1u, extent.height / 2) };
        return extent;
    }

    Bloom::Bloom(vsg::Context& context, vsg::ref_ptr<vsgUtil::CompileContext> compile,
        vsg::ref_ptr<const vsg::Options> shaderOptions)
        : mCompile(std::move(compile))
        , mView(vsg::View::create())
        , mDownPass(createPass(context.device, false))
        , mUpPass(createPass(context.device, true))
        , mShaderOptions(std::move(shaderOptions))
        , mNode(vsg::Group::create())
    {
        mView->viewDependentState = nullptr;
    }

    Bloom::~Bloom() {}

    vsg::ref_ptr<vsg::Framebuffer> Bloom::registrationFramebuffer(vsg::Context& context)
    {
        auto image = vsgUtil::createImage(HdrTarget::colorFormat, VkExtent2D{ 1, 1 },
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        auto view = vsg::createImageView(context, image, VK_IMAGE_ASPECT_COLOR_BIT);
        return vsg::Framebuffer::create(mDownPass, vsg::ImageViews{ view }, 1, 1, 1);
    }

    bool Bloom::build(vsg::Context& context, vsg::ref_ptr<vsg::ImageView> sceneColor, const VkExtent2D& sceneExtent)
    {
        // Task 0072, stage g: the stages were strings here.
        if (mDownStages.empty())
            mDownStages = vsgUtil::readPassStages(
                { { "post/fullscreen.vert", {} }, { "post/bloomdown.frag", {} } }, mShaderOptions);
        if (mUpStages.empty())
            mUpStages = vsgUtil::readPassStages(
                { { "post/fullscreen.vert", {} }, { "post/bloomup.frag", {} } }, mShaderOptions);
        if (mDownStages.empty() || mUpStages.empty())
        {
            Log(Debug::Error) << "Failed to compile a bloom shader";
            return false;
        }

        mNode->children.clear();
        mResult = {};
        const uint32_t levels = levelsFor(sceneExtent);
        if (levels == 0)
            return true;

        // The chain: image i is half the size of image i - 1, and image 0 half the scene.
        std::vector<vsg::ref_ptr<vsg::ImageView>> images;
        std::vector<vsg::ref_ptr<vsg::Framebuffer>> framebuffers;
        for (uint32_t level = 0; level < levels; ++level)
        {
            const VkExtent2D extent = levelExtent(sceneExtent, level);
            auto image = vsgUtil::createImage(
                HdrTarget::colorFormat, extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            auto view = vsg::createImageView(context, image, VK_IMAGE_ASPECT_COLOR_BIT);
            images.push_back(view);
            framebuffers.push_back(
                vsg::Framebuffer::create(mDownPass, vsg::ImageViews{ view }, extent.width, extent.height, 1));
            mLive.push_back(view);
            mLive.push_back(framebuffers.back());
        }

        std::vector<Pass> passes;
        passes.push_back({ sceneColor, framebuffers[0], sceneExtent, levelExtent(sceneExtent, 0), false });
        for (uint32_t level = 1; level < levels; ++level)
            passes.push_back({ images[level - 1], framebuffers[level], levelExtent(sceneExtent, level - 1),
                levelExtent(sceneExtent, level), false });
        for (uint32_t level = levels - 1; level-- > 0;)
            passes.push_back({ images[level + 1], framebuffers[level], levelExtent(sceneExtent, level + 1),
                levelExtent(sceneExtent, level), true });

        const auto imageType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        auto sampler = vsg::Sampler::create();
        sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        auto setLayout = vsg::DescriptorSetLayout::create(
            vsg::DescriptorSetLayoutBindings{ { 0, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } });
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{ { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vsg::vec4) } });
        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_FALSE;
        depthStencil->depthWriteEnable = VK_FALSE;
        auto raster = vsg::RasterizationState::create();
        raster->cullMode = VK_CULL_MODE_NONE;
        const VkColorComponentFlags all
            = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        auto replace = vsg::ColorBlendState::create(
            vsg::ColorBlendState::ColorBlendAttachments{ { VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, all } });
        auto add = vsg::ColorBlendState::create(
            vsg::ColorBlendState::ColorBlendAttachments{ { VK_TRUE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
                VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, all } });

        // One compile for all the draws, under the view that the chain's render pass was registered with.
        auto compileForView = mCompile->clone(~vsg::Mask(0));
        const vsg::ref_ptr<vsg::View> view = mView;
        uint32_t viewId = 0;
        bool matched = false;
        compileForView->contextSelectionFunction = [view, &viewId, &matched](vsg::Context& selected) {
            if (selected.view.get() != view.get())
                return false;
            matched = true;
            viewId = selected.viewID;
            return true;
        };

        auto draws = vsg::Commands::create(); // compiled together; each pass records its own part
        std::vector<vsg::ref_ptr<vsg::Commands>> perPass;
        for (const Pass& pass : passes)
        {
            const vsg::GraphicsPipelineStates states{ vsg::VertexInputState::create(),
                vsg::InputAssemblyState::create(), raster, vsg::MultisampleState::create(), pass.up ? add : replace,
                depthStencil, vsg::ViewportState::create(0, 0, pass.targetExtent.width, pass.targetExtent.height) };
            auto pipeline = vsg::BindGraphicsPipeline::create(
                vsg::GraphicsPipeline::create(layout, pass.up ? mUpStages : mDownStages, states));
            vsg::Descriptors descriptors{ vsg::DescriptorImage::create(
                vsg::ImageInfo::create(sampler, pass.source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), 0, 0,
                imageType) };
            auto texel = vsg::vec4Value::create(
                vsg::vec4(1.f / pass.sourceExtent.width, 1.f / pass.sourceExtent.height, 0.f, 0.f));
            auto commands = vsg::Commands::create();
            commands->children = { vsgUtil::SetViewportState::create(vsg::ViewportState::create(
                                       0, 0, pass.targetExtent.width, pass.targetExtent.height)),
                pipeline,
                vsg::BindDescriptorSet::create(
                    VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, vsg::DescriptorSet::create(setLayout, descriptors)),
                vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, 0, texel), vsg::Draw::create(3, 1, 0, 0) };
            if (!compileForView->compile(commands))
                return false;
            perPass.push_back(commands);
        }
        if (!matched)
        {
            Log(Debug::Error) << "The bloom view is not registered with the compile manager";
            return false;
        }

        for (std::size_t i = 0; i < passes.size(); ++i)
        {
            auto graph = vsg::RenderGraph::create();
            graph->framebuffer = passes[i].target;
            if (passes[i].up)
                graph->renderPass = mUpPass;
            graph->renderArea = { { 0, 0 }, passes[i].targetExtent };
            graph->windowResizeHandler = nullptr;
            auto draw = vsgUtil::RecordWithViewId::create(perPass[i], viewId);
            graph->children = { draw };
            mNode->children.push_back(graph);
            mLive.push_back(draw);
        }
        mResult = images[0];
        return true;
    }
}
