#include "postprocess.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <variant>

#include <vsg/app/Camera.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/commands/ClearAttachments.h>
#include <vsg/commands/ClearImage.h>
#include <vsg/commands/Draw.h>
#include <vsg/io/Logger.h>
#include <vsg/nodes/Switch.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DynamicState.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/state/ViewportState.h>
#include <vsg/utils/ShaderCompiler.h>
#include <vsg/vk/Context.h>
#include <vsg/vk/Framebuffer.h>
#include <vsg/vk/RenderPass.h>

#include <components/debug/debuglog.hpp>
#include <components/fx/technique.hpp>
#include <components/fx/uniformsettings.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/render/attachmentformat.hpp>
#include <components/render/engine.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/compilermessage.hpp>
#include <components/vsgutil/image.hpp>
#include <components/vsgutil/passstages.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/readshader.hpp>
#include <components/vsgutil/recordwithviewid.hpp>
#include <components/vsgutil/setviewportstate.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwgui/postprocessorhud.hpp"

#include "mask.hpp"

namespace MWRender
{
    namespace
    {
        vsg::ref_ptr<vsg::RenderPass> createColorRenderPass(vsg::Device* device, VkFormat format)
        {
            auto attachment = vsg::defaultColorAttachment(format);
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            vsg::AttachmentReference reference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            vsg::SubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachments.emplace_back(reference);

            // Reads of an earlier pass's target finish before this pass writes; this target is read after it.
            const vsg::RenderPass::Dependencies dependencies{
                { VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0 },
                { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, 0 }
            };
            return vsg::RenderPass::create(device, vsg::RenderPass::Attachments{ attachment },
                vsg::RenderPass::Subpasses{ subpass }, dependencies);
        }

        vsg::ref_ptr<vsg::Sampler> createSampler(VkFilter filter)
        {
            auto sampler = vsg::Sampler::create();
            sampler->minFilter = sampler->magFilter = filter;
            sampler->addressModeU = sampler->addressModeV = sampler->addressModeW
                = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            return sampler;
        }

        VkFilter toVulkan(Fx::Types::FilterMode mode)
        {
            return mode == Fx::Types::FilterMode::Nearest || mode == Fx::Types::FilterMode::NearestMipmapNearest
                    || mode == Fx::Types::FilterMode::NearestMipmapLinear
                ? VK_FILTER_NEAREST
                : VK_FILTER_LINEAR;
        }

        VkSamplerAddressMode toVulkan(Fx::Types::WrapMode mode)
        {
            switch (mode)
            {
                case Fx::Types::WrapMode::Repeat:
                    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                case Fx::Types::WrapMode::Mirror:
                    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                case Fx::Types::WrapMode::ClampToBorder:
                    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
                default:
                    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            }
        }

        // Mipmaps are not generated (see the class comment), so a mipmap filter uses its base filter.
        template <class Description>
        vsg::ref_ptr<vsg::Sampler> createSampler(const Description& description)
        {
            auto sampler = vsg::Sampler::create();
            sampler->minFilter = toVulkan(description.mMinFilter);
            sampler->magFilter = toVulkan(description.mMagFilter);
            sampler->addressModeU = toVulkan(description.mWrapS);
            sampler->addressModeV = toVulkan(description.mWrapT);
            sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            return sampler;
        }

        VkImageBlit flippedRegion(VkImageAspectFlags aspect, int32_t width, int32_t height)
        {
            VkImageBlit region{};
            region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = aspect;
            region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
            region.srcOffsets[0] = VkOffset3D{ 0, 0, 0 };
            region.srcOffsets[1] = VkOffset3D{ width, height, 1 };
            region.dstOffsets[0] = VkOffset3D{ 0, height, 0 };
            region.dstOffsets[1] = VkOffset3D{ width, 0, 1 };
            return region;
        }

        vsg::ref_ptr<vsg::ImageMemoryBarrier> barrier(VkAccessFlags srcAccess, VkAccessFlags dstAccess,
            VkImageLayout from, VkImageLayout to, vsg::ref_ptr<vsg::Image> image, VkImageAspectFlags aspect)
        {
            return vsg::ImageMemoryBarrier::create(srcAccess, dstAccess, from, to, VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED, image, VkImageSubresourceRange{ aspect, 0, 1, 0, 1 });
        }

