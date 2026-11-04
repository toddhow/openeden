#include "aotemporal.hpp"

#include <array>

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

#include "aomath.hpp"

namespace Render
{
    namespace
    {
        // The raw value needs only one channel and 16 bits. The result and the history carry a value, an encoded depth,
        // a count and a packed normal (24 bits: exact only in 32-bit floats), and an exponential average at weight 0.9
        // would stall in 8 bits.
        constexpr VkFormat rawFormat = VK_FORMAT_R16_SFLOAT;
        constexpr VkFormat stateFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        // files/shaders/post/aotemporal.slang: one thread group is 8 by 8 pixels.
        constexpr std::uint32_t groupSize = 8;
        const char* const genPath = "post/main.aogen.spv";
        const char* const resolvePath = "post/main.aoresolve.spv";
        const char* const copyPath = "post/main.aocopy.spv";
        // Settings constants of the fragment pass (post/aosettings.slang, Resource::settingsConstantTable).
        constexpr std::uint32_t radiusConstant = 1100;
        constexpr std::uint32_t slicesConstant = 1101;
        constexpr std::uint32_t stepsConstant = 1102;

        // The shader's debug view numbers (files/shaders/post/aotemporal.slang, Push::misc.x): 0 shows the value.
        int debugCode(Ao::Temporal::DebugView view)
        {
            switch (view)
            {
                case Ao::Temporal::DebugView::Raw:
                    return 1;
                case Ao::Temporal::DebugView::Weight:
                    return 2;
                case Ao::Temporal::DebugView::Reject:
                    return 3;
                case Ao::Temporal::DebugView::None:
                case Ao::Temporal::DebugView::Static:
                    break;
            }
            return 0;
        }

        std::uint32_t groups(std::uint32_t pixels)
        {
            return (pixels + groupSize - 1) / groupSize;
        }

        vsg::ref_ptr<vsg::Image> createStorageImage(VkFormat format, const VkExtent2D& extent, bool sampled)
        {
            VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT;
            if (sampled)
                usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            return vsgUtil::createImage(format, extent, usage);
        }

