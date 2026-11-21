// Task 0355 stage e: the ambient cube of an interior. The maths (a surface's ambient from six colours by its normal
// squared), how the six colours and the flat baseline ride in the sky-visibility header buffer, the pipeline define and
// a text check that both shader languages carry the same constants and the same rule.
#include <components/pipeline/mode.hpp>
#include <components/render/ambientcube.hpp>
#include <components/render/cloudgpu.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Pipeline
{
    std::string getDefine(Mode mode); // components/pipeline/graphics.cpp
}

namespace
{
    using Render::AmbientCube::Cube;

    constexpr float kEps = 1e-5f;

    Cube cube()
    {
        return { vsg::vec3(0.1f, 0.2f, 0.3f), vsg::vec3(0.4f, 0.5f, 0.6f), vsg::vec3(0.7f, 0.8f, 0.9f),
            vsg::vec3(1.0f, 1.1f, 1.2f), vsg::vec3(1.3f, 1.4f, 1.5f), vsg::vec3(1.6f, 1.7f, 1.8f) };
    }

    void expectNear(const vsg::vec3& a, const vsg::vec3& b, float eps = kEps)
    {
        EXPECT_NEAR(a.x, b.x, eps);
        EXPECT_NEAR(a.y, b.y, eps);
        EXPECT_NEAR(a.z, b.z, eps);
    }

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

    // ---- the maths ------------------------------------------------------------------------------------------

