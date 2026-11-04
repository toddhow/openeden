#ifndef OPENEDEN_MWRENDER_POSTPROCESS_H
#define OPENEDEN_MWRENDER_POSTPROCESS_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <vsg/app/View.h>
#include <vsg/commands/BlitImage.h>
#include <vsg/commands/Commands.h>
#include <vsg/commands/PipelineBarrier.h>
#include <vsg/core/Array.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/ShaderStage.h>

#include <components/fx/vulkanpass.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgutil/compilecontext.hpp>

namespace Render
{
    class Engine;
}
namespace VFS
{
    class Manager;
}
namespace vsg
{
    class Context;
    class Framebuffer;
    class Options;
    class RenderPass;
    class Switch;
}

namespace MWRender
{
    /**
     * @brief Runs `.omwfx` post-processing on the finished scene (task 0012, V4p): the frame side of OpenMW's
     * MWRender::PostProcessor at 442eec6e2f, without touching the main render pass.
     *
     * After the scene's render graph, the scene's colour and depth are copied to images of its own, flipped vertically.
     * Those images, and every pass's target, then have OpenGL's bottom-left origin, so `.omwfx` code and its texture
     * coordinates work unchanged. Each pass draws a fullscreen triangle into its target; the last full-size target is
     * copied back, flipped again, before the GUI draws.
     *
     * Not ported yet (task 0012 lists them): custom vertex stages, compute passes, blending, render target formats and
     * mipmaps (targets are the swapchain's colour format), sampler files other than 2D, normals, point lights, HDR, the
     * flags that turn a technique off in interiors or underwater.
     *
     * With antialiasing on the GUI pass resolves its multisampled colour over the swapchain image, so the result is
     * also drawn into that colour at the start of the GUI pass (presentNode()).
     *
     * Owned by RenderManager; every member function runs on the main thread. With no techniques nothing is recorded.
     */
    class PostProcess
    {
    public:
        PostProcess(Render::Engine& engine, vsg::ref_ptr<vsg::Context> context,
            vsg::ref_ptr<vsgUtil::CompileContext> compile, const VFS::Manager& vfs,
            vsg::ref_ptr<const vsg::Options> textureOptions, vsg::ref_ptr<const vsg::Options> shaderOptions,
            uint32_t width, uint32_t height);
        ~PostProcess();

        vsg::ref_ptr<vsg::Group> node() const { return mRoot; }

        /// Copies the scene's depth; goes where the main pass is suspended, before the depth-sorted bins, so it is the
        /// opaque depth. Empty until a frame sets its images.
        vsg::ref_ptr<vsg::Commands> depthCopy() const { return mDepthCopy; }

        /// Goes first in the GUI pass. Empty without antialiasing.
        vsg::ref_ptr<vsg::Node> presentNode() const { return mPresentSwitch; }
        /// The view whose render pass is compatible with the GUI pass, for compiling presentNode()'s pipeline.
        void setMainView(vsg::ref_ptr<vsg::View> view) { mMainView = view; }
        /// The depth image to copy for the depth pass, when the scene is not drawn into the window's own depth (the
        /// linear HDR path, task 0025). Single-sample, left in DEPTH_STENCIL_ATTACHMENT_OPTIMAL. Call every frame: a
        /// resize replaces it.
        void setDepthSource(vsg::ref_ptr<vsg::ImageView> depth) { mDepthSource = depth; }
        /// Whether the scene renders: in menus and loading screens presentNode() must not draw an old result.
        void setSceneVisible(bool visible);

        /// The view and a framebuffer to register with the compile manager, after Render::Engine::setup, so pipelines
        /// are compiled for this render pass (as RenderManager does for shadow cascades).
        vsg::ref_ptr<vsg::View> baseView() const { return mBaseView; }
        vsg::ref_ptr<vsg::Framebuffer> registrationFramebuffer();
        vsg::ref_ptr<vsgUtil::CompileContext> compileContext() const { return mCompile; }

        /// Loads the techniques named (OpenMW's `[Post Processing] chain`) and enables them. A technique that fails to
        /// load is logged and left out. An empty result records nothing.
        void setChain(const std::vector<std::string>& names);

        /// Writes the enabled techniques to `[Post Processing] chain`, leaving out dynamic ones (OpenMW's saveChain).
        void saveChain();

        /// The Lua API's operations, as OpenMW's PostProcessor. Changing what is enabled rebuilds the passes, and
        /// refreshes the HUD if it is open. enableTechnique refuses a technique that failed to parse. loadTechnique
        /// returns the same technique for the same name, or null if it failed to load.
        std::shared_ptr<Fx::Technique> loadTechnique(const std::string& name);
        bool enableTechnique(const std::shared_ptr<Fx::Technique>& technique, std::optional<int> location);
        void disableTechnique(const std::shared_ptr<Fx::Technique>& technique);
        bool isTechniqueEnabled(const std::shared_ptr<Fx::Technique>& technique) const;

        /// The `.omwfx` files directly in the VFS `shaders/` directory, read once at startup (OpenMW's
        /// getTechniqueFiles).
        const std::set<VFS::Path::Normalized, std::less<>>& getTechniqueFiles() const { return mTechniqueFiles; }

        /// As loadTechnique, but never null: a technique that failed to load is returned with its status and error, so
        /// the HUD can show them. A stored value (shaders.yaml) replaces each uniform's default.
        std::shared_ptr<Fx::Technique> loadTechniqueFile(VFS::Path::NormalizedView file);

