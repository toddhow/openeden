#include "hdrtarget.hpp"

#include <vsg/vk/Context.h>

#include <components/vsgutil/image.hpp>

#include "attachmentformat.hpp"

namespace Render
{
    namespace
    {
        // As Render::standardDependencies, plus what a target that is sampled afterwards needs. The same images are
        // drawn into every frame while the previous frame's tonemap (and the copy for the water) may still be reading
        // them, so the render pass waits for those readers, and makes its writes visible to the next readers.
        vsg::RenderPass::Dependencies hdrDependencies()
        {
            constexpr VkPipelineStageFlags writeStages
                = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            constexpr VkAccessFlags writeAccess
                = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            constexpr VkPipelineStageFlags readStages
                = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
            constexpr VkAccessFlags readAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

            vsg::SubpassDependency before = { VK_SUBPASS_EXTERNAL, 0, writeStages | readStages,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                writeAccess | readAccess,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                    | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                0 };
            vsg::SubpassDependency after
                = { 0, VK_SUBPASS_EXTERNAL, writeStages, readStages, writeAccess, readAccess, 0 };
            return { before, after };
        }

        vsg::AttachmentDescription attachment(VkFormat format, VkSampleCountFlagBits samples, VkAttachmentLoadOp load,
            VkAttachmentStoreOp store, VkImageLayout finalLayout)
        {
            vsg::AttachmentDescription a = {};
            a.format = format;
            a.samples = samples;
            a.loadOp = load;
            a.storeOp = store;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            a.finalLayout = finalLayout;
            return a;
        }

