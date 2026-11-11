#include "layout.hpp"

#include "lightstages.hpp"
#include "viewbindings.hpp"

namespace Pipeline
{
    vsg::DescriptorSetLayouts getCompatibleDescriptorSetLayouts()
    {
        static const auto objectLayout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } });
        return { getViewDescriptorSetLayout(), objectLayout };
    }

    vsg::ref_ptr<vsg::DescriptorSetLayout> getViewDescriptorSetLayout()
    {
        static const auto sceneLayout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
            { Descriptors::VIEW_SCENE_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                lightStages | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { Descriptors::VIEW_ENV_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::VIEW_LIGHTS_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                lightStages | VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { Descriptors::VIEW_LIGHT_GRID_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                lightStages | VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { Descriptors::VIEW_LIGHT_INDICES_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                lightStages | VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { Descriptors::VIEW_LIGHT_GRID_SIZE_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                lightStages | VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            // OpenEden (task 0041): the volumetric fog's fill pass looks the sun's shadow up from a compute shader.
            { Descriptors::VIEW_SHADOW_MAP_BINDING, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { Descriptors::VIEW_SHADOW_SAMPLER_BINDING, VK_DESCRIPTOR_TYPE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { Descriptors::VIEW_REFLECT_MAP_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Descriptors::VIEW_REFLECT_DEPTH_MAP_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            // OpenEden (task 0028): the baked light probes.
            { Descriptors::VIEW_PROBE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            // OpenEden (task 0028, stage d): the baked reflection probes.
            { Descriptors::VIEW_REFLECTION_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            // OpenEden (task 0036): the exterior sky-visibility field and its header.
            { Descriptors::VIEW_SKYVIS_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::VIEW_SKYVIS_SKY_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            // OpenEden (task 0041): the volumetric fog's volume (read by the forward shaders) and its parameters (also
            // read by the compute passes that fill it).
            { Descriptors::VIEW_VOLUME_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Descriptors::VIEW_VOLUME_PARAMS_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            // OpenEden (task 0042): the volumetric clouds' ground shadow map.
            { Descriptors::VIEW_CLOUD_SHADOW_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            // OpenEden (task 0077): the point lights' shadow maps, their comparison sampler and their data.
            { Descriptors::VIEW_POINT_SHADOW_MAP_BINDING, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Descriptors::VIEW_POINT_SHADOW_SAMPLER_BINDING, VK_DESCRIPTOR_TYPE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Descriptors::VIEW_POINT_SHADOW_DATA_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            // OpenEden (task 0095): the baked puddle map's window of cells.
            { Descriptors::VIEW_PUDDLE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr } });
        return sceneLayout;
    }

    vsg::ref_ptr<vsg::PipelineLayout> getCompatiblePipelineLayout()
    {
        static const auto layout = vsg::PipelineLayout::create(getCompatibleDescriptorSetLayouts(),
            vsg::PushConstantRanges{ { VK_SHADER_STAGE_VERTEX_BIT, 0, getCompatiblePushConstantSize() } });
        return layout;
    }
}
