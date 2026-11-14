#ifndef OPENEDEN_RENDER_TONEMAP_H
#define OPENEDEN_RENDER_TONEMAP_H

#include <array>
#include <cstdint>

#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/ShaderStage.h>

#include <components/render/gbuffermath.hpp>
#include <components/render/hdrmath.hpp>
#include <components/vsgutil/compilecontext.hpp>

namespace Render
{
    /*
     * Turns the float scene into the swapchain's 8-bit picture (task 0025, F2): a fullscreen triangle in a render graph
     * on the window's own render pass reads the HdrTarget's resolved colour, applies exposure and a tonemap curve and,
     * on the linear path, encodes to display values. Everything after it (the screenshot, the `.omwfx` chain, the GUI
     * pass, present) is the 8-bit pipeline as it was.
     *
     * Owner: RenderManager, which places graph() in the frame's command graph. Main thread only; setParameters may be
     * called every frame (it writes a push constant value that is read when recording).
     */
    class Tonemap
    {
    public:
        struct Parameters
        {
            Hdr::Tonemap curve = Hdr::Tonemap::Vanilla;
            float exposureStops = 0.f;
            // Encode the linear result to display values. Off before the shaders light in linear space (stage F2a).
            bool encode = false;
            // Bloom is added to the scene before the exposure: 0 means none (stage F2d).
            float bloomStrength = 0.f;
            // Task 0026: draw a stored G-buffer image in place of the scene (None: the scene). `depthRange` is the
            // distance at which the depth view reaches 63% grey; `velocityScale` multiplies velocity for display.
            GBuffer::DebugView debugView = GBuffer::DebugView::None;
            float depthRange = 1000.f;
            float velocityScale = 200.f;
            // Task 0029: how much of the occlusion applies (pow(ao, strength)); only with ambient occlusion on.
            float aoStrength = 1.f;
            // Task 0033: subsurface scattering of skin. `sssStrength` 0 means none; the width is in game units and the
            // focal length (the projection's, in pixels of the target) turns it into a size on the screen. Only with
            // the step on (SssSources given).
            float sssStrength = 1.f;
            float sssWidth = 1.5f;
            float sssFocalPixels = 0.f;
            // Task 0127 stage W-S4: the image space grade of a game's weather. `gradeShader` puts the GRADE define in the
            // fragment stage (set before setSource, like the debug view); `gradeOn` says the values apply (off: the
            // shader's grade switch is 0 and the picture is what it would be without the define).
            bool gradeShader = false;
            bool gradeOn = false;
            float gradeSaturation = 1.f;
            float gradeContrast = 1.f;
            float gradeTintAmount = 0.f;
            std::array<float, 3> gradeTint{ 1.f, 1.f, 1.f };
        };

        /// Task 0033: what the subsurface scattering step needs: the material output (its alpha marks skin), and the
        /// stored linear depth and normals that keep the blur on one surface. All three, or none.
        struct SssSources
        {
            vsg::ref_ptr<vsg::ImageView> material;
            vsg::ref_ptr<vsg::ImageView> linearDepth;
            vsg::ref_ptr<vsg::ImageView> normal;
            bool valid() const { return material && linearDepth && normal; }
        };

        /// Task 0029: what the ambient occlusion step needs: the ambient part of the scene colour, the half
        /// resolution occlusion, and the stored normals and linear depth the filter compares. All four, or none.
        struct AoSources
        {
            vsg::ref_ptr<vsg::ImageView> ambient;
            vsg::ref_ptr<vsg::ImageView> ao;
            vsg::ref_ptr<vsg::ImageView> normal;
            vsg::ref_ptr<vsg::ImageView> linearDepth;
            VkExtent2D aoExtent{};
            bool valid() const { return ambient && ao && normal && linearDepth; }
        };

        /// The G-buffer images the debug view can show; any may be null, and the debug view then shows black. Only
        /// read when the parameters' debug view is not None.
        struct DebugSources
        {
            vsg::ref_ptr<vsg::ImageView> normal;
            vsg::ref_ptr<vsg::ImageView> linearDepth;
            vsg::ref_ptr<vsg::ImageView> velocity;
            vsg::ref_ptr<vsg::ImageView> material; // task 0027
        };

