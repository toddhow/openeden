#include "cloudvolume.hpp"

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
#include <components/pipeline/sets.hpp>
#include <components/render/bluenoise.hpp>

#include <components/pipeline/computebindings.hpp>
#include <components/pipeline/layout.hpp>
#include <components/pipeline/viewbindings.hpp>

namespace View
{
    namespace
    {
        constexpr VkFormat noiseFormat = VK_FORMAT_R8G8B8A8_UNORM;
        constexpr VkFormat pictureFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        // The compute set's bindings (files/shaders/comp/cloud/cloud.slang).
        constexpr std::uint32_t paramsBinding = 10;
        constexpr std::uint32_t blueNoiseBinding = 12;

        // The blue noise texture (components/render/bluenoise.hpp), made on the CPU from a fixed seed: 64 by 64 R8,
        // read with a nearest sampler that repeats. The data is uploaded when the descriptor is compiled.
        vsg::ref_ptr<vsg::DescriptorImage> createBlueNoiseDescriptor()
        {
            using namespace Render::BlueNoise;
            const std::vector<std::uint8_t> texels = generate(textureSize, defaultSeed);
            auto data
                = vsg::ubyteArray2D::create(textureSize, textureSize, vsg::Data::Properties{ VK_FORMAT_R8_UNORM });
            std::copy(texels.begin(), texels.end(), static_cast<std::uint8_t*>(data->dataPointer()));
            data->properties.dataVariance = vsg::DataVariance::STATIC_DATA;
            auto sampler = vsg::Sampler::create();
            sampler->minFilter = sampler->magFilter = VK_FILTER_NEAREST;
            sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            return vsg::DescriptorImage::create(
                sampler, data, blueNoiseBinding, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }

        vsg::ref_ptr<vsg::Image> createImage(
            VkImageType type, VkFormat format, VkExtent3D extent, VkImageUsageFlags usage = 0)
        {
            auto image = vsg::Image::create();
            image->imageType = type;
            image->format = format;
            image->extent = extent;
            image->mipLevels = 1;
            image->arrayLayers = 1;
            image->samples = VK_SAMPLE_COUNT_1_BIT;
            image->tiling = VK_IMAGE_TILING_OPTIMAL;
            image->usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | usage;
            return image;
        }

        vsg::ref_ptr<vsg::ImageView> createView(vsg::ref_ptr<vsg::Image> image)
        {
            auto view = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
            view->viewType = image->imageType == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
            return view;
        }

        vsg::ref_ptr<vsg::Sampler> createSampler(VkSamplerAddressMode mode, VkFilter filter = VK_FILTER_LINEAR)
        {
            auto sampler = vsg::Sampler::create();
            sampler->minFilter = sampler->magFilter = filter;
            sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = mode;
            return sampler;
        }

        vsg::ref_ptr<vsg::DescriptorImage> sampledDescriptor(
            vsg::ref_ptr<vsg::Image> image, uint32_t binding, VkSamplerAddressMode mode)
        {
            return vsg::DescriptorImage::create(
                vsg::ImageInfo::create(createSampler(mode), createView(image), VK_IMAGE_LAYOUT_GENERAL), binding, 0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }

        // A storage image's view is not shared with the sampled one (VUID-VkWriteDescriptorSet-descriptorType-00336).
        vsg::ref_ptr<vsg::DescriptorImage> storageDescriptor(vsg::ref_ptr<vsg::Image> image, uint32_t binding)
        {
            return vsg::DescriptorImage::create(
                vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>(), createView(image), VK_IMAGE_LAYOUT_GENERAL),
                binding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        }

        vsg::ref_ptr<vsg::BindComputePipeline> createPipeline(vsg::ref_ptr<vsg::PipelineLayout> layout,
            vsg::ref_ptr<const vsg::Options> shaderOptions, const std::string& name)
        {
            const std::string path = "comp/cloud/main." + name + ".spv";
            auto module = vsg::read_cast<vsg::ShaderModule>(path, shaderOptions);
            if (!module || module->code.empty())
            {
                Log(Debug::Error) << "Volumetric clouds: the compute shader " << path
                                  << " was not found (is slangc missing from this build?)";
                return {};
            }
            auto stage = vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT, "main", module);
            return vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout, stage));
        }
    }

    CloudVolume::CloudVolume(Render::CloudVolume::Quality quality, std::array<std::uint32_t, 2> fullSize)
        : mQuality(quality)
        , mMarchSize(Render::CloudVolume::marchExtent(fullSize[0], fullSize[1], quality))
        , mFullSize(fullSize)
    {
        using namespace Render::CloudVolume;
        mShapeImage = createImage(VK_IMAGE_TYPE_3D, noiseFormat, { shapeSize, shapeSize, shapeSize });
        mDetailImage = createImage(VK_IMAGE_TYPE_3D, noiseFormat, { detailSize, detailSize, detailSize });
        mCurlImage = createImage(VK_IMAGE_TYPE_2D, noiseFormat, { curlSize, curlSize, 1 });
        mMarchImage = createImage(VK_IMAGE_TYPE_2D, pictureFormat, { mMarchSize[0], mMarchSize[1], 1 });
        mShadowImage
            = createImage(VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, { shadowImageWidth, shadowMapSize, 1 });
        mResolveImage = createImage(VK_IMAGE_TYPE_2D, pictureFormat, { mFullSize[0], mFullSize[1], 1 });
        mHistoryImage = createImage(VK_IMAGE_TYPE_2D, pictureFormat, { mFullSize[0], mFullSize[1], 1 });

        // The dome shader reads the resolved picture at the diffuse binding (the clouds' own picture goes there).
        mResultDescriptor = sampledDescriptor(mResolveImage, 0, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        // The ground shadow map is read with texelFetch (the shader filters it itself), so the sampler is a nearest
        // one: linear filtering of a 32 bit float image is optional in Vulkan.
        mShadowDescriptor = vsg::DescriptorImage::create(
            vsg::ImageInfo::create(createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FILTER_NEAREST),
                createView(mShadowImage), VK_IMAGE_LAYOUT_GENERAL),
            Pipeline::Descriptors::VIEW_CLOUD_SHADOW_BINDING, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        mParams = vsg::vec4Array::create(paramsVec4s + curveParamsVec4s);
        for (auto& v : *mParams)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        mParams->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
    }

    bool CloudVolume::shadersAvailable(vsg::ref_ptr<const vsg::Options> shaderOptions)
    {
        bool all = true;
        for (const char* name : { "noiseshape", "noisedetail", "noisecurl", "march", "resolve", "copy", "shadow" })
        {
            const std::string path = std::string("comp/cloud/main.") + name + ".spv";
            auto module = vsg::read_cast<vsg::ShaderModule>(path, shaderOptions);
            if (!module || module->code.empty())
            {
                Log(Debug::Error) << "Volumetric clouds: the compute shader " << path
                                  << " was not found (is slangc missing from this build?); the clouds are off";
                all = false;
            }
        }
        return all;
    }

    bool CloudVolume::createCommands(
        vsg::ref_ptr<const vsg::Options> shaderOptions, vsg::ref_ptr<vsg::DescriptorSet> viewSet)
    {
        vsg::DescriptorSetLayoutBindings bindings{
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { paramsBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { blueNoiseBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };
        vsg::DescriptorSetLayouts setLayouts = Pipeline::getCompatibleDescriptorSetLayouts();
        setLayouts.emplace_back(vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings()));
        auto computeLayout = vsg::DescriptorSetLayout::create(bindings);
        setLayouts.emplace_back(computeLayout);
        auto layout = vsg::PipelineLayout::create(setLayouts, vsg::PushConstantRanges());

        auto noiseShape = createPipeline(layout, shaderOptions, "noiseshape");
        auto noiseDetail = createPipeline(layout, shaderOptions, "noisedetail");
        auto noiseCurl = createPipeline(layout, shaderOptions, "noisecurl");
        auto march = createPipeline(layout, shaderOptions, "march");
        auto resolve = createPipeline(layout, shaderOptions, "resolve");
        auto copy = createPipeline(layout, shaderOptions, "copy");
        auto shadow = createPipeline(layout, shaderOptions, "shadow");
        if (!noiseShape || !noiseDetail || !noiseCurl || !march || !resolve || !copy || !shadow)
            return false;

        auto bindViewSet
            = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, layout, Pipeline::VIEW_SET, viewSet);
        auto computeSet = vsg::DescriptorSet::create(computeLayout,
            vsg::Descriptors{ storageDescriptor(mShapeImage, 0), storageDescriptor(mDetailImage, 1),
                storageDescriptor(mCurlImage, 2), sampledDescriptor(mShapeImage, 3, VK_SAMPLER_ADDRESS_MODE_REPEAT),
                sampledDescriptor(mDetailImage, 4, VK_SAMPLER_ADDRESS_MODE_REPEAT),
                sampledDescriptor(mCurlImage, 5, VK_SAMPLER_ADDRESS_MODE_REPEAT), storageDescriptor(mMarchImage, 6),
                storageDescriptor(mResolveImage, 7),
                sampledDescriptor(mHistoryImage, 8, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
                storageDescriptor(mHistoryImage, 9),
                vsg::DescriptorBuffer::create(mParams, paramsBinding, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                storageDescriptor(mShadowImage, 11), createBlueNoiseDescriptor() });
        auto bindComputeSet
            = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, layout, Pipeline::COMPUTE_SET, computeSet);

        using Render::CloudVolume::dispatchGroups;
        using Render::CloudVolume::groupSizeXY;
        using Render::CloudVolume::noiseGroupSize3D;
        using Render::CloudVolume::shadowImageWidth;
        using Render::CloudVolume::shadowMapSize;
        auto dispatchShape = vsg::Dispatch::create(dispatchGroups(Render::CloudVolume::shapeSize, noiseGroupSize3D),
            dispatchGroups(Render::CloudVolume::shapeSize, noiseGroupSize3D),
            dispatchGroups(Render::CloudVolume::shapeSize, noiseGroupSize3D));
        auto dispatchDetail = vsg::Dispatch::create(dispatchGroups(Render::CloudVolume::detailSize, noiseGroupSize3D),
            dispatchGroups(Render::CloudVolume::detailSize, noiseGroupSize3D),
            dispatchGroups(Render::CloudVolume::detailSize, noiseGroupSize3D));
        auto dispatchCurl = vsg::Dispatch::create(dispatchGroups(Render::CloudVolume::curlSize, groupSizeXY),
            dispatchGroups(Render::CloudVolume::curlSize, groupSizeXY), 1);
        auto dispatchMarch = vsg::Dispatch::create(
            dispatchGroups(mMarchSize[0], groupSizeXY), dispatchGroups(mMarchSize[1], groupSizeXY), 1);
        auto dispatchShadow = vsg::Dispatch::create(
            dispatchGroups(shadowImageWidth, groupSizeXY), dispatchGroups(shadowMapSize, groupSizeXY), 1);
        auto dispatchFull = vsg::Dispatch::create(
            dispatchGroups(mFullSize[0], groupSizeXY), dispatchGroups(mFullSize[1], groupSizeXY), 1);

        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        // What the last frame's fragment reads (the dome reads the resolved picture) and compute writes left behind.
        auto memory = [] {
            return vsg::MemoryBarrier::create(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        };
        const VkPipelineStageFlags beforeSources = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
            | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        // The march and the resolved picture are written whole each frame, so their old contents are dropped.
        auto discard = [&](vsg::ref_ptr<vsg::Image> image) {
            return vsg::ImageMemoryBarrier::create(0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, range);
        };
        auto normal = vsg::PipelineBarrier::create(beforeSources, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, memory(),
            discard(mMarchImage), discard(mResolveImage));
        auto first = vsg::PipelineBarrier::create(beforeSources, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, memory(),
            discard(mMarchImage), discard(mResolveImage), discard(mShapeImage), discard(mDetailImage),
            discard(mCurlImage), discard(mHistoryImage));
        auto between = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            vsg::MemoryBarrier::create(
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
        auto after = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));

        // The first frames: put the images into their layout and build the noise (the build is the same every time,
        // so a frame that was dropped does no harm).
        auto firstCommands = vsg::Commands::create();
        firstCommands->addChild(first);
        firstCommands->addChild(noiseShape);
        firstCommands->addChild(dispatchShape);
        firstCommands->addChild(noiseDetail);
        firstCommands->addChild(dispatchDetail);
        firstCommands->addChild(noiseCurl);
        firstCommands->addChild(dispatchCurl);
        firstCommands->addChild(between);
        mFirstSwitch = vsg::Switch::create();
        mFirstSwitch->addChild(true, firstCommands);
        mFirstSwitch->addChild(false, normal);

        auto passes = vsg::Commands::create();
        passes->addChild(march);
        passes->addChild(dispatchMarch);
        passes->addChild(between);
        passes->addChild(resolve);
        passes->addChild(dispatchFull);
        passes->addChild(between);
        passes->addChild(copy);
        passes->addChild(dispatchFull);
        passes->addChild(after);

        // The ground shadow map is made every frame, also when the clouds are not drawn (it then says "no shadow",
        // which the fragment shaders read; a stale map would shadow the ground of a weather that keeps the vanilla
        // sky). Its texels are all written, so the old contents are dropped.
        auto shadowBarrier = vsg::PipelineBarrier::create(
            beforeSources, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, memory(), discard(mShadowImage));
        auto shadowCommands = vsg::Commands::create();
        shadowCommands->addChild(shadow);
        shadowCommands->addChild(dispatchShadow);
        shadowCommands->addChild(after);

        auto drawn = vsg::Group::create();
        drawn->addChild(mFirstSwitch);
        drawn->addChild(passes);
        mEnableSwitch = vsg::Switch::create();
        mEnableSwitch->addChild(mEnabled, drawn);

        auto group = vsg::Group::create();
        group->addChild(bindViewSet);
        group->addChild(bindComputeSet);
        group->addChild(shadowBarrier);
        group->addChild(mEnableSwitch);
        group->addChild(shadowCommands);
        mNode = group;
        return true;
    }

    void CloudVolume::setEnabled(bool enabled)
    {
        mEnabled = enabled;
        if (mEnableSwitch)
            mEnableSwitch->setAllChildren(enabled);
        if (!enabled)
            mHavePrevious = false; // the next picture starts from nothing
    }

    void CloudVolume::setParams(
        Render::CloudVolume::ParamsInput input, const vsg::dmat4& view, const vsg::dmat4& projection)
    {
        if (!mEnabled)
        {
            // The clouds are not drawn: keep the last parameters but say that nothing casts a shadow.
            mLastParams[22] = 0.f;
            auto* out = reinterpret_cast<float*>(mParams->dataPointer());
            std::copy(mLastParams.begin(), mLastParams.end(), out);
            std::copy(mLastCurve.begin(), mLastCurve.end(), out + Render::CloudVolume::paramsFloats);
            mParams->dirty();
            return;
        }
        const vsg::dmat4 inverseView = vsg::inverse(view);
        const vsg::dvec3 eye(inverseView(3, 0), inverseView(3, 1), inverseView(3, 2));
        // The history is looked up where the picture was: nothing to look up on the first frame, and a jump of the
        // camera (a teleport, a new cell), a pause (a menu or a loading screen records no frame, so the previous
        // matrices may belong to a frame the GPU never drew) or a jump of the coverage (a new weather taking over at
        // once) makes the old picture meaningless.
        const auto now = std::chrono::steady_clock::now();
        const bool resumed = now - mPreviousTime > std::chrono::milliseconds(250);
        mPreviousTime = now;
        constexpr float coverageJump = 0.2f;
        const bool jumped = mHavePrevious
            && (resumed || std::abs(input.coverage - mPreviousCoverage) > coverageJump
                || Render::Volume::cameraJumped(mPreviousEye.x, mPreviousEye.y, mPreviousEye.z, eye.x, eye.y, eye.z));
        mPreviousCoverage = input.coverage;
        if (!mHavePrevious || jumped)
        {
            mPreviousView = view;
            mPreviousProjection = projection;
        }
        // The rotation only: the view matrices without their translation.
        auto rotationOnly = [](vsg::dmat4 m) {
            m(3, 0) = m(3, 1) = m(3, 2) = 0.0;
            return m;
        };
        const vsg::mat4 toPrevious(
            mPreviousProjection * rotationOnly(mPreviousView) * vsg::inverse(rotationOnly(view)));
        for (std::size_t i = 0; i < 16; ++i)
            input.toPreviousClip[i] = toPrevious.data()[i];
        input.marchSize = mMarchSize;
        input.fullSize = mFullSize;
        input.jitter = Render::CloudVolume::rayJitter(mFrame);
        input.frame = mFrame;
        // The first frames discard the history's contents (see below), so they take nothing from it.
        input.historyWeight = (mHavePrevious && !jumped && mFrame >= 3) ? Render::CloudVolume::historyWeight : 0.f;
        // The first frames put the images into their layout and build the noise (a frame may be dropped, so not only
        // the first); later ones keep their contents.
        if (mFirstSwitch)
            mFirstSwitch->setSingleChildOn(mFrame < 3 ? 0 : 1);
        ++mFrame;
        mHavePrevious = true;
        mPreviousView = view;
        mPreviousProjection = projection;
        mPreviousEye = eye;

        const auto values = Render::CloudVolume::buildParams(input);
        mLastParams = values;
        mLastCurve = Render::CloudVolume::buildCurveParams(input);
        auto* out = reinterpret_cast<float*>(mParams->dataPointer());
        std::copy(values.begin(), values.end(), out);
        std::copy(mLastCurve.begin(), mLastCurve.end(), out + Render::CloudVolume::paramsFloats);
        mParams->dirty();
    }
}
