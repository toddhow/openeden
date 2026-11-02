// Tests for the procedural sun's maths (task 0047, stage a) and for its Slang twin (stage b).
//
// Written from the spec alone, before the implementation: limb darkening, a soft edge, a two-part glow, the HDR
// intensity and its cap, the settings gate, and that the shader repeats the C++ constants and keeps the interface
// the C++ side builds the pipeline layout for. No GPU, window or game data is needed.

#include <components/render/sunmath.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace Render::Sun;

    TEST(RenderProceduralSunLimb, CentreIsOneAndEdgeIsDarker)
    {
        EXPECT_FLOAT_EQ(limbDarkening(0.f, 0.6f), 1.f);
        EXPECT_NEAR(limbDarkening(1.f, 0.6f), 0.4f, 1e-6f);
    }

    TEST(RenderProceduralSunLimb, FlatWhenCoefficientIsZero)
    {
        for (float r = 0.f; r <= 1.f; r += 0.1f)
            EXPECT_FLOAT_EQ(limbDarkening(r, 0.f), 1.f);
    }

    TEST(RenderProceduralSunLimb, DecreasesMonotonicallyAndIsZeroOutside)
    {
        float previous = limbDarkening(0.f, 0.6f);
        for (float r = 0.01f; r <= 1.f; r += 0.01f)
        {
            const float now = limbDarkening(r, 0.6f);
            EXPECT_LE(now, previous + 1e-6f) << r;
            previous = now;
        }
        EXPECT_EQ(limbDarkening(1.01f, 0.6f), 0.f);
        EXPECT_EQ(limbDarkening(50.f, 0.6f), 0.f);
    }

    TEST(RenderProceduralSunLimb, BadInputGivesZeroNotNaN)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(limbDarkening(nan, 0.6f), 0.f);
        EXPECT_FALSE(std::isnan(limbDarkening(0.5f, nan)));
        EXPECT_GE(limbDarkening(-0.5f, 0.6f), 0.f);
    }

    TEST(RenderProceduralSunEdge, OneInsideZeroOutsideAndMonotone)
    {
        EXPECT_FLOAT_EQ(edgeCoverage(0.f, 0.2f, 0.02f), 1.f);
        EXPECT_FLOAT_EQ(edgeCoverage(0.1f, 0.2f, 0.02f), 1.f);
        EXPECT_FLOAT_EQ(edgeCoverage(0.3f, 0.2f, 0.02f), 0.f);
        EXPECT_NEAR(edgeCoverage(0.2f, 0.2f, 0.02f), 0.5f, 1e-5f);
        float previous = 1.f;
        for (float r = 0.f; r <= 0.4f; r += 0.001f)
        {
            const float now = edgeCoverage(r, 0.2f, 0.02f);
            EXPECT_LE(now, previous + 1e-6f) << r;
            EXPECT_GE(now, 0.f);
            EXPECT_LE(now, 1.f);
            previous = now;
        }
    }

    TEST(RenderProceduralSunEdge, ZeroSoftnessIsAHardEdgeNotNaN)
    {
        EXPECT_EQ(edgeCoverage(0.1f, 0.2f, 0.f), 1.f);
        EXPECT_EQ(edgeCoverage(0.3f, 0.2f, 0.f), 0.f);
    }

    TEST(RenderProceduralSunGlow, NonNegativeFiniteAndFallsOff)
    {
        const GlowParams p;
        float previous = std::numeric_limits<float>::max();
        for (float r = 0.f; r <= 1.f; r += 0.005f)
        {
            const float g = glowFalloff(r, 0.18f, p);
            EXPECT_TRUE(std::isfinite(g)) << r;
            EXPECT_GE(g, 0.f) << r;
            EXPECT_LE(g, previous + 1e-6f) << r;
            previous = g;
        }
    }

    TEST(RenderProceduralSunGlow, ZeroScaleGivesNoGlow)
    {
        GlowParams p;
        p.scale = 0.f;
        for (float r = 0.f; r <= 1.f; r += 0.05f)
            EXPECT_EQ(glowFalloff(r, 0.18f, p), 0.f);
    }

    TEST(RenderProceduralSunGlow, ScalesLinearlyWithTheSetting)
    {
        GlowParams one, two;
        two.scale = 2.f;
        EXPECT_NEAR(glowFalloff(0.3f, 0.18f, two), 2.f * glowFalloff(0.3f, 0.18f, one), 1e-6f);
    }

    TEST(RenderProceduralSunGlow, ReachesZeroAtTheQuadEdgeSoThereIsNoVisibleSquare)
    {
        EXPECT_EQ(glowFalloff(1.f, 0.18f, GlowParams{}), 0.f);
        EXPECT_EQ(glowFalloff(1.5f, 0.18f, GlowParams{}), 0.f);
    }

    TEST(RenderProceduralSunGlow, IntegratesToAFiniteAmountAndIsContinuousAtTheDiscEdge)
    {
        const GlowParams p;
        double sum = 0.0;
        for (int i = 0; i < 10000; ++i)
        {
            const float r = (static_cast<float>(i) + 0.5f) / 10000.f;
            sum += static_cast<double>(glowFalloff(r, 0.18f, p)) * r * 6.2831853 / 10000.0;
        }
        EXPECT_TRUE(std::isfinite(sum));
        EXPECT_GT(sum, 0.0);
        EXPECT_LT(sum, 5.0);
        EXPECT_NEAR(glowFalloff(0.18f - 1e-4f, 0.18f, p), glowFalloff(0.18f + 1e-4f, 0.18f, p), 1e-3f);
    }

    TEST(RenderProceduralSunGlow, DiscIsBrighterThanItsOwnGlow)
    {
        const float centre = pixelBrightness(0.f, 0.18f, 0.01f, GlowParams{});
        const float outside = pixelBrightness(0.5f, 0.18f, 0.01f, GlowParams{});
        EXPECT_GT(centre, 0.9f);
        EXPECT_LT(outside, 0.2f);
        EXPECT_GT(outside, 0.f);
    }

    TEST(RenderProceduralSunPixel, AlphaNeverAboveOneAndColourCarriesTheIntensity)
    {
        EXPECT_EQ(pixelAlpha(5.f), 1.f);
        EXPECT_EQ(pixelAlpha(0.25f), 0.25f);
        EXPECT_EQ(pixelAlpha(-1.f), 0.f);
        // Blending gives colour * scale * alpha, which must be colour * intensity * brightness for brightness <= 1.
        for (const float b : { 0.01f, 0.25f, 0.9f })
            EXPECT_NEAR(colourScale(30.f, b) * pixelAlpha(b), 30.f * b, 1e-4f);
        // Above 1 the colour is the intensity times the brightness (alpha is 1).
        EXPECT_NEAR(colourScale(30.f, 1.2f), 36.f, 1e-4f);
        EXPECT_EQ(colourScale(30.f, 0.f), 0.f);
    }

    TEST(RenderProceduralSunIntensity, IsCappedAndNeverNaN)
    {
        EXPECT_EQ(clampIntensity(30.f), 30.f);
        EXPECT_EQ(clampIntensity(1e9f), maxIntensity);
        EXPECT_EQ(clampIntensity(-4.f), 1.f);
        EXPECT_FLOAT_EQ(clampIntensity(std::numeric_limits<float>::quiet_NaN()), defaultIntensity);
        // The HDR target is RGBA16F (about 65000 at most): the disc stays far below.
        EXPECT_LT(maxIntensity * 1.5f, 60000.f);
    }

    TEST(RenderProceduralSunGate, NeedsTheSettingAndHdr)
    {
        EXPECT_TRUE(useProceduralSun(true, true));
        EXPECT_FALSE(useProceduralSun(true, false));
        EXPECT_FALSE(useProceduralSun(false, true));
        EXPECT_FALSE(useProceduralSun(false, false));
    }

    TEST(RenderProceduralSunSize, RadiusStaysInsideTheQuad)
    {
        EXPECT_NEAR(discRadius(1.f), discRadiusAtSizeOne, 1e-6f);
        EXPECT_LE(discRadius(1e6f), maxDiscRadius);
        EXPECT_GT(discRadius(0.f), 0.f);
        EXPECT_FALSE(std::isnan(discRadius(std::numeric_limits<float>::quiet_NaN())));
    }

    // The shader repeats the constants of sunmath.hpp.
    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    float shaderConstant(const std::string& text, const std::string& name)
    {
        const std::regex re("static const float " + name + R"(\s*=\s*([-0-9.eE+]+)\s*;)");
        std::smatch m;
        if (!std::regex_search(text, m, re))
            return std::numeric_limits<float>::quiet_NaN();
        return std::stof(m[1].str());
    }

    TEST(RenderProceduralSunShader, RepeatsTheConstantsOfTheCppTwin)
    {
        const auto path = std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / "sky/sun/proceduralsun.slang";
        const std::string text = readFile(path);
        ASSERT_FALSE(text.empty()) << path;
        const GlowParams p;
        EXPECT_FLOAT_EQ(shaderConstant(text, "kLimbCoefficient"), limbCoefficient);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kTightAmount"), p.tightAmount);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kTightWidth"), p.tightWidth);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kWideAmount"), p.wideAmount);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kWideWidth"), p.wideWidth);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kGlowGain"), glowGain);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kWindowStart"), glowWindowStart);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kEdgeSoftness"), edgeSoftness);
    }

    // A small SPIR-V reader: the descriptor (set, binding) pairs, the entry point name and push constants.
    struct SpirvInfo
    {
        std::set<std::pair<uint32_t, uint32_t>> bindings;
        std::string entry;
        bool hasPushConstant = false;
        uint32_t version = 0;
        bool valid = false;
    };

    std::string literalString(const std::vector<uint32_t>& w, std::size_t from, std::size_t to)
    {
        std::string name;
        for (std::size_t k = from; k < to; ++k)
            for (int b = 0; b < 4; ++b)
            {
                const char c = static_cast<char>((w[k] >> (8 * b)) & 0xFF);
                if (c == 0)
                    return name;
                name += c;
            }
        return name;
    }

    SpirvInfo readSpirv(const std::filesystem::path& path)
    {
        SpirvInfo info;
        std::ifstream in(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() < 20 || bytes.size() % 4 != 0)
            return info;
        std::vector<uint32_t> w(bytes.size() / 4);
        std::memcpy(w.data(), bytes.data(), bytes.size());
        if (w[0] != 0x07230203)
            return info;
        info.version = w[1];
        std::map<uint32_t, uint32_t> sets, binds;
        for (std::size_t i = 5; i < w.size();)
        {
            const uint32_t op = w[i] & 0xFFFF, count = w[i] >> 16;
            if (count == 0 || i + count > w.size())
                return info;
            if (op == 15 && count >= 4) // OpEntryPoint: model, id, name...
                info.entry = literalString(w, i + 3, i + count);
            if (op == 71 && count >= 4) // OpDecorate: target, decoration, value
            {
                if (w[i + 2] == 34)
                    sets[w[i + 1]] = w[i + 3];
                if (w[i + 2] == 33)
                    binds[w[i + 1]] = w[i + 3];
            }
            if (op == 59 && count >= 4 && w[i + 3] == 9) // OpVariable in the PushConstant storage class
                info.hasPushConstant = true;
            i += count;
        }
        for (const auto& [id, set] : sets)
            if (binds.count(id))
                info.bindings.insert({ set, binds[id] });
        info.valid = true;
        return info;
    }

    class ProceduralSunSpirv : public ::testing::TestWithParam<std::string>
    {
    };

    TEST_P(ProceduralSunSpirv, UsesOnlyTheSunsDescriptorsAndTheStandardEntryPoint)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc";
