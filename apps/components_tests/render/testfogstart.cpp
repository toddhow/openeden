// Task 0362 item 1: the fog gradient. The fog's start colour rides in the ambient cube's header entries (components/render/
// ambientcube.hpp) and the shaders blend the fog colour from it to the scene's fog colour by the fog's linear share
// (lib/view/fog.glsl and .slang, lib/view/ambientcube.glsl and .slang). Hand-worked numbers.
#include <gtest/gtest.h>

#include <components/render/ambientcube.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>


namespace
{
    std::string readShader(const std::string& relative)
    {
        std::ifstream in(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/" + relative, std::ios::binary);
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    const vsg::vec4 kSentinel(-7.f, -7.f, -7.f, -7.f);

    TEST(FogStartGpu, aStartColourIsWrittenNextToTheCubesFlagAndSaysItIsOn)
    {
        std::vector<vsg::vec4> header(Render::AmbientCube::headerTotalVec4s, kSentinel);
        const Render::AmbientCube::Cube cube = Render::AmbientCube::flat(vsg::vec3(0.5f, 0.5f, 0.5f));
        const vsg::vec3 start(0.1f, 0.2f, 0.3f);
        Render::AmbientCube::write(header.data(), &cube, vsg::vec3(0.4f, 0.4f, 0.4f), &start, true);
        const std::size_t b = Render::AmbientCube::base;
        EXPECT_EQ(header[b], vsg::vec4(1.f, 0.1f, 0.2f, 0.3f));
        EXPECT_EQ(header[b + 1], vsg::vec4(0.4f, 0.4f, 0.4f, 1.f)); // +1: the colour is the fog's far end
        EXPECT_EQ(header[b + 2], vsg::vec4(0.5f, 0.5f, 0.5f, 0.f));
    }

    TEST(FogStartGpu, withNoCubeAStartColourStillRidesAndTheBaselineIsLeftAlone)
    {
        std::vector<vsg::vec4> header(Render::AmbientCube::headerTotalVec4s, kSentinel);
        const vsg::vec3 start(0.9f, 0.8f, 0.7f);
        Render::AmbientCube::write(header.data(), nullptr, vsg::vec3(0.f, 0.f, 0.f), &start, false);
        const std::size_t b = Render::AmbientCube::base;
        EXPECT_EQ(header[b], vsg::vec4(0.f, 0.9f, 0.8f, 0.7f));
        EXPECT_EQ(header[b + 1].w, -1.f); // -1: the colour is where the fog starts
        EXPECT_EQ(header[b + 1].x, -7.f); // untouched: the baseline is only meaningful with a cube
    }

    TEST(FogStartGpu, noStartColourSaysOffInBothCases)
    {
        std::vector<vsg::vec4> header(Render::AmbientCube::headerTotalVec4s, vsg::vec4(1.f, 1.f, 1.f, 1.f));
        const Render::AmbientCube::Cube cube = Render::AmbientCube::flat(vsg::vec3(0.5f, 0.5f, 0.5f));
        Render::AmbientCube::write(header.data(), &cube, vsg::vec3(0.4f, 0.4f, 0.4f));
        const std::size_t b = Render::AmbientCube::base;
        EXPECT_EQ(header[b], vsg::vec4(1.f, 0.f, 0.f, 0.f));
        EXPECT_EQ(header[b + 1].w, 0.f);
        Render::AmbientCube::write(header.data(), nullptr, vsg::vec3(0.f, 0.f, 0.f));
        EXPECT_EQ(header[b], vsg::vec4(0.f, 0.f, 0.f, 0.f));
        EXPECT_EQ(header[b + 1].w, 0.f);
    }

    TEST(FogStartShaders, bothLanguagesReadTheStartColourFromTheHeaderAndBlendByTheLinearShare)
    {
        for (const char* ext : { "glsl", "slang" })
        {
            const std::string cube = readShader(std::string("lib/view/ambientcube.") + ext);
            EXPECT_NE(cube.find("fogStartColour"), std::string::npos) << ext;
            EXPECT_NE(cube.find("skyHeader[AMBIENT_CUBE_BASE].yzw"), std::string::npos) << ext;
            EXPECT_NE(cube.find("skyHeader[AMBIENT_CUBE_BASE + 1].w"), std::string::npos) << ext;
            const std::string fog = readShader(std::string("lib/view/fog.") + ext);
            EXPECT_NE(fog.find("fogStart.w > 0.5"), std::string::npos) << ext;
            const std::string blend = std::string(ext) == "glsl" ? "mix" : "lerp";
            EXPECT_NE(fog.find("fogStart.w < -0.5"), std::string::npos) << ext;
            EXPECT_NE(fog.find(blend + "(fogColour, fogStart.xyz, linearShare)"), std::string::npos) << ext;
            EXPECT_NE(fog.find(blend + "(fogStart.xyz, fogColour, linearShare)"), std::string::npos) << ext;
        }
    }

    TEST(FogStartShaders, theObjectAndTerrainShadersPassItToTheFog)
    {
        for (const char* file : { "default/main.frag", "terrain/main.frag", "default/default.frag.slang",
                 "terrain/terrain.frag.slang" })
        {
            const std::string src = readShader(file);
            EXPECT_NE(src.find("fogStartColour()"), std::string::npos) << file;
            EXPECT_NE(src.find("fogStart"), std::string::npos) << file;
        }
    }
}
