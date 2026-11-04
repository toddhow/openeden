#include "taa.hpp"

#include <vsg/commands/Dispatch.h>
#include <vsg/commands/PipelineBarrier.h>
#include <vsg/io/read.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ComputePipeline.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/PushConstants.h>
#include <vsg/state/Sampler.h>
#include <vsg/vk/Context.h>

#include <components/debug/debuglog.hpp>
#include <components/vsgutil/image.hpp>

namespace Render
{
    namespace
    {
        constexpr VkFormat imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        // files/shaders/post/taa.slang: one thread group is 8 by 8 pixels.
        constexpr std::uint32_t groupSize = 8;
        const char* const resolvePath = "post/main.taaresolve.spv";
        const char* const outputPath = "post/main.taaoutput.spv";

        std::uint32_t groups(std::uint32_t pixels)
        {
            return (pixels + groupSize - 1) / groupSize;
        }

        vsg::ref_ptr<vsg::Image> createImage(const VkExtent2D& extent)
        {
            return vsgUtil::createImage(imageFormat, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        }

        vsg::ref_ptr<vsg::ImageView> createView(vsg::ref_ptr<vsg::Image> image)
        {
            return vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        vsg::ref_ptr<vsg::Sampler> createSampler(VkFilter filter)
        {
            auto sampler = vsg::Sampler::create();
            sampler->minFilter = sampler->magFilter = filter;
            sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler->addressModeU = sampler->addressModeV = sampler->addressModeW
                = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            return sampler;
        }

        vsg::ref_ptr<vsg::DescriptorImage> sampledDescriptor(
            vsg::ref_ptr<vsg::ImageView> view, VkFilter filter, VkImageLayout layout, uint32_t binding)
        {
            return vsg::DescriptorImage::create(vsg::ImageInfo::create(createSampler(filter), view, layout), binding, 0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }

        // A storage image has a view of its own (VUID-VkWriteDescriptorSet-descriptorType-00336).
        vsg::ref_ptr<vsg::DescriptorImage> storageDescriptor(vsg::ref_ptr<vsg::Image> image, uint32_t binding)
        {
            return vsg::DescriptorImage::create(
                vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>(), createView(image), VK_IMAGE_LAYOUT_GENERAL),
                binding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        }

        vsg::ref_ptr<vsg::BindComputePipeline> createPipeline(
            vsg::ref_ptr<vsg::PipelineLayout> layout, vsg::ref_ptr<const vsg::Options> shaderOptions, const char* path)
        {
            auto module = vsg::read_cast<vsg::ShaderModule>(path, shaderOptions);
            if (!module || module->code.empty())
            {
                Log(Debug::Error) << "Temporal anti-aliasing: the compute shader " << path
                                  << " was not found (is slangc missing from this build?)";
                return {};
            }
            auto stage = vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT, "main", module);
            return vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout, stage));
        }
    }

    TaaPass::TaaPass(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> shaderOptions,
        const Taa::Settings& settings, vsg::ref_ptr<Taa::JitterOffset> jitter)
        : mCompile(std::move(compile))
        , mShaderOptions(std::move(shaderOptions))
        , mSettings(settings)
        , mNode(vsg::Group::create())
        , mJitter(std::move(jitter))
        , mMatrix(vsg::mat4Value::create(vsg::mat4()))
        , mScale(vsg::vec4Value::create(vsg::vec4(1.f, 1.f, 1.f, 1.f)))
        , mParams(vsg::vec4Value::create(vsg::vec4(0.f, 0.f, Taa::varianceGamma, 0.f)))
        , mLook(vsg::vec4Value::create(vsg::vec4(settings.sharpness, 0.f, 0.f, 0.f)))
    {
    }

    TaaPass::~TaaPass() {}

    bool TaaPass::shadersAvailable(vsg::ref_ptr<const vsg::Options> shaderOptions)
    {
        bool all = true;
        for (const char* path : { resolvePath, outputPath })
        {
            auto module = vsg::read_cast<vsg::ShaderModule>(path, shaderOptions);
            if (!module || module->code.empty())
            {
                Log(Debug::Error) << "Temporal anti-aliasing: the compute shader " << path
                                  << " was not found (is slangc missing from this build?); TAA is off";
                all = false;
            }
        }
        return all;
    }

