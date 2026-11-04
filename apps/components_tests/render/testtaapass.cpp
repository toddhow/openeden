// Tests for the parts of temporal anti-aliasing (task 0075) that are not pure maths but need no device: the jittered
// projection, the pass's per-frame state, and the twin of the constants and the push-constant block in
// files/shaders/post/taa.slang. See docs/tasks/0075-temporal-anti-aliasing.md.

#include <components/render/taa.hpp>
#include <components/render/taamath.hpp>

#include <gtest/gtest.h>

#include <vsg/app/ProjectionMatrix.h>
#include <vsg/maths/transform.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>

namespace
{
    using namespace Render;

    vsg::dvec4 transformPoint(const vsg::dmat4& m, const vsg::dvec4& p)
    {
        return m * p;
    }

    vsg::ref_ptr<vsg::Perspective> makePerspective()
    {
        auto p = vsg::Perspective::create();
        p->fieldOfViewY = 60.0;
        p->aspectRatio = 16.0 / 9.0;
        p->nearDistance = 4.0;
        p->farDistance = 40000.0;
        return p;
    }

    TEST(RenderTaaProjection, withNoOffsetItIsTheBaseProjection)
    {
        auto base = makePerspective();
        auto jitter = Taa::JitterOffset::create();
        auto projection = Taa::JitteredProjection::create(base, jitter);
        const vsg::dmat4 a = projection->transform();
        const vsg::dmat4 b = base->transform();
        for (std::size_t c = 0; c < 4; ++c)
            for (std::size_t r = 0; r < 4; ++r)
                EXPECT_DOUBLE_EQ(a(c, r), b(c, r)) << c << "," << r;
    }

    TEST(RenderTaaProjection, theOffsetShiftsTheClipPositionByNdcTimesW)
    {
        auto base = makePerspective();
        auto jitter = Taa::JitterOffset::create();
        jitter->ndc = vsg::dvec2(0.002, -0.003);
        auto projection = Taa::JitteredProjection::create(base, jitter);
        const vsg::dvec4 view(10.0, -5.0, -300.0, 1.0);
        const vsg::dvec4 plain = transformPoint(base->transform(), view);
        const vsg::dvec4 shifted = transformPoint(projection->transform(), view);
        EXPECT_NEAR(shifted.x / shifted.w, plain.x / plain.w + 0.002, 1e-12);
        EXPECT_NEAR(shifted.y / shifted.w, plain.y / plain.w - 0.003, 1e-12);
        // Depth and w are not touched, so the depth buffer and the linear depth stay what they were.
        EXPECT_DOUBLE_EQ(shifted.z, plain.z);
        EXPECT_DOUBLE_EQ(shifted.w, plain.w);
    }

    TEST(RenderTaaProjection, theShiftIsTheSameForNearAndFarPoints)
    {
        auto base = makePerspective();
        auto jitter = Taa::JitterOffset::create();
        jitter->ndc = vsg::dvec2(0.001, 0.0005);
        auto projection = Taa::JitteredProjection::create(base, jitter);
        for (const double z : { -5.0, -100.0, -10000.0 })
        {
            const vsg::dvec4 p(3.0, 2.0, z, 1.0);
            const vsg::dvec4 a = transformPoint(base->transform(), p);
            const vsg::dvec4 b = transformPoint(projection->transform(), p);
            EXPECT_NEAR(b.x / b.w - a.x / a.w, 0.001, 1e-12) << z;
            EXPECT_NEAR(b.y / b.w - a.y / a.w, 0.0005, 1e-12) << z;
        }
    }

    TEST(RenderTaaProjection, itFollowsTheBaseAndTheSharedOffset)
    {
        auto base = makePerspective();
        auto jitter = Taa::JitterOffset::create();
        auto projection = Taa::JitteredProjection::create(base, jitter);
        base->fieldOfViewY = 90.0;
        jitter->ndc = vsg::dvec2(0.004, 0.0);
        const vsg::dvec4 p(0.0, 0.0, -10.0, 1.0);
        const vsg::dvec4 c = transformPoint(projection->transform(), p);
        EXPECT_NEAR(c.x / c.w, 0.004, 1e-12);
        // The field of view change reached it: a point on the edge of a 90 degree view lies at |y| = 1.
        const vsg::dvec4 edge = transformPoint(projection->transform(), vsg::dvec4(0.0, 10.0, -10.0, 1.0));
        EXPECT_NEAR(std::abs(edge.y / edge.w), 1.0, 1e-9);
    }

    TEST(RenderTaaProjection, aResizeDoesNotRescaleTheOffset)
    {
        auto base = makePerspective();
        auto jitter = Taa::JitterOffset::create();
        jitter->ndc = vsg::dvec2(0.002, 0.002);
        auto projection = Taa::JitteredProjection::create(base, jitter);
        const vsg::dmat4 before = projection->transform();
        projection->changeExtent(VkExtent2D{ 1280, 720 }, VkExtent2D{ 1920, 1080 });
        const vsg::dmat4 after = projection->transform();
        for (std::size_t c = 0; c < 4; ++c)
            for (std::size_t r = 0; r < 4; ++r)
                EXPECT_DOUBLE_EQ(before(c, r), after(c, r));
    }

