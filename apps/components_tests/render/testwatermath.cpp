// Tests for the water's far fade, Render::Water (task 0068). Written before the implementation: the share of a ripple
// octave kept when a pixel covers more texels of the normal map than one.

#include <components/render/watermath.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>

namespace
{
    using Render::Water::octaveFade;

    TEST(WaterOctaveFade, IsOneWhenAPixelCoversNoMoreThanOneTexel)
    {
        EXPECT_FLOAT_EQ(octaveFade(0.f), 1.f);
        EXPECT_FLOAT_EQ(octaveFade(0.25f), 1.f);
        EXPECT_FLOAT_EQ(octaveFade(1.f), 1.f);
    }

    TEST(WaterOctaveFade, IsOneOverTheTexelCountBeyondOne)
    {
        EXPECT_FLOAT_EQ(octaveFade(2.f), 0.5f);
        EXPECT_FLOAT_EQ(octaveFade(8.f), 0.125f);
        EXPECT_NEAR(octaveFade(80.f), 0.0125f, 1e-6f);
    }

    TEST(WaterOctaveFade, NeverIncreasesWithDistance)
    {
        float previous = octaveFade(0.f);
        for (float texels = 0.1f; texels < 5000.f; texels *= 1.07f)
        {
            const float fade = octaveFade(texels);
            EXPECT_LE(fade, previous + 1e-7f) << texels;
            previous = fade;
        }
    }

    TEST(WaterOctaveFade, StaysInZeroToOne)
    {
        for (float texels : { 0.f, 0.5f, 1.f, 1.0001f, 3.f, 100.f, 1e6f })
        {
            const float fade = octaveFade(texels);
            EXPECT_GT(fade, 0.f) << texels;
            EXPECT_LE(fade, 1.f) << texels;
        }
    }

    TEST(WaterOctaveFade, HandlesNegativeAndHugeInputsWithoutNaN)
    {
        EXPECT_FLOAT_EQ(octaveFade(-5.f), 1.f);
        EXPECT_FALSE(std::isnan(octaveFade(1e30f)));
        EXPECT_GT(octaveFade(1e30f), 0.f);
    }

    // The shader repeats the function; this reads its text back so that one cannot change without the other.
    TEST(WaterOctaveFade, TheShaderRepeatsTheSameRule)
    {
        std::ifstream file(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/water/main.frag");
        ASSERT_TRUE(file.good()) << "water/main.frag not found";
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string text = buffer.str();
        EXPECT_NE(text.find("float waterOctaveFade(float texelsPerPixel)"), std::string::npos);
        EXPECT_TRUE(std::regex_search(text, std::regex(R"(1\.0\s*/\s*max\(\s*1\.0\s*,\s*texelsPerPixel\s*\))")));
    }

    std::string readShaderText(const std::string& name)
    {
        std::ifstream file(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/" + name);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // The value of `name` in a line such as "const float name = 128.0;" (GLSL) or "static const float name = 128.0;"
    // (Slang), or an empty string.
    std::string constantValue(const std::string& text, const std::string& name)
    {
        std::smatch match;
        if (std::regex_search(text, match, std::regex("float " + name + R"(\s*=\s*([0-9.]+))")))
            return match[1].str();
        return {};
    }

    // Task 0072 (water family): the Slang twin repeats the rule and its constants exactly as the GLSL has them.
    TEST(WaterOctaveFade, TheSlangTwinRepeatsTheSameRuleAndConstants)
    {
        const std::string glsl = readShaderText("water/main.frag");
        const std::string slang = readShaderText("water/water.frag.slang");
        ASSERT_FALSE(slang.empty()) << "water/water.frag.slang not found";
        EXPECT_NE(slang.find("float waterOctaveFade(float texelsPerPixel)"), std::string::npos);
        EXPECT_TRUE(std::regex_search(slang, std::regex(R"(1\.0\s*/\s*max\(\s*1\.0\s*,\s*texelsPerPixel\s*\))")));
        for (const char* name : { "bumpMapSize", "waterFadeFromFar", "waveScale", "shininess" })
        {
            SCOPED_TRACE(name);
            EXPECT_FALSE(constantValue(glsl, name).empty());
            EXPECT_EQ(constantValue(slang, name), constantValue(glsl, name));
        }
    }
}