    bool TaaPass::build(vsg::ref_ptr<vsg::ImageView> colour, vsg::ref_ptr<vsg::ImageView> linearDepth,
        vsg::ref_ptr<vsg::ImageView> velocity, const VkExtent2D& extent)
    {
        mNode->children.clear();
        mResult = {};
        mFirstSwitch = {};
        mFramesSinceBuild = 0;
        mExtent = extent;
        // The node can be recorded before the next beginFrame (a frame with no scene update): the shader must then
        // know the new size and take nothing from the history.
        {
            const vsg::vec4 scale = mScale->value();
            mScale->set(
                vsg::vec4(scale.x, scale.y, static_cast<float>(extent.width), static_cast<float>(extent.height)));
            vsg::vec4 params = mParams->value();
            params.x = 0.f;
            mParams->set(params);
        }
        if (!colour || !linearDepth || !velocity || extent.width == 0 || extent.height == 0)
            return false;

        auto resolveImage = createImage(extent);
        auto historyImage = createImage(extent);
        auto outputImage = createImage(extent);
        mResult = createView(outputImage);

        vsg::DescriptorSetLayoutBindings bindings{
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };
        auto setLayout = vsg::DescriptorSetLayout::create(bindings);
        // The matrix, then three vec4 (files/shaders/post/taa.slang: Push).
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{
                { VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(vsg::mat4) + 3 * sizeof(vsg::vec4)) } });

        auto resolve = createPipeline(layout, mShaderOptions, resolvePath);
        auto output = createPipeline(layout, mShaderOptions, outputPath);
        if (!resolve || !output)
            return false;

        auto descriptorSet = vsg::DescriptorSet::create(setLayout,
            vsg::Descriptors{ sampledDescriptor(colour, VK_FILTER_NEAREST, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0),
                sampledDescriptor(linearDepth, VK_FILTER_NEAREST, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1),
                sampledDescriptor(velocity, VK_FILTER_NEAREST, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2),
                sampledDescriptor(createView(historyImage), VK_FILTER_LINEAR, VK_IMAGE_LAYOUT_GENERAL, 3),
                storageDescriptor(resolveImage, 4), storageDescriptor(historyImage, 5),
                storageDescriptor(outputImage, 6) });
        auto bindSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, descriptorSet);
        auto dispatch = vsg::Dispatch::create(groups(extent.width), groups(extent.height), 1);

        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        // The images are written whole every frame, so their old contents are dropped; the history is only put into
        // its layout on the first frames.
        auto discard = [&](vsg::ref_ptr<vsg::Image> image) {
            return vsg::ImageMemoryBarrier::create(0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, range);
        };
        // What the scene, velocity and occlusion passes wrote (also through the render passes' layout transitions: a
        // barrier's first scope holds every command submitted before it, components/render/CLAUDE.md, screenshots) and
        // what last frame's bloom and tonemap read of the output.
        auto memory = [] {
            return vsg::MemoryBarrier::create(
                VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        };
        auto normal = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, memory(), discard(resolveImage), discard(outputImage));
        auto first
            = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                memory(), discard(resolveImage), discard(outputImage), discard(historyImage));
        auto firstCommands = vsg::Commands::create();
        firstCommands->addChild(first);
        auto normalCommands = vsg::Commands::create();
        normalCommands->addChild(normal);
        mFirstSwitch = vsg::Switch::create();
        mFirstSwitch->addChild(true, firstCommands);
        mFirstSwitch->addChild(false, normalCommands);

        auto between = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            vsg::MemoryBarrier::create(
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
        // The output goes to the bloom and the tonemap, which sample it in SHADER_READ_ONLY_OPTIMAL.
        auto after = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            vsg::ImageMemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED, outputImage, range));

        auto commands = vsg::Commands::create();
        // The pipeline first: a push constant is recorded against the layout of the pipeline bound last, so before a
        // bind it would go to the previous pass's layout (review of stage c). Both pipelines share one layout, so the
        // values survive the second bind.
        commands->addChild(resolve);
        commands->addChild(bindSet);
        commands->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, mMatrix));
        commands->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(vsg::mat4), mScale));
        commands->addChild(
            vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(vsg::mat4) + sizeof(vsg::vec4), mParams));
        commands->addChild(
            vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(vsg::mat4) + 2 * sizeof(vsg::vec4), mLook));
        commands->addChild(dispatch);
        commands->addChild(between);
        commands->addChild(output);
        commands->addChild(dispatch);
        commands->addChild(after);

        // Compiled with the first context that is offered: a compute pipeline and its descriptors do not depend on
        // the view or the render pass.
        auto compileOnce = mCompile->clone(~vsg::Mask(0));
        bool taken = false;
        compileOnce->contextSelectionFunction = [&taken](vsg::Context&) {
            if (taken)
                return false;
            taken = true;
            return true;
        };
        mNode->children.push_back(mFirstSwitch);
        mNode->children.push_back(commands);
        if (!compileOnce->compile(mNode) || !taken)
        {
            Log(Debug::Error) << "Temporal anti-aliasing: the compute commands could not be compiled";
            mNode->children.clear();
            mResult = {};
            return false;
        }

        mLive.push_back(mFirstSwitch);
        mLive.push_back(commands);
        mLive.push_back(resolveImage);
        mLive.push_back(historyImage);
        mLive.push_back(outputImage);
        mLive.push_back(mResult);
        return true;
    }

    void TaaPass::beginFrame(const Taa::FrameInput& input)
    {
        mState.previousJitterPixels = mState.jitterPixels;
        mState.jitterPixels = Taa::jitterPixels(mState.frame, mSettings.jitterScale);
        mState.jitterNdc = Taa::jitterNdc(mState.jitterPixels, mExtent.width, mExtent.height);
        // Without a successful build nothing resolves the picture, so nothing may shift it either.
        mJitter->ndc = mResult ? mState.jitterNdc : vsg::dvec2(0.0, 0.0);
        mState.historyValid = mResult && !input.reset && mFramesSinceBuild >= warmupFrames;
        mState.toPreviousClip = input.toPreviousClip;
        ++mState.frame;

        if (mFirstSwitch)
            mFirstSwitch->setSingleChildOn(mFramesSinceBuild < warmupFrames ? 0 : 1);
        if (mResult)
            ++mFramesSinceBuild;

        mMatrix->set(input.toPreviousClip);
        mScale->set(vsg::vec4(input.projectionX, input.projectionY, static_cast<float>(mExtent.width),
            static_cast<float>(mExtent.height)));
        mParams->set(vsg::vec4(mState.historyValid ? mSettings.history : 0.f,
            mSettings.clipping == Taa::Clipping::MinMax ? 1.f : 0.f, mSettings.clipGamma,
            static_cast<float>(static_cast<int>(mSettings.debug))));
        mLook->set(vsg::vec4(mSettings.sharpness, 0.f, 0.f, 0.f));
    }
}
