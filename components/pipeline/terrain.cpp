#include "terrain.hpp"

#include <vsg/core/Value.h>
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
        bool aoTerrain, bool pointShadow, bool ambientCube)
    {
        std::set<std::string> defines;
        // Task 0355 stage k: the terrain is lit by the ambient cube of the weather (a game pack's exterior); the cube's
        // header buffer says per frame whether one is loaded.
        if (ambientCube)
            defines.insert("AMBIENT_CUBE");
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

    vsg::ShaderStage::SpecializationConstants terrainConstants(bool maps, unsigned int gbufferOutputs, bool giSky,
        const WetnessOptions& wetness, bool pbr, const CoverOptions& cover, unsigned int parallaxQuality,
        bool aoTerrain, bool pointShadow, bool ambientCube)
    {
        // Worked out from the define set itself, so the two forms cannot disagree (the test compares them too).
        const std::set<std::string> defines = terrainDefines(
            maps, gbufferOutputs, giSky, wetness, pbr, cover, parallaxQuality, aoTerrain, pointShadow, ambientCube);
        const auto flag = [&](const char* name) { return vsg::Value<VkBool32>::create(defines.count(name) ? VK_TRUE : VK_FALSE); };
        vsg::ShaderStage::SpecializationConstants constants;
        constants[TerrainConstantId::GiSky] = flag("GI_SKY");
        constants[TerrainConstantId::Wetness] = flag("WETNESS");
        constants[TerrainConstantId::WetnessPuddles] = flag("WETNESS_PUDDLES");
        constants[TerrainConstantId::WetnessDebug] = flag("WETNESS_DEBUG");
        constants[TerrainConstantId::Cover] = flag("COVER");
        constants[TerrainConstantId::CoverDebug] = flag("COVER_DEBUG");
        constants[TerrainConstantId::AmbientCube] = flag("AMBIENT_CUBE");
        constants[TerrainConstantId::PointShadow] = flag("POINT_SHADOW");
        constants[TerrainConstantId::AoTerrain] = flag("AO_TERRAIN");
        // lib/material/parallax.slang: low, high, and medium (also the value when parallax is off).
        int minSteps = 10, maxSteps = 20, refineTaps = 4;
        if (defines.count("PARALLAX_POM_LOW"))
        {
            minSteps = 8;
            maxSteps = 12;
            refineTaps = 0;
        }
        else if (defines.count("PARALLAX_POM_HIGH"))
        {
            minSteps = 16;
            maxSteps = 48;
        }
        constants[TerrainConstantId::PomMinSteps] = vsg::intValue::create(minSteps);
        constants[TerrainConstantId::PomMaxSteps] = vsg::intValue::create(maxSteps);
        constants[TerrainConstantId::PomRefineTaps] = vsg::intValue::create(refineTaps);
        return constants;
    }

    vsg::ref_ptr<vsg::BindGraphicsPipeline> terrain(vsg::ref_ptr<const vsg::Options> shaderOptions, bool maps,
        unsigned int gbufferOutputs, bool giSky, const WetnessOptions& wetness, bool pbr, const CoverOptions& cover,
        unsigned int parallaxQuality, bool aoTerrain, bool pointShadow, bool ambientCube)
    {
        vsg::ShaderStages shaders = { vsgUtil::readShader("terrain/main.vert", shaderOptions),
            vsgUtil::readShader("terrain/main.frag", shaderOptions) };
        std::set<std::string> defines
            = terrainDefines(maps, gbufferOutputs, giSky, wetness, pbr, cover, parallaxQuality, aoTerrain, pointShadow,
                ambientCube);
        if (!defines.empty())
        {
            // As Pipeline::Graphics adds its modes: the stages carry the requested defines (task 0363, stage g: a module
            // of its own with no code, vsgUtil::readShader).
            auto compileSettings = vsg::ShaderCompileSettings::create();
            compileSettings->defines = std::move(defines);
            for (auto& stage : shaders)
                stage->module->hints = compileSettings;
        }
        static const std::set<std::string> noDefines;
        const std::array<const char*, 2> stageFiles = { "terrain/main.vert", "terrain/main.frag" };
        std::vector<std::set<std::string>> tracedSets;
        for (std::size_t i = 0; i < shaders.size(); ++i)
        {
            const auto& hints = shaders[i]->module->hints;
            // Task 0072: OPENEDEN_SHADER_TRACE lists the (stage, defines) pairs a run uses.
            vsgUtil::recordShaderUse(stageFiles[i], hints ? hints->defines : noDefines, shaderOptions);
        }
        // Task 0072, stage e: the Slang permutation compiled for exactly the set this stage is drawn with (its
        // imported defines and its settings files' defines); a twin takes the `*.settings` values as specialization
        // constants. Task 0363, stage g: there is no GLSL to fall back on; a set with no permutation is waited for
        // (compiled at run time), and a stage that still has none gives an error: the terrain is not drawn.
        std::vector<vsg::ref_ptr<vsg::ShaderModule>> twins(shaders.size());
        for (std::size_t i = 0; i < shaders.size(); ++i)
        {
            const auto& hints = shaders[i]->module->hints;
            const auto& requested = hints ? hints->defines : noDefines;
            twins[i] = vsgUtil::slangTwin(stageFiles[i], requested, shaderOptions);
            if (vsgUtil::shaderTraceActive())
            {
                if (const auto set = vsgUtil::slangSelectionSet(stageFiles[i], requested, shaderOptions))
                    tracedSets.push_back(*set);
            }
        }
        if (!vsgUtil::takeSlangTwinsTogether(shaders, twins))
        {
            for (std::size_t i = 0; i < shaders.size(); ++i)
            {
                if (!twins[i])
                {
                    const auto& hints = shaders[i]->module->hints;
                    throw std::runtime_error(
                        vsgUtil::missingTwinText(stageFiles[i], hints ? hints->defines : noDefines, shaderOptions));
                }
            }
        }
        // Task 0363 (stage c): a twin that reads the terrain's defines as constants (MODE_CONSTANTS) gets their values;
        // a twin made with defines does not declare these ids, which Vulkan ignores, so they are only given when the
        // switch is on.
        if (const auto* constants = shaderOptions->getObject<vsgUtil::SettingsConstants>(vsgUtil::settingsConstantsKey);
            constants != nullptr && constants->constantDefines)
        {
            const auto values = terrainConstants(maps, gbufferOutputs, giSky, wetness, pbr, cover, parallaxQuality,
                aoTerrain, pointShadow, ambientCube);
            for (auto& twin : shaders)
            {
                for (const auto& [id, value] : values)
                    twin->specializationConstants[id] = value;
            }
        }
        for (auto& twin : shaders)
            vsgUtil::addSettingsConstants(*twin, shaderOptions.get());
        // Review of stage f: the pipeline's pair of sets in the trace.
        if (tracedSets.size() == 2)
            vsgUtil::recordPipelineUse("terrain", tracedSets[0], tracedSets[1], true);

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