        // Copies one uniform's current value into a pass's uniform block; bools as 32-bit integers (std140).
        void writeUniform(const Fx::Types::UniformBase& uniform, char* destination)
        {
            std::visit(
                [&](const auto& typed) {
                    using T = typename std::decay_t<decltype(typed)>::value_type;
                    const auto writeOne = [](const T& value, char* out) {
                        if constexpr (std::is_same_v<T, bool>)
                        {
                            const std::int32_t asInt = value ? 1 : 0;
                            std::memcpy(out, &asInt, sizeof(asInt));
                        }
                        else
                            std::memcpy(out, &value, sizeof(T));
                    };
                    if (typed.isArray())
                    {
                        std::size_t i = 0;
                        for (const T& value : typed.getArray())
                            writeOne(value, destination + 16 * i++);
                    }
                    else
                        writeOne(typed.getValue(), destination);
                },
                uniform.mData);
        }

        // Frames with no further edit before an edited static uniform recompiles: dragging a slider would otherwise run
        // glslang and rebuild every target each frame, and reset shaders that read last frame's targets.
        constexpr int sRebuildDelayFrames = 15;
    }

    vsg::ref_ptr<vsg::ShaderStage> compileGlsl(
        VkShaderStageFlagBits stage, const std::string& source, std::string* error)
    {
        auto shaderStage = vsg::ShaderStage::create(stage, "main", source);
        auto compiler = vsg::ShaderCompiler::create();

        // vsg::ShaderCompiler reports glslang's message only through vsg::Logger.
        std::string message;
        bool compiled = false;
        {
            const vsgUtil::CompilerMessageCapture capture(message);
            compiled = compiler->compile(shaderStage);
        }

        if (!compiled)
        {
            Log(Debug::Error) << "Failed to compile a post-processing shader: " << message;
            if (error)
                *error = message;
            return {};
        }
        return shaderStage;
    }

    // A pass's colour target.
    struct PostProcess::Target
    {
        vsg::ref_ptr<vsg::Image> image;
        vsg::ref_ptr<vsg::ImageView> view;
        vsg::ref_ptr<vsg::Framebuffer> framebuffer;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct PostProcess::LoadedPass
    {
        Fx::VulkanPass translated;
        std::string target;
        vsg::ref_ptr<vsg::ShaderStage> fragment;
        vsg::ref_ptr<vsg::ubyteArray> uniforms;
    };

    struct PostProcess::LoadedTechnique
    {
        std::shared_ptr<Fx::Technique> technique;
        std::vector<LoadedPass> passes;
        std::map<std::string, std::unique_ptr<Target>> namedTargets;
        std::vector<std::unique_ptr<Target>> targets;
    };

    PostProcess::PostProcess(Render::Engine& engine, vsg::ref_ptr<vsg::Context> context,
        vsg::ref_ptr<vsgUtil::CompileContext> compile, const VFS::Manager& vfs,
        vsg::ref_ptr<const vsg::Options> textureOptions, vsg::ref_ptr<const vsg::Options> shaderOptions, uint32_t width,
        uint32_t height)
        : mEngine(engine)
        , mContext(context)
        , mCompile(compile->clone(Mask_PostProcess))
        , mVfs(vfs)
        , mTextureOptions(textureOptions)
        , mShaderOptions(std::move(shaderOptions))
        , mWidth(width)
        , mHeight(height)
        , mRoot(vsg::Group::create())
        , mLinear(createSampler(VK_FILTER_LINEAR))
        , mNearest(createSampler(VK_FILTER_NEAREST))
        , mRenderPass(createColorRenderPass(mCompile->device, Render::compatibleColorFormat))
        , mBaseView(vsg::View::create())
    {
        mBaseView->viewDependentState = nullptr;
        mBaseView->mask = Mask_PostProcess;
        // Task 0072, stage h: the passes' vertex stage is the Slang twin with the switch on (see mVertex).
        {
            std::string error;
            mVertex = vsgUtil::readOmwfxVertexStage(mShaderOptions, &error);
            if (!mVertex)
                Log(Debug::Error) << "Post-processing has no vertex stage: " << error;
        }
        mMultisampled = mEngine.framebufferSamples() != VK_SAMPLE_COUNT_1_BIT;
        mPresentSwitch = vsg::Switch::create();
        if (mMultisampled)
        {
            std::string error;
            mPresentStages = vsgUtil::readPassStages(
                { { "post/present.vert", {} }, { "post/present.frag", {} } }, mShaderOptions, &error);
            if (mPresentStages.empty())
                Log(Debug::Error) << "Failed to compile the post-processing present shader: " << error;
        }
        createCopies();
        for (const auto& path : mVfs.getRecursiveDirectoryIterator(Fx::Technique::sSubdir))
            if (path.parent().parent().empty() && Misc::getFileExtension(path) == Fx::Technique::sExt)
                mTechniqueFiles.emplace(path);
    }

