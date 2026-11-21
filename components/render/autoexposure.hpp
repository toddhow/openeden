#ifndef OPENEDEN_RENDER_AUTOEXPOSURE_H
#define OPENEDEN_RENDER_AUTOEXPOSURE_H

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>
#include <vsg/state/Buffer.h>
#include <vsg/state/ImageView.h>

#include <components/render/autoexposuremath.hpp>
#include <components/vsgutil/compilecontext.hpp>

namespace Render
{
    /*
     * The measuring half of automatic exposure (task 0364a; spec docs/tasks/0364a-auto-exposure.md): three compute
     * dispatches (files/shaders/post/autoexposure.slang) in a node that RenderManager puts after the TAA overlay and
     * before bloom and the tonemap: clear the histogram, count a sample of the scene's luminance into it, reduce it to
     * a trimmed mean in a slot of a small ring in host-visible memory. `read()` returns the newest slot that is old
     * enough to have finished (Render::AutoExposure::pickSlot); nothing waits for the GPU. The adaptation over time is
     * the caller's (Render::AutoExposure::State), so the pass has no clock.
     *
     * Owner: RenderManager, main thread only. A resize calls setSource again; the old build goes to `retire`.
     */
    class AutoExposurePass
    {
    public:
        AutoExposurePass(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> shaderOptions,
            float lowPercentile, float highPercentile, float highlightPercentile);
        ~AutoExposurePass();

        /// True if the three compute shader files are there (a build without slangc has none).
        static bool shadersAvailable(vsg::ref_ptr<const vsg::Options> shaderOptions);

        /// The pixels between two samples for a scene of this size, so that no more than maxSamples are taken (the
        /// luminance sum is a 32-bit integer).
        static std::uint32_t strideFor(const VkExtent2D& extent);

        /// Builds the buffers, descriptors and commands for this scene colour image (in SHADER_READ_ONLY_OPTIMAL when
        /// the pass runs). The old build goes to `retire`. False (and a log line) on failure; the pass then does
        /// nothing.
        template <typename Retire>
        bool setSource(vsg::ref_ptr<vsg::ImageView> sceneColour, const VkExtent2D& extent, Retire&& retire)
        {
            for (const auto& object : mLive)
                retire(object);
            mLive.clear();
            return build(sceneColour, extent);
        }

        vsg::ref_ptr<vsg::Node> node() const { return mNode; }

        /// Main thread, once a frame before the scene is recorded: writes the push constant of the frame.
        void beginFrame();

        /// The newest measurement at least `latency` frames old, or nothing. Cheap: reads eight slots of host memory.
        std::optional<AutoExposure::Measurement> read();

        std::uint64_t frame() const { return mFrame; }

    private:
        bool build(vsg::ref_ptr<vsg::ImageView> sceneColour, const VkExtent2D& extent);

        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<const vsg::Options> mShaderOptions;
        float mLow;
        float mHigh;
        float mHighlight;
        vsg::ref_ptr<vsg::Group> mNode; // the same object across resizes: its children are replaced
        vsg::ref_ptr<vsg::vec4Value> mPush;
        vsg::ref_ptr<vsg::vec4Value> mPush2;
        vsg::ref_ptr<vsg::Buffer> mResults; // host visible, coherent, kept mapped
        const volatile std::uint32_t* mMapped = nullptr;
        std::uint32_t mStride = 4;
        std::uint64_t mFrame = 0;
        std::vector<vsg::ref_ptr<vsg::Object>> mLive;
    };
}

#endif
