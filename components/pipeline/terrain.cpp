#include "terrain.hpp"

#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/state/VertexInputState.h>

#include <components/vsgutil/readshader.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include "gbufferblend.hpp"
#include "layout.hpp"

namespace Pipeline
{
    namespace Descriptors
    {
#include <files/shaders/terrain/bindings.glsl>
    }

    vsg::ref_ptr<vsg::BindGraphicsPipeline> terrain(vsg::ref_ptr<const vsg::Options> shaderOptions, bool maps,
        unsigned int gbufferOutputs, bool giSky, const WetnessOptions& wetness, bool pbr, const CoverOptions& cover,
        unsigned int parallaxQuality, bool aoTerrain)
    {
        vsg::ShaderStages shaders = { vsgUtil::readShader("terrain/main.vert", shaderOptions),
            vsgUtil::readShader("terrain/main.frag", shaderOptions) };
        if (maps || gbufferOutputs > 0 || giSky || wetness.enabled || cover.enabled || (maps && parallaxQuality > 0))
        {
            // As Pipeline::Graphics adds its modes: a copy of each stage compiled with the define.
            auto compileSettings = vsg::ShaderCompileSettings::create();
            if (maps)
                compileSettings->defines.insert("TERRAIN_MAPS");
            if (maps && pbr)
                compileSettings->defines.insert("TERRAIN_PBR");
            if (maps && parallaxQuality > 0)
            {
                compileSettings->defines.insert("PARALLAX_POM");
                if (parallaxQuality == 1)
                    compileSettings->defines.insert("PARALLAX_POM_LOW");
                else if (parallaxQuality == 3)
                    compileSettings->defines.insert("PARALLAX_POM_HIGH");
            }
            if (gbufferOutputs > 0)
                compileSettings->defines.insert("GBUFFER");
            // Task 0036: the terrain takes the exterior sky-visibility field.
            if (giSky)
                compileSettings->defines.insert("GI_SKY");
            if (wetness.enabled)
            {
                compileSettings->defines.insert("WETNESS");
                if (wetness.puddles)
                    compileSettings->defines.insert("WETNESS_PUDDLES");
                if (wetness.debug)
                    compileSettings->defines.insert("WETNESS_DEBUG");
            }
            if (cover.enabled)
            {
                compileSettings->defines.insert("COVER");
                if (cover.debug)
                    compileSettings->defines.insert("COVER_DEBUG");
            }
            // The fourth extra output is the material (Render::GBuffer::extraColorFormats, task 0027).
            if (gbufferOutputs >= 4)
                compileSettings->defines.insert("GBUFFER_MATERIAL");
            // The fifth is the ambient part of the colour (task 0029); the terrain writes zero unless `ao terrain` is
            // on (task 0048).
            if (gbufferOutputs >= 5)
            {
                compileSettings->defines.insert("GBUFFER_AMBIENT");
                if (aoTerrain)
                    compileSettings->defines.insert("AO_TERRAIN");
            }
            for (auto& stage : shaders)
            {
                stage = vsg::ShaderStage::create(*stage);
                stage->module = vsg::ShaderModule::create(*stage->module);
                stage->module->hints = compileSettings;
            }
        }
        // Task 0072: OPENEDEN_SHADER_TRACE lists the (stage, defines) pairs a run uses.
        for (std::size_t i = 0; i < shaders.size(); ++i)
        {
            const auto& hints = shaders[i]->module->hints;
            vsgUtil::recordShaderUse(i == 0 ? "terrain/main.vert" : "terrain/main.frag",
                hints ? hints->defines : std::set<std::string>{}, shaders[i].get(), shaderOptions);
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
            if (pbr)
            {
                descriptorBindings.push_back({ Pipeline::Descriptors::MATERIAL_LAYER_BINDING,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
                descriptorBindings.push_back({ Pipeline::Descriptors::LAYER_SCALES_BINDING,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
            }
        }

        if (maps && parallaxQuality > 0)
        {
            descriptorBindings.push_back({ Pipeline::Descriptors::HEIGHT_LAYER_BINDING,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
            descriptorBindings.push_back({ Pipeline::Descriptors::LAYER_HEIGHTS_BINDING,
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
        vsg::ref_ptr<vsg::ColorBlendState> colorBlendState;
        if (gbufferOutputs > 0)
            colorBlendState
                = GBufferBlendState::create(colorBlendAttachments, colorBlendAttachment, 1 + gbufferOutputs);
        else
            colorBlendState = vsg::ColorBlendState::create(colorBlendAttachments);

        vsg::GraphicsPipelineStates pipelineStates{ vsg::VertexInputState::create(), vsg::InputAssemblyState::create(),
            vsg::RasterizationState::create(), vsg::MultisampleState::create(), colorBlendState,
            vsg::DepthStencilState::create() };

        return vsg::BindGraphicsPipeline::create(
            vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates));
    }
}
