// Tests for the procedural cirrus layer's maths (task 0079, stage a) and for its Slang twin.
//
// Written from the spec alone, before the implementation: the shell and its distance, the wind frame, the twilight
// weight, the octave fade by pixel footprint, the weather share, the optical depth, the phase function, the fibre
// density's properties, the drift and its wrapping, the settings gates and the dome's vertices. The shader tests read
// the Slang source for its constants and the built SPIR-V for its interface. No GPU, window or game data is needed.

#include <components/render/atmospheremath.hpp>
#include <components/render/cirrusmath.hpp>
#include <components/render/cloudvolumemath.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace Cirrus = Render::Cirrus;

    constexpr double pi = 3.14159265358979323846;

    float degrees(float radians)
    {
        return radians * 180.f / static_cast<float>(pi);
    }

    float lengthOf(const vsg::vec3& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    float distanceBetween(const vsg::vec3& a, const vsg::vec3& b)
    {
        return lengthOf(vsg::vec3(a.x - b.x, a.y - b.y, a.z - b.z));
    }

    // A fixed, spread-out set of points of the wind frame, in metres (several noise cells apart, both signs).
    std::vector<vsg::vec2> spreadPoints(int count)
    {
        std::vector<vsg::vec2> points;
        for (int i = 0; i < count; ++i)
        {
            points.emplace_back(
                static_cast<float>((i * 7919) % 200000), static_cast<float>((i * 104729) % 600000) - 300000.f);
        }
        return points;
    }

    float meanDensity(const std::vector<vsg::vec2>& points, float coverage)
    {
        double sum = 0.0;
        for (const vsg::vec2& p : points)
        {
            sum += static_cast<double>(Cirrus::density(p, 0.37f, 5, 2, coverage));
        }
        return static_cast<float>(sum / static_cast<double>(points.size()));
    }

    // --- Constants ---------------------------------------------------------------------------------------------------

    TEST(RenderCirrusConstants, HoldTheSpecifiedValues)
    {
        EXPECT_DOUBLE_EQ(Cirrus::earthRadiusMeters, 6371000.0);
        EXPECT_FLOAT_EQ(Cirrus::tauMax, 0.3f);
        EXPECT_FLOAT_EQ(Cirrus::fibreStretch, 6.f);
        EXPECT_FLOAT_EQ(Cirrus::veerRadians, 0.52f);
        EXPECT_FLOAT_EQ(Cirrus::cellMeters, 2000.f);
        EXPECT_EQ(Cirrus::noisePeriodCells, 128);
        EXPECT_FLOAT_EQ(Cirrus::morphSeconds, 600.f);
        EXPECT_FLOAT_EQ(Cirrus::ambientShare, 0.6f);
        EXPECT_FLOAT_EQ(Cirrus::phaseAnisotropy, 0.75f);
        EXPECT_FLOAT_EQ(Cirrus::phaseCap, 8.f);
        EXPECT_FLOAT_EQ(Cirrus::shareSeconds, 3.f);
        EXPECT_FLOAT_EQ(Cirrus::horizonCutZ, -0.05f);
        EXPECT_FLOAT_EQ(Cirrus::horizonFadeEndZ, 0.02f);
        EXPECT_FLOAT_EQ(Cirrus::baseCoverage, 0.55f);
        EXPECT_FLOAT_EQ(Cirrus::sunGain, 0.5f);
        EXPECT_FLOAT_EQ(Cirrus::minAltitudeKm, 6.f);
        EXPECT_FLOAT_EQ(Cirrus::maxAltitudeKm, 12.f);
        EXPECT_FLOAT_EQ(Cirrus::defaultAltitudeKm, 9.f);
        EXPECT_EQ(Cirrus::domeColumns, 32);
        EXPECT_EQ(Cirrus::domeRows, 8);
    }

    TEST(RenderCirrusConstants, RelationsHold)
    {
        EXPECT_LT(Cirrus::minAltitudeKm, Cirrus::defaultAltitudeKm);
        EXPECT_LT(Cirrus::defaultAltitudeKm, Cirrus::maxAltitudeKm);
        EXPECT_EQ(Cirrus::domeVertexCount, static_cast<std::uint32_t>(32 * 8 * 6));
        EXPECT_EQ(Cirrus::domeVertexCount, 1536u);
        EXPECT_NEAR(Cirrus::domeBelowHorizonRadians, 3.0 * pi / 180.0, 1e-6);
        EXPECT_FLOAT_EQ(Cirrus::cellMeters * static_cast<float>(Cirrus::noisePeriodCells), 256000.f);
        EXPECT_LT(Cirrus::horizonCutZ, Cirrus::horizonFadeEndZ);
        // The share eases at the speed of the volumetric clouds' share.
        EXPECT_FLOAT_EQ(Cirrus::shareSeconds, Render::CloudVolume::shareSeconds);
    }

    // --- The shell ---------------------------------------------------------------------------------------------------

    TEST(RenderCirrusShell, DistanceIsTheAltitudeStraightUp)
    {
        for (int h = 6000; h <= 12000; h += 500)
        {
            EXPECT_NEAR(Cirrus::shellDistance(1.f, static_cast<float>(h)), static_cast<float>(h), 0.5f) << h;
        }
    }

    TEST(RenderCirrusShell, DistanceAtTheHorizonIsTheTangentLength)
    {
        EXPECT_NEAR(Cirrus::shellDistance(0.f, 9000.f), 338700.f, 300.f);
        for (int h = 6000; h <= 12000; h += 1000)
        {
            const double altitude = static_cast<double>(h);
            const double expected = std::sqrt(2.0 * Cirrus::earthRadiusMeters * altitude + altitude * altitude);
            EXPECT_NEAR(static_cast<double>(Cirrus::shellDistance(0.f, static_cast<float>(h))), expected, 0.5) << h;
        }
    }

    TEST(RenderCirrusShell, DistanceIsFinitePositiveAndClampsItsDirection)
    {
        for (int h = 6000; h <= 12000; h += 1500)
        {
            for (int i = -60; i <= 60; ++i)
            {
                const float dz = 0.025f * static_cast<float>(i); // -1.5 to 1.5
                const float t = Cirrus::shellDistance(dz, static_cast<float>(h));
                EXPECT_TRUE(std::isfinite(t)) << dz << " " << h;
                EXPECT_GT(t, 0.f) << dz << " " << h;
            }
            const float altitude = static_cast<float>(h);
            EXPECT_EQ(Cirrus::shellDistance(2.f, altitude), Cirrus::shellDistance(1.f, altitude));
            EXPECT_EQ(Cirrus::shellDistance(-2.f, altitude), Cirrus::shellDistance(-1.f, altitude));
        }
    }

    TEST(RenderCirrusShell, DistanceFallsAsTheDirectionRisesToTheZenith)
    {
        for (int h = 6000; h <= 12000; h += 3000)
        {
            float previous = std::numeric_limits<float>::max();
            for (int i = 0; i <= 400; ++i)
            {
                const float dz = -1.f + 0.005f * static_cast<float>(i);
                const float t = Cirrus::shellDistance(dz, static_cast<float>(h));
                EXPECT_LE(t, previous) << dz << " " << h;
                previous = t;
            }
        }
    }

    TEST(RenderCirrusShell, LayerPointStraightUpIsTheOrigin)
    {
        const vsg::vec2 p = Cirrus::layerPoint(vsg::vec3(0.f, 0.f, 1.f), 9000.f);
        EXPECT_NEAR(p.x, 0.f, 1e-3f);
        EXPECT_NEAR(p.y, 0.f, 1e-3f);
    }

    TEST(RenderCirrusShell, LayerPointAlongXHasNoY)
    {
        const float dz = 0.1f;
        const float dx = std::sqrt(1.f - dz * dz);
        const vsg::vec2 p = Cirrus::layerPoint(vsg::vec3(dx, 0.f, dz), 9000.f);
        EXPECT_NEAR(p.y, 0.f, 1e-3f);
        EXPECT_NEAR(p.x, Cirrus::shellDistance(dz, 9000.f) * dx, 0.05f);
        EXPECT_GT(p.x, 0.f);
    }

    TEST(RenderCirrusShell, LayerPointIsSymmetricUnderTurningTheDirection)
    {
        for (int h = 6000; h <= 12000; h += 3000)
        {
            const vsg::vec2 a = Cirrus::layerPoint(vsg::vec3(0.48f, 0.64f, 0.6f), static_cast<float>(h));
            const vsg::vec2 b = Cirrus::layerPoint(vsg::vec3(-0.48f, -0.64f, 0.6f), static_cast<float>(h));
            EXPECT_NEAR(a.x, -b.x, 1e-3f) << h;
            EXPECT_NEAR(a.y, -b.y, 1e-3f) << h;
            EXPECT_GT(a.x, 0.f);
            EXPECT_GT(a.y, 0.f);
        }
    }

    // --- The wind frame ----------------------------------------------------------------------------------------------

    TEST(RenderCirrusWindFrame, AlongXStretchesTheAcrossAxis)
    {
        const vsg::vec2 r = Cirrus::windFrame(vsg::vec2(100.f, 40.f), vsg::vec2(1.f, 0.f));
        EXPECT_NEAR(r.x, 100.f, 1e-4f);
        EXPECT_NEAR(r.y, 240.f, 1e-3f);
    }

    TEST(RenderCirrusWindFrame, AlongYUsesTheLeftHandPerpendicular)
    {
        const vsg::vec2 r = Cirrus::windFrame(vsg::vec2(100.f, 40.f), vsg::vec2(0.f, 1.f));
        EXPECT_NEAR(r.x, 40.f, 1e-4f);
        EXPECT_NEAR(r.y, -600.f, 1e-3f);
    }

    TEST(RenderCirrusWindFrame, TurningPointAndWindTogetherKeepsBothCoordinates)
    {
        const vsg::vec2 base = Cirrus::windFrame(vsg::vec2(100.f, 40.f), vsg::vec2(1.f, 0.f));
        for (int i = 1; i < 12; ++i)
        {
            const float angle = 0.5f * static_cast<float>(i);
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const vsg::vec2 p(100.f * c - 40.f * s, 100.f * s + 40.f * c);
            const vsg::vec2 r = Cirrus::windFrame(p, vsg::vec2(c, s));
            EXPECT_NEAR(r.x, base.x, 1e-3f) << angle;
            EXPECT_NEAR(r.y, base.y, 1e-2f) << angle;
        }
    }

    TEST(RenderCirrusWindFrame, IsLinearInThePoint)
    {
        const vsg::vec2 wind(0.6f, 0.8f);
        const vsg::vec2 a(1200.f, -300.f);
        const vsg::vec2 b(-450.f, 970.f);
        const vsg::vec2 ra = Cirrus::windFrame(a, wind);
        const vsg::vec2 rb = Cirrus::windFrame(b, wind);
        const vsg::vec2 sum = Cirrus::windFrame(vsg::vec2(a.x + 2.f * b.x, a.y + 2.f * b.y), wind);
        EXPECT_NEAR(sum.x, ra.x + 2.f * rb.x, 1e-2f);
        EXPECT_NEAR(sum.y, ra.y + 2.f * rb.y, 1e-2f);
        const vsg::vec2 zero = Cirrus::windFrame(vsg::vec2(0.f, 0.f), wind);
        EXPECT_NEAR(zero.x, 0.f, 1e-6f);
        EXPECT_NEAR(zero.y, 0.f, 1e-6f);
    }

    // --- Twilight ----------------------------------------------------------------------------------------------------

    TEST(RenderCirrusLight, DepressionLimitIsTheGeometricDip)
    {
        EXPECT_NEAR(degrees(Cirrus::depressionLimit(6000.f)), 2.49f, 0.02f);
        EXPECT_NEAR(degrees(Cirrus::depressionLimit(9000.f)), 3.04f, 0.02f);
        EXPECT_NEAR(degrees(Cirrus::depressionLimit(12000.f)), 3.52f, 0.02f);
        float previous = 0.f;
        for (int h = 6000; h <= 12000; h += 250)
        {
            const float limit = Cirrus::depressionLimit(static_cast<float>(h));
            EXPECT_GT(limit, previous) << h;
            previous = limit;
        }
    }

    TEST(RenderCirrusLight, IsOneAtAndAboveTheHorizon)
    {
        for (int h = 6000; h <= 12000; h += 3000)
        {
            for (int i = 0; i <= 150; ++i)
            {
                const float elevation = 0.01f * static_cast<float>(i);
                EXPECT_FLOAT_EQ(Cirrus::litWeight(elevation, static_cast<float>(h)), 1.f) << elevation << " " << h;
            }
        }
    }

    TEST(RenderCirrusLight, IsZeroAtAndBelowTheDepressionLimit)
    {
        for (int h = 6000; h <= 12000; h += 3000)
        {
            const float limit = Cirrus::depressionLimit(static_cast<float>(h));
            EXPECT_EQ(Cirrus::litWeight(-limit, static_cast<float>(h)), 0.f) << h;
            EXPECT_EQ(Cirrus::litWeight(-limit - 0.01f, static_cast<float>(h)), 0.f) << h;
            EXPECT_EQ(Cirrus::litWeight(-1.5f, static_cast<float>(h)), 0.f) << h;
        }
    }

    TEST(RenderCirrusLight, IsHalfAtHalfTheLimit)
    {
        for (int h = 6000; h <= 12000; h += 3000)
        {
            const float limit = Cirrus::depressionLimit(static_cast<float>(h));
            EXPECT_NEAR(Cirrus::litWeight(-0.5f * limit, static_cast<float>(h)), 0.5f, 1e-4f) << h;
        }
    }

    TEST(RenderCirrusLight, RisesWithTheSunAndStaysInRange)
    {
        for (int h = 6000; h <= 12000; h += 3000)
        {
            float previous = -1.f;
            for (int i = 0; i <= 1800; ++i)
            {
                const float elevation = -0.3f + 0.001f * static_cast<float>(i); // -0.3 to 1.5
                const float w = Cirrus::litWeight(elevation, static_cast<float>(h));
                EXPECT_GE(w, previous) << elevation << " " << h;
                EXPECT_GE(w, 0.f) << elevation;
                EXPECT_LE(w, 1.f) << elevation;
                previous = w;
            }
        }
    }

    // --- Octave fade by footprint ------------------------------------------------------------------------------------

    TEST(RenderCirrusOctave, FullBelowAQuarterOfTheWavelengthNoneAboveTheWavelength)
    {
        for (const float wavelength : { 100.f, 2000.f, 31250.f })
        {
            EXPECT_FLOAT_EQ(Cirrus::octaveWeight(0.25f * wavelength, wavelength), 1.f) << wavelength;
            EXPECT_FLOAT_EQ(Cirrus::octaveWeight(0.1f * wavelength, wavelength), 1.f) << wavelength;
            EXPECT_FLOAT_EQ(Cirrus::octaveWeight(0.f, wavelength), 1.f) << wavelength;
            EXPECT_FLOAT_EQ(Cirrus::octaveWeight(-5.f, wavelength), 1.f) << wavelength;
            EXPECT_FLOAT_EQ(Cirrus::octaveWeight(wavelength, wavelength), 0.f) << wavelength;
            EXPECT_FLOAT_EQ(Cirrus::octaveWeight(3.f * wavelength, wavelength), 0.f) << wavelength;
        }
    }

    TEST(RenderCirrusOctave, IsHalfAtTheMiddleOfTheRamp)
    {
        for (const float wavelength : { 100.f, 2000.f, 31250.f })
        {
            EXPECT_NEAR(Cirrus::octaveWeight(0.625f * wavelength, wavelength), 0.5f, 1e-4f) << wavelength;
        }
    }

    TEST(RenderCirrusOctave, FallsSmoothlyBetweenAndStaysInRange)
    {
        const float wavelength = 2000.f;
        float previous = 2.f;
        for (int i = 0; i <= 3000; ++i)
        {
            const float footprint = static_cast<float>(i);
            const float w = Cirrus::octaveWeight(footprint, wavelength);
            EXPECT_LE(w, previous) << footprint;
            EXPECT_GE(w, 0.f) << footprint;
            EXPECT_LE(w, 1.f) << footprint;
            if (i > 0)
            {
                // Smooth: no step bigger than the steepest slope of a smoothstep over 1500 m allows (1.5 / 1500) per
                // metre, with a margin.
                EXPECT_LT(previous - w, 0.01f) << footprint;
            }
            previous = w;
        }
    }

    TEST(RenderCirrusOctave, BadWavelengthIsNeverNaN)
    {
        for (const float wavelength : { 0.f, -10.f, -1e6f })
        {
            for (const float footprint : { -5.f, 0.f, 3.f, 1e6f })
            {
                const float w = Cirrus::octaveWeight(footprint, wavelength);
                EXPECT_TRUE(std::isfinite(w)) << footprint << " " << wavelength;
                EXPECT_GE(w, 0.f) << footprint << " " << wavelength;
                EXPECT_LE(w, 1.f) << footprint << " " << wavelength;
            }
        }
    }

    // --- Pixel footprint ---------------------------------------------------------------------------------------------

    TEST(RenderCirrusFootprint, StraightUpIsAlphaTimesAltitude)
    {
        const float alpha = 9.6e-4f;
        for (int h = 6000; h <= 12000; h += 3000)
        {
            const float expected = alpha * static_cast<float>(h);
            EXPECT_NEAR(Cirrus::footprint(alpha, 1.f, static_cast<float>(h)), expected, 0.005f * expected) << h;
        }
    }

    TEST(RenderCirrusFootprint, AtTheHorizonIsAlphaTimesTheRadiusPlusAltitude)
    {
        const float alpha = 9.6e-4f;
        for (int h = 6000; h <= 12000; h += 3000)
        {
            const float expected = alpha * (static_cast<float>(Cirrus::earthRadiusMeters) + static_cast<float>(h));
            EXPECT_NEAR(Cirrus::footprint(alpha, 0.f, static_cast<float>(h)), expected, 0.005f * expected) << h;
        }
    }

    TEST(RenderCirrusFootprint, GrowsTowardsTheHorizon)
    {
        const float alpha = 9.6e-4f;
        EXPECT_NEAR(Cirrus::footprint(alpha, 0.1f, 9000.f), 713.f, 0.05f * 713.f);
        EXPECT_NEAR(Cirrus::footprint(alpha, 0.05f, 9000.f), 1900.f, 0.05f * 1900.f);
        float previous = std::numeric_limits<float>::max();
        for (int i = 0; i <= 98; ++i)
        {
            const float dz = 0.02f + 0.01f * static_cast<float>(i); // 0.02 to 1
            const float f = Cirrus::footprint(alpha, dz, 9000.f);
            EXPECT_LE(f, previous * 1.0001f + 1e-3f) << dz;
            previous = f;
        }
    }

    TEST(RenderCirrusFootprint, ZeroAngleGivesZeroAndTheResultIsBounded)
    {
        for (int i = -20; i <= 20; ++i)
        {
            const float dz = 0.05f * static_cast<float>(i);
            EXPECT_EQ(Cirrus::footprint(0.f, dz, 9000.f), 0.f) << dz;
            for (const float alpha : { 1e-4f, 1e-2f, 1.f, 1e9f })
            {
                const float f = Cirrus::footprint(alpha, dz, 9000.f);
                EXPECT_TRUE(std::isfinite(f)) << dz << " " << alpha;
                EXPECT_GE(f, 0.f) << dz << " " << alpha;
                EXPECT_LE(f, 1e7f) << dz << " " << alpha;
            }
        }
        EXPECT_FLOAT_EQ(Cirrus::footprint(1e9f, 1.f, 9000.f), 1e7f);
    }

    // --- Horizon fade ------------------------------------------------------------------------------------------------

    TEST(RenderCirrusHorizon, FadeIsZeroBelowTheCutAndOneAboveTheEnd)
    {
        EXPECT_EQ(Cirrus::horizonFade(Cirrus::horizonCutZ), 0.f);
        EXPECT_EQ(Cirrus::horizonFade(Cirrus::horizonCutZ - 0.3f), 0.f);
        EXPECT_EQ(Cirrus::horizonFade(-1.f), 0.f);
        EXPECT_EQ(Cirrus::horizonFade(Cirrus::horizonFadeEndZ), 1.f);
        EXPECT_EQ(Cirrus::horizonFade(0.5f), 1.f);
        EXPECT_EQ(Cirrus::horizonFade(1.f), 1.f);
    }

    TEST(RenderCirrusHorizon, FadeIsAMonotoneSmoothstep)
    {
        float previous = -1.f;
        for (int i = 0; i <= 2000; ++i)
        {
            const float z = -1.f + 0.001f * static_cast<float>(i);
            const float f = Cirrus::horizonFade(z);
            EXPECT_GE(f, previous) << z;
            EXPECT_GE(f, 0.f) << z;
            EXPECT_LE(f, 1.f) << z;
            previous = f;
        }
        const float middle = 0.5f * (Cirrus::horizonCutZ + Cirrus::horizonFadeEndZ);
        EXPECT_NEAR(Cirrus::horizonFade(middle), 0.5f, 1e-4f);
    }

    // --- Weather share -----------------------------------------------------------------------------------------------

    TEST(RenderCirrusShare, TargetIsOneInClearWeather)
    {
        EXPECT_FLOAT_EQ(Cirrus::shareTarget(0.39f, false, 0.69f), 1.f);
    }

    TEST(RenderCirrusShare, TargetFollowsTheFormula)
    {
        using Render::CloudVolume::smoothstep;
        using Render::CloudVolume::volumeShareTarget;
        for (int i = 0; i <= 20; ++i)
        {
            const float coverage = 0.05f * static_cast<float>(i);
            for (const float fog : { 0.2f, 0.69f, 0.88f, 0.9f, 1.f })
            {
                const float expected = (1.f - smoothstep(0.55f, 0.95f, coverage)) * volumeShareTarget(false, fog);
                EXPECT_NEAR(Cirrus::shareTarget(coverage, false, fog), expected, 1e-6f) << coverage << " " << fog;
            }
        }
        const float cloudy = Cirrus::shareTarget(0.63f, false, 0.69f);
        EXPECT_NEAR(cloudy, 1.f - smoothstep(0.55f, 0.95f, 0.63f), 1e-6f);
        EXPECT_GT(cloudy, 0.85f);
        EXPECT_LT(cloudy, 0.95f);
    }

    TEST(RenderCirrusShare, TargetIsZeroForThickCloudStormsAndThickFog)
    {
        EXPECT_EQ(Cirrus::shareTarget(0.95f, false, 0.69f), 0.f);
        EXPECT_EQ(Cirrus::shareTarget(1.f, false, 0.69f), 0.f);
        EXPECT_EQ(Cirrus::shareTarget(0.2f, true, 0.69f), 0.f);
        EXPECT_EQ(Cirrus::shareTarget(0.2f, true, 0.f), 0.f);
        EXPECT_EQ(Cirrus::shareTarget(0.2f, false, 0.95f), 0.f);
        EXPECT_EQ(Cirrus::shareTarget(0.2f, false, 2.8f), 0.f);
    }

    TEST(RenderCirrusShare, TargetNeverRisesWithCoverageOrFog)
    {
        for (const float fog : { 0.3f, 0.69f, 0.9f })
        {
            float previous = 2.f;
            for (int i = 0; i <= 200; ++i)
            {
                const float t = Cirrus::shareTarget(0.005f * static_cast<float>(i), false, fog);
                EXPECT_LE(t, previous) << i << " " << fog;
                EXPECT_GE(t, 0.f);
                EXPECT_LE(t, 1.f);
                previous = t;
            }
        }
        for (const float coverage : { 0.1f, 0.6f })
        {
            float previous = 2.f;
            for (int i = 0; i <= 300; ++i)
            {
                const float t = Cirrus::shareTarget(coverage, false, 0.01f * static_cast<float>(i));
                EXPECT_LE(t, previous) << i << " " << coverage;
                previous = t;
            }
        }
    }

    TEST(RenderCirrusShare, EasedShareMovesByAtMostTheStepAndNeverOvershoots)
    {
        for (const float current : { 0.f, 0.3f, 1.f })
        {
            for (const float target : { 0.f, 0.25f, 0.9f, 1.f })
            {
                for (const float seconds : { 0.016f, 0.1f, 1.f, 10.f })
                {
                    const float next = Cirrus::easedShare(current, target, seconds);
                    const float step = seconds / Cirrus::shareSeconds;
                    EXPECT_LE(std::abs(next - current), step + 1e-6f) << current << " " << target << " " << seconds;
                    EXPECT_LE(std::abs(next - target), std::abs(current - target) + 1e-6f);
                    if (target >= current)
                    {
                        EXPECT_LE(next, target + 1e-6f);
                        EXPECT_GE(next, current - 1e-6f);
                    }
                    else
                    {
                        EXPECT_GE(next, target - 1e-6f);
                        EXPECT_LE(next, current + 1e-6f);
                    }
                    EXPECT_NEAR(next, Render::CloudVolume::easeShare(current, target, seconds), 1e-6f);
                }
            }
        }
    }

    TEST(RenderCirrusShare, EasedShareReachesTheTargetInThreeSeconds)
    {
        float up = 0.f;
        float down = 1.f;
        for (int i = 0; i < 40; ++i)
        {
            up = Cirrus::easedShare(up, 1.f, 0.1f);
            down = Cirrus::easedShare(down, 0.f, 0.1f);
        }
        EXPECT_FLOAT_EQ(up, 1.f);
        EXPECT_FLOAT_EQ(down, 0.f);
        // And not before: after one second a third of the way.
        float partial = 0.f;
        for (int i = 0; i < 10; ++i)
        {
            partial = Cirrus::easedShare(partial, 1.f, 0.1f);
        }
        EXPECT_NEAR(partial, 1.f / 3.f, 1e-4f);
    }

    TEST(RenderCirrusShare, EasedShareStandsStillWithoutTime)
    {
        EXPECT_EQ(Cirrus::easedShare(0.4f, 1.f, 0.f), 0.4f);
        EXPECT_EQ(Cirrus::easedShare(0.4f, 0.f, -1.f), 0.4f);
        EXPECT_EQ(Cirrus::easedShare(0.4f, 0.4f, 1.f), 0.4f);
    }

    // --- Optical depth, opacity and phase ----------------------------------------------------------------------------

    TEST(RenderCirrusOptics, OpticalDepthIsTheProductOfItsFactors)
    {
        EXPECT_NEAR(Cirrus::opticalDepth(0.5f, 0.8f, 1.f, 1.f), Cirrus::tauMax * 0.5f * 0.8f, 1e-6f);
        EXPECT_NEAR(Cirrus::opticalDepth(1.f, 1.f, 2.f, 1.5f), Cirrus::tauMax * 2.f * 1.5f, 1e-6f);
        EXPECT_NEAR(Cirrus::opticalDepth(0.25f, 1.f, 0.5f, 2.f), Cirrus::tauMax * 0.25f * 0.5f * 2.f, 1e-6f);
    }

    TEST(RenderCirrusOptics, OpticalDepthIsZeroWhenAnyFactorIsZero)
    {
        EXPECT_EQ(Cirrus::opticalDepth(0.f, 1.f, 1.f, 1.f), 0.f);
        EXPECT_EQ(Cirrus::opticalDepth(1.f, 0.f, 1.f, 1.f), 0.f);
        EXPECT_EQ(Cirrus::opticalDepth(1.f, 1.f, 0.f, 1.f), 0.f);
        EXPECT_EQ(Cirrus::opticalDepth(1.f, 1.f, 1.f, 0.f), 0.f);
    }

    TEST(RenderCirrusOptics, OpticalDepthClampsItsFactors)
    {
        EXPECT_FLOAT_EQ(Cirrus::opticalDepth(5.f, 1.f, 1.f, 1.f), Cirrus::opticalDepth(1.f, 1.f, 1.f, 1.f));
        EXPECT_FLOAT_EQ(Cirrus::opticalDepth(1.f, 7.f, 1.f, 1.f), Cirrus::opticalDepth(1.f, 1.f, 1.f, 1.f));
        EXPECT_EQ(Cirrus::opticalDepth(-1.f, 1.f, 1.f, 1.f), 0.f);
        EXPECT_EQ(Cirrus::opticalDepth(1.f, -1.f, 1.f, 1.f), 0.f);
        EXPECT_EQ(Cirrus::opticalDepth(1.f, 1.f, -1.f, 1.f), 0.f);
        EXPECT_EQ(Cirrus::opticalDepth(1.f, 1.f, 1.f, -1.f), 0.f);
    }

    TEST(RenderCirrusOptics, OpticalDepthIsBoundedForInRangeInputs)
    {
        for (int d = 0; d <= 4; ++d)
        {
            for (int s = 0; s <= 4; ++s)
            {
                for (int c = 0; c <= 4; ++c)
                {
                    for (int o = 0; o <= 3; ++o)
                    {
                        const float tau = Cirrus::opticalDepth(0.25f * static_cast<float>(d),
                            0.25f * static_cast<float>(s), 0.5f * static_cast<float>(c), 0.5f * static_cast<float>(o));
                        EXPECT_GE(tau, 0.f);
                        EXPECT_LE(tau, Cirrus::tauMax * 2.f * 1.5f + 1e-6f);
                    }
                }
            }
        }
    }

    TEST(RenderCirrusOptics, OpacityIsOneMinusBeer)
    {
        EXPECT_EQ(Cirrus::opacity(0.f), 0.f);
        EXPECT_EQ(Cirrus::opacity(-1.f), 0.f);
        EXPECT_EQ(Cirrus::opacity(-100.f), 0.f);
        EXPECT_NEAR(Cirrus::opacity(0.3f), 1.f - std::exp(-0.3f), 1e-6f);
        EXPECT_NEAR(Cirrus::opacity(1.f), 1.f - std::exp(-1.f), 1e-6f);
    }

    TEST(RenderCirrusOptics, OpacityRisesAndStaysBelowOne)
    {
        float previous = 0.f;
        for (int i = 1; i <= 500; ++i)
        {
            const float tau = 0.01f * static_cast<float>(i); // up to 5
            const float o = Cirrus::opacity(tau);
            EXPECT_GT(o, previous) << tau;
            EXPECT_LT(o, 1.f) << tau;
            previous = o;
        }
        for (int i = 50; i <= 100; ++i)
        {
            EXPECT_LT(Cirrus::opacity(0.1f * static_cast<float>(i)), 1.f) << i; // up to 10
        }
        for (int i = 100; i <= 200; ++i)
        {
            EXPECT_LE(Cirrus::opacity(0.1f * static_cast<float>(i)), 1.f) << i; // up to 20
        }
    }

    TEST(RenderCirrusOptics, ForwardPhaseIsCappedBrightAheadAndDimBehind)
    {
        EXPECT_GT(Cirrus::forwardPhase(1.f), 1.f);
        EXPECT_FLOAT_EQ(Cirrus::forwardPhase(1.f), Cirrus::phaseCap);
        EXPECT_LT(Cirrus::forwardPhase(-1.f), 1.f);
        for (int i = 0; i <= 2000; ++i)
        {
            const float cosTheta = -1.f + 0.001f * static_cast<float>(i);
            const float p = Cirrus::forwardPhase(cosTheta);
            EXPECT_LE(p, Cirrus::phaseCap) << cosTheta;
            EXPECT_GE(p, 0.f) << cosTheta;
            EXPECT_TRUE(std::isfinite(p)) << cosTheta;
        }
    }

    TEST(RenderCirrusOptics, ForwardPhaseIsTheCappedHenyeyGreenstein)
    {
        for (int i = 0; i <= 200; ++i)
        {
            const float cosTheta = -1.f + 0.01f * static_cast<float>(i);
            const float raw = 4.f * Render::Atmosphere::pi
                * Render::Atmosphere::henyeyGreenstein(cosTheta, Cirrus::phaseAnisotropy);
            const float expected = raw < Cirrus::phaseCap ? raw : Cirrus::phaseCap;
            EXPECT_NEAR(Cirrus::forwardPhase(cosTheta), expected, 1e-4f * (1.f + expected)) << cosTheta;
        }
    }

    TEST(RenderCirrusOptics, PatternCoverageScalesTheBaseAndClamps)
    {
        EXPECT_EQ(Cirrus::patternCoverage(0.f), 0.f);
        EXPECT_FLOAT_EQ(Cirrus::patternCoverage(1.f), Cirrus::baseCoverage);
        EXPECT_FLOAT_EQ(Cirrus::patternCoverage(2.f), 1.f);
        EXPECT_FLOAT_EQ(Cirrus::patternCoverage(100.f), 1.f);
        EXPECT_EQ(Cirrus::patternCoverage(-1.f), 0.f);
        EXPECT_NEAR(Cirrus::patternCoverage(0.5f), 0.5f * Cirrus::baseCoverage, 1e-6f);
    }

    // --- The fibre density -------------------------------------------------------------------------------------------

    TEST(RenderCirrusDensity, IsDeterministic)
    {
        const std::vector<vsg::vec2> points = spreadPoints(100);
        for (const vsg::vec2& p : points)
        {
            const float a = Cirrus::density(p, 0.37f, 5, 2, 0.6f, 120.f);
            const float b = Cirrus::density(p, 0.37f, 5, 2, 0.6f, 120.f);
            EXPECT_EQ(a, b) << p.x << " " << p.y;
        }
    }

    // Regression values (pinned after the implementation, by its author; the independent writer left them out). The
    // noise is Render::CloudVolume::perlin (itself pinned by testcloudvolumemath.cpp) with the cirrus seed; the GPU
    // repeats it, and the debug view of the shader is compared with these at fixed pixels in the game (task 0079, stage
    // c).
    TEST(RenderCirrusDensity, RegressionValuesAtFixedPoints)
    {
        struct Sample
        {
            vsg::vec2 point;
            float morph;
            int octaves;
            int warps;
            float coverage;
            float footprint;
        };
        const Sample samples[] = {
            { vsg::vec2(0.f, 0.f), 0.f, 5, 2, 0.9f, 0.f },
            { vsg::vec2(1234.5f, 567.8f), 0.f, 5, 2, 0.9f, 0.f },
            { vsg::vec2(-45000.f, 12000.f), 3.7f, 4, 1, 0.55f, 0.f },
            { vsg::vec2(100000.f, -3000.f), 3.7f, 3, 0, 0.9f, 0.f },
            { vsg::vec2(250000.f, 777.f), 12.25f, 5, 2, 1.f, 0.f },
            { vsg::vec2(-9000.f, 4000.f), 0.5f, 5, 2, 0.9f, 600.f },
            { vsg::vec2(31000.f, -700.f), 0.5f, 5, 2, 0.8f, 0.f },
            { vsg::vec2(-120000.f, 2500.f), 99.f, 4, 1, 0.7f, 0.f },
        };
        // Re-pinned on 2026-11-04 when the fibre field was redesigned for variety (patches, types, swirl, tails, veil).
        const float expected[] = { 0.344365031f, 0.366307467f, 0.0178119168f, 0.617833555f, 0.408171654f, 0.0700234324f,
            0.906726301f, 0.413809538f };
        std::size_t i = 0;
        for (const Sample& sample : samples)
        {
            const float d = Cirrus::density(
                sample.point, sample.morph, sample.octaves, sample.warps, sample.coverage, sample.footprint);
            EXPECT_NEAR(d, expected[i], 1e-5f) << i;
            ++i;
        }
    }

    // --- The variety of the sky (2026-11-04)
    // --------------------------------------------------------------------------- The first frames showed one kind of
    // fibre, all along the wind, over the whole sky. These tests pin that the field has patches and gaps, fibres that
    // turn from place to place, and changes slowly with the evolution coordinate.

    namespace
    {
        // The density at a point given in true metres east and north of the wind frame's origin.
        float densityAt(float x, float yTrue, float morph = 0.f, float coverage = 0.55f)
        {
            return Cirrus::density(vsg::vec2(x, Cirrus::fibreStretch * yTrue), morph, 4, 1, coverage);
        }

        struct Block
        {
            double mean = 0.0;
            double energy = 0.0; // the structure tensor's trace (how much structure there is)
            double angle = 0.0; // the dominant gradient direction, in the frame's true metres, radians
        };

        // An 8 by 8 grid of blocks of `size` metres, each sampled 16 by 16 times.
        std::vector<Block> blocks(float size, float morph = 0.f)
        {
            std::vector<Block> result;
            for (int bx = 0; bx < 8; ++bx)
            {
                for (int by = 0; by < 8; ++by)
                {
                    Block block;
                    double jxx = 0.0, jyy = 0.0, jxy = 0.0;
                    for (int i = 0; i < 16; ++i)
                    {
                        for (int j = 0; j < 16; ++j)
                        {
                            const float x
                                = static_cast<float>(bx) * size + (static_cast<float>(i) + 0.5f) * size / 16.f;
                            const float y = static_cast<float>(by) * size + (static_cast<float>(j) + 0.5f) * size / 16.f
                                - 4.f * size;
                            const float h = 100.f;
                            const double gx = (densityAt(x + h, y, morph) - densityAt(x - h, y, morph)) / (2.f * h);
                            const double gy = (densityAt(x, y + h, morph) - densityAt(x, y - h, morph)) / (2.f * h);
                            jxx += gx * gx;
                            jyy += gy * gy;
                            jxy += gx * gy;
                            block.mean += static_cast<double>(densityAt(x, y, morph));
                        }
                    }
                    block.mean /= 256.0;
                    block.energy = jxx + jyy;
                    block.angle = 0.5 * std::atan2(2.0 * jxy, jxx - jyy);
                    result.push_back(block);
                }
            }
            return result;
        }
    }

    TEST(RenderCirrusVariety, TheSkyHasGapsAndClumps)
    {
        // 64 km square in 8 km blocks: some blocks are almost clear and many are clearly cloudy.
        const std::vector<Block> all = blocks(8000.f);
        int clear = 0;
        int cloudy = 0;
        double lowest = 1.0;
        double highest = 0.0;
        for (const Block& block : all)
        {
            clear += block.mean < 0.05 ? 1 : 0;
            cloudy += block.mean > 0.12 ? 1 : 0;
            lowest = std::min(lowest, block.mean);
            highest = std::max(highest, block.mean);
        }
        EXPECT_GE(clear, 3);
        EXPECT_GE(cloudy, 20);
        EXPECT_GT(highest - lowest, 0.3);
    }

    TEST(RenderCirrusVariety, TheFibresTurnFromPlaceToPlace)
    {
        // The dominant direction of the structure in each block (doubled angles: a direction and its opposite are one
        // fibre direction). R is the length of their mean: 1 for one direction everywhere, 0 for none.
        double cx = 0.0;
        double cy = 0.0;
        int counted = 0;
        double smallest = 10.0;
        double largest = -10.0;
        for (const Block& block : blocks(8000.f))
        {
            if (block.energy < 2e-9)
            {
                continue;
            }
            cx += std::cos(2.0 * block.angle);
            cy += std::sin(2.0 * block.angle);
            ++counted;
            // The gradient across the fibres is near +-90 degrees: bring both into one range.
            const double wrapped = block.angle < 0.0 ? block.angle + 3.14159265358979 : block.angle;
            smallest = std::min(smallest, wrapped);
            largest = std::max(largest, wrapped);
        }
        ASSERT_GE(counted, 40);
        const double resultant = std::sqrt(cx * cx + cy * cy) / static_cast<double>(counted);
        EXPECT_LT(resultant, 0.92); // 0.85 measured; the first version, with one wind direction, was above 0.99
        // And the spread is real, not a few outliers: the blocks' directions cover more than 50 degrees.
        EXPECT_GT(largest - smallest, 0.87);
    }

    TEST(RenderCirrusVariety, TheFieldEvolvesSlowlyWithTheMorphCoordinate)
    {
        // A small step of the evolution coordinate changes little (the clouds shift, they do not flicker); a large one
        // gives another pattern.
        double nearDiff = 0.0;
        double farDiff = 0.0;
        int n = 0;
        for (int i = 0; i < 40; ++i)
        {
            for (int j = 0; j < 40; ++j)
            {
                const float x = 1500.f * static_cast<float>(i);
                const float y = 1500.f * static_cast<float>(j) - 30000.f;
                const float base = densityAt(x, y, 2.f);
                nearDiff += std::fabs(densityAt(x, y, 2.01f) - base);
                farDiff += std::fabs(densityAt(x, y, 6.f) - base);
                ++n;
            }
        }
        EXPECT_LT(nearDiff / n, 0.02);
        EXPECT_GT(farDiff / n, 0.08);
    }

    TEST(RenderCirrusVariety, FibresDifferInSharpnessByPlace)
    {
        // Short tufts and long streaks: the energy of the structure differs by a factor of at least 10 from the
        // dullest to the sharpest cloudy block.
        double lowest = 1e9;
        double highest = 0.0;
        for (const Block& block : blocks(8000.f))
        {
            if (block.mean > 0.1)
            {
                lowest = std::min(lowest, block.energy);
                highest = std::max(highest, block.energy);
            }
        }
        EXPECT_GT(highest / lowest, 10.0);
    }

    // The pattern debug view (task 0079): the GPU's density on a fixed 40 km square agreed with the C++ at 91 of the 93
    // pixels of a frame that were not covered by scenery (0.006 at most; the rest is the shader's float arithmetic).
    // The view is the sixth name, after the five of the spec.
    TEST(RenderCirrusSettings, ThePatternDebugViewIsTheSixth)
    {
        const auto view = Cirrus::parseDebugView("pattern");
        ASSERT_TRUE(view.has_value());
        EXPECT_EQ(static_cast<int>(*view), 5);
        EXPECT_EQ(static_cast<int>(Cirrus::DebugView::Pattern), 5);
    }

    // Stage d: some days have more cirrus than others (a hash of the date).
    TEST(RenderCirrusShare, TheDayFactorIsAFixedHashOfTheDateBetweenTheLimits)
    {
        std::set<float> seen;
        float sum = 0.f;
        int count = 0;
        for (int month = 0; month < 12; ++month)
        {
            for (int day = 1; day <= 31; ++day)
            {
                const float factor = Cirrus::dayFactor(day, month);
                EXPECT_GE(factor, Cirrus::dayFactorMin);
                EXPECT_LE(factor, 1.f);
                EXPECT_EQ(factor, Cirrus::dayFactor(day, month));
                seen.insert(factor);
                sum += factor;
                ++count;
            }
        }
        // Many different days, and the mean is the middle of the range (0.7) within sampling noise.
        EXPECT_GT(seen.size(), 300u);
        EXPECT_NEAR(sum / static_cast<float>(count), 0.7f, 0.03f);
        // Pinned values (a change of the hash or its seed would change every player's skies).
        EXPECT_NEAR(Cirrus::dayFactor(1, 0), 0.658158064f, 1e-6f);
        EXPECT_NEAR(Cirrus::dayFactor(15, 6), 0.900482059f, 1e-6f);
        // A date out of range does not break it.
        EXPECT_GE(Cirrus::dayFactor(-5, -1), Cirrus::dayFactorMin);
        EXPECT_LE(Cirrus::dayFactor(400, 99), 1.f);
    }

    TEST(RenderCirrusDensity, StaysInRangeAndFinite)
    {
        const std::vector<vsg::vec2> points = spreadPoints(150);
        for (const float morph : { 0.f, 0.37f, 51.9f, 127.99f })
        {
            for (int octaves = 1; octaves <= 5; ++octaves)
            {
                for (int warps = 0; warps <= 2; ++warps)
                {
                    for (const float footprint : { 0.f, 500.f, 1e6f })
                    {
                        for (const vsg::vec2& p : points)
                        {
                            const float d = Cirrus::density(p, morph, octaves, warps, 0.8f, footprint);
                            EXPECT_TRUE(std::isfinite(d));
                            EXPECT_GE(d, 0.f);
                            EXPECT_LE(d, 1.f);
                        }
                    }
                }
            }
        }
    }

    TEST(RenderCirrusDensity, IsZeroWithoutCoverage)
    {
        for (const vsg::vec2& p : spreadPoints(200))
        {
            EXPECT_EQ(Cirrus::density(p, 0.37f, 5, 2, 0.f), 0.f) << p.x << " " << p.y;
            EXPECT_EQ(Cirrus::density(p, 11.f, 3, 0, 0.f, 300.f), 0.f) << p.x << " " << p.y;
            EXPECT_EQ(Cirrus::density(p, 11.f, 3, 0, -2.f), 0.f) << p.x << " " << p.y;
        }
    }

    TEST(RenderCirrusDensity, ClampsOctavesWarpsAndCoverage)
    {
        for (const vsg::vec2& p : spreadPoints(120))
        {
            EXPECT_EQ(Cirrus::density(p, 0.37f, 9, 2, 0.7f), Cirrus::density(p, 0.37f, 5, 2, 0.7f));
            EXPECT_EQ(Cirrus::density(p, 0.37f, 0, 1, 0.7f), Cirrus::density(p, 0.37f, 1, 1, 0.7f));
            EXPECT_EQ(Cirrus::density(p, 0.37f, -3, 1, 0.7f), Cirrus::density(p, 0.37f, 1, 1, 0.7f));
            EXPECT_EQ(Cirrus::density(p, 0.37f, 4, 7, 0.7f), Cirrus::density(p, 0.37f, 4, 2, 0.7f));
            EXPECT_EQ(Cirrus::density(p, 0.37f, 4, -1, 0.7f), Cirrus::density(p, 0.37f, 4, 0, 0.7f));
            EXPECT_EQ(Cirrus::density(p, 0.37f, 4, 1, 2.f), Cirrus::density(p, 0.37f, 4, 1, 1.f));
            EXPECT_EQ(Cirrus::density(p, 0.37f, 4, 1, -1.f), Cirrus::density(p, 0.37f, 4, 1, 0.f));
        }
    }

    TEST(RenderCirrusDensity, NeverFallsWhenCoverageGrows)
    {
        for (const vsg::vec2& p : spreadPoints(120))
        {
            float previous = 0.f;
            for (int i = 0; i <= 20; ++i)
            {
                const float d = Cirrus::density(p, 0.37f, 5, 2, 0.05f * static_cast<float>(i));
                EXPECT_GE(d, previous - 1e-6f) << p.x << " " << p.y << " " << i;
                previous = d;
            }
        }
    }

    TEST(RenderCirrusDensity, MeanRisesWithCoverage)
    {
        const std::vector<vsg::vec2> points = spreadPoints(200);
        const float none = meanDensity(points, 0.f);
        const float half = meanDensity(points, 0.5f);
        const float full = meanDensity(points, 1.f);
        EXPECT_EQ(none, 0.f);
        EXPECT_GT(half, none);
        EXPECT_GT(full, half);
    }

    TEST(RenderCirrusDensity, ALargeFootprintSmoothsTheField)
    {
        // 200 nearby points, 37 m apart along the wind, as they would lie on one row of pixels.
        std::vector<float> sharp;
        std::vector<float> smooth;
        for (int i = 0; i < 200; ++i)
        {
            const vsg::vec2 p(5000.f + 37.f * static_cast<float>(i), 1500.f);
            sharp.push_back(Cirrus::density(p, 0.37f, 5, 2, 0.7f, 0.f));
            smooth.push_back(Cirrus::density(p, 0.37f, 5, 2, 0.7f, 1e6f));
        }
        const auto deviation = [](const std::vector<float>& values) {
            double mean = 0.0;
            for (const float v : values)
            {
                mean += static_cast<double>(v);
            }
            mean /= static_cast<double>(values.size());
            double sum = 0.0;
            for (const float v : values)
            {
                sum += (static_cast<double>(v) - mean) * (static_cast<double>(v) - mean);
            }
            return std::sqrt(sum / static_cast<double>(values.size()));
        };
        const double sharpDeviation = deviation(sharp);
        EXPECT_GT(sharpDeviation, 0.0);
        EXPECT_LT(deviation(smooth), sharpDeviation);
    }

    // --- Wind and drift ----------------------------------------------------------------------------------------------

    TEST(RenderCirrusWind, VeeredDirectionIsUnitAndTurnedByTheVeer)
    {
        const vsg::vec2 v = Cirrus::veeredWindDirection();
        EXPECT_NEAR(std::sqrt(v.x * v.x + v.y * v.y), 1.f, 1e-5f);
        const float wx = Render::CloudVolume::windDirection[0];
        const float wy = Render::CloudVolume::windDirection[1];
        EXPECT_NEAR(wx * v.x + wy * v.y, std::cos(Cirrus::veerRadians), 1e-5f);
        // Counter-clockwise: the cross product of the wind and the veered direction is the sine, positive.
        EXPECT_NEAR(wx * v.y - wy * v.x, std::sin(Cirrus::veerRadians), 1e-5f);
    }

    TEST(RenderCirrusWind, DriftDistanceIsTheVolumeCloudsWindStep)
    {
        EXPECT_NEAR(Cirrus::driftDistance(1.25f, 6.f, 10.f), 300.f, 1e-3f);
        for (const float speed : { 0.5f, 1.25f, 3.f })
        {
            for (const float scale : { 0.5f, 6.f })
            {
                EXPECT_NEAR(
                    Cirrus::driftDistance(speed, scale, 7.f), Render::CloudVolume::windStep(speed * scale, 7.f), 1e-3f);
            }
        }
    }

    TEST(RenderCirrusWind, DriftDistanceIsZeroForNonPositiveInputsAndLinear)
    {
        EXPECT_EQ(Cirrus::driftDistance(0.f, 6.f, 10.f), 0.f);
        EXPECT_EQ(Cirrus::driftDistance(-1.f, 6.f, 10.f), 0.f);
        EXPECT_EQ(Cirrus::driftDistance(1.25f, 0.f, 10.f), 0.f);
        EXPECT_EQ(Cirrus::driftDistance(1.25f, -2.f, 10.f), 0.f);
        EXPECT_EQ(Cirrus::driftDistance(1.25f, 6.f, 0.f), 0.f);
        EXPECT_EQ(Cirrus::driftDistance(1.25f, 6.f, -4.f), 0.f);
        const float base = Cirrus::driftDistance(1.f, 2.f, 3.f);
        EXPECT_NEAR(Cirrus::driftDistance(2.f, 2.f, 3.f), 2.f * base, 1e-3f);
        EXPECT_NEAR(Cirrus::driftDistance(1.f, 4.f, 3.f), 2.f * base, 1e-3f);
        EXPECT_NEAR(Cirrus::driftDistance(1.f, 2.f, 6.f), 2.f * base, 1e-3f);
    }

    TEST(RenderCirrusWind, DriftStepIsTheDistanceAlongTheVeeredWind)
    {
        const vsg::vec2 v = Cirrus::veeredWindDirection();
        const float distance = Cirrus::driftDistance(1.25f, 6.f, 10.f);
        const std::array<float, 2> step = Cirrus::driftStep(1.25f, 6.f, 10.f);
        EXPECT_NEAR(step[0], distance * v.x, 1e-3f);
        EXPECT_NEAR(step[1], distance * v.y, 1e-3f);
        EXPECT_NEAR(std::sqrt(step[0] * step[0] + step[1] * step[1]), 300.f, 1e-2f);
        for (const std::array<float, 3>& input :
            { std::array<float, 3>{ 0.f, 6.f, 10.f }, std::array<float, 3>{ 1.25f, -1.f, 10.f },
                std::array<float, 3>{ 1.25f, 6.f, 0.f }, std::array<float, 3>{ -1.f, -1.f, -1.f } })
        {
            const std::array<float, 2> zero = Cirrus::driftStep(input[0], input[1], input[2]);
            EXPECT_EQ(zero[0], 0.f);
            EXPECT_EQ(zero[1], 0.f);
        }
    }

    TEST(RenderCirrusWind, WrapDriftKeepsValuesInsideOnePeriod)
    {
        EXPECT_EQ(Cirrus::wrapDrift(0.0), 0.0);
        EXPECT_EQ(Cirrus::wrapDrift(100.0), 100.0);
        EXPECT_EQ(Cirrus::wrapDrift(255999.5), 255999.5);
        EXPECT_NEAR(Cirrus::wrapDrift(256000.0 * 3.0 + 1234.5), 1234.5, 1e-6);
        EXPECT_NEAR(Cirrus::wrapDrift(256000.0), 0.0, 1e-6);
        EXPECT_NEAR(Cirrus::wrapDrift(1e9 + 0.25), std::fmod(1e9 + 0.25, 256000.0), 1e-6);
    }

    TEST(RenderCirrusWind, WrapDriftWrapsNegativeValuesAndIgnoresNonFiniteOnes)
    {
        EXPECT_NEAR(Cirrus::wrapDrift(-1.0), 255999.0, 1e-6);
        EXPECT_NEAR(Cirrus::wrapDrift(-256000.0 * 2.0 - 10.0), 255990.0, 1e-6);
        EXPECT_EQ(Cirrus::wrapDrift(std::numeric_limits<double>::quiet_NaN()), 0.0);
        EXPECT_EQ(Cirrus::wrapDrift(std::numeric_limits<double>::infinity()), 0.0);
        EXPECT_EQ(Cirrus::wrapDrift(-std::numeric_limits<double>::infinity()), 0.0);
        for (int i = -500; i <= 500; ++i)
        {
            const double r = Cirrus::wrapDrift(static_cast<double>(i) * 12345.678);
            EXPECT_GE(r, 0.0) << i;
            EXPECT_LT(r, 256000.0) << i;
        }
    }

    TEST(RenderCirrusWind, WrapDriftStaysBelowThePeriodForTinyNegativeValues)
    {
        // The spec says the result is always below 256000; -1e-12 + 256000 rounds to 256000 in a double.
        for (const double tiny : { -1e-12, -1e-9, -1e-6 })
        {
            const double r = Cirrus::wrapDrift(tiny);
            EXPECT_GE(r, 0.0) << tiny;
            EXPECT_LT(r, 256000.0) << tiny;
        }
    }

    TEST(RenderCirrusWind, WrapMorphCountsMorphPeriodsAndWrapsAtTheNoisePeriod)
    {
        EXPECT_NEAR(Cirrus::wrapMorph(0.0), 0.f, 1e-6f);
        EXPECT_NEAR(Cirrus::wrapMorph(600.0), 1.f, 1e-6f);
        EXPECT_NEAR(Cirrus::wrapMorph(300.0), 0.5f, 1e-6f);
        EXPECT_NEAR(Cirrus::wrapMorph(600.0 * 128.0 + 300.0), 0.5f, 1e-4f);
        EXPECT_NEAR(Cirrus::wrapMorph(600.0 * 128.0 * 10.0 + 1200.0), 2.f, 1e-4f);
    }

    TEST(RenderCirrusWind, WrapMorphWrapsNegativeValuesAndIgnoresNonFiniteOnes)
    {
        EXPECT_NEAR(Cirrus::wrapMorph(-600.0), 127.f, 1e-4f);
        EXPECT_EQ(Cirrus::wrapMorph(std::numeric_limits<double>::quiet_NaN()), 0.f);
        EXPECT_EQ(Cirrus::wrapMorph(std::numeric_limits<double>::infinity()), 0.f);
        EXPECT_EQ(Cirrus::wrapMorph(-std::numeric_limits<double>::infinity()), 0.f);
        for (int i = -300; i <= 5000; ++i)
        {
            const float m = Cirrus::wrapMorph(static_cast<double>(i) * 37.3);
            EXPECT_GE(m, 0.f) << i;
            EXPECT_LT(m, 128.f) << i;
        }
    }

    // --- Gates and settings ------------------------------------------------------------------------------------------

    TEST(RenderCirrusGate, NeedsTheSettingHdrAndTheAtmosphere)
    {
        for (int bits = 0; bits < 8; ++bits)
        {
            const bool setting = (bits & 1) != 0;
            const bool hdr = (bits & 2) != 0;
            const bool atmosphere = (bits & 4) != 0;
            EXPECT_EQ(Cirrus::useCirrus(setting, hdr, atmosphere), bits == 7) << bits;
        }
    }

    TEST(RenderCirrusGate, CloudGridsAreNeededByAnyOfTheThreeUsers)
    {
        for (int bits = 0; bits < 8; ++bits)
        {
            const bool shadows = (bits & 1) != 0;
            const bool volumetric = (bits & 2) != 0;
            const bool cirrus = (bits & 4) != 0;
            EXPECT_EQ(Cirrus::needsCloudGrids(shadows, volumetric, cirrus), bits != 0) << bits;
        }
    }

    TEST(RenderCirrusSettings, QualityNamesAreCaseSensitive)
    {
        const auto low = Cirrus::parseQuality("low");
        const auto medium = Cirrus::parseQuality("medium");
        const auto high = Cirrus::parseQuality("high");
        ASSERT_TRUE(low.has_value());
        ASSERT_TRUE(medium.has_value());
        ASSERT_TRUE(high.has_value());
        EXPECT_TRUE(*low == Cirrus::Quality::Low);
        EXPECT_TRUE(*medium == Cirrus::Quality::Medium);
        EXPECT_TRUE(*high == Cirrus::Quality::High);
        for (const char* bad : { "", "Low", "LOW", "Medium", "HIGH", "ultra", " low", "low ", "none" })
        {
            EXPECT_FALSE(Cirrus::parseQuality(bad).has_value()) << bad;
        }
    }

    TEST(RenderCirrusSettings, QualityChoosesOctavesAndWarps)
    {
        EXPECT_EQ(Cirrus::qualityOctaves(Cirrus::Quality::Low), 3);
        EXPECT_EQ(Cirrus::qualityOctaves(Cirrus::Quality::Medium), 4);
        EXPECT_EQ(Cirrus::qualityOctaves(Cirrus::Quality::High), 5);
        EXPECT_EQ(Cirrus::qualityWarps(Cirrus::Quality::Low), 0);
        EXPECT_EQ(Cirrus::qualityWarps(Cirrus::Quality::Medium), 1);
        EXPECT_EQ(Cirrus::qualityWarps(Cirrus::Quality::High), 2);
    }

    TEST(RenderCirrusSettings, DebugViewNamesAreCaseSensitiveAndNumberedInOrder)
    {
        const char* const names[] = { "none", "density", "share", "light", "footprint" };
        for (int i = 0; i < 5; ++i)
        {
            const auto view = Cirrus::parseDebugView(names[i]);
            ASSERT_TRUE(view.has_value()) << names[i];
            EXPECT_EQ(static_cast<int>(*view), i) << names[i];
        }
        EXPECT_EQ(static_cast<int>(Cirrus::DebugView::None), 0);
        EXPECT_EQ(static_cast<int>(Cirrus::DebugView::Density), 1);
        EXPECT_EQ(static_cast<int>(Cirrus::DebugView::Share), 2);
        EXPECT_EQ(static_cast<int>(Cirrus::DebugView::Light), 3);
        EXPECT_EQ(static_cast<int>(Cirrus::DebugView::Footprint), 4);
        for (const char* bad : { "", "None", "DENSITY", "Share", "lights", "normals", "ultra" })
        {
            EXPECT_FALSE(Cirrus::parseDebugView(bad).has_value()) << bad;
        }
    }

    // --- The dome ----------------------------------------------------------------------------------------------------

    vsg::vec3 expectedDomeVertex(std::uint32_t index)
    {
        const std::uint32_t quad = index / 6;
        const std::uint32_t corner = index % 6;
        const std::uint32_t column = quad % static_cast<std::uint32_t>(Cirrus::domeColumns);
        const std::uint32_t row = quad / static_cast<std::uint32_t>(Cirrus::domeColumns);
        const std::uint32_t dc = (corner == 1 || corner == 3 || corner == 4) ? 1u : 0u;
        const std::uint32_t dr = (corner == 2 || corner == 4 || corner == 5) ? 1u : 0u;
        const double below = static_cast<double>(Cirrus::domeBelowHorizonRadians);
        const double azimuth = 2.0 * pi * static_cast<double>(column + dc) / static_cast<double>(Cirrus::domeColumns);
        const double elevation
            = -below + (pi / 2.0 + below) * static_cast<double>(row + dr) / static_cast<double>(Cirrus::domeRows);
        return vsg::vec3(static_cast<float>(std::cos(elevation) * std::cos(azimuth)),
            static_cast<float>(std::cos(elevation) * std::sin(azimuth)), static_cast<float>(std::sin(elevation)));
    }

    std::uint32_t domeIndex(std::uint32_t column, std::uint32_t row, std::uint32_t corner)
    {
        return (row * static_cast<std::uint32_t>(Cirrus::domeColumns) + column) * 6 + corner;
    }

    TEST(RenderCirrusDome, EveryVertexIsAUnitDirectionAboveTheLowestRing)
    {
        const float lowest = std::sin(-Cirrus::domeBelowHorizonRadians);
        for (std::uint32_t i = 0; i < Cirrus::domeVertexCount; ++i)
        {
            const vsg::vec3 v = Cirrus::domeVertex(i);
            EXPECT_NEAR(lengthOf(v), 1.f, 1e-5f) << i;
            EXPECT_GE(v.z, lowest - 1e-6f) << i;
        }
    }

    TEST(RenderCirrusDome, EveryVertexFollowsTheFormula)
    {
        for (std::uint32_t i = 0; i < Cirrus::domeVertexCount; ++i)
        {
            const vsg::vec3 expected = expectedDomeVertex(i);
            const vsg::vec3 v = Cirrus::domeVertex(i);
            EXPECT_LT(distanceBetween(v, expected), 1e-5f) << i;
        }
    }

    TEST(RenderCirrusDome, TheLowestRingIsThreeDegreesBelowTheHorizon)
    {
        const float expected = std::sin(-Cirrus::domeBelowHorizonRadians);
        for (std::uint32_t column = 0; column < static_cast<std::uint32_t>(Cirrus::domeColumns); ++column)
        {
            for (const std::uint32_t corner : { 0u, 1u, 3u })
            {
                const vsg::vec3 v = Cirrus::domeVertex(domeIndex(column, 0, corner));
                EXPECT_NEAR(v.z, expected, 1e-6f) << column << " " << corner;
            }
        }
    }

    TEST(RenderCirrusDome, TheTopRowEndsInTheZenith)
    {
        for (std::uint32_t column = 0; column < static_cast<std::uint32_t>(Cirrus::domeColumns); ++column)
        {
            for (const std::uint32_t corner : { 2u, 4u, 5u })
            {
                const vsg::vec3 v = Cirrus::domeVertex(domeIndex(column, 7, corner));
                EXPECT_NEAR(v.x, 0.f, 1e-5f) << column << " " << corner;
                EXPECT_NEAR(v.y, 0.f, 1e-5f) << column << " " << corner;
                EXPECT_NEAR(v.z, 1.f, 1e-5f) << column << " " << corner;
            }
        }
    }

    TEST(RenderCirrusDome, TheLastColumnMeetsTheFirst)
    {
        for (std::uint32_t row = 0; row < static_cast<std::uint32_t>(Cirrus::domeRows); ++row)
        {
            // Column 31 with a column offset of 1 against column 0 with none, at the lower and the upper edge.
            const vsg::vec3 lowerEnd = Cirrus::domeVertex(domeIndex(31, row, 1));
            const vsg::vec3 lowerStart = Cirrus::domeVertex(domeIndex(0, row, 0));
            EXPECT_LT(distanceBetween(lowerEnd, lowerStart), 1e-5f) << row;
            const vsg::vec3 upperEnd = Cirrus::domeVertex(domeIndex(31, row, 4));
            const vsg::vec3 upperStart = Cirrus::domeVertex(domeIndex(0, row, 2));
            EXPECT_LT(distanceBetween(upperEnd, upperStart), 1e-5f) << row;
        }
    }

    TEST(RenderCirrusDome, TheLastIndexIsValid)
    {
        const vsg::vec3 v = Cirrus::domeVertex(Cirrus::domeVertexCount - 1);
        EXPECT_NEAR(lengthOf(v), 1.f, 1e-5f);
        EXPECT_NEAR(v.z, 1.f, 1e-5f);
        EXPECT_LT(distanceBetween(v, expectedDomeVertex(Cirrus::domeVertexCount - 1)), 1e-5f);
    }

    TEST(RenderCirrusDome, TrianglesHaveAFiniteAreaAndOnlyTheTopRowMayBeDegenerate)
    {
        const std::uint32_t triangles = Cirrus::domeVertexCount / 3;
        for (std::uint32_t t = 0; t < triangles; ++t)
        {
            const vsg::vec3 a = Cirrus::domeVertex(3 * t);
            const vsg::vec3 b = Cirrus::domeVertex(3 * t + 1);
            const vsg::vec3 c = Cirrus::domeVertex(3 * t + 2);
            const vsg::vec3 ab(b.x - a.x, b.y - a.y, b.z - a.z);
            const vsg::vec3 ac(c.x - a.x, c.y - a.y, c.z - a.z);
            const vsg::vec3 cross(ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x);
            const float area = 0.5f * lengthOf(cross);
            EXPECT_TRUE(std::isfinite(area)) << t;
            EXPECT_GE(area, 0.f) << t;
            const std::uint32_t quad = t / 2;
            const std::uint32_t row = quad / static_cast<std::uint32_t>(Cirrus::domeColumns);
            if (row < 7)
            {
                EXPECT_GT(area, 1e-4f) << t;
            }
        }
    }

    TEST(RenderCirrusDome, AQuadHasFourDistinctCornersInTheSpecifiedOrder)
    {
        const std::uint32_t quad = 3 * static_cast<std::uint32_t>(Cirrus::domeColumns) + 10;
        vsg::vec3 corners[6];
        for (std::uint32_t c = 0; c < 6; ++c)
        {
            corners[c] = Cirrus::domeVertex(quad * 6 + c);
        }
        // Corner order (0,0), (1,0), (0,1), (1,0), (1,1), (0,1): the fourth repeats the second, the sixth the third.
        EXPECT_LT(distanceBetween(corners[1], corners[3]), 1e-6f);
        EXPECT_LT(distanceBetween(corners[2], corners[5]), 1e-6f);
        int distinct = 0;
        for (int i = 0; i < 6; ++i)
        {
            bool seen = false;
            for (int j = 0; j < i; ++j)
            {
                if (distanceBetween(corners[i], corners[j]) < 1e-6f)
                {
                    seen = true;
                }
            }
            if (!seen)
            {
                ++distinct;
            }
        }
        EXPECT_EQ(distinct, 4);
        // The (1,1) corner is above and beyond the (0,0) corner.
        EXPECT_GT(corners[4].z, corners[0].z);
        EXPECT_GT(corners[2].z, corners[0].z);
        EXPECT_NEAR(corners[1].z, corners[0].z, 1e-6f);
    }

    // --- The Slang twin ----------------------------------------------------------------------------------------------

    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    float shaderConstant(const std::string& text, const std::string& name)
    {
        const std::regex re("static const float " + name + R"(\s*=\s*([-0-9.eE+]+)\s*;)");
        std::smatch m;
        if (!std::regex_search(text, m, re))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return std::stof(m[1].str());
    }

    int shaderIntConstant(const std::string& text, const std::string& name)
    {
        const std::regex re("static const int " + name + R"(\s*=\s*(-?[0-9]+)\s*;)");
        std::smatch m;
        if (!std::regex_search(text, m, re))
        {
            return std::numeric_limits<int>::min();
        }
        return std::stoi(m[1].str());
    }

    TEST(RenderCirrusShader, RepeatsTheConstantsOfTheCppTwin)
    {
        const auto path = std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / "sky/cirrus/cirrus.slang";
        const std::string text = readFile(path);
        ASSERT_FALSE(text.empty()) << path;
        EXPECT_FLOAT_EQ(shaderConstant(text, "kTauMax"), Cirrus::tauMax);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kFibreStretch"), Cirrus::fibreStretch);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kVeerRadians"), Cirrus::veerRadians);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kCellMeters"), Cirrus::cellMeters);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kMorphSeconds"), Cirrus::morphSeconds);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kAmbientShare"), Cirrus::ambientShare);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kPhaseAnisotropy"), Cirrus::phaseAnisotropy);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kPhaseCap"), Cirrus::phaseCap);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kHorizonCutZ"), Cirrus::horizonCutZ);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kHorizonFadeEndZ"), Cirrus::horizonFadeEndZ);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kSunGain"), Cirrus::sunGain);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kEarthRadiusMeters"), static_cast<float>(Cirrus::earthRadiusMeters));
        EXPECT_FLOAT_EQ(shaderConstant(text, "kBaseCoverage"), Cirrus::baseCoverage);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kPatchScale"), Cirrus::patchScale);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kPatchContrast"), Cirrus::patchContrast);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kTypeLow"), Cirrus::typeLow);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kSwirlScale"), Cirrus::swirlScale);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kSwirlCells"), Cirrus::swirlCells);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kGapEdgeAtZero"), Cirrus::gapEdgeAtZero);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kHookWarpGain"), Cirrus::hookWarpGain);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kTailStrength"), Cirrus::tailStrength);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kTailFloor"), Cirrus::tailFloor);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kVeilAmount"), Cirrus::veilAmount);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kLongAlong"), Cirrus::longAlong);
        EXPECT_FLOAT_EQ(shaderConstant(text, "kTuftAcross"), Cirrus::tuftAcross);
        EXPECT_EQ(shaderIntConstant(text, "kTuftOctaves"), Cirrus::tuftOctaves);
        EXPECT_EQ(shaderIntConstant(text, "kNoisePeriodCells"), Cirrus::noisePeriodCells);
        EXPECT_EQ(shaderIntConstant(text, "kDomeColumns"), Cirrus::domeColumns);
        EXPECT_EQ(shaderIntConstant(text, "kDomeRows"), Cirrus::domeRows);
    }

    TEST(RenderCirrusShader, HoldsBothEntryPointsInOneFile)
    {
        const auto path = std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / "sky/cirrus/cirrus.slang";
        const std::string text = readFile(path);
        ASSERT_FALSE(text.empty()) << path;
        EXPECT_NE(text.find("vertMain"), std::string::npos);
        EXPECT_NE(text.find("fragMain"), std::string::npos);
    }

    TEST(RenderCirrusShader, DoesNotReadAClock)
    {
        const auto path = std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / "sky/cirrus/cirrus.slang";
        const std::string text = readFile(path);
        ASSERT_FALSE(text.empty()) << path;
        EXPECT_EQ(text.find("clock"), std::string::npos);
    }

    // A small SPIR-V reader: the descriptor (set, binding) pairs, the entry point name and push constants.
    struct SpirvInfo
    {
        std::set<std::pair<uint32_t, uint32_t>> bindings;
        std::string entry;
        bool hasPushConstant = false;
        uint32_t version = 0;
        bool valid = false;
    };

    std::string literalString(const std::vector<uint32_t>& w, std::size_t from, std::size_t to)
    {
        std::string name;
        for (std::size_t k = from; k < to; ++k)
        {
            for (int b = 0; b < 4; ++b)
            {
                const char c = static_cast<char>((w[k] >> (8 * b)) & 0xFF);
                if (c == 0)
                {
                    return name;
                }
                name += c;
            }
        }
        return name;
    }

    SpirvInfo readSpirv(const std::filesystem::path& path)
    {
        SpirvInfo result;
        std::ifstream in(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() < 20 || bytes.size() % 4 != 0)
        {
            return result;
        }
        std::vector<uint32_t> w(bytes.size() / 4);
        std::memcpy(w.data(), bytes.data(), bytes.size());
        if (w[0] != 0x07230203)
        {
            return result;
        }
        result.version = w[1];
        std::map<uint32_t, uint32_t> sets, binds;
        for (std::size_t i = 5; i < w.size();)
        {
            const uint32_t op = w[i] & 0xFFFF, count = w[i] >> 16;
            if (count == 0 || i + count > w.size())
            {
                return result;
            }
            if (op == 15 && count >= 4) // OpEntryPoint: model, id, name...
            {
                result.entry = literalString(w, i + 3, i + count);
            }
            if (op == 71 && count >= 4) // OpDecorate: target, decoration, value
            {
                if (w[i + 2] == 34)
                {
                    sets[w[i + 1]] = w[i + 3];
                }
                if (w[i + 2] == 33)
                {
                    binds[w[i + 1]] = w[i + 3];
                }
            }
            if (op == 59 && count >= 4 && w[i + 3] == 9) // OpVariable in the PushConstant storage class
            {
                result.hasPushConstant = true;
            }
            i += count;
        }
        for (const auto& [id, set] : sets)
        {
            if (binds.count(id) != 0)
            {
                result.bindings.insert({ set, binds[id] });
            }
        }
        result.valid = true;
        return result;
    }