    PostProcess::~PostProcess() {}

    vsg::ref_ptr<vsg::Framebuffer> PostProcess::registrationFramebuffer()
    {
        return createTarget(1, 1)->framebuffer;
    }

    std::unique_ptr<PostProcess::Target> PostProcess::createTarget(uint32_t width, uint32_t height)
    {
        auto target = std::make_unique<Target>();
        target->width = width;
        target->height = height;
        target->image = vsgUtil::createImage(Render::compatibleColorFormat, VkExtent2D{ width, height },
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        target->image->compile(*mContext);
        target->view = vsg::ImageView::create(target->image);
        target->view->compile(mCompile->device);
        target->framebuffer = vsg::Framebuffer::create(mRenderPass, vsg::ImageViews{ target->view }, width, height, 1);
        return target;
    }

    PostProcess::Target& PostProcess::getOrCreateNamedTarget(LoadedTechnique& technique, const std::string& name)
    {
        auto found = technique.namedTargets.find(name);
        if (found != technique.namedTargets.end())
            return *found->second;
        const auto& map = technique.technique->getRenderTargetsMap();
        const auto description = map.find(name);
        uint32_t width = mWidth;
        uint32_t height = mHeight;
        if (description != map.end())
        {
            const auto [w, h] = description->second.mSize.get(static_cast<int>(mWidth), static_cast<int>(mHeight));
            width = static_cast<uint32_t>(std::max(1, w));
            height = static_cast<uint32_t>(std::max(1, h));
        }
        return *technique.namedTargets.emplace(name, createTarget(width, height)).first->second;
    }

    void PostProcess::createCopies()
    {
        const VkExtent2D extent{ mWidth, mHeight };
        constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        mSceneColor = vsgUtil::createImage(Render::compatibleColorFormat, extent, usage);
        mSceneDepth = vsgUtil::createImage(Render::compatibleDepthFormat, extent, usage);
        mSceneColor->compile(*mContext);
        mSceneDepth->compile(*mContext);
        mSceneColorView = vsg::ImageView::create(mSceneColor);
        mSceneDepthView = vsg::ImageView::create(mSceneDepth, VK_IMAGE_ASPECT_DEPTH_BIT);
        mSceneColorView->compile(mCompile->device);
        mSceneDepthView->compile(mCompile->device);

        const auto present = mEngine.getPresentLayout();
        const auto depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        constexpr auto color = VK_IMAGE_ASPECT_COLOR_BIT;
        constexpr auto depth = VK_IMAGE_ASPECT_DEPTH_BIT;
        constexpr auto src = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        constexpr auto dst = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        constexpr auto read = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // The swapchain and depth images are this frame's; update() sets them.
        mBeforeCopyIn = vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            barrier(VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, dst,
                mSceneColor, color),
            barrier(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, present, src, {}, color));
        mAfterCopyIn
            = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                barrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, dst, read, mSceneColor, color));

