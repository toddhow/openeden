// Tests for the sun glow maths, Render::Atmosphere::sunGlow (task 0040, stage c): the Henyey-Greenstein phase function,
// the haze weight (clear air has no glow) and that the shader twin (files/shaders/lib/view/fog.glsl) holds the same
// constants. Pure CPU maths: no device, window or game data.

#include <components/render/atmospheremath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

namespace
{
    using namespace Render::Atmosphere;

    TEST(RenderSunGlow, phaseIntegratesToOneOverTheSphere)
    {
        // The integral over all directions of the function is 1: 2 pi times the integral over cos theta.
        for (float g : { -0.5f, 0.f, 0.3f, 0.7f, 0.9f })
        {
            double sum = 0.0;
            const int steps = 200000;
            for (int i = 0; i < steps; ++i)
            {
                const float c = -1.f + 2.f * (static_cast<float>(i) + 0.5f) / static_cast<float>(steps);
                sum += henyeyGreenstein(c, g);
            }
            sum *= 2.0 * pi * (2.0 / steps);
            EXPECT_NEAR(sum, 1.0, 2e-3) << g;
        }
    }

    TEST(RenderSunGlow, zeroAnisotropyIsEvenInEveryDirection)
    {
        EXPECT_NEAR(henyeyGreenstein(1.f, 0.f), 1.f / (4.f * pi), 1e-6f);
        EXPECT_NEAR(henyeyGreenstein(-1.f, 0.f), 1.f / (4.f * pi), 1e-6f);
        EXPECT_NEAR(henyeyGreenstein(0.2f, 0.f), 1.f / (4.f * pi), 1e-6f);
    }

    TEST(RenderSunGlow, forwardLobeIsBrightestTowardTheSunAndFallsOff)
    {
        float last = henyeyGreenstein(1.f, sunGlowAnisotropy);
        for (float c = 0.95f; c >= -1.f; c -= 0.05f)
        {
            const float v = henyeyGreenstein(c, sunGlowAnisotropy);
            EXPECT_LE(v, last + 1e-6f) << c;
            last = v;
        }
    }

    TEST(RenderSunGlow, badInputsStayFinite)
    {
        EXPECT_TRUE(std::isfinite(henyeyGreenstein(5.f, 5.f)));
        EXPECT_TRUE(std::isfinite(henyeyGreenstein(-5.f, -5.f)));
        EXPECT_GT(henyeyGreenstein(1.f, 1.f), 0.f);
    }

    const vsg::vec3 white(1.f, 1.f, 1.f);
    const float elevationNoon = 1.0f;
    const float elevationLow = 0.05f;

    float luma(const vsg::vec3& c)
    {
        return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
    }

    TEST(RenderSunGlow, clearAirHasNoGlow)
    {
        const vsg::vec3 g = sunGlow(white, white, 1.f, elevationNoon, 1.f, 1.f);
        EXPECT_FLOAT_EQ(g.x, 0.f);
        EXPECT_FLOAT_EQ(g.y, 0.f);
        EXPECT_FLOAT_EQ(g.z, 0.f);
        EXPECT_FLOAT_EQ(hazeAmount(1.f), 0.f);
        EXPECT_FLOAT_EQ(hazeAmount(2.f), 0.f);
        EXPECT_FLOAT_EQ(hazeAmount(-1.f), 1.f);
    }

    TEST(RenderSunGlow, glowIsAroundTheSunAndNotElsewhere)
    {
        const float nearSun = sunGlow(white, white, 0.99f, elevationLow, 0.25f, 1.f).x;
        const float side = sunGlow(white, white, 0.5f, elevationLow, 0.25f, 1.f).x;
        const float away = sunGlow(white, white, -0.9f, elevationLow, 0.25f, 1.f).x;
        EXPECT_GT(nearSun, side * 3.f);
        EXPECT_GT(side, away);
        EXPECT_LT(away, 0.01f);
    }

    TEST(RenderSunGlow, glowFollowsTheFogsBrightnessAndTheStrength)
    {
        const vsg::vec3 dim(0.1f, 0.1f, 0.1f);
        const vsg::vec3 bright(0.5f, 0.5f, 0.5f);
        const vsg::vec3 a = sunGlow(white, dim, 0.95f, elevationLow, 0.f, 1.f);
        const vsg::vec3 b = sunGlow(white, bright, 0.95f, elevationLow, 0.f, 1.f);
        EXPECT_NEAR(b.y, 5.f * a.y, 1e-5f);
        EXPECT_NEAR(sunGlow(white, bright, 0.95f, elevationLow, 0.f, 2.f).y, 2.f * b.y, 1e-6f);
        EXPECT_FLOAT_EQ(sunGlow(white, bright, 0.95f, elevationLow, 0.f, 0.f).y, 0.f);
        EXPECT_FLOAT_EQ(sunGlow(white, bright, 0.95f, elevationLow, 0.f, -1.f).y, 0.f);
        // Black fog, no halo (the night's dark).
        EXPECT_FLOAT_EQ(sunGlow(white, vsg::vec3(0.f, 0.f, 0.f), 1.f, elevationLow, 0.f, 1.f).x, 0.f);
    }

