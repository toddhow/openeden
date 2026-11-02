// Tests for the volumetric fog's GPU-facing parts, Render::Volume (task 0041). Written from the contract before the
// implementation: the std140 parameter block, the dispatch helper, the volume's size and the twin of the block in the
// GLSL and Slang shaders. Pure CPU: no device, window or game data.

#include <components/render/volumegpu.hpp>
#include <components/render/volumemath.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace Render::Volume;

    ParamsInput custom()
    {
        ParamsInput in;
        in.grid = { 11, 22, 33 };
        in.enabled = true;
        in.fogEnd = 3000.f;
        in.densityScale = 2.5f;
        in.heightScale = 200.f;
        in.anisotropy = 0.25f;
        in.sunGain = 1.5f;
        in.shadowMaxDistance = 1800.f;
        in.shadowFadeStart = 0.75f;
        in.cascades = 3;
        in.lampGain = 0.6f;
        in.shadowBias = 0.002f;
        in.jitter = { 0.1f, 0.2f, 0.3f };
        in.historyWeight = 0.8f;
        for (std::size_t i = 0; i < 16; ++i)
            in.toPreviousClip[i] = static_cast<float>(i) + 0.5f;
        return in;
    }

    TEST(RenderVolumeGpu, sizesOfTheBlock)
    {
        EXPECT_EQ(paramsVec4s, 10u);
        EXPECT_EQ(paramsFloats, 40u);
        EXPECT_EQ(paramsFloats, paramsVec4s * 4);
    }

    TEST(RenderVolumeGpu, fieldPositionsForACustomInput)
    {
        const auto p = buildParams(custom());
        const float far = farDistance(3000.f);

        // grid
        EXPECT_FLOAT_EQ(p[0], 11.f);
        EXPECT_FLOAT_EQ(p[1], 22.f);
        EXPECT_FLOAT_EQ(p[2], 33.f);
        EXPECT_FLOAT_EQ(p[3], 1.f);
        // range
        EXPECT_FLOAT_EQ(p[4], nearDistance);
        EXPECT_FLOAT_EQ(p[5], far);
        EXPECT_FLOAT_EQ(p[6], std::log(far / nearDistance));
        EXPECT_FLOAT_EQ(p[7], 0.f);
        // fog
        EXPECT_FLOAT_EQ(p[8], 2.5f);
        EXPECT_FLOAT_EQ(p[9], 1.f / 200.f);
        EXPECT_FLOAT_EQ(p[10], 0.f);
        EXPECT_FLOAT_EQ(p[11], 0.25f);
        // sun
        EXPECT_FLOAT_EQ(p[12], 1.5f);
        EXPECT_FLOAT_EQ(p[13], 1800.f);
        EXPECT_FLOAT_EQ(p[14], 0.75f);
        EXPECT_FLOAT_EQ(p[15], 3.f);
        // lamps
        EXPECT_FLOAT_EQ(p[16], 0.6f);
        EXPECT_FLOAT_EQ(p[17], 0.002f);
        EXPECT_FLOAT_EQ(p[18], 0.f);
        EXPECT_FLOAT_EQ(p[19], 0.f);
        // jitter and history
        EXPECT_FLOAT_EQ(p[20], 0.1f);
        EXPECT_FLOAT_EQ(p[21], 0.2f);
        EXPECT_FLOAT_EQ(p[22], 0.3f);
        EXPECT_FLOAT_EQ(p[23], 0.8f);
        // matrix, column-major as given
        for (std::size_t i = 0; i < 16; ++i)
            EXPECT_FLOAT_EQ(p[24 + i], static_cast<float>(i) + 0.5f) << i;
    }

    TEST(RenderVolumeGpu, defaultsUseTheMediumGrid)
    {
        const auto p = buildParams(ParamsInput{});
        EXPECT_FLOAT_EQ(p[0], 160.f);
        EXPECT_FLOAT_EQ(p[1], 90.f);
        EXPECT_FLOAT_EQ(p[2], 64.f);
        EXPECT_FLOAT_EQ(p[3], 1.f);
        EXPECT_FLOAT_EQ(p[4], nearDistance);
        EXPECT_FLOAT_EQ(p[5], 7168.f);
        EXPECT_FLOAT_EQ(p[8], 1.f);
        EXPECT_FLOAT_EQ(p[11], 0.5f);
        EXPECT_FLOAT_EQ(p[20], 0.5f);
        EXPECT_FLOAT_EQ(p[21], 0.5f);
        EXPECT_FLOAT_EQ(p[22], 0.5f);
        EXPECT_FLOAT_EQ(p[23], 0.f);
    }

    TEST(RenderVolumeGpu, farDistanceClampsTheFogEnd)
    {
        ParamsInput in;
        in.fogEnd = 100.f;
        EXPECT_FLOAT_EQ(buildParams(in)[5], 256.f);
        in.fogEnd = 1e6f;
        EXPECT_FLOAT_EQ(buildParams(in)[5], 8192.f);
        in.fogEnd = 7168.f;
        EXPECT_FLOAT_EQ(buildParams(in)[5], 7168.f);
    }

    TEST(RenderVolumeGpu, logTermIsTheLogOfTheRangeRatio)
    {
        for (float fogEnd : { 100.f, 1000.f, 7168.f, 1e6f })
        {
            ParamsInput in;
            in.fogEnd = fogEnd;
            const auto p = buildParams(in);
            EXPECT_FLOAT_EQ(p[6], std::log(p[5] / p[4])) << fogEnd;
            EXPECT_GT(p[6], 0.f) << fogEnd;
        }
    }

    TEST(RenderVolumeGpu, enabledFlagIsTheGridsW)
    {
        ParamsInput in;
        in.enabled = false;
        const auto off = buildParams(in);
        EXPECT_FLOAT_EQ(off[3], 0.f);
        // The counts are still there.
        EXPECT_FLOAT_EQ(off[0], 160.f);
        in.enabled = true;
        EXPECT_FLOAT_EQ(buildParams(in)[3], 1.f);
    }

    TEST(RenderVolumeGpu, heightScaleIsInverted)
    {
        ParamsInput in;
        in.heightScale = 0.f;
        EXPECT_FLOAT_EQ(buildParams(in)[9], 0.f);
        in.heightScale = 100.f;
        EXPECT_FLOAT_EQ(buildParams(in)[9], 0.01f);
        in.heightScale = -5.f;
        EXPECT_FLOAT_EQ(buildParams(in)[9], 0.f);
    }

    TEST(RenderVolumeGpu, anisotropyIsClamped)
    {
        ParamsInput in;
        in.anisotropy = 5.f;
        EXPECT_FLOAT_EQ(buildParams(in)[11], 0.99f);
        in.anisotropy = -5.f;
        EXPECT_FLOAT_EQ(buildParams(in)[11], -0.99f);
        in.anisotropy = -0.3f;
        EXPECT_FLOAT_EQ(buildParams(in)[11], -0.3f);
    }

    TEST(RenderVolumeGpu, historyWeightIsClamped)
    {
        ParamsInput in;
        in.historyWeight = 3.f;
        EXPECT_FLOAT_EQ(buildParams(in)[23], 1.f);
        in.historyWeight = -1.f;
        EXPECT_FLOAT_EQ(buildParams(in)[23], 0.f);
        in.historyWeight = 0.9f;
        EXPECT_FLOAT_EQ(buildParams(in)[23], 0.9f);
    }

    TEST(RenderVolumeGpu, negativeDensityIsClamped)
    {
        ParamsInput in;
        in.densityScale = -2.f;
        EXPECT_FLOAT_EQ(buildParams(in)[8], 0.f);
        in.densityScale = 0.f;
        EXPECT_FLOAT_EQ(buildParams(in)[8], 0.f);
    }

    TEST(RenderVolumeGpu, defaultMatrixIsTheIdentity)
    {
        const auto p = buildParams(ParamsInput{});
        for (std::size_t i = 0; i < 16; ++i)
        {
            const bool diagonal = i % 5 == 0; // 0, 5, 10, 15
            EXPECT_FLOAT_EQ(p[24 + i], diagonal ? 1.f : 0.f) << i;
        }
        EXPECT_FLOAT_EQ(p[24], 1.f);
        EXPECT_FLOAT_EQ(p[29], 1.f);
        EXPECT_FLOAT_EQ(p[34], 1.f);
        EXPECT_FLOAT_EQ(p[39], 1.f);
    }

    TEST(RenderVolumeGpu, disabledParamsAreAllZero)
    {
        const auto p = disabledParams();
        EXPECT_EQ(p.size(), paramsFloats);
        for (std::size_t i = 0; i < p.size(); ++i)
            EXPECT_FLOAT_EQ(p[i], 0.f) << i;
        EXPECT_FLOAT_EQ(p[3], 0.f);
    }

    TEST(RenderVolumeGpu, dispatchGroupsRoundUp)
    {
        EXPECT_EQ(groupSizeXY, 8u);
        EXPECT_EQ(dispatchGroups(80, 8), 10u);
        EXPECT_EQ(dispatchGroups(45, 8), 6u);
        EXPECT_EQ(dispatchGroups(0, 8), 0u);
        EXPECT_EQ(dispatchGroups(1, 8), 1u);
        EXPECT_EQ(dispatchGroups(8, 8), 1u);
        EXPECT_EQ(dispatchGroups(9, 8), 2u);
    }

    TEST(RenderVolumeGpu, volumeSizeIsTexelsTimesEightBytes)
    {
        EXPECT_EQ(volumeBytes(gridSize(Quality::Low)), 80u * 45u * 32u * 8u);
        EXPECT_EQ(volumeBytes(gridSize(Quality::Low)), 921600u);
        EXPECT_EQ(volumeBytes(gridSize(Quality::Medium)), 7372800u);
        EXPECT_EQ(volumeBytes(gridSize(Quality::High)), 24883200u);

        for (Quality q : { Quality::Low, Quality::Medium, Quality::High })
        {
            const GridSize g = gridSize(q);
            EXPECT_EQ(volumeTexels(g), static_cast<std::size_t>(g.x) * g.y * g.z);
            EXPECT_EQ(volumeBytes(g), volumeTexels(g) * 8);
        }
    }

    // The shader twin: the parameter block's members are read back from the GLSL and the Slang text.
    std::string readShaderFile(const std::vector<std::string>& parts)
    {
        std::filesystem::path source
            = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "files" / "shaders";
        for (const std::string& part : parts)
            source /= part;
        std::ifstream in(source);
        if (!in)
            return {};
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::string withoutWhitespace(const std::string& text)
    {
        std::string out;
        for (char c : text)
            if (!std::isspace(static_cast<unsigned char>(c)))
                out.push_back(c);
        return out;
    }

    // The positions of the members, each name followed by ';', searched in increasing order after `blockStart`.
    // Returns false (with a message) if one is missing or out of order.
    ::testing::AssertionResult membersInOrder(const std::string& text, std::size_t blockStart)
    {
        const std::vector<std::string> names{ "grid", "range", "fog", "sun", "lamps", "jitter", "toPreviousClip" };
        std::size_t from = blockStart;
        for (const std::string& name : names)
        {
            const std::regex member("\\b" + name + "\\s*;");
            std::smatch m;
            const std::string rest = text.substr(from);
            if (!std::regex_search(rest, m, member))
                return ::testing::AssertionFailure() << name << " is missing or out of order";
            from += static_cast<std::size_t>(m.position(0)) + static_cast<std::size_t>(m.length(0));
        }
        return ::testing::AssertionSuccess();
    }

    TEST(RenderVolumeGpu, glslBlockHasTheMembersInOrder)
    {
        const std::string glsl = readShaderFile({ "lib", "view", "volume.glsl" });
        if (glsl.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const std::size_t start = glsl.find("uniform VolumeParams");
        ASSERT_NE(start, std::string::npos);
        const std::size_t brace = glsl.find('{', start);
        ASSERT_NE(brace, std::string::npos);
        EXPECT_TRUE(membersInOrder(glsl, brace));
    }

    TEST(RenderVolumeGpu, slangStructHasTheMembersInOrder)
    {
        const std::string slang = readShaderFile({ "comp", "volume", "volume.slang" });
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const std::size_t start = slang.find("struct VolumeParams");
        ASSERT_NE(start, std::string::npos);
        const std::size_t brace = slang.find('{', start);
        ASSERT_NE(brace, std::string::npos);
        EXPECT_TRUE(membersInOrder(slang, brace));
    }

    TEST(RenderVolumeGpu, bindingsAreDeclaredInTheViewSet)
    {
        const std::string bindings = readShaderFile({ "lib", "view", "bindings.glsl" });
        if (bindings.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const std::string flat = withoutWhitespace(bindings);
        // Either "VIEW_VOLUME_BINDING=14" or "#define VIEW_VOLUME_BINDING 14" once the blanks are gone.
        const std::regex volume("VIEW_VOLUME_BINDING=?14(?![0-9])");
        const std::regex params("VIEW_VOLUME_PARAMS_BINDING=?15(?![0-9])");
        EXPECT_TRUE(std::regex_search(flat, volume));
        EXPECT_TRUE(std::regex_search(flat, params));
    }

    TEST(RenderVolumeGpu, slangBindingsMatchTheViewAndVolumeSets)
    {
        const std::string slang = readShaderFile({ "comp", "volume", "volume.slang" });
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const std::string flat = withoutWhitespace(slang);
        EXPECT_NE(flat.find("[[vk::binding(15,0)]]"), std::string::npos);
        EXPECT_NE(flat.find("[[vk::binding(0,3)]]"), std::string::npos);
        EXPECT_NE(flat.find("[[vk::binding(1,3)]]"), std::string::npos);
    }
}