#if OPENEDEN_SLANG_ENABLED
    constexpr bool slangBuilt = true;
#else
    constexpr bool slangBuilt = false;
#endif

    class CirrusSpirv : public ::testing::TestWithParam<std::string>
    {
    };

    TEST_P(CirrusSpirv, UsesOnlyTheCirrusDescriptorsAndTheStandardEntryPoint)
    {
        if (!slangBuilt)
        {
            GTEST_SKIP() << "this build has no slangc";
        }
        const auto path
            = std::filesystem::path(OPENEDEN_SHADER_BUILD_DIR) / "sky/cirrus" / ("main." + GetParam() + ".spv");
        const SpirvInfo spirv = readSpirv(path);
        ASSERT_TRUE(spirv.valid) << path;
        EXPECT_EQ(spirv.version, 0x00010000u);
        EXPECT_EQ(spirv.entry, "main");
        // The layer's layout: the scene block (set 0, binding 0) and the material (set 2, binding 1).
        const std::set<std::pair<uint32_t, uint32_t>> allowed{ { 0, 0 }, { 2, 1 } };
        for (const auto& b : spirv.bindings)
        {
            EXPECT_TRUE(allowed.count(b) != 0) << "set " << b.first << " binding " << b.second;
        }
        if (GetParam() == "vert")
        {
            EXPECT_TRUE(spirv.hasPushConstant) << "the vertex stage takes the projection and modelview";
        }
        else
        {
            EXPECT_TRUE(spirv.bindings.count({ 2, 1 }) != 0) << "the fragment stage must read the material";
            EXPECT_TRUE(spirv.bindings.count({ 0, 0 }) != 0) << "the fragment stage must read the scene";
        }
    }

    INSTANTIATE_TEST_SUITE_P(Stages, CirrusSpirv, ::testing::Values("vert", "frag"));
}
