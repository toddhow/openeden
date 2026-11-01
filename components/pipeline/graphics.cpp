#include "graphics.hpp"

#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/VertexInputState.h>

#include <vsg/io/read.h>

#include <components/debug/debuglog.hpp>
#include <components/vsgutil/readshader.hpp>

#include "bindings.hpp"
#include "gbufferblend.hpp"
#include "layout.hpp"

namespace Pipeline
{
    std::string getDefine(Mode mode)
    {
        switch (mode)
        {
            case Mode::MATERIAL:
                return "MATERIAL";
            case Mode::TEXMAT:
                return "TEXMAT";
            case Mode::DIFFUSE_MAP:
                return "DIFFUSE_MAP";
            case Mode::DARK_MAP:
                return "DARK_MAP";
            case Mode::DETAIL_MAP:
                return "DETAIL_MAP";
            case Mode::DECAL_MAP:
                return "DECAL_MAP";
            case Mode::GLOW_MAP:
                return "GLOW_MAP";
            case Mode::BUMP_MAP:
                return "BUMP_MAP";
            case Mode::ENV_MAP:
                return "ENV_MAP";
            case Mode::BILLBOARD:
                return "BILLBOARD";
            case Mode::PARTICLE:
                return "PARTICLE";
            case Mode::MORPH:
                return "MORPH";
            case Mode::VERTEX:
                return "VERTEX";
            case Mode::NORMAL:
                return "NORMAL";
            case Mode::COLOR:
                return "COLOR";
            case Mode::TEXCOORD:
                return "TEXCOORD";
            case Mode::SKIN:
                return "SKIN";
            case Mode::INSTANCE:
                return "INSTANCE";
            case Mode::NORMAL_MAP:
                return "NORMAL_MAP";
            case Mode::PARALLAX:
                return "PARALLAX";
            case Mode::RECONSTRUCT_NORMAL_Z:
                return "RECONSTRUCT_NORMAL_Z";
            case Mode::SPECULAR_MAP:
                return "SPECULAR_MAP";
            case Mode::ALPHA_TO_COVERAGE:
                return "ALPHA_TO_COVERAGE";
            case Mode::ADJUST_COVERAGE:
                return "ADJUST_COVERAGE";
            case Mode::SOFT_PARTICLE:
                return "SOFT_PARTICLE";
            case Mode::PARTICLE_OCCLUSION:
                return "PARTICLE_OCCLUSION";
            case Mode::GBUFFER:
                return "GBUFFER";
        }
        return "";
    }

    CoverageModes coverageModes(const Options& key, bool alphaToCoverage, bool adjustCoverage)
    {
        // OpenMW's ShaderVisitor::createProgram: only when the alpha func is not ALWAYS and there is no blending.
        const bool applies
            = key.alphaTestMode != 0 && !key.blend && key.shader.path.empty() && key.shader.hasMode(Mode::MATERIAL);
        return { applies && alphaToCoverage, applies && adjustCoverage };
    }

