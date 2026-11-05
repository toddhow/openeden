// Tests for the curved cloud layer, the far fade and the large scale variety of the volumetric clouds (task 0042,
// follow-up of 2026-11-05): the pure maths in components/render/cloudcurvemath.hpp, the extra parameter vector and the
// wind wrap of cloudvolumegpu.hpp, and the twin of the constants in cloud.slang. Written from the contract in the
// task's spec. Pure CPU: no device, window or game data.

#include <components/render/cloudcurvemath.hpp>
#include <components/render/cloudmath.hpp>
#include <components/render/cloudvolumegpu.hpp>
#include <components/render/cloudvolumemath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>

namespace
{
    using namespace Render::CloudVolume;

    constexpr float radiusM = 6371000.f;
    constexpr float inf = std::numeric_limits<float>::infinity();
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();

    // The height above the sphere's surface of the point `t` metres along a ray from a camera `z0` above it, in double
    // precision (the reference the float twin is checked against).
    double referenceHeight(double z0, double dirZ, double t, double radius)
    {
        const double dirH = std::sqrt(std::max(0.0, 1.0 - dirZ * dirZ));
        const double x = dirH * t;
        const double z = radius + z0 + dirZ * t;
        return std::sqrt(x * x + z * z) - radius;
    }

    double elevation(double degrees)
    {
        return std::sin(degrees * 3.14159265358979323846 / 180.0);
    }

    // --- curvedLayerSpan -----------------------------------------------------------------------------------------

    TEST(RenderCloudCurved, aRayStraightUpCrossesTheLayerAtItsHeights)
    {
        const Span s = curvedLayerSpan(2.f, 1.f, radiusM, 1500.f, 4000.f, 300000.f);
        ASSERT_TRUE(s.hit);
        EXPECT_NEAR(s.enter, 1498.f, 1.f);
        EXPECT_NEAR(s.exit, 3998.f, 1.f);
    }

    TEST(RenderCloudCurved, aRayAtTenthOfADegreeHitsTheShellAtAFiniteDistance)
    {
        const double dz = elevation(0.1);
        const Span s = curvedLayerSpan(2.f, static_cast<float>(dz), radiusM, 1500.f, 4000.f, 300000.f);
        ASSERT_TRUE(s.hit);
        // sqrt(2 R h) is about 138 km at 1.5 km and 226 km at 4 km, a little less at a tenth of a degree.
        EXPECT_GT(s.enter, 90000.f);
        EXPECT_LT(s.enter, 140000.f);
        EXPECT_GT(s.exit, 150000.f);
        EXPECT_LT(s.exit, 230000.f);
        EXPECT_GT(s.exit, s.enter);
        // The points where it enters and leaves are on the two spheres.
        EXPECT_NEAR(referenceHeight(2.0, dz, s.enter, radiusM), 1500.0, 3.0);
        EXPECT_NEAR(referenceHeight(2.0, dz, s.exit, radiusM), 4000.0, 3.0);
    }

    TEST(RenderCloudCurved, entryAndExitMatchTheDoublePrecisionSolutionAtManyAngles)
    {
        for (const double deg : { 0.01, 0.05, 0.3, 1.0, 3.0, 10.0, 30.0, 60.0, 89.0 })
        {
            const double dz = elevation(deg);
            const Span s = curvedLayerSpan(2.f, static_cast<float>(dz), radiusM, 1500.f, 4000.f, 1.0e9f);
            ASSERT_TRUE(s.hit) << deg;
            EXPECT_NEAR(referenceHeight(2.0, dz, s.enter, radiusM), 1500.0, 1.5 + s.enter * 1e-5) << deg;
            EXPECT_NEAR(referenceHeight(2.0, dz, s.exit, radiusM), 4000.0, 1.5 + s.exit * 1e-5) << deg;
        }
    }