    TEST(RenderTaaProjection, theJitterMatrixOfTheSequenceKeepsAPointInsideItsPixel)
    {
        // The whole sequence moves a pixel centre by less than half a pixel in each direction.
        const std::uint32_t width = 1280;
        const std::uint32_t height = 720;
        for (std::uint32_t frame = 0; frame < 2 * Taa::jitterPeriod; ++frame)
        {
            const vsg::dvec2 ndc = Taa::jitterNdc(Taa::jitterPixels(frame, 1.f), width, height);
            EXPECT_LT(std::abs(ndc.x) * width / 2.0, 0.5 + 1e-12) << frame;
            EXPECT_LT(std::abs(ndc.y) * height / 2.0, 0.5 + 1e-12) << frame;
        }
    }

    TEST(RenderTaaPass, theFrameStateFollowsTheJitterSequence)
    {
        auto jitter = Taa::JitterOffset::create();
        TaaPass pass(nullptr, nullptr, Taa::Settings{}, jitter);
        Taa::FrameInput input;
        for (std::uint32_t frame = 0; frame < 20; ++frame)
        {
            pass.beginFrame(input);
            const Taa::TemporalState& state = pass.state();
            const vsg::dvec2 expected = Taa::jitterPixels(frame, 1.f);
            EXPECT_DOUBLE_EQ(state.jitterPixels.x, expected.x) << frame;
            EXPECT_DOUBLE_EQ(state.jitterPixels.y, expected.y) << frame;
            EXPECT_EQ(state.frame, frame + 1);
            // Nothing has been built (no device here): the history can never be used.
            EXPECT_FALSE(state.historyValid);
            if (frame > 0)
            {
                const vsg::dvec2 previous = Taa::jitterPixels(frame - 1, 1.f);
                EXPECT_DOUBLE_EQ(state.previousJitterPixels.x, previous.x) << frame;
                EXPECT_DOUBLE_EQ(state.previousJitterPixels.y, previous.y) << frame;
            }
        }
    }

    TEST(RenderTaaPass, twoPassesGiveTheSameSequence)
    {
        auto jitterA = Taa::JitterOffset::create();
        auto jitterB = Taa::JitterOffset::create();
        Taa::Settings settings;
        settings.jitterScale = 0.5f;
        TaaPass a(nullptr, nullptr, settings, jitterA);
        TaaPass b(nullptr, nullptr, settings, jitterB);
        Taa::FrameInput input;
        for (int i = 0; i < 40; ++i)
        {
            a.beginFrame(input);
            b.beginFrame(input);
            EXPECT_DOUBLE_EQ(a.state().jitterPixels.x, b.state().jitterPixels.x);
            EXPECT_DOUBLE_EQ(a.state().jitterPixels.y, b.state().jitterPixels.y);
        }
    }

    TEST(RenderTaaPass, aJitterScaleOfZeroGivesNoJitter)
    {
        auto jitter = Taa::JitterOffset::create();
        Taa::Settings settings;
        settings.jitterScale = 0.f;
        TaaPass pass(nullptr, nullptr, settings, jitter);
        Taa::FrameInput input;
        for (int i = 0; i < 20; ++i)
        {
            pass.beginFrame(input);
            EXPECT_EQ(pass.state().jitterPixels.x, 0.0);
            EXPECT_EQ(pass.state().jitterPixels.y, 0.0);
        }
    }

    // --- depthRejectRange (added with the implementation: the range form of depthReject) ----------------------------

    TEST(RenderTaaDepthRange, aRangeOfOneValueIsDepthReject)
    {
        for (const float expected : { 0.f, 1.f, 50.f, 400.f, 9000.f })
            for (const float stored : { -1.f, 0.5f, 3.f, 6.f, 12.f })
                EXPECT_FLOAT_EQ(Taa::depthRejectRange(expected, stored, stored), Taa::depthReject(expected, stored))
                    << expected << " " << stored;
    }

    TEST(RenderTaaDepthRange, aDepthInsideTheRangeIsKept)
    {
        const float e = Taa::encodeDepth(100.f);
        EXPECT_FLOAT_EQ(Taa::depthRejectRange(100.f, e - 3.f, e + 3.f), 0.f);
        EXPECT_FLOAT_EQ(Taa::depthRejectRange(100.f, e, e + 3.f), 0.f);
        EXPECT_FLOAT_EQ(Taa::depthRejectRange(100.f, e - 3.f, e), 0.f);
    }

    TEST(RenderTaaDepthRange, aDepthFarOutsideTheRangeIsRejected)
    {
        const float e = Taa::encodeDepth(100.f);
        EXPECT_FLOAT_EQ(Taa::depthRejectRange(100.f, e + 1.f, e + 2.f), 1.f);
        EXPECT_FLOAT_EQ(Taa::depthRejectRange(100.f, e - 2.f, e - 1.f), 1.f);
    }

