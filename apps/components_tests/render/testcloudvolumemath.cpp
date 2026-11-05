// Tests for the volumetric cloud maths, Render::CloudVolume (task 0042, stage a). Written from the header's contract
// before the implementation: units, the height gradient, the density function, Beer's law and the powder term, the
// two-lobe phase function, multiple scattering, the ray and layer meeting, the sample jitter, the weather mapping and
// the CPU reference noise. Pure CPU maths: no device, window or game data.

#include <components/render/cloudvolumemath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

namespace
{
    using namespace Render::CloudVolume;

    constexpr float pi = 3.14159265358979323846f;
    constexpr float tol = 1e-5f;

    // A reference remap and saturate, written here so that the density tests do not lean on the code under test.
    float refSaturate(float v)
    {
        return std::clamp(v, 0.f, 1.f);
    }

    float refRemap(float v, float a0, float a1, float b0, float b1)
    {
        if (a1 == a0)
            return b0;
        return b0 + (v - a0) / (a1 - a0) * (b1 - b0);
    }

    // Henyey-Greenstein lobe that integrates to 1 over the sphere.
    double refHG(double cosTheta, double g)
    {
        const double denom = 1.0 + g * g - 2.0 * g * cosTheta;
        return (1.0 - g * g) / (4.0 * 3.14159265358979323846 * std::pow(denom, 1.5));
    }