        // The depth attachment is not stored past the main pass, so depth is copied inside it (depthCopy()).
        mBeforeCopyDepth = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // the stage of a depth resolve
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            barrier(VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, dst,
                mSceneDepth, depth),
            barrier(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, depthLayout, src, {}, depth));
        mAfterCopyDepth = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
            barrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, dst, read, mSceneDepth, depth),
            barrier(VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, src,
                depthLayout, {}, depth));

        const auto w = static_cast<int32_t>(mWidth);
        const auto h = static_cast<int32_t>(mHeight);
        mBlitColor = vsg::BlitImage::create();
        mBlitColor->dstImage = mSceneColor;
        mBlitColor->srcImageLayout = src;
        mBlitColor->dstImageLayout = dst;
        mBlitColor->filter = VK_FILTER_NEAREST;
        mBlitColor->regions = { flippedRegion(color, w, h) };
        mBlitDepth = vsg::BlitImage::create();
        mBlitDepth->dstImage = mSceneDepth;
        mBlitDepth->srcImageLayout = src;
        mBlitDepth->dstImageLayout = dst;
        mBlitDepth->filter = VK_FILTER_NEAREST;
        mBlitDepth->regions = { flippedRegion(depth, w, h) };

        mCopyIn = vsg::Commands::create();
        if (!mDepthCopy)
            mDepthCopy = vsg::Commands::create();
        mCopyIn->children = { mBeforeCopyIn, mBlitColor, mAfterCopyIn };

        mBlitOut = vsg::BlitImage::create();
        mBlitOut->srcImageLayout = src;
        mBlitOut->dstImageLayout = dst;
        mBlitOut->filter = VK_FILTER_NEAREST;
        mBlitOut->regions = { flippedRegion(color, w, h) };
    }

    vsg::ref_ptr<vsg::Node> PostProcess::createPass(LoadedPass& pass, Target& target,
        vsg::ref_ptr<vsg::ImageView> lastShader, vsg::ref_ptr<vsg::ImageView> lastPass, LoadedTechnique& technique)
    {
        if (!mVertex)
            return {};
        const auto read = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const auto imageType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        const auto makeImage = [&](vsg::ref_ptr<vsg::Sampler> sampler, vsg::ref_ptr<vsg::ImageView> view,
                                   uint32_t binding) -> vsg::ref_ptr<vsg::Descriptor> {
            return vsg::DescriptorImage::create(vsg::ImageInfo::create(sampler, view, read), binding, 0, imageType);
        };
        vsg::Descriptors descriptors{
            makeImage(mLinear, lastShader, Fx::VulkanBinding_LastShader),
            makeImage(mLinear, lastPass, Fx::VulkanBinding_LastPass),
            makeImage(mNearest, mSceneDepthView, Fx::VulkanBinding_Depth),
            vsg::DescriptorBuffer::create(
                pass.uniforms, Fx::VulkanBinding_Uniforms, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
            // Normals and distortion are not rendered yet; these read the scene's colour.
            makeImage(mLinear, mSceneColorView, Fx::VulkanBinding_Normals),
            makeImage(mLinear, mSceneColorView, Fx::VulkanBinding_Distortion),
        };
        vsg::DescriptorSetLayoutBindings bindings{
            { Fx::VulkanBinding_LastShader, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Fx::VulkanBinding_LastPass, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Fx::VulkanBinding_Depth, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Fx::VulkanBinding_Uniforms, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Fx::VulkanBinding_Normals, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Fx::VulkanBinding_Distortion, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        };

        uint32_t binding = Fx::VulkanBinding_FirstSampler;
        for (const std::string& name : pass.translated.samplers)
        {
            bindings.push_back({ binding, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
            const auto renderTarget = technique.technique->getRenderTargetsMap().find(name);
            if (renderTarget != technique.technique->getRenderTargetsMap().end())
                descriptors.push_back(makeImage(
                    createSampler(renderTarget->second), getOrCreateNamedTarget(technique, name).view, binding));
            else
            {
                vsg::ref_ptr<vsg::Data> image;
                vsg::ref_ptr<vsg::Sampler> imageSampler;
                for (const auto& sampler : technique.technique->getSamplers())
                    if (sampler.mName == name)
                    {
                        image = vsgUtil::readOptionalImage(std::string(sampler.mSource.value()), mTextureOptions);
                        imageSampler = createSampler(sampler);
                    }
                if (image)
                    descriptors.push_back(vsg::DescriptorImage::create(imageSampler, image, binding, 0, imageType));
                else
                {
                    Log(Debug::Warning) << "Post-processing sampler '" << name << "' has no image";
                    descriptors.push_back(makeImage(mLinear, mSceneColorView, binding));
                }
            }
            ++binding;
        }
        auto setLayout = vsg::DescriptorSetLayout::create(bindings);
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout }, vsg::PushConstantRanges{});

        auto colorBlend
            = vsg::ColorBlendState::create(vsg::ColorBlendState::ColorBlendAttachments{ { VK_FALSE, VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT } });
        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_FALSE;
        depthStencil->depthWriteEnable = VK_FALSE;
        auto raster = vsg::RasterizationState::create();
        raster->cullMode = VK_CULL_MODE_NONE;
        const vsg::GraphicsPipelineStates states{ vsg::VertexInputState::create(), vsg::InputAssemblyState::create(),
            raster, vsg::MultisampleState::create(), colorBlend, depthStencil,
            vsg::ViewportState::create(0, 0, target.width, target.height) };

        auto pipeline = vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{ mVertex, pass.fragment }, states);

        auto view = vsg::View::create(*mBaseView); // the same view ID, which pipelines are compiled for
        view->camera = vsg::Camera::create(vsg::Orthographic::create(), vsg::LookAt::create(),
            vsg::ViewportState::create(0, 0, target.width, target.height));
        view->viewDependentState = nullptr;
        view->mask = Mask_PostProcess;
        auto commands = vsg::Commands::create();
        commands->children = { vsg::BindGraphicsPipeline::create(pipeline),
            vsg::BindDescriptorSet::create(
                VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, vsg::DescriptorSet::create(setLayout, descriptors)),
            vsg::Draw::create(3, 1, 0, 0) };
        // Compiled on their own, as scene objects are: registered views select the render pass and view ID.
        if (!mCompile->compile(commands))
            return {};
        view->addChild(commands);

        auto graph = vsg::RenderGraph::create();
        graph->framebuffer = target.framebuffer;
        graph->renderArea.offset = {};
        graph->renderArea.extent = { target.width, target.height };
        graph->children = { view };
        return graph;
    }

    std::shared_ptr<Fx::Technique> PostProcess::loadTechnique(const std::string& name)
    {
        const VFS::Path::Normalized file = Fx::Technique::makeFileName(name);
        auto technique = loadTechniqueFile(file);
        return technique->isValid() ? technique : nullptr;
    }

    std::shared_ptr<Fx::Technique> PostProcess::loadTechniqueFile(VFS::Path::NormalizedView file)
    {
        const auto found = mLoaded.find(file.value());
        if (found != mLoaded.end())
            return found->second;

        // The file's own spelling names it, as in OpenMW's PostProcessor::loadTechnique: uniforms are stored under it.
        auto technique = std::make_shared<Fx::Technique>(
            mVfs, file, mTechniqueFiles.contains(file) ? mVfs.getStem(file) : std::string(file.stem()), true, false);
        if (technique->compile())
        {
            for (const auto& uniform : technique->getUniformMap())
                Fx::applyStoredValue(*uniform);
        }
        else
            Log(Debug::Error) << "Failed to load post-processing shader '" << file
                              << "': " << technique->getLastError();
        mLoaded.emplace(std::string(file.value()), technique);
        return technique;
    }

    void PostProcess::saveChain()
    {
        std::vector<std::string> chain;
        for (const auto& technique : mChain)
            if (!technique->getDynamic() && !technique->getInternal())
                chain.push_back(technique->getName());
        Settings::postProcessing().mChain.set(chain);
    }

    std::string PostProcess::getRenderError(const std::shared_ptr<Fx::Technique>& technique) const
    {
        const auto found = mRenderErrors.find(technique.get());
        return found != mRenderErrors.end() ? found->second : std::string();
    }

    void PostProcess::requestRebuild(const std::shared_ptr<Fx::Technique>& technique)
    {
        if (isTechniqueEnabled(technique))
        {
            mRebuildRequested = true;
            mRebuildDelay = sRebuildDelayFrames;
        }
    }

    void PostProcess::updateHud()
    {
        if (auto* hud = MWBase::Environment::get().getWindowManager()->getPostProcessorHud())
            hud->updateTechniques();
    }

    void PostProcess::setSceneVisible(bool visible)
    {
        mSceneVisible = visible;
        if (mPresentSwitch)
            mPresentSwitch->setAllChildren(visible);
    }

    vsg::ref_ptr<vsg::Node> PostProcess::createPresent(vsg::ref_ptr<vsg::ImageView> result)
    {
        if (mPresentStages.empty())
            return {};
        auto setLayout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } });
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout }, vsg::PushConstantRanges{});
        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_FALSE;
        depthStencil->depthWriteEnable = VK_FALSE;
        auto raster = vsg::RasterizationState::create();
        raster->cullMode = VK_CULL_MODE_NONE;
        auto colorBlend
            = vsg::ColorBlendState::create(vsg::ColorBlendState::ColorBlendAttachments{ { VK_FALSE, VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT } });
        const vsg::GraphicsPipelineStates states{ vsg::VertexInputState::create(), vsg::InputAssemblyState::create(),
            raster, vsg::MultisampleState::create(), colorBlend, depthStencil,
            vsg::ViewportState::create(0, 0, mWidth, mHeight) };
        auto pipeline
            = vsg::BindGraphicsPipeline::create(vsg::GraphicsPipeline::create(layout, mPresentStages, states));

        auto descriptor = vsg::DescriptorImage::create(
            vsg::ImageInfo::create(mNearest, result, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), 0, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        auto commands = vsg::Commands::create();
        // The main view's pipeline states make the viewport dynamic, and MyGUI sets its own only after this draw.
        commands->children
            = { vsgUtil::SetViewportState::create(vsg::ViewportState::create(0, 0, mWidth, mHeight)), pipeline,
                  vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                      vsg::DescriptorSet::create(setLayout, vsg::Descriptors{ descriptor })),
                  vsg::Draw::create(3, 1, 0, 0) };

        // The GUI pass's render pass is compatible with the main view's, so the pipeline is compiled for that view.
        auto presentCompile = mCompile->clone(Mask_PostProcess);
        const vsg::ref_ptr<vsg::View> mainView = mMainView;
        uint32_t viewId = 0;
        presentCompile->contextSelectionFunction = [mainView, &viewId](vsg::Context& context) {
            if (context.view.get() != mainView.get())
                return false;
            viewId = context.viewID;
            return true;
        };
        if (!presentCompile->compile(commands))
            return {};
        return vsgUtil::RecordWithViewId::create(commands, viewId);
    }

    void PostProcess::setChain(const std::vector<std::string>& names)
    {
        mChain.clear();
        for (const std::string& name : names)
            if (auto technique = loadTechnique(name))
                mChain.push_back(std::move(technique));
        rebuild();
    }

    bool PostProcess::enableTechnique(const std::shared_ptr<Fx::Technique>& technique, std::optional<int> location)
    {
        if (!technique || !technique->isValid() || (location && *location < 0))
            return false;
        std::erase(mChain, technique);
        const std::size_t position = std::min<std::size_t>(location.value_or(mChain.size()), mChain.size());
        mChain.insert(mChain.begin() + static_cast<std::ptrdiff_t>(position), technique);
        rebuild();
        updateHud();
        return true;
    }

    void PostProcess::disableTechnique(const std::shared_ptr<Fx::Technique>& technique)
    {
        if (std::erase(mChain, technique) > 0)
        {
            rebuild();
            updateHud();
        }
    }

    bool PostProcess::isTechniqueEnabled(const std::shared_ptr<Fx::Technique>& technique) const
    {
        return std::find(mChain.begin(), mChain.end(), technique) != mChain.end();
    }

    void PostProcess::resize(uint32_t width, uint32_t height)
    {
        if (width == mWidth && height == mHeight)
            return;
        mWidth = width;
        mHeight = height;
        createCopies();
        rebuild();
    }

    void PostProcess::rebuild()
    {
        for (const auto& child : mRoot->children)
            mCompile->detach(child);
        mRoot->children.clear();
        mTechniques.clear();
        mRenderErrors.clear();
        mRebuildRequested = false;
        for (const auto& child : mPresentSwitch->children)
            mCompile->detach(child.node);
        mPresentSwitch->children.clear();
        mPending = {};

        const Fx::VulkanPassOptions options{ Settings::fog().mRadialFog, Settings::fog().mExponentialFog };
        auto group = vsg::Group::create();
        auto firstFrame = vsg::Group::create();
        mFirstFrame = vsg::Switch::create();
        mFirstFrame->addChild(true, firstFrame);
        group->addChild(mFirstFrame);
        group->addChild(mCopyIn);
        vsg::ref_ptr<vsg::ImageView> lastShader = mSceneColorView;
        vsg::ref_ptr<vsg::ImageView> lastPass = mSceneColorView;
        vsg::ref_ptr<vsg::Image> lastImage;
        vsg::ref_ptr<vsg::ImageView> lastImageView;

        for (const std::shared_ptr<Fx::Technique>& technique : mChain)
        {
            const std::string name = technique->getName();
            auto loaded = std::make_unique<LoadedTechnique>();
            loaded->technique = technique;

            bool valid = true;
            for (const auto& description : loaded->technique->getPasses())
            {
                LoadedPass pass;
                pass.target = description->getTarget();
                valid = description->mType == Fx::Types::PassDesc::Type::Pixel
                    && Fx::translatePass(*loaded->technique, *description, options, pass.translated);
                std::string compileError;
                if (valid)
                    pass.fragment = compileGlsl(VK_SHADER_STAGE_FRAGMENT_BIT, pass.translated.fragment, &compileError);
                if (!valid || !pass.fragment)
                {
                    Log(Debug::Error) << "Post-processing shader '" << name << "': pass '" << description->getName()
                                      << "' is not supported";
                    if (description->mType == Fx::Types::PassDesc::Type::Compute)
                        compileError = "Compute passes are not supported.";
                    else if (!valid)
                        compileError = "The pass has no fragment shader.";
                    mRenderErrors[technique.get()] = "Pass '" + description->getName() + "': " + compileError;
                    valid = false;
                    break;
                }
                pass.uniforms = vsg::ubyteArray::create(static_cast<uint32_t>(pass.translated.uniformBytes));
                pass.uniforms->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
                loaded->passes.push_back(std::move(pass));
            }
            if (!valid)
                continue;

            for (LoadedPass& pass : loaded->passes)
            {
                Target* target;
                if (!pass.target.empty())
                    target = &getOrCreateNamedTarget(*loaded, pass.target);
                else
                {
                    loaded->targets.push_back(createTarget(mWidth, mHeight));
                    target = loaded->targets.back().get();
                }
                auto node = createPass(pass, *target, lastShader, lastPass, *loaded);
                if (!node)
                {
                    Log(Debug::Error) << "Failed to compile post-processing shader '" << name << "'";
                    mRenderErrors[technique.get()] = "The pipeline failed to compile; see the log.";
                    continue;
                }
                group->addChild(node);
                if (pass.target.empty())
                {
                    lastImage = target->image;
                    lastImageView = target->view;
                }
                lastPass = target->view;
            }
            if (!loaded->targets.empty())
                lastShader = loaded->targets.back()->view;
            for (const auto& [targetName, target] : loaded->namedTargets)
            {
                // A pass may read a target before any pass has written it, so each starts cleared. Once only: shaders
                // may read what they wrote last frame.
                VkClearColorValue clearColor{};
                const auto description = loaded->technique->getRenderTargetsMap().find(targetName);
                if (description != loaded->technique->getRenderTargetsMap().end())
                    clearColor
                        = VkClearColorValue{ { description->second.mClearColor.r, description->second.mClearColor.g,
                            description->second.mClearColor.b, description->second.mClearColor.a } };
                auto clear = vsg::ClearColorImage::create();
                clear->image = target->image;
                clear->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                clear->color = clearColor;
                clear->ranges = { VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
                firstFrame->addChild(
                    vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                        barrier(0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, target->image, VK_IMAGE_ASPECT_COLOR_BIT)));
                firstFrame->addChild(clear);
                firstFrame->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                    barrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, target->image,
                        VK_IMAGE_ASPECT_COLOR_BIT)));
            }
            mTechniques.push_back(std::move(loaded));
        }

        if (!lastImage)
        {
            mTechniques.clear();
            return;
        }

        constexpr auto color = VK_IMAGE_ASPECT_COLOR_BIT;
        constexpr auto src = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        constexpr auto dst = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        const auto present = mEngine.getPresentLayout();
        mBlitOut->srcImage = lastImage;
        // The swapchain image is in the transfer source layout since the copy in.
        mBeforeCopyOut = vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            barrier(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, src, lastImage, color),
            barrier(VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, src, dst, {}, color));
        mAfterCopyOut = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            barrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, dst, present, {}, color));
        group->addChild(mBeforeCopyOut);
        group->addChild(mBlitOut);
        group->addChild(mAfterCopyOut);
        if (mMultisampled)
        {
            // The GUI pass resolves its multisampled colour over the swapchain image, so the result is drawn in that
            // pass too. The copy above stays for screenshots, taken from the swapchain image before that pass.
            mAfterCopyOut->imageMemoryBarriers.push_back(barrier(VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, lastImage, color));
            if (auto node = createPresent(lastImageView))
                mPresentSwitch->addChild(mSceneVisible, node);
        }

        // Under the scene's mask, so nothing is recorded in frames where the scene is not rendered, such as loading
        // screens: their swapchain images were never set by updateImages(). Shown by updateImages() itself.
        mPending = vsg::Switch::create();
        mPending->addChild(Mask_PostProcess, group);
    }

    void PostProcess::updateImages()
    {
        if (!mPending && mRoot->children.empty())
        {
            // depthCopy() must not record with no technique, or before a frame sets its images.
            mDepthCopy->children.clear();
            return;
        }
        mDepthCopy->children = { mBeforeCopyDepth, mBlitDepth, mAfterCopyDepth };
        auto srcColor = mEngine.colorImageView(0)->image;
        auto srcDepth = mDepthSource ? mDepthSource->image : mEngine.depthImageView()->image;
        mBeforeCopyIn->imageMemoryBarriers[1]->image = srcColor;
        mBeforeCopyDepth->imageMemoryBarriers[1]->image = srcDepth;
        mAfterCopyDepth->imageMemoryBarriers[1]->image = srcDepth;
        mBlitColor->srcImage = srcColor;
        mBlitDepth->srcImage = srcDepth;
        mBeforeCopyOut->imageMemoryBarriers[1]->image = srcColor;
        mAfterCopyOut->imageMemoryBarriers[0]->image = srcColor;
        mBlitOut->dstImage = srcColor;
        // The frame before this one recorded the clears.
        if (!mPending && mFirstFrame)
        {
            mFirstFrame->setAllChildren(false);
            mFirstFrame = {};
        }
        if (mPending)
        {
            mRoot->addChild(mPending);
            mPending = {};
        }
    }

    void PostProcess::update(Fx::OmwData data)
    {
        if (mRebuildRequested && --mRebuildDelay <= 0)
            rebuild();
        if (mTechniques.empty())
            return;
        for (auto& technique : mTechniques)
        {
            for (LoadedPass& pass : technique->passes)
            {
                char* bytes = reinterpret_cast<char*>(pass.uniforms->dataPointer());
                std::memcpy(bytes, &data, sizeof(data));
                for (const Fx::VulkanUniformSlot& slot : pass.translated.uniforms)
                {
                    const auto uniform = technique->technique->findUniform(slot.name);
                    if (uniform != technique->technique->getUniformMap().end())
                        writeUniform(**uniform, bytes + slot.offset);
                }
                pass.uniforms->dirty();
            }
        }
    }
}