    Graphics::Graphics(vsg::ref_ptr<const vsg::Options> readShaderOptions)
        : mShaderOptions(readShaderOptions)
    {
        /*
         * A fixed layout may allow the implementation to cheaply switch pipelines without reprogramming the bindings.
         */
        vsg::DescriptorSetLayoutBindings descriptorBindings = {
            { Descriptors::MATERIAL_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::PARTICLE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
                nullptr },
            { Descriptors::MORPH_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { Descriptors::MORPH_WEIGHTS_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
                nullptr },
            { Descriptors::BONE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { Descriptors::DIFFUSE_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::DARK_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::DETAIL_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::DECAL_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::GLOW_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::BUMP_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::ENV_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::NORMAL_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::SPECULAR_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::OCCLUSION_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr },
            { Descriptors::TEXMAT_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr }
        };

        vsg::DescriptorSetLayouts descriptorSetLayouts = getCompatibleDescriptorSetLayouts();
        descriptorSetLayouts.emplace_back(vsg::DescriptorSetLayout::create(descriptorBindings));

        vsg::PushConstantRanges pushConstantRanges{ { VK_SHADER_STAGE_VERTEX_BIT, 0,
            getCompatiblePushConstantSize() } };

        mLayout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);
    }

    Graphics::~Graphics() {}

    vsg::ref_ptr<vsg::ShaderStage> Graphics::createShaderStage(
        const ShaderOptions& key, const std::string& suffix) const
    {
        auto compileSettings = vsg::ShaderCompileSettings::create();
        for (size_t i = 0; i < key.modes.size(); ++i)
        {
            if (key.modes[i])
                compileSettings->defines.insert(getDefine(static_cast<Mode>(i)));
        }
        auto path = key.path;
        if (path.empty())
            path = "default";
        auto shaderStage = vsgUtil::readShader(path + suffix, mShaderOptions);
        // if (!compileSettings->defines.empty())
        {
            shaderStage = vsg::ShaderStage::create(*shaderStage);
            shaderStage->module = vsg::ShaderModule::create(*shaderStage->module);
            shaderStage->module->hints = compileSettings;
        }
        if (preferSlang)
        {
            const std::string spirvPath = path + suffix + ".spv";
            // The reader finds the file along the shader options' paths; read_cast gives null if there is none.
            if (auto precompiled = vsg::read_cast<vsg::ShaderModule>(spirvPath, mShaderOptions);
                precompiled && !precompiled->code.empty())
            {
                shaderStage->module = precompiled;
                Log(Debug::Verbose) << "Using the Slang shader " << spirvPath;
            }
        }
        return shaderStage;
    }

    vsg::ref_ptr<vsg::ShaderStage> Graphics::create(const FragmentStageOptions& key) const
    {
        return createShaderStage(key, "/main.frag");
    }

    vsg::ref_ptr<vsg::ShaderStage> Graphics::create(const VertexStageOptions& key) const
    {
        return createShaderStage(key, "/main.vert");
    }

    vsg::ref_ptr<vsg::BindGraphicsPipeline> Graphics::create(const Options& key) const
    {
        VertexStageOptions vertexStageOptions{ key.shader.path, {} };
        for (auto mode : vertexModes)
        {
            if (key.shader.hasMode(mode))
                vertexStageOptions.addMode(mode);
        }
        auto vertexStage = mVertexStageCache.getOrCreate(vertexStageOptions, *this);

        FragmentStageOptions fragmentStageOptions{ key.shader.path, {} };
        for (auto mode : fragmentModes)
        {
            if (key.shader.hasMode(mode))
                fragmentStageOptions.addMode(mode);
        }
        const CoverageModes coverage = coverageModes(key, alphaToCoverage, adjustCoverage);
        if (coverage.alphaToCoverage)
            fragmentStageOptions.addMode(Mode::ALPHA_TO_COVERAGE);
        if (coverage.adjustCoverage)
            fragmentStageOptions.addMode(Mode::ADJUST_COVERAGE);
        // OpenEden (task 0026): only the draws that write the G-buffer get the define.
        const bool toGBuffer = gbufferOutputs > 0 && writesGBuffer(key);
        if (toGBuffer)
            fragmentStageOptions.addMode(Mode::GBUFFER);
        auto fragmentStage = mFragmentStageCache.getOrCreate(fragmentStageOptions, *this);

        vsg::ShaderStages shaders
            = { vsg::ShaderStage::create(*vertexStage), vsg::ShaderStage::create(*fragmentStage) };

        auto numUvSets = key.numUvSets;
        vsg::ShaderStage::SpecializationConstants specConstants = { { 0, vsg::intValue::create(numUvSets) } };

        auto vertexSpecConstants = specConstants;
        for (auto [binding, uvSet] : key.nonStandardUvSets)
            if (binding == Descriptors::TEXMAT_BINDING)
                vertexSpecConstants.emplace(Descriptors::START_UVSET_CONSTANTS + binding, vsg::intValue::create(uvSet));

        shaders[0]->specializationConstants = vertexSpecConstants;

        auto fragmentSpecConstants = specConstants;
        fragmentSpecConstants.emplace(1, vsg::intValue::create(key.alphaTestMode));
        fragmentSpecConstants.emplace(2, vsg::intValue::create(key.colorMode));
        fragmentSpecConstants.emplace(3, vsg::Value<VkBool32>::create(key.specular));
        for (auto [binding, uvSet] : key.nonStandardUvSets)
            if (binding != Descriptors::TEXMAT_BINDING && binding != Descriptors::ENV_UNIT)
                fragmentSpecConstants.emplace(
                    Descriptors::START_UVSET_CONSTANTS + binding, vsg::intValue::create(uvSet));
        shaders[1]->specializationConstants = fragmentSpecConstants;

        uint32_t vertexBindingIndex = 0;
        vsg::VertexInputState::Bindings vertexBindingsDescriptions;
        vsg::VertexInputState::Attributes vertexAttributeDescriptions;
        static_assert(static_cast<int>(Mode::VERTEX) == 0);
        if (key.shader.hasMode(Mode::VERTEX))
        {
            vertexBindingsDescriptions.push_back(
                { vertexBindingIndex, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX });
            vertexAttributeDescriptions.push_back(
                { vertexBindingIndex, vertexBindingIndex++, VK_FORMAT_R32G32B32_SFLOAT, 0 });
        }
        static_assert(static_cast<int>(Mode::NORMAL) == 1);
        if (key.shader.hasMode(Mode::NORMAL))
        {
            vertexBindingsDescriptions.push_back(
                { vertexBindingIndex, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX });
            vertexAttributeDescriptions.push_back(
                { vertexBindingIndex, vertexBindingIndex++, VK_FORMAT_R32G32B32_SFLOAT, 0 });
        }
        static_assert(static_cast<int>(Mode::COLOR) == 2);
        if (key.shader.hasMode(Mode::COLOR))
        {
            vertexBindingsDescriptions.push_back(
                { vertexBindingIndex, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_VERTEX });
            vertexAttributeDescriptions.push_back(
                { vertexBindingIndex, vertexBindingIndex++, VK_FORMAT_R32G32B32A32_SFLOAT, 0 });
        }
        static_assert(static_cast<int>(Mode::SKIN) == 3);
        if (key.shader.hasMode(Mode::SKIN))
        {
            vertexBindingsDescriptions.push_back(
                { vertexBindingIndex, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_VERTEX });
            vertexAttributeDescriptions.push_back(
                { vertexBindingIndex, vertexBindingIndex++, VK_FORMAT_R32G32B32A32_SFLOAT, 0 });

            vertexBindingsDescriptions.push_back(
                { vertexBindingIndex, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_VERTEX });
            vertexAttributeDescriptions.push_back(
                { vertexBindingIndex, vertexBindingIndex++, VK_FORMAT_R32G32B32A32_SFLOAT, 0 });
        }
        // OpenEden: the tangent and the per-instance inputs come before the texture coordinates, whose count is a
        // specialization constant (files/shaders/lib/attributes.glsl).
        if (key.shader.hasMode(Mode::NORMAL_MAP))
        {
            vertexBindingsDescriptions.push_back(
                { vertexBindingIndex, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_VERTEX });
            vertexAttributeDescriptions.push_back(
                { vertexBindingIndex, vertexBindingIndex++, VK_FORMAT_R32G32B32A32_SFLOAT, 0 });
        }
        if (key.shader.hasMode(Mode::INSTANCE))
        {
            vertexBindingsDescriptions.push_back(
                { vertexBindingIndex, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_INSTANCE });
            vertexAttributeDescriptions.push_back(
                { vertexBindingIndex, vertexBindingIndex++, VK_FORMAT_R32G32B32A32_SFLOAT, 0 });
            vertexBindingsDescriptions.push_back(
                { vertexBindingIndex, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_INSTANCE });
            vertexAttributeDescriptions.push_back(
                { vertexBindingIndex, vertexBindingIndex++, VK_FORMAT_R32G32B32_SFLOAT, 0 });
        }
        static_assert(static_cast<int>(Mode::TEXCOORD) == 4);
        if (key.shader.hasMode(Mode::TEXCOORD))
        {
            for (uint32_t i = 0; i < numUvSets; ++i)
            {
                vertexBindingsDescriptions.push_back(
                    { vertexBindingIndex, uint32_t(sizeof(vsg::vec2)), VK_VERTEX_INPUT_RATE_VERTEX });
                vertexAttributeDescriptions.push_back(
                    { vertexBindingIndex, vertexBindingIndex, VK_FORMAT_R32G32_SFLOAT, 0 });
                ++vertexBindingIndex;
            }
        }

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.blendEnable = key.blend;
        if (key.colorWrite)
        {
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }
        if (key.blend)
        {
            colorBlendAttachment.srcColorBlendFactor = key.srcBlendFactor;
            colorBlendAttachment.dstColorBlendFactor = key.dstBlendFactor;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = key.srcAlphaBlendFactor;
            colorBlendAttachment.dstAlphaBlendFactor = key.dstAlphaBlendFactor;
            ;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        vsg::ColorBlendState::ColorBlendAttachments colorBlendAttachments = { colorBlendAttachment };
        vsg::ref_ptr<vsg::ColorBlendState> colorBlendState;
        if (gbufferOutputs > 0)
        {
            // The extra entries for the G-buffer's outputs: written only by the draws that define GBUFFER.
            const auto entries = gbufferBlendAttachments(colorBlendAttachment, key, 2);
            colorBlendState = GBufferBlendState::create(colorBlendAttachments, entries[1], 1 + gbufferOutputs);
        }
        else
            colorBlendState = vsg::ColorBlendState::create(colorBlendAttachments);

        auto depthStencilState = vsg::DepthStencilState::create();
        depthStencilState->depthTestEnable = key.depthTest;
        depthStencilState->depthWriteEnable = key.depthWrite;

        auto rasterState = vsg::RasterizationState::create();
        rasterState->cullMode = key.cullMode;
        rasterState->frontFace = key.frontFace;
        rasterState->polygonMode = key.polygonMode;
        if (key.depthBias)
        {
            rasterState->depthBiasEnable = true;
            rasterState->depthBiasConstantFactor = 10000;
            rasterState->depthBiasSlopeFactor = 10000;
        }

        vsg::GraphicsPipelineStates pipelineStates{ vsg::VertexInputState::create(
                                                        vertexBindingsDescriptions, vertexAttributeDescriptions),
            vsg::InputAssemblyState::create(key.primitiveTopology), rasterState, colorBlendState,
            vsg::MultisampleState::create(), depthStencilState };

        return vsg::BindGraphicsPipeline::create(vsg::GraphicsPipeline::create(mLayout, shaders, pipelineStates));
    }
}
