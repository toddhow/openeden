#include "autoexposure.hpp"

#include <cmath>
#include <cstring>

#include <vsg/commands/Commands.h>
#include <vsg/commands/Dispatch.h>
#include <vsg/commands/PipelineBarrier.h>
#include <vsg/io/read.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ComputePipeline.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/PushConstants.h>
#include <vsg/state/Sampler.h>
#include <vsg/vk/Context.h>
#include <vsg/vk/DeviceMemory.h>

#include <components/debug/debuglog.hpp>

namespace Render
{
    namespace
    {
        const char* const clearPath = "post/main.aeclear.spv";
        const char* const histPath = "post/main.aehist.spv";
        const char* const resolvePath = "post/main.aeresolve.spv";
        // files/shaders/post/autoexposure.slang: histMain is 16 by 16 threads.
        constexpr std::uint32_t groupSize = 16;

        vsg::ref_ptr<vsg::BindComputePipeline> createPipeline(
            vsg::ref_ptr<vsg::PipelineLayout> layout, vsg::ref_ptr<const vsg::Options> shaderOptions, const char* path)
        {
            auto module = vsg::read_cast<vsg::ShaderModule>(path, shaderOptions);
            if (!module || module->code.empty())
            {
                Log(Debug::Error) << "Auto exposure: the compute shader " << path
                                  << " was not found (is slangc missing from this build?)";
                return {};
            }
            auto stage = vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT, "main", module);
            return vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout, stage));
        }
    }

    AutoExposurePass::AutoExposurePass(vsg::ref_ptr<vsgUtil::CompileContext> compile,
        vsg::ref_ptr<const vsg::Options> shaderOptions, float lowPercentile, float highPercentile,
        float highlightPercentile)
        : mCompile(std::move(compile))
        , mShaderOptions(std::move(shaderOptions))
        , mLow(lowPercentile)
        , mHigh(highPercentile)
        , mHighlight(highlightPercentile)
        , mNode(vsg::Group::create())
        , mPush(vsg::vec4Value::create(vsg::vec4(4.f, 1.f, lowPercentile, highPercentile)))
        , mPush2(vsg::vec4Value::create(vsg::vec4(highlightPercentile, 0.f, 0.f, 0.f)))
    {
    }

    AutoExposurePass::~AutoExposurePass() {}

    bool AutoExposurePass::shadersAvailable(vsg::ref_ptr<const vsg::Options> shaderOptions)
    {
        bool all = true;
        for (const char* path : { clearPath, histPath, resolvePath })
        {
            auto module = vsg::read_cast<vsg::ShaderModule>(path, shaderOptions);
            if (!module || module->code.empty())
            {
                Log(Debug::Error) << "Auto exposure: the compute shader " << path
                                  << " was not found (is slangc missing from this build?); it is off";
                all = false;
            }
        }
        return all;
    }

    std::uint32_t AutoExposurePass::strideFor(const VkExtent2D& extent)
    {
        std::uint32_t stride = 4;
        while (static_cast<std::uint64_t>((extent.width + stride - 1) / stride)
                * ((extent.height + stride - 1) / stride)
            > AutoExposure::maxSamples)
            ++stride;
        return stride;
    }

    bool AutoExposurePass::build(vsg::ref_ptr<vsg::ImageView> sceneColour, const VkExtent2D& extent)
    {
        mNode->children.clear();
        mResults = {};
        mMapped = nullptr;
        if (!sceneColour || extent.width == 0 || extent.height == 0 || !mCompile || !mCompile->device)
            return false;
        vsg::Device* device = mCompile->device;
        mStride = strideFor(extent);

        const VkDeviceSize histogramBytes = AutoExposure::histogramEntries * sizeof(std::uint32_t);
        auto histogram = vsg::createBufferAndMemory(device, histogramBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        const VkDeviceSize ringBytes = AutoExposure::slots * AutoExposure::wordsPerSlot * sizeof(std::uint32_t);
        mResults = vsg::createBufferAndMemory(device, ringBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!histogram || !mResults)
        {
            Log(Debug::Error) << "Auto exposure: no memory for the histogram buffers";
            mResults = {};
            return false;
        }
        // The ring starts as "never written": serial 0 in every slot.
        void* mapped = nullptr;
        auto* memory = mResults->getDeviceMemory(device->deviceID);
        if (!memory || memory->map(mResults->getMemoryOffset(device->deviceID), ringBytes, 0, &mapped) != VK_SUCCESS
            || !mapped)
        {
            Log(Debug::Error) << "Auto exposure: the result buffer could not be mapped";
            mResults = {};
            return false;
        }
        std::memset(mapped, 0, ringBytes);
        mMapped = static_cast<const volatile std::uint32_t*>(mapped);

        auto sampler = vsg::Sampler::create();
        sampler->minFilter = sampler->magFilter = VK_FILTER_LINEAR;
        sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        auto colour = vsg::DescriptorImage::create(
            vsg::ImageInfo::create(sampler, sceneColour, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), 0, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        auto histogramDescriptor = vsg::DescriptorBuffer::create(
            vsg::BufferInfoList{ vsg::BufferInfo::create(histogram, 0, histogramBytes) }, 1, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto resultDescriptor = vsg::DescriptorBuffer::create(
            vsg::BufferInfoList{ vsg::BufferInfo::create(mResults, 0, ringBytes) }, 2, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        vsg::DescriptorSetLayoutBindings bindings{
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };
        auto setLayout = vsg::DescriptorSetLayout::create(bindings);
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{ { VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(2 * sizeof(vsg::vec4)) } });
        auto clear = createPipeline(layout, mShaderOptions, clearPath);
        auto hist = createPipeline(layout, mShaderOptions, histPath);
        auto resolve = createPipeline(layout, mShaderOptions, resolvePath);
        if (!clear || !hist || !resolve)
        {
            mResults = {};
            mMapped = nullptr;
            return false;
        }
        auto descriptorSet
            = vsg::DescriptorSet::create(setLayout, vsg::Descriptors{ colour, histogramDescriptor, resultDescriptor });
        auto bindSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, descriptorSet);

        // What the scene passes wrote (a barrier's first scope holds every command submitted before it) and what last
        // frame's resolve wrote to the histogram.
        auto first = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            vsg::MemoryBarrier::create(
                VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
        auto between = [] {
            return vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                vsg::MemoryBarrier::create(
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
        };
        // The ring is read by the host.
        auto last = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
            vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT));

        auto commands = vsg::Commands::create();
        commands->addChild(first);
        // The pipeline first: a push constant is recorded against the layout of the pipeline bound last. The three
        // pipelines share one layout.
        commands->addChild(clear);
        commands->addChild(bindSet);
        commands->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, mPush));
        commands->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(vsg::vec4), mPush2));
        commands->addChild(vsg::Dispatch::create(1, 1, 1));
        commands->addChild(between());
        commands->addChild(hist);
        const std::uint32_t samplesX = (extent.width + mStride - 1) / mStride;
        const std::uint32_t samplesY = (extent.height + mStride - 1) / mStride;
        commands->addChild(vsg::Dispatch::create(
            (samplesX + groupSize - 1) / groupSize, (samplesY + groupSize - 1) / groupSize, 1));
        commands->addChild(between());
        commands->addChild(resolve);
        commands->addChild(vsg::Dispatch::create(1, 1, 1));
        commands->addChild(last);

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
        mNode->children.push_back(commands);
        if (!compileOnce->compile(mNode) || !taken)
        {
            Log(Debug::Error) << "Auto exposure: the compute commands could not be compiled";
            mNode->children.clear();
            mResults = {};
            mMapped = nullptr;
            return false;
        }
        mLive.push_back(commands);
        mLive.push_back(histogram);
        mLive.push_back(mResults);
        return true;
    }

    void AutoExposurePass::beginFrame()
    {
        ++mFrame;
        mPush->set(
            vsg::vec4(static_cast<float>(mStride), static_cast<float>(AutoExposure::serialOf(mFrame)), mLow, mHigh));
    }

    std::optional<AutoExposure::Measurement> AutoExposurePass::read()
    {
        if (!mMapped)
            return std::nullopt;
        std::array<std::uint32_t, AutoExposure::slots * AutoExposure::wordsPerSlot> words{};
        for (std::size_t i = 0; i < words.size(); ++i)
            words[i] = mMapped[i];
        std::array<std::uint32_t, AutoExposure::slots> serials{};
        for (int i = 0; i < AutoExposure::slots; ++i)
            serials[i] = words[i * AutoExposure::wordsPerSlot + 3];
        const auto slot = AutoExposure::pickSlot(serials, AutoExposure::serialOf(mFrame));
        if (!slot)
            return std::nullopt;
        AutoExposure::Measurement m;
        const std::size_t base = static_cast<std::size_t>(*slot) * AutoExposure::wordsPerSlot;
        std::memcpy(&m.meanLog2, &words[base + 0], sizeof(float));
        std::memcpy(&m.meanLuma, &words[base + 1], sizeof(float));
        std::memcpy(&m.weight, &words[base + 2], sizeof(float));
        m.serial = words[base + 3];
        std::memcpy(&m.highLog2, &words[base + 4], sizeof(float));
        return m;
    }
}