#else
        const auto path
            = std::filesystem::path(OPENEDEN_SHADER_BUILD_DIR) / "sky/sunproc" / ("main." + GetParam() + ".spv");
        const SpirvInfo info = readSpirv(path);
        ASSERT_TRUE(info.valid) << path;
        EXPECT_EQ(info.version, 0x00010000u);
        EXPECT_EQ(info.entry, "main");
        // The sun's layout: scene (set 0, binding 0), material (set 2, binding 1); the picture (set 2, binding 0) is
        // not read by this shader but is allowed to be bound.
        const std::set<std::pair<uint32_t, uint32_t>> allowed{ { 0, 0 }, { 2, 0 }, { 2, 1 } };
        for (const auto& b : info.bindings)
            EXPECT_TRUE(allowed.count(b)) << "set " << b.first << " binding " << b.second;
        if (GetParam() == "vert")
            EXPECT_TRUE(info.hasPushConstant);
        else
        {
            EXPECT_TRUE(info.bindings.count({ 2, 1 })) << "the fragment stage must read the material";
            EXPECT_TRUE(info.bindings.count({ 0, 0 })) << "the fragment stage must read the scene (linear view)";
        }
#endif
    }

    INSTANTIATE_TEST_SUITE_P(Stages, ProceduralSunSpirv, ::testing::Values("vert", "frag"));
}
