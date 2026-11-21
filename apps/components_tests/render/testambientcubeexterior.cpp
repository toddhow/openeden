// Task 0355, stage k: the ambient cube of an exterior. The cube maths a weather transition needs (a blend, a flat cube),
// the terrain pipeline's define, a text check that the terrain shaders (both languages) take the cube by the same rule
// and that the sky-visibility header buffer is declared once, and the setting.
#include <components/pipeline/terrain.hpp>
#include <components/render/ambientcube.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace
{
    using Render::AmbientCube::Cube;

    std::string readSource(const std::string& relative)
    {
        std::ifstream in(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/" + relative, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    TEST(AmbientCubeExterior, aFlatCubeLightsEverySurfaceWithItsColour)
    {
        const Cube c = Render::AmbientCube::flat(vsg::vec3(0.2f, 0.4f, 0.6f));
        for (const vsg::vec3& n : { vsg::vec3(1, 0, 0), vsg::vec3(0, -1, 0), vsg::vec3(0.6f, 0.0f, 0.8f) })
        {
            const vsg::vec3 s = Render::AmbientCube::sample(c, n);
            EXPECT_NEAR(s.x, 0.2f, 1e-5f);
            EXPECT_NEAR(s.y, 0.4f, 1e-5f);
            EXPECT_NEAR(s.z, 0.6f, 1e-5f);
        }
    }

    TEST(AmbientCubeExterior, aBlendIsLinearPerFaceAndClamped)
    {
        const Cube a = Render::AmbientCube::flat(vsg::vec3(0.f, 0.f, 0.f));
        Cube b = Render::AmbientCube::flat(vsg::vec3(1.f, 0.5f, 0.f));
        b[4] = vsg::vec3(0.f, 1.f, 1.f);
        const Cube m = Render::AmbientCube::lerp(a, b, 0.25f);
        EXPECT_FLOAT_EQ(m[0].x, 0.25f);
        EXPECT_FLOAT_EQ(m[0].y, 0.125f);
        EXPECT_FLOAT_EQ(m[4].y, 0.25f);
        EXPECT_FLOAT_EQ(m[4].x, 0.f);
        EXPECT_FLOAT_EQ(Render::AmbientCube::lerp(a, b, 2.f)[0].x, 1.f);
        EXPECT_FLOAT_EQ(Render::AmbientCube::lerp(a, b, -1.f)[0].x, 0.f);
    }

    TEST(AmbientCubeExterior, theTerrainPipelineAsksForTheCubeOnlyWhenTold)
    {
        const Pipeline::WetnessOptions wetness;
        const Pipeline::CoverOptions cover;
        const auto without = Pipeline::terrainDefines(false, 0, false, wetness, false, cover, 0, false, false);
        EXPECT_EQ(without.count("AMBIENT_CUBE"), 0u);
        const auto with = Pipeline::terrainDefines(false, 0, false, wetness, false, cover, 0, false, false, true);
        EXPECT_EQ(with.count("AMBIENT_CUBE"), 1u);
        EXPECT_EQ(with.size(), 1u);
    }

    TEST(AmbientCubeExterior, theTerrainShaderTakesTheCube)
    {
        const std::string slang = readSource("terrain/terrain.frag.slang");
        EXPECT_TRUE(contains(slang, "ambientCubeAmbient")) << "the terrain shader does not call the cube";
        EXPECT_TRUE(contains(slang, "ambientcube.")) << "the terrain shader does not include the cube";
    }

    TEST(AmbientCubeExterior, theSkyVisibilityHeaderIsDeclaredByOneFileOnly)
    {
        // The terrain imports GI_SKY without GI, which the cube's old condition did not know: the cube file now asks a
        // macro the declaring file sets (both languages).
        for (const char* ext : { ".glsl", ".slang" })
        {
            const std::string skyvis = readSource(std::string("lib/view/skyvis") + ext);
            const std::string cube = readSource(std::string("lib/view/ambientcube") + ext);
            EXPECT_TRUE(contains(skyvis, "SKY_HEADER_DECLARED")) << ext;
            EXPECT_TRUE(contains(cube, "SKY_HEADER_DECLARED")) << ext;
        }
    }

    TEST(AmbientCubeExterior, theSettingIsDeclaredAndOnByDefault)
    {
        const std::string defaults = readSource("../settings-default.cfg");
        EXPECT_TRUE(contains(defaults, "neutral weather ambient cube = true"));
    }
}
