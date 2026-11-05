#ifndef OPENEDEN_RENDER_HDRTARGET_H
#define OPENEDEN_RENDER_HDRTARGET_H

#include <vector>

#include <vsg/app/RenderGraph.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/state/ImageView.h>
#include <vsg/vk/Framebuffer.h>
#include <vsg/vk/RenderPass.h>

namespace vsg
{
    class Context;
    class Device;
}

namespace Render
{
    /*
     * The float render target of the linear HDR path (task 0025, F2): the main scene draws here and Render::Tonemap
     * turns it into the swapchain's 8-bit picture.
     *
     * Attachments are what later stages add to: colour output 0 is the scene's light; F3 appends outputs (view-space
     * normals, velocity) through `extraColorFormats`, which the render pass, the framebuffer and the resolved views
     * already support; the shaders then get a second colour output behind a define.
     *
     * Each colour output is a multisampled image (with more than one sample) resolved into a single-sample image that
     * can be sampled and copied, and is left in SHADER_READ_ONLY_OPTIMAL by the render pass. Depth is the same: a
     * multisampled image resolved (sample zero) into a single-sample one, left in DEPTH_STENCIL_ATTACHMENT_OPTIMAL as
     * the window's depth is, which Reflect and PostProcess::depthCopy rely on.
     *
     * Multisampling (task 0026): the extra outputs resolve by averaging, so at an object's edge a normal is the average
     * of two surfaces' normals (not unit length) and the linear depth lies between two distances; only the hardware
     * depth is exact (sample zero).
     *
     * Owner: RenderManager. Everything runs on the main thread. A resize replaces the images and the framebuffer and
     * keeps the render pass object, because the scene's pipelines were compiled against it.
     */
    class HdrTarget
    {
    public:
        // 16-bit float, so that light above 1 survives to the tonemap. The format is in the specification's list of
        // formats every device supports for sampling, blending, blits and as a colour attachment.
        static constexpr VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

        // The layout the render pass leaves the resolved colour in.
        static constexpr VkImageLayout colorFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        HdrTarget(vsg::Context& context, VkSampleCountFlagBits samples, const VkExtent2D& extent,
            const std::vector<VkFormat>& extraColorFormats = {});

        vsg::ref_ptr<vsg::RenderPass> renderPass() const { return mRenderPass; }
        vsg::ref_ptr<vsg::Framebuffer> framebuffer() const { return mFramebuffer; }
        VkExtent2D extent() const { return mExtent; }
        VkSampleCountFlagBits samples() const { return mSamples; }

        // The resolved, single-sample colour of output i (0 is the scene), and the resolved depth.
        vsg::ref_ptr<vsg::ImageView> colorView(std::size_t output = 0) const { return mResolvedColor.at(output); }
        vsg::ref_ptr<vsg::ImageView> depthView() const { return mResolvedDepth; }
        std::size_t outputCount() const { return mResolvedColor.size(); }

        // Task 0026 (F3): the G-buffer outputs by name, null when the target was made without them. Output 1 is the
        // view-space normal, 2 the linear depth, 3 the skinned actors' motion, 4 the material
        // (Render::GBuffer::extraColorFormats).
        vsg::ref_ptr<vsg::ImageView> normalView() const { return outputOrNull(1); }
        vsg::ref_ptr<vsg::ImageView> linearDepthView() const { return outputOrNull(2); }
        vsg::ref_ptr<vsg::ImageView> objectMotionView() const { return outputOrNull(3); }
        // Task 0027: metalness, reflectance, occlusion and the subsurface mark; only with the material model.
        vsg::ref_ptr<vsg::ImageView> materialView() const { return outputOrNull(4); }
        // Task 0029: the ambient part of the colour, for the occlusion; only with ambient occlusion on.
        vsg::ref_ptr<vsg::ImageView> ambientView() const { return outputOrNull(5); }

        // Task 0075: a framebuffer in a second render pass compatible with this target's, over the same extra outputs
        // and depth (loaded, not cleared) and `colour` in place of colour 0, for the draws that follow the TAA resolve.
        // `colour` must be a colour attachment of this target's format and extent, in GENERAL when the pass begins; it
        // leaves in SHADER_READ_ONLY_OPTIMAL. Null when the target is multisampled. Rebuild after a resize.
        vsg::ref_ptr<vsg::Framebuffer> createOverlayFramebuffer(vsg::ref_ptr<vsg::ImageView> colour) const;

        // The G-buffer outputs start every frame as "no surface" (all zero), not at the scene's clear colour: call
        // after RenderGraph::setClearValues, which gives every colour attachment the same value.
        void clearExtraOutputs(vsg::RenderGraph& graph) const;

        // Makes new images and a new framebuffer; the old ones are given to `retire`, which must keep them alive until
        // the frames already recorded have drawn (vsgUtil::CompileContext::detach). Does nothing at the same extent.
        template <typename Retire>
        void resize(vsg::Context& context, const VkExtent2D& extent, Retire&& retire)
        {
            if (extent.width == mExtent.width && extent.height == mExtent.height)
                return;
            retire(mFramebuffer);
            for (const auto& view : mAllViews)
                retire(view);
            createImages(context, extent);
        }

    private:
        vsg::ref_ptr<vsg::ImageView> outputOrNull(std::size_t i) const
        {
            return i < mResolvedColor.size() ? mResolvedColor[i] : vsg::ref_ptr<vsg::ImageView>();
        }
        void createImages(vsg::Context& context, const VkExtent2D& extent);

        VkSampleCountFlagBits mSamples;
        VkExtent2D mExtent{};
        std::vector<VkFormat> mColorFormats;
        vsg::ref_ptr<vsg::RenderPass> mRenderPass;
        vsg::ref_ptr<vsg::RenderPass> mOverlayRenderPass;
        vsg::ref_ptr<vsg::Framebuffer> mFramebuffer;
        std::vector<vsg::ref_ptr<vsg::ImageView>> mResolvedColor;
        vsg::ref_ptr<vsg::ImageView> mResolvedDepth;
        vsg::ImageViews mAllViews;
    };
}

#endif
