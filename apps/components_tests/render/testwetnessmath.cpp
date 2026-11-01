// Tests for the wetness maths, Render::Wetness (task 0037, stage a). Written from the task spec before the
// implementation: the wetness curve (rise, fall, no negative or over-one values), the darkening, roughness and puddle
// functions, the shelter test and the rain ripples. Pure CPU maths: no device, window or game data.

#include <components/render/wetnessmath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace
{
    using namespace Render::Wetness;

    TEST(RenderWetness, targetIsZeroWithoutRainEffect)
    {
        EXPECT_FLOAT_EQ(target(false, 1.f), 0.f);
        EXPECT_FLOAT_EQ(target(false, 0.f), 0.f);
    }

    TEST(RenderWetness, targetFollowsPrecipitationAlphaAndIsClamped)
    {
        EXPECT_FLOAT_EQ(target(true, 0.4f), 0.4f);
        EXPECT_FLOAT_EQ(target(true, 3.f), 1.f);
        EXPECT_FLOAT_EQ(target(true, -2.f), 0.f);
    }

    TEST(RenderWetness, stepRisesAtOneOverRiseSeconds)
    {
        const float wet = step(0.f, 1.f, riseSeconds * 0.5f, 600.f);
        EXPECT_NEAR(wet, 0.5f, 1e-5f);
    }

    TEST(RenderWetness, stepRiseStopsAtTarget)
    {
        EXPECT_FLOAT_EQ(step(0.9f, 1.f, 1000.f, 600.f), 1.f);
        EXPECT_FLOAT_EQ(step(0.f, 0.3f, 1000.f, 600.f), 0.3f);
    }

    TEST(RenderWetness, stepFallsOverDryTime)
    {
        // From soaked to dry takes exactly the dry time at the constant rate.
        EXPECT_NEAR(step(1.f, 0.f, 300.f, 600.f), 0.5f, 1e-5f);
        EXPECT_FLOAT_EQ(step(1.f, 0.f, 600.f, 600.f), 0.f);
    }

    TEST(RenderWetness, stepNeverGoesNegativeOrAboveOne)
    {
        EXPECT_FLOAT_EQ(step(0.1f, 0.f, 1e9f, 600.f), 0.f);
        EXPECT_FLOAT_EQ(step(0.9f, 1.f, 1e9f, 600.f), 1.f);
        EXPECT_FLOAT_EQ(step(5.f, 0.f, 0.f, 600.f), 1.f);
        EXPECT_FLOAT_EQ(step(-5.f, 0.f, 0.f, 600.f), 0.f);
    }

    TEST(RenderWetness, stepIgnoresNegativeTimeAndBadDryTime)
    {
        EXPECT_FLOAT_EQ(step(0.5f, 0.f, -10.f, 600.f), 0.5f);
        // A dry time of zero (or less) dries at once rather than dividing by zero.
        EXPECT_FLOAT_EQ(step(0.5f, 0.f, 1.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(step(0.5f, 0.f, 1.f, -3.f), 0.f);
        EXPECT_FLOAT_EQ(step(0.5f, 0.f, 0.f, 0.f), 0.5f);
    }

    TEST(RenderWetness, stepHoldsWhenWetEqualsTarget)
    {
        EXPECT_FLOAT_EQ(step(0.4f, 0.4f, 10.f, 600.f), 0.4f);
    }

    TEST(RenderWetness, stepFallsToAPartialTargetNotBelowIt)
    {
        // Rain easing off (alpha 0.3) while the ground is soaked.
        EXPECT_FLOAT_EQ(step(1.f, 0.3f, 1e6f, 600.f), 0.3f);
    }

    TEST(RenderWetness, stepIsNotANaNForNaNInput)
    {
        EXPECT_FALSE(std::isnan(step(std::nanf(""), 0.f, 1.f, 600.f)));
        EXPECT_FALSE(std::isnan(step(0.5f, std::nanf(""), 1.f, 600.f)));
    }

    TEST(RenderWetness, darkeningIsOneWhenDry)
    {
        for (float r : { 0.f, 0.3f, 0.8f, 1.f })
            EXPECT_FLOAT_EQ(darkening(0.f, r), 1.f);
    }

    TEST(RenderWetness, darkeningFallsWithWetnessAndStaysPositive)
    {
        float before = 1.f;
        for (float w = 0.1f; w <= 1.001f; w += 0.1f)
        {
            const float d = darkening(w, 0.8f);
            EXPECT_LT(d, before);
            EXPECT_GT(d, 0.4f);
            before = d;
        }
    }

    TEST(RenderWetness, roughSurfacesDarkenMoreThanPolishedOnes)
    {
        EXPECT_LT(darkening(1.f, 1.f), darkening(1.f, 0.1f));
    }

    TEST(RenderWetness, roughnessIsUnchangedWhenDry)
    {
        EXPECT_FLOAT_EQ(roughness(0.8f, 0.f), 0.8f);
    }

    TEST(RenderWetness, roughnessNeverRisesAndReachesTheWetValue)
    {
        EXPECT_NEAR(roughness(0.8f, 1.f), wetRoughness, 1e-5f);
        EXPECT_FLOAT_EQ(roughness(0.05f, 1.f), 0.05f);
        float before = 0.8f;
        for (float w = 0.1f; w <= 1.001f; w += 0.1f)
        {
            const float r = roughness(0.8f, w);
            EXPECT_LE(r, before);
            before = r;
        }
    }

    TEST(RenderWetness, flatnessIsOnlyForNearHorizontalSurfaces)
    {
        EXPECT_FLOAT_EQ(flatness(1.f), 1.f);
        EXPECT_FLOAT_EQ(flatness(0.f), 0.f);
        EXPECT_FLOAT_EQ(flatness(-1.f), 0.f);
        EXPECT_FLOAT_EQ(flatness(0.7f), 0.f);
        EXPECT_GT(flatness(0.95f), 0.f);
        EXPECT_LT(flatness(0.95f), 1.f);
    }

    TEST(RenderWetness, puddleFillStartsAfterHalfWet)
    {
        EXPECT_FLOAT_EQ(puddleFill(0.f), 0.f);
        EXPECT_FLOAT_EQ(puddleFill(0.5f), 0.f);
        EXPECT_GT(puddleFill(0.75f), 0.f);
        EXPECT_LE(puddleFill(1.f), 1.f);
        EXPECT_GT(puddleFill(1.f), puddleFill(0.75f));
    }

    TEST(RenderWetness, noiseIsInRangeAndDeterministic)
    {
        for (int i = -20; i < 20; ++i)
            for (int j = -20; j < 20; ++j)
            {
                const float x = static_cast<float>(i) * 0.37f;
                const float y = static_cast<float>(j) * 0.51f;
                const float n = noise(x, y);
                EXPECT_GE(n, 0.f);
                EXPECT_LT(n, 1.f);
                EXPECT_EQ(n, noise(x, y));
            }
    }

    TEST(RenderWetness, noiseVariesAndIsContinuous)
    {
        float lo = 1.f, hi = 0.f;
        for (int i = 0; i < 200; ++i)
        {
            const float n = noise(static_cast<float>(i) * 0.7f, 3.f);
            lo = std::min(lo, n);
            hi = std::max(hi, n);
        }
        EXPECT_GT(hi - lo, 0.4f);
        EXPECT_NEAR(noise(10.5f, 4.5f), noise(10.5001f, 4.5f), 0.01f);
    }

    TEST(RenderWetness, noiseAtALatticePointMatchesFromEitherSide)
    {
        EXPECT_NEAR(noise(5.f, 7.f), noise(4.99999f, 7.00001f), 1e-3f);
    }

    TEST(RenderWetness, noPuddlesWhenNotWetEnough)
    {
        for (float n : { 0.f, 0.5f, 0.99f })
            EXPECT_FLOAT_EQ(puddleMask(0.4f, 1.f, n), 0.f);
    }

    TEST(RenderWetness, noPuddlesOnSlopes)
    {
        for (float n : { 0.f, 0.5f, 0.99f })
            EXPECT_FLOAT_EQ(puddleMask(1.f, 0.5f, n), 0.f);
    }

    TEST(RenderWetness, puddlesGatherWhereTheNoiseIsHighest)
    {
        EXPECT_GT(puddleMask(1.f, 1.f, 0.999f), 0.99f);
        EXPECT_FLOAT_EQ(puddleMask(1.f, 1.f, 0.f), 0.f);
        EXPECT_GE(puddleMask(1.f, 1.f, 0.9f), puddleMask(1.f, 1.f, 0.8f));
    }

    TEST(RenderWetness, puddleMaskIsInZeroToOneAndGrowsWithWetness)
    {
        for (float w = 0.f; w <= 1.001f; w += 0.05f)
            for (float n = 0.f; n < 1.f; n += 0.05f)
            {
                const float m = puddleMask(w, 1.f, n);
                EXPECT_GE(m, 0.f);
                EXPECT_LE(m, 1.f);
            }
        float area0 = 0.f, area1 = 0.f;
        for (float n = 0.f; n < 1.f; n += 0.01f)
        {
            area0 += puddleMask(0.7f, 1.f, n);
            area1 += puddleMask(1.f, 1.f, n);
        }
        EXPECT_GT(area1, area0);
    }

    TEST(RenderWetness, exposureIsFullWithNoData)
    {
        // No baked cell: nothing says it is covered.
        EXPECT_FLOAT_EQ(exposure(0.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(exposure(0.9f, 0.f), 1.f);
    }

    TEST(RenderWetness, exposureIsFullInTheOpenAndZeroUnderACover)
    {
        EXPECT_FLOAT_EQ(exposure(1.f, 1.f), 1.f);
        EXPECT_FLOAT_EQ(exposure(0.8f, 1.f), 1.f);
        EXPECT_FLOAT_EQ(exposure(0.f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(exposure(0.1f, 1.f), 0.f);
    }

    TEST(RenderWetness, exposureFadesAcrossACellBorder)
    {
        EXPECT_NEAR(exposure(0.f, 0.5f), 0.5f, 1e-5f);
    }

    TEST(RenderWetness, exposureRisesWithSkyVisibility)
    {
        float before = -1.f;
        for (float v = 0.f; v <= 1.001f; v += 0.05f)
        {
            const float e = exposure(v, 1.f);
            EXPECT_GE(e, before);
            before = e;
        }
    }

    TEST(RenderWetness, waterFresnelIsWaterF0AtNormalIncidenceAndOneAtGrazing)
    {
        EXPECT_NEAR(waterFresnel(1.f), waterF0, 1e-6f);
        EXPECT_NEAR(waterFresnel(0.f), 1.f, 1e-6f);
        EXPECT_GT(waterFresnel(0.2f), waterFresnel(0.8f));
    }

    TEST(RenderWetness, noRipplesWithoutRain)
    {
        const vsg::vec2 s = ripple(12.f, 34.f, 5.f, 0.f);
        EXPECT_FLOAT_EQ(s.x, 0.f);
        EXPECT_FLOAT_EQ(s.y, 0.f);
    }

    TEST(RenderWetness, ripplesAreBoundedDeterministicAndMove)
    {
        float maxLen = 0.f;
        bool moved = false;
        for (int i = 0; i < 100; ++i)
        {
            const float x = static_cast<float>(i) * 3.1f;
            const vsg::vec2 a = ripple(x, 10.f, 1.f, 1.f);
            const vsg::vec2 b = ripple(x, 10.f, 1.f, 1.f);
            EXPECT_EQ(a.x, b.x);
            EXPECT_EQ(a.y, b.y);
            EXPECT_FALSE(std::isnan(a.x));
            maxLen = std::max(maxLen, std::sqrt(a.x * a.x + a.y * a.y));
            const vsg::vec2 later = ripple(x, 10.f, 1.4f, 1.f);
            if (later.x != a.x || later.y != a.y)
                moved = true;
        }
        EXPECT_LE(maxLen, rippleMaxSlope + 1e-4f);
        EXPECT_GT(maxLen, 0.f);
        EXPECT_TRUE(moved);
    }

    TEST(RenderWetness, rippleStrengthScalesWithRain)
    {
        float light = 0.f, heavy = 0.f;
        for (int i = 0; i < 100; ++i)
        {
            const float x = static_cast<float>(i) * 2.3f;
            const vsg::vec2 a = ripple(x, 3.f, 0.7f, 0.25f);
            const vsg::vec2 b = ripple(x, 3.f, 0.7f, 1.f);
            light += std::abs(a.x) + std::abs(a.y);
            heavy += std::abs(b.x) + std::abs(b.y);
        }
        EXPECT_LT(light, heavy);
    }
}
