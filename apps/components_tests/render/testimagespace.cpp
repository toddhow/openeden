// Task 0127, stage W-S4: the image space grade on the tonemapped picture. The CPU twin (Render::ImageSpace::apply) is
// pinned with numbers worked out by hand; the push constants of Render::Tonemap and the shader twins are checked
// against it. Written before the code.
#include <gtest/gtest.h>

#include <components/render/imagespacemath.hpp>
#include <components/render/tonemap.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
    using namespace Render::ImageSpace;

    void expectRgb(const Rgb& got, float r, float g, float b, float tolerance = 1e-5f)
    {
        EXPECT_NEAR(got[0], r, tolerance);
        EXPECT_NEAR(got[1], g, tolerance);
        EXPECT_NEAR(got[2], b, tolerance);
    }

    TEST(RenderImageSpace, lumaUsesTheRec709Weights)
    {
        // 0.2126 * 0.2 + 0.7152 * 0.5 + 0.0722 * 0.8
        EXPECT_NEAR(luma({ 0.2f, 0.5f, 0.8f }), 0.45788f, 1e-5f);
        EXPECT_NEAR(luma({ 1.f, 1.f, 1.f }), 1.f, 1e-6f);
        EXPECT_NEAR(luma({ 1.f, 0.f, 0.f }), 0.2126f, 1e-6f);
    }

    TEST(RenderImageSpace, aNeutralGradeChangesNothing)
    {
        expectRgb(apply({ 0.2f, 0.5f, 0.8f }, Grade{}), 0.2f, 0.5f, 0.8f);
        expectRgb(apply({ 0.f, 1.f, 0.33f }, Grade{}), 0.f, 1.f, 0.33f);
    }

    TEST(RenderImageSpace, saturationZeroIsTheGreyOfTheLuma)
    {
        Grade g;
        g.saturation = 0.f;
        expectRgb(apply({ 0.2f, 0.5f, 0.8f }, g), 0.45788f, 0.45788f, 0.45788f);
    }

    TEST(RenderImageSpace, saturationScalesTheDistanceFromTheLumaAndTheResultIsClamped)
    {
        Grade g;
        g.saturation = 2.f;
        // luma + 2 (c - luma): -0.05788 (clamped to 0), 0.54212, 1.14212 (clamped to 1)
        expectRgb(apply({ 0.2f, 0.5f, 0.8f }, g), 0.f, 0.54212f, 1.f);
    }

    TEST(RenderImageSpace, contrastTurnsAroundOneHalf)
    {
        Grade g;
        g.contrast = 2.f;
        expectRgb(apply({ 0.25f, 0.5f, 0.75f }, g), 0.f, 0.5f, 1.f);
        g.contrast = 0.5f;
        expectRgb(apply({ 0.f, 0.5f, 1.f }, g), 0.25f, 0.5f, 0.75f);
    }

    TEST(RenderImageSpace, aGreyTintOfFullAmountIsTheGreyOfTheLuma)
    {
        // a tint is a colour to colorize with, kept at the picture's own brightness: (0.5, 0.5, 0.5) has no hue
        Grade g;
        g.tintAmount = 1.f;
        g.tint = { 0.5f, 0.5f, 0.5f };
        expectRgb(apply({ 0.2f, 0.5f, 0.8f }, g), 0.45788f, 0.45788f, 0.45788f);
        g.tintAmount = 0.5f;
        expectRgb(apply({ 0.2f, 0.5f, 0.8f }, g), 0.32894f, 0.47894f, 0.62894f);
    }

    TEST(RenderImageSpace, aColouredTintKeepsTheBrightnessOfTheTint)
    {
        // tint (0.4, 0.2, 0.1): luma 0.2126 * 0.4 + 0.7152 * 0.2 + 0.0722 * 0.1 = 0.08504 + 0.14304 + 0.00722 = 0.2353;
        // normalised (1.69996, 0.84998, 0.42499); the picture's grey (0.5, 0.5, 0.5) has luma 0.5 -> (0.84998, 0.42499,
        // 0.212495)
        Grade g;
        g.tintAmount = 1.f;
        g.tint = { 0.4f, 0.2f, 0.1f };
        const Rgb out = apply({ 0.5f, 0.5f, 0.5f }, g);
        expectRgb(out, 0.84998f, 0.42499f, 0.212495f, 1e-4f);
        EXPECT_NEAR(luma(out), 0.5f, 1e-3f);
    }

    TEST(RenderImageSpace, aBlackTintIsLeftAlone)
    {
        Grade g;
        g.tintAmount = 1.f;
        g.tint = { 0.f, 0.f, 0.f }; // no luma to keep: not a colour at all
        expectRgb(apply({ 0.2f, 0.5f, 0.8f }, g), 0.2f, 0.5f, 0.8f);
    }

    TEST(RenderImageSpace, theOrderIsSaturationThenTintThenContrast)
    {
        Grade g;
        g.saturation = 0.5f;
        g.contrast = 2.f;
        // saturation: luma 0.45788 + 0.5 (c - luma) = (0.32894, 0.47894, 0.62894); contrast: (c - 0.5) * 2 + 0.5
        expectRgb(apply({ 0.2f, 0.5f, 0.8f }, g), 0.15788f, 0.45788f, 0.75788f);
    }

    TEST(RenderImageSpace, notANumberOrInfinityNeverComesOut)
    {
        Grade g;
        g.saturation = std::numeric_limits<float>::quiet_NaN();
        g.contrast = std::numeric_limits<float>::infinity();
        const Rgb out = apply({ 0.2f, 0.5f, 0.8f }, g);
        for (float v : out)
        {
            EXPECT_TRUE(std::isfinite(v));
            EXPECT_GE(v, 0.f);
            EXPECT_LE(v, 1.f);
        }
        // and an input that is not finite is black, not a hole in the picture
        const Rgb bad = apply({ std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.8f }, Grade{});
        for (float v : bad)
            EXPECT_TRUE(std::isfinite(v));
    }

    // ---- the push constants and the shaders ------------------------------------------------------------------------

    TEST(RenderImageSpacePush, theGradeRidesInTwoVec4s)
    {
        Render::Tonemap::Parameters p;
        p.gradeOn = true;
        p.gradeSaturation = 1.5f;
        p.gradeContrast = 1.25f;
        p.gradeTintAmount = 0.5f;
        p.gradeTint = { 0.1f, 0.2f, 0.3f };
        const vsg::vec4 a = Render::Tonemap::gradePushConstants(p);
        EXPECT_FLOAT_EQ(a.x, 1.5f);
        EXPECT_FLOAT_EQ(a.y, 1.25f);
        EXPECT_FLOAT_EQ(a.z, 0.5f);
        EXPECT_FLOAT_EQ(a.w, 1.f);
        const vsg::vec4 b = Render::Tonemap::gradeTintPushConstants(p);
        EXPECT_FLOAT_EQ(b.x, 0.1f);
        EXPECT_FLOAT_EQ(b.y, 0.2f);
        EXPECT_FLOAT_EQ(b.z, 0.3f);
    }

    TEST(RenderImageSpacePush, withTheGradeOffTheSwitchIsZero)
    {
        const Render::Tonemap::Parameters p; // default: off
        EXPECT_FLOAT_EQ(Render::Tonemap::gradePushConstants(p).w, 0.f);
        EXPECT_FLOAT_EQ(Render::Tonemap::gradePushConstants(p).x, 1.f);
        EXPECT_FLOAT_EQ(Render::Tonemap::gradePushConstants(p).y, 1.f);
        EXPECT_FLOAT_EQ(Render::Tonemap::gradePushConstants(p).z, 0.f);
    }

    TEST(RenderImageSpacePush, theDefaultPushConstantsAreExactlyWhatTheyWere)
    {
        // The grade is a separate push constant: the first one is untouched.
        const vsg::vec4 push = Render::Tonemap::pushConstants(Render::Tonemap::Parameters{});
        EXPECT_FLOAT_EQ(push.x, 1.f);
        EXPECT_FLOAT_EQ(push.y, 0.f);
        EXPECT_FLOAT_EQ(push.z, 0.f);
        EXPECT_FLOAT_EQ(push.w, 0.f);
    }

    std::string shaderText(const char* file)
    {
        std::ifstream in(std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / file, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    TEST(RenderImageSpaceShader, bothTonemapShadersHaveTheGradeBehindTheirOwnDefineAndTheSameWeights)
    {
        for (const char* file : { "post/tonemap.frag", "post/tonemap.slang" })
        {
            const std::string text = shaderText(file);
            ASSERT_FALSE(text.empty()) << file;
            EXPECT_NE(text.find("GRADE"), std::string::npos) << file;
            EXPECT_NE(text.find("0.2126"), std::string::npos) << file;
            EXPECT_NE(text.find("0.7152"), std::string::npos) << file;
            EXPECT_NE(text.find("0.0722"), std::string::npos) << file;
        }
        // The GLSL declares the define it reads, so a pipeline without it is the old pipeline.
        EXPECT_NE(shaderText("post/tonemap.frag").find("import_defines (AO, BLOOM, DEBUG_VIEW, GRADE, SSS)"),
            std::string::npos);
    }
}
