#ifndef OPENEDEN_RENDER_TONEMAP_H
#define OPENEDEN_RENDER_TONEMAP_H

#include <cstdint>

#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/core/Value.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/ShaderStage.h>

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
        };

        /// `compile` compiles for the window's render pass; `graph` is a render graph on that window.
        Tonemap(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<vsg::RenderGraph> graph);
        ~Tonemap();

        /// The view the tonemap pipeline is compiled for: register it with the compile manager on the window
        /// (CompileManager::add(window, view)) after Engine::setup, before setSource.
        vsg::ref_ptr<vsg::View> view() const { return mView; }

        /// Builds the draw for this source, replacing the previous one (a resize); the old one is detached through the
        /// compile context. `extent` is the swapchain's. `bloom` may be null.
        bool setSource(
            vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom, const VkExtent2D& extent);

        // Between frames, from the main thread: the record thread reads the value.
        void setParameters(const Parameters& parameters);
        const Parameters& parameters() const { return mParameters; }

        vsg::ref_ptr<vsg::RenderGraph> graph() const { return mGraph; }

        /// The shader's push constants: x exposure scale, y curve (0 vanilla, 1 reinhard, 2 aces), z encode (0 or 1),
        /// w bloom strength. Public for the unit tests.
        static vsg::vec4 pushConstants(const Parameters& parameters);

    private:
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<vsg::RenderGraph> mGraph;
        vsg::ref_ptr<vsg::View> mView;
        vsg::ref_ptr<vsg::vec4Value> mPush;
        vsg::ref_ptr<vsg::ShaderStage> mVertex;
        vsg::ref_ptr<vsg::ShaderStage> mFragment;
        vsg::ref_ptr<vsg::ShaderStage> mFragmentBloom;
        vsg::ref_ptr<vsg::Node> mDraw;
        Parameters mParameters;
    };
}

#endif
