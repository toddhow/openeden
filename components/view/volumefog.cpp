#include "volumefog.hpp"

#include <algorithm>
#include <chrono>

#include <vsg/commands/Dispatch.h>
#include <vsg/commands/PipelineBarrier.h>
#include <vsg/io/read.h>
#include <vsg/maths/mat4.h>
#include <vsg/nodes/Group.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ComputePipeline.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/Sampler.h>

#include <components/debug/debuglog.hpp>
#include <components/pipeline/computebindings.hpp>
#include <components/pipeline/layout.hpp>
#include <components/pipeline/sets.hpp>
#include <components/pipeline/viewbindings.hpp>

namespace View
{
    namespace
    {
        constexpr VkFormat volumeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

        vsg::ref_ptr<vsg::Image> createVolumeImage(Render::Volume::GridSize grid)
        {
            auto image = vsg::Image::create();
            image->imageType = VK_IMAGE_TYPE_3D;
            image->format = volumeFormat;
            image->extent = { grid.x, grid.y, grid.z };
            image->mipLevels = 1;
            image->arrayLayers = 1;
            image->samples = VK_SAMPLE_COUNT_1_BIT;
            image->tiling = VK_IMAGE_TILING_OPTIMAL;
            image->usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            return image;
        }

        vsg::ref_ptr<vsg::ImageView> createVolumeView(vsg::ref_ptr<vsg::Image> image)
        {
            auto view = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
            view->viewType = VK_IMAGE_VIEW_TYPE_3D;
            return view;
        }

        vsg::ref_ptr<vsg::Sampler> linearClampSampler()
        {
            auto sampler = vsg::Sampler::create();
            sampler->minFilter = sampler->magFilter = VK_FILTER_LINEAR;
            sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler->addressModeU = sampler->addressModeV = sampler->addressModeW
                = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            return sampler;
        }

