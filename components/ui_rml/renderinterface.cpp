#include "renderinterface.hpp"

#include <algorithm>
#include <cstring>

#include <vsg/app/CompileManager.h>
#include <vsg/app/CompileTraversal.h>
#include <vsg/commands/Command.h>
#include <vsg/core/Array.h>
#include <vsg/core/Array2D.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/DynamicState.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/PipelineLayout.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/Sampler.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/state/ViewportState.h>
#include <vsg/vk/CommandBuffer.h>

#include <RmlUi/Core/Log.h>

#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/imageio.hpp>
#include <components/vsgutil/readshader.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include "fileinterface.hpp"

namespace UiRml
{
    namespace
    {
        // The shader files of this pipeline (files/shaders/gui/rml.vert and rml.frag).
        constexpr const char* sVertexShader = "gui/rml.vert";
        constexpr const char* sFragmentShader = "gui/rml.frag";

        constexpr std::size_t sExpectedItems = 4096; // draw list capacity reserved up front

        vsg::GraphicsPipelineStates createPipelineStates()
        {
            // Premultiplied alpha: the colour already holds alpha (RmlUi's ColourbPremultiplied and the font sheets).
            VkPipelineColorBlendAttachmentState blend{};
            blend.blendEnable = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
            blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT;

            auto depthStencilState = vsg::DepthStencilState::create();
            depthStencilState->depthTestEnable = false;
            depthStencilState->depthWriteEnable = false;

            auto rasterState = vsg::RasterizationState::create();
            rasterState->cullMode = VK_CULL_MODE_NONE;

            vsg::VertexInputState::Bindings bindings
                = { VkVertexInputBindingDescription{ 0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX } };
            vsg::VertexInputState::Attributes attributes = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, mX) },
                { 1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GpuVertex, mRed) },
                { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, mU) } };

            return { vsg::VertexInputState::create(bindings, attributes), vsg::InputAssemblyState::create(),
                rasterState, vsg::MultisampleState::create(),
                vsg::ColorBlendState::create(vsg::ColorBlendState::ColorBlendAttachments{ blend }), depthStencilState,
                vsg::DynamicState::create(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR) };
        }

        vsg::ref_ptr<vsg::Sampler> createSampler(bool nearest)
        {
            auto sampler = vsg::Sampler::create();
            sampler->magFilter = sampler->minFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            sampler->addressModeU = sampler->addressModeV = sampler->addressModeW
                = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler->anisotropyEnable = VK_FALSE;
            return sampler;
        }

        // The node of the render graph: calls back into the interface's owner when recorded.
        class UiCommand final : public vsg::Inherit<vsg::Command, UiCommand>
        {
        public:
            RenderInterface::FrameCallback mCallback;
            void record(vsg::CommandBuffer& commandBuffer) const override
            {
                if (mCallback)
                    mCallback(commandBuffer);
            }
        };
    }

    struct RenderInterface::Geometry
    {
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<vsg::VertexIndexDraw> mDraw;
        std::uint32_t mVertexCount = 0;
        std::uint32_t mIndexCount = 0;
        bool mCompiled = false;
        bool mDrawable = false; // compiled, and its upload is queued for this frame's transfer (task 0164)
        ~Geometry()
        {
            if (mCompiled)
                mCompile->detach(mDraw);
        }
    };

    struct RenderInterface::Texture
    {
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<vsg::BindDescriptorSet> mBind;
        int mWidth = 0;
        int mHeight = 0;
        bool mCompiled = false;
        ~Texture()
        {
            if (mCompiled)
                mCompile->detach(mBind);
        }
    };

    RenderInterface::RenderInterface(vsg::ref_ptr<vsgUtil::CompileContext> compile,
        vsg::ref_ptr<const vsg::Options> shaderOptions, const VfsFileInterface& files)
        : mCompile(std::move(compile))
        , mFiles(files)
    {
        std::fill(std::begin(mTransform), std::end(mTransform), 0.f);
        mItems.reserve(sExpectedItems);

        vsg::DescriptorSetLayoutBindings textureBindings
            = { { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } };
        mPipelineLayout = vsg::PipelineLayout::create(
            vsg::DescriptorSetLayouts{ vsg::DescriptorSetLayout::create(textureBindings) },
            vsg::PushConstantRanges{ { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants) } });

        // Task 0111 (U2): with `[Rendering] slang shaders` on, the Slang twins of the two stages (no defines), both or
        // neither, as the MyGUI render does; otherwise the GLSL, exactly as before.
        const vsg::ShaderStages shaders
            = vsgUtil::withSlangTwinsTogether(vsg::ShaderStages{ vsgUtil::readShader(sVertexShader, shaderOptions),
                                                  vsgUtil::readShader(sFragmentShader, shaderOptions) },
                { sVertexShader, sFragmentShader }, shaderOptions);
        mState = vsg::StateGroup::create();
        mState->add(vsg::BindGraphicsPipeline::create(
            vsg::GraphicsPipeline::create(mPipelineLayout, shaders, createPipelineStates())));

        mLinearSampler = createSampler(false);
        mNearestSampler = createSampler(true);
        mCompileTraversal = vsg::CompileTraversal::create(vsg::ref_ptr{ mCompile->device });

        const std::uint8_t white[4] = { 255, 255, 255, 255 };
        mWhite = makeTexture(std::vector<std::uint8_t>(white, white + 4), 1, 1, true);
    }

    RenderInterface::~RenderInterface() = default;

    vsg::ref_ptr<vsg::Node> RenderInterface::node(FrameCallback callback)
    {
        auto command = UiCommand::create();
        command->mCallback = std::move(callback);
        mState->addChild(command);
        return mState;
    }

    void RenderInterface::setExtent(std::uint32_t width, std::uint32_t height)
    {
        mWidth = std::max(1u, width);
        mHeight = std::max(1u, height);
    }

    std::shared_ptr<RenderInterface::Texture> RenderInterface::makeTexture(
        std::vector<std::uint8_t>&& rgbaPremultiplied, int width, int height, bool nearest)
    {
        auto data = vsg::ubvec4Array2D::create(
            std::uint32_t(width), std::uint32_t(height), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        std::memcpy(data->dataPointer(), rgbaPremultiplied.data(),
            std::min<std::size_t>(rgbaPremultiplied.size(), data->dataSize()));
        auto image = vsg::DescriptorImage::create(nearest ? mNearestSampler : mLinearSampler, data);
        auto texture = std::make_shared<Texture>();
        texture->mCompile = mCompile;
        texture->mWidth = width;
        texture->mHeight = height;
        texture->mBind = vsg::BindDescriptorSet::create(
            VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, vsg::Descriptors{ image });
        mPendingTextures.push_back(texture);
        return texture;
    }

    Rml::CompiledGeometryHandle RenderInterface::CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
    {
        const std::vector<GpuVertex> packed = packVertices(vertices);
        std::vector<std::uint32_t> packedIndices;
        if (packed.empty() || !packIndices(indices, packed.size(), packedIndices) || packedIndices.empty())
        {
            Rml::Log::Message(Rml::Log::LT_WARNING, "UI geometry refused (%zu vertices, %zu indices)", vertices.size(),
                indices.size());
            return 0;
        }
        auto vertexData = vsg::ubyteArray::create(std::uint32_t(packed.size() * sizeof(GpuVertex)));
        // Task 0164: "updated during the record traversal": VSG's TransferTask uploads such data after the record
        // traversal of the SAME frame and the frame's submission waits for it. With the default (static) variance the
        // upload came in the next frame's early transfer, so geometry was drawable one frame after it was compiled and a
        // text that changes every frame (RmlUi builds new geometry for each change) was never drawn.
        vertexData->properties.dataVariance = vsg::DYNAMIC_DATA_TRANSFER_AFTER_RECORD;
        std::memcpy(vertexData->dataPointer(), packed.data(), packed.size() * sizeof(GpuVertex));
        auto indexData = vsg::uintArray::create(std::uint32_t(packedIndices.size()));
        indexData->properties.dataVariance = vsg::DYNAMIC_DATA_TRANSFER_AFTER_RECORD;
        std::memcpy(indexData->dataPointer(), packedIndices.data(), packedIndices.size() * sizeof(std::uint32_t));

        auto geometry = std::make_shared<Geometry>();
        geometry->mCompile = mCompile;
        geometry->mDraw = vsg::VertexIndexDraw::create();
        geometry->mDraw->assignArrays(vsg::DataList{ vertexData });
        geometry->mDraw->assignIndices(indexData);
        geometry->mDraw->indexCount = std::uint32_t(packedIndices.size());
        geometry->mDraw->instanceCount = 1;
        geometry->mVertexCount = std::uint32_t(packed.size());
        geometry->mIndexCount = std::uint32_t(packedIndices.size());
        const auto handle = reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.get());
        mGeometries.emplace(handle, geometry);
        mPendingGeometry.push_back(geometry);
        return handle;
    }

    void RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        mGeometries.erase(geometry);
    }

    Rml::TextureHandle RenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
    {
        if (dimensions.x <= 0 || dimensions.y <= 0
            || source.size() < static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4)
            return 0;
        auto texture = makeTexture(std::vector<std::uint8_t>(source.begin(),
                                       source.begin() + std::size_t(dimensions.x) * std::size_t(dimensions.y) * 4),
            dimensions.x, dimensions.y, mNextNearest);
        const auto handle = reinterpret_cast<Rml::TextureHandle>(texture.get());
        mTextures.emplace(handle, texture);
        return handle;
    }

    Rml::TextureHandle RenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
    {
        std::vector<char> bytes;
        if (!mFiles.readAll(source, bytes))
        {
            Rml::Log::Message(Rml::Log::LT_WARNING, "UI image %s not found", source.c_str());
            return 0;
        }
        const vsg::ref_ptr<vsg::Data> data = vsgUtil::readImageFromMemory(bytes);
        if (!data || data->width() == 0 || data->height() == 0 || data->depth() != 1)
        {
            Rml::Log::Message(Rml::Log::LT_WARNING, "UI image %s could not be decoded", source.c_str());
            return 0;
        }
        const std::size_t width = data->width();
        const std::size_t height = data->height();
        std::vector<std::uint8_t> pixels(width * height * 4, 255);
        const auto* raw = static_cast<const std::uint8_t*>(data->dataPointer());
        switch (data->properties.format)
        {
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SRGB:
                std::memcpy(pixels.data(), raw, pixels.size());
                break;
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_SRGB:
                for (std::size_t i = 0; i < width * height; ++i)
                {
                    pixels[i * 4] = raw[i * 4 + 2];
                    pixels[i * 4 + 1] = raw[i * 4 + 1];
                    pixels[i * 4 + 2] = raw[i * 4];
                    pixels[i * 4 + 3] = raw[i * 4 + 3];
                }
                break;
            case VK_FORMAT_R8G8B8_UNORM:
            case VK_FORMAT_R8G8B8_SRGB:
                for (std::size_t i = 0; i < width * height; ++i)
                    std::memcpy(&pixels[i * 4], raw + i * 3, 3);
                break;
            default:
                Rml::Log::Message(Rml::Log::LT_WARNING, "UI image %s has a pixel format (%d) this slice does not read",
                    source.c_str(), int(data->properties.format));
                return 0;
        }
        for (std::size_t i = 0; i < width * height; ++i) // RmlUi draws premultiplied
            for (std::size_t c = 0; c < 3; ++c)
                pixels[i * 4 + c] = std::uint8_t((unsigned(pixels[i * 4 + c]) * pixels[i * 4 + 3] + 127u) / 255u);
        dimensions = { int(width), int(height) };
        auto texture = makeTexture(std::move(pixels), int(width), int(height), false);
        const auto handle = reinterpret_cast<Rml::TextureHandle>(texture.get());
        mTextures.emplace(handle, texture);
        return handle;
    }

    void RenderInterface::ReleaseTexture(Rml::TextureHandle texture)
    {
        mTextures.erase(texture);
    }

    void RenderInterface::EnableScissorRegion(bool enable)
    {
        mScissorEnabled = enable;
    }

    void RenderInterface::SetScissorRegion(Rml::Rectanglei region)
    {
        mScissorRegion = region;
    }

    void RenderInterface::SetTransform(const Rml::Matrix4f* transform)
    {
        mTransformEnabled = transform != nullptr;
        if (transform != nullptr)
            std::memcpy(mTransform, transform->data(), sizeof(mTransform));
    }

    void RenderInterface::RenderGeometry(
        Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
    {
        const auto geometryIt = mGeometries.find(geometry);
        if (geometryIt == mGeometries.end())
            return;
        Item item;
        item.mGeometry = geometryIt->second;
        if (texture == 0)
        {
            item.mTexture = mWhite;
        }
        else
        {
            const auto textureIt = mTextures.find(texture);
            if (textureIt == mTextures.end())
                return;
            item.mTexture = textureIt->second;
        }
        item.mTranslation = translation;
        item.mScissor = mScissorEnabled;
        item.mScissorRegion = mScissorRegion;
        item.mTransform = mTransformEnabled;
        if (mTransformEnabled)
            std::memcpy(item.mMatrix, mTransform, sizeof(mTransform));
        mItems.push_back(std::move(item));
    }

    void RenderInterface::beginFrame()
    {
        mItems.clear();
        mScissorEnabled = false;
        mTransformEnabled = false;
    }

    void RenderInterface::compilePending()
    {
        bool any = false;
        vsg::CompileResult result;
        for (const auto& geometry : mPendingGeometry)
        {
            if (geometry->mCompiled)
                continue;
            mCompileTraversal->apply(*geometry->mDraw);
            geometry->mCompiled = true;
            // This compile traversal has no transfer task of its own: onCompiled hands the buffers to the viewer's
            // TransferTask, whose late transfer (they are DYNAMIC_DATA_TRANSFER_AFTER_RECORD) runs after this frame's
            // record traversal, and the frame's submission waits for it: the draws below are valid in this frame.
            result.dynamicData.bufferInfos.push_back(geometry->mDraw->arrays.front());
            result.dynamicData.bufferInfos.push_back(geometry->mDraw->indices);
            geometry->mDrawable = true;
            ++mLastFrame.mCompiledGeometries;
            any = true;
        }
        for (const auto& texture : mPendingTextures)
        {
            if (texture->mCompiled)
                continue;
            mCompileTraversal->apply(*texture->mBind);
            texture->mCompiled = true;
            ++mLastFrame.mCompiledTextures;
            any = true;
        }
        mPendingGeometry.clear();
        mPendingTextures.clear();
        // Waits for the transfer: the draws below read what this uploads. Only frames with new content get here.
        if (any && mCompileTraversal->record())
            mCompileTraversal->waitForCompletion();
        if (!result.dynamicData.bufferInfos.empty())
            mCompile->onCompiled(result);
    }

    void RenderInterface::draw(vsg::CommandBuffer& commandBuffer)
    {
        mLastFrame = Stats{};
        compilePending(); // geometry and textures new this frame are drawable below (task 0164)

        const std::uint32_t width = mWidth.load();
        const std::uint32_t height = mHeight.load();
        const VkCommandBuffer vkCommandBuffer = commandBuffer.vk();
        const VkPipelineLayout layout = mPipelineLayout->vk(commandBuffer.deviceID);

        const VkViewport viewport{ 0.f, 0.f, float(width), float(height), 0.f, 1.f };
        vkCmdSetViewport(vkCommandBuffer, 0, 1, &viewport);
        const VkRect2D full{ { 0, 0 }, { width, height } };
        vkCmdSetScissor(vkCommandBuffer, 0, 1, &full);

        float ortho[16];
        orthographic(width, height, ortho);

        const Texture* lastTexture = nullptr;
        bool scissorOn = false;
        Rml::Rectanglei lastRegion;
        for (const Item& item : mItems)
        {
            if (!item.mGeometry->mDrawable || !item.mTexture->mCompiled)
                continue; // a failed compile: skipped, the frame goes on
            if (item.mScissor != scissorOn || (item.mScissor && !(item.mScissorRegion == lastRegion)))
            {
                VkRect2D rect = full;
                if (item.mScissor)
                {
                    const ScissorRect clamped = clampScissor(item.mScissorRegion, width, height);
                    rect.offset = { clamped.mX, clamped.mY };
                    rect.extent = { clamped.mWidth, clamped.mHeight };
                }
                vkCmdSetScissor(vkCommandBuffer, 0, 1, &rect);
                scissorOn = item.mScissor;
                lastRegion = item.mScissorRegion;
                ++mLastFrame.mScissorChanges;
            }
            PushConstants constants{};
            if (item.mTransform)
                multiply(ortho, item.mMatrix, constants.mMatrix);
            else
                std::memcpy(constants.mMatrix, ortho, sizeof(ortho));
            constants.mTranslation[0] = item.mTranslation.x;
            constants.mTranslation[1] = item.mTranslation.y;
            vkCmdPushConstants(vkCommandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(constants), &constants);
            if (item.mTexture.get() != lastTexture)
            {
                item.mTexture->mBind->record(commandBuffer);
                lastTexture = item.mTexture.get();
                ++mLastFrame.mTextureBinds;
            }
            item.mGeometry->mDraw->record(commandBuffer);
            ++mLastFrame.mDraws;
        }
        // Leave the scissor as the rest of the frame expects it.
        if (scissorOn)
            vkCmdSetScissor(vkCommandBuffer, 0, 1, &full);
    }
}
