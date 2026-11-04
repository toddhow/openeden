// Tests for MWRender::MergedBodies::pipelineDefines and compareDefines (task 0072, review of stage f, finding 1). The
// merged body refuses a merged pipeline whose defines differ from the stock part's pipeline. It read the defines from
// each stage's module hints, but a Slang twin's module has none, so with `[Rendering] slang shaders` on both sides were
// empty and the check was silently skipped (it also skipped a stage only one side had). The defines are now those of
// the GLSL kept beside the twin (vsgUtil::glslDefinesOf), and a stage only one side has fails the check. The pipelines
// are built by hand from plain VSG objects (no device), their stages twinned the way the engine twins them. Written
// before the implementation.

#include "apps/openmw/mwrender/mergedbody.hpp"

#include <components/vsgutil/shaderpermutation.hpp>

#include <vsg/io/Options.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/PipelineLayout.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/state/ShaderStage.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    using MWRender::MergedBodies::compareDefines;
    using MWRender::MergedBodies::pipelineDefines;
    using Names = std::set<std::string>;
    using StageDefines = std::map<int, Names>;

    const int sVertex = static_cast<int>(VK_SHADER_STAGE_VERTEX_BIT);
    const int sFragment = static_cast<int>(VK_SHADER_STAGE_FRAGMENT_BIT);

    // The defines the merged look changes on purpose, which the comparison ignores, and some it must not ignore.
    const std::vector<std::string> sAdaptedDefines{ "COLOR", "MORPH", "NORMAL_MAP", "PARALLAX", "RECONSTRUCT_NORMAL_Z",
        "NORMAL", "TEXCOORD", "SKIN", "VERTEX", "GBUFFER_MOTION" };
    const std::vector<std::string> sComparedDefines{ "MATERIAL", "DIFFUSE_MAP", "PBR", "GI", "GBUFFER",
        "ALPHA_TO_COVERAGE", "GLOW_MAP", "PBR_PHYSICAL" };

    const StageDefines sStock{ { sVertex, { "NORMAL", "SKIN", "TEXCOORD", "VERTEX" } },
        { sFragment, { "DIFFUSE_MAP", "MATERIAL", "NORMAL", "TEXCOORD" } } };

    // Task 0072, review of stage f, finding 1 (written before the implementation).
    TEST(MergedBodyDefines, equal_defines_compare_equal)
    {
        EXPECT_EQ(compareDefines(sStock, sStock), "");
        EXPECT_EQ(compareDefines(StageDefines{}, StageDefines{}), "");
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation).
    TEST(MergedBodyDefines, a_define_the_merged_pipeline_lacks_is_a_reason)
    {
        StageDefines merged = sStock;
        merged[sFragment].erase("MATERIAL");
        EXPECT_NE(compareDefines(sStock, merged), "");
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation).
    TEST(MergedBodyDefines, a_define_only_the_merged_pipeline_has_is_a_reason)
    {
        StageDefines merged = sStock;
        merged[sFragment].insert("GLOW_MAP");
        EXPECT_NE(compareDefines(sStock, merged), "");
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation): the merged look takes away the
    // stock part's vertex inputs and adds skinning and its motion.
    TEST(MergedBodyDefines, the_defines_the_merged_look_changes_are_ignored)
    {
        StageDefines lacking = sStock;
        for (const char* name : { "SKIN", "TEXCOORD", "VERTEX" })
        {
            lacking[sVertex].erase(name);
            lacking[sFragment].erase(name);
        }
        EXPECT_EQ(compareDefines(sStock, lacking), "");

        StageDefines extra = sStock;
        extra[sVertex].insert("GBUFFER_MOTION");
        extra[sFragment].insert("SKIN");
        extra[sFragment].insert("GBUFFER_MOTION");
        EXPECT_EQ(compareDefines(sStock, extra), "");
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation): exactly these ten names are the
    // adaptation's, in either direction; other names are compared.
    TEST(MergedBodyDefines, only_the_adapted_names_are_ignored)
    {
        for (const std::string& name : sAdaptedDefines)
        {
            SCOPED_TRACE(name);
            const StageDefines with{ { sVertex, { name } }, { sFragment, { name, "MATERIAL" } } };
            const StageDefines without{ { sVertex, {} }, { sFragment, { "MATERIAL" } } };
            EXPECT_EQ(compareDefines(with, without), "");
            EXPECT_EQ(compareDefines(without, with), "");
        }
        for (const std::string& name : sComparedDefines)
        {
            SCOPED_TRACE(name);
            const StageDefines with{ { sVertex, { "VERTEX" } }, { sFragment, { name } } };
            const StageDefines without{ { sVertex, { "VERTEX" } }, { sFragment, {} } };
            EXPECT_NE(compareDefines(with, without), "");
            EXPECT_NE(compareDefines(without, with), "");
        }
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation): a stage only one side has defines
    // for fails the check closed, whichever side lacks it.
    TEST(MergedBodyDefines, a_stage_only_one_side_has_is_a_reason)
    {
        StageDefines vertexOnly = sStock;
        vertexOnly.erase(sFragment);
        EXPECT_NE(compareDefines(sStock, vertexOnly), "");
        EXPECT_NE(compareDefines(vertexOnly, sStock), "");
        // Even when the stage the other side has holds only adapted names, or none.
        const StageDefines adaptedOnly{ { sVertex, { "VERTEX" } }, { sFragment, { "TEXCOORD" } } };
        const StageDefines oneStage{ { sVertex, { "VERTEX" } } };
        EXPECT_NE(compareDefines(adaptedOnly, oneStage), "");
        EXPECT_NE(compareDefines(oneStage, adaptedOnly), "");
        const StageDefines emptyStage{ { sVertex, { "VERTEX" } }, { sFragment, {} } };
        EXPECT_NE(compareDefines(emptyStage, oneStage), "");
        EXPECT_NE(compareDefines(oneStage, emptyStage), "");
    }

    const std::string sGlslSource = "#version 450\nvoid main() {}\n";
    const std::vector<uint32_t> sTwinWords{ 0x07230203u, 0x00010000u, 0u, 16u, 0u };

    vsg::ref_ptr<vsg::ShaderStage> glslStage(VkShaderStageFlagBits flag, const Names& requested)
    {
        auto hints = vsg::ShaderCompileSettings::create();
        hints->defines = requested;
        return vsg::ShaderStage::create(flag, "main", vsg::ShaderModule::create(sGlslSource, hints));
    }

    // A stage twinned the way Pipeline::Graphics twins one: the choice attached to the GLSL stage, then the module
    // replaced by the twin's SPIR-V, which has no hints.
    vsg::ref_ptr<vsg::ShaderStage> twinnedStage(
        VkShaderStageFlagBits flag, const std::string& path, const Names& requested)
    {
        auto stage = glslStage(flag, requested);
        EXPECT_TRUE(vsgUtil::attachSlangStageChoice(*stage, path, vsg::Options::create())) << path;
        stage->module = vsg::ShaderModule::create(sTwinWords);
        return stage;
    }

    vsg::ref_ptr<vsg::GraphicsPipeline> pipelineOf(const vsg::ShaderStages& stages)
    {
        return vsg::GraphicsPipeline::create(vsg::PipelineLayout::create(), stages, vsg::GraphicsPipelineStates{});
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation): a GLSL pipeline's defines are its
    // stages' hints.
    TEST(MergedBodyDefines, a_glsl_pipeline_gives_its_stages_hints)
    {
        const auto pipeline = pipelineOf({ glslStage(VK_SHADER_STAGE_VERTEX_BIT, { "SKIN", "VERTEX" }),
            glslStage(VK_SHADER_STAGE_FRAGMENT_BIT, { "DIFFUSE_MAP", "MATERIAL" }) });
        const StageDefines expected{ { sVertex, { "SKIN", "VERTEX" } }, { sFragment, { "DIFFUSE_MAP", "MATERIAL" } } };
        EXPECT_EQ(pipelineDefines(*pipeline), expected);
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation): a twinned pipeline's defines are
    // those its GLSL stages were compiled with, kept beside the twins.
    TEST(MergedBodyDefines, a_twinned_pipeline_gives_the_glsl_defines_kept_beside_its_twins)
    {
        const auto pipeline
            = pipelineOf({ twinnedStage(VK_SHADER_STAGE_VERTEX_BIT, "default/main.vert", { "SKIN", "VERTEX" }),
                twinnedStage(VK_SHADER_STAGE_FRAGMENT_BIT, "default/main.frag", { "DIFFUSE_MAP", "MATERIAL" }) });
        for (const auto& stage : pipeline->stages)
        {
            EXPECT_TRUE(stage->module->source.empty());
            EXPECT_FALSE(stage->module->hints);
        }
        const StageDefines expected{ { sVertex, { "SKIN", "VERTEX" } }, { sFragment, { "DIFFUSE_MAP", "MATERIAL" } } };
        EXPECT_EQ(pipelineDefines(*pipeline), expected);
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation): a stage with neither hints nor a
    // kept choice (a Slang-only shader) is left out.
    TEST(MergedBodyDefines, a_stage_without_glsl_defines_is_left_out)
    {
        const auto pipeline = pipelineOf({ glslStage(VK_SHADER_STAGE_VERTEX_BIT, { "VERTEX" }),
            vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(sTwinWords)) });
        const StageDefines expected{ { sVertex, { "VERTEX" } } };
        EXPECT_EQ(pipelineDefines(*pipeline), expected);
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation): the regression. The stock part's
    // pipeline is GLSL with PBR; the merged pipeline's GLSL lacked PBR and was twinned. Before the fix the twins had no
    // hints, both sides read as empty and the merged body was drawn with the wrong look.
    TEST(MergedBodyDefines, a_twinned_merged_pipeline_that_lacks_a_define_is_refused)
    {
        const auto stock = pipelineOf({ glslStage(VK_SHADER_STAGE_VERTEX_BIT, { "NORMAL", "TEXCOORD", "VERTEX" }),
            glslStage(VK_SHADER_STAGE_FRAGMENT_BIT, { "DIFFUSE_MAP", "MATERIAL", "PBR" }) });
        const auto merged = pipelineOf(
            { twinnedStage(VK_SHADER_STAGE_VERTEX_BIT, "default/main.vert", { "NORMAL", "SKIN", "TEXCOORD", "VERTEX" }),
                twinnedStage(VK_SHADER_STAGE_FRAGMENT_BIT, "default/main.frag", { "DIFFUSE_MAP", "MATERIAL" }) });
        EXPECT_NE(compareDefines(pipelineDefines(*stock), pipelineDefines(*merged)), "");
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation): with the switch on both sides can
    // be twinned; the same GLSL defines compare equal.
    TEST(MergedBodyDefines, two_twinned_pipelines_with_the_same_glsl_defines_compare_equal)
    {
        const auto stock = pipelineOf({ twinnedStage(VK_SHADER_STAGE_VERTEX_BIT, "default/main.vert", { "VERTEX" }),
            twinnedStage(VK_SHADER_STAGE_FRAGMENT_BIT, "default/main.frag", { "DIFFUSE_MAP", "MATERIAL", "PBR" }) });
        const auto merged = pipelineOf({ twinnedStage(VK_SHADER_STAGE_VERTEX_BIT, "default/main.vert", { "VERTEX" }),
            twinnedStage(VK_SHADER_STAGE_FRAGMENT_BIT, "default/main.frag", { "DIFFUSE_MAP", "MATERIAL", "PBR" }) });
        EXPECT_EQ(compareDefines(pipelineDefines(*stock), pipelineDefines(*merged)), "");
    }
}
