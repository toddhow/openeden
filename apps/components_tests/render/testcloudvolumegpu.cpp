// Tests for the volumetric clouds' GPU-facing parts, Render::CloudVolume (task 0042): the std140 parameter block, the
// wind wrap, the dispatch helper, the image sizes, the debug view names and the twin of the constants and the block in
// the Slang and GLSL shaders. Written from the header's contract. Pure CPU: no device, window or game data.

#include <components/render/cloudvolumegpu.hpp>
#include <components/render/cloudvolumemath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace Render::CloudVolume;

    ParamsInput custom()
    {
        ParamsInput in;
        in.marchSize = { 480, 270 };
        in.fullSize = { 1920, 1080 };
        in.coverage = 0.6f;
        in.densityScale = 1.5f;
        in.windMeters = { 123.f, 456.f };
        in.steps = 40;
        in.lightSteps = 5;
        in.jitter = { 0.25f, 0.75f };
        in.historyWeight = 0.9f;
        in.erosion = 0.4f;
        in.sunGain = 1.2f;
        in.ambientGain = 0.8f;
        in.hazeLengthMeters = 30000.f;
        in.debug = DebugView::ShapeNoise;
        in.frame = 77;
        in.shadowShare = 0.5f;
        in.shadowStrength = 0.7f;
        for (std::size_t i = 0; i < 16; ++i)
            in.toPreviousClip[i] = static_cast<float>(i) + 0.5f;
        return in;
    }

    // --- The parameter block ---------------------------------------------------------------------------------------

    TEST(RenderCloudVolumeGpu, sizesOfTheBlock)
    {
        EXPECT_EQ(paramsVec4s, 10u);
        EXPECT_EQ(paramsFloats, 40u);
        EXPECT_EQ(buildParams(ParamsInput{}).size(), 40u);
    }

    TEST(RenderCloudVolumeGpu, everySlotForACustomInput)
    {
        const auto p = buildParams(custom());
        EXPECT_FLOAT_EQ(p[0], 480.f);
        EXPECT_FLOAT_EQ(p[1], 270.f);
        EXPECT_FLOAT_EQ(p[2], 1920.f);
        EXPECT_FLOAT_EQ(p[3], 1080.f);
        EXPECT_FLOAT_EQ(p[4], metersToUnits(layerBaseMeters));
        EXPECT_FLOAT_EQ(p[5], metersToUnits(layerTopMeters));
        EXPECT_FLOAT_EQ(p[6], metersToUnits(maxRayMeters));
        EXPECT_FLOAT_EQ(p[7], 0.6f);
        EXPECT_FLOAT_EQ(p[8], 1.5f);
        EXPECT_FLOAT_EQ(p[9], 123.f);
        EXPECT_FLOAT_EQ(p[10], 456.f);
        EXPECT_FLOAT_EQ(p[11], 40.f);
        EXPECT_FLOAT_EQ(p[12], 5.f);
        EXPECT_FLOAT_EQ(p[13], 0.25f);
        EXPECT_FLOAT_EQ(p[14], 0.75f);
        EXPECT_FLOAT_EQ(p[15], 0.9f);
        EXPECT_FLOAT_EQ(p[16], 0.4f);
        EXPECT_FLOAT_EQ(p[17], 1.2f);
        EXPECT_FLOAT_EQ(p[18], 0.8f);
        EXPECT_FLOAT_EQ(p[19], 30000.f);
        EXPECT_FLOAT_EQ(p[20], 2.f);
        EXPECT_FLOAT_EQ(p[21], 77.f);
        EXPECT_FLOAT_EQ(p[22], 0.5f);
        EXPECT_FLOAT_EQ(p[23], 0.7f);
        for (std::size_t i = 0; i < 16; ++i)
            EXPECT_FLOAT_EQ(p[24 + i], static_cast<float>(i) + 0.5f) << i;
    }

    TEST(RenderCloudVolumeGpu, theLayerIsInGameUnits)
    {
        const auto p = buildParams(ParamsInput{});
        EXPECT_NEAR(p[4], 1500.f * 69.99125109f, 0.1f);
        EXPECT_NEAR(p[5], 4000.f * 69.99125109f, 0.1f);
        EXPECT_NEAR(p[6], 120000.f * 69.99125109f, 1.f);
    }

    TEST(RenderCloudVolumeGpu, coverageAndShadowValuesAreClampedToTheUnitInterval)
    {
        ParamsInput in;
        in.coverage = 3.f;
        in.shadowShare = 2.f;
        in.shadowStrength = 9.f;
        in.historyWeight = 1.5f;
        in.erosion = 4.f;
        auto p = buildParams(in);
        EXPECT_FLOAT_EQ(p[7], 1.f);
        EXPECT_FLOAT_EQ(p[22], 1.f);
        EXPECT_FLOAT_EQ(p[23], 1.f);
        EXPECT_FLOAT_EQ(p[15], 1.f);
        EXPECT_FLOAT_EQ(p[16], 1.f);
        in.coverage = -1.f;
        in.shadowShare = -2.f;
        in.shadowStrength = -3.f;
        in.historyWeight = -0.5f;
        in.erosion = -4.f;
        p = buildParams(in);
        EXPECT_FLOAT_EQ(p[7], 0.f);
        EXPECT_FLOAT_EQ(p[22], 0.f);
        EXPECT_FLOAT_EQ(p[23], 0.f);
        EXPECT_FLOAT_EQ(p[15], 0.f);
        EXPECT_FLOAT_EQ(p[16], 0.f);
    }

    TEST(RenderCloudVolumeGpu, jitterIsClampedToTheUnitInterval)
    {
        ParamsInput in;
        in.jitter = { -0.5f, 1.5f };
        const auto p = buildParams(in);
        EXPECT_FLOAT_EQ(p[13], 0.f);
        EXPECT_FLOAT_EQ(p[14], 1.f);
    }

    TEST(RenderCloudVolumeGpu, gainsAndScalesAreNeverNegative)
    {
        ParamsInput in;
        in.densityScale = -1.f;
        in.sunGain = -2.f;
        in.ambientGain = -3.f;
        auto p = buildParams(in);
        EXPECT_FLOAT_EQ(p[8], 0.f);
        EXPECT_FLOAT_EQ(p[17], 0.f);
        EXPECT_FLOAT_EQ(p[18], 0.f);
        in.densityScale = 4.f;
        in.sunGain = 2.5f;
        in.ambientGain = 3.5f;
        p = buildParams(in);
        EXPECT_FLOAT_EQ(p[8], 4.f);
        EXPECT_FLOAT_EQ(p[17], 2.5f);
        EXPECT_FLOAT_EQ(p[18], 3.5f);
    }

    TEST(RenderCloudVolumeGpu, theHazeLengthIsAtLeastOneMeter)
    {
        ParamsInput in;
        in.hazeLengthMeters = 0.f;
        EXPECT_GE(buildParams(in)[19], 1.f);
        in.hazeLengthMeters = -50.f;
        EXPECT_GE(buildParams(in)[19], 1.f);
    }

    TEST(RenderCloudVolumeGpu, stepsAreNeverZero)
    {
        ParamsInput in;
        in.steps = 0;
        EXPECT_GE(buildParams(in)[11], 1.f);
        in.steps = 64;
        EXPECT_FLOAT_EQ(buildParams(in)[11], 64.f);
    }

    TEST(RenderCloudVolumeGpu, theFrameCounterWrapsAtSixtyFiveThousand)
    {
        ParamsInput in;
        in.frame = 65535;
        EXPECT_FLOAT_EQ(buildParams(in)[21], 65535.f);
        in.frame = 65536;
        EXPECT_FLOAT_EQ(buildParams(in)[21], 0.f);
        in.frame = 65536 + 12;
        EXPECT_FLOAT_EQ(buildParams(in)[21], 12.f);
        in.frame = 0xFFFFFFFFu;
        EXPECT_FLOAT_EQ(buildParams(in)[21], 65535.f);
    }

    TEST(RenderCloudVolumeGpu, theDebugViewIsItsNumber)
    {
        const std::pair<DebugView, float> views[] = { { DebugView::None, 0.f }, { DebugView::Transmittance, 1.f },
            { DebugView::ShapeNoise, 2.f }, { DebugView::DetailNoise, 3.f }, { DebugView::CurlNoise, 4.f } };
        for (const auto& [view, number] : views)
        {
            ParamsInput in;
            in.debug = view;
            EXPECT_FLOAT_EQ(buildParams(in)[20], number);
        }
    }

    TEST(RenderCloudVolumeGpu, theMatrixIsCopiedInOrder)
    {
        ParamsInput in;
        for (std::size_t i = 0; i < 16; ++i)
            in.toPreviousClip[i] = static_cast<float>(100 + i);
        const auto p = buildParams(in);
        for (std::size_t i = 0; i < 16; ++i)
            EXPECT_FLOAT_EQ(p[24 + i], static_cast<float>(100 + i));
    }

    TEST(RenderCloudVolumeGpu, buildParamsIsDeterministic)
    {
        EXPECT_EQ(buildParams(custom()), buildParams(custom()));
    }

    // --- Wind ------------------------------------------------------------------------------------------------------

    TEST(RenderCloudVolumeGpu, wrapWindStaysInsideTheShapeTile)
    {
        for (double v : { 0.0, 1.0, 11999.0, 12000.0, 12001.5, 36000.0, 1e6, -1.0, -12000.0, -50000.5 })
        {
            const float w = wrapWind(v);
            EXPECT_GE(w, 0.f) << v;
            EXPECT_LT(w, shapeTileMeters) << v;
        }
    }

    TEST(RenderCloudVolumeGpu, wrapWindKeepsSmallValuesAndWrapsNegativeOnes)
    {
        EXPECT_FLOAT_EQ(wrapWind(0.0), 0.f);
        EXPECT_FLOAT_EQ(wrapWind(250.5), 250.5f);
        EXPECT_FLOAT_EQ(wrapWind(shapeTileMeters + 100.0), 100.f);
        EXPECT_FLOAT_EQ(wrapWind(-100.0), shapeTileMeters - 100.f);
        EXPECT_FLOAT_EQ(wrapWind(-static_cast<double>(shapeTileMeters) - 100.0), shapeTileMeters - 100.f);
    }

    TEST(RenderCloudVolumeGpu, wrapWindKeepsPrecisionForLargeDoubles)
    {
        // A float could not hold 1e9 + 0.5; the double wrap does, and only then is the result narrowed.
        const double v = 1e9 + 0.5;
        EXPECT_EQ(wrapWind(v), static_cast<float>(std::fmod(v, static_cast<double>(shapeTileMeters))));
        const double big = 123456789012.25;
        EXPECT_EQ(wrapWind(big), static_cast<float>(std::fmod(big, static_cast<double>(shapeTileMeters))));
    }

    // --- Dispatch and sizes ----------------------------------------------------------------------------------------

    TEST(RenderCloudVolumeGpu, groupSizesAreEightAndFour)
    {
        EXPECT_EQ(groupSizeXY, 8u);
        EXPECT_EQ(noiseGroupSize3D, 4u);
    }

    TEST(RenderCloudVolumeGpu, dispatchGroupsRoundsUp)
    {
        EXPECT_EQ(dispatchGroups(0, 8), 0u);
        EXPECT_EQ(dispatchGroups(1, 8), 1u);
        EXPECT_EQ(dispatchGroups(8, 8), 1u);
        EXPECT_EQ(dispatchGroups(9, 8), 2u);
        EXPECT_EQ(dispatchGroups(480, 8), 60u);
        EXPECT_EQ(dispatchGroups(270, 8), 34u);
        EXPECT_EQ(dispatchGroups(128, 4), 32u);
        EXPECT_EQ(dispatchGroups(32, 4), 8u);
    }

    TEST(RenderCloudVolumeGpu, imageBytesForAFullHdScreenOnMedium)
    {
        // March image 480 x 270 at 8 bytes: 1,036,800. The resolved picture and the history, 1920 x 1080 each at
        // 8 bytes: 33,177,600. The noise at 4 bytes a texel: (128^3 + 32^3 + 128^2) = 2,146,304 texels = 8,585,216.
        EXPECT_EQ(imageBytes({ 480, 270 }, { 1920, 1080 }), 1036800u + 33177600u + 8585216u);
        EXPECT_EQ(imageBytes({ 480, 270 }, { 1920, 1080 }), 42799616u);
    }

    TEST(RenderCloudVolumeGpu, imageBytesForTheSmallestScreenIsTheNoisePlusThreeTexels)
    {
        // 1 x 1 march (8) + two 1 x 1 full images (16) + the noise (8,585,216).
        EXPECT_EQ(imageBytes({ 1, 1 }, { 1, 1 }), 8585240u);
    }

    // --- Debug view names ------------------------------------------------------------------------------------------

    TEST(RenderCloudVolumeGpu, parseDebugViewReadsTheNames)
    {
        EXPECT_EQ(parseDebugView(""), DebugView::None);
        EXPECT_EQ(parseDebugView("none"), DebugView::None);
        EXPECT_EQ(parseDebugView("transmittance"), DebugView::Transmittance);
        EXPECT_EQ(parseDebugView("shape"), DebugView::ShapeNoise);
        EXPECT_EQ(parseDebugView("detail"), DebugView::DetailNoise);
        EXPECT_EQ(parseDebugView("curl"), DebugView::CurlNoise);
    }

    TEST(RenderCloudVolumeGpu, parseDebugViewTakesGarbageAsNone)
    {
        EXPECT_EQ(parseDebugView("garbage"), DebugView::None);
        EXPECT_EQ(parseDebugView("shapes"), DebugView::None);
        EXPECT_EQ(parseDebugView("  "), DebugView::None);
        EXPECT_EQ(parseDebugView("transmittance "), DebugView::None);
    }

    TEST(RenderCloudVolumeGpu, otherConstantsHaveTheirValues)
    {
        EXPECT_FLOAT_EQ(historyWeight, 0.9f);
        EXPECT_FLOAT_EQ(maxRayMeters, 120000.f);
        EXPECT_EQ(shadowMapSize, 256u);
        EXPECT_FLOAT_EQ(shadowHalfExtentMeters, 20000.f);
        EXPECT_EQ(shadowImageWidth, shadowMapSize + 1);
    }

    // --- The shader twin -------------------------------------------------------------------------------------------

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

    // The value of `static const <type> <name> = <value>;` in a Slang file.
    std::optional<double> slangNumber(const std::string& text, const std::string& name)
    {
        const std::regex re(
            "static\\s+const\\s+\\w+\\s+" + name + "\\s*=\\s*(0[xX][0-9A-Fa-f]+|[-+0-9.eE]+)[uUfF]?\\s*;");
        std::smatch m;
        if (!std::regex_search(text, m, re))
            return std::nullopt;
        const std::string v = m[1].str();
        if (v.size() > 2 && (v[1] == 'x' || v[1] == 'X'))
            return static_cast<double>(std::stoull(v, nullptr, 16));
        return std::stod(v);
    }

    void expectSlangNumber(const std::string& text, const std::string& name, double expected)
    {
        const auto value = slangNumber(text, name);
        ASSERT_TRUE(value.has_value()) << name << " is not declared in cloud.slang";
        EXPECT_NEAR(*value, expected, std::max(1e-6, std::fabs(expected) * 1e-6)) << name;
    }

    TEST(RenderCloudVolumeGpu, slangConstantsMatchTheCppConstants)
    {
        const std::string slang = readShaderFile({ "comp", "cloud", "cloud.slang" });
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        expectSlangNumber(slang, "unitsPerMeter", metersToUnits(1.f));
        expectSlangNumber(slang, "shapeTileMeters", shapeTileMeters);
        expectSlangNumber(slang, "detailTileMeters", detailTileMeters);
        expectSlangNumber(slang, "detailFineTileMeters", detailFineTileMeters);
        expectSlangNumber(slang, "detailFineWeight", detailFineWeight);
        expectSlangNumber(slang, "detailContrast", detailContrast);
        expectSlangNumber(slang, "sunPhaseScale", sunPhaseScale);
        expectSlangNumber(slang, "shapeSize", shapeSize);
        expectSlangNumber(slang, "detailSize", detailSize);
        expectSlangNumber(slang, "curlSize", curlSize);
        expectSlangNumber(slang, "noiseSeed", noiseSeed);
        expectSlangNumber(slang, "shapePerlinPeriod", shapePerlinPeriod);
        expectSlangNumber(slang, "curlPeriod", curlPeriod);
        expectSlangNumber(slang, "forwardAnisotropy", forwardAnisotropy);
        expectSlangNumber(slang, "backAnisotropy", backAnisotropy);
        expectSlangNumber(slang, "forwardWeight", forwardWeight);
        expectSlangNumber(slang, "multiScatterExtinction", multiScatterExtinction);
        expectSlangNumber(slang, "multiScatterEnergy", multiScatterEnergy);
        expectSlangNumber(slang, "multiScatterPhase", multiScatterPhase);
        expectSlangNumber(slang, "extinctionPerMeter", extinctionPerMeter);
        expectSlangNumber(slang, "baseAmbientShare", baseAmbientShare);
        expectSlangNumber(slang, "lightRayMeters", lightRayMeters);
        expectSlangNumber(slang, "minTransmittance", minTransmittance);
        expectSlangNumber(slang, "shadowMapSize", shadowMapSize);
        expectSlangNumber(slang, "shadowHalfExtentMeters", shadowHalfExtentMeters);
    }

    TEST(RenderCloudVolumeGpu, slangNoiseSeedIsWrittenInHex)
    {
        const std::string slang = readShaderFile({ "comp", "cloud", "cloud.slang" });
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        EXPECT_TRUE(std::regex_search(slang, std::regex("noiseSeed\\s*=\\s*0x0E0E1042u\\s*;")));
        EXPECT_EQ(noiseSeed, 0x0E0E1042u);
    }

    TEST(RenderCloudVolumeGpu, slangStructHasTheMembersInOrder)
    {
        const std::string slang = readShaderFile({ "comp", "cloud", "cloud.slang" });
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const std::size_t start = slang.find("struct CloudParams");
        ASSERT_NE(start, std::string::npos);
        const std::size_t end = slang.find('}', start);
        ASSERT_NE(end, std::string::npos);
        const std::string body = slang.substr(start, end - start);
        const std::vector<std::pair<std::string, std::string>> members{ { "float4", "sizes" }, { "float4", "layer" },
            { "float4", "density" }, { "float4", "march" }, { "float4", "look" }, { "float4", "debug" },
            { "float4x4", "toPreviousClip" } };
        std::size_t from = 0;
        for (const auto& [type, name] : members)
        {
            const std::regex re("\\b" + type + "\\s+" + name + "\\s*;");
            std::smatch m;
            const std::string rest = body.substr(from);
            ASSERT_TRUE(std::regex_search(rest, m, re)) << name << " is missing or out of order";
            from += static_cast<std::size_t>(m.position(0) + m.length(0));
        }
    }

    TEST(RenderCloudVolumeGpu, slangBindingsOfTheComputeSet)
    {
        const std::string slang = readShaderFile({ "comp", "cloud", "cloud.slang" });
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        std::map<int, std::string> found;
        const std::regex re("\\[\\[vk::binding\\((\\d+),\\s*3\\)\\]\\]([^;]*);");
        for (auto it = std::sregex_iterator(slang.begin(), slang.end(), re); it != std::sregex_iterator(); ++it)
        {
            const std::string decl = (*it)[2].str();
            const std::size_t end = decl.find_last_not_of(" \t\r\n");
            const std::size_t begin = decl.find_last_of(" \t\r\n>", end) + 1;
            found[std::stoi((*it)[1].str())] = decl.substr(begin, end - begin + 1);
        }
        const std::map<int, std::string> expected{ { 0, "shapeStore" }, { 1, "detailStore" }, { 2, "curlStore" },
            { 3, "shapeTex" }, { 4, "detailTex" }, { 5, "curlTex" }, { 6, "marchStore" }, { 7, "resolveStore" },
            { 8, "historyTex" }, { 9, "historyStore" }, { 10, "cloud" }, { 11, "shadowStore" } };
        EXPECT_EQ(found, expected);
    }

    TEST(RenderCloudVolumeGpu, slangHasTheSevenEntryPoints)
    {
        const std::string slang = readShaderFile({ "comp", "cloud", "cloud.slang" });
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        for (const char* name : { "noiseShapeMain", "noiseDetailMain", "noiseCurlMain", "marchMain", "resolveMain",
                 "copyMain", "shadowMain" })
            EXPECT_TRUE(std::regex_search(slang, std::regex(std::string("\\b") + name + "\\s*\\("))) << name;
    }

    TEST(RenderCloudVolumeGpu, theShadowReaderHasTheSameSizeAndABinding)
    {
        const std::string reader = readShaderFile({ "lib", "view", "volumecloudshadow.glsl" });
        const std::string bindings = readShaderFile({ "lib", "view", "bindings.glsl" });
        if (reader.empty() || bindings.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        std::smatch m;
        ASSERT_TRUE(
            std::regex_search(reader, m, std::regex("const\\s+int\\s+VOLUME_CLOUD_SHADOW_SIZE\\s*=\\s*(\\d+)\\s*;")));
        EXPECT_EQ(static_cast<std::uint32_t>(std::stoul(m[1].str())), shadowMapSize);
        EXPECT_TRUE(std::regex_search(reader, std::regex("binding\\s*=\\s*VIEW_CLOUD_SHADOW_BINDING")));
        ASSERT_TRUE(std::regex_search(bindings, m, std::regex("VIEW_CLOUD_SHADOW_BINDING\\s*=\\s*(\\d+)\\s*;")));
        EXPECT_EQ(std::stoi(m[1].str()), 16);
    }
}
