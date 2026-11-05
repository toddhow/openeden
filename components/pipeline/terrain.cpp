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

    std::set<std::string> terrainDefines(bool maps, unsigned int gbufferOutputs, bool giSky,
        const WetnessOptions& wetness, bool pbr, const CoverOptions& cover, unsigned int parallaxQuality,
        bool aoTerrain, bool pointShadow)
    {
        std::set<std::string> defines;
        if (maps)
            defines.insert("TERRAIN_MAPS");
        if (maps && pbr)
            defines.insert("TERRAIN_PBR");
        if (maps && parallaxQuality > 0)
        {
            defines.insert("PARALLAX_POM");
            if (parallaxQuality == 1)
                defines.insert("PARALLAX_POM_LOW");
            else if (parallaxQuality == 3)
                defines.insert("PARALLAX_POM_HIGH");
        }
        if (gbufferOutputs > 0)
            defines.insert("GBUFFER");
        // Task 0077: the point lights' shadows, in the terrain lit per pixel (with its layers' maps; a terrain lit per
        // vertex cannot take one light's share out of the sum).
        if (pointShadow && maps)
            defines.insert("POINT_SHADOW");
        // Task 0036: the terrain takes the exterior sky-visibility field.
        if (giSky)
            defines.insert("GI_SKY");
        if (wetness.enabled)
        {
            defines.insert("WETNESS");
            if (wetness.puddles)
                defines.insert("WETNESS_PUDDLES");
            if (wetness.debug)
                defines.insert("WETNESS_DEBUG");
        }
        if (cover.enabled)
        {
            defines.insert("COVER");
            if (cover.debug)
                defines.insert("COVER_DEBUG");
        }
        // The fourth extra output is the material (Render::GBuffer::extraColorFormats, task 0027).
        if (gbufferOutputs >= 4)
            defines.insert("GBUFFER_MATERIAL");
        // The fifth is the ambient part of the colour (task 0029); the terrain writes zero unless `ao terrain` is
        // on (task 0048).
        if (gbufferOutputs >= 5)
        {
            defines.insert("GBUFFER_AMBIENT");
            if (aoTerrain)
                defines.insert("AO_TERRAIN");
        }
        return defines;
    }

    vsg::ref_ptr<vsg::BindGraphicsPipeline> terrain(vsg::ref_ptr<const vsg::Options> shaderOptions, bool maps,
        unsigned int gbufferOutputs, bool giSky, const WetnessOptions& wetness, bool pbr, const CoverOptions& cover,
        unsigned int parallaxQuality, bool aoTerrain, bool pointShadow)
    {
        vsg::ShaderStages shaders = { vsgUtil::readShader("terrain/main.vert", shaderOptions),
            vsgUtil::readShader("terrain/main.frag", shaderOptions) };
        std::set<std::string> defines
            = terrainDefines(maps, gbufferOutputs, giSky, wetness, pbr, cover, parallaxQuality, aoTerrain, pointShadow);
        if (!defines.empty())
        {
            // As Pipeline::Graphics adds its modes: a copy of each stage compiled with the define.
            auto compileSettings = vsg::ShaderCompileSettings::create();
            compileSettings->defines = std::move(defines);
            for (auto& stage : shaders)
            {
                stage = vsg::ShaderStage::create(*stage);
                stage->module = vsg::ShaderModule::create(*stage->module);
                stage->module->hints = compileSettings;
            }
        }
        // Task 0072: OPENEDEN_SHADER_TRACE lists the (stage, defines) pairs a run uses. Both branches are references,
        // so no set is copied when the trace is off (stage b review).
        static const std::set<std::string> noDefines;
        std::vector<std::set<std::string>> tracedSets;
        for (std::size_t i = 0; i < shaders.size(); ++i)
        {
            const auto& hints = shaders[i]->module->hints;
            vsgUtil::recordShaderUse(i == 0 ? "terrain/main.vert" : "terrain/main.frag",
                hints ? hints->defines : noDefines, shaders[i].get(), shaderOptions);
        }
        if (vsgUtil::shaderTraceActive())
        {
            for (const auto& stage : shaders)
                tracedSets.push_back(
                    vsgUtil::stageDefines(stage->module->hints ? stage->module->hints->defines : noDefines,
                        stage->module->source, shaderOptions.get()));
        }
        // Task 0072, stage e: with `slang shaders` on, the Slang permutation compiled for exactly the set this stage
        // is drawn with (its imported defines and its settings files' defines); a set with no file keeps the GLSL. A
        // twin takes the `*.settings` values as specialization constants.
        // Stage f: both stages take their twins or neither does, since VSG compiles a pipeline's stages together when
        // one is GLSL and glslang cannot link a SPIR-V stage (vsgUtil::takeSlangTwinsTogether): a fragment set with no
        // file keeps the vertex stage's GLSL too.
        bool slang = false;
        if (vsgUtil::prefersSlang(shaderOptions.get()))
        {
            std::vector<vsg::ref_ptr<vsg::ShaderModule>> twins(shaders.size());
            vsg::ShaderStages copies;
            for (std::size_t i = 0; i < shaders.size(); ++i)
            {
                const auto& hints = shaders[i]->module->hints;
                twins[i] = vsgUtil::slangTwin(i == 0 ? "terrain/main.vert" : "terrain/main.frag",
                    hints ? hints->defines : noDefines, shaders[i]->module->source, true, shaderOptions);
                copies.push_back(vsg::ShaderStage::create(*shaders[i]));
            }
            if (vsgUtil::takeSlangTwinsTogether(copies, twins))
            {
                for (auto& twin : copies)
                    vsgUtil::addSettingsConstants(*twin, shaderOptions.get());
                shaders = copies;
                slang = true;
            }
        }
        // Review of stage f: the pipeline's pair of sets, and which compiler draws it, in the trace.
        if (!tracedSets.empty())
            vsgUtil::recordPipelineUse("terrain", tracedSets[0], tracedSets[1], slang);

        vsg::DescriptorSetLayoutBindings descriptorBindings{ { Pipeline::Descriptors::BLENDMAP_BINDING,
                                                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                                 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Pipeline::Descriptors::LAYER_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            // Task 0087: the fragment stage reads the batch's fade (a world LOD tile dithering in or out).
            { Pipeline::Descriptors::BATCH_DATA_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
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