    TEST(RenderCloudCurved, theEntryDistanceGrowsAsTheRayFlattens)
    {
        float previous = 0.f;
        for (const double deg : { 89.0, 60.0, 30.0, 10.0, 3.0, 1.0, 0.3, 0.1, 0.03 })
        {
            const Span s = curvedLayerSpan(2.f, static_cast<float>(elevation(deg)), radiusM, 1500.f, 4000.f, 1.0e9f);
            ASSERT_TRUE(s.hit) << deg;
            EXPECT_GT(s.enter, previous) << deg;
            previous = s.enter;
        }
    }

    TEST(RenderCloudCurved, aLevelOrDownwardRayMissesAndTheSmallestUpwardOneHits)
    {
        EXPECT_FALSE(curvedLayerSpan(2.f, 0.f, radiusM, 1500.f, 4000.f, 300000.f).hit);
        EXPECT_FALSE(curvedLayerSpan(2.f, -0.2f, radiusM, 1500.f, 4000.f, 300000.f).hit);
        const Span s = curvedLayerSpan(2.f, 1e-4f, radiusM, 1500.f, 4000.f, 300000.f);
        ASSERT_TRUE(s.hit);
        EXPECT_NEAR(s.enter, std::sqrt(2.0 * radiusM * 1498.0), 4000.0);
    }

    TEST(RenderCloudCurved, aCameraInsideTheLayerStartsAtZero)
    {
        const Span s = curvedLayerSpan(2000.f, 1.f, radiusM, 1500.f, 4000.f, 300000.f);
        ASSERT_TRUE(s.hit);
        EXPECT_FLOAT_EQ(s.enter, 0.f);
        EXPECT_NEAR(s.exit, 2000.f, 1.f);
        const Span level
            = curvedLayerSpan(2000.f, static_cast<float>(elevation(0.1)), radiusM, 1500.f, 4000.f, 300000.f);
        ASSERT_TRUE(level.hit);
        EXPECT_FLOAT_EQ(level.enter, 0.f);
        EXPECT_GT(level.exit, 100000.f);
    }

    TEST(RenderCloudCurved, aCameraAboveTheLayerOrOnTheSpheresIsSafe)
    {
        EXPECT_FALSE(curvedLayerSpan(5000.f, 1.f, radiusM, 1500.f, 4000.f, 300000.f).hit);
        EXPECT_FALSE(curvedLayerSpan(4000.f, 1.f, radiusM, 1500.f, 4000.f, 300000.f).hit);
        const Span onBase = curvedLayerSpan(1500.f, 1.f, radiusM, 1500.f, 4000.f, 300000.f);
        ASSERT_TRUE(onBase.hit);
        EXPECT_FLOAT_EQ(onBase.enter, 0.f);
        EXPECT_NEAR(onBase.exit, 2500.f, 1.f);
    }

    TEST(RenderCloudCurved, theCapClipsTheExitAndAnEntryBeyondItIsAMiss)
    {
        const float dz = static_cast<float>(elevation(0.1));
        const Span full = curvedLayerSpan(2.f, dz, radiusM, 1500.f, 4000.f, 1.0e9f);
        const Span capped = curvedLayerSpan(2.f, dz, radiusM, 1500.f, 4000.f, full.enter + 1000.f);
        ASSERT_TRUE(capped.hit);
        EXPECT_FLOAT_EQ(capped.exit, full.enter + 1000.f);
        EXPECT_FALSE(curvedLayerSpan(2.f, dz, radiusM, 1500.f, 4000.f, full.enter * 0.5f).hit);
        EXPECT_FALSE(curvedLayerSpan(2.f, dz, radiusM, 1500.f, 4000.f, 0.f).hit);
    }