    TEST(RenderSunGlow, hazierAirGlowsMore)
    {
        EXPECT_GT(sunGlow(white, white, 0.9f, elevationLow, 0.2f, 1.f).x,
            sunGlow(white, white, 0.9f, elevationLow, 0.8f, 1.f).x);
    }

    TEST(RenderSunGlow, aLowSunGlowsWarmAndAHighSunNearlyWhite)
    {
        const vsg::vec3 low = sunGlowTint(0.06f);
        EXPECT_FLOAT_EQ(std::max({ low.x, low.y, low.z }), 1.f);
        EXPECT_GT(low.x, low.y);
        EXPECT_GT(low.y, low.z);
        const vsg::vec3 high = sunGlowTint(1.2f);
        EXPECT_LT(high.x - high.z, low.x - low.z);
        EXPECT_GT(high.z, 0.6f);
        // Below the horizon the tint is the horizon's, finite and not black.
        const vsg::vec3 below = sunGlowTint(-0.5f);
        EXPECT_TRUE(std::isfinite(below.x) && std::isfinite(below.y) && std::isfinite(below.z));
        EXPECT_FLOAT_EQ(below.x, sunGlowTint(0.f).x);
    }

    TEST(RenderSunGlow, theTintIsTheModelsSunlightSoftenedTowardWhite)
    {
        // At full share it would be the model's transmittance scaled to its brightest channel; the tint is that, a
        // share of the way from white.
        const float e = 0.2f;
        const vsg::vec3 t = sunTransmittance(e, 6.f);
        const float m = std::max({ t.x, t.y, t.z });
        const vsg::vec3 tint = sunGlowTint(e);
        EXPECT_NEAR(tint.z, 1.f + (t.z / m - 1.f) * sunGlowTintShare, 1e-5f);
        EXPECT_NEAR(tint.y, 1.f + (t.y / m - 1.f) * sunGlowTintShare, 1e-5f);
    }

    TEST(RenderSunGlow, noGlowWhenTheLightIsTooDimToBeTheSun)
    {
        EXPECT_FLOAT_EQ(sunGlow(vsg::vec3(0.01f, 0.01f, 0.02f), white, 1.f, elevationLow, 0.f, 1.f).x, 0.f);
        EXPECT_GT(sunGlow(vsg::vec3(0.5f, 0.4f, 0.3f), white, 1.f, elevationLow, 0.f, 1.f).x, 0.f);
        // And it comes in smoothly: between the two edges the glow is between nothing and full.
        const float mid = sunGlow(vsg::vec3(0.1f, 0.1f, 0.1f), white, 1.f, elevationLow, 0.f, 1.f).x;
        const float full = sunGlow(vsg::vec3(1.f, 1.f, 1.f), white, 1.f, elevationLow, 0.f, 1.f).x;
        EXPECT_GT(mid, 0.f);
        EXPECT_LT(mid, full);
    }

    TEST(RenderSunGlow, thePeakIsBrighterThanTheFogAndNotASecondSun)
    {
        const vsg::vec3 g = sunGlow(white, white, 1.f, elevationNoon, 0.f, 1.f);
        EXPECT_GT(luma(g), 0.3f);
        EXPECT_LT(luma(g), 1.5f);
    }

    std::map<std::string, float> readShaderConstants()
    {
        std::map<std::string, float> out;
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / "lib" / "view" / "fog.glsl";
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

    TEST(RenderSunGlow, shaderTwinHoldsTheSameConstants)
    {
        const auto c = readShaderConstants();
        if (c.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const auto expect = [&c](const char* name, float value) {
            ASSERT_TRUE(c.count(name)) << name;
            EXPECT_FLOAT_EQ(c.at(name), value) << name;
        };
        expect("sunGlowAnisotropy", sunGlowAnisotropy);
        expect("sunGlowGain", sunGlowGain);
        expect("sunGlowTintShare", sunGlowTintShare);
        expect("sunGlowPresenceFrom", sunGlowPresenceFrom);
        expect("sunGlowPresenceTo", sunGlowPresenceTo);
        expect("sunGlowHazeTurbidity", Detail::hazeTurbidity);
        expect("sunGlowWavelengthR", wavelengths.x);
        expect("sunGlowWavelengthG", wavelengths.y);
        expect("sunGlowWavelengthB", wavelengths.z);
    }
}