        // The history is read through a sampler, so that the lookup between froxels is trilinear.
        vsg::ref_ptr<vsg::DescriptorImage> historyDescriptor(vsg::ref_ptr<vsg::Image> image, uint32_t binding)
        {
            return vsg::DescriptorImage::create(
                vsg::ImageInfo::create(linearClampSampler(), createVolumeView(image), VK_IMAGE_LAYOUT_GENERAL), binding,
                0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }

        vsg::ref_ptr<vsg::DescriptorImage> storageDescriptor(vsg::ref_ptr<vsg::Image> image, uint32_t binding)
        {
            // A storage image's view is not shared with the sampled one (VUID-VkWriteDescriptorSet-descriptorType-00336).
            return vsg::DescriptorImage::create(
                vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>(), createVolumeView(image), VK_IMAGE_LAYOUT_GENERAL),
                binding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        }

        vsg::ref_ptr<vsg::BindComputePipeline> createPipeline(vsg::ref_ptr<vsg::PipelineLayout> layout,
            vsg::ref_ptr<const vsg::Options> shaderOptions, const std::string& path)
        {
            auto module = vsg::read_cast<vsg::ShaderModule>(path, shaderOptions);
            if (!module || module->code.empty())
            {
                Log(Debug::Error) << "Volumetrics: the compute shader " << path
                                  << " was not found (is slangc missing from this build?)";
                return {};
            }
            auto stage = vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT, "main", module);
            return vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout, stage));
        }
    }

    VolumeFog::VolumeFog(Render::Volume::Quality quality, bool temporal)
        : mQuality(quality)
        , mTemporal(temporal)
        , mGrid(Render::Volume::gridSize(quality))
    {
        mFillImage = createVolumeImage(mGrid);
        mResultImage = createVolumeImage(mGrid);
        if (mTemporal)
        {
            mBlendImages[0] = createVolumeImage(mGrid);
            mBlendImages[1] = createVolumeImage(mGrid);
        }

        mVolumeDescriptor = vsg::DescriptorImage::create(
            vsg::ImageInfo::create(linearClampSampler(), createVolumeView(mResultImage), VK_IMAGE_LAYOUT_GENERAL),
            Pipeline::Descriptors::VIEW_VOLUME_BINDING, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        mParams = vsg::vec4Array::create(Render::Volume::paramsVec4s);
        for (auto& v : *mParams)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        mParams->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        mParamsDescriptor = vsg::DescriptorBuffer::create(
            mParams, Pipeline::Descriptors::VIEW_VOLUME_PARAMS_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }

    bool VolumeFog::createCommands(
        vsg::ref_ptr<const vsg::Options> shaderOptions, vsg::ref_ptr<vsg::DescriptorSet> viewSet)
    {
        // The compute set: this frame's samples, the result, the blend (the samples again without a temporal blend: the
        // integration always reads binding 2) and the history.
        vsg::DescriptorSetLayoutBindings bindings{
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };
        vsg::DescriptorSetLayouts setLayouts = Pipeline::getCompatibleDescriptorSetLayouts();
        setLayouts.emplace_back(vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings()));
        auto computeLayout = vsg::DescriptorSetLayout::create(bindings);
        setLayouts.emplace_back(computeLayout);
        auto layout = vsg::PipelineLayout::create(setLayouts, vsg::PushConstantRanges());

        auto fill = createPipeline(layout, shaderOptions, "comp/volume/main.fill.spv");
        auto integrate = createPipeline(layout, shaderOptions, "comp/volume/main.integrate.spv");
        vsg::ref_ptr<vsg::BindComputePipeline> temporal;
        if (mTemporal)
            temporal = createPipeline(layout, shaderOptions, "comp/volume/main.temporal.spv");
        if (!fill || !integrate || (mTemporal && !temporal))
            return false;

        auto bindViewSet
            = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, layout, Pipeline::VIEW_SET, viewSet);

        using Render::Volume::dispatchGroups;
        using Render::Volume::groupSizeXY;
        auto dispatchFill = vsg::Dispatch::create(
            dispatchGroups(mGrid.x, groupSizeXY), dispatchGroups(mGrid.y, groupSizeXY), mGrid.z);
        auto dispatchColumns
            = vsg::Dispatch::create(dispatchGroups(mGrid.x, groupSizeXY), dispatchGroups(mGrid.y, groupSizeXY), 1);

        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        // Everything the earlier graphs and the last frame wrote or read: the shadow maps (depth writes), the light grid
        // and the history (compute writes), and the last frame's fragment reads of the result.
        auto memory = [] {
            return vsg::MemoryBarrier::create(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                    | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        };
        const VkPipelineStageFlags beforeSources = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
            | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        // The samples and the result are written whole each frame, so their old contents are dropped.
        auto discard = [&](vsg::ref_ptr<vsg::Image> image) {
            return vsg::ImageMemoryBarrier::create(0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, range);
        };
        auto normal = vsg::PipelineBarrier::create(beforeSources, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, memory(),
            discard(mFillImage), discard(mResultImage));
        auto first = vsg::PipelineBarrier::create(beforeSources, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, memory(),
            discard(mFillImage), discard(mResultImage));
        if (mTemporal)
        {
            // The very first frame also puts the blend and history images into their layout (they are kept after).
            first->imageMemoryBarriers.push_back(discard(mBlendImages[0]));
            first->imageMemoryBarriers.push_back(discard(mBlendImages[1]));
        }
        mBarrierSwitch = vsg::Switch::create();
        mBarrierSwitch->addChild(true, first);
        mBarrierSwitch->addChild(false, normal);

        auto between = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));
        auto after = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));

        // One run of the passes with the compute set `computeSet`.
        auto passes = [&](vsg::ref_ptr<vsg::DescriptorSet> computeSet) {
            auto commands = vsg::Commands::create();
            commands->addChild(vsg::BindDescriptorSet::create(
                VK_PIPELINE_BIND_POINT_COMPUTE, layout, Pipeline::COMPUTE_SET, computeSet));
            commands->addChild(fill);
            commands->addChild(dispatchFill);
            commands->addChild(between);
            if (mTemporal)
            {
                commands->addChild(temporal);
                commands->addChild(dispatchFill);
                commands->addChild(between);
            }
            commands->addChild(integrate);
            commands->addChild(dispatchColumns);
            return commands;
        };

        auto group = vsg::Group::create();
        group->addChild(mBarrierSwitch);
        group->addChild(bindViewSet);
        if (mTemporal)
        {
            // Even frames blend into image 0 and read image 1 as the history; odd frames the other way round.
            mParitySwitch = vsg::Switch::create();
            for (int parity = 0; parity < 2; ++parity)
            {
                auto computeSet = vsg::DescriptorSet::create(computeLayout,
                    vsg::Descriptors{ storageDescriptor(mFillImage, 0), storageDescriptor(mResultImage, 1),
                        storageDescriptor(mBlendImages[parity], 2), historyDescriptor(mBlendImages[1 - parity], 3) });
                mParitySwitch->addChild(parity == 0, passes(computeSet));
            }
            group->addChild(mParitySwitch);
        }
        else
        {
            auto computeSet = vsg::DescriptorSet::create(computeLayout,
                vsg::Descriptors{ storageDescriptor(mFillImage, 0), storageDescriptor(mResultImage, 1),
                    storageDescriptor(mFillImage, 2), historyDescriptor(mFillImage, 3) });
            group->addChild(passes(computeSet));
        }
        group->addChild(after);
        mNode = group;
        return true;
    }

    void VolumeFog::setParams(Render::Volume::ParamsInput input, const vsg::dmat4& view, const vsg::dmat4& projection)
    {
        const vsg::dmat4 inverseView = vsg::inverse(view);
        const vsg::dvec3 eye(inverseView(3, 0), inverseView(3, 1), inverseView(3, 2));
        // The history is looked up where the camera was: nothing to look up on the first frame, and a jump of the
        // camera (a teleport, a new cell) makes the old picture meaningless.
        // setParams runs on every update, but a menu or a loading screen records no frame: a long gap since the last call
        // means the previous matrices may belong to a frame the GPU never drew. A change of the grid's reach (weather,
        // a new cell, going under water) changes where the slices are, so the history no longer lines up either.
        const auto now = std::chrono::steady_clock::now();
        const bool resumed = now - mPreviousTime > std::chrono::milliseconds(250);
        mPreviousTime = now;
        const float reach = Render::Volume::farDistance(input.fogEnd);
        const bool reachChanged = reach != mPreviousReach || input.enabled != mPreviousEnabled;
        mPreviousReach = reach;
        mPreviousEnabled = input.enabled;
        const bool jumped = mHavePrevious
            && (resumed || reachChanged
                || Render::Volume::cameraJumped(mPreviousEye.x, mPreviousEye.y, mPreviousEye.z, eye.x, eye.y, eye.z));
        if (!mHavePrevious || jumped)
        {
            mPreviousView = view;
            mPreviousProjection = projection;
        }
        const vsg::mat4 toPrevious(mPreviousProjection * mPreviousView * inverseView);
        for (std::size_t i = 0; i < 16; ++i)
            input.toPreviousClip[i] = toPrevious.data()[i];
        if (mTemporal)
        {
            input.jitter = Render::Volume::jitter(mFrame);
            input.historyWeight = (mHavePrevious && !jumped) ? Render::Volume::historyWeight : 0.f;
            if (mParitySwitch)
                mParitySwitch->setSingleChildOn(mFrame % 2);
        }
        if (mBarrierSwitch)
            // The first frames put the blend and history images into their layout (a frame may be dropped, so not only
            // the first); later ones keep their contents.
            mBarrierSwitch->setSingleChildOn(mFrame < 3 ? 0 : 1);
        ++mFrame;
        mHavePrevious = true;
        mPreviousView = view;
        mPreviousProjection = projection;
        mPreviousEye = eye;

        const auto values = Render::Volume::buildParams(input);
        auto* out = reinterpret_cast<float*>(mParams->dataPointer());
        std::copy(values.begin(), values.end(), out);
        mParams->dirty();
    }
}