    TEST(RenderCloudCurved, degenerateInputsAreAMissNeverANaNSpan)
    {
        for (const float bad : { nan, inf, -inf })
        {
            EXPECT_FALSE(curvedLayerSpan(bad, 1.f, radiusM, 1500.f, 4000.f, 300000.f).hit);
            EXPECT_FALSE(curvedLayerSpan(2.f, bad, radiusM, 1500.f, 4000.f, 300000.f).hit);
            EXPECT_FALSE(curvedLayerSpan(2.f, 1.f, bad, 1500.f, 4000.f, 300000.f).hit);
            EXPECT_FALSE(curvedLayerSpan(2.f, 1.f, radiusM, bad, 4000.f, 300000.f).hit);
            EXPECT_FALSE(curvedLayerSpan(2.f, 1.f, radiusM, 1500.f, bad, 300000.f).hit);
        }
        EXPECT_FALSE(curvedLayerSpan(2.f, 1.f, radiusM, 1500.f, 4000.f, nan).hit);
        EXPECT_FALSE(curvedLayerSpan(2.f, 1.f, 0.f, 1500.f, 4000.f, 300000.f).hit);
        EXPECT_FALSE(curvedLayerSpan(2.f, 1.f, -radiusM, 1500.f, 4000.f, 300000.f).hit);
        EXPECT_FALSE(curvedLayerSpan(2.f, 1.f, radiusM, 4000.f, 1500.f, 300000.f).hit);
        // A direction a hair past a unit vector (rounding) still gives a finite span.
        const Span s = curvedLayerSpan(2.f, 1.0000001f, radiusM, 1500.f, 4000.f, 300000.f);
        if (s.hit)
        {
            EXPECT_TRUE(std::isfinite(s.enter));
            EXPECT_TRUE(std::isfinite(s.exit));
        }
    }

    TEST(RenderCloudCurved, aSmallPlanetCurvesMoreAndAHugeOneApproachesTheFlatLayer)
    {
        const float dz = static_cast<float>(elevation(1.0));
        const Span small = curvedLayerSpan(2.f, dz, 1000000.f, 1500.f, 4000.f, 1.0e9f);
        const Span big = curvedLayerSpan(2.f, dz, 1.0e11f, 1500.f, 4000.f, 1.0e9f);
        const Span flat = layerSpan(2.f, dz, 1500.f, 4000.f, 1.0e9f);
        ASSERT_TRUE(small.hit && big.hit && flat.hit);
        EXPECT_LT(small.enter, flat.enter + 1.f);
        EXPECT_NEAR(big.enter, flat.enter, flat.enter * 1e-3f);
        EXPECT_NEAR(big.exit, flat.exit, flat.exit * 1e-3f);
    }

    // --- radialHeight ----------------------------------------------------------------------------------------------

    TEST(RenderCloudCurved, theRadialHeightAboveTheCameraIsItsHeight)
    {
        EXPECT_NEAR(radialHeight(0.f, 0.f, 2.f, 1000.f, radiusM), 1002.f, 0.5f);
        EXPECT_NEAR(radialHeight(0.f, 0.f, 0.f, 0.f, radiusM), 0.f, 0.5f);
    }

    TEST(RenderCloudCurved, aPointFarAwayAtTheSameFlatHeightIsAboveTheCurvedSurface)
    {
        // 100 km out at z = 0: the surface has fallen away by d^2 / (2 R), about 785 m, so the point is that far above
        // it.
        EXPECT_NEAR(radialHeight(100000.f, 0.f, 0.f, 0.f, radiusM), 784.8f, 3.f);
        EXPECT_NEAR(radialHeight(0.f, 100000.f, 0.f, 0.f, radiusM), 784.8f, 3.f);
        for (const double d : { 1000.0, 20000.0, 140000.0, 226000.0 })
        {
            const double expected = referenceHeight(0.0, 0.0, d, radiusM);
            EXPECT_NEAR(radialHeight(static_cast<float>(d), 0.f, 0.f, 0.f, radiusM), expected, 2.0) << d;
        }
    }

    TEST(RenderCloudCurved, theRadialHeightFollowsTheRaysDistanceInTheLayer)
    {
        const double dz = elevation(0.5);
        for (const double t : { 1000.0, 50000.0, 100000.0, 180000.0 })
        {
            const double dirH = std::sqrt(1.0 - dz * dz);
            const float h = radialHeight(static_cast<float>(dirH * t), 0.f, 2.f, static_cast<float>(dz * t), radiusM);
            EXPECT_NEAR(h, referenceHeight(2.0, dz, t, radiusM), 2.0) << t;
        }
    }

