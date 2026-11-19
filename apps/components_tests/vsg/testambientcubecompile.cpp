// Task 0355, stage k: the GLSL of the terrain and of the object shader compiles with the ambient cube in every
// combination the sky-visibility header buffer makes interesting (the buffer is declared once, by whichever file comes
// first: cloudshadow.glsl with CLOUD_SHADOW, skyvis.glsl with GI_SKY, else ambientcube.glsl). A second declaration is a
// compile error, which is what this guards.
#include <components/resource/shadersettings.hpp>

#include <gtest/gtest.h>

#include <vsg/all.h>
#include <vsg/io/glsl.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
    bool compiles(const std::string& file, const std::vector<std::string>& defines, const std::set<std::string>& settings)
    {
        auto options = vsg::Options::create();
        options->paths = { OPENEDEN_SHADER_SOURCE_DIR };
        vsg::ref_ptr<Resource::ShaderSettings> shaderSettings{ new Resource::ShaderSettings };
        for (const std::string& name : settings)
        {
            auto& object = shaderSettings->objects[name == "DEBUG_CHUNKS" ? "terrain.settings" : "shadow.settings"];
            const auto* module = dynamic_cast<const vsg::ShaderModule*>(object.get());
            std::string text = module ? module->source : std::string();
            if (!text.empty() && text.back() != '\n')
                text += '\n';
            object = vsg::ShaderModule::create(text + "#define " + name + " 1\n");
        }
        options->readerWriters = { shaderSettings, vsg::glsl::create() };
        auto stage = vsg::read_cast<vsg::ShaderStage>(file, options);
        if (!stage)
            return false;
        vsg::ShaderStages stages{ stage };
        return vsg::ShaderCompiler::create()->compile(stages, defines, options) && !stage->module->code.empty();
    }

    TEST(AmbientCubeCompile, theTerrainStageCompilesWithTheCubeAlone)
    {
        EXPECT_TRUE(compiles("terrain/main.frag", { "AMBIENT_CUBE" }, {}));
    }

    TEST(AmbientCubeCompile, theTerrainStageCompilesWithTheCubeAndTheSkyVisibilityField)
    {
        EXPECT_TRUE(compiles("terrain/main.frag", { "AMBIENT_CUBE", "GI_SKY" }, {}));
    }

    TEST(AmbientCubeCompile, theTerrainStageCompilesWithTheCubeAndTheCloudShadowHeader)
    {
        EXPECT_TRUE(compiles("terrain/main.frag", { "AMBIENT_CUBE" }, { "CLOUD_SHADOW" }));
        EXPECT_TRUE(compiles("terrain/main.frag", { "AMBIENT_CUBE", "GI_SKY" }, { "CLOUD_SHADOW" }));
    }

    TEST(AmbientCubeCompile, theTerrainStageCompilesWithTheCubeInThePbrAndAmbientOutputPaths)
    {
        EXPECT_TRUE(compiles("terrain/main.frag",
            { "AMBIENT_CUBE", "TERRAIN_MAPS", "TERRAIN_PBR", "GBUFFER", "GBUFFER_MATERIAL", "GBUFFER_AMBIENT",
                "AO_TERRAIN", "GI_SKY" },
            {}));
        EXPECT_TRUE(compiles("terrain/main.frag", { "AMBIENT_CUBE", "TERRAIN_MAPS", "GBUFFER", "GBUFFER_AMBIENT",
                                                    "AO_TERRAIN" },
            {}));
    }

    TEST(AmbientCubeCompile, theTerrainStageStillCompilesWithoutTheCube)
    {
        EXPECT_TRUE(compiles("terrain/main.frag", {}, {}));
        EXPECT_TRUE(compiles("terrain/main.frag", { "GI_SKY", "TERRAIN_MAPS", "TERRAIN_PBR" }, {}));
    }

    TEST(AmbientCubeCompile, theObjectStageCompilesWithTheCubeInEveryHeaderCombination)
    {
        const std::vector<std::string> base{ "AMBIENT_CUBE", "NORMAL", "DIFFUSE_MAP", "TEXCOORD", "MATERIAL" };
        EXPECT_TRUE(compiles("default/main.frag", base, {}));
        auto sky = base;
        sky.push_back("GI");
        sky.push_back("GI_SKY");
        EXPECT_TRUE(compiles("default/main.frag", sky, {}));
        auto skyOnly = base;
        skyOnly.push_back("GI_SKY"); // GI_SKY without GI: skyvis.glsl is not included, the cube declares the header
        EXPECT_TRUE(compiles("default/main.frag", skyOnly, {}));
        EXPECT_TRUE(compiles("default/main.frag", base, { "CLOUD_SHADOW" }));
        EXPECT_TRUE(compiles("default/main.frag", sky, { "CLOUD_SHADOW" }));
    }
}
