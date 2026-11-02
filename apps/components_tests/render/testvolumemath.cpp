// Tests for the volumetric fog maths, Render::Volume. Written from the contract before the implementation: the froxel
// grid sizes, the exponential slice layout, the Henyey-Greenstein phase function, the analytic slice integration,
// the engine-matching fog extinction, the jitter sequence and the small switches. Pure CPU maths: no device, window or
// game data.

#include <components/render/volumemath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace
{
    using namespace Render::Volume;

    constexpr float pi = 3.14159265358979323846f;

    bool finite(const std::array<float, 3>& v)
    {
        return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
    }

    // Integrates uniform fog with extinction sigma and in-scatter sigma * colour over [nearD, farD] in `slices`
    // exponential slices.
    Accum integrateUniform(float sigma, const std::array<float, 3>& colour, uint32_t slices, float nearD, float farD)
    {
        Accum a;
        const std::array<float, 3> scatter{ sigma * colour[0], sigma * colour[1], sigma * colour[2] };
        for (uint32_t i = 0; i < slices; ++i)
        {
            const float d0 = sliceDepth(static_cast<float>(i), slices, nearD, farD);
            const float d1 = sliceDepth(static_cast<float>(i + 1), slices, nearD, farD);
            integrateSlice(a, scatter, sigma, d1 - d0);
        }
        return a;
    }

    TEST(RenderVolume, qualityParsesItsThreeNames)
    {
        ASSERT_TRUE(parseQuality("low").has_value());
        ASSERT_TRUE(parseQuality("medium").has_value());
        ASSERT_TRUE(parseQuality("high").has_value());
        EXPECT_EQ(*parseQuality("low"), Quality::Low);
        EXPECT_EQ(*parseQuality("medium"), Quality::Medium);
        EXPECT_EQ(*parseQuality("high"), Quality::High);
    }

    TEST(RenderVolume, qualityRejectsOtherNames)
    {
        EXPECT_FALSE(parseQuality("").has_value());
        EXPECT_FALSE(parseQuality("ultra").has_value());
        EXPECT_FALSE(parseQuality("Low").has_value());
        EXPECT_FALSE(parseQuality("medium ").has_value());
    }

    TEST(RenderVolume, gridSizesMatchTheTiers)
    {
        const GridSize low = gridSize(Quality::Low);
        EXPECT_EQ(low.x, 80u);
        EXPECT_EQ(low.y, 45u);
        EXPECT_EQ(low.z, 32u);
        const GridSize medium = gridSize(Quality::Medium);
        EXPECT_EQ(medium.x, 160u);
        EXPECT_EQ(medium.y, 90u);
        EXPECT_EQ(medium.z, 64u);
        const GridSize high = gridSize(Quality::High);
        EXPECT_EQ(high.x, 240u);
        EXPECT_EQ(high.y, 135u);
        EXPECT_EQ(high.z, 96u);
    }

    TEST(RenderVolume, farDistanceClampsToTheRange)
    {
        EXPECT_FLOAT_EQ(nearDistance, 8.f);
        EXPECT_FLOAT_EQ(maxRange, 8192.f);
        EXPECT_FLOAT_EQ(farDistance(100.f), 256.f);
        EXPECT_FLOAT_EQ(farDistance(0.f), 256.f);
        EXPECT_FLOAT_EQ(farDistance(-50.f), 256.f);
        EXPECT_FLOAT_EQ(farDistance(5000.f), 5000.f);
        EXPECT_FLOAT_EQ(farDistance(1e6f), maxRange);
        EXPECT_FLOAT_EQ(farDistance(maxRange), maxRange);
    }

    TEST(RenderVolume, sliceDepthEndpoints)
    {
        EXPECT_NEAR(sliceDepth(0.f, 64, 8.f, 4000.f), 8.f, 1e-4f);
        EXPECT_NEAR(sliceDepth(64.f, 64, 8.f, 4000.f), 4000.f, 4000.f * 1e-4f);
    }

    TEST(RenderVolume, sliceDepthIsGeometric)
    {
        // Halfway through the slices is the geometric mean of near and far.
        EXPECT_NEAR(sliceDepth(32.f, 64, 8.f, 2048.f), 128.f, 128.f * 1e-4f);
    }

    TEST(RenderVolume, sliceDepthIsMonotonic)
    {
        float previous = sliceDepth(0.f, 64, 8.f, 4000.f);
        for (uint32_t i = 1; i <= 64; ++i)
        {
            const float depth = sliceDepth(static_cast<float>(i), 64, 8.f, 4000.f);
            EXPECT_GT(depth, previous);
            previous = depth;
        }
    }

    TEST(RenderVolume, slicesAreThinnerNearTheCamera)
    {
        const float first = sliceDepth(1.f, 64, 8.f, 4000.f) - sliceDepth(0.f, 64, 8.f, 4000.f);
        const float last = sliceDepth(64.f, 64, 8.f, 4000.f) - sliceDepth(63.f, 64, 8.f, 4000.f);
        EXPECT_LT(first, last);
    }

    TEST(RenderVolume, sliceCoordinateEndpointsAndClamping)
    {
        EXPECT_NEAR(sliceCoordinate(8.f, 64, 8.f, 4000.f), 0.f, 1e-3f);
        EXPECT_NEAR(sliceCoordinate(4000.f, 64, 8.f, 4000.f), 64.f, 1e-3f);
        EXPECT_FLOAT_EQ(sliceCoordinate(1.f, 64, 8.f, 4000.f), 0.f);
        EXPECT_FLOAT_EQ(sliceCoordinate(1e7f, 64, 8.f, 4000.f), 64.f);
    }

    TEST(RenderVolume, sliceCoordinateInvertsSliceDepth)
    {
        for (uint32_t slices : { 32u, 64u, 96u })
        {
            for (float s = 0.f; s <= static_cast<float>(slices); s += 0.37f)
            {
                const float depth = sliceDepth(s, slices, 8.f, 4000.f);
                const float back = sliceCoordinate(depth, slices, 8.f, 4000.f);
                EXPECT_NEAR(back, s, std::max(1e-3f * s, 1e-3f)) << "slices " << slices << " s " << s;
            }
        }
    }

    TEST(RenderVolume, sliceDepthInvertsSliceCoordinate)
    {
        for (float depth = 9.f; depth < 4000.f; depth *= 1.3f)
        {
            const float s = sliceCoordinate(depth, 64, 8.f, 4000.f);
            const float back = sliceDepth(s, 64, 8.f, 4000.f);
            EXPECT_NEAR(back, depth, depth * 1e-3f);
        }
    }

    TEST(RenderVolume, sliceCoordinateIsMonotonic)
    {
        float previous = sliceCoordinate(8.f, 64, 8.f, 4000.f);
        for (float depth = 9.f; depth < 4000.f; depth *= 1.1f)
        {
            const float s = sliceCoordinate(depth, 64, 8.f, 4000.f);
            EXPECT_GE(s, previous);
            previous = s;
        }
    }

    TEST(RenderVolume, phaseIsIsotropicWithoutAnisotropy)
    {
        const float expected = 1.f / (4.f * pi);
        for (float c = -1.f; c <= 1.f; c += 0.25f)
            EXPECT_NEAR(henyeyGreenstein(c, 0.f), expected, 1e-6f);
    }

    TEST(RenderVolume, phaseIntegratesToOneOverTheSphere)
    {
        for (float g : { 0.f, 0.5f, -0.5f, 0.7f })
        {
            // Midpoint rule over cosTheta in [-1, 1]; the integral over the sphere is 2 pi times it.
            constexpr int steps = 20000;
            double sum = 0.0;
            for (int i = 0; i < steps; ++i)
            {
                const float c = -1.f + (static_cast<float>(i) + 0.5f) * (2.f / steps);
                sum += henyeyGreenstein(c, g);
            }
            const double integral = 2.0 * 3.14159265358979323846 * sum * (2.0 / steps);
            EXPECT_NEAR(integral, 1.0, 1e-3) << "g " << g;
        }
    }

    TEST(RenderVolume, phaseFavoursTheForwardDirectionForPositiveG)
    {
        EXPECT_GT(henyeyGreenstein(1.f, 0.6f), henyeyGreenstein(-1.f, 0.6f));
    }

    TEST(RenderVolume, phaseFavoursTheBackwardDirectionForNegativeG)
    {
        EXPECT_LT(henyeyGreenstein(1.f, -0.6f), henyeyGreenstein(-1.f, -0.6f));
    }

    TEST(RenderVolume, phaseIsSymmetricUnderFlippingBoth)
    {
        for (float g : { 0.f, 0.3f, 0.7f, 0.95f })
            for (float c = -1.f; c <= 1.f; c += 0.2f)
                EXPECT_NEAR(henyeyGreenstein(c, g), henyeyGreenstein(-c, -g), 1e-5f * henyeyGreenstein(c, g) + 1e-7f);
    }

    TEST(RenderVolume, phaseStaysFiniteAndPositiveAtTheExtremes)
    {
        for (float g : { -5.f, -1.f, -0.99f, 0.99f, 1.f, 5.f })
            for (float c : { -3.f, -1.f, 0.f, 1.f, 3.f })
            {
                const float value = henyeyGreenstein(c, g);
                EXPECT_TRUE(std::isfinite(value)) << "g " << g << " c " << c;
                EXPECT_GT(value, 0.f);
            }
    }

    TEST(RenderVolume, phaseClampsAnisotropyAndCosine)
    {
        EXPECT_FLOAT_EQ(henyeyGreenstein(0.3f, 2.f), henyeyGreenstein(0.3f, 0.99f));
        EXPECT_FLOAT_EQ(henyeyGreenstein(0.3f, -2.f), henyeyGreenstein(0.3f, -0.99f));
        EXPECT_FLOAT_EQ(henyeyGreenstein(2.f, 0.5f), henyeyGreenstein(1.f, 0.5f));
        EXPECT_FLOAT_EQ(henyeyGreenstein(-2.f, 0.5f), henyeyGreenstein(-1.f, 0.5f));
    }

    TEST(RenderVolume, transmittanceIsBeerLambert)
    {
        EXPECT_FLOAT_EQ(transmittance(0.5f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(transmittance(0.f, 100.f), 1.f);
        EXPECT_NEAR(transmittance(0.01f, 100.f), std::exp(-1.f), 1e-6f);
        EXPECT_NEAR(transmittance(2.f, 3.f), std::exp(-6.f), 1e-7f);
    }

    TEST(RenderVolume, transmittanceClampsNegativeInputs)
    {
        EXPECT_FLOAT_EQ(transmittance(-1.f, 10.f), 1.f);
        EXPECT_FLOAT_EQ(transmittance(1.f, -10.f), 1.f);
        EXPECT_FLOAT_EQ(transmittance(-1.f, -10.f), 1.f);
    }

    TEST(RenderVolume, transmittanceDecreasesWithDistance)
    {
        float previous = 1.f;
        for (float d = 1.f; d < 500.f; d += 7.f)
        {
            const float t = transmittance(0.01f, d);
            EXPECT_LE(t, previous);
            EXPECT_GT(t, 0.f);
            previous = t;
        }
    }

    TEST(RenderVolume, accumStartsClear)
    {
        const Accum a;
        EXPECT_FLOAT_EQ(a.scattered[0], 0.f);
        EXPECT_FLOAT_EQ(a.scattered[1], 0.f);
        EXPECT_FLOAT_EQ(a.scattered[2], 0.f);
        EXPECT_FLOAT_EQ(a.transmittance, 1.f);
    }

    TEST(RenderVolume, uniformFogMatchesTheClosedForm)
    {
        const float sigma = 0.002f;
        const std::array<float, 3> colour{ 0.8f, 0.6f, 0.3f };
        const float total = 4000.f - 8.f;
        const Accum a = integrateUniform(sigma, colour, 64, 8.f, 4000.f);
        const float expectedT = std::exp(-sigma * total);
        EXPECT_NEAR(a.transmittance, expectedT, expectedT * 1e-5f);
        for (int i = 0; i < 3; ++i)
        {
            const float expected = colour[i] * (1.f - expectedT);
            EXPECT_NEAR(a.scattered[i], expected, expected * 1e-5f) << "channel " << i;
        }
    }

    TEST(RenderVolume, splittingTheRangeDoesNotChangeTheAnswer)
    {
        const float sigma = 0.0015f;
        const std::array<float, 3> colour{ 0.9f, 0.5f, 0.2f };
        const Accum one = integrateUniform(sigma, colour, 1, 8.f, 4000.f);
        const Accum many = integrateUniform(sigma, colour, 64, 8.f, 4000.f);
        EXPECT_NEAR(one.transmittance, many.transmittance, one.transmittance * 1e-5f);
        for (int i = 0; i < 3; ++i)
            EXPECT_NEAR(one.scattered[i], many.scattered[i], one.scattered[i] * 1e-5f) << "channel " << i;
    }

    TEST(RenderVolume, zeroExtinctionAddsScatterTimesLength)
    {
        Accum a;
        const std::array<float, 3> scatter{ 0.1f, 0.2f, 0.3f };
        integrateSlice(a, scatter, 0.f, 10.f);
        integrateSlice(a, scatter, 0.f, 5.f);
        EXPECT_NEAR(a.scattered[0], 1.5f, 1e-5f);
        EXPECT_NEAR(a.scattered[1], 3.0f, 1e-5f);
        EXPECT_NEAR(a.scattered[2], 4.5f, 1e-5f);
        EXPECT_FLOAT_EQ(a.transmittance, 1.f);
    }

    TEST(RenderVolume, tinyExtinctionHasNoNaN)
    {
        Accum a;
        const std::array<float, 3> scatter{ 0.1f, 0.2f, 0.3f };
        for (int i = 0; i < 10; ++i)
            integrateSlice(a, scatter, 1e-9f, 100.f);
        EXPECT_TRUE(finite(a.scattered));
        EXPECT_TRUE(std::isfinite(a.transmittance));
        EXPECT_NEAR(a.transmittance, 1.f, 1e-5f);
        EXPECT_NEAR(a.scattered[0], 100.f, 1e-2f);
        EXPECT_NEAR(a.scattered[2], 300.f, 1e-2f);
    }

    TEST(RenderVolume, zeroThicknessSliceChangesNothing)
    {
        Accum a;
        integrateSlice(a, { 1.f, 1.f, 1.f }, 0.5f, 0.f);
        EXPECT_TRUE(finite(a.scattered));
        EXPECT_FLOAT_EQ(a.scattered[0], 0.f);
        EXPECT_FLOAT_EQ(a.transmittance, 1.f);
    }

    TEST(RenderVolume, scatteredNeverExceedsTheFogColour)
    {
        const std::array<float, 3> colour{ 0.7f, 0.4f, 0.9f };
        for (float sigma : { 1e-6f, 1e-4f, 0.01f, 1.f, 100.f })
        {
            const Accum a = integrateUniform(sigma, colour, 64, 8.f, 4000.f);
            for (int i = 0; i < 3; ++i)
            {
                EXPECT_LE(a.scattered[i], colour[i] * (1.f + 1e-5f)) << "sigma " << sigma;
                EXPECT_GE(a.scattered[i], 0.f);
            }
            EXPECT_TRUE(finite(a.scattered));
            EXPECT_GE(a.transmittance, 0.f);
            EXPECT_LE(a.transmittance, 1.f);
        }
    }

    TEST(RenderVolume, transmittanceNeverIncreasesAcrossSlices)
    {
        Accum a;
        float previous = a.transmittance;
        for (uint32_t i = 0; i < 64; ++i)
        {
            const float d0 = sliceDepth(static_cast<float>(i), 64, 8.f, 4000.f);
            const float d1 = sliceDepth(static_cast<float>(i + 1), 64, 8.f, 4000.f);
            integrateSlice(a, { 0.1f, 0.1f, 0.1f }, 0.003f, d1 - d0);
            EXPECT_LE(a.transmittance, previous);
            previous = a.transmittance;
        }
    }

    TEST(RenderVolume, scatteredNeverDecreasesAcrossSlices)
    {
        Accum a;
        float previous = 0.f;
        for (uint32_t i = 0; i < 64; ++i)
        {
            const float d0 = sliceDepth(static_cast<float>(i), 64, 8.f, 4000.f);
            const float d1 = sliceDepth(static_cast<float>(i + 1), 64, 8.f, 4000.f);
            integrateSlice(a, { 0.1f, 0.2f, 0.3f }, 0.003f, d1 - d0);
            EXPECT_GE(a.scattered[1], previous);
            previous = a.scattered[1];
        }
    }

    TEST(RenderVolume, fogExtinctionMatchesTheEnginesExponentialFog)
    {
        EXPECT_NEAR(fogExtinction(0.f, 7168.f), 2.f / 7168.f, 1e-9f);
        EXPECT_NEAR(fogExtinction(1000.f, 6000.f), 2.f / 5500.f, 1e-9f);
        EXPECT_NEAR(fogExtinction(200.f, 3000.f), 2.f / 2900.f, 1e-9f);
    }

    TEST(RenderVolume, fogExtinctionStaysFiniteWhenEndIsNotBeyondHalfStart)
    {
        for (const auto& pair : { std::pair{ 100.f, 50.f }, std::pair{ 100.f, 10.f }, std::pair{ 0.f, 0.f },
                 std::pair{ 1000.f, -5.f } })
        {
            const float e = fogExtinction(pair.first, pair.second);
            EXPECT_TRUE(std::isfinite(e));
            EXPECT_GT(e, 0.f);
            EXPECT_NEAR(e, 2.f, 1e-6f);
        }
    }

    TEST(RenderVolume, heightFalloffIsOneAtAndBelowTheBase)
    {
        EXPECT_FLOAT_EQ(heightFalloff(100.f, 100.f, 500.f), 1.f);
        EXPECT_FLOAT_EQ(heightFalloff(0.f, 100.f, 500.f), 1.f);
        EXPECT_FLOAT_EQ(heightFalloff(-1e6f, 100.f, 500.f), 1.f);
    }

    TEST(RenderVolume, heightFalloffDecaysWithHeight)
    {
        EXPECT_NEAR(heightFalloff(600.f, 100.f, 500.f), std::exp(-1.f), 1e-6f);
        float previous = 1.f;
        for (float z = 100.f; z < 5000.f; z += 100.f)
        {
            const float f = heightFalloff(z, 100.f, 500.f);
            EXPECT_LE(f, previous);
            EXPECT_GT(f, 0.f);
            EXPECT_LE(f, 1.f);
            previous = f;
        }
    }

    TEST(RenderVolume, heightFalloffSurvivesATinyScaleHeight)
    {
        for (float scale : { 0.f, -5.f, 1e-9f })
        {
            const float f = heightFalloff(50.f, 0.f, scale);
            EXPECT_TRUE(std::isfinite(f));
            EXPECT_GE(f, 0.f);
            EXPECT_LE(f, 1.f);
            EXPECT_FLOAT_EQ(heightFalloff(0.f, 0.f, scale), 1.f);
        }
    }

    TEST(RenderVolume, tailTransmittanceIsOneUpToTheFarPlane)
    {
        EXPECT_FLOAT_EQ(tailTransmittance(0.01f, 100.f, 4000.f), 1.f);
        EXPECT_FLOAT_EQ(tailTransmittance(0.01f, 4000.f, 4000.f), 1.f);
        EXPECT_FLOAT_EQ(tailTransmittance(0.01f, 0.f, 4000.f), 1.f);
    }

    TEST(RenderVolume, tailTransmittanceContinuesTheSameExtinction)
    {
        EXPECT_NEAR(tailTransmittance(0.001f, 6000.f, 4000.f), std::exp(-2.f), 1e-6f);
        // Joined with the in-volume part it is plain Beer-Lambert over the whole distance.
        const float joined = transmittance(0.001f, 4000.f) * tailTransmittance(0.001f, 6000.f, 4000.f);
        EXPECT_NEAR(joined, transmittance(0.001f, 6000.f), 1e-6f);
    }

    TEST(RenderVolume, tailTransmittanceNeverIncreasesWithDistance)
    {
        float previous = 1.f;
        for (float d = 0.f; d < 20000.f; d += 500.f)
        {
            const float t = tailTransmittance(0.0005f, d, 4000.f);
            EXPECT_LE(t, previous);
            previous = t;
        }
    }

    TEST(RenderVolume, jitterPeriodIsSixteen)
    {
        EXPECT_EQ(jitterPeriod, 16u);
    }

    TEST(RenderVolume, jitterStartsWithTheHaltonValues)
    {
        EXPECT_NEAR(jitter(0)[0], 0.5f, 1e-6f);
        EXPECT_NEAR(jitter(1)[0], 0.25f, 1e-6f);
        EXPECT_NEAR(jitter(2)[0], 0.75f, 1e-6f);
        EXPECT_NEAR(jitter(0)[1], 1.f / 3.f, 1e-6f);
        EXPECT_NEAR(jitter(0)[2], 0.2f, 1e-6f);
    }

    TEST(RenderVolume, jitterStaysInTheUnitInterval)
    {
        for (uint32_t frame = 0; frame < 100; ++frame)
        {
            const auto j = jitter(frame);
            for (int i = 0; i < 3; ++i)
            {
                EXPECT_GE(j[i], 0.f);
                EXPECT_LT(j[i], 1.f);
            }
        }
    }

    TEST(RenderVolume, jitterIsDeterministicAndRepeatsEverySixteenFrames)
    {
        for (uint32_t frame = 0; frame < 64; ++frame)
        {
            EXPECT_EQ(jitter(frame), jitter(frame));
            EXPECT_EQ(jitter(frame), jitter(frame + jitterPeriod));
            EXPECT_EQ(jitter(frame), jitter(frame + 5 * jitterPeriod));
        }
    }

    TEST(RenderVolume, jitterWrapsWithoutOverflowAtTheTopOfTheRange)
    {
        const uint32_t frame = std::numeric_limits<uint32_t>::max();
        const auto j = jitter(frame);
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_TRUE(std::isfinite(j[i]));
            EXPECT_GE(j[i], 0.f);
            EXPECT_LT(j[i], 1.f);
        }
        EXPECT_EQ(j, jitter(frame % jitterPeriod));
    }

    TEST(RenderVolume, jitterValuesAreDistinctWithinAPeriod)
    {
        for (int component = 0; component < 3; ++component)
        {
            std::set<float> seen;
            for (uint32_t frame = 0; frame < jitterPeriod; ++frame)
                seen.insert(jitter(frame)[component]);
            EXPECT_EQ(seen.size(), static_cast<size_t>(jitterPeriod)) << "component " << component;
        }
    }

    TEST(RenderVolume, volumetricsNeedTheSettingAndHdr)
    {
        EXPECT_TRUE(useVolumetrics(true, true));
        EXPECT_FALSE(useVolumetrics(true, false));
        EXPECT_FALSE(useVolumetrics(false, true));
        EXPECT_FALSE(useVolumetrics(false, false));
    }

    TEST(RenderVolume, temporalNeedsTheSettingAndNotLowQuality)
    {
        EXPECT_FALSE(temporalEnabled(Quality::Low, true));
        EXPECT_TRUE(temporalEnabled(Quality::Medium, true));
        EXPECT_TRUE(temporalEnabled(Quality::High, true));
        EXPECT_FALSE(temporalEnabled(Quality::Low, false));
        EXPECT_FALSE(temporalEnabled(Quality::Medium, false));
        EXPECT_FALSE(temporalEnabled(Quality::High, false));
    }

    TEST(RenderVolume, blendHistoryIsALerpWithTheWeightOnHistory)
    {
        EXPECT_FLOAT_EQ(blendHistory(2.f, 10.f, 0.f), 10.f);
        EXPECT_FLOAT_EQ(blendHistory(2.f, 10.f, 1.f), 2.f);
        EXPECT_FLOAT_EQ(blendHistory(2.f, 10.f, 0.5f), 6.f);
        EXPECT_NEAR(blendHistory(0.f, 1.f, 0.9f), 0.1f, 1e-6f);
    }

    TEST(RenderVolume, blendHistoryClampsTheWeight)
    {
        EXPECT_FLOAT_EQ(blendHistory(2.f, 10.f, -1.f), 10.f);
        EXPECT_FLOAT_EQ(blendHistory(2.f, 10.f, 3.f), 2.f);
    }

    TEST(RenderVolume, blendHistoryStaysBetweenTheInputs)
    {
        for (float w = 0.f; w <= 1.f; w += 0.1f)
        {
            const float v = blendHistory(3.f, 7.f, w);
            EXPECT_GE(v, 3.f - 1e-6f);
            EXPECT_LE(v, 7.f + 1e-6f);
        }
    }

    TEST(RenderVolume, historyWeightIsNinetyPercent)
    {
        EXPECT_FLOAT_EQ(historyWeight, 0.9f);
        EXPECT_GT(historyWeight, 0.f);
        EXPECT_LT(historyWeight, 1.f);
    }
}