    TEST(RenderTaaDepthRange, theDistanceIsToTheNearerEdgeOfTheRange)
    {
        const float e = Taa::encodeDepth(100.f);
        const float gap = 0.5f * (Taa::depthRejectLow + Taa::depthRejectHigh);
        // Half way between the limits: smoothstep(0.5) = 0.5.
        EXPECT_NEAR(Taa::depthRejectRange(100.f, e + gap, e + 5.f), 0.5f, 1e-5f);
        EXPECT_NEAR(Taa::depthRejectRange(100.f, e - 5.f, e - gap), 0.5f, 1e-5f);
    }

    TEST(RenderTaaDepthRange, aSurfaceOverNothingButSkyIsRejectedAndOverASkyAndSurfaceMixIsNot)
    {
        EXPECT_FLOAT_EQ(Taa::depthRejectRange(100.f, -1.f, -1.f), 1.f);
        // The pixel's surroundings held sky and a surface at about the same depth: a silhouette, not a disocclusion.
        EXPECT_FLOAT_EQ(Taa::depthRejectRange(100.f, -1.f, Taa::encodeDepth(100.f)), 0.f);
        // Nothing opaque now and sky before: nothing to reject.
        EXPECT_FLOAT_EQ(Taa::depthRejectRange(0.f, -1.f, -1.f), 0.f);
    }

    // --- The shader twin -------------------------------------------------------------------------------------------

    std::string readShaderFile()
    {
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / "post" / "taa.slang";
        std::ifstream in(source);
        if (!in)
            return {};
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    // The value of `static const <type> <name> = <value>;` in a Slang file.
    std::optional<double> slangNumber(const std::string& text, const std::string& name)
    {
        const std::regex re("static\\s+const\\s+\\w+\\s+" + name + "\\s*=\\s*([-+0-9.eE]+)[uUfF]?\\s*;");
        std::smatch m;
        if (!std::regex_search(text, m, re))
            return std::nullopt;
        return std::stod(m[1].str());
    }

    void expectSlangNumber(const std::string& text, const std::string& name, double expected)
    {
        const auto value = slangNumber(text, name);
        ASSERT_TRUE(value.has_value()) << name << " is not declared in taa.slang";
        EXPECT_NEAR(*value, expected, std::max(1e-9, std::fabs(expected) * 1e-6)) << name;
    }

    TEST(RenderTaaShader, theConstantsMatchTheCppConstants)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        expectSlangNumber(slang, "velocityFullPixels", Taa::velocityFullPixels);
        expectSlangNumber(slang, "movingWeightShare", Taa::movingWeightShare);
        expectSlangNumber(slang, "depthRejectLow", Taa::depthRejectLow);
        expectSlangNumber(slang, "depthRejectHigh", Taa::depthRejectHigh);
        expectSlangNumber(slang, "varianceGamma", Taa::varianceGamma);
    }

    TEST(RenderTaaShader, thePushConstantBlockIsTheMatrixThenThreeVectors)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const std::size_t block = slang.find("struct Push");
        ASSERT_NE(block, std::string::npos);
        const std::size_t end = slang.find("};", block);
        ASSERT_NE(end, std::string::npos);
        const std::string body = slang.substr(block, end - block);
        const std::size_t a = body.find("float4x4 toPrevClip");
        const std::size_t b = body.find("float4 projScale");
        const std::size_t c = body.find("float4 params");
        const std::size_t d = body.find("float4 look");
        ASSERT_NE(a, std::string::npos);
        ASSERT_NE(b, std::string::npos);
        ASSERT_NE(c, std::string::npos);
        ASSERT_NE(d, std::string::npos);
        EXPECT_LT(a, b);
        EXPECT_LT(b, c);
        EXPECT_LT(c, d);
        // TaaPass declares a range of exactly this size, and 128 bytes is all a device has to give.
        EXPECT_LE(sizeof(vsg::mat4) + 3 * sizeof(vsg::vec4), 128u);
    }

    TEST(RenderTaaShader, theBindingsAreTheOnesTheCppSideDeclares)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const char* const expected[]
            = { "[[vk::binding(0, 0)]] Sampler2D colourTex", "[[vk::binding(1, 0)]] Sampler2D depthTex",
                  "[[vk::binding(2, 0)]] Sampler2D velocityTex", "[[vk::binding(3, 0)]] Sampler2D historyTex",
                  "[[vk::binding(4, 0)]] [format(\"rgba16f\")] RWTexture2D<float4> resolveStore",
                  "[[vk::binding(5, 0)]] [format(\"rgba16f\")] RWTexture2D<float4> historyStore",
                  "[[vk::binding(6, 0)]] [format(\"rgba16f\")] RWTexture2D<float4> outputStore" };
        for (const char* line : expected)
            EXPECT_NE(slang.find(line), std::string::npos) << line;
    }
}
