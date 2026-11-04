#include "worldoverlay.hpp"

#include <components/vsgutil/readshader.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include "sets.hpp"

namespace Pipeline
{
    vsg::ref_ptr<vsg::BindComputePipeline> worldOverlay(vsg::ref_ptr<const vsg::Options> shaderOptions)
    {
        VkShaderStageFlags computeStage = VK_SHADER_STAGE_COMPUTE_BIT;
        vsg::DescriptorSetLayoutBindings textureBindings{
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, computeStage, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, computeStage, nullptr },
        };
        vsg::DescriptorSetLayoutBindings computeBindings{
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, computeStage, nullptr },
        };
        vsg::DescriptorSetLayouts descriptorSetLayouts;
        for (int i = 0; i < Pipeline::TEXTURE_SET; ++i)
            descriptorSetLayouts.emplace_back(vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings()));
        descriptorSetLayouts.emplace_back(vsg::DescriptorSetLayout::create(textureBindings));
        descriptorSetLayouts.emplace_back(vsg::DescriptorSetLayout::create(computeBindings));

        vsg::PushConstantRanges pushConstantRanges{ { computeStage, 0, sizeof(vsg::vec4) } };
        auto layout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);

        // Task 0072, stage c: the Slang twin when `[Rendering] slang shaders` is on and it was built.
        const std::string path = "comp/worldoverlay/update.comp";
        auto shaderStage = vsgUtil::withSlangTwin(vsgUtil::readShader(path, shaderOptions), path, shaderOptions);
        return vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout, shaderStage));
    }
}
