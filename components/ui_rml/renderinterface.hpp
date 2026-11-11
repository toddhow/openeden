#ifndef OPENEDEN_COMPONENTS_UI_RML_RENDERINTERFACE_H
#define OPENEDEN_COMPONENTS_UI_RML_RENDERINTERFACE_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <vsg/core/ref_ptr.h>

#include <RmlUi/Core/RenderInterface.h>

#include "fontengine.hpp"
#include "uigeometry.hpp"

namespace vsg
{
    class CommandBuffer;
    class CompileTraversal;
    class Node;
    class Options;
    class PipelineLayout;
    class Sampler;
    class StateGroup;
    class VertexIndexDraw;
    class BindDescriptorSet;
}

namespace vsgUtil
{
    class CompileContext;
}

namespace UiRml
{
    class VfsFileInterface;

    /// RmlUi's render interface over VSG 1.1: one pipeline (premultiplied alpha, no depth), one indexed draw per
    /// compiled geometry, one descriptor set per texture, scissor and 2D transform per draw.
    ///
    /// NOT implemented (RmlUi only asks for them for some effects, which the first themes do not use, decision 0015 /
    /// note 0045 14.9): clip masks, layers, filters, shaders. Their functions keep RmlUi's defaults (no support).
    ///
    /// Use: the Overlay installs `node()` in the GUI render graph. During the recording of that node the frame callback
    /// runs: `beginFrame()`, RmlUi's Context::Update and Render (which fill the draw list through RenderGeometry), then
    /// `draw(commandBuffer)`, which compiles what is new (one transfer, waited for) and records the draws. Geometry
    /// and texture handles are shared_ptr-owned, so a handle RmlUi releases while the list still names it stays valid
    /// until the frame is drawn; the Vulkan objects go to the compile context's deletion queue (`detach`).
    ///
    /// Owner: the Overlay. Thread: everything runs on the GUI command graph's recording thread, except the
    /// constructor, `node()` and `setExtent` (main thread, never while a frame is recorded).
    class RenderInterface final : public Rml::RenderInterface, public TextureFilterHint
    {
    public:
        using FrameCallback = std::function<void(vsg::CommandBuffer&)>;

        RenderInterface(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> shaderOptions,
            const VfsFileInterface& files);
        ~RenderInterface() override;

        /// The node to add to the GUI render graph (a StateGroup that binds the pipeline, with a command that calls
        /// `callback` when recorded). Created once.
        vsg::ref_ptr<vsg::Node> node(FrameCallback callback);

        /// The size of the framebuffer drawn into (the GUI render area).
        void setExtent(std::uint32_t width, std::uint32_t height);
        std::uint32_t width() const { return mWidth.load(); }
        std::uint32_t height() const { return mHeight.load(); }

        void beginFrame();
        void draw(vsg::CommandBuffer& commandBuffer);

        /// Draw statistics of the last drawn frame (for the debug panel and the tests).
        struct Stats
        {
            std::uint32_t mDraws = 0;
            std::uint32_t mScissorChanges = 0;
            std::uint32_t mTextureBinds = 0;
            std::uint32_t mCompiledGeometries = 0;
            std::uint32_t mCompiledTextures = 0;
        };
        const Stats& lastFrame() const { return mLastFrame; }

        // TextureFilterHint
        void setNextTextureNearest(bool nearest) override { mNextNearest = nearest; }

        // Rml::RenderInterface
        Rml::CompiledGeometryHandle CompileGeometry(
            Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
        void RenderGeometry(
            Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
        Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
        void ReleaseTexture(Rml::TextureHandle texture) override;
        void EnableScissorRegion(bool enable) override;
        void SetScissorRegion(Rml::Rectanglei region) override;
        void SetTransform(const Rml::Matrix4f* transform) override;

    private:
        struct Geometry;
        struct Texture;
        struct Item
        {
            std::shared_ptr<Geometry> mGeometry;
            std::shared_ptr<Texture> mTexture;
            Rml::Vector2f mTranslation;
            bool mScissor = false;
            Rml::Rectanglei mScissorRegion;
            bool mTransform = false;
            float mMatrix[16];
        };

        std::shared_ptr<Texture> makeTexture(
            std::vector<std::uint8_t>&& rgbaPremultiplied, int width, int height, bool nearest);
        void compilePending();

        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        const VfsFileInterface& mFiles;
        vsg::ref_ptr<vsg::PipelineLayout> mPipelineLayout;
        vsg::ref_ptr<vsg::StateGroup> mState;
        vsg::ref_ptr<vsg::Sampler> mLinearSampler;
        vsg::ref_ptr<vsg::Sampler> mNearestSampler;
        vsg::ref_ptr<vsg::CompileTraversal> mCompileTraversal;

        std::atomic<std::uint32_t> mWidth{ 1 };
        std::atomic<std::uint32_t> mHeight{ 1 };
        bool mNextNearest = false;

        std::unordered_map<Rml::CompiledGeometryHandle, std::shared_ptr<Geometry>> mGeometries;
        std::unordered_map<Rml::TextureHandle, std::shared_ptr<Texture>> mTextures;
        std::shared_ptr<Texture> mWhite;
        std::vector<Item> mItems;
        std::vector<std::shared_ptr<Geometry>> mPendingGeometry;
        std::vector<std::shared_ptr<Geometry>> mJustCompiled;
        std::vector<std::shared_ptr<Texture>> mPendingTextures;

        bool mScissorEnabled = false;
        Rml::Rectanglei mScissorRegion;
        bool mTransformEnabled = false;
        float mTransform[16];

        Stats mLastFrame;
    };
}

#endif