    TEST(RenderCloudCurved, aBadRadiusOrInputGivesTheFlatHeightOrAFiniteNumber)
    {
        EXPECT_FLOAT_EQ(radialHeight(100000.f, 0.f, 3.f, 500.f, 0.f), 503.f);
        EXPECT_FLOAT_EQ(radialHeight(100000.f, 0.f, 3.f, 500.f, -1.f), 503.f);
        EXPECT_FLOAT_EQ(radialHeight(100000.f, 0.f, 3.f, 500.f, nan), 503.f);
        EXPECT_TRUE(std::isfinite(radialHeight(nan, 0.f, 3.f, 500.f, radiusM)));
        EXPECT_TRUE(std::isfinite(radialHeight(inf, 0.f, 3.f, 500.f, radiusM)));
    }

    // --- the far fade ----------------------------------------------------------------------------------------------

    TEST(RenderCloudCurved, theFarFadeKeepsTheNearCloudAndRemovesTheCap)
    {
        const float cap = 300000.f;
        EXPECT_FLOAT_EQ(farFade(0.f, cap), 1.f);
        EXPECT_FLOAT_EQ(farFade(cap * farFadeStart, cap), 1.f);
        EXPECT_FLOAT_EQ(farFade(cap, cap), 0.f);
        EXPECT_FLOAT_EQ(farFade(cap * 2.f, cap), 0.f);
        float previous = 1.f;
        for (int i = 0; i <= 100; ++i)
        {
            const float v = farFade(cap * static_cast<float>(i) / 100.f, cap);
            EXPECT_LE(v, previous + 1e-6f) << i;
            EXPECT_GE(v, 0.f);
            EXPECT_LE(v, 1.f);
            previous = v;
        }
        EXPECT_GT(farFadeStart, 0.3f);
        EXPECT_LT(farFadeStart, 0.95f);
    }

