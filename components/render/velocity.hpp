#ifndef OPENEDEN_RENDER_VELOCITY_H
#define OPENEDEN_RENDER_VELOCITY_H

#include <cmath>
#include <vector>
#include <cstdint>

#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/core/Value.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>
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
     * Screen motion of the opaque scene from camera motion (task 0026, F3b). A fullscreen pass reads the G-buffer's
     * linear depth, rebuilds each pixel's view-space position, moves it into the previous frame's clip space with one
     * matrix and writes `currentUV - previousUV` (RG16F). Every pixel is treated as static scenery: exact for the world,
     * and wrong for anything that moves on its own (stage c adds skinned actors' own motion). The CPU twin of the
     * shader is Render::GBuffer::reprojectVelocity.
     *
     * Not exact for the first-person weapon, which is drawn with another field of view than the one used here: while
     * the camera turns it shows as an odd patch in the debug view.
     *
     * Owner: RenderManager. Main thread only; setMatrix may be called every frame (the record thread reads the value).
     * A resize replaces the image, framebuffer and draw (setSource); the render pass stays, since the pipeline is
     * compiled against it.
     */
    class Velocity
    {
    public:
        static constexpr VkFormat format = VK_FORMAT_R16G16_SFLOAT;

        // The camera has jumped, and the previous frame's matrices are not used, when it moves further than this in one
        // frame. A cell of the game world is 8192 units across; no walking, flying or levitation reaches that in a frame,
        // so only a teleport or a cell change does.
        static constexpr double cameraJumpDistance = 4096.0;

        /// The matrix the shader takes a view-space position of this frame into the previous frame's clip space with:
        /// `prevProjection * prevView * currentInvView`, multiplied in double precision (world positions are large, and
        /// the product is a small, exact relative motion once it is formed) and then narrowed.
        static vsg::mat4 toPreviousClip(
            const vsg::dmat4& prevProjection, const vsg::dmat4& prevView, const vsg::dmat4& currentInvView)
        {
            return vsg::mat4(prevProjection * prevView * currentInvView);
        }

        static bool cameraJumped(const vsg::dvec3& previousEye, const vsg::dvec3& currentEye)
        {
            const vsg::dvec3 d = currentEye - previousEye;
            return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) > cameraJumpDistance;
        }

        Velocity(vsg::Context& context, vsg::ref_ptr<vsgUtil::CompileContext> compile);
        ~Velocity();

        /// Register with the compile manager (CompileManager::add(framebuffer, view)) after Engine::setup, before
        /// setSource: the pipeline is compiled for this view and its render pass.
        vsg::ref_ptr<vsg::View> view() const { return mView; }
        vsg::ref_ptr<vsg::Framebuffer> registrationFramebuffer(vsg::Context& context);

        /// Builds the pass for a scene of this size, reading its linear depth; retired objects go to `retire`.
        template <typename Retire>
        bool setSource(vsg::Context& context, vsg::ref_ptr<vsg::ImageView> linearDepth, const VkExtent2D& extent,
            Retire&& retire)
        {
            for (const auto& object : mLive)
                retire(object);
            mLive.clear();
            return build(context, linearDepth, extent);
        }

        /// Between frames, from the main thread: `toPreviousClip` and the current projection's x and y scales.
        void setMatrix(const vsg::mat4& toPreviousClip, float projectionX, float projectionY);

        vsg::ref_ptr<vsg::RenderGraph> graph() const { return mGraph; }
        /// The velocity image, valid after a successful setSource.
        vsg::ref_ptr<vsg::ImageView> result() const { return mResult; }

    private:
        bool build(vsg::Context& context, vsg::ref_ptr<vsg::ImageView> linearDepth, const VkExtent2D& extent);

        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<vsg::View> mView;
        vsg::ref_ptr<vsg::RenderPass> mPass;
        vsg::ref_ptr<vsg::ShaderStage> mVertex;
        vsg::ref_ptr<vsg::ShaderStage> mFragment;
        vsg::ref_ptr<vsg::RenderGraph> mGraph;
        vsg::ref_ptr<vsg::ImageView> mResult;
        vsg::ref_ptr<vsg::mat4Value> mMatrix;
        vsg::ref_ptr<vsg::vec4Value> mScale;
        // The image, framebuffer and draw, given to `retire` by the next setSource.
        std::vector<vsg::ref_ptr<vsg::Object>> mLive;
    };
}

#endif