    TEST(AmbientCubeMath, aSurfaceFacingAnAxisGetsThatFacesColour)
    {
        const Cube c = cube();
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(1, 0, 0)), c[0]);
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(-1, 0, 0)), c[1]);
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(0, 1, 0)), c[2]);
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(0, -1, 0)), c[3]);
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(0, 0, 1)), c[4]);
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(0, 0, -1)), c[5]);
    }

    TEST(AmbientCubeMath, aDiagonalNormalWeighsTwoFacesByTheSquaredComponents)
    {
        const Cube c = cube();
        const float h = std::sqrt(0.5f);
        // half +X, half +Z
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(h, 0, h)), (c[0] + c[4]) * 0.5f);
        // half -Y, half -Z
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(0, -h, -h)), (c[3] + c[5]) * 0.5f);
        // 3 / 4 +X, 1 / 4 -Z
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(std::sqrt(0.75f), 0, -0.5f)), c[0] * 0.75f + c[5] * 0.25f);
    }

    TEST(AmbientCubeMath, theNormalNeedNotBeAUnitVectorAndAZeroNormalFacesUp)
    {
        const Cube c = cube();
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(0, 0, 7)), c[4]);
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(0, 0, 0)), c[4]);
        expectNear(Render::AmbientCube::sample(c, vsg::vec3(3, 0, 3)), (c[0] + c[4]) * 0.5f);
    }

    TEST(AmbientCubeMath, sixEqualColoursAreTheFlatAmbientForEveryNormal)
    {
        Cube flat;
        flat.fill(vsg::vec3(0.25f, 0.3f, 0.35f));
        for (const vsg::vec3 n : { vsg::vec3(1, 2, 3), vsg::vec3(-1, 0.1f, -4), vsg::vec3(0, 0, -1) })
            expectNear(Render::AmbientCube::sample(flat, n), vsg::vec3(0.25f, 0.3f, 0.35f), 1e-5f);
    }

    TEST(AmbientCubeMath, theAverageOfTheSixIsTheFlatColourOfAUniformSphere)
    {
        const Cube c = cube();
        expectNear(Render::AmbientCube::average(c), vsg::vec3(0.85f, 0.95f, 1.05f));
    }

    TEST(AmbientCubeMath, theBytesBecomeFloatsByOver255InTheFaceOrder)
    {
        std::array<std::array<std::uint8_t, 3>, 6> bytes{};
        bytes[0] = { 255, 0, 51 };
        bytes[5] = { 0, 255, 102 };
        const Cube c = Render::AmbientCube::fromBytes(bytes);
        expectNear(c[0], vsg::vec3(1.f, 0.f, 0.2f));
        expectNear(c[5], vsg::vec3(0.f, 1.f, 0.4f));
        expectNear(c[2], vsg::vec3(0.f, 0.f, 0.f));
    }

    TEST(AmbientCubeMath, aDimCubeIsBrightenedTogetherToTheMinimumLuminanceAndABrightOneIsLeftAlone)
    {
        Cube dim;
        dim.fill(vsg::vec3(0.01f, 0.01f, 0.01f));
        dim[4] = vsg::vec3(0.03f, 0.03f, 0.03f); // average 0.0133
        const Cube out = Render::AmbientCube::brightened(dim, 0.04f);
        expectNear(Render::AmbientCube::average(out), vsg::vec3(0.04f, 0.04f, 0.04f), 1e-4f);
        // the faces keep their ratios
        EXPECT_NEAR(out[4].x / out[0].x, 3.f, 1e-4f);
        const Cube bright = cube();
        EXPECT_EQ(Render::AmbientCube::brightened(bright, 0.04f), bright);
    }

    TEST(AmbientCubeMath, aBlackCubeBecomesTheMinimumGrey)
    {
        const Cube out = Render::AmbientCube::brightened(Cube{}, 0.04f);
        for (const vsg::vec3& face : out)
            expectNear(face, vsg::vec3(0.04f, 0.04f, 0.04f));
    }

    TEST(AmbientCubeMath, whatTheFlatAmbientGainedSinceTheBaselineIsAddedAndTheResultNeverGoesBelowZero)
    {
        const Cube c = cube();
        const vsg::vec3 baseline(0.3f, 0.3f, 0.3f);
        // Night Eye raised the flat colour by 0.7
        expectNear(
            Render::AmbientCube::ambientFor(c, vsg::vec3(0, 0, 1), baseline + vsg::vec3(0.7f, 0.7f, 0.7f), baseline),
            c[4] + vsg::vec3(0.7f, 0.7f, 0.7f));
        // unchanged flat colour: the cube alone
        expectNear(Render::AmbientCube::ambientFor(c, vsg::vec3(1, 0, 0), baseline, baseline), c[0]);
        // a flat colour far under its baseline cannot make light negative
        const vsg::vec3 out
            = Render::AmbientCube::ambientFor(c, vsg::vec3(1, 0, 0), vsg::vec3(0, 0, 0), baseline + vsg::vec3(5, 5, 5));
        EXPECT_GE(out.x, 0.f);
        EXPECT_GE(out.y, 0.f);
        EXPECT_GE(out.z, 0.f);
    }

    // ---- the header buffer ---------------------------------------------------------------------------------------

    TEST(AmbientCubeGpu, theCubeRidesAfterTheCloudDataAndTakesEightVec4s)
    {
        EXPECT_EQ(Render::AmbientCube::base, Render::Cloud::headerTotalVec4s);
        EXPECT_EQ(Render::AmbientCube::vec4s, 8u);
        EXPECT_EQ(Render::AmbientCube::headerTotalVec4s, Render::Cloud::headerTotalVec4s + 8u);
    }

    TEST(AmbientCubeGpu, writeStoresTheFlagTheBaselineAndTheSixColours)
    {
        std::vector<vsg::vec4> header(Render::AmbientCube::headerTotalVec4s, vsg::vec4(-7.f, -7.f, -7.f, -7.f));
        const Cube c = cube();
        const vsg::vec3 baseline(0.2f, 0.3f, 0.4f);
        Render::AmbientCube::write(header.data(), &c, baseline);
        const std::size_t b = Render::AmbientCube::base;
        EXPECT_EQ(header[b].x, 1.f);
        expectNear(vsg::vec3(header[b + 1].x, header[b + 1].y, header[b + 1].z), baseline);
        for (std::size_t i = 0; i < 6; ++i)
            expectNear(vsg::vec3(header[b + 2 + i].x, header[b + 2 + i].y, header[b + 2 + i].z), c[i]);
        // nothing before the cube's own entries was touched
        EXPECT_EQ(header[b - 1], vsg::vec4(-7.f, -7.f, -7.f, -7.f));
    }

    TEST(AmbientCubeGpu, writingNoCubeSaysOffAndTouchesNothingElse)
    {
        std::vector<vsg::vec4> header(Render::AmbientCube::headerTotalVec4s, vsg::vec4(-7.f, -7.f, -7.f, -7.f));
        Render::AmbientCube::write(header.data(), nullptr, vsg::vec3(0.1f, 0.1f, 0.1f));
        const std::size_t b = Render::AmbientCube::base;
        EXPECT_EQ(header[b].x, 0.f);
        for (std::size_t i = 0; i < b; ++i)
            ASSERT_EQ(header[i], vsg::vec4(-7.f, -7.f, -7.f, -7.f)) << i;
    }

    // ---- the pipeline and the shaders -----------------------------------------------------------------------

    TEST(AmbientCubePipeline, theModeIsADefineOfTheFragmentStageThatALoaderCanRequest)
    {
        EXPECT_EQ(Pipeline::getDefine(Pipeline::Mode::AMBIENT_CUBE), "AMBIENT_CUBE");
        EXPECT_NE(
            std::find(Pipeline::fragmentModes.begin(), Pipeline::fragmentModes.end(), Pipeline::Mode::AMBIENT_CUBE),
            Pipeline::fragmentModes.end());
        EXPECT_EQ(std::find(Pipeline::vertexModes.begin(), Pipeline::vertexModes.end(), Pipeline::Mode::AMBIENT_CUBE),
            Pipeline::vertexModes.end());
    }

    TEST(AmbientCubeShaders, theShaderCarriesTheSameBaseAndTheSameRule)
    {
        const std::string base = std::to_string(Render::AmbientCube::base);
        const std::string slang = readSource("lib/view/ambientcube.slang");
        EXPECT_TRUE(contains(slang, "static const int AMBIENT_CUBE_BASE = " + base + ";"));
        EXPECT_TRUE(contains(slang, "skyHeader[AMBIENT_CUBE_BASE]")) << "the flag entry";
        EXPECT_TRUE(contains(slang, "AMBIENT_CUBE_BASE + 1")) << "the baseline";
        EXPECT_TRUE(contains(slang, "AMBIENT_CUBE_BASE + (n.x >= 0.0 ? 2 : 3)")) << "the first colour";
    }

    TEST(AmbientCubeShaders, theObjectShaderTakesTheCubeBeforeTheProbesAndLightsWithTheAmbientTerm)
    {
        const std::string slang = readSource("default/default.frag.slang");
        EXPECT_TRUE(contains(slang, "lib/view/ambientcube.slang"));
        EXPECT_TRUE(contains(slang, "ambientCubeAmbient("));
        EXPECT_TRUE(contains(slang, "defined(GI) || defined(AMBIENT_CUBE)"));
    }
}
