// Tests for the bicubic (Catmull-Rom) upscale of the volumetric clouds' march image (task 0042, stage log
// 2026-11-04): the weights, and the clamp that keeps the negative lobes from ringing. Pure CPU.

#include <components/render/cloudvolumemath.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using namespace Render::CloudVolume;

    TEST(RenderCloudVolumeBicubic, theWeightsAreAPartitionOfUnity)
    {
        for (int i = 0; i <= 64; ++i)
        {
            const auto w = catmullRomWeights(static_cast<float>(i) / 64.f);
            EXPECT_NEAR(w[0] + w[1] + w[2] + w[3], 1.f, 1e-6f) << i;
        }
    }

    TEST(RenderCloudVolumeBicubic, itInterpolatesTheTexelCentres)
    {
        const auto at0 = catmullRomWeights(0.f);
        EXPECT_NEAR(at0[0], 0.f, 1e-7f);
        EXPECT_NEAR(at0[1], 1.f, 1e-7f);
        EXPECT_NEAR(at0[2], 0.f, 1e-7f);
        EXPECT_NEAR(at0[3], 0.f, 1e-7f);
        const auto at1 = catmullRomWeights(1.f);
        EXPECT_NEAR(at1[0], 0.f, 1e-7f);
        EXPECT_NEAR(at1[1], 0.f, 1e-7f);
        EXPECT_NEAR(at1[2], 1.f, 1e-7f);
        EXPECT_NEAR(at1[3], 0.f, 1e-7f);
    }

    TEST(RenderCloudVolumeBicubic, itReproducesALinearRampExactly)
    {
        // Taps at -1, 0, 1, 2 around the fraction t: a linear function f(x) = x is returned as t.
        for (int i = 0; i <= 32; ++i)
        {
            const float t = static_cast<float>(i) / 32.f;
            const auto w = catmullRomWeights(t);
            const float value = w[0] * -1.f + w[1] * 0.f + w[2] * 1.f + w[3] * 2.f;
            EXPECT_NEAR(value, t, 1e-6f) << t;
        }
    }

    TEST(RenderCloudVolumeBicubic, itHasNegativeOutsideLobesOnlyAndPositiveMiddleTaps)
    {
        for (int i = 1; i < 32; ++i)
        {
            const auto w = catmullRomWeights(static_cast<float>(i) / 32.f);
            EXPECT_LE(w[0], 0.f);
            EXPECT_LE(w[3], 0.f);
            EXPECT_GT(w[1], 0.f);
            EXPECT_GT(w[2], 0.f);
        }
    }

    TEST(RenderCloudVolumeBicubic, aFractionOutsideZeroToOneIsClamped)
    {
        EXPECT_EQ(catmullRomWeights(-0.5f), catmullRomWeights(0.f));
        EXPECT_EQ(catmullRomWeights(1.5f), catmullRomWeights(1.f));
        EXPECT_EQ(catmullRomWeights(std::nanf("")), catmullRomWeights(0.f));
    }

    TEST(RenderCloudVolumeBicubic, aStepEdgeIsLimitedToTheNeighbouringValues)
    {
        // The ringing of the negative lobes is cut by clamping to the four nearest taps (the shader does the same).
        const float taps[4] = { 0.f, 0.f, 1.f, 1.f };
        for (int i = 0; i <= 16; ++i)
        {
            const auto w = catmullRomWeights(static_cast<float>(i) / 16.f);
            const float raw = w[0] * taps[0] + w[1] * taps[1] + w[2] * taps[2] + w[3] * taps[3];
            const float limited = clampToNearestTaps(raw, taps[1], taps[2]);
            EXPECT_GE(limited, 0.f);
            EXPECT_LE(limited, 1.f);
        }
        EXPECT_FLOAT_EQ(clampToNearestTaps(-0.2f, 0.f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(clampToNearestTaps(1.3f, 1.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(clampToNearestTaps(std::nanf(""), 0.25f, 0.75f), 0.25f);
    }
}