    TEST(RenderCloudCurved, theFarFadeIsSafeForBadInput)
    {
        EXPECT_FLOAT_EQ(farFade(nan, 300000.f), 0.f);
        EXPECT_FLOAT_EQ(farFade(1000.f, nan), 0.f);
        EXPECT_FLOAT_EQ(farFade(1000.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(farFade(1000.f, -5.f), 0.f);
        EXPECT_FLOAT_EQ(farFade(-10.f, 300000.f), 1.f);
    }

    // --- the large scale variety -----------------------------------------------------------------------------------

    TEST(RenderCloudCurved, everyTileDividesTheLargeTileThatTheWindWrapsBy)
    {
        for (const float tile : { shapeTileMeters, detailTileMeters, detailFineTileMeters })
            EXPECT_FLOAT_EQ(std::fmod(largeTileMeters, tile), 0.f) << tile;
        EXPECT_GT(largeTileMeters, shapeTileMeters * 2.f);
    }

    TEST(RenderCloudCurved, theWarpIsZeroAtTheMiddleOfTheNoiseAndBounded)
    {
        const auto w = varietyWarp(0.5f, 0.5f);
        EXPECT_NEAR(w[0], 0.f, 1e-3f);
        EXPECT_NEAR(w[1], 0.f, 1e-3f);
        for (int i = 0; i <= 10; ++i)
            for (int j = 0; j <= 10; ++j)
            {
                const auto v = varietyWarp(static_cast<float>(i) / 10.f, static_cast<float>(j) / 10.f);
                EXPECT_LE(std::fabs(v[0]), varietyWarpMeters + 1e-3f);
                EXPECT_LE(std::fabs(v[1]), varietyWarpMeters + 1e-3f);
            }
        EXPECT_GT(varietyWarpMeters, 0.f);
        // Out of range and bad noise values are clamped, never infinite.
        const auto hi = varietyWarp(7.f, -7.f);
        EXPECT_NEAR(hi[0], varietyWarpMeters, 1e-3f);
        EXPECT_NEAR(hi[1], -varietyWarpMeters, 1e-3f);
        const auto bad = varietyWarp(nan, inf);
        EXPECT_TRUE(std::isfinite(bad[0]));
        EXPECT_TRUE(std::isfinite(bad[1]));
    }

    TEST(RenderCloudCurved, theWarpIsContinuousInTheNoise)
    {
        float previous = varietyWarp(0.f, 0.f)[0];
        for (int i = 1; i <= 1000; ++i)
        {
            const float v = varietyWarp(static_cast<float>(i) / 1000.f, 0.f)[0];
            EXPECT_LT(std::fabs(v - previous), varietyWarpMeters * 0.01f) << i;
            previous = v;
        }
    }

    TEST(RenderCloudCurved, theCoverageVarietyMovesItBothWaysAndStaysInTheUnitInterval)
    {
        EXPECT_NEAR(varietyCoverage(0.5f, 0.5f), 0.5f, 1e-6f);
        EXPECT_GT(varietyCoverage(0.5f, 1.f), 0.5f);
        EXPECT_LT(varietyCoverage(0.5f, 0.f), 0.5f);
        EXPECT_NEAR(varietyCoverage(0.5f, 1.f) - 0.5f, varietyCoverageAmount, 1e-5f);
        EXPECT_NEAR(0.5f - varietyCoverage(0.5f, 0.f), varietyCoverageAmount, 1e-5f);
        EXPECT_FLOAT_EQ(varietyCoverage(1.f, 1.f), 1.f);
        EXPECT_FLOAT_EQ(varietyCoverage(0.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(varietyCoverage(0.9f, 5.f), 1.f);
        EXPECT_GT(varietyCoverageAmount, 0.f);
        EXPECT_LT(varietyCoverageAmount, 0.5f);
        EXPECT_TRUE(std::isfinite(varietyCoverage(0.5f, nan)));
        EXPECT_FLOAT_EQ(varietyCoverage(nan, 0.5f), 0.f);
    }

    // --- the dome ----------------------------------------------------------------------------------------------------

    TEST(RenderCloudCurved, theLoweredDomeReachesBelowTheHorizonAndKeepsTheApexAbove)
    {
        using namespace Render::Cloud;
        // The rim ring, with the dome lowered: below the horizon, but not by more than a few degrees.
        const float rim = (domeHeight[domeRings - 1] - domeHorizonDrop) / domeRadius[domeRings - 1];
        EXPECT_LT(rim, 0.f);
        EXPECT_GT(rim, -0.1f);
        // Every ring is higher up than the one outside it, so the dome stays a dome, and the apex is above all of them.
        for (std::size_t i = 1; i < domeRings; ++i)
        {
            const float inner = (domeHeight[i - 1] - domeHorizonDrop) / std::max(domeRadius[i - 1], 1.f);
            const float outer = (domeHeight[i] - domeHorizonDrop) / domeRadius[i];
            EXPECT_GT(inner, outer) << i;
        }
        EXPECT_GT(domeHeight[0] - domeHorizonDrop, 0.f);
    }

    // --- the parameter vector and the wind wrap --------------------------------------------------------------------

    TEST(RenderCloudCurved, theCurveParamsAreOffByDefault)
    {
        const auto p = buildCurveParams(ParamsInput{});
        EXPECT_FLOAT_EQ(p[0], 0.f);
        EXPECT_FLOAT_EQ(p[1], 0.f);
        EXPECT_EQ(p.size(), curveParamsFloats);
        EXPECT_EQ(curveParamsFloats, 4u);
        EXPECT_EQ(curveParamsVec4s, 1u);
    }

    TEST(RenderCloudCurved, theCurveParamsCarryTheRadiusAndTheVarietyFlag)
    {
        ParamsInput in;
        in.planetRadiusMeters = radiusM;
        in.variety = true;
        const auto p = buildCurveParams(in);
        EXPECT_FLOAT_EQ(p[0], radiusM);
        EXPECT_FLOAT_EQ(p[1], 1.f);
        in.planetRadiusMeters = nan;
        EXPECT_FLOAT_EQ(buildCurveParams(in)[0], 0.f);
        in.planetRadiusMeters = -4.f;
        EXPECT_FLOAT_EQ(buildCurveParams(in)[0], 0.f);
        in.planetRadiusMeters = 100.f; // a planet smaller than the layer is no planet
        EXPECT_FLOAT_EQ(buildCurveParams(in)[0], 0.f);
    }

    TEST(RenderCloudCurved, theRayLimitIsCarriedInTheMainBlockAndKeepsItsDefault)
    {
        ParamsInput in;
        EXPECT_FLOAT_EQ(in.rayLimitMeters, maxRayMeters);
        in.rayLimitMeters = curvedMaxRayMeters;
        EXPECT_NEAR(buildParams(in)[6], metersToUnits(curvedMaxRayMeters), 1.f);
        EXPECT_GT(curvedMaxRayMeters, 226000.f);
    }

    TEST(RenderCloudCurved, theWindWrapsByTheGivenTile)
    {
        EXPECT_FLOAT_EQ(wrapWind(0.0, largeTileMeters), 0.f);
        EXPECT_FLOAT_EQ(wrapWind(largeTileMeters + 100.0, largeTileMeters), 100.f);
        EXPECT_FLOAT_EQ(wrapWind(-100.0, largeTileMeters), largeTileMeters - 100.f);
        EXPECT_FLOAT_EQ(wrapWind(250.5, shapeTileMeters), wrapWind(250.5));
        // A wind wrapped by the large tile moves the shape tile's noise as the same wind wrapped by the small one.
        for (const double v : { 3.0e5, 12345.678, 1.0e9 + 0.25 })
        {
            const double a = std::fmod(wrapWind(v, largeTileMeters), shapeTileMeters);
            const double b = wrapWind(v);
            EXPECT_NEAR(a, b, 0.05) << v;
        }
    }

    // --- the shader twin -------------------------------------------------------------------------------------------

    std::string readSlang()
    {
        const auto path = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "files" / "shaders"
            / "comp" / "cloud" / "cloud.slang";
        std::ifstream in(path);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::optional<double> slangNumber(const std::string& text, const std::string& name)
    {
        const std::regex re("static\\s+const\\s+\\w+\\s+" + name + "\\s*=\\s*([-+0-9.eE]+)[uUfF]?\\s*;");
        std::smatch m;
        if (!std::regex_search(text, m, re))
            return std::nullopt;
        return std::stod(m[1].str());
    }

    TEST(RenderCloudCurved, slangRepeatsTheConstants)
    {
        const std::string slang = readSlang();
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const std::pair<const char*, double> expected[]
            = { { "farFadeStart", farFadeStart }, { "largeTileMeters", largeTileMeters },
                  { "varietyWarpMeters", varietyWarpMeters }, { "varietyCoverageAmount", varietyCoverageAmount } };
        for (const auto& [name, value] : expected)
        {
            const auto v = slangNumber(slang, name);
            ASSERT_TRUE(v.has_value()) << name;
            EXPECT_NEAR(*v, value, std::max(1e-6, std::fabs(value) * 1e-6)) << name;
        }
    }

    TEST(RenderCloudCurved, slangDeclaresTheCurveVectorAfterTheMatrix)
    {
        const std::string slang = readSlang();
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const std::size_t start = slang.find("struct CloudParams");
        ASSERT_NE(start, std::string::npos);
        const std::size_t end = slang.find('}', start);
        const std::string body = slang.substr(start, end - start);
        const std::size_t matrix = body.find("toPreviousClip");
        const std::size_t curve = body.find("float4 curve");
        ASSERT_NE(matrix, std::string::npos);
        ASSERT_NE(curve, std::string::npos);
        EXPECT_GT(curve, matrix);
    }
}
