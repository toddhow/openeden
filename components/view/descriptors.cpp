#include "descriptors.hpp"

#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/DescriptorSet.h>

#include <components/pipeline/layout.hpp>
#include <components/pipeline/viewbindings.hpp>
#include <components/render/cloudgpu.hpp>
#include <components/render/depth.hpp>
#include <components/render/pointshadowgpu.hpp>
#include <components/render/probegpu.hpp>
#include <components/render/puddlemapgpu.hpp>
#include <components/render/reflectiongpu.hpp>
#include <components/render/skyvisgpu.hpp>
#include <components/render/volumegpu.hpp>
#include <components/vsgutil/share.hpp>

#include "gridsize.hpp"

namespace
{
    vsg::ref_ptr<vsg::uintArray> createLightGrid(int maxLights, int numClusters)
    {
        auto array = vsg::uintArray::create(numClusters * 2);
        for (size_t i = 0; i < array->size(); ++i)
            array->at(i) = (i % 2 == 0) ? 0 : maxLights;
        return array;
    }

    vsg::ref_ptr<vsg::uintArray> createLightIndices(int maxLights, int numClusters, bool setDefaults)
    {
#include <files/shaders/lib/light/maxlights.glsl>
        int maxPer = std::min(maxLights, maxLightsPerCluster);
        auto array = vsg::uintArray::create(numClusters * maxPer);
        if (setDefaults)
        {
            for (size_t i = 0; i < array->size(); ++i)
                array->at(i) = i;
        }
        return array;
    }
}
namespace View
{
    vsg::ref_ptr<vsg::Descriptor> dummyEnvMap()
    {
        auto sampler = vsgUtil::shareDefault<vsg::Sampler>();
        auto image = vsg::ubvec4Array3D::create(1, 1, Pipeline::Descriptors::VIEW_ENV_COUNT, vsg::ubvec4(0, 0, 0, 0),
            vsg::Data::Properties(VK_FORMAT_R8G8B8A8_UNORM));
        image->properties.imageViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        return vsg::DescriptorImage::create(
            sampler, image, Pipeline::Descriptors::VIEW_ENV_BINDING, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyShadowMap()
    {
        auto image = vsg::floatArray3D::create(1, 1, 1, Render::farDepth, vsg::Data::Properties(VK_FORMAT_D32_SFLOAT));
        image->properties.imageViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        return vsg::DescriptorImage::create(vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>(), image),
            Pipeline::Descriptors::VIEW_SHADOW_MAP_BINDING, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyShadowSampler()
    {
        auto sampler = vsg::Sampler::create();
        sampler->minFilter = sampler->magFilter = VK_FILTER_NEAREST;
        return vsg::DescriptorImage::create(vsg::ImageInfo::create(sampler, vsg::ref_ptr<vsg::ImageView>()),
            Pipeline::Descriptors::VIEW_SHADOW_SAMPLER_BINDING, 0, VK_DESCRIPTOR_TYPE_SAMPLER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyReflectDepth()
    {
        auto sampler = vsgUtil::shareDefault<vsg::Sampler>();
        auto image = vsg::floatArray2D::create(1, 1, Render::farDepth, vsg::Data::Properties(VK_FORMAT_D32_SFLOAT));
        return vsg::DescriptorImage::create(sampler, image, Pipeline::Descriptors::VIEW_REFLECT_DEPTH_MAP_BINDING, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyProbes()
    {
        auto data = vsg::vec4Array::create(Render::Gi::headerVec4s);
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_PROBE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> probeBuffer(vsg::ref_ptr<vsg::vec4Array>& data)
    {
        data
            = vsg::vec4Array::create(Render::Gi::headerVec4s + Render::Gi::vec4sPerProbe * Render::Gi::maxVolumeProbes);
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        data->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_PROBE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyReflections()
    {
        auto data = vsg::vec4Array::create(Render::Gi::reflectionHeaderVec4s);
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_REFLECTION_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> reflectionBuffer(vsg::ref_ptr<vsg::vec4Array>& data)
    {
        data = vsg::vec4Array::create(Render::Gi::reflectionBufferCapacity);
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        data->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_REFLECTION_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummySkyVis()
    {
        auto data = vsg::vec4Array::create(1);
        (*data)[0] = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_SKYVIS_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> skyVisBuffer(vsg::ref_ptr<vsg::vec4Array>& data)
    {
        data = vsg::vec4Array::create(Render::Gi::skyPoolVec4s);
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        data->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_SKYVIS_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummySkyVisSky()
    {
        // OpenEden (task 0038): the header is followed by the cloud shadow data (Render::Cloud, cloudgpu.hpp).
        auto data = vsg::vec4Array::create(Render::Cloud::headerTotalVec4s);
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_SKYVIS_SKY_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> skyVisSkyBuffer(vsg::ref_ptr<vsg::vec4Array>& data)
    {
        data = vsg::vec4Array::create(Render::Cloud::headerTotalVec4s);
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        data->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_SKYVIS_SKY_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyVolume()
    {
        auto sampler = vsg::Sampler::create();
        sampler->minFilter = sampler->magFilter = VK_FILTER_LINEAR;
        sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        auto image = vsg::vec4Array3D::create(
            1, 1, 1, vsg::vec4(0.f, 0.f, 0.f, 1.f), vsg::Data::Properties(VK_FORMAT_R32G32B32A32_SFLOAT));
        image->properties.imageViewType = VK_IMAGE_VIEW_TYPE_3D;
        return vsg::DescriptorImage::create(
            sampler, image, Pipeline::Descriptors::VIEW_VOLUME_BINDING, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyVolumeParams()
    {
        auto data = vsg::vec4Array::create(Render::Volume::paramsVec4s);
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_VOLUME_PARAMS_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyCloudShadow()
    {
        auto sampler = vsg::Sampler::create();
        sampler->minFilter = sampler->magFilter = VK_FILTER_NEAREST;
        sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        auto image = vsg::vec4Array2D::create(
            1, 1, vsg::vec4(0.f, 0.f, 0.f, 0.f), vsg::Data::Properties(VK_FORMAT_R32G32B32A32_SFLOAT));
        return vsg::DescriptorImage::create(sampler, image, Pipeline::Descriptors::VIEW_CLOUD_SHADOW_BINDING, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyPointShadowMap()
    {
        auto image = vsg::floatArray3D::create(1, 1, 1, Render::farDepth, vsg::Data::Properties(VK_FORMAT_D32_SFLOAT));
        image->properties.imageViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        return vsg::DescriptorImage::create(vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>(), image),
            Pipeline::Descriptors::VIEW_POINT_SHADOW_MAP_BINDING, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyPointShadowSampler()
    {
        auto sampler = vsg::Sampler::create();
        sampler->minFilter = sampler->magFilter = VK_FILTER_NEAREST;
        sampler->compareEnable = VK_TRUE;
        sampler->compareOp = VK_COMPARE_OP_GREATER;
        return vsg::DescriptorImage::create(vsg::ImageInfo::create(sampler, vsg::ref_ptr<vsg::ImageView>()),
            Pipeline::Descriptors::VIEW_POINT_SHADOW_SAMPLER_BINDING, 0, VK_DESCRIPTOR_TYPE_SAMPLER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyPointShadowData()
    {
        // The header, one light entry that says "no slot", and the slots; the shader never reads past the entry of a
        // light that does not exist (the count of lights it is given is 0 when nothing is lit).
        auto data = vsg::vec4Array::create(Render::PointShadow::bufferVec4s(1));
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        (*data)[Render::PointShadow::lightEntryOffset()] = vsg::vec4(-1.f, 0.f, 0.f, 0.f);
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_POINT_SHADOW_DATA_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> dummyPuddleMap()
    {
        // The header and the cell table, all zero: the shader reads "no map loaded" and draws the old puddles.
        auto data = vsg::uintArray::create(Render::PuddleMap::dataBase);
        for (auto& v : *data)
            v = 0;
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_PUDDLE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> puddleMapBuffer(vsg::ref_ptr<vsg::uintArray>& data)
    {
        data = vsg::uintArray::create(Render::PuddleMap::bufferWordsWithTops(
            Render::PuddleMap::windowSide, Render::PuddleMap::maxTexelsPerSide));
        for (auto& v : *data)
            v = 0;
        data->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_PUDDLE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    vsg::ref_ptr<vsg::Descriptor> volumeParamsBuffer(vsg::ref_ptr<vsg::vec4Array>& data)
    {
        data = vsg::vec4Array::create(Render::Volume::paramsVec4s);
        for (auto& v : *data)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        data->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        return vsg::DescriptorBuffer::create(
            data, Pipeline::Descriptors::VIEW_VOLUME_PARAMS_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }

    vsg::ref_ptr<vsg::DescriptorSet> createViewDescriptorSet(const vsg::Descriptors& descriptors)
    {
        return vsg::DescriptorSet::create(Pipeline::getViewDescriptorSetLayout(), descriptors);
    }

    vsg::Descriptors createLightingDescriptors(vsg::ref_ptr<vsg::Data> lightData, bool compute)
    {
        size_t maxLights = std::max<size_t>(
            1, lightData->valueCount() / 2); // vsgopenmw-fixme(dont-repeat-yourself(light-data-size))
        vsg::uivec3 gridSize = { gridSizeX, gridSizeY, gridSizeZ };
        if (maxLights <= 1)
            gridSize = { 1, 1, 1 };
        int numClusters = gridSize.x * gridSize.y * gridSize.z;

        auto lightGrid = vsg::DescriptorBuffer::create(createLightGrid(maxLights, numClusters),
            Pipeline::Descriptors::VIEW_LIGHT_GRID_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto lightIndices = vsg::DescriptorBuffer::create(createLightIndices(maxLights, numClusters, !compute),
            Pipeline::Descriptors::VIEW_LIGHT_INDICES_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        auto lightDescriptor = vsg::DescriptorBuffer::create(
            lightData, Pipeline::Descriptors::VIEW_LIGHTS_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto gridSizeDescriptor = vsg::DescriptorBuffer::create(vsg::uivec4Value::create(vsg::uivec4(gridSize, 0)),
            Pipeline::Descriptors::VIEW_LIGHT_GRID_SIZE_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        return vsg::Descriptors{ lightDescriptor, gridSizeDescriptor, lightGrid, lightIndices };
    }
}