    // Integrates a phase function over the sphere: 2 pi times the integral over cos(theta) from -1 to 1 (midpoint
    // rule).
    template <class F>
    double integrateOverSphere(F phase)
    {
        constexpr int n = 200000;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double c = -1.0 + 2.0 * (i + 0.5) / n;
            sum += phase(c) * (2.0 / n);
        }
        return 2.0 * 3.14159265358979323846 * sum;
    }

    // --- Units and settings ----------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, metersToUnitsUsesTheGameFactor)
    {
        constexpr float oneMeter = metersToUnits(1.f);
        EXPECT_NEAR(oneMeter, 69.99125109f, 1e-3f);
        EXPECT_NEAR(metersToUnits(100.f), 6999.125109f, 0.05f);
        EXPECT_FLOAT_EQ(metersToUnits(0.f), 0.f);
    }

    TEST(RenderCloudVolume, unitsAndMetersRoundTrip)
    {
        for (float m : { 0.f, 1.f, 37.5f, 1500.f, 4000.f, 24000.f, -250.f })
            EXPECT_NEAR(unitsToMeters(metersToUnits(m)), m, std::max(1e-4f, std::fabs(m) * 1e-5f));
        for (float u : { 0.f, 1.f, 512.f, 8192.f })
            EXPECT_NEAR(metersToUnits(unitsToMeters(u)), u, std::max(1e-4f, std::fabs(u) * 1e-5f));
    }

    TEST(RenderCloudVolume, namedConstantsHaveTheDocumentedValues)
    {
        EXPECT_FLOAT_EQ(layerBaseMeters, 1500.f);
        EXPECT_FLOAT_EQ(layerTopMeters, 4000.f);
        EXPECT_FLOAT_EQ(defaultErosion, 0.85f);
        EXPECT_FLOAT_EQ(extinctionPerMeter, 0.045f);
        EXPECT_FLOAT_EQ(forwardAnisotropy, 0.8f);
        EXPECT_FLOAT_EQ(backAnisotropy, -0.3f);
        EXPECT_FLOAT_EQ(forwardWeight, 0.5f);
        EXPECT_FLOAT_EQ(multiScatterExtinction, 0.5f);
        EXPECT_FLOAT_EQ(multiScatterEnergy, 0.5f);
        EXPECT_FLOAT_EQ(multiScatterPhase, 0.5f);
        EXPECT_EQ(maxOctaves, 8u);
        EXPECT_EQ(shapeSize, 128u);
        EXPECT_EQ(detailSize, 32u);
        EXPECT_FLOAT_EQ(shapeTileMeters, 12000.f);
        EXPECT_FLOAT_EQ(detailTileMeters, 800.f);
        EXPECT_FLOAT_EQ(shareSeconds, 3.f);
    }

    // --- The detail look (task 0042 stage log, 2026-11-04) ----------------------------------------------------------

    TEST(RenderCloudVolumeDetailLook, theTilesDivideTheWindWrap)
    {
        // wrapWind keeps the offset below the shape tile: every detail tile must divide it, or the clouds jump.
        EXPECT_FLOAT_EQ(std::fmod(shapeTileMeters, detailTileMeters), 0.f);
        EXPECT_FLOAT_EQ(std::fmod(shapeTileMeters, detailFineTileMeters), 0.f);
        EXPECT_LT(detailFineTileMeters, detailTileMeters);
    }

    TEST(RenderCloudVolumeDetailLook, combineDetailKeepsTheMiddleAndTheRange)
    {
        EXPECT_NEAR(combineDetail(0.5f, 0.5f), 0.5f, 1e-6f);
        EXPECT_FLOAT_EQ(combineDetail(0.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(combineDetail(1.f, 1.f), 1.f);
        for (float a : { -3.f, 0.f, 0.2f, 0.9f, 1.f, 7.f })
            for (float b : { -3.f, 0.f, 0.4f, 1.f, 7.f })
            {
                const float v = combineDetail(a, b);
                EXPECT_GE(v, 0.f);
                EXPECT_LE(v, 1.f);
            }
        EXPECT_FLOAT_EQ(combineDetail(std::nanf(""), 0.5f), combineDetail(0.f, 0.5f));
    }

    TEST(RenderCloudVolumeDetailLook, combineDetailStretchesTheContrast)
    {
        // Two lookups averaged would flatten the detail; the contrast factor puts the spread back.
        EXPECT_GT(combineDetail(0.7f, 0.7f), 0.7f);
        EXPECT_LT(combineDetail(0.3f, 0.3f), 0.3f);
        EXPECT_NEAR(combineDetail(0.7f, 0.7f) - 0.5f, (0.7f - 0.5f) * detailContrast, 1e-5f);
    }

    TEST(RenderCloudVolumeDetailLook, combineDetailIsMonotoneAndWeighsTheCoarseLookupMore)
    {
        float last = -1.f;
        for (int i = 0; i <= 20; ++i)
        {
            const float v = combineDetail(static_cast<float>(i) / 20.f, 0.5f);
            EXPECT_GE(v, last);
            last = v;
        }
        // Moving the coarse value by d moves the result by more than moving the fine one by d (where not clamped).
        const float dCoarse = combineDetail(0.55f, 0.5f) - combineDetail(0.5f, 0.5f);
        const float dFine = combineDetail(0.5f, 0.55f) - combineDetail(0.5f, 0.5f);
        EXPECT_GT(dCoarse, dFine);
        EXPECT_NEAR(dCoarse / dFine, (1.f - detailFineWeight) / detailFineWeight, 1e-3f);
    }

    TEST(RenderCloudVolumeDetailLook, theSunScaleIsTwoPi)
    {
        EXPECT_NEAR(sunPhaseScale, 6.28318530718f, 1e-5f);
    }

    TEST(RenderCloudVolume, parseQualityReadsTheThreeNames)
    {
        ASSERT_TRUE(parseQuality("low").has_value());
        ASSERT_TRUE(parseQuality("medium").has_value());
        ASSERT_TRUE(parseQuality("high").has_value());
        EXPECT_EQ(*parseQuality("low"), Quality::Low);
        EXPECT_EQ(*parseQuality("medium"), Quality::Medium);
        EXPECT_EQ(*parseQuality("high"), Quality::High);
    }

    TEST(RenderCloudVolume, parseQualityRefusesUnknownNames)
    {
        EXPECT_FALSE(parseQuality("").has_value());
        EXPECT_FALSE(parseQuality("ultra").has_value());
        EXPECT_FALSE(parseQuality("lowest").has_value());
        EXPECT_FALSE(parseQuality("med").has_value());
    }

    TEST(RenderCloudVolume, useVolumetricCloudsNeedsAllThree)
    {
        for (int bits = 0; bits < 8; ++bits)
        {
            const bool setting = (bits & 1) != 0;
            const bool hdr = (bits & 2) != 0;
            const bool atmosphere = (bits & 4) != 0;
            EXPECT_EQ(useVolumetricClouds(setting, hdr, atmosphere), setting && hdr && atmosphere) << bits;
        }
    }

    TEST(RenderCloudVolume, tiersFollowTheDecisionTable)
    {
        const TierSettings low = tierSettings(Quality::Low);
        const TierSettings medium = tierSettings(Quality::Medium);
        const TierSettings high = tierSettings(Quality::High);
        EXPECT_EQ(low.resolutionDivisor, 8u);
        EXPECT_EQ(medium.resolutionDivisor, 4u);
        EXPECT_EQ(high.resolutionDivisor, 2u);
        EXPECT_EQ(low.steps, 24u);
        EXPECT_EQ(medium.steps, 40u);
        EXPECT_EQ(high.steps, 64u);
        EXPECT_FLOAT_EQ(low.budgetMs, 1.0f);
        EXPECT_FLOAT_EQ(medium.budgetMs, 1.8f);
        EXPECT_FLOAT_EQ(high.budgetMs, 3.0f);
        EXPECT_GT(low.lightSteps, 0u);
        EXPECT_GT(medium.lightSteps, 0u);
        EXPECT_GT(high.lightSteps, 0u);
    }

    TEST(RenderCloudVolume, marchExtentDividesAndRoundsUp)
    {
        EXPECT_EQ(marchExtent(1920, 1080, Quality::Medium), (std::array<std::uint32_t, 2>{ 480, 270 }));
        EXPECT_EQ(marchExtent(1920, 1080, Quality::Low), (std::array<std::uint32_t, 2>{ 240, 135 }));
        EXPECT_EQ(marchExtent(1920, 1080, Quality::High), (std::array<std::uint32_t, 2>{ 960, 540 }));
        // 1081 / 8 = 135.125 rounds up; 1921 / 4 = 480.25 rounds up.
        EXPECT_EQ(marchExtent(1921, 1081, Quality::Low), (std::array<std::uint32_t, 2>{ 241, 136 }));
        EXPECT_EQ(marchExtent(1921, 1081, Quality::Medium), (std::array<std::uint32_t, 2>{ 481, 271 }));
        EXPECT_EQ(marchExtent(1921, 1081, Quality::High), (std::array<std::uint32_t, 2>{ 961, 541 }));
    }

    TEST(RenderCloudVolume, marchExtentIsNeverZero)
    {
        for (Quality q : { Quality::Low, Quality::Medium, Quality::High })
        {
            const auto zero = marchExtent(0, 0, q);
            EXPECT_GE(zero[0], 1u);
            EXPECT_GE(zero[1], 1u);
            const auto one = marchExtent(1, 1, q);
            EXPECT_EQ(one[0], 1u);
            EXPECT_EQ(one[1], 1u);
        }
    }

    // --- Small shaping functions -----------------------------------------------------------------------------------

    TEST(RenderCloudVolume, heightFractionIsZeroAtTheBaseAndOneAtTheTop)
    {
        EXPECT_NEAR(heightFraction(1500.f, 1500.f, 4000.f), 0.f, tol);
        EXPECT_NEAR(heightFraction(4000.f, 1500.f, 4000.f), 1.f, tol);
        EXPECT_NEAR(heightFraction(2750.f, 1500.f, 4000.f), 0.5f, tol);
        EXPECT_NEAR(heightFraction(1500.f + 625.f, 1500.f, 4000.f), 0.25f, tol);
    }

    TEST(RenderCloudVolume, heightFractionClamps)
    {
        EXPECT_FLOAT_EQ(heightFraction(0.f, 1500.f, 4000.f), 0.f);
        EXPECT_FLOAT_EQ(heightFraction(-100000.f, 1500.f, 4000.f), 0.f);
        EXPECT_FLOAT_EQ(heightFraction(9000.f, 1500.f, 4000.f), 1.f);
    }

    TEST(RenderCloudVolume, heightFractionOfADegenerateLayerIsFiniteAndInRange)
    {
        for (float h : { 0.f, 1500.f, 3000.f })
        {
            const float f = heightFraction(h, 1500.f, 1500.f);
            EXPECT_TRUE(std::isfinite(f)) << h;
            EXPECT_GE(f, 0.f);
            EXPECT_LE(f, 1.f);
        }
    }

    TEST(RenderCloudVolume, saturateClampsToUnitInterval)
    {
        EXPECT_FLOAT_EQ(saturate(-3.f), 0.f);
        EXPECT_FLOAT_EQ(saturate(0.25f), 0.25f);
        EXPECT_FLOAT_EQ(saturate(7.f), 1.f);
    }

    TEST(RenderCloudVolume, smoothstepIsZeroOneAndHalfwayHalf)
    {
        EXPECT_NEAR(smoothstep(0.2f, 0.8f, 0.1f), 0.f, tol);
        EXPECT_NEAR(smoothstep(0.2f, 0.8f, 0.9f), 1.f, tol);
        EXPECT_NEAR(smoothstep(0.2f, 0.8f, 0.5f), 0.5f, tol);
        float previous = 0.f;
        for (int i = 0; i <= 100; ++i)
        {
            const float v = smoothstep(0.2f, 0.8f, 0.2f + 0.6f * i / 100.f);
            EXPECT_GE(v, previous - tol);
            previous = v;
        }
    }

    TEST(RenderCloudVolume, remapMapsLinearly)
    {
        EXPECT_NEAR(remap(0.5f, 0.f, 1.f, 10.f, 20.f), 15.f, tol);
        EXPECT_NEAR(remap(0.f, 0.f, 1.f, 10.f, 20.f), 10.f, tol);
        EXPECT_NEAR(remap(1.f, 0.f, 1.f, 10.f, 20.f), 20.f, tol);
        EXPECT_NEAR(remap(3.f, 2.f, 4.f, 0.f, 1.f), 0.5f, tol);
        // Not clamped: the range extends past the ends.
        EXPECT_NEAR(remap(2.f, 0.f, 1.f, 0.f, 1.f), 2.f, tol);
        // A reversed output range is allowed.
        EXPECT_NEAR(remap(0.25f, 0.f, 1.f, 1.f, 0.f), 0.75f, tol);
    }

    TEST(RenderCloudVolume, remapOfAnEmptyInputRangeIsTheOutputStart)
    {
        EXPECT_FLOAT_EQ(remap(0.7f, 0.5f, 0.5f, 3.f, 9.f), 3.f);
        EXPECT_FLOAT_EQ(remap(-4.f, 1.f, 1.f, -2.f, 8.f), -2.f);
    }

    TEST(RenderCloudVolume, heightGradientHasFlatBasesAndRoundedTops)
    {
        EXPECT_NEAR(heightGradient(0.f), 0.f, tol);
        EXPECT_NEAR(heightGradient(0.1f), 1.f, tol);
        EXPECT_NEAR(heightGradient(0.3f), 1.f, tol);
        EXPECT_NEAR(heightGradient(0.5f), 1.f, tol);
        EXPECT_NEAR(heightGradient(0.6f), 1.f, tol);
        EXPECT_NEAR(heightGradient(1.f), 0.f, tol);
    }

    TEST(RenderCloudVolume, heightGradientRisesThenFalls)
    {
        // Non-decreasing up to a tenth of the layer, flat to 0.6, non-increasing to the top, always in 0..1.
        float previous = heightGradient(0.f);
        for (int i = 1; i <= 100; ++i)
        {
            const float f = i / 100.f;
            const float g = heightGradient(f);
            EXPECT_GE(g, 0.f);
            EXPECT_LE(g, 1.f + tol);
            if (f <= 0.1f)
            {
                EXPECT_GE(g, previous - tol) << f;
            }
            else if (f >= 0.6f)
            {
                EXPECT_LE(g, previous + tol) << f;
            }
            previous = g;
        }
        EXPECT_GT(heightGradient(0.05f), 0.f);
        EXPECT_LT(heightGradient(0.05f), 1.f);
        EXPECT_GT(heightGradient(0.8f), 0.f);
        EXPECT_LT(heightGradient(0.8f), 1.f);
    }

    // --- Density ---------------------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, shapeBaseMatchesItsClosedForm)
    {
        for (float pw : { 0.f, 0.2f, 0.5f, 0.8f, 1.f })
            for (float wf : { 0.f, 0.3f, 0.6f, 0.9f, 1.f })
                EXPECT_NEAR(shapeBase(pw, wf), refSaturate(refRemap(pw, wf - 1.f, 1.f, 0.f, 1.f)), tol)
                    << pw << " " << wf;
    }

    TEST(RenderCloudVolume, shapeBaseGrowsWithPerlinWorleyAndShrinksWithWorley)
    {
        EXPECT_LE(shapeBase(0.3f, 0.5f), shapeBase(0.6f, 0.5f));
        EXPECT_GE(shapeBase(0.5f, 0.2f), shapeBase(0.5f, 0.8f));
    }

    TEST(RenderCloudVolume, cloudDensityIsZeroWithoutCoverageOrGradient)
    {
        for (float shape : { 0.f, 0.4f, 0.9f, 1.f })
            for (float detail : { 0.f, 0.5f, 1.f })
            {
                EXPECT_FLOAT_EQ(cloudDensity(shape, detail, 0.f, 1.f), 0.f) << shape << " " << detail;
                EXPECT_FLOAT_EQ(cloudDensity(shape, detail, 1.f, 0.f), 0.f) << shape << " " << detail;
                EXPECT_FLOAT_EQ(cloudDensity(shape, detail, 0.7f, 0.f), 0.f) << shape << " " << detail;
            }
    }

    TEST(RenderCloudVolume, cloudDensityWithFullCoverageAndNoDetailIsTheShape)
    {
        for (float shape : { 0.f, 0.1f, 0.35f, 0.6f, 0.95f, 1.f })
            EXPECT_NEAR(cloudDensity(shape, 0.f, 1.f, 1.f), shape, tol) << shape;
    }

    TEST(RenderCloudVolume, cloudDensityMatchesItsDocumentedFormula)
    {
        const float erosion = defaultErosion;
        for (float shape : { 0.3f, 0.6f, 0.85f })
            for (float detail : { 0.1f, 0.5f, 0.9f })
                for (float coverage : { 0.4f, 0.6f, 0.9f })
                    for (float gradient : { 0.5f, 0.8f, 1.f })
                    {
                        const float base = shape * gradient;
                        float d = refSaturate(refRemap(base, 1.f - coverage, 1.f, 0.f, 1.f));
                        d = refSaturate(refRemap(d, detail * erosion, 1.f, 0.f, 1.f));
                        EXPECT_NEAR(cloudDensity(shape, detail, coverage, gradient), d, tol)
                            << shape << " " << detail << " " << coverage << " " << gradient;
                    }
    }

    TEST(RenderCloudVolume, cloudDensityHonoursAnExplicitErosion)
    {
        const float shape = 0.7f, detail = 0.6f, coverage = 0.8f, gradient = 1.f, erosion = 0.9f;
        float d = refSaturate(refRemap(shape * gradient, 1.f - coverage, 1.f, 0.f, 1.f));
        d = refSaturate(refRemap(d, detail * erosion, 1.f, 0.f, 1.f));
        EXPECT_NEAR(cloudDensity(shape, detail, coverage, gradient, erosion), d, tol);
        // With no erosion the detail value has no effect.
        EXPECT_NEAR(
            cloudDensity(shape, 0.f, coverage, gradient, 0.f), cloudDensity(shape, 1.f, coverage, gradient, 0.f), tol);
    }

    TEST(RenderCloudVolume, cloudDensityStaysInTheUnitInterval)
    {
        for (float shape = 0.f; shape <= 1.f; shape += 0.125f)
            for (float detail = 0.f; detail <= 1.f; detail += 0.25f)
                for (float coverage = 0.f; coverage <= 1.f; coverage += 0.25f)
                    for (float gradient = 0.f; gradient <= 1.f; gradient += 0.25f)
                    {
                        const float d = cloudDensity(shape, detail, coverage, gradient);
                        EXPECT_GE(d, 0.f);
                        EXPECT_LE(d, 1.f);
                    }
    }

    TEST(RenderCloudVolume, cloudDensityGrowsWithShapeAndWithCoverage)
    {
        float previous = 0.f;
        for (int i = 0; i <= 20; ++i)
        {
            const float d = cloudDensity(i / 20.f, 0.3f, 0.7f, 1.f);
            EXPECT_GE(d, previous - tol);
            previous = d;
        }
        previous = 0.f;
        for (int i = 0; i <= 20; ++i)
        {
            const float d = cloudDensity(0.6f, 0.3f, i / 20.f, 1.f);
            EXPECT_GE(d, previous - tol);
            previous = d;
        }
    }

    TEST(RenderCloudVolume, cloudDensityDetailErodesTheEdges)
    {
        // A thin edge (low density) loses more to the detail noise than the plain value; more detail never adds cloud.
        const float noDetail = cloudDensity(0.6f, 0.f, 0.7f, 1.f);
        const float fullDetail = cloudDensity(0.6f, 1.f, 0.7f, 1.f);
        EXPECT_GT(noDetail, 0.f);
        EXPECT_LT(fullDetail, noDetail);
        float previous = noDetail;
        for (int i = 1; i <= 10; ++i)
        {
            const float d = cloudDensity(0.6f, i / 10.f, 0.7f, 1.f);
            EXPECT_LE(d, previous + tol);
            previous = d;
        }
    }

    // --- Optical depth and light -----------------------------------------------------------------------------------

    TEST(RenderCloudVolume, opticalDepthIsExtinctionTimesLength)
    {
        const float stepUnits = metersToUnits(100.f);
        EXPECT_NEAR(opticalDepth(1.f, 1.f, stepUnits), extinctionPerMeter * 100.f, 1e-4f);
        EXPECT_NEAR(opticalDepth(0.5f, 2.f, stepUnits), extinctionPerMeter * 100.f, 1e-4f);
        EXPECT_FLOAT_EQ(opticalDepth(0.f, 1.f, stepUnits), 0.f);
        EXPECT_FLOAT_EQ(opticalDepth(1.f, 1.f, 0.f), 0.f);
    }

    TEST(RenderCloudVolume, opticalDepthIsLinearInDensityScaleAndLength)
    {
        const float base = opticalDepth(0.4f, 1.f, 700.f);
        EXPECT_NEAR(opticalDepth(0.8f, 1.f, 700.f), 2.f * base, 1e-5f);
        EXPECT_NEAR(opticalDepth(0.4f, 3.f, 700.f), 3.f * base, 1e-5f);
        EXPECT_NEAR(opticalDepth(0.4f, 1.f, 2100.f), 3.f * base, 1e-5f);
    }

    TEST(RenderCloudVolume, opticalDepthAlongSumsTheExtinctions)
    {
        const std::vector<float> e{ 0.01f, 0.04f, 0.f, 0.09f };
        EXPECT_NEAR(opticalDepthAlong(e.data(), e.size(), 50.f), 0.14f * 50.f, 1e-5f);
        EXPECT_FLOAT_EQ(opticalDepthAlong(e.data(), 0, 50.f), 0.f);
    }

    TEST(RenderCloudVolume, beerIsOneAtZeroAndFallsExponentially)
    {
        EXPECT_NEAR(beer(0.f), 1.f, tol);
        EXPECT_NEAR(beer(1.f), std::exp(-1.f), tol);
        EXPECT_NEAR(beer(2.5f), std::exp(-2.5f), tol);
        EXPECT_NEAR(beer(1.f) * beer(2.f), beer(3.f), tol);
    }

    TEST(RenderCloudVolume, slabTransmittanceIsTheClosedForm)
    {
        EXPECT_NEAR(slabTransmittance(0.045f, 100.f), std::exp(-4.5f), tol);
        EXPECT_NEAR(slabTransmittance(0.f, 5000.f), 1.f, tol);
        EXPECT_NEAR(slabTransmittance(0.02f, 0.f), 1.f, tol);
    }

    TEST(RenderCloudVolume, aUniformSlabOfEqualStepsMatchesTheClosedForm)
    {
        for (std::size_t n : { 1u, 4u, 16u, 40u })
        {
            const float sigma = 0.03f;
            const float stepMeters = 25.f;
            const std::vector<float> extinctions(n, sigma);
            const float depth = opticalDepthAlong(extinctions.data(), n, stepMeters);
            EXPECT_NEAR(beer(depth), slabTransmittance(sigma, stepMeters * static_cast<float>(n)), tol) << n;
        }
    }

    TEST(RenderCloudVolume, powderIsZeroAtZeroAndTendsToOne)
    {
        EXPECT_NEAR(powder(0.f), 0.f, tol);
        EXPECT_NEAR(powder(1.f), 1.f - std::exp(-2.f), tol);
        EXPECT_NEAR(powder(20.f), 1.f, tol);
    }

    TEST(RenderCloudVolume, beerPowderHasTheDocumentedValues)
    {
        EXPECT_NEAR(beerPowder(0.f), 0.f, tol);
        for (float d : { 0.1f, 0.5f, 1.f, 2.f, 4.f })
            EXPECT_NEAR(beerPowder(d), 2.f * std::exp(-d) * (1.f - std::exp(-2.f * d)), tol) << d;
    }

    TEST(RenderCloudVolume, beerPowderRisesToAPeakAndTendsToZero)
    {
        EXPECT_GT(beerPowder(0.2f), 0.f);
        EXPECT_GT(beerPowder(0.5f), beerPowder(0.05f));
        EXPECT_GT(beerPowder(0.5f), beerPowder(6.f));
        EXPECT_LT(beerPowder(15.f), 1e-5f);
        EXPECT_GE(beerPowder(30.f), 0.f);
        for (float d = 0.01f; d < 10.f; d += 0.37f)
            EXPECT_GT(beerPowder(d), 0.f) << d;
    }

    // --- Phase function --------------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, aSingleLobeIntegratesToOneOverTheSphere)
    {
        // weight 1 keeps only the forward lobe, weight 0 only the back lobe.
        const double forward = integrateOverSphere([](double c) {
            return static_cast<double>(dualLobePhase(static_cast<float>(c), forwardAnisotropy, backAnisotropy, 1.f));
        });
        const double back = integrateOverSphere([](double c) {
            return static_cast<double>(dualLobePhase(static_cast<float>(c), forwardAnisotropy, backAnisotropy, 0.f));
        });
        EXPECT_NEAR(forward, 1.0, 2e-3);
        EXPECT_NEAR(back, 1.0, 2e-3);
    }

    TEST(RenderCloudVolume, theMixedLobeIntegratesToOneOverTheSphere)
    {
        const double mixed
            = integrateOverSphere([](double c) { return static_cast<double>(dualLobePhase(static_cast<float>(c))); });
        EXPECT_NEAR(mixed, 1.0, 2e-3);
        const double other = integrateOverSphere(
            [](double c) { return static_cast<double>(dualLobePhase(static_cast<float>(c), 0.6f, -0.5f, 0.3f)); });
        EXPECT_NEAR(other, 1.0, 2e-3);
    }

    TEST(RenderCloudVolume, anIsotropicLobeIsOneOverFourPi)
    {
        for (float c : { -1.f, -0.4f, 0.f, 0.7f, 1.f })
            EXPECT_NEAR(dualLobePhase(c, 0.f, 0.f, 0.5f), 1.f / (4.f * pi), 1e-5f) << c;
    }

    TEST(RenderCloudVolume, theForwardLobeIsBrighterThanTheBackAtTheSun)
    {
        EXPECT_GT(dualLobePhase(1.f), dualLobePhase(-1.f));
        EXPECT_GT(dualLobePhase(1.f, forwardAnisotropy, backAnisotropy, 1.f),
            dualLobePhase(1.f, forwardAnisotropy, backAnisotropy, 0.f));
    }

    TEST(RenderCloudVolume, theWeightMixesTheTwoLobesLinearly)
    {
        for (float c : { -0.8f, -0.1f, 0.5f, 0.95f })
        {
            const float fwd = dualLobePhase(c, 0.7f, -0.4f, 1.f);
            const float bck = dualLobePhase(c, 0.7f, -0.4f, 0.f);
            EXPECT_NEAR(dualLobePhase(c, 0.7f, -0.4f, 0.3f), 0.3f * fwd + 0.7f * bck, 1e-5f) << c;
            EXPECT_NEAR(dualLobePhase(c, 0.7f, -0.4f, 0.5f), 0.5f * (fwd + bck), 1e-5f) << c;
        }
    }

    TEST(RenderCloudVolume, theWeightExtremesIgnoreTheOtherLobe)
    {
        for (float c : { -0.6f, 0.2f, 0.9f })
        {
            // Weight 1: the back anisotropy has no effect. Weight 0: the forward anisotropy has none.
            EXPECT_NEAR(dualLobePhase(c, 0.8f, -0.3f, 1.f), dualLobePhase(c, 0.8f, 0.4f, 1.f), 1e-6f);
            EXPECT_NEAR(dualLobePhase(c, 0.8f, -0.3f, 0.f), dualLobePhase(c, 0.1f, -0.3f, 0.f), 1e-6f);
        }
    }

    TEST(RenderCloudVolume, dualLobePhaseMatchesHenyeyGreenstein)
    {
        for (float c : { -1.f, -0.5f, 0.f, 0.5f, 0.9f, 1.f })
        {
            const double expected = 0.5 * refHG(c, forwardAnisotropy) + 0.5 * refHG(c, backAnisotropy);
            EXPECT_NEAR(dualLobePhase(c), static_cast<float>(expected), static_cast<float>(expected) * 1e-4f + 1e-6f)
                << c;
        }
    }

    // --- Multiple scattering and ambient ---------------------------------------------------------------------------

    TEST(RenderCloudVolume, oneOctaveIsSingleScattering)
    {
        for (float d : { 0.f, 0.5f, 2.f })
            for (float c : { -0.7f, 0.f, 0.9f })
                EXPECT_NEAR(multiScatter(d, c, 1), dualLobePhase(c) * beer(d), 1e-6f) << d << " " << c;
    }

    TEST(RenderCloudVolume, multiScatterSumsTheOctaves)
    {
        for (std::uint32_t octaves : { 2u, 3u, 5u })
        {
            const float d = 1.2f, c = 0.6f;
            float expected = 0.f;
            float a = 1.f, b = 1.f, cc = 1.f;
            for (std::uint32_t i = 0; i < octaves; ++i)
            {
                expected += b * dualLobePhase(c, forwardAnisotropy * cc, backAnisotropy * cc) * beer(d * a);
                a *= multiScatterExtinction;
                b *= multiScatterEnergy;
                cc *= multiScatterPhase;
            }
            EXPECT_NEAR(multiScatter(d, c, octaves), expected, 1e-5f) << octaves;
        }
    }

    TEST(RenderCloudVolume, moreOctavesNeverGiveLess)
    {
        float previous = multiScatter(1.5f, 0.3f, 0);
        for (std::uint32_t octaves = 1; octaves <= maxOctaves; ++octaves)
        {
            const float v = multiScatter(1.5f, 0.3f, octaves);
            EXPECT_GE(v, previous - 1e-7f) << octaves;
            previous = v;
        }
    }

    TEST(RenderCloudVolume, multiScatterFallsWithOpticalDepth)
    {
        for (std::uint32_t octaves : { 1u, 4u, 8u })
        {
            float previous = multiScatter(0.f, 0.5f, octaves);
            for (float d = 0.25f; d <= 6.f; d += 0.25f)
            {
                const float v = multiScatter(d, 0.5f, octaves);
                EXPECT_LE(v, previous + 1e-7f) << octaves << " " << d;
                previous = v;
            }
        }
    }

    TEST(RenderCloudVolume, multiScatterClampsTheOctaves)
    {
        EXPECT_FLOAT_EQ(multiScatter(1.f, 0.4f, maxOctaves + 1), multiScatter(1.f, 0.4f, maxOctaves));
        EXPECT_FLOAT_EQ(multiScatter(1.f, 0.4f, 1000), multiScatter(1.f, 0.4f, maxOctaves));
    }

    TEST(RenderCloudVolume, multiScatterOfZeroOctavesIsZero)
    {
        EXPECT_FLOAT_EQ(multiScatter(1.f, 0.4f, 0), 0.f);
    }

    TEST(RenderCloudVolume, ambientHeightFactorGoesFromTheMinimumToOne)
    {
        EXPECT_NEAR(ambientHeightFactor(0.f), baseAmbientShare, tol);
        EXPECT_NEAR(ambientHeightFactor(1.f), 1.f, tol);
        float previous = ambientHeightFactor(0.f);
        for (int i = 1; i <= 50; ++i)
        {
            const float v = ambientHeightFactor(i / 50.f);
            EXPECT_GE(v, baseAmbientShare - tol);
            EXPECT_LE(v, 1.f + tol);
            EXPECT_GE(v, previous - tol);
            previous = v;
        }
    }

    TEST(RenderCloudVolume, hazeBlendIsTheClosedForm)
    {
        EXPECT_NEAR(hazeBlend(0.f, 20000.f), 0.f, tol);
        EXPECT_NEAR(hazeBlend(20000.f, 20000.f), 1.f - std::exp(-1.f), tol);
        EXPECT_NEAR(hazeBlend(5000.f, 20000.f), 1.f - std::exp(-0.25f), tol);
        EXPECT_NEAR(hazeBlend(1e7f, 20000.f), 1.f, tol);
        EXPECT_LT(hazeBlend(1000.f, 40000.f), hazeBlend(1000.f, 10000.f));
    }

    TEST(RenderCloudVolume, hazeLengthHasAThousandMeterFloor)
    {
        EXPECT_FLOAT_EQ(hazeLengthMeters(0.f), 1000.f);
        EXPECT_GE(hazeLengthMeters(1.f), 1000.f);
        EXPECT_GE(hazeLengthMeters(metersToUnits(1.f)), 1000.f);
    }

    TEST(RenderCloudVolume, hazeLengthScalesWithTheFogDistance)
    {
        // A fog that ends at 100 m is a haze of 400 * 100 m.
        EXPECT_NEAR(hazeLengthMeters(metersToUnits(100.f)), 100.f * hazeLengthPerFogMeter, 1.f);
        EXPECT_NEAR(hazeLengthMeters(metersToUnits(250.f)), 250.f * hazeLengthPerFogMeter, 2.f);
        float previous = hazeLengthMeters(0.f);
        for (float units = 100.f; units < 100000.f; units *= 2.f)
        {
            const float v = hazeLengthMeters(units);
            EXPECT_GE(v, previous);
            previous = v;
        }
    }

    // --- Rays and steps --------------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, aRayFromBelowLookingUpEntersAtTheBase)
    {
        const Span straight = layerSpan(0.f, 1.f, 1500.f, 4000.f, 1e9f);
        ASSERT_TRUE(straight.hit);
        EXPECT_NEAR(straight.enter, 1500.f, 1e-2f);
        EXPECT_NEAR(straight.exit, 4000.f, 1e-2f);
        // Slanted: the distance is the height difference over dirZ.
        const Span slanted = layerSpan(0.f, 0.5f, 1500.f, 4000.f, 1e9f);
        ASSERT_TRUE(slanted.hit);
        EXPECT_NEAR(slanted.enter, 3000.f, 1e-2f);
        EXPECT_NEAR(slanted.exit, 8000.f, 1e-2f);
    }

    TEST(RenderCloudVolume, aRayInsideTheLayerStartsAtZero)
    {
        const Span up = layerSpan(2000.f, 1.f, 1500.f, 4000.f, 1e9f);
        ASSERT_TRUE(up.hit);
        EXPECT_FLOAT_EQ(up.enter, 0.f);
        EXPECT_NEAR(up.exit, 2000.f, 1e-2f);
        const Span down = layerSpan(2000.f, -1.f, 1500.f, 4000.f, 1e9f);
        ASSERT_TRUE(down.hit);
        EXPECT_FLOAT_EQ(down.enter, 0.f);
        EXPECT_NEAR(down.exit, 500.f, 1e-2f);
        const Span slanted = layerSpan(2000.f, 0.5f, 1500.f, 4000.f, 1e9f);
        ASSERT_TRUE(slanted.hit);
        EXPECT_FLOAT_EQ(slanted.enter, 0.f);
        EXPECT_NEAR(slanted.exit, 4000.f, 1e-2f);
    }

    TEST(RenderCloudVolume, aRayFromAboveLookingUpMisses)
    {
        EXPECT_FALSE(layerSpan(5000.f, 1.f, 1500.f, 4000.f, 1e9f).hit);
        EXPECT_FALSE(layerSpan(5000.f, 0.3f, 1500.f, 4000.f, 1e9f).hit);
    }

    TEST(RenderCloudVolume, aRayFromBelowLookingDownMisses)
    {
        EXPECT_FALSE(layerSpan(0.f, -1.f, 1500.f, 4000.f, 1e9f).hit);
    }

    TEST(RenderCloudVolume, aRayFromAboveLookingDownCrossesTheTopFirst)
    {
        const Span s = layerSpan(5000.f, -1.f, 1500.f, 4000.f, 1e9f);
        ASSERT_TRUE(s.hit);
        EXPECT_NEAR(s.enter, 1000.f, 1e-2f);
        EXPECT_NEAR(s.exit, 3500.f, 1e-2f);
        const Span slanted = layerSpan(5000.f, -0.5f, 1500.f, 4000.f, 1e9f);
        ASSERT_TRUE(slanted.hit);
        EXPECT_NEAR(slanted.enter, 2000.f, 1e-2f);
        EXPECT_NEAR(slanted.exit, 7000.f, 1e-2f);
    }

    TEST(RenderCloudVolume, aHorizontalRayHitsOnlyFromInside)
    {
        const Span inside = layerSpan(2500.f, 0.f, 1500.f, 4000.f, 12345.f);
        ASSERT_TRUE(inside.hit);
        EXPECT_FLOAT_EQ(inside.enter, 0.f);
        EXPECT_NEAR(inside.exit, 12345.f, 1e-2f);
        EXPECT_FALSE(layerSpan(500.f, 0.f, 1500.f, 4000.f, 12345.f).hit);
        EXPECT_FALSE(layerSpan(6000.f, 0.f, 1500.f, 4000.f, 12345.f).hit);
    }

    TEST(RenderCloudVolume, maxDistanceCapsTheExit)
    {
        const Span below = layerSpan(0.f, 1.f, 1500.f, 4000.f, 2000.f);
        ASSERT_TRUE(below.hit);
        EXPECT_NEAR(below.enter, 1500.f, 1e-2f);
        EXPECT_NEAR(below.exit, 2000.f, 1e-2f);
        const Span inside = layerSpan(2000.f, 0.5f, 1500.f, 4000.f, 1000.f);
        ASSERT_TRUE(inside.hit);
        EXPECT_FLOAT_EQ(inside.enter, 0.f);
        EXPECT_NEAR(inside.exit, 1000.f, 1e-2f);
    }

    TEST(RenderCloudVolume, aDegenerateLayerNeverGivesANonsenseSpan)
    {
        for (float origin : { 0.f, 1500.f, 3000.f })
            for (float dirZ : { -1.f, 0.f, 0.6f, 1.f })
            {
                const Span s = layerSpan(origin, dirZ, 1500.f, 1500.f, 1e6f);
                EXPECT_TRUE(std::isfinite(s.enter));
                EXPECT_TRUE(std::isfinite(s.exit));
                if (s.hit)
                {
                    EXPECT_GE(s.exit, s.enter);
                }
            }
    }

    TEST(RenderCloudVolume, aHitSpanIsOrdered)
    {
        for (float origin : { 0.f, 1700.f, 3900.f, 6000.f })
            for (float dirZ : { -0.9f, -0.2f, 0.f, 0.2f, 0.9f })
            {
                const Span s = layerSpan(origin, dirZ, 1500.f, 4000.f, 100000.f);
                if (s.hit)
                {
                    EXPECT_GE(s.enter, 0.f);
                    EXPECT_GE(s.exit, s.enter);
                    EXPECT_LE(s.exit, 100000.f + 1e-2f);
                }
            }
    }

    TEST(RenderCloudVolume, sampleDistanceIsTheDocumentedFormula)
    {
        Span span;
        span.hit = true;
        span.enter = 100.f;
        span.exit = 500.f;
        for (std::uint32_t i = 0; i < 8; ++i)
            for (float offset : { 0.f, 0.25f, 0.99f })
                EXPECT_NEAR(sampleDistance(span, i, 8, offset), 100.f + (static_cast<float>(i) + offset) * 50.f, 1e-3f);
    }

    TEST(RenderCloudVolume, sampleDistancesStayInsideTheSpanAndIncrease)
    {
        Span span;
        span.hit = true;
        span.enter = 10.f;
        span.exit = 90.f;
        float previous = -1.f;
        for (std::uint32_t i = 0; i < 40; ++i)
        {
            const float d = sampleDistance(span, i, 40, 0.5f);
            EXPECT_GT(d, span.enter);
            EXPECT_LT(d, span.exit);
            EXPECT_GT(d, previous);
            previous = d;
        }
    }

    TEST(RenderCloudVolume, lightStepsAddUpToTheTotalAndGrow)
    {
        for (std::uint32_t count : { 1u, 5u, 6u, 12u })
        {
            float sum = 0.f;
            float previous = 0.f;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const float step = lightStepLength(i, count, 600.f);
                EXPECT_GT(step, 0.f);
                if (i > 0)
                {
                    EXPECT_GT(step, previous);
                }
                previous = step;
                sum += step;
            }
            EXPECT_NEAR(sum, 600.f, 1e-3f) << count;
        }
    }

    TEST(RenderCloudVolume, lightStepLengthMatchesTheFormula)
    {
        // Five steps: 1, 2, 3, 4 and 5 parts of 15.
        for (std::uint32_t i = 0; i < 5; ++i)
            EXPECT_NEAR(lightStepLength(i, 5, 150.f), static_cast<float>(i + 1) * 10.f, 1e-4f);
    }

    // --- Jitter ----------------------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, interleavedGradientNoiseIsZeroAtTheOrigin)
    {
        EXPECT_FLOAT_EQ(interleavedGradientNoise(0.f, 0.f), 0.f);
    }

    TEST(RenderCloudVolume, interleavedGradientNoiseStaysInRangeAndIsDeterministic)
    {
        float lowest = 1.f, highest = 0.f;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
            {
                const float v = interleavedGradientNoise(static_cast<float>(x), static_cast<float>(y));
                EXPECT_GE(v, 0.f);
                EXPECT_LT(v, 1.f);
                EXPECT_EQ(v, interleavedGradientNoise(static_cast<float>(x), static_cast<float>(y)));
                lowest = std::min(lowest, v);
                highest = std::max(highest, v);
            }
        // Well spread: it covers most of the interval.
        EXPECT_LT(lowest, 0.1f);
        EXPECT_GT(highest, 0.9f);
    }

    TEST(RenderCloudVolume, rayJitterStaysInTheUnitInterval)
    {
        for (std::uint32_t frame = 0; frame < 64; ++frame)
        {
            const auto j = rayJitter(frame);
            EXPECT_GE(j[0], 0.f);
            EXPECT_LT(j[0], 1.f);
            EXPECT_GE(j[1], 0.f);
            EXPECT_LT(j[1], 1.f);
        }
    }

    TEST(RenderCloudVolume, rayJitterIsDeterministicAndRepeatsEverySixteenFrames)
    {
        for (std::uint32_t frame = 0; frame < 32; ++frame)
        {
            EXPECT_EQ(rayJitter(frame), rayJitter(frame));
            EXPECT_EQ(rayJitter(frame), rayJitter(frame + 16));
            EXPECT_EQ(rayJitter(frame), rayJitter(frame + 5 * 16));
        }
    }

    TEST(RenderCloudVolume, rayJitterIsDistinctWithinAPeriod)
    {
        std::set<std::pair<float, float>> seen;
        for (std::uint32_t frame = 0; frame < 16; ++frame)
        {
            const auto j = rayJitter(frame);
            seen.insert({ j[0], j[1] });
        }
        EXPECT_EQ(seen.size(), 16u);
    }

    // --- Weather ---------------------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, coverageFromThicknessHitsTheTableEnds)
    {
        EXPECT_NEAR(coverageFromThickness(coverageTable.front().thickness), coverageTable.front().coverage, tol);
        EXPECT_NEAR(coverageFromThickness(coverageTable.back().thickness), coverageTable.back().coverage, tol);
    }

    TEST(RenderCloudVolume, coverageFromThicknessHitsEveryTableRow)
    {
        for (const CoveragePoint& p : coverageTable)
            EXPECT_NEAR(coverageFromThickness(p.thickness), p.coverage, tol) << p.thickness;
    }

    TEST(RenderCloudVolume, coverageFromThicknessClampsOutsideTheTable)
    {
        EXPECT_NEAR(coverageFromThickness(-5.f), coverageTable.front().coverage, tol);
        EXPECT_NEAR(coverageFromThickness(5.f), coverageTable.back().coverage, tol);
    }

    TEST(RenderCloudVolume, coverageFromThicknessInterpolatesBetweenRows)
    {
        for (std::size_t i = 0; i + 1 < coverageTable.size(); ++i)
        {
            const CoveragePoint& a = coverageTable[i];
            const CoveragePoint& b = coverageTable[i + 1];
            const float mid = 0.5f * (a.thickness + b.thickness);
            EXPECT_NEAR(coverageFromThickness(mid), 0.5f * (a.coverage + b.coverage), tol) << i;
        }
    }

    TEST(RenderCloudVolume, coverageFromThicknessIsSafeForNonFiniteInput)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        for (float v : { nan, inf, -inf })
        {
            const float c = coverageFromThickness(v);
            EXPECT_TRUE(std::isfinite(c));
            EXPECT_GE(c, coverageTable.front().coverage - tol);
            EXPECT_LE(c, coverageTable.back().coverage + tol);
        }
    }

    TEST(RenderCloudVolume, coverageFromThicknessNeverDecreases)
    {
        float previous = coverageFromThickness(-0.5f);
        for (int i = -50; i <= 150; ++i)
        {
            const float c = coverageFromThickness(i / 100.f);
            EXPECT_GE(c, previous - tol) << i;
            previous = c;
        }
    }

    TEST(RenderCloudVolume, blendCoverageMixesCurrentAndNext)
    {
        EXPECT_NEAR(blendCoverage(0.2f, 0.9f, 0.f), 0.2f, tol);
        EXPECT_NEAR(blendCoverage(0.2f, 0.9f, 1.f), 0.9f, tol);
        EXPECT_NEAR(blendCoverage(0.2f, 0.9f, 0.5f), 0.55f, tol);
        EXPECT_NEAR(blendCoverage(0.8f, 0.4f, 0.25f), 0.7f, tol);
    }

    TEST(RenderCloudVolume, windStepIsLinearInSpeedAndTime)
    {
        EXPECT_NEAR(windStep(1.25f, 10.f), 1.25f * windMetersPerSecondPerSpeed * 10.f, 1e-3f);
        EXPECT_NEAR(windStep(3.f, 2.f), 2.f * windStep(3.f, 1.f), 1e-4f);
        EXPECT_NEAR(windStep(3.f, 2.f), 3.f * windStep(1.f, 2.f), 1e-4f);
        EXPECT_FLOAT_EQ(windStep(0.f, 5.f), 0.f);
        EXPECT_FLOAT_EQ(windStep(2.f, 0.f), 0.f);
    }

    TEST(RenderCloudVolume, windStepIsZeroForNegativeInput)
    {
        EXPECT_FLOAT_EQ(windStep(-2.f, 5.f), 0.f);
        EXPECT_FLOAT_EQ(windStep(2.f, -5.f), 0.f);
    }

    TEST(RenderCloudVolume, windDirectionIsAUnitVector)
    {
        EXPECT_NEAR(windDirection[0] * windDirection[0] + windDirection[1] * windDirection[1], 1.f, 1e-5f);
    }

    TEST(RenderCloudVolume, stormsKeepTheirVanillaSky)
    {
        for (float depth : { 0.f, 0.5f, 0.9f, 1.f })
            EXPECT_FLOAT_EQ(volumeShareTarget(true, depth), 0.f) << depth;
    }

    TEST(RenderCloudVolume, thickLandFogKeepsTheVanillaSky)
    {
        EXPECT_FLOAT_EQ(volumeShareTarget(false, 1.f), 0.f);
        EXPECT_FLOAT_EQ(volumeShareTarget(false, 1.5f), 0.f);
        EXPECT_FLOAT_EQ(volumeShareTarget(false, 4.f), 0.f);
    }

    TEST(RenderCloudVolume, thinFogGivesTheWholeSkyToTheVolume)
    {
        EXPECT_FLOAT_EQ(volumeShareTarget(false, 0.f), 1.f);
        EXPECT_FLOAT_EQ(volumeShareTarget(false, 0.5f), 1.f);
        EXPECT_FLOAT_EQ(volumeShareTarget(false, 0.8f), 1.f);
    }

    TEST(RenderCloudVolume, volumeShareFadesBetweenTheFogDepths)
    {
        for (float depth : { 0.87f, 0.9f, 0.93f })
        {
            const float s = volumeShareTarget(false, depth);
            EXPECT_GT(s, 0.f) << depth;
            EXPECT_LT(s, 1.f) << depth;
        }
        float previous = volumeShareTarget(false, 0.8f);
        for (int i = 80; i <= 100; ++i)
        {
            const float s = volumeShareTarget(false, i / 100.f);
            EXPECT_LE(s, previous + tol) << i;
            previous = s;
        }
    }

    TEST(RenderCloudVolume, easeShareReachesTheTargetAfterShareSeconds)
    {
        EXPECT_NEAR(easeShare(0.f, 1.f, shareSeconds), 1.f, tol);
        EXPECT_NEAR(easeShare(1.f, 0.f, shareSeconds), 0.f, tol);
        EXPECT_NEAR(easeShare(0.f, 1.f, 10.f * shareSeconds), 1.f, tol);
    }

    TEST(RenderCloudVolume, easeShareMovesTowardTheTargetWithoutOvershoot)
    {
        for (float seconds : { 0.f, 0.1f, 0.5f, 1.f, 2.9f, 3.f, 7.f, 100.f })
        {
            const float up = easeShare(0.2f, 0.9f, seconds);
            EXPECT_GE(up, 0.2f - tol) << seconds;
            EXPECT_LE(up, 0.9f + tol) << seconds;
            const float down = easeShare(0.9f, 0.2f, seconds);
            EXPECT_LE(down, 0.9f + tol) << seconds;
            EXPECT_GE(down, 0.2f - tol) << seconds;
        }
        EXPECT_GT(easeShare(0.f, 1.f, 1.f), 0.f);
        EXPECT_LT(easeShare(0.f, 1.f, 1.f), 1.f);
        EXPECT_NEAR(easeShare(0.4f, 0.4f, 1.f), 0.4f, tol);
    }

    // --- Where the noise is read -----------------------------------------------------------------------------------

    TEST(RenderCloudVolume, wrapCoordinateIsTheFractionalPart)
    {
        EXPECT_NEAR(wrapCoordinate(0.25f), 0.25f, tol);
        EXPECT_NEAR(wrapCoordinate(1.25f), 0.25f, tol);
        EXPECT_NEAR(wrapCoordinate(-0.25f), 0.75f, tol);
        EXPECT_NEAR(wrapCoordinate(-3.75f), 0.25f, tol);
        EXPECT_NEAR(wrapCoordinate(7.f), 0.f, tol);
    }

    TEST(RenderCloudVolume, wrapCoordinateStaysBelowOne)
    {
        for (float v : { -1e-9f, -1e-7f, -0.0f, 0.f, 0.999999f, 1.f, -1.f, 12345.678f, -98765.4f })
        {
            const float w = wrapCoordinate(v);
            EXPECT_GE(w, 0.f) << v;
            EXPECT_LT(w, 1.f) << v;
        }
    }

    TEST(RenderCloudVolume, wrapCoordinateIsPeriodic)
    {
        for (float v : { 0.1f, 0.5f, 0.77f })
            for (float k : { -3.f, 1.f, 5.f })
                EXPECT_NEAR(wrapCoordinate(v + k), v, 1e-4f);
    }

    TEST(RenderCloudVolume, noiseCoordinateWrapsPositionPlusWindOverTheTile)
    {
        EXPECT_NEAR(noiseCoordinate(3000.f, 0.f, shapeTileMeters), 0.25f, tol);
        EXPECT_NEAR(noiseCoordinate(3000.f, 3000.f, shapeTileMeters), 0.5f, tol);
        EXPECT_NEAR(noiseCoordinate(8.f * detailTileMeters, 0.f, detailTileMeters), 0.f, tol);
        EXPECT_NEAR(noiseCoordinate(-3000.f, 0.f, shapeTileMeters), 0.75f, tol);
        // One whole tile away is the same place.
        EXPECT_NEAR(noiseCoordinate(1000.f + shapeTileMeters, 0.f, shapeTileMeters),
            noiseCoordinate(1000.f, 0.f, shapeTileMeters), 1e-4f);
    }

    // --- Hashes ----------------------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, hashUintIsDeterministicAndSpreads)
    {
        std::set<std::uint32_t> seen;
        for (std::uint32_t v = 0; v < 256; ++v)
        {
            EXPECT_EQ(hashUint(v), hashUint(v));
            seen.insert(hashUint(v));
        }
        EXPECT_GE(seen.size(), 250u);
        EXPECT_NE(hashUint(1), 1u);
    }

    TEST(RenderCloudVolume, hashCellIsDeterministic)
    {
        EXPECT_EQ(hashCell(3, 5, 7, 8, noiseSeed), hashCell(3, 5, 7, 8, noiseSeed));
        EXPECT_EQ(hashCell(-2, 0, 9, 4, 17u), hashCell(-2, 0, 9, 4, 17u));
    }

    TEST(RenderCloudVolume, hashCellTilesOnEveryAxis)
    {
        const int period = 8;
        for (int x = -3; x < 11; ++x)
        {
            EXPECT_EQ(hashCell(x + period, 2, 3, period, noiseSeed), hashCell(x, 2, 3, period, noiseSeed)) << x;
            EXPECT_EQ(hashCell(1, x + period, 3, period, noiseSeed), hashCell(1, x, 3, period, noiseSeed)) << x;
            EXPECT_EQ(hashCell(1, 2, x + period, period, noiseSeed), hashCell(1, 2, x, period, noiseSeed)) << x;
            EXPECT_EQ(hashCell(x - 2 * period, 2, 3, period, noiseSeed), hashCell(x, 2, 3, period, noiseSeed)) << x;
        }
    }

    TEST(RenderCloudVolume, hashCellWrapsNegativeCoordinates)
    {
        const int period = 4;
        EXPECT_EQ(
            hashCell(-1, -1, -1, period, noiseSeed), hashCell(period - 1, period - 1, period - 1, period, noiseSeed));
        EXPECT_EQ(hashCell(-4, 0, 0, period, noiseSeed), hashCell(0, 0, 0, period, noiseSeed));
    }

    TEST(RenderCloudVolume, hashCellDependsOnTheSeedAndTheCell)
    {
        int seedDiffers = 0, cellDiffers = 0;
        for (int i = 0; i < 16; ++i)
        {
            if (hashCell(i, 1, 2, 16, 1u) != hashCell(i, 1, 2, 16, 2u))
                ++seedDiffers;
            if (hashCell(i, 1, 2, 16, 1u) != hashCell(i + 1, 1, 2, 16, 1u))
                ++cellDiffers;
        }
        EXPECT_GE(seedDiffers, 14);
        EXPECT_GE(cellDiffers, 14);
    }

    TEST(RenderCloudVolume, hashToUnitUsesTheTopTwentyFourBits)
    {
        EXPECT_FLOAT_EQ(hashToUnit(0u), 0.f);
        EXPECT_FLOAT_EQ(hashToUnit(0x80000000u), 0.5f);
        EXPECT_FLOAT_EQ(hashToUnit(0xFFFFFFFFu), 16777215.f / 16777216.f);
        EXPECT_LT(hashToUnit(0xFFFFFFFFu), 1.f);
        // The low eight bits are dropped.
        EXPECT_EQ(hashToUnit(0x123456FFu), hashToUnit(0x12345600u));
        EXPECT_FLOAT_EQ(hashToUnit(0x12345600u), static_cast<float>(0x123456u) / 16777216.f);
    }

    TEST(RenderCloudVolume, hashToUnitStaysInRangeOverManyHashes)
    {
        for (std::uint32_t v = 0; v < 4096; ++v)
        {
            const float u = hashToUnit(hashUint(v));
            EXPECT_GE(u, 0.f);
            EXPECT_LT(u, 1.f);
        }
    }

    // --- Noise -----------------------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, worleyIsDeterministicAndInRange)
    {
        float lowest = 1.f, highest = 0.f;
        for (int i = 0; i < 400; ++i)
        {
            const float x = 0.37f * i, y = 0.21f * i + 0.5f, z = 0.13f * i + 1.1f;
            const float v = worley(x, y, z, 8, noiseSeed);
            EXPECT_GE(v, 0.f);
            EXPECT_LE(v, 1.f);
            EXPECT_EQ(v, worley(x, y, z, 8, noiseSeed));
            lowest = std::min(lowest, v);
            highest = std::max(highest, v);
        }
        // The noise actually varies.
        EXPECT_GT(highest - lowest, 0.2f);
    }

    TEST(RenderCloudVolume, worleyTiles)
    {
        for (int period : { 2, 4, 8 })
            for (int i = 0; i < 40; ++i)
            {
                const float x = 0.31f * i, y = 0.17f * i + 0.3f, z = 0.23f * i + 0.7f;
                const float p = static_cast<float>(period);
                const float v = worley(x, y, z, period, noiseSeed);
                EXPECT_NEAR(worley(x + p, y, z, period, noiseSeed), v, 1e-4f);
                EXPECT_NEAR(worley(x, y + p, z, period, noiseSeed), v, 1e-4f);
                EXPECT_NEAR(worley(x, y, z + p, period, noiseSeed), v, 1e-4f);
            }
    }

    TEST(RenderCloudVolume, worleyDiffersBetweenSeeds)
    {
        bool differs = false;
        for (int i = 0; i < 50 && !differs; ++i)
        {
            const float x = 0.41f * i, y = 0.29f * i, z = 0.11f * i;
            differs = worley(x, y, z, 8, 1u) != worley(x, y, z, 8, 2u);
        }
        EXPECT_TRUE(differs);
    }

    TEST(RenderCloudVolume, perlinIsDeterministicAndInRange)
    {
        float lowest = 1.f, highest = 0.f;
        for (int i = 0; i < 400; ++i)
        {
            const float x = 0.37f * i + 0.2f, y = 0.21f * i + 0.5f, z = 0.13f * i + 1.1f;
            const float v = perlin(x, y, z, 8, noiseSeed);
            EXPECT_GE(v, 0.f);
            EXPECT_LE(v, 1.f);
            EXPECT_EQ(v, perlin(x, y, z, 8, noiseSeed));
            lowest = std::min(lowest, v);
            highest = std::max(highest, v);
        }
        EXPECT_GT(highest - lowest, 0.2f);
    }

    TEST(RenderCloudVolume, perlinTiles)
    {
        for (int period : { 2, 4, 8 })
            for (int i = 0; i < 40; ++i)
            {
                const float x = 0.31f * i + 0.15f, y = 0.17f * i + 0.3f, z = 0.23f * i + 0.7f;
                const float p = static_cast<float>(period);
                const float v = perlin(x, y, z, period, noiseSeed);
                EXPECT_NEAR(perlin(x + p, y, z, period, noiseSeed), v, 1e-4f);
                EXPECT_NEAR(perlin(x, y + p, z, period, noiseSeed), v, 1e-4f);
                EXPECT_NEAR(perlin(x, y, z + p, period, noiseSeed), v, 1e-4f);
            }
    }

    TEST(RenderCloudVolume, perlinDiffersBetweenSeeds)
    {
        bool differs = false;
        for (int i = 0; i < 50 && !differs; ++i)
        {
            const float x = 0.41f * i + 0.3f, y = 0.29f * i + 0.6f, z = 0.11f * i + 0.2f;
            differs = perlin(x, y, z, 8, 1u) != perlin(x, y, z, 8, 2u);
        }
        EXPECT_TRUE(differs);
    }

    TEST(RenderCloudVolume, perlinFbmIsInRangeDeterministicAndTiles)
    {
        for (int octaves : { 1, 3, 5 })
            for (int i = 0; i < 40; ++i)
            {
                const float x = 0.27f * i + 0.1f, y = 0.19f * i + 0.4f, z = 0.31f * i + 0.8f;
                const float v = perlinFbm(x, y, z, 4, noiseSeed, octaves);
                EXPECT_GE(v, 0.f);
                EXPECT_LE(v, 1.f);
                EXPECT_EQ(v, perlinFbm(x, y, z, 4, noiseSeed, octaves));
                EXPECT_NEAR(perlinFbm(x + 4.f, y, z, 4, noiseSeed, octaves), v, 1e-4f);
                EXPECT_NEAR(perlinFbm(x, y + 4.f, z, 4, noiseSeed, octaves), v, 1e-4f);
                EXPECT_NEAR(perlinFbm(x, y, z + 4.f, 4, noiseSeed, octaves), v, 1e-4f);
            }
    }

    TEST(RenderCloudVolume, perlinWorleyIsTheDocumentedRemap)
    {
        for (float p : { 0.f, 0.3f, 0.6f, 0.9f, 1.f })
            for (float w : { 0.f, 0.2f, 0.5f, 0.8f })
                EXPECT_NEAR(perlinWorley(p, w), refSaturate(refRemap(p, w, 1.f, 0.f, 1.f)), tol) << p << " " << w;
        EXPECT_NEAR(perlinWorley(0.2f, 0.5f), 0.f, tol);
        EXPECT_NEAR(perlinWorley(1.f, 0.5f), 1.f, tol);
    }

    TEST(RenderCloudVolume, worleyFbmWeightsThreeFrequencies)
    {
        EXPECT_NEAR(worleyFbm(1.f, 0.f, 0.f), 0.625f, tol);
        EXPECT_NEAR(worleyFbm(0.f, 1.f, 0.f), 0.25f, tol);
        EXPECT_NEAR(worleyFbm(0.f, 0.f, 1.f), 0.125f, tol);
        EXPECT_NEAR(worleyFbm(1.f, 1.f, 1.f), 1.f, tol);
        EXPECT_NEAR(worleyFbm(0.4f, 0.6f, 0.8f), 0.625f * 0.4f + 0.25f * 0.6f + 0.125f * 0.8f, tol);
    }

    TEST(RenderCloudVolume, noisePeriodsAreTheDocumentedFrequencies)
    {
        EXPECT_EQ(shapePerlinPeriod, 4);
        EXPECT_EQ(shapeWorleyPeriods[0], 4);
        EXPECT_EQ(shapeWorleyPeriods[1], 8);
        EXPECT_EQ(shapeWorleyPeriods[2], 16);
        EXPECT_EQ(detailWorleyPeriods[0], 2);
        EXPECT_EQ(detailWorleyPeriods[1], 4);
        EXPECT_EQ(detailWorleyPeriods[2], 8);
        EXPECT_EQ(curlPeriod, 8);
    }

    // --- Texels ----------------------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, shapeTexelChannelsAreInRangeAndDeterministic)
    {
        const std::uint32_t size = 16;
        for (std::uint32_t z = 0; z < size; z += 5)
            for (std::uint32_t y = 0; y < size; y += 3)
                for (std::uint32_t x = 0; x < size; x += 3)
                {
                    const auto t = shapeTexel(x, y, z, size);
                    for (float c : t)
                    {
                        EXPECT_GE(c, 0.f);
                        EXPECT_LE(c, 1.f);
                    }
                    EXPECT_EQ(t, shapeTexel(x, y, z, size));
                }
    }

    TEST(RenderCloudVolume, detailTexelChannelsAreInRangeAndDeterministic)
    {
        const std::uint32_t size = 16;
        for (std::uint32_t z = 0; z < size; z += 5)
            for (std::uint32_t y = 0; y < size; y += 3)
                for (std::uint32_t x = 0; x < size; x += 3)
                {
                    const auto t = detailTexel(x, y, z, size);
                    for (float c : t)
                    {
                        EXPECT_GE(c, 0.f);
                        EXPECT_LE(c, 1.f);
                    }
                    EXPECT_EQ(t, detailTexel(x, y, z, size));
                }
    }

    TEST(RenderCloudVolume, shapeTexelsDifferFromOneAnother)
    {
        std::set<std::array<float, 4>> seen;
        for (std::uint32_t i = 0; i < 24; ++i)
            seen.insert(shapeTexel(i, (i * 5) % 32, (i * 7) % 32, 32));
        EXPECT_GT(seen.size(), 12u);
        // Each channel varies over the sample.
        for (int channel = 0; channel < 4; ++channel)
        {
            float lowest = 1.f, highest = 0.f;
            for (const auto& t : seen)
            {
                lowest = std::min(lowest, t[channel]);
                highest = std::max(highest, t[channel]);
            }
            EXPECT_GT(highest - lowest, 0.01f) << channel;
        }
    }

    TEST(RenderCloudVolume, detailTexelsDifferFromOneAnother)
    {
        std::set<std::array<float, 4>> seen;
        for (std::uint32_t i = 0; i < 24; ++i)
            seen.insert(detailTexel(i, (i * 5) % 32, (i * 7) % 32, 32));
        EXPECT_GT(seen.size(), 12u);
    }

    TEST(RenderCloudVolume, detailTexelAlphaIsTheWorleyFbmOfItsOtherChannels)
    {
        for (std::uint32_t i = 0; i < 32; ++i)
        {
            const auto t = detailTexel(i, (i * 3) % 32, (i * 11) % 32, 32);
            EXPECT_EQ(t[3], worleyFbm(t[0], t[1], t[2])) << i;
        }
    }

    // --- Robustness ------------------------------------------------------------------------------------------------

    TEST(RenderCloudVolume, wrapCoordinateOfNonFiniteInputIsZeroOrInRange)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        for (float v : { nan, inf, -inf })
        {
            const float w = wrapCoordinate(v);
            EXPECT_TRUE(std::isfinite(w));
            EXPECT_GE(w, 0.f);
            EXPECT_LT(w, 1.f);
        }
    }

    TEST(RenderCloudVolume, layerSpanWithNaNIsAMiss)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(layerSpan(nan, 1.f, 1500.f, 4000.f, 1e6f).hit);
        EXPECT_FALSE(layerSpan(0.f, nan, 1500.f, 4000.f, 1e6f).hit);
        EXPECT_FALSE(layerSpan(2000.f, nan, 1500.f, 4000.f, 1e6f).hit);
        EXPECT_FALSE(layerSpan(0.f, 1.f, nan, 4000.f, 1e6f).hit);
        EXPECT_FALSE(layerSpan(0.f, 1.f, 1500.f, nan, 1e6f).hit);
        EXPECT_FALSE(layerSpan(0.f, 1.f, 1500.f, 4000.f, nan).hit);
        EXPECT_FALSE(layerSpan(2000.f, 0.5f, 1500.f, 4000.f, nan).hit);
    }

    TEST(RenderCloudVolume, beerPowderPeaksNearHalfAnOpticalDepth)
    {
        // d/dd [2 e^-d (1 - e^-2d)] = 0 at e^-2d = 1/3, d = ln(3) / 2 = 0.5493, where the value is 4 / (3 sqrt(3)) =
        // 0.7698.
        EXPECT_NEAR(beerPowder(0.5493f), 0.7698f, 2e-3f);
        for (float d : { 0.2f, 0.4f, 0.7f, 1.0f, 2.0f })
            EXPECT_LT(beerPowder(d), beerPowder(0.5493f)) << d;
    }

    TEST(RenderCloudVolume, multiScatterLastOctaveHasAnAlmostIsotropicPhase)
    {
        // The anisotropies shrink by c each octave: by the eighth the lobes are 0.8 / 128 and -0.3 / 128 wide.
        const float last = forwardAnisotropy * std::pow(multiScatterPhase, 7.f);
        const float lastBack = backAnisotropy * std::pow(multiScatterPhase, 7.f);
        for (float c : { -1.f, 0.f, 1.f })
            EXPECT_NEAR(dualLobePhase(c, last, lastBack), 1.f / (4.f * pi), 1e-3f) << c;
    }

    // --- Regression values ----------------------------------------------------------------------------------------
    // These numbers were read from the implementation (task 0042) and are not derived independently. A Slang port of
    // the noise must reproduce them: the hashes bit for bit, the floats within 1e-6.

    TEST(RenderCloudVolume, regressionHashUint)
    {
        EXPECT_EQ(hashUint(0u), 129708002u);
        EXPECT_EQ(hashUint(1u), 2831084092u);
        EXPECT_EQ(hashUint(2u), 2055130248u);
        EXPECT_EQ(hashUint(12345u), 4099845390u);
        EXPECT_EQ(hashUint(0xFFFFFFFFu), 3861530882u);
    }

    TEST(RenderCloudVolume, regressionHashCell)
    {
        EXPECT_EQ(hashCell(0, 0, 0, 8, noiseSeed), 1344052252u);
        EXPECT_EQ(hashCell(1, 2, 3, 8, noiseSeed), 1725770672u);
        EXPECT_EQ(hashCell(-1, -2, -3, 8, noiseSeed), 1966806653u);
        // (9, 10, 11) wraps to (1, 2, 3).
        EXPECT_EQ(hashCell(9, 10, 11, 8, noiseSeed), 1725770672u);
        EXPECT_EQ(hashCell(5, -7, 12, 4, 1u), 831221656u);
    }

    TEST(RenderCloudVolume, regressionWorley)
    {
        EXPECT_NEAR(worley(0.f, 0.f, 0.f, 8, noiseSeed), 0.447809935f, 1e-6f);
        EXPECT_NEAR(worley(0.5f, 1.25f, 2.75f, 8, noiseSeed), 0.227275491f, 1e-6f);
        EXPECT_NEAR(worley(3.3f, 0.1f, 7.9f, 8, noiseSeed), 0.613951564f, 1e-6f);
        EXPECT_NEAR(worley(5.5f, 6.5f, 7.5f, 8, noiseSeed), 0.27806145f, 1e-6f);
    }

    TEST(RenderCloudVolume, regressionPerlin)
    {
        EXPECT_NEAR(perlin(0.f, 0.f, 0.f, 8, noiseSeed), 0.5f, 1e-6f);
        EXPECT_NEAR(perlin(0.5f, 1.25f, 2.75f, 8, noiseSeed), 0.470999956f, 1e-6f);
        EXPECT_NEAR(perlin(3.3f, 0.1f, 7.9f, 8, noiseSeed), 0.427818298f, 1e-6f);
        EXPECT_NEAR(perlin(5.5f, 6.5f, 7.5f, 8, noiseSeed), 0.375f, 1e-6f);
    }

    TEST(RenderCloudVolume, regressionTexels)
    {
        struct Case
        {
            std::uint32_t x, y, z;
            std::array<float, 4> shape, detail;
        };
        const Case cases[] = {
            { 0, 0, 0, { 0.418891102f, 0.135702908f, 0.309262455f, 0.824733496f },
                { 0.377556384f, 0.746767759f, 0.188356936f, 0.446209282f } },
            { 5, 9, 17, { 0.f, 0.600968003f, 0.451392114f, 0.856496572f },
                { 0.297549963f, 0.658313036f, 0.421304226f, 0.403210014f } },
            { 31, 30, 1, { 0.368224859f, 0.228132486f, 0.0357053876f, 0.342244565f },
                { 0.417832613f, 0.62495625f, 0.362959981f, 0.462754428f } },
        };
        for (const Case& c : cases)
        {
            const auto s = shapeTexel(c.x, c.y, c.z, 32);
            const auto d = detailTexel(c.x, c.y, c.z, 32);
            for (int i = 0; i < 4; ++i)
            {
                EXPECT_NEAR(s[i], c.shape[i], 1e-6f) << "shape " << c.x << " " << c.y << " " << c.z << " ch " << i;
                EXPECT_NEAR(d[i], c.detail[i], 1e-6f) << "detail " << c.x << " " << c.y << " " << c.z << " ch " << i;
            }
        }
    }
}