        /*
         * Attachment order. One sample: colour 0..n-1, depth. More: multisampled colour 0..n-1, resolved colour
         * 0..n-1, multisampled depth, resolved depth. Everything is stored, as in the main pass
         * (Render::createMultisampledRenderPass with store on), so that vsgUtil::SuspendRenderPass can end and resume
         * the pass for the copies the water needs.
         */
        vsg::ref_ptr<vsg::RenderPass> createHdrRenderPass(
            vsg::Device* device, VkSampleCountFlagBits samples, const std::vector<VkFormat>& colorFormats)
        {
            const bool multisampled = samples != VK_SAMPLE_COUNT_1_BIT;
            const auto depthFinal = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            vsg::RenderPass::Attachments attachments;
            vsg::SubpassDescription subpass;
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            const uint32_t n = static_cast<uint32_t>(colorFormats.size());

            if (!multisampled)
            {
                for (uint32_t i = 0; i < n; ++i)
                {
                    attachments.push_back(attachment(colorFormats[i], VK_SAMPLE_COUNT_1_BIT,
                        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, HdrTarget::colorFinalLayout));
                    subpass.colorAttachments.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
                }
                attachments.push_back(attachment(compatibleDepthFormat, VK_SAMPLE_COUNT_1_BIT,
                    VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, depthFinal));
                subpass.depthStencilAttachments.push_back({ n, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL });
            }
            else
            {
                for (uint32_t i = 0; i < n; ++i)
                {
                    attachments.push_back(attachment(colorFormats[i], samples, VK_ATTACHMENT_LOAD_OP_CLEAR,
                        VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
                    subpass.colorAttachments.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
                }
                for (uint32_t i = 0; i < n; ++i)
                {
                    attachments.push_back(attachment(colorFormats[i], VK_SAMPLE_COUNT_1_BIT,
                        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE, HdrTarget::colorFinalLayout));
                    subpass.resolveAttachments.push_back({ n + i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
                }
                attachments.push_back(attachment(compatibleDepthFormat, samples, VK_ATTACHMENT_LOAD_OP_CLEAR,
                    VK_ATTACHMENT_STORE_OP_STORE, depthFinal));
                attachments.push_back(attachment(compatibleDepthFormat, VK_SAMPLE_COUNT_1_BIT,
                    VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE, depthFinal));
                subpass.depthStencilAttachments.push_back({ 2 * n, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL });
                subpass.depthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                subpass.stencilResolveMode = VK_RESOLVE_MODE_NONE;
                subpass.depthStencilResolveAttachments.push_back(
                    { 2 * n + 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL });
            }
            return vsg::RenderPass::create(
                device, attachments, vsg::RenderPass::Subpasses{ subpass }, hdrDependencies());
        }

        /*
         * Task 0075: a second render pass over the same attachments, for the draws that follow the TAA resolve. Same
         * formats, sample count and subpass as createHdrRenderPass (so the scene's pipelines are compatible with it);
         * every attachment is loaded and stored, and colour 0 is the TAA output, which the resolve leaves in GENERAL.
         * Single sample only (TAA refuses multisampling).
         */
        vsg::ref_ptr<vsg::RenderPass> createOverlayRenderPass(
            vsg::Device* device, const std::vector<VkFormat>& colorFormats)
        {
            vsg::RenderPass::Attachments attachments;
            vsg::SubpassDescription subpass;
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            const uint32_t n = static_cast<uint32_t>(colorFormats.size());
            for (uint32_t i = 0; i < n; ++i)
            {
                auto a = attachment(colorFormats[i], VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD,
                    VK_ATTACHMENT_STORE_OP_STORE, HdrTarget::colorFinalLayout);
                a.initialLayout = i == 0 ? VK_IMAGE_LAYOUT_GENERAL : HdrTarget::colorFinalLayout;
                attachments.push_back(a);
                subpass.colorAttachments.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
            }
            auto depth = attachment(compatibleDepthFormat, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD,
                VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments.push_back(depth);
            subpass.depthStencilAttachments.push_back({ n, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL });

            // Before: everything recorded earlier (the scene pass, the compute resolve, whose writes are made
            // available by the explicit barrier that ends the resolve node), as the screenshot code argues for its
            // copy. After: the bloom and the tonemap sample colour 0.
            constexpr VkPipelineStageFlags attachmentStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            constexpr VkAccessFlags attachmentAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            vsg::SubpassDependency before = { VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                attachmentStages, VK_ACCESS_MEMORY_WRITE_BIT, attachmentAccess, 0 };
            vsg::SubpassDependency after = { 0, VK_SUBPASS_EXTERNAL, attachmentStages,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, attachmentAccess,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, 0 };
            return vsg::RenderPass::create(device, attachments, vsg::RenderPass::Subpasses{ subpass },
                vsg::RenderPass::Dependencies{ before, after });
        }
    }

    HdrTarget::HdrTarget(vsg::Context& context, VkSampleCountFlagBits samples, const VkExtent2D& extent,
        const std::vector<VkFormat>& extraColorFormats)
        : mSamples(samples)
        , mColorFormats{ colorFormat }
    {
        mColorFormats.insert(mColorFormats.end(), extraColorFormats.begin(), extraColorFormats.end());
        mRenderPass = createHdrRenderPass(context.device, samples, mColorFormats);
        if (samples == VK_SAMPLE_COUNT_1_BIT)
            mOverlayRenderPass = createOverlayRenderPass(context.device, mColorFormats);
        createImages(context, extent);
    }

    vsg::ref_ptr<vsg::Framebuffer> HdrTarget::createOverlayFramebuffer(vsg::ref_ptr<vsg::ImageView> colour) const
    {
        if (!mOverlayRenderPass || !colour)
            return {};
        vsg::ImageViews attachments;
        attachments.push_back(colour);
        for (std::size_t i = 1; i < mResolvedColor.size(); ++i)
            attachments.push_back(mResolvedColor[i]);
        attachments.push_back(mResolvedDepth);
        return vsg::Framebuffer::create(mOverlayRenderPass, attachments, mExtent.width, mExtent.height, 1);
    }

    void HdrTarget::clearExtraOutputs(vsg::RenderGraph& graph) const
    {
        // Attachment order (createHdrRenderPass): the multisampled colours first, then, with multisampling, the
        // resolved ones; the extra outputs are indices 1.. of each group.
        const std::size_t n = mColorFormats.size();
        const bool multisampled = mSamples != VK_SAMPLE_COUNT_1_BIT;
        for (std::size_t i = 1; i < n; ++i)
        {
            for (std::size_t index : { i, n + i })
            {
                if ((index != i && !multisampled) || index >= graph.clearValues.size())
                    continue;
                graph.clearValues[index].color = VkClearColorValue{ { 0.f, 0.f, 0.f, 0.f } };
            }
        }
    }

    void HdrTarget::createImages(vsg::Context& context, const VkExtent2D& extent)
    {
        mExtent = extent;
        mAllViews.clear();
        mResolvedColor.clear();
        const bool multisampled = mSamples != VK_SAMPLE_COUNT_1_BIT;
        // The resolved images are sampled by the tonemap and copied by the water's opaque-scene copy.
        constexpr VkImageUsageFlags readable = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        const auto makeView
            = [&](VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples, VkImageAspectFlags aspect) {
                  auto image = vsgUtil::createImage(format, extent, usage, samples);
                  auto view = vsg::createImageView(context, image, aspect);
                  mAllViews.push_back(view);
                  return view;
              };

        vsg::ImageViews attachments;
        if (multisampled)
            for (VkFormat format : mColorFormats)
                attachments.push_back(
                    makeView(format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, mSamples, VK_IMAGE_ASPECT_COLOR_BIT));
        for (VkFormat format : mColorFormats)
        {
            auto view = makeView(format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | readable, VK_SAMPLE_COUNT_1_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            attachments.push_back(view);
            mResolvedColor.push_back(view);
        }
        if (multisampled)
            attachments.push_back(makeView(compatibleDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, mSamples,
                VK_IMAGE_ASPECT_DEPTH_BIT));
        mResolvedDepth = makeView(compatibleDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | readable,
            VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        attachments.push_back(mResolvedDepth);

        mFramebuffer = vsg::Framebuffer::create(mRenderPass, attachments, extent.width, extent.height, 1);
    }
}
