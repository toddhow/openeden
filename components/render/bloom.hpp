#ifndef OPENEDEN_RENDER_BLOOM_H
#define OPENEDEN_RENDER_BLOOM_H

#include <cstdint>
#include <vector>

#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/Group.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/vk/Framebuffer.h>
#include <vsg/vk/RenderPass.h>

#include <components/vsgutil/compilecontext.hpp>

namespace vsg
{
    class Context;
}

namespace Render
{
    /*
     * Bloom for the linear HDR path (task 0025, F2d): light that is brighter than the picture can show spills over its
     * surroundings. A chain of half-size float images: each is a filtered copy of the one before (a 13-tap downsample,
     * as in Jimenez's "Next Generation Post Processing in Call of Duty: Advanced Warfare", 2014), then the chain is
     * walked back up, each image added to the one above through a 3x3 tent filter. The largest image, a half of the
     * scene's size, is what the tonemap adds to the scene (Tonemap::Parameters::bloomStrength).
     *
     * There is no threshold: every pixel contributes, and the strength setting is small. That is how a physical lens
     * blurs, and it needs no tuning against the scene's brightness.
     *
     * Owner: RenderManager. Main thread only. A resize replaces the images, framebuffers and draws (setSource); the
     * render passes stay, since the pipelines are compiled against them.
     */
    class Bloom
    {
    public:
        // The images are half the size of the one before, down to this many or the smallest side of 8 pixels.
        static constexpr uint32_t maxLevels = 6;
        static constexpr uint32_t minSide = 8;

        /// `shaderOptions` read the shader files (post/fullscreen.vert, post/bloomdown.frag, post/bloomup.frag; task
        /// 0072, stage g).
        Bloom(vsg::Context& context, vsg::ref_ptr<vsgUtil::CompileContext> compile,
            vsg::ref_ptr<const vsg::Options> shaderOptions);
        ~Bloom();

        /// Register with the compile manager (CompileManager::add(framebuffer, view)) after Engine::setup, before
        /// setSource: the pipelines are compiled for this view and its render pass.
        vsg::ref_ptr<vsg::View> view() const { return mView; }
        vsg::ref_ptr<vsg::Framebuffer> registrationFramebuffer(vsg::Context& context);

        /// Builds the chain for a scene of this size, reading its colour; retired objects go to `retire`. False if
        /// a shader or pipeline could not be built.
        template <typename Retire>
        bool setSource(vsg::Context& context, vsg::ref_ptr<vsg::ImageView> sceneColor, const VkExtent2D& sceneExtent,
            Retire&& retire)
        {
            for (const auto& object : mLive)
                retire(object);
            mLive.clear();
            return build(context, sceneColor, sceneExtent);
        }

        /// The passes, in order. Goes in the frame's command graph before the tonemap.
        vsg::ref_ptr<vsg::Group> node() const { return mNode; }
        /// What the tonemap adds: the largest image of the chain.
        vsg::ref_ptr<vsg::ImageView> result() const { return mResult; }

        /// Number of images for a scene this size: halves until the smaller side would go under minSide.
        static uint32_t levelsFor(const VkExtent2D& sceneExtent);
        /// The size of image `level` (0 is half the scene's).
        static VkExtent2D levelExtent(const VkExtent2D& sceneExtent, uint32_t level);

    private:
        bool build(vsg::Context& context, vsg::ref_ptr<vsg::ImageView> sceneColor, const VkExtent2D& sceneExtent);

        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<vsg::View> mView;
        vsg::ref_ptr<vsg::RenderPass> mDownPass;
        vsg::ref_ptr<vsg::RenderPass> mUpPass;
        vsg::ref_ptr<const vsg::Options> mShaderOptions;
        // The stages of the down and the up pipeline; each takes the Slang twins of both its stages or neither.
        vsg::ShaderStages mDownStages;
        vsg::ShaderStages mUpStages;
        vsg::ref_ptr<vsg::Group> mNode;
        vsg::ref_ptr<vsg::ImageView> mResult;
        // The chain's images, framebuffers and draws, given to `retire` by the next setSource.
        std::vector<vsg::ref_ptr<vsg::Object>> mLive;
    };
}

#endif
