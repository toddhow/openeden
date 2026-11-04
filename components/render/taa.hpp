#ifndef OPENEDEN_RENDER_TAA_H
#define OPENEDEN_RENDER_TAA_H

#include <cstdint>
#include <vector>

#include <vsg/app/ProjectionMatrix.h>
#include <vsg/commands/Commands.h>
#include <vsg/core/Object.h>
#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/maths/mat4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>
#include <vsg/state/ImageView.h>

#include <components/render/taamath.hpp>
#include <components/vsgutil/compilecontext.hpp>

namespace vsg
{
    class Context;
}

namespace Render::Taa
{
    /// The clip-space shift of the current frame, shared by the cameras that draw with it (task 0075). Written on the
    /// main thread between frames, read when the projection is asked for while recording.
    class JitterOffset : public vsg::Inherit<vsg::Object, JitterOffset>
    {
    public:
        vsg::dvec2 ndc{ 0.0, 0.0 };
    };

    /// A projection with the jitter in front: `jitterMatrix(offset->ndc) * base->transform()`. It holds the base by
    /// reference, so the base's field of view and extent changes are seen; `changeExtent` does nothing because
    /// `vsg::RelativeProjection::changeExtent` would rescale the shift on a resize. Owned by the cameras that use it.
    class JitteredProjection : public vsg::Inherit<vsg::ProjectionMatrix, JitteredProjection>
    {
    public:
        JitteredProjection(vsg::ref_ptr<vsg::ProjectionMatrix> inBase, vsg::ref_ptr<JitterOffset> inOffset)
            : base(std::move(inBase))
            , offset(std::move(inOffset))
        {
        }
        vsg::ref_ptr<vsg::ProjectionMatrix> base;
        vsg::ref_ptr<JitterOffset> offset;

        vsg::dmat4 transform() const override { return jitterMatrix(offset->ndc) * base->transform(); }
        void changeExtent(const VkExtent2D& /*prevExtent*/, const VkExtent2D& /*newExtent*/) override {}
    };

    /// What anything that accumulates over time (the ambient occlusion of the next feature) needs to line up with the
    /// resolve: read it, do not change it. Valid after `TaaPass::beginFrame`.
    struct TemporalState
    {
        std::uint32_t frame = 0; // counts calls of beginFrame since the start
        vsg::dvec2 jitterPixels{ 0.0, 0.0 }; // this frame's shift of the picture, in pixels
        vsg::dvec2 jitterNdc{ 0.0, 0.0 };
        vsg::dvec2 previousJitterPixels{ 0.0, 0.0 };
        bool historyValid = false; // false on a reset: nothing may be taken from the previous frame
        vsg::mat4 toPreviousClip; // this frame's view space to the previous frame's clip space (unjittered)
    };

    struct Settings
    {
        float sharpness = 0.25f;
        float jitterScale = 1.f;
        float history = defaultHistoryWeight;
        Clipping clipping = Clipping::Variance;
        float clipGamma = varianceGamma; // the variance box's width in standard deviations (Salvi 2016: 1)
        DebugView debug = DebugView::None;
    };

    struct FrameInput
    {
        vsg::mat4 toPreviousClip; // Velocity::toPreviousClip of this frame
        float projectionX = 1.f; // the (unjittered) projection's [0][0] and [1][1]
        float projectionY = 1.f;
        bool reset = false; // historyReset's answer for this frame
    };
}

namespace Render
{
    /*
     * The temporal anti-aliasing resolve (task 0075, docs/tasks/0075-temporal-anti-aliasing.md). Two compute passes
     * (files/shaders/post/taa.slang) in a node that RenderManager puts after the scene, velocity and occlusion passes
     * and before the bloom: `resolveMain` blends the scene with the history, `outputMain` keeps the history and writes
     * the sharpened picture that `result()` shows to the bloom and the tonemap.
     *
     * Images (RGBA16F, the scene's size): resolve and history stay in GENERAL; the output is GENERAL while written and
     * SHADER_READ_ONLY_OPTIMAL after. Owner: RenderManager, main thread only. A resize calls setSource again; what the
     * old build held is given to `retire` (vsgUtil::CompileContext::detach).
     */
    class TaaPass
    {
    public:
        /// `jitter` is the offset the cameras' JitteredProjections hold; the pass writes it each frame.
        TaaPass(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> shaderOptions,
            const Taa::Settings& settings, vsg::ref_ptr<Taa::JitterOffset> jitter);
        ~TaaPass();

        /// True if both compute shader files are there (a build without slangc has none).
        static bool shadersAvailable(vsg::ref_ptr<const vsg::Options> shaderOptions);

        /// Builds the images, descriptors and commands for a scene of this size from the scene's colour, the stored
        /// linear depth and the velocity (each left in SHADER_READ_ONLY_OPTIMAL by its pass). The old build goes to
        /// `retire`. False (and a log line) if a shader cannot be read or compiled; the pass then does nothing.
        template <typename Retire>
        bool setSource(vsg::ref_ptr<vsg::ImageView> colour, vsg::ref_ptr<vsg::ImageView> linearDepth,
            vsg::ref_ptr<vsg::ImageView> velocity, const VkExtent2D& extent, Retire&& retire)
        {
            for (const auto& object : mLive)
                retire(object);
            mLive.clear();
            return build(colour, linearDepth, velocity, extent);
        }

        /// The node to put in the command graph; it stays the same object across resizes.
        vsg::ref_ptr<vsg::Node> node() const { return mNode; }

        /// The picture bloom and the tonemap read, valid after a successful setSource.
        vsg::ref_ptr<vsg::ImageView> result() const { return mResult; }

        /// Main thread, once a frame before the scene is recorded: advances the frame counter, picks the jitter and
        /// writes the push constants.
        void beginFrame(const Taa::FrameInput& input);

        const Taa::TemporalState& state() const { return mState; }
        const Taa::Settings& settings() const { return mSettings; }
        VkExtent2D extent() const { return mExtent; }

        /// The history needs this many frames after a build before it is trusted (the first frames discard its
        /// contents to put it in its layout; a frame may be dropped, so not only the first).
        static constexpr std::uint32_t warmupFrames = 3;

    private:
        bool build(vsg::ref_ptr<vsg::ImageView> colour, vsg::ref_ptr<vsg::ImageView> linearDepth,
            vsg::ref_ptr<vsg::ImageView> velocity, const VkExtent2D& extent);

        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<const vsg::Options> mShaderOptions;
        Taa::Settings mSettings;
        vsg::ref_ptr<vsg::Group> mNode; // the same object across resizes: its children are replaced
        vsg::ref_ptr<vsg::Switch> mFirstSwitch;
        vsg::ref_ptr<vsg::ImageView> mResult;
        vsg::ref_ptr<Taa::JitterOffset> mJitter;
        vsg::ref_ptr<vsg::mat4Value> mMatrix;
        vsg::ref_ptr<vsg::vec4Value> mScale; // xy: projection scales, zw: the image size
        vsg::ref_ptr<vsg::vec4Value> mParams; // history weight, clipping, gamma, debug view
        vsg::ref_ptr<vsg::vec4Value> mLook; // sharpness
        VkExtent2D mExtent{};
        Taa::TemporalState mState;
        std::uint32_t mFramesSinceBuild = 0;
        // Everything the last build made, given to `retire` by the next setSource.
        std::vector<vsg::ref_ptr<vsg::Object>> mLive;
    };
}

#endif
