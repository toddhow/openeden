#include "graphics.hpp"

#include <algorithm>
#include <stdexcept>

#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/VertexInputState.h>

#include <vsg/io/read.h>

#include <components/debug/debuglog.hpp>
#include <components/vsgutil/readshader.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

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
            case Mode::GBUFFER_MOTION:
                return "GBUFFER_MOTION";
            case Mode::GBUFFER_MATERIAL:
                return "GBUFFER_MATERIAL";
            case Mode::PBR:
                return "PBR";
            case Mode::PBR_PHYSICAL:
                return "PBR_PHYSICAL";
            case Mode::MATERIAL_MAP:
                return "MATERIAL_MAP";
            case Mode::MATERIAL_MAP_RMAOS:
                return "MATERIAL_MAP_RMAOS";
            case Mode::GI:
                return "GI";
            case Mode::GI_SKY:
                return "GI_SKY";
            case Mode::GBUFFER_AMBIENT:
                return "GBUFFER_AMBIENT";
            case Mode::WETNESS:
                return "WETNESS";
            case Mode::WETNESS_PUDDLES:
                return "WETNESS_PUDDLES";
            case Mode::WETNESS_DEBUG:
                return "WETNESS_DEBUG";
            case Mode::COVER:
                return "COVER";
            case Mode::COVER_DEBUG:
                return "COVER_DEBUG";
            case Mode::PRECIPITATION_LIT:
                return "PRECIPITATION_LIT";
            case Mode::HEIGHT_MAP:
                return "HEIGHT_MAP";
            case Mode::PARALLAX_POM:
                return "PARALLAX_POM";
            case Mode::PARALLAX_POM_LOW:
                return "PARALLAX_POM_LOW";
            case Mode::PARALLAX_POM_HIGH:
                return "PARALLAX_POM_HIGH";
            case Mode::PARALLAX_DISCARD:
                return "PARALLAX_DISCARD";
            case Mode::LOD_FADE:
                return "LOD_FADE";
            case Mode::POINT_SHADOW:
                return "POINT_SHADOW";
            case Mode::LOD_SWAY:
                return "LOD_SWAY";
            case Mode::WATER_CELL:
                return "WATER_CELL";
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
        // Task 0072, review of the stage f and g rounds, finding 7: `[Rendering] slang shaders` from the options it
        // reads with (Resource::createShaderOptions), so no stage can be made before the switch is known.
        , mPreferSlang(vsgUtil::prefersSlang(readShaderOptions.get()))
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
            { Descriptors::MATERIAL_MAP_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { Descriptors::HEIGHT_UNIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
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
        if (std::find(slangOnlyPaths.begin(), slangOnlyPaths.end(), path) != slangOnlyPaths.end())
        {
            const std::string spirvPath = path + suffix + ".spv";
            auto module = vsg::read_cast<vsg::ShaderModule>(spirvPath, mShaderOptions);
            if (!module || module->code.empty())
                throw std::runtime_error("!read Slang shader \"" + spirvPath + "\"");
            const VkShaderStageFlagBits stage
                = suffix == "/main.vert" ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
            return vsg::ShaderStage::create(stage, "main", module);
        }
        auto shaderStage = vsgUtil::readShader(path + suffix, mShaderOptions);
        // Task 0072: OPENEDEN_SHADER_TRACE lists the (stage, defines) pairs a run uses.
        vsgUtil::recordShaderUse(path + suffix, compileSettings->defines, shaderStage.get(), mShaderOptions);
        // if (!compileSettings->defines.empty())
        {
            shaderStage = vsg::ShaderStage::create(*shaderStage);
            shaderStage->module = vsg::ShaderModule::create(*shaderStage->module);
            shaderStage->module->hints = compileSettings;
        }
        // Task 0072: the Slang twin is chosen by the pipeline (Graphics::create), which takes the twins of both its
        // stages or neither (vsgUtil::takeSlangTwinsTogether); the stage, shared by many pipelines, keeps its GLSL.
        // Review of stage f: what the choice needs from the stage (its set from the expanded source and the settings
        // defines, whether its family counts texture coordinate sets, the twin when it does not, and the GLSL defines
        // the merged body compares) is worked out here, once per cached stage, and every copy carries it.
        if (mPreferSlang)
            vsgUtil::attachSlangStageChoice(*shaderStage, path + suffix, mShaderOptions);
        return shaderStage;
    }

    bool Graphics::hasSlangShader(const std::string& path) const
    {
        for (const char* suffix : { "/main.vert.spv", "/main.frag.spv" })
        {
            auto module = vsg::read_cast<vsg::ShaderModule>(path + suffix, mShaderOptions);
            if (!module || module->code.empty())
                return false;
        }
        return true;
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
        // OpenEden (task 0026): only the draws that write the G-buffer get its defines; a skinned one also its motion.
        const bool toGBuffer = gbufferOutputs > 0 && writesGBuffer(key);
        const bool gbufferMotion = toGBuffer && key.shader.hasMode(Mode::SKIN);
        VertexStageOptions vertexStageOptions{ key.shader.path, {} };
        for (auto mode : vertexModes)
        {
            if (key.shader.hasMode(mode))
                vertexStageOptions.addMode(mode);
        }
        if (gbufferMotion)
            vertexStageOptions.addMode(Mode::GBUFFER_MOTION);
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
        if (toGBuffer)
            fragmentStageOptions.addMode(Mode::GBUFFER);
        // Task 0027: the material model, for every lit draw that is not a particle or debug geometry.
        if (pbrLook > 0 && !key.shader.hasMode(Mode::PARTICLE) && key.colorMode != 3)
        {
            fragmentStageOptions.addMode(Mode::PBR);
            if (pbrLook > 1)
                fragmentStageOptions.addMode(Mode::PBR_PHYSICAL);
        }
        // Task 0028: baked light probes, for every lit draw that is not a particle or debug geometry.
        if ((giEnabled || giSkyEnabled) && !key.shader.hasMode(Mode::PARTICLE) && key.colorMode != 3)
            fragmentStageOptions.addMode(Mode::GI);
        // Task 0036: the exterior sky-visibility field, in the same draws.
        if (giSkyEnabled && !key.shader.hasMode(Mode::PARTICLE) && key.colorMode != 3)
            fragmentStageOptions.addMode(Mode::GI_SKY);
        // Task 0037: rain wetness, in lit draws that take the material model (the lighting it changes).
        if (wetnessEnabled && pbrLook > 0 && !key.shader.hasMode(Mode::PARTICLE) && key.colorMode != 3)
        {
            fragmentStageOptions.addMode(Mode::WETNESS);
            if (wetnessPuddles)
                fragmentStageOptions.addMode(Mode::WETNESS_PUDDLES);
            if (wetnessDebug)
                fragmentStageOptions.addMode(Mode::WETNESS_DEBUG);
        }
        // Task 0039: snow and ash cover, in the same draws.
        if (coverEnabled && pbrLook > 0 && !key.shader.hasMode(Mode::PARTICLE) && key.colorMode != 3)
        {
            fragmentStageOptions.addMode(Mode::COVER);
            if (coverDebug)
                fragmentStageOptions.addMode(Mode::COVER_DEBUG);
        }
        // Task 0077: shadows from the nearest point lights, in the same draws (they sum the lights per pixel only with
        // the material model).
        if (pointShadowEnabled && pbrLook > 0 && !key.shader.hasMode(Mode::PARTICLE) && key.colorMode != 3)
            fragmentStageOptions.addMode(Mode::POINT_SHADOW);
        // Task 0045: parallax occlusion mapping, for draws whose texture has a height file (the loader sets HEIGHT_MAP
        // only with the setting on).
        if (parallaxQuality > 0 && key.shader.hasMode(Mode::HEIGHT_MAP))
        {
            fragmentStageOptions.addMode(Mode::PARALLAX_POM);
            if (parallaxQuality == 1)
                fragmentStageOptions.addMode(Mode::PARALLAX_POM_LOW);
            else if (parallaxQuality == 3)
                fragmentStageOptions.addMode(Mode::PARALLAX_POM_HIGH);
        }
        if (toGBuffer && gbufferOutputs >= 4)
            fragmentStageOptions.addMode(Mode::GBUFFER_MATERIAL);
        if (toGBuffer && gbufferOutputs >= 5)
            fragmentStageOptions.addMode(Mode::GBUFFER_AMBIENT);
        if (gbufferMotion)
            fragmentStageOptions.addMode(Mode::GBUFFER_MOTION);
        auto fragmentStage = mFragmentStageCache.getOrCreate(fragmentStageOptions, *this);

        vsg::ShaderStages shaders
            = { vsg::ShaderStage::create(*vertexStage), vsg::ShaderStage::create(*fragmentStage) };

        bool slangPending = false;
        auto numUvSets = key.numUvSets;
        vsg::ShaderStage::SpecializationConstants specConstants = { { 0, vsg::intValue::create(numUvSets) } };

        // Task 0072: a Slang twin compiled for exactly the defines the GLSL compiler would apply (the requested ones
        // the stage imports); stage f: for a set with the object shader's texture coordinate arrays, also for this
        // pipeline's count of them (vsgUtil::slangSelectionSet), which only the pipeline knows, so the twins are
        // chosen here, on this pipeline's copies. VSG compiles a pipeline's stages together when one is GLSL and
        // glslang cannot link a SPIR-V stage, so both stages take their twins or neither does: a set with no file
        // draws with the GLSL, and so does the other stage of its pipeline.
        // Review of stage f: the choice is the stage's, worked out once (vsgUtil::attachSlangStageChoice); a pipeline
        // only adds its count (vsgUtil::slangTwinFor). With the switch off and no trace nothing here runs (finding 9).
        if (const bool trace = vsgUtil::shaderTraceActive(); mPreferSlang || trace)
        {
            const std::string shaderPath = key.shader.path.empty() ? "default" : key.shader.path;
            std::vector<vsg::ref_ptr<vsg::ShaderModule>> twins(shaders.size());
            std::vector<std::set<std::string>> traced(shaders.size());
            bool glslPipeline = true;
            bool anyPending = false;
            for (size_t i = 0; i < shaders.size(); ++i)
            {
                auto& shader = shaders[i];
                if (!shader->module || shader->module->source.empty() || !shader->module->hints)
                {
                    glslPipeline = false; // a Slang-only shader (slangOnlyPaths): nothing to choose
                    continue;
                }
                const std::string stageFile = shaderPath + (i == 0 ? "/main.vert" : "/main.frag");
                const vsgUtil::SlangStageChoice* choice = vsgUtil::slangStageChoiceOf(*shader);
                // A stage made before the switch was set has no choice: work it out on this pipeline's copy.
                vsg::ref_ptr<vsgUtil::SlangStageChoice> ownChoice;
                if (choice == nullptr && mPreferSlang)
                {
                    ownChoice = vsgUtil::attachSlangStageChoice(*shader, stageFile, mShaderOptions);
                    choice = ownChoice.get();
                }
                if (trace)
                {
                    const auto& defines = shader->module->hints->defines;
                    vsgUtil::recordShaderUse(stageFile, defines, shader.get(), mShaderOptions, numUvSets);
                    if (choice != nullptr)
                    {
                        traced[i] = choice->defines;
                        if (choice->byPipeline)
                            traced[i].insert(vsgUtil::uvSetsDefine(numUvSets));
                    }
                    else if (auto set
                        = vsgUtil::slangSelectionSet(stageFile, defines, shader->module->source, mShaderOptions))
                        traced[i] = std::move(*set);
                    else if (auto counted = vsgUtil::slangSelectionSet(
                                 stageFile, defines, shader->module->source, mShaderOptions, numUvSets))
                        traced[i] = std::move(*counted);
                }
                if (mPreferSlang && choice != nullptr)
                {
                    bool pending = false;
                    twins[i] = vsgUtil::slangTwinFor(*choice, numUvSets, mShaderOptions, &pending);
                    anyPending = anyPending || pending;
                }
            }
            const bool anyTwin = std::any_of(twins.begin(), twins.end(), [](const auto& twin) { return bool(twin); });
            const bool slang = mPreferSlang && anyTwin && vsgUtil::takeSlangTwinsTogether(shaders, twins);
            if (slang)
                Log(Debug::Verbose) << "Using the Slang permutations of " << shaderPath << " for " << numUvSets
                                    << " texture coordinate sets";
            if (trace && glslPipeline && shaders.size() == 2)
                vsgUtil::recordPipelineUse(shaderPath, traced[0], traced[1], slang);
            // A Slang version of one of the stages is being compiled on the worker (queue item 3 of decision 0012):
            // this pipeline draws with the GLSL, and is not kept in the cache (getOrCreate), so the next object that
            // needs it builds it again and takes the twin once it is ready.
            slangPending = anyPending;
        }

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
        // Task 0072, stage b: a Slang twin takes the `*.settings` values as specialization constants (a GLSL stage has
        // them written into its source and is left as it is).
        for (auto& shader : shaders)
            vsgUtil::addSettingsConstants(*shader, mShaderOptions.get());

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
        else if (usesOverlayDepthBias(key, overlayDepthBiasSlope))
        {
            // Task 0075: slope only (no constant factor), reverse-Z: a positive bias moves the surface nearer.
            rasterState->depthBiasEnable = true;
            rasterState->depthBiasConstantFactor = 0.f;
            rasterState->depthBiasClamp = 0.f;
            rasterState->depthBiasSlopeFactor = overlayDepthBiasSlope;
        }

        vsg::GraphicsPipelineStates pipelineStates{ vsg::VertexInputState::create(
                                                        vertexBindingsDescriptions, vertexAttributeDescriptions),
            vsg::InputAssemblyState::create(key.primitiveTopology), rasterState, colorBlendState,
            vsg::MultisampleState::create(), depthStencilState };

        auto pipeline
            = vsg::BindGraphicsPipeline::create(vsg::GraphicsPipeline::create(mLayout, shaders, pipelineStates));
        if (slangPending)
            pipeline->setValue(slangPendingKey, true);
        return pipeline;
    }
}
