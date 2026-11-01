#ifndef OPENEDEN_RENDER_TONEMAP_H
#define OPENEDEN_RENDER_TONEMAP_H

#include <cstdint>

#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/core/Value.h>
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
        };

        /// The G-buffer images the debug view can show; any may be null, and the debug view then shows black. Only
        /// read when the parameters' debug view is not None.
        struct DebugSources
        {
            vsg::ref_ptr<vsg::ImageView> normal;
            vsg::ref_ptr<vsg::ImageView> linearDepth;
            vsg::ref_ptr<vsg::ImageView> velocity;
        };

        /// `compile` compiles for the window's render pass; `graph` is a render graph on that window.
        Tonemap(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<vsg::RenderGraph> graph);
        ~Tonemap();

        /// The view the tonemap pipeline is compiled for: register it with the compile manager on the window
        /// (CompileManager::add(window, view)) after Engine::setup, before setSource.
        vsg::ref_ptr<vsg::View> view() const { return mView; }

        /// Builds the draw for this source, replacing the previous one (a resize); the old one is detached through the
        /// compile context. `extent` is the swapchain's. `bloom` may be null.
        /// With a debug view in the parameters (set before this call: the shader is chosen here), `debug` supplies its
        /// images.
        bool setSource(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom, const VkExtent2D& extent,
            const DebugSources& debug = {});

        // Between frames, from the main thread: the record thread reads the value.
        void setParameters(const Parameters& parameters);
        const Parameters& parameters() const { return mParameters; }

        vsg::ref_ptr<vsg::RenderGraph> graph() const { return mGraph; }

        /// The shader's push constants: x exposure scale, y curve (0 vanilla, 1 reinhard, 2 aces), z encode (0 or 1),
        /// w bloom strength. Public for the unit tests.
        static vsg::vec4 pushConstants(const Parameters& parameters);

        /// The debug view's second push constant: x the mode (0 none, 1 normals, 2 depth, 3 velocity), y the depth
        /// range, z the velocity scale, w 0. Public for the unit tests.
        static vsg::vec4 debugPushConstants(GBuffer::DebugView view, float depthRange, float velocityScale);

    private:
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<vsg::RenderGraph> mGraph;
        vsg::ref_ptr<vsg::View> mView;
        vsg::ref_ptr<vsg::vec4Value> mPush;
        vsg::ref_ptr<vsg::ShaderStage> mVertex;
        vsg::ref_ptr<vsg::vec4Value> mPushDebug;
        // Indexed by bloom (1) and debug view (2).
        vsg::ref_ptr<vsg::ShaderStage> mFragments[4];
        vsg::ref_ptr<vsg::Node> mDraw;
        Parameters mParameters;
    };
}

#endif
