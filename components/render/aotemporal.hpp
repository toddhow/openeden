#ifndef OPENEDEN_RENDER_AOTEMPORAL_H
#define OPENEDEN_RENDER_AOTEMPORAL_H

#include <cstdint>
#include <optional>
#include <vector>

#include <vsg/commands/Commands.h>
#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>
#include <vsg/state/ImageView.h>

#include <components/render/aotemporalmath.hpp>
#include <components/vsgutil/compilecontext.hpp>

namespace vsg
{
    class Context;
}

namespace Render::Ao::Temporal
{
    struct Settings
    {
        float history = defaultHistoryWeight;
        float clampGamma = defaultClampGamma;
        DebugView debug = DebugView::None;
        // The pass's three occlusion settings ([Rendering] ao radius, ao slices, ao steps): specialization constants
        // 1100 to 1102 of the shader, the way post/aosettings.slang has them.
        float radius = 100.f;
        int slices = 2;
        int steps = 6;
    };

    struct FrameInput
    {
        vsg::mat4
            toPreviousClip; // Velocity::toPreviousClip of this frame (only its last row, the previous depth, is used)
        Rotation3 rotation{}; // relativeRotation(this frame's view, the previous frame's)
        float projectionX = 1.f; // the (unjittered) projection's [0][0] and [1][1]
        float projectionY = 1.f;
        vsg::vec2 jitterDelta{ 0.f, 0.f }; // jitterDeltaUv(...) with TAA, else zero
        bool reset = false; // historyReset's answer for this frame
        std::optional<std::uint32_t> frame; // TAA's frame index (RenderManager::temporalState()), else the pass counts
    };

    /// What the pass pushed for a frame, for tests.
    struct PushedState
    {
        std::uint32_t frame = 0;
        vsg::vec2 noise{ 0.f, 0.f };
        float historyWeight = 0.f; // what the shader gets as the base weight (0: no history this frame)
        bool historyUsable = false;
    };
}

namespace Render
{
    /*
     * The temporal accumulation of ambient occlusion (task 0076, docs/tasks/0076-ao-temporal-accumulation.md). Three
     * compute dispatches (files/shaders/post/aotemporal.slang) in a node that RenderManager puts after the scene and
     * velocity passes, in place of the fragment pass of Render::AmbientOcclusion: `genMain` generates the occlusion
     * with a different noise offset every frame, `resolveMain` blends it with the reprojected history, `copyMain` keeps
     * the result as the history. The tonemap reads `result()`'s first channel, as it read the fragment pass's image.
     *
     * Images (the occlusion's half size): the raw value (R16F), the result and the history (RGBA32F: value, encoded
     * depth, count, packed normal). The result is GENERAL while written and SHADER_READ_ONLY_OPTIMAL after.
     * Owner: RenderManager, main thread only. A resize calls setSource again; the old build goes to `retire`.
     */
    class AoTemporalPass
    {
    public:
        AoTemporalPass(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> shaderOptions,
            const Ao::Temporal::Settings& settings);
        ~AoTemporalPass();

        /// True if the three compute shader files are there (a build without slangc has none).
        static bool shadersAvailable(vsg::ref_ptr<const vsg::Options> shaderOptions);

        /// Builds the images, descriptors and commands for a scene of this size from the stored linear depth, the
        /// view-space normals and the velocity (each left in SHADER_READ_ONLY_OPTIMAL by its pass). The old build goes
        /// to `retire`. False (and a log line) if a shader cannot be read or compiled; the pass then does nothing.
        template <typename Retire>
        bool setSource(vsg::ref_ptr<vsg::ImageView> linearDepth, vsg::ref_ptr<vsg::ImageView> normal,
            vsg::ref_ptr<vsg::ImageView> velocity, const VkExtent2D& sceneExtent, Retire&& retire)
        {
            for (const auto& object : mLive)
                retire(object);
            mLive.clear();
            return build(linearDepth, normal, velocity, sceneExtent);
        }

        /// The node to put in the command graph; it stays the same object across resizes.
        vsg::ref_ptr<vsg::Node> node() const { return mNode; }

        /// The occlusion image the tonemap reads, valid after a successful setSource.
        vsg::ref_ptr<vsg::ImageView> result() const { return mResult; }
        VkExtent2D resultExtent() const { return mExtent; }

        /// Main thread, once a frame before the scene is recorded: picks the noise offset and writes the push
        /// constants.
        void beginFrame(const Ao::Temporal::FrameInput& input);

        const Ao::Temporal::PushedState& state() const { return mState; }
        const Ao::Temporal::Settings& settings() const { return mSettings; }

    private:
        bool build(vsg::ref_ptr<vsg::ImageView> linearDepth, vsg::ref_ptr<vsg::ImageView> normal,
            vsg::ref_ptr<vsg::ImageView> velocity, const VkExtent2D& sceneExtent);

        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<const vsg::Options> mShaderOptions;
        Ao::Temporal::Settings mSettings;
        vsg::ref_ptr<vsg::Group> mNode; // the same object across resizes: its children are replaced
        vsg::ref_ptr<vsg::Switch> mFirstSwitch;
        vsg::ref_ptr<vsg::ImageView> mResult;
        vsg::ref_ptr<vsg::vec4Value> mDepthRow;
        vsg::ref_ptr<vsg::vec4Value> mRot0;
        vsg::ref_ptr<vsg::vec4Value> mRot1;
        vsg::ref_ptr<vsg::vec4Value> mRot2;
        vsg::ref_ptr<vsg::vec4Value> mNoise;
        vsg::ref_ptr<vsg::vec4Value> mMisc;
        VkExtent2D mExtent{ 1, 1 };
        Ao::Temporal::PushedState mState;
        std::uint32_t mCounter = 0; // the frame index when TAA gives none
        std::uint32_t mFramesSinceBuild = 0;
        // Everything the last build made, given to `retire` by the next setSource.
        std::vector<vsg::ref_ptr<vsg::Object>> mLive;
    };
}

#endif