        /// `compile` compiles for the window's render pass; `graph` is a render graph on that window. `shaderOptions`
        /// read the shader files (post/fullscreen.vert, post/tonemap.frag; task 0072, stage g).
        Tonemap(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<vsg::RenderGraph> graph,
            vsg::ref_ptr<const vsg::Options> shaderOptions);
        ~Tonemap();

        /// The view the tonemap pipeline is compiled for: register it with the compile manager on the window
        /// (CompileManager::add(window, view)) after Engine::setup, before setSource.
        vsg::ref_ptr<vsg::View> view() const { return mView; }

        /// Builds the draw for this source, replacing the previous one (a resize); the old one is detached through the
        /// compile context. `extent` is the swapchain's. `bloom` may be null.
        /// With a debug view in the parameters (set before this call: the shader is chosen here), `debug` supplies its
        /// images.
        bool setSource(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom, const VkExtent2D& extent,
            const DebugSources& debug, const AoSources& aoSources, const SssSources& sssSources);
        bool setSource(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom, const VkExtent2D& extent,
            const DebugSources& debug, const AoSources& aoSources);
        bool setSource(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom, const VkExtent2D& extent,
            const DebugSources& debug = {});

        // Between frames, from the main thread: the record thread reads the value.
        void setParameters(const Parameters& parameters);
        const Parameters& parameters() const { return mParameters; }

        vsg::ref_ptr<vsg::RenderGraph> graph() const { return mGraph; }

        /// The shader's push constants: x exposure scale, y curve (0 vanilla, 1 reinhard, 2 aces), z encode (0 or 1),
        /// w bloom strength. Public for the unit tests.
        static vsg::vec4 pushConstants(const Parameters& parameters);

        /// The debug view's second push constant: x the mode (0 none, 1 normals, 2 depth, 3 velocity, 4 roughness, 5
        /// material, 6 subsurface), y the depth range, z the velocity scale, w 0. Public for the unit tests.
        static vsg::vec4 debugPushConstants(GBuffer::DebugView view, float depthRange, float velocityScale);

        /// The ambient occlusion's push constants: the size of one texel of the half resolution occlusion image, and
        /// the strength. Public for the unit tests.
        static vsg::vec4 aoPushConstants(const VkExtent2D& aoExtent, float strength);

        /// The subsurface scattering's push constants: x strength, y width in game units, z the focal length in
        /// pixels, w 0. Public for the unit tests.
        static vsg::vec4 sssPushConstants(float strength, float width, float focalPixels);

        /// The grade's two push constants (after the debug view's): x saturation, y contrast, z tint amount, w 1 when the
        /// grade applies (else 0); and the tint colour in xyz. Public for the unit tests.
        static vsg::vec4 gradePushConstants(const Parameters& parameters);
        static vsg::vec4 gradeTintPushConstants(const Parameters& parameters);

    private:
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<vsg::RenderGraph> mGraph;
        vsg::ref_ptr<const vsg::Options> mShaderOptions;
        vsg::ref_ptr<vsg::View> mView;
        vsg::ref_ptr<vsg::vec4Value> mPush;
        vsg::ref_ptr<vsg::vec4Value> mPushDebug;
        vsg::ref_ptr<vsg::vec4Value> mPushAo;
        vsg::ref_ptr<vsg::vec4Value> mPushSss;
        vsg::ref_ptr<vsg::vec4Value> mPushGrade;
        vsg::ref_ptr<vsg::vec4Value> mPushGradeTint;
        VkExtent2D mAoExtent{ 1, 1 };
        // The vertex and fragment stage of each variant, indexed by bloom (1), debug view (2), ambient occlusion (4),
        // subsurface scattering (8) and the image space grade (16); a variant takes the Slang twins of both stages or
        // neither.
        vsg::ShaderStages mStages[32];
        vsg::ref_ptr<vsg::Node> mDraw;
        Parameters mParameters;
    };
}

#endif