        vsg::ref_ptr<vsg::ImageView> createView(vsg::ref_ptr<vsg::Image> image)
        {
            return vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        vsg::ref_ptr<vsg::DescriptorImage> sampledDescriptor(vsg::ref_ptr<vsg::ImageView> view, uint32_t binding)
        {
            auto sampler = vsg::Sampler::create();
            sampler->minFilter = sampler->magFilter = VK_FILTER_NEAREST;
            sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler->addressModeU = sampler->addressModeV = sampler->addressModeW
                = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            return vsg::DescriptorImage::create(
                vsg::ImageInfo::create(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), binding, 0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }

        // A storage image has a view of its own (VUID-VkWriteDescriptorSet-descriptorType-00336).
        vsg::ref_ptr<vsg::DescriptorImage> storageDescriptor(vsg::ref_ptr<vsg::Image> image, uint32_t binding)
        {
            return vsg::DescriptorImage::create(
                vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>(), createView(image), VK_IMAGE_LAYOUT_GENERAL),
                binding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        }

        vsg::ref_ptr<vsg::BindComputePipeline> createPipeline(vsg::ref_ptr<vsg::PipelineLayout> layout,
            vsg::ref_ptr<const vsg::Options> shaderOptions, const char* path, const Ao::Temporal::Settings& settings)
        {
            auto module = vsg::read_cast<vsg::ShaderModule>(path, shaderOptions);
            if (!module || module->code.empty())
            {
                Log(Debug::Error) << "Ambient occlusion accumulation: the compute shader " << path
                                  << " was not found (is slangc missing from this build?)";
                return {};
            }
            auto stage = vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT, "main", module);
            stage->specializationConstants[radiusConstant] = vsg::floatValue::create(settings.radius);
            stage->specializationConstants[slicesConstant] = vsg::intValue::create(settings.slices);
            stage->specializationConstants[stepsConstant] = vsg::intValue::create(settings.steps);
            return vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout, stage));
        }
    }

    AoTemporalPass::AoTemporalPass(vsg::ref_ptr<vsgUtil::CompileContext> compile,
        vsg::ref_ptr<const vsg::Options> shaderOptions, const Ao::Temporal::Settings& settings)
        : mCompile(std::move(compile))
        , mShaderOptions(std::move(shaderOptions))
        , mSettings(settings)
        , mNode(vsg::Group::create())
        , mDepthRow(vsg::vec4Value::create(vsg::vec4(0.f, 0.f, 0.f, 1.f)))
        , mRot0(vsg::vec4Value::create(vsg::vec4(1.f, 0.f, 0.f, 1.f)))
        , mRot1(vsg::vec4Value::create(vsg::vec4(0.f, 1.f, 0.f, 1.f)))
        , mRot2(vsg::vec4Value::create(vsg::vec4(0.f, 0.f, 1.f, 0.f)))
        , mNoise(vsg::vec4Value::create(vsg::vec4(0.f, 0.f, 0.f, 0.f)))
        , mMisc(vsg::vec4Value::create(vsg::vec4(0.f, 0.f, 0.f, 0.f)))
    {
    }

    AoTemporalPass::~AoTemporalPass() {}

    bool AoTemporalPass::shadersAvailable(vsg::ref_ptr<const vsg::Options> shaderOptions)
    {
        bool all = true;
        for (const char* path : { genPath, resolvePath, copyPath })
        {
            auto module = vsg::read_cast<vsg::ShaderModule>(path, shaderOptions);
            if (!module || module->code.empty())
            {
                Log(Debug::Error) << "Ambient occlusion accumulation: the compute shader " << path
                                  << " was not found (is slangc missing from this build?); it is off";
                all = false;
            }
        }
        return all;
    }

    bool AoTemporalPass::build(vsg::ref_ptr<vsg::ImageView> linearDepth, vsg::ref_ptr<vsg::ImageView> normal,
        vsg::ref_ptr<vsg::ImageView> velocity, const VkExtent2D& sceneExtent)
    {
        mNode->children.clear();
        mResult = {};
        mFirstSwitch = {};
        mFramesSinceBuild = 0;
        mExtent = Ao::halfExtent(sceneExtent);
        // The node can be recorded before the next beginFrame (a frame with no scene update): the shader must then take
        // nothing from the history.
        {
            vsg::vec4 weight = mRot2->value();
            weight.w = 0.f;
            mRot2->set(weight);
        }
        if (!linearDepth || !normal || !velocity || sceneExtent.width == 0 || sceneExtent.height == 0)
            return false;

        auto rawImage = createStorageImage(rawFormat, mExtent, false);
        auto resolveImage = createStorageImage(stateFormat, mExtent, true);
        auto historyImage = createStorageImage(stateFormat, mExtent, false);
        mResult = createView(resolveImage);

        vsg::DescriptorSetLayoutBindings bindings{
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };
        auto setLayout = vsg::DescriptorSetLayout::create(bindings);
        // Six vec4 (files/shaders/post/aotemporal.slang: Push).
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{
                { VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(6 * sizeof(vsg::vec4)) } });

        auto gen = createPipeline(layout, mShaderOptions, genPath, mSettings);
        auto resolve = createPipeline(layout, mShaderOptions, resolvePath, mSettings);
        auto copy = createPipeline(layout, mShaderOptions, copyPath, mSettings);
        if (!gen || !resolve || !copy)
        {
            mResult = {};
            return false;
        }

        auto descriptorSet = vsg::DescriptorSet::create(setLayout,
            vsg::Descriptors{ sampledDescriptor(linearDepth, 0), sampledDescriptor(normal, 1),
                sampledDescriptor(velocity, 2), storageDescriptor(rawImage, 3), storageDescriptor(resolveImage, 4),
                storageDescriptor(historyImage, 5) });
        auto bindSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, descriptorSet);
        auto dispatch = vsg::Dispatch::create(groups(mExtent.width), groups(mExtent.height), 1);

        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        // The raw and result images are written whole every frame, so their old contents are dropped; the history is
        // only put into its layout on the first frames.
        auto discard = [&](vsg::ref_ptr<vsg::Image> image) {
            return vsg::ImageMemoryBarrier::create(0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, range);
        };
        // What the scene and velocity passes wrote (also through the render passes' layout transitions: a barrier's
        // first scope holds every command submitted before it, components/render/CLAUDE.md, screenshots), what last
        // frame's copy wrote into the history and what last frame's tonemap read of the result.
        auto memory = [] {
            return vsg::MemoryBarrier::create(
                VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        };
        auto normalBarrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, memory(), discard(rawImage), discard(resolveImage));
        auto firstBarrier
            = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                memory(), discard(rawImage), discard(resolveImage), discard(historyImage));
        auto firstCommands = vsg::Commands::create();
        firstCommands->addChild(firstBarrier);
        auto normalCommands = vsg::Commands::create();
        normalCommands->addChild(normalBarrier);
        mFirstSwitch = vsg::Switch::create();
        mFirstSwitch->addChild(true, firstCommands);
        mFirstSwitch->addChild(false, normalCommands);

        auto between = [] {
            return vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                vsg::MemoryBarrier::create(
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
        };
        // The result goes to the tonemap, which samples its first channel in SHADER_READ_ONLY_OPTIMAL.
        auto after = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            vsg::ImageMemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED, resolveImage, range));

        auto commands = vsg::Commands::create();
        // The pipeline first: a push constant is recorded against the layout of the pipeline bound last (task 0075,
        // review of stage c). The three pipelines share one layout, so the values survive the other binds.
        commands->addChild(gen);
        commands->addChild(bindSet);
        const std::array<vsg::ref_ptr<vsg::vec4Value>, 6> pushed{ mDepthRow, mRot0, mRot1, mRot2, mNoise, mMisc };
        for (std::size_t i = 0; i < pushed.size(); ++i)
            commands->addChild(vsg::PushConstants::create(
                VK_SHADER_STAGE_COMPUTE_BIT, static_cast<uint32_t>(i * sizeof(vsg::vec4)), pushed[i]));
        commands->addChild(dispatch);
        commands->addChild(between());
        commands->addChild(resolve);
        commands->addChild(dispatch);
        commands->addChild(between());
        commands->addChild(copy);
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
            Log(Debug::Error) << "Ambient occlusion accumulation: the compute commands could not be compiled";
            mNode->children.clear();
            mResult = {};
            return false;
        }

        mLive.push_back(mFirstSwitch);
        mLive.push_back(commands);
        mLive.push_back(rawImage);
        mLive.push_back(resolveImage);
        mLive.push_back(historyImage);
        mLive.push_back(mResult);
        return true;
    }

    void AoTemporalPass::beginFrame(const Ao::Temporal::FrameInput& input)
    {
        using namespace Ao::Temporal;
        const std::uint32_t frame = input.frame ? *input.frame : mCounter++;
        mState.frame = frame;
        // The first frames after a build put the history in its layout and take nothing from it.
        const bool usable = mResult && historyUsable(input.reset, mFramesSinceBuild);
        if (mFirstSwitch)
            mFirstSwitch->setSingleChildOn(mFramesSinceBuild < warmupFrames ? 0 : 1);
        if (mResult)
            ++mFramesSinceBuild;

        // `static`: the plain pass's noise and no history (the value must equal the fragment pass's).
        const bool plain = mSettings.debug == DebugView::Static;
        mState.noise = plain ? vsg::vec2(0.f, 0.f) : noiseOffset(frame);
        mState.historyUsable = usable && !plain;
        mState.historyWeight = mState.historyUsable ? mSettings.history : 0.f;

        // Only the last row of the matrix: the previous camera's depth of a point, w of its clip position. vsg's
        // matrices are indexed (column, row).
        const vsg::mat4& m = input.toPreviousClip;
        mDepthRow->set(vsg::vec4(m(0, 3), m(1, 3), m(2, 3), m(3, 3)));
        mRot0->set(
            vsg::vec4(input.rotation.m[0][0], input.rotation.m[0][1], input.rotation.m[0][2], input.projectionX));
        mRot1->set(
            vsg::vec4(input.rotation.m[1][0], input.rotation.m[1][1], input.rotation.m[1][2], input.projectionY));
        mRot2->set(
            vsg::vec4(input.rotation.m[2][0], input.rotation.m[2][1], input.rotation.m[2][2], mState.historyWeight));
        // The jitter only matters when a history is used.
        const vsg::vec2 jitter = mState.historyUsable ? input.jitterDelta : vsg::vec2(0.f, 0.f);
        mNoise->set(vsg::vec4(mState.noise.x, mState.noise.y, jitter.x, jitter.y));
        mMisc->set(vsg::vec4(static_cast<float>(debugCode(mSettings.debug)), mSettings.clampGamma, 0.f, 0.f));
    }
}