        /// The enabled techniques, in order.
        const std::vector<std::shared_ptr<Fx::Technique>>& getTechniques() const { return mChain; }

        /// Why this enabled technique parsed but did not run, or empty. Set by the latest rebuild.
        std::string getRenderError(const std::shared_ptr<Fx::Technique>& technique) const;

        /// A static uniform of this technique changed. They are constants in the shader, so an enabled technique's
        /// passes are rebuilt, at most once a frame, by the next update().
        void requestRebuild(const std::shared_ptr<Fx::Technique>& technique);

        /// Rebuilds everything at a new size.
        void resize(uint32_t width, uint32_t height);

        /// Points the copies at this frame's swapchain image; only valid once a frame is acquired.
        void updateImages();

        /// Fills this frame's uniform blocks.
        void update(Fx::OmwData data);

    private:
        struct Target;
        struct LoadedPass;
        struct LoadedTechnique;

        void createCopies();
        std::unique_ptr<Target> createTarget(uint32_t width, uint32_t height);
        Target& getOrCreateNamedTarget(LoadedTechnique& technique, const std::string& name);
        vsg::ref_ptr<vsg::Node> createPass(LoadedPass& pass, Target& target, vsg::ref_ptr<vsg::ImageView> lastShader,
            vsg::ref_ptr<vsg::ImageView> lastPass, LoadedTechnique& technique);
        void rebuild();
        vsg::ref_ptr<vsg::Node> createPresent(vsg::ref_ptr<vsg::ImageView> result);
        void updateHud();

        Render::Engine& mEngine;
        vsg::ref_ptr<vsg::Context> mContext;
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        const VFS::Manager& mVfs;
        vsg::ref_ptr<const vsg::Options> mTextureOptions;
        // Reads post-processing's own stages (task 0072, stage g: they were strings in postprocess.cpp).
        vsg::ref_ptr<const vsg::Options> mShaderOptions;
        uint32_t mWidth;
        uint32_t mHeight;
        vsg::ref_ptr<vsg::Group> mRoot;
        vsg::ref_ptr<vsg::Switch> mPending;
        vsg::ref_ptr<vsg::Switch> mFirstFrame;
        vsg::ref_ptr<vsg::Switch> mPresentSwitch;
        vsg::ref_ptr<vsg::View> mMainView;
        vsg::ref_ptr<vsg::ImageView> mDepthSource;
        // post/present.vert and present.frag: the Slang twins of both or neither.
        vsg::ShaderStages mPresentStages;
        bool mMultisampled = false;
        bool mSceneVisible = true;
        vsg::ref_ptr<vsg::Sampler> mLinear;
        vsg::ref_ptr<vsg::Sampler> mNearest;
        vsg::ref_ptr<vsg::RenderPass> mRenderPass;
        vsg::ref_ptr<vsg::View> mBaseView;
        // post/omwfx.vert, the vertex stage of every pass, SPIR-V either way (vsgUtil::readOmwfxVertexStage): the Slang
        // twin with `slang shaders` on, else the GLSL compiled alone. Its partner is always a pass's fragment stage
        // translated from an .omwfx file (decision 0009), compiled alone too, so VSG never links the two and the twin
        // may stand beside GLSL: the one exemption from "a pipeline takes the twins of all its stages or none".
        vsg::ref_ptr<vsg::ShaderStage> mVertex;
        vsg::ref_ptr<vsg::Image> mSceneColor;
        vsg::ref_ptr<vsg::Image> mSceneDepth;
        vsg::ref_ptr<vsg::ImageView> mSceneColorView;
        vsg::ref_ptr<vsg::ImageView> mSceneDepthView;
        vsg::ref_ptr<vsg::Commands> mCopyIn;
        vsg::ref_ptr<vsg::PipelineBarrier> mBeforeCopyIn;
        vsg::ref_ptr<vsg::PipelineBarrier> mAfterCopyIn;
        vsg::ref_ptr<vsg::Commands> mDepthCopy;
        vsg::ref_ptr<vsg::PipelineBarrier> mBeforeCopyDepth;
        vsg::ref_ptr<vsg::PipelineBarrier> mAfterCopyDepth;
        vsg::ref_ptr<vsg::BlitImage> mBlitColor;
        vsg::ref_ptr<vsg::BlitImage> mBlitDepth;
        vsg::ref_ptr<vsg::PipelineBarrier> mBeforeCopyOut;
        vsg::ref_ptr<vsg::PipelineBarrier> mAfterCopyOut;
        vsg::ref_ptr<vsg::BlitImage> mBlitOut;
        std::set<VFS::Path::Normalized, std::less<>> mTechniqueFiles;
        std::map<std::string, std::shared_ptr<Fx::Technique>, std::less<>> mLoaded; // by file, including failed ones
        std::vector<std::shared_ptr<Fx::Technique>> mChain;
        std::vector<std::unique_ptr<LoadedTechnique>> mTechniques;
        std::map<const Fx::Technique*, std::string> mRenderErrors;
        bool mRebuildRequested = false;
        int mRebuildDelay = 0;
    };

    /// Compiles Vulkan GLSL to a shader stage holding SPIR-V, the one place post-processing depends on a compiler
    /// (task 0012, V4p). On failure logs the compiler's message, also gives it in `error` if not null, and returns
    /// null.
    vsg::ref_ptr<vsg::ShaderStage> compileGlsl(
        VkShaderStageFlagBits stage, const std::string& source, std::string* error = nullptr);
}

#endif
