#ifndef OPENEDEN_RENDER_AMBIENTOCCLUSION_H
#define OPENEDEN_RENDER_AMBIENTOCCLUSION_H

#include <vector>

#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/maths/vec4.h>
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
     * Ambient occlusion (task 0029, stage b): a fullscreen pass at half resolution that reads the G-buffer's linear
     * depth and view-space normals and writes how open each pixel is (1 open, 0 closed), with the slice integral of
     * ground truth ambient occlusion (Render::Ao::gtaoSliceVisibility; the shader repeats aomath.hpp's functions:
     * change both together). The image is noisy by design (slice direction and step offsets come from an interleaved
     * gradient noise of the pixel position, no time); the tonemap filters it with the depth and normal aware weights
     * and takes the ambient part of the colour out by it (Render::Tonemap).
     *
     * Modelled on Render::Velocity: the render pass is made once (the pipeline is compiled against it), a resize
     * replaces the image, framebuffer and draw (setSource). Owner: RenderManager; main thread only.
     */
    class AmbientOcclusion
    {
    public:
        static constexpr VkFormat format = VK_FORMAT_R8_UNORM;

        /// `shaderOptions` read the shader files (post/fullscreen.vert, post/ao.frag) and give the fragment stage
        /// [Rendering] ao radius, ao slices and ao steps through ao.settings (Resource::ShaderSettings; task 0072,
        /// stage g: this class took them as Settings and wrote them into its GLSL string).
        AmbientOcclusion(vsg::Context& context, vsg::ref_ptr<vsgUtil::CompileContext> compile,
            vsg::ref_ptr<const vsg::Options> shaderOptions);
        ~AmbientOcclusion();

        /// Register with the compile manager (CompileManager::add(framebuffer, view)) after Engine::setup, before
        /// setSource.
        vsg::ref_ptr<vsg::View> view() const { return mView; }
        vsg::ref_ptr<vsg::Framebuffer> registrationFramebuffer(vsg::Context& context);

        /// Builds the pass for a scene of this size; retired objects go to `retire`. The occlusion image is half the
        /// size (Render::Ao::halfExtent).
        template <typename Retire>
        bool setSource(vsg::Context& context, vsg::ref_ptr<vsg::ImageView> linearDepth,
            vsg::ref_ptr<vsg::ImageView> normal, const VkExtent2D& extent, Retire&& retire)
        {
            for (const auto& object : mLive)
                retire(object);
            mLive.clear();
            return build(context, linearDepth, normal, extent);
        }

        /// Between frames, from the main thread: the projection's x and y scales.
        void setProjection(float projectionX, float projectionY);

        vsg::ref_ptr<vsg::RenderGraph> graph() const { return mGraph; }
        vsg::ref_ptr<vsg::ImageView> result() const { return mResult; }
        VkExtent2D resultExtent() const { return mResultExtent; }

    private:
        bool build(vsg::Context& context, vsg::ref_ptr<vsg::ImageView> linearDepth, vsg::ref_ptr<vsg::ImageView> normal,
            const VkExtent2D& extent);

        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<vsg::View> mView;
        vsg::ref_ptr<vsg::RenderPass> mPass;
        vsg::ref_ptr<const vsg::Options> mShaderOptions;
        // The vertex and fragment stage: the Slang twins of both or neither.
        vsg::ShaderStages mStages;
        vsg::ref_ptr<vsg::RenderGraph> mGraph;
        vsg::ref_ptr<vsg::ImageView> mResult;
        VkExtent2D mResultExtent{ 1, 1 };
        vsg::ref_ptr<vsg::vec4Value> mProjection;
        std::vector<vsg::ref_ptr<vsg::Object>> mLive;
    };
}

#endif
