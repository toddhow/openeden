#include "terrain.hpp"

#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/state/VertexInputState.h>

#include <components/vsgutil/readshader.hpp>

#include "layout.hpp"

namespace Pipeline
{
    namespace Descriptors
    {
#include <files/shaders/terrain/bindings.glsl>
    }

    vsg::ref_ptr<vsg::BindGraphicsPipeline> terrain(vsg::ref_ptr<const vsg::Options> shaderOptions, bool maps)
    {
        vsg::ShaderStages shaders = { vsgUtil::readShader("terrain/main.vert", shaderOptions),
            vsgUtil::readShader("terrain/main.frag", shaderOptions) };
        if (maps)
        {
            // As Pipeline::Graphics adds its modes: a copy of each stage compiled with the define.
            auto compileSettings = vsg::ShaderCompileSettings::create();
            compileSettings->defines.insert("TERRAIN_MAPS");
            for (auto& stage : shaders)
            {
                stage = vsg::ShaderStage::create(*stage);
                stage->module = vsg::ShaderModule::create(*stage->module);
                stage->module->hints = compileSettings;
            }
        }

        vsg::DescriptorSetLayoutBindings descriptorBindings{ { Pipeline::Descriptors::BLENDMAP_BINDING,
                                                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                                 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Pipeline::Descriptors::LAYER_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Pipeline::Descriptors::BATCH_DATA_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { Pipeline::Descriptors::HEIGHTMAP_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { Pipeline::Descriptors::NORMALMAP_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { Pipeline::Descriptors::COLORMAP_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } };
        if (maps)
        {
            descriptorBindings.push_back({ Pipeline::Descriptors::NORMAL_LAYER_BINDING,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
            descriptorBindings.push_back({ Pipeline::Descriptors::LAYER_FLAGS_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
        }

        vsg::DescriptorSetLayouts descriptorSetLayouts = getCompatibleDescriptorSetLayouts();
        descriptorSetLayouts.emplace_back(vsg::DescriptorSetLayout::create(descriptorBindings));

        vsg::PushConstantRanges pushConstantRanges{ { VK_SHADER_STAGE_VERTEX_BIT, 0,
            getCompatiblePushConstantSize() } };

        auto pipelineLayout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);

        vsg::ColorBlendState::ColorBlendAttachments colorBlendAttachments;
        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.blendEnable = false;
        colorBlendAttachment.colorWriteMask
            = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments.push_back(colorBlendAttachment);

        vsg::GraphicsPipelineStates pipelineStates{ vsg::VertexInputState::create(), vsg::InputAssemblyState::create(),
            vsg::RasterizationState::create(), vsg::MultisampleState::create(),
            vsg::ColorBlendState::create(colorBlendAttachments), vsg::DepthStencilState::create() };

        return vsg::BindGraphicsPipeline::create(
            vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates));
    }
}
