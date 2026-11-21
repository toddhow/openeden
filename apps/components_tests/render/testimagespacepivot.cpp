// Task 0362a: the grade's contrast pivots on a value the shader computes (the scene's average brightness).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#include <components/render/imagespacemath.hpp>

namespace
{
    using namespace Render::ImageSpace;

    TEST(ImageSpacePivot, theDefaultPivotIsMidGreyAsBefore)
    {
        Grade g;
        EXPECT_FLOAT_EQ(g.pivot, 0.5f);
        g.contrast = 2.f;
        EXPECT_NEAR(apply({ 0.25f, 0.5f, 0.75f }, g)[0], 0.f, 1e-6f);
    }

    TEST(ImageSpacePivot, aDarkPivotKeepsDarkValuesInsteadOfCrushingThem)
    {
        Grade g;
        g.contrast = 1.2f;
        g.pivot = 0.2f;
        // (0.1 - 0.2) * 1.2 + 0.2 = 0.08: not black; with a mid grey pivot it is 0.
        EXPECT_NEAR(apply({ 0.1f, 0.1f, 0.1f }, g)[0], 0.08f, 1e-6f);
        g.pivot = 0.5f;
        EXPECT_NEAR(apply({ 0.1f, 0.1f, 0.1f }, g)[0], 0.02f, 1e-6f);
    }

    TEST(ImageSpacePivot, theValueAtThePivotNeverMoves)
    {
        Grade g;
        g.contrast = 3.f;
        g.pivot = 0.3f;
        EXPECT_NEAR(apply({ 0.3f, 0.3f, 0.3f }, g)[1], 0.3f, 1e-6f);
    }

    TEST(ImageSpacePivot, aBadPivotIsMidGrey)
    {
        Grade g;
        g.contrast = 2.f;
        g.pivot = std::numeric_limits<float>::quiet_NaN();
        EXPECT_NEAR(apply({ 0.75f, 0.75f, 0.75f }, g)[0], 1.f, 1e-6f);
    }

    std::string readFile(const char* name)
    {
        std::ifstream in(std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / name, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    TEST(ImageSpacePivot, bothShadersPivotOnTheAverageAndNotOnOneHalf)
    {
        for (const char* file : { "files/shaders/post/tonemap.frag", "files/shaders/post/tonemap.slang" })
        {
            const std::string text = readFile(file);
            ASSERT_FALSE(text.empty()) << file;
            EXPECT_NE(text.find("gradePivot()"), std::string::npos) << file;
            EXPECT_NE(text.find("contrast + vec3(pivot)") == std::string::npos
                    ? text.find("contrast + float3(pivot)")
                    : text.find("contrast + vec3(pivot)"),
                std::string::npos)
                << file;
            EXPECT_EQ(text.find("- vec3(0.5)) * contrast"), std::string::npos) << file;
            EXPECT_EQ(text.find("- float3(0.5)) * contrast"), std::string::npos) << file;
        }
    }
}
