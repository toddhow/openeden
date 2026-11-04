// Tests for the aerial perspective maths, Render::Aerial (task 0066, stage c). Written from the spec before the
// implementation: the transmittance of a straight ray through air that thins with height, the weather's effect on it,
// and the marker that tells the shaders to use it. Pure CPU maths: no device, window or game data.

#include <components/render/aerialmath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace Render::Aerial;

    constexpr float visibility = 590000.f; // the distance at which a horizontal ray at the ground keeps 10 percent
    constexpr float scaleHeight = 4000.f;
    constexpr float gain = 12.f;

    Params params()
    {
        Params p;
        p.mVisibility = visibility;
        p.mScaleHeight = scaleHeight;
        p.mWeatherGain = gain;
        return p;
    }

    // The optical depth by Simpson's rule over the ray, with the ground clamp the closed form has: the air does not
    // thin below height 0.
    double numericalOpticalDepth(double beta, double H, double h0, double dz, double distance, int steps)
    {
        const double dt = distance / steps;
        auto density = [&](double t) { return std::exp(-std::max(h0 + t * dz, 0.0) / H); };
        double sum = density(0.0) + density(distance);
        for (int i = 1; i < steps; ++i)
            sum += density(i * dt) * (i % 2 == 0 ? 2.0 : 4.0);
        return beta * sum * dt / 3.0;
    }

    struct Ray
    {
        float mHeight;
        float mDirZ;
        float mDistance;
    };

    std::vector<Ray> rays()
    {
        std::vector<Ray> r;
        for (const float h : { 0.f, 50.f, 1500.f, 6000.f, 25000.f, 80000.f })
            for (const float dz : { -0.9f, -0.2f, -0.01f, 0.f, 0.001f, 0.05f, 0.5f, 1.f })
                for (const float d : { 100.f, 5000.f, 40000.f, 200000.f, 590000.f })
                    r.push_back({ h, dz, d });
        return r;
    }

    TEST(AerialMath, TransmittanceIsOneTenthAtTheVisibilityForAHorizontalRayAtTheGround)
    {
        const float beta = baseBeta(visibility);
        EXPECT_NEAR(transmittance(beta, scaleHeight, 0.f, 0.f, visibility), 0.1f, 1e-4f);
        EXPECT_NEAR(fogAmount(beta, scaleHeight, 0.f, 0.f, visibility), 0.9f, 1e-4f);
    }

    TEST(AerialMath, TransmittanceIsOneAtZeroDistance)
    {
        const float beta = baseBeta(visibility);
        for (const Ray& r : rays())
            EXPECT_FLOAT_EQ(transmittance(beta, scaleHeight, r.mHeight, r.mDirZ, 0.f), 1.f);
    }

    TEST(AerialMath, TransmittanceStaysInZeroToOneAndFallsWithDistance)
    {
        for (const float beta : { baseBeta(visibility), weatherBeta(params(), 1.0f), weatherBeta(params(), 3.0f) })
        {
            for (const float h : { 0.f, 1500.f, 25000.f })
                for (const float dz : { -0.9f, -0.01f, 0.f, 0.001f, 0.5f, 1.f })
                {
                    float previous = 1.f;
                    for (float d = 0.f; d <= 600000.f; d += 3000.f)
                    {
                        const float t = transmittance(beta, scaleHeight, h, dz, d);
                        ASSERT_TRUE(std::isfinite(t));
                        ASSERT_GE(t, 0.f);
                        ASSERT_LE(t, 1.f);
                        ASSERT_LE(t, previous + 1e-6f) << "h=" << h << " dz=" << dz << " d=" << d;
                        previous = t;
                    }
                }
        }
    }

    TEST(AerialMath, OpticalDepthMatchesNumericalIntegrationWithinOnePercent)
    {
        const float beta = baseBeta(visibility) * 7.f;
        for (const Ray& r : rays())
        {
            const double expected = numericalOpticalDepth(beta, scaleHeight, r.mHeight, r.mDirZ, r.mDistance, 200000);
            const double actual = opticalDepth(beta, scaleHeight, r.mHeight, r.mDirZ, r.mDistance);
            // Relative within 1 percent, with a floor for the depths that are close to zero.
            EXPECT_NEAR(actual, expected, std::max(0.01 * expected, 1e-6))
                << "h=" << r.mHeight << " dz=" << r.mDirZ << " d=" << r.mDistance;
        }
    }

    TEST(AerialMath, ARayThatDescendsToTheGroundKeepsAddingDepthAtTheGroundDensity)
    {
        // From 1000 units up, straight down: 1000 units of thinning air, then the rest at full density.
        const float beta = 1e-4f;
        const double expected = numericalOpticalDepth(beta, scaleHeight, 1000.0, -1.0, 5000.0, 400000);
        EXPECT_NEAR(opticalDepth(beta, scaleHeight, 1000.f, -1.f, 5000.f), expected, 0.01 * expected);
        // The same ray ends at the ground when the distance is 1000: no part of it lies below.
        EXPECT_LT(opticalDepth(beta, scaleHeight, 1000.f, -1.f, 1000.f), beta * 1000.f);
    }

    TEST(AerialMath, TheAirIsThinnerFromAltitude)
    {
        const float beta = baseBeta(visibility) * 7.f;
        const float ground = fogAmount(beta, scaleHeight, 0.f, -0.05f, 100000.f);
        const float high = fogAmount(beta, scaleHeight, 25000.f, -0.05f, 100000.f);
        EXPECT_GT(ground, high);
        // Looking along the ground from 25000 units up: a horizontal ray, density beta * exp(-h / H) all the way.
        const double expected
            = 1.0 - std::exp(-static_cast<double>(beta) * std::exp(-25000.0 / scaleHeight) * 100000.0);
        EXPECT_NEAR(fogAmount(beta, scaleHeight, 25000.f, 0.f, 100000.f), expected, 0.01 * expected);
    }

    TEST(AerialMath, TheClearWeathersShareTheBaseDensity)
    {
        const float base = baseBeta(visibility);
        // Clear, cloudy and overcast fog depths in Morrowind's data are 0.69 to 0.72 (Land_Fog_Day_Depth).
        EXPECT_FLOAT_EQ(weatherBeta(params(), 0.69f), base);
        EXPECT_FLOAT_EQ(weatherBeta(params(), 0.f), base);
        EXPECT_FLOAT_EQ(weatherBeta(params(), clearFogDepth), base);
    }

    TEST(AerialMath, WeatherRaisesTheDensityAndShortensTheView)
    {
        const Params p = params();
        float previous = weatherBeta(p, 0.f);
        for (float depth = 0.f; depth <= 3.5f; depth += 0.05f)
        {
            const float beta = weatherBeta(p, depth);
            ASSERT_GE(beta, previous);
            previous = beta;
        }
        // Storm and fog (depth 1.0) are denser than rain (0.8), which is denser than clear.
        EXPECT_GT(weatherBeta(p, 0.8f), weatherBeta(p, 0.69f));
        EXPECT_GT(weatherBeta(p, 1.0f), weatherBeta(p, 0.8f));
        // Where the view ends (a ground ray keeping 10 percent): a storm sees far less than clear weather, but more
        // than nothing.
        const float stormView = std::log(10.f) / weatherBeta(p, 1.0f);
        EXPECT_LT(stormView, 0.1f * visibility);
        EXPECT_GT(stormView, 1000.f);
        // Blizzards (depths 2.8 and 3.0) use the cap: equal, and finite.
        EXPECT_FLOAT_EQ(weatherBeta(p, 2.8f), weatherBeta(p, 3.0f));
        EXPECT_TRUE(std::isfinite(weatherBeta(p, 1e9f)));
        EXPECT_GT(weatherBeta(p, 3.0f), weatherBeta(p, 1.0f));
    }

    TEST(AerialMath, TheWeatherGainOfZeroLeavesAllWeatherAtTheBaseDensity)
    {
        Params p = params();
        p.mWeatherGain = 0.f;
        EXPECT_FLOAT_EQ(weatherBeta(p, 3.0f), baseBeta(visibility));
    }

    TEST(AerialMath, FogDepthIsRecoveredFromTheLinearFogRange)
    {
        // FogManager: start = view * (1 - depth), end = view.
        for (const float view : { 7168.f, 40000.f, 590000.f })
            for (const float depth : { 0.f, 0.69f, 1.0f, 1.9f, 3.0f })
                EXPECT_NEAR(fogDepthFromRange(view * (1.f - depth), view), depth, 1e-4f * std::max(1.f, depth));
        // No usable end: depth 0, not a crash.
        EXPECT_EQ(fogDepthFromRange(0.f, 0.f), 0.f);
    }

    TEST(AerialMath, WithoutAerialPerspectiveTheSceneFogRangeIsExactlyTheLinearOne)
    {
        // View::Scene::setFogRange stored start and 1 / max(0.001, end - start) before this stage: unchanged, bit for
        // bit, whatever the numbers.
        const float starts[] = { -3000.f, 0.f, 2100.f, 7000.f, 590000.f };
        const float ends[] = { 0.f, 7168.f, 7168.0005f, 40000.f, 590000.f, 3.4e38f };
        for (const float start : starts)
            for (const float end : ends)
            {
                const auto [s, scale] = sceneFogRange(start, end, false);
                EXPECT_EQ(s, start);
                EXPECT_EQ(scale, 1.f / std::max(0.001f, end - start));
                EXPECT_FALSE(isAerial(scale));
            }
    }

    TEST(AerialMath, WithAerialPerspectiveTheScaleIsNegativeAndKeepsItsSize)
    {
        for (const float end : { 7168.f, 40000.f, 590000.f })
        {
            const auto [s, scale] = sceneFogRange(0.3f * end, end, true);
            EXPECT_EQ(s, 0.3f * end);
            EXPECT_TRUE(isAerial(scale));
            EXPECT_EQ(-scale, 1.f / std::max(0.001f, end - 0.3f * end));
            // The fog depth can still be read back from the marked range.
            EXPECT_NEAR(fogDepthFromRange(s, s - 1.f / scale), 0.7f, 1e-3f);
        }
    }

    TEST(AerialMath, TheMarkIsOnlyANegativeScale)
    {
        EXPECT_FALSE(isAerial(0.f));
        EXPECT_FALSE(isAerial(1e-9f));
        EXPECT_TRUE(isAerial(-1e-9f));
    }

    // The shader twin: the constants of Render::Aerial that lib/view/fog.glsl repeats are read back from its text.
    TEST(AerialMath, TheShaderTwinHoldsTheSameConstants)
    {
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / "lib" / "view" / "fog.glsl";
        std::ifstream in(source);
        if (!in)
            GTEST_SKIP() << "the shader source is not beside the test sources";
        std::stringstream text;
        text << in.rdbuf();
        const std::string all = text.str();
        std::map<std::string, float> c;
        const std::regex line(R"(const\s+(?:float|int)\s+(\w+)\s*=\s*([-+0-9.eE]+)\s*;)");
        for (auto it = std::sregex_iterator(all.begin(), all.end(), line); it != std::sregex_iterator(); ++it)
            c[(*it)[1]] = std::stof((*it)[2]);
        ASSERT_TRUE(c.count("aerialClearFogDepth"));
        ASSERT_TRUE(c.count("aerialMaxWeatherExcess"));
        ASSERT_TRUE(c.count("aerialMaxCameraHeightScales"));
        EXPECT_FLOAT_EQ(c.at("aerialClearFogDepth"), clearFogDepth);
        EXPECT_FLOAT_EQ(c.at("aerialMaxWeatherExcess"), maxWeatherExcess);
        EXPECT_FLOAT_EQ(c.at("aerialMaxCameraHeightScales"), maxCameraHeightScales);
    }
}
