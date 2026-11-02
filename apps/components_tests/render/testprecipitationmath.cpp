// Tests for the precipitation light maths, Render::Precipitation (task 0040, stage b): the light on a streak (dark in
// the dark, bright near a lamp, brighter against the sun), the distance fade, the stretch, the snow colour, and that
// the shader twin (files/shaders/lib/light/precipitation.glsl) holds the same constants. Pure CPU maths.

#include <components/render/precipitationmath.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <string>

namespace
{
    using namespace Render::Precipitation;

    float luma(const vsg::vec3& c)
    {
        return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
    }

    TEST(RenderPrecipitation, darkInTheDarkAwayFromLamps)
    {
        const vsg::vec3 dark = lightColour({ 0.01f, 0.01f, 0.02f }, { 0.f, 0.f, 0.f }, 0.f, { 0.f, 0.f, 0.f });
        EXPECT_LT(luma(dark), 0.02f);
    }

    TEST(RenderPrecipitation, brightNearALamp)
    {
        const vsg::vec3 ambient(0.01f, 0.01f, 0.02f);
        const vsg::vec3 off = lightColour(ambient, { 0.f, 0.f, 0.f }, 0.f, { 0.f, 0.f, 0.f });
        const vsg::vec3 on = lightColour(ambient, { 0.f, 0.f, 0.f }, 0.f, { 1.f, 0.7f, 0.4f });
        EXPECT_GT(luma(on), luma(off) * 10.f);
        EXPECT_GT(on.x, on.z); // a warm lamp lights warm streaks
    }

    TEST(RenderPrecipitation, brighterWhenLookingTowardTheSun)
    {
        const vsg::vec3 ambient(0.1f, 0.1f, 0.1f);
        const vsg::vec3 sun(1.f, 0.8f, 0.5f);
        const float toward = luma(lightColour(ambient, sun, 0.95f, { 0.f, 0.f, 0.f }));
        const float across = luma(lightColour(ambient, sun, 0.f, { 0.f, 0.f, 0.f }));
        const float away = luma(lightColour(ambient, sun, -0.95f, { 0.f, 0.f, 0.f }));
        EXPECT_GT(toward, across);
        EXPECT_GT(across, away);
    }

    TEST(RenderPrecipitation, warmAtDuskFollowsTheSunColour)
    {
        const vsg::vec3 ambient(0.02f, 0.02f, 0.03f);
        const vsg::vec3 c = lightColour(ambient, { 1.2f, 0.5f, 0.2f }, 0.9f, { 0.f, 0.f, 0.f });
        EXPECT_GT(c.x, c.z * 2.f);
    }

    TEST(RenderPrecipitation, neverNegative)
    {
        // Morrowind has negative (darkness) lamps.
        const vsg::vec3 c = lightColour({ 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }, 0.f, { -3.f, -3.f, -3.f });
        EXPECT_GE(c.x, 0.f);
        EXPECT_GE(c.y, 0.f);
        EXPECT_GE(c.z, 0.f);
    }

    TEST(RenderPrecipitation, phaseIsOneOnAverageOverTheSphere)
    {
        double sum = 0.0;
        const int steps = 100000;
        for (int i = 0; i < steps; ++i)
            sum += sunPhase(-1.f + 2.f * (static_cast<float>(i) + 0.5f) / static_cast<float>(steps));
        EXPECT_NEAR(sum / steps, 1.0, 2e-3);
    }

    TEST(RenderPrecipitation, fadeIsOneNearZeroFarAndMonotonic)
    {
        EXPECT_FLOAT_EQ(distanceFade(0.f), 1.f);
        EXPECT_FLOAT_EQ(distanceFade(fadeStart), 1.f);
        EXPECT_FLOAT_EQ(distanceFade(fadeEnd), 0.f);
        EXPECT_FLOAT_EQ(distanceFade(fadeEnd * 3.f), 0.f);
        float last = 1.f;
        for (float d = 0.f; d < 500.f; d += 5.f)
        {
            const float f = distanceFade(d);
            EXPECT_LE(f, last + 1e-6f);
            EXPECT_GE(f, 0.f);
            last = f;
        }
        EXPECT_FLOAT_EQ(distanceFade(std::numeric_limits<float>::quiet_NaN()), 1.f);
    }

