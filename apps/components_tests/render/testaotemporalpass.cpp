// Tests for the parts of the ambient occlusion accumulation (task 0076) that are not pure maths but need no device:
// the pass's per-frame state, and the twin of the constants, the push-constant block and the bindings in
// files/shaders/post/aotemporal.slang. See docs/tasks/0076-ao-temporal-accumulation.md.

#include <components/render/aomath.hpp>
#include <components/render/aotemporal.hpp>
#include <components/render/aotemporalmath.hpp>

#include <gtest/gtest.h>

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

    TEST(RenderAoTemporalPass, theFrameStateCountsWithoutTaaAndFollowsTheNoiseSequence)
    {
        AoTemporalPass pass(nullptr, nullptr, Ao::Temporal::Settings{});
        Ao::Temporal::FrameInput input;
        for (std::uint32_t frame = 0; frame < 20; ++frame)
        {
            pass.beginFrame(input);
            const Ao::Temporal::PushedState& state = pass.state();
            EXPECT_EQ(state.frame, frame);
            const vsg::vec2 expected = Ao::Temporal::noiseOffset(frame);
            EXPECT_FLOAT_EQ(state.noise.x, expected.x) << frame;
            EXPECT_FLOAT_EQ(state.noise.y, expected.y) << frame;
            // Nothing has been built (no device here): the history can never be used.
            EXPECT_FALSE(state.historyUsable) << frame;
            EXPECT_EQ(state.historyWeight, 0.f) << frame;
        }
    }

    TEST(RenderAoTemporalPass, theTaaFrameIndexIsUsedWhenGiven)
    {
        AoTemporalPass pass(nullptr, nullptr, Ao::Temporal::Settings{});
        Ao::Temporal::FrameInput input;
        input.frame = 70;
        pass.beginFrame(input);
        EXPECT_EQ(pass.state().frame, 70u);
        const vsg::vec2 expected = Ao::Temporal::noiseOffset(70);
        EXPECT_FLOAT_EQ(pass.state().noise.x, expected.x);
        EXPECT_FLOAT_EQ(pass.state().noise.y, expected.y);
        // The pass's own counter is not advanced by a frame that came with an index.
        input.frame.reset();
        pass.beginFrame(input);
        EXPECT_EQ(pass.state().frame, 0u);
    }

    TEST(RenderAoTemporalPass, theStaticViewHasNoNoiseOffsetAndNoHistory)
    {
        Ao::Temporal::Settings settings;
        settings.debug = Ao::Temporal::DebugView::Static;
        AoTemporalPass pass(nullptr, nullptr, settings);
        Ao::Temporal::FrameInput input;
        for (std::uint32_t frame = 0; frame < 5; ++frame)
        {
            pass.beginFrame(input);
            EXPECT_EQ(pass.state().noise.x, 0.f);
            EXPECT_EQ(pass.state().noise.y, 0.f);
            EXPECT_EQ(pass.state().historyWeight, 0.f);
        }
    }

    TEST(RenderAoTemporalPass, theVectorsPushedAreTheMatrixRowAndTheRotationRowsWithTheProjectionInTheirW)
    {
        Ao::Temporal::Settings settings;
        settings.clampGamma = 0.75f;
        AoTemporalPass pass(nullptr, nullptr, settings);
        Ao::Temporal::FrameInput input;
        // vsg indexes (column, row): entry 10 * column + row, so a swapped index shows.
        for (std::size_t c = 0; c < 4; ++c)
        {
            for (std::size_t r = 0; r < 4; ++r)
            {
                input.toPreviousClip(c, r) = static_cast<float>(10 * c + r);
            }
        }
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                input.rotation.m[i][j] = static_cast<float>(100 * i + j);
            }
        }
        input.projectionX = 1.5f;
        input.projectionY = 2.5f;
        input.frame = 5;
        pass.beginFrame(input);
        const auto pushed = pass.pushedVectors();
        // The last row of the matrix: the previous camera's depth of a point (its clip position's w).
        EXPECT_EQ(pushed[0].x, 3.f);
        EXPECT_EQ(pushed[0].y, 13.f);
        EXPECT_EQ(pushed[0].z, 23.f);
        EXPECT_EQ(pushed[0].w, 33.f);
        // The rotation's rows (not its columns), with the projection scales and the history weight in w.
        EXPECT_EQ(pushed[1].x, 0.f);
        EXPECT_EQ(pushed[1].y, 1.f);
        EXPECT_EQ(pushed[1].z, 2.f);
        EXPECT_EQ(pushed[1].w, 1.5f);
        EXPECT_EQ(pushed[2].x, 100.f);
        EXPECT_EQ(pushed[2].y, 101.f);
        EXPECT_EQ(pushed[2].z, 102.f);
        EXPECT_EQ(pushed[2].w, 2.5f);
        EXPECT_EQ(pushed[3].x, 200.f);
        EXPECT_EQ(pushed[3].y, 201.f);
        EXPECT_EQ(pushed[3].z, 202.f);
        EXPECT_EQ(pushed[3].w, 0.f); // nothing is built: no history
        // The noise offset of the frame, then the history's jitter shift (none without a history).
        const vsg::vec2 noise = Ao::Temporal::noiseOffset(5);
        EXPECT_FLOAT_EQ(pushed[4].x, noise.x);
        EXPECT_FLOAT_EQ(pushed[4].y, noise.y);
        EXPECT_EQ(pushed[4].z, 0.f);
        EXPECT_EQ(pushed[4].w, 0.f);
        EXPECT_EQ(pushed[5].x, 0.f); // no debug view
        EXPECT_FLOAT_EQ(pushed[5].y, 0.75f);
    }

    // --- The shader twin -------------------------------------------------------------------------------------------

    std::string readShaderFile()
    {
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / "post" / "aotemporal.slang";
        std::ifstream in(source);
        if (!in)
        {
            return {};
        }
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
        {
            return std::nullopt;
        }
        return std::stod(m[1].str());
    }

    void expectSlangNumber(const std::string& text, const std::string& name, double expected)
    {
        const auto value = slangNumber(text, name);
        ASSERT_TRUE(value.has_value()) << name << " is not declared in aotemporal.slang";
        EXPECT_NEAR(*value, expected, std::max(1e-9, std::fabs(expected) * 1e-6)) << name;
    }

    TEST(RenderAoTemporalShader, theConstantsMatchTheCppConstants)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
        {
            GTEST_SKIP() << "the shader source is not beside the test sources";
        }
        expectSlangNumber(slang, "maxHistoryWeight", Ao::Temporal::maxHistoryWeight);
        expectSlangNumber(slang, "maxCount", Ao::Temporal::maxCount);
        expectSlangNumber(slang, "depthRejectLow", Ao::Temporal::depthRejectLow);
        expectSlangNumber(slang, "depthRejectHigh", Ao::Temporal::depthRejectHigh);
        expectSlangNumber(slang, "normalRejectLow", Ao::Temporal::normalRejectLow);
        expectSlangNumber(slang, "normalRejectHigh", Ao::Temporal::normalRejectHigh);
    }

    TEST(RenderAoTemporalShader, theNormalPackingUsesTwelveBitsPerAxis)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
        {
            GTEST_SKIP() << "the shader source is not beside the test sources";
        }
        // Ao::Temporal::packNormal: x * 4096 + y with 4095 levels, as the shader's packNormal/unpackNormal.
        EXPECT_EQ(Ao::Temporal::normalLevels, 4095u);
        EXPECT_NE(slang.find("q.x * 4096.0 + q.y"), std::string::npos);
        EXPECT_NE(slang.find("floor(packedValue / 4096.0)"), std::string::npos);
        EXPECT_NE(slang.find("* 4095.0"), std::string::npos);
    }

    TEST(RenderAoTemporalShader, theFilterWeightsAreTheOnesOfRenderAo)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
        {
            GTEST_SKIP() << "the shader source is not beside the test sources";
        }
        // Render::Ao::bilateralWeight: depth within 2 to 5 percent, normal dot 0.707 to 0.9.
        EXPECT_NE(slang.find("smoothStep(0.02, 0.05, relative)"), std::string::npos);
        EXPECT_NE(slang.find("smoothStep(0.707, 0.9, normalDot)"), std::string::npos);
        EXPECT_FLOAT_EQ(Ao::bilateralWeight(0.f, 100.f, 1.f), 1.f);
    }

    TEST(RenderAoTemporalShader, theSettingsConstantsHaveTheIdsOfTheFragmentPass)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
        {
            GTEST_SKIP() << "the shader source is not beside the test sources";
        }
        EXPECT_NE(slang.find("[vk::constant_id(1100)] const float AO_RADIUS"), std::string::npos);
        EXPECT_NE(slang.find("[vk::constant_id(1101)] const int AO_SLICES"), std::string::npos);
        EXPECT_NE(slang.find("[vk::constant_id(1102)] const int AO_STEPS"), std::string::npos);
    }

    TEST(RenderAoTemporalShader, thePushConstantBlockIsSixVectors)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
        {
            GTEST_SKIP() << "the shader source is not beside the test sources";
        }
        const std::size_t block = slang.find("struct Push");
        ASSERT_NE(block, std::string::npos);
        const std::size_t end = slang.find("};", block);
        ASSERT_NE(end, std::string::npos);
        const std::string body = slang.substr(block, end - block);
        const char* const names[]
            = { "float4 depthRow", "float4 rot0", "float4 rot1", "float4 rot2", "float4 noise", "float4 misc" };
        std::size_t last = 0;
        for (const char* name : names)
        {
            const std::size_t at = body.find(name);
            ASSERT_NE(at, std::string::npos) << name;
            EXPECT_GE(at, last) << name;
            last = at;
        }
        // AoTemporalPass declares a range of exactly this size, and 128 bytes is all a device has to give.
        EXPECT_LE(6 * sizeof(vsg::vec4), 128u);
    }

    TEST(RenderAoTemporalShader, theBindingsAreTheOnesTheCppSideDeclares)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
        {
            GTEST_SKIP() << "the shader source is not beside the test sources";
        }
        const char* const expected[] = { "[[vk::binding(0, 0)]] Sampler2D linearDepth",
            "[[vk::binding(1, 0)]] Sampler2D viewNormal", "[[vk::binding(2, 0)]] Sampler2D velocityTex",
            "[[vk::binding(3, 0)]] [format(\"r32f\")] RWTexture2D<float> rawStore",
            "[[vk::binding(4, 0)]] [format(\"rgba32f\")] RWTexture2D<float4> resolveStore",
            "[[vk::binding(5, 0)]] [format(\"rgba32f\")] RWTexture2D<float4> historyStore" };
        for (const char* line : expected)
        {
            EXPECT_NE(slang.find(line), std::string::npos) << line;
        }
    }

    TEST(RenderAoTemporalShader, theThreeEntryPointsExist)
    {
        const std::string slang = readShaderFile();
        if (slang.empty())
        {
            GTEST_SKIP() << "the shader source is not beside the test sources";
        }
        for (const char* entry : { "void genMain(", "void resolveMain(", "void copyMain(" })
        {
            EXPECT_NE(slang.find(entry), std::string::npos) << entry;
        }
    }
}