    TEST(RenderPrecipitation, rainBoxEdgeIsNotYetGone)
    {
        // The box is 300 units from the camera at its edge: drops there are still partly visible.
        EXPECT_GT(distanceFade(300.f), 0.2f);
    }

    TEST(RenderPrecipitation, stretchGrowsWithSpeedAndIsBounded)
    {
        EXPECT_FLOAT_EQ(streakStretch(0.f), 1.f);
        EXPECT_GT(streakStretch(400.f), 1.f);
        EXPECT_FLOAT_EQ(streakStretch(stretchSpeed), 1.f + maxStretch);
        EXPECT_FLOAT_EQ(streakStretch(1e9f), 1.f + maxStretch);
        EXPECT_FLOAT_EQ(streakStretch(-5.f), 1.f);
        EXPECT_FLOAT_EQ(streakStretch(std::numeric_limits<float>::quiet_NaN()), 1.f);
    }

    TEST(RenderPrecipitation, snowGetsNoLightFromNoSun)
    {
        // The ambient part is the material's; with no sun the snow's own glow is gone (the NIF's constant 0.3).
        const vsg::vec3 none = snowSunLight({ 0.f, 0.f, 0.f }, 0.5f);
        EXPECT_FLOAT_EQ(none.x, 0.f);
        EXPECT_FLOAT_EQ(none.y, 0.f);
        EXPECT_FLOAT_EQ(none.z, 0.f);
    }

    TEST(RenderPrecipitation, snowIsWarmAtDuskAndNeverNegative)
    {
        const vsg::vec3 dusk = snowSunLight({ 1.2f, 0.5f, 0.2f }, 0.9f);
        EXPECT_GT(dusk.x, dusk.z);
        const vsg::vec3 dark = snowSunLight({ -1.f, -1.f, -1.f }, 0.9f);
        EXPECT_GE(dark.x, 0.f);
    }

    TEST(RenderPrecipitation, snowPhaseIsFlatterThanRains)
    {
        const vsg::vec3 ambient(0.f, 0.f, 0.f);
        const vsg::vec3 sun(1.f, 1.f, 1.f);
        const float rainRatio = luma(lightColour(ambient, sun, 0.95f, { 0.f, 0.f, 0.f }))
            / luma(lightColour(ambient, sun, -0.95f, { 0.f, 0.f, 0.f }));
        const float snowRatio = luma(snowSunLight(sun, 0.95f)) / luma(snowSunLight(sun, -0.95f));
        EXPECT_GT(rainRatio, snowRatio);
        EXPECT_GT(snowRatio, 1.f);
    }

    std::map<std::string, float> readShaderConstants()
    {
        std::map<std::string, float> out;
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / "lib" / "light" / "precipitation.glsl";
        std::ifstream in(source);
        if (!in)
            return out;
        std::stringstream text;
        text << in.rdbuf();
        const std::string all = text.str();
        const std::regex line(R"(const\s+(?:float|int)\s+(\w+)\s*=\s*([-+0-9.eE]+)\s*;)");
        for (auto it = std::sregex_iterator(all.begin(), all.end(), line); it != std::sregex_iterator(); ++it)
            out[(*it)[1]] = std::stof((*it)[2]);
        return out;
    }

    TEST(RenderPrecipitation, shaderTwinHoldsTheSameConstants)
    {
        const auto c = readShaderConstants();
        if (c.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const auto expect = [&c](const char* name, float value) {
            ASSERT_TRUE(c.count(name)) << name;
            EXPECT_FLOAT_EQ(c.at(name), value) << name;
        };
        expect("precipAmbientShare", ambientShare);
        expect("precipPointShare", pointShare);
        expect("precipSunShare", sunShare);
        expect("precipSunAnisotropy", sunAnisotropy);
        expect("precipFadeStart", fadeStart);
        expect("precipFadeEnd", fadeEnd);
    }
}
