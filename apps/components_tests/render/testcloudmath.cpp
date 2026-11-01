// Tests for the cloud maths, Render::Cloud (task 0038, stage a). Written from the task spec before the
// implementation: thickness limits, the lit look (edge glow towards the sun, the silver lining only with the sun
// behind the cloud, dark undersides), the dome's geometry, the shadow factor and its direction, and the grid.
// Pure CPU maths: no device, window or game data.

#include <components/render/cloudgpu.hpp>
#include <components/render/cloudmath.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

namespace
{
    using namespace Render::Cloud;

    TEST(RenderCloud, settingsNeedHdrAndShadowsAlsoTheAtmosphere)
    {
        EXPECT_FALSE(useClouds(true, false));
        EXPECT_FALSE(useClouds(false, true));
        EXPECT_TRUE(useClouds(true, true));
        EXPECT_FALSE(useShadows(true, true, false));
        EXPECT_FALSE(useShadows(true, false, true));
        EXPECT_FALSE(useShadows(false, true, true));
        EXPECT_TRUE(useShadows(true, true, true));
    }

    TEST(RenderCloud, lookDefaultsToLit)
    {
        EXPECT_EQ(parseLook("flat"), Look::Flat);
        EXPECT_EQ(parseLook("lit"), Look::Lit);
        EXPECT_EQ(parseLook(""), Look::Lit);
        EXPECT_EQ(parseLook("nonsense"), Look::Lit);
    }

    TEST(RenderCloud, thicknessLimits)
    {
        EXPECT_FLOAT_EQ(thickness(0.5f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(thickness(0.f, 1.f), 1.f);
        for (float b = 0.f; b <= 1.f; b += 0.1f)
            for (float a = 0.f; a <= 1.f; a += 0.1f)
            {
                EXPECT_GE(thickness(b, a), 0.f);
                EXPECT_LE(thickness(b, a), 1.f);
            }
        // Out of range input and NaN stay in range.
        EXPECT_FLOAT_EQ(thickness(-3.f, 7.f), 1.f);
        EXPECT_FLOAT_EQ(thickness(std::nanf(""), 0.5f), thickness(0.f, 0.5f));
    }

    TEST(RenderCloud, brighterMeansThinnerAndMoreAlphaMeansThicker)
    {
        EXPECT_GT(thickness(0.3f, 0.6f), thickness(0.8f, 0.6f));
        EXPECT_GT(thickness(0.55f, 0.9f), thickness(0.55f, 0.4f));
        // At the pivot the alpha is the thickness.
        EXPECT_NEAR(thickness(brightnessPivot, 0.4f), 0.4f, 1e-6f);
    }

    TEST(RenderCloud, luminanceOfGreyIsGrey)
    {
        EXPECT_NEAR(luminance(0.5f, 0.5f, 0.5f), 0.5f, 1e-5f);
        EXPECT_FLOAT_EQ(luminance(0.f, 0.f, 0.f), 0.f);
    }

    TEST(RenderCloud, transmittanceFallsFromOne)
    {
        EXPECT_FLOAT_EQ(transmittance(0.f), 1.f);
        EXPECT_LT(transmittance(1.f), transmittance(0.5f));
        EXPECT_NEAR(transmittance(1.f), std::exp(-extinction), 1e-6f);
        EXPECT_GT(transmittance(1.f), 0.f);
    }

    TEST(RenderCloud, forwardPhasePeaksAtTheSun)
    {
        EXPECT_NEAR(forwardPhase(1.f), 1.f, 1e-5f);
        EXPECT_LT(forwardPhase(0.f), 0.2f);
        EXPECT_LT(forwardPhase(-1.f), forwardPhase(0.f));
        float last = 2.f;
        for (float c = 1.f; c >= -1.f; c -= 0.1f)
        {
            EXPECT_LE(forwardPhase(c), last + 1e-6f);
            last = forwardPhase(c);
        }
        // Rounding past the unit range is clamped.
        EXPECT_NEAR(forwardPhase(1.0001f), 1.f, 1e-5f);
    }

    TEST(RenderCloud, silverLiningOnlyWithTheSunBehindAndOnThinCloud)
    {
        // Looking at the sun (cos = 1) the thin cloud glows; looking away it hardly does.
        EXPECT_GT(silverLining(0.1f, 1.f), 0.5f);
        EXPECT_LT(silverLining(0.1f, -1.f), 0.05f);
        // Thick cloud blocks the lining.
        EXPECT_GT(silverLining(0.1f, 1.f), silverLining(1.f, 1.f));
    }

    TEST(RenderCloud, undersideShadeGrowsWithThicknessAndFallsAsTheSunSinks)
    {
        EXPECT_FLOAT_EQ(undersideShade(0.f, 1.f), 0.f);
        EXPECT_GT(undersideShade(1.f, 1.f), undersideShade(0.3f, 1.f));
        EXPECT_GT(undersideShade(1.f, 1.f), undersideShade(1.f, 0.f));
        EXPECT_LE(undersideShade(1.f, 1.f), maxShade);
        EXPECT_GT(undersideShade(1.f, -1.f), 0.f);
    }

    TEST(RenderCloud, litMultiplierIsOneWhenOff)
    {
        for (float t : { 0.f, 0.4f, 1.f })
            for (float c : { -1.f, 0.f, 1.f })
                EXPECT_FLOAT_EQ(litMultiplier(t, c, 0.7f, 0.f), 1.f);
    }

    TEST(RenderCloud, edgesTowardsTheSunAreBrighterThanAwayAndThickIsDarkerThanThin)
    {
        const float towards = litMultiplier(0.1f, 0.99f, 0.8f, 1.f);
        const float away = litMultiplier(0.1f, -0.5f, 0.8f, 1.f);
        EXPECT_GT(towards, away);
        // A glowing edge is brighter than the unlit picture, a thick underside darker.
        EXPECT_GT(towards, 1.f);
        const float thickAway = litMultiplier(1.f, -0.5f, 0.8f, 1.f);
        EXPECT_LT(thickAway, 1.f);
        EXPECT_LT(thickAway, away);
        EXPECT_GT(thickAway, 0.f);
    }

    TEST(RenderCloud, litMultiplierBlendsWithTheAmount)
    {
        const float full = litMultiplier(1.f, -0.5f, 0.8f, 1.f);
        const float half = litMultiplier(1.f, -0.5f, 0.8f, 0.5f);
        EXPECT_NEAR(half, 0.5f * (1.f + full), 1e-6f);
        EXPECT_FLOAT_EQ(litMultiplier(1.f, -0.5f, 0.8f, 9.f), full);
    }

    TEST(RenderCloud, strengthNeedsLightAndAWeatherThatHasSun)
    {
        EXPECT_FLOAT_EQ(strength(0.01f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(strength(2.f, 1.f), 1.f);
        EXPECT_FLOAT_EQ(strength(2.f, 0.f), 0.f);
        const float mid = strength((lightFrom + lightTo) * 0.5f, 1.f);
        EXPECT_GT(mid, 0.f);
        EXPECT_LT(mid, 1.f);
    }

    TEST(RenderCloud, weatherShareStormsKeepTheirLookOvercastShadesLess)
    {
        EXPECT_FLOAT_EQ(weatherShare(1.f, true), 0.f);
        EXPECT_FLOAT_EQ(weatherShare(1.f, false), 1.f);
        EXPECT_FLOAT_EQ(weatherShare(0.f, false), minWeatherShare);
        EXPECT_GT(weatherShare(1.f, false), weatherShare(0.25f, false));
        EXPECT_FLOAT_EQ(weatherShare(5.f, false), 1.f);
    }

    TEST(RenderCloud, domePointAtTheZenithIsTheCentre)
    {
        const vsg::vec2 p = domePoint(vsg::vec3(0.f, 0.f, 1.f));
        EXPECT_FLOAT_EQ(p.x, 0.f);
        EXPECT_FLOAT_EQ(p.y, 0.f);
    }

    TEST(RenderCloud, domePointFollowsTheRayAndStaysOnTheRim)
    {
        // A ray towards +Y at 45 degrees meets the dome where its height equals its distance: between the second ring
        // (275, 288) and the third (462, 254).
        const float s = std::sqrt(0.5f);
        const vsg::vec2 p = domePoint(vsg::vec3(0.f, s, s));
        EXPECT_NEAR(p.x, 0.f, 1e-3f);
        EXPECT_GT(p.y, 275.f);
        EXPECT_LT(p.y, 462.f);
        // Direction sign: towards -X gives negative X.
        EXPECT_LT(domePoint(vsg::vec3(-s, 0.f, s)).x, 0.f);
        // Below the rim's slope, the point stays on the rim.
        const vsg::vec2 low = domePoint(vsg::vec3(1.f, 0.f, 0.f));
        EXPECT_NEAR(low.x, 1000.f, 1e-3f);
        const vsg::vec2 below = domePoint(vsg::vec3(0.f, 1.f, -0.5f));
        EXPECT_NEAR(below.y, 1000.f, 1e-3f);
    }

    TEST(RenderCloud, domePointDistanceGrowsAsTheRayDrops)
    {
        float last = -1.f;
        for (float elevation = 1.4f; elevation > 0.05f; elevation -= 0.05f)
        {
            const vsg::vec2 p = domePoint(vsg::vec3(std::cos(elevation), 0.f, std::sin(elevation)));
            EXPECT_GE(p.x, last);
            last = p.x;
        }
    }

    TEST(RenderCloud, domePointMatchesAMeshVertex)
    {
        // Vertex 18 of the mesh: (-182.336, 426.665, 254.262), read from the file.
        const vsg::vec3 v(-182.336f, 426.665f, 254.262f);
        const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        const vsg::vec2 p = domePoint(vsg::vec3(v.x / len, v.y / len, v.z / len));
        // The mesh's rings are polygons: radii of one ring differ by a few units.
        EXPECT_NEAR(p.x, v.x, 3.f);
        EXPECT_NEAR(p.y, v.y, 3.f);
    }

    TEST(RenderCloud, domeUvIsTheMeshsProjection)
    {
        // Vertex 50 of the mesh: x = -382.683 gives u = 1.5442, y = 923.879 gives v = -3.8074.
        const vsg::vec2 uv = domeUv(vsg::vec2(-382.683f, 923.879f), 0.f);
        EXPECT_NEAR(uv.x, 1.5442f, 1e-3f);
        EXPECT_NEAR(uv.y, -3.8074f, 1e-3f);
        // The scroll is taken off v.
        EXPECT_NEAR(domeUv(vsg::vec2(0.f, 0.f), 0.25f).y, -1.75f, 1e-6f);
        EXPECT_NEAR(domeUv(vsg::vec2(0.f, 0.f), 0.f).x, 2.5f, 1e-6f);
    }

    TEST(RenderCloud, shadowDirectionUnderTheCameraIsTheSun)
    {
        const vsg::vec3 sun(0.3f, 0.4f, std::sqrt(1.f - 0.09f - 0.16f));
        const vsg::vec3 d = shadowDirection(vsg::vec3(10.f, 20.f, 5.f), sun, vsg::vec3(10.f, 20.f, 5.f));
        EXPECT_NEAR(d.x, sun.x, 1e-5f);
        EXPECT_NEAR(d.y, sun.y, 1e-5f);
        EXPECT_NEAR(d.z, sun.z, 1e-5f);
    }

    TEST(RenderCloud, shadowDirectionIsAUnitVectorAndTiltsAwayFromTheCameraGroundPoint)
    {
        const vsg::vec3 sun(0.f, 0.f, 1.f);
        const vsg::vec3 d = shadowDirection(vsg::vec3(6000.f, 0.f, 0.f), sun, vsg::vec3(0.f, 0.f, 0.f));
        EXPECT_NEAR(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), 1.f, 1e-5f);
        // With the sun overhead, the cloud shading a point 6000 units east of the camera is east of the camera.
        EXPECT_GT(d.x, 0.f);
        EXPECT_NEAR(d.y, 0.f, 1e-6f);
        // The farther the ground point, the lower the direction.
        const vsg::vec3 far = shadowDirection(vsg::vec3(30000.f, 0.f, 0.f), sun, vsg::vec3(0.f, 0.f, 0.f));
        EXPECT_GT(far.x, d.x);
    }

    TEST(RenderCloud, shadowDirectionSurvivesAHorizonSun)
    {
        const vsg::vec3 d
            = shadowDirection(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(1.f, 0.f, 0.f), vsg::vec3(0.f, 0.f, 0.f));
        EXPECT_TRUE(std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z));
        EXPECT_NEAR(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), 1.f, 1e-4f);
    }

    TEST(RenderCloud, sunFadeRisesWithTheSun)
    {
        EXPECT_FLOAT_EQ(sunFade(-0.5f), 0.f);
        EXPECT_FLOAT_EQ(sunFade(sunUpFrom), 0.f);
        EXPECT_FLOAT_EQ(sunFade(sunUpTo), 1.f);
        EXPECT_FLOAT_EQ(sunFade(1.f), 1.f);
        EXPECT_GT(sunFade(0.2f), sunFade(0.1f));
    }

    TEST(RenderCloud, shadowFactorStaysInZeroToOne)
    {
        for (float t = -0.5f; t <= 1.5f; t += 0.1f)
            for (float a = -0.5f; a <= 1.5f; a += 0.1f)
            {
                const float f = shadowFactor(t, a);
                EXPECT_GE(f, 0.f);
                EXPECT_LE(f, 1.f);
            }
        EXPECT_FLOAT_EQ(shadowFactor(std::nanf(""), 1.f), 1.f);
    }

    TEST(RenderCloud, shadowFactorIsOneWithoutCloudOrStrength)
    {
        EXPECT_FLOAT_EQ(shadowFactor(0.f, 1.f), 1.f);
        EXPECT_FLOAT_EQ(shadowFactor(1.f, 0.f), 1.f);
    }

    TEST(RenderCloud, shadowFactorDarkensWithThicknessAndStrength)
    {
        EXPECT_LT(shadowFactor(1.f, 1.f), shadowFactor(0.3f, 1.f));
        EXPECT_LT(shadowFactor(1.f, 1.f), shadowFactor(1.f, 0.5f));
        EXPECT_NEAR(shadowFactor(1.f, 1.f), std::exp(-shadowExtinction), 1e-6f);
    }

    TEST(RenderCloud, gridOfAUniformPictureIsUniform)
    {
        // 16 by 16 pixels, BGRA: alpha 128, grey at the pivot (so thickness is the alpha).
        std::vector<unsigned char> px(16 * 16 * 4);
        for (std::size_t i = 0; i < 16 * 16; ++i)
        {
            const unsigned char grey = static_cast<unsigned char>(std::lround(brightnessPivot * 255.f));
            px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = grey;
            px[i * 4 + 3] = 128;
        }
        std::vector<float> grid(gridSize * gridSize);
        buildGrid(px.data(), 16, 16, true, grid.data());
        for (float v : grid)
            EXPECT_NEAR(v, 128.f / 255.f, 0.01f);
    }

    TEST(RenderCloud, gridKeepsTheRowOrderAndTheColourOrder)
    {
        // 64 by 64: the top half is opaque, the bottom half clear. Row 0 is first in memory.
        std::vector<unsigned char> px(64 * 64 * 4, 0);
        for (std::size_t y = 0; y < 32; ++y)
            for (std::size_t x = 0; x < 64; ++x)
                px[(y * 64 + x) * 4 + 3] = 255;
        std::vector<float> grid(gridSize * gridSize);
        buildGrid(px.data(), 64, 64, true, grid.data());
        EXPECT_GT(grid[0], 0.9f);
        EXPECT_NEAR(grid[(gridSize - 1) * gridSize], 0.f, 1e-6f);

        // 255 in the first byte is blue (weight 0.0722) as BGRA and red (0.2126) as RGBA, so the thickness of a
        // translucent pixel is lower for the RGBA reading (a brighter pixel is thinner).
        std::vector<unsigned char> one(4, 0);
        one[0] = 255;
        one[3] = 100;
        std::vector<float> g1(gridSize * gridSize), g2(gridSize * gridSize);
        buildGrid(one.data(), 1, 1, true, g1.data());
        buildGrid(one.data(), 1, 1, false, g2.data());
        EXPECT_GT(g1[0], g2[0]);
    }

    TEST(RenderCloud, sampleGridWrapsAndInterpolates)
    {
        std::vector<float> grid(gridSize * gridSize, 0.f);
        grid[0] = 1.f;
        const float n = static_cast<float>(gridSize);
        // At the centre of cell (0, 0) the value is the cell's.
        EXPECT_NEAR(sampleGrid(grid.data(), 0.5f / n, 0.5f / n), 1.f, 1e-5f);
        // Half a cell on, it is halfway to the neighbour.
        EXPECT_NEAR(sampleGrid(grid.data(), 1.f / n, 0.5f / n), 0.5f, 1e-5f);
        // Wrapping: one repeat later is the same; so is a negative coordinate's.
        EXPECT_NEAR(sampleGrid(grid.data(), 1.f + 0.5f / n, 3.f + 0.5f / n), 1.f, 1e-4f);
        EXPECT_NEAR(sampleGrid(grid.data(), 0.f, 0.5f / n), 0.5f, 1e-5f);
        EXPECT_NEAR(sampleGrid(grid.data(), -2.f + 0.5f / n, -5.f + 0.5f / n), 1.f, 1e-4f);
    }

    TEST(RenderCloud, sampleGridStaysInTheRangeOfItsCells)
    {
        std::vector<float> grid(gridSize * gridSize);
        for (std::size_t i = 0; i < grid.size(); ++i)
            grid[i] = static_cast<float>(i % 7) / 6.f;
        for (float u = -1.3f; u < 2.f; u += 0.173f)
            for (float v = -2.1f; v < 2.f; v += 0.211f)
            {
                const float s = sampleGrid(grid.data(), u, v);
                EXPECT_GE(s, -1e-6f);
                EXPECT_LE(s, 1.f + 1e-6f);
            }
    }

    TEST(RenderCloud, shadowAndSkyAgreeAtTheCamera)
    {
        // The cloud overhead in the sun's direction and the cloud that shades the ground point under the camera are
        // the same pixel of the picture (acceptance: the shadow agrees with the cloud overhead).
        const vsg::vec3 sun(0.2f, -0.3f, std::sqrt(1.f - 0.04f - 0.09f));
        const vsg::vec2 sky = domeUv(domePoint(sun), 0.37f);
        const vsg::vec3 d = shadowDirection(vsg::vec3(100.f, 100.f, 0.f), sun, vsg::vec3(100.f, 100.f, 0.f));
        const vsg::vec2 ground = domeUv(domePoint(d), 0.37f);
        EXPECT_NEAR(sky.x, ground.x, 1e-5f);
        EXPECT_NEAR(sky.y, ground.y, 1e-5f);
    }

    TEST(RenderCloud, shadowMovesWithTheScroll)
    {
        const vsg::vec3 sun(0.f, 0.f, 1.f);
        const vsg::vec3 origin(0.f, 0.f, 0.f);
        const vsg::vec2 a = domeUv(domePoint(shadowDirection(origin, sun, origin)), 0.f);
        const vsg::vec2 b = domeUv(domePoint(shadowDirection(origin, sun, origin)), 0.1f);
        EXPECT_NEAR(a.y - b.y, 0.1f, 1e-6f);
        EXPECT_NEAR(a.x, b.x, 1e-6f);
    }

    TEST(RenderCloudGpu, headerExtendsTheSkyHeaderWithoutOverlap)
    {
        EXPECT_EQ(headerTotalVec4s, Render::Gi::skyHeaderVec4s + 8 + 2 * 1024);
        EXPECT_EQ(gridVec4s, 1024u);
    }

    TEST(RenderCloudGpu, paramsAreWrittenAfterTheSkysEntriesOnly)
    {
        std::vector<vsg::vec4> header(headerTotalVec4s, vsg::vec4(-7.f, -7.f, -7.f, -7.f));
        Params p;
        p.strength = 0.5f;
        p.blend = 0.25f;
        p.scroll = 3.f;
        p.hasLayer[0] = true;
        p.rotation[1] = { 0.f, 1.f, 0.f, -1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
        writeParams(header.data(), p);
        for (std::size_t i = 0; i < Render::Gi::skyHeaderVec4s; ++i)
            EXPECT_EQ(header[i].x, -7.f) << i;
        const vsg::vec4* base = header.data() + Render::Gi::skyHeaderVec4s;
        EXPECT_EQ(base[0].x, 0.5f);
        EXPECT_EQ(base[0].y, 0.25f);
        EXPECT_EQ(base[0].z, 3.f);
        EXPECT_EQ(base[1].x, 1.f); // the first layer's first column
        EXPECT_EQ(base[4].y, 1.f); // the second layer's first column is (0, 1, 0)
        EXPECT_EQ(base[5].x, -1.f);
        EXPECT_EQ(base[7].x, 1.f);
        EXPECT_EQ(base[7].y, 0.f);
        // The grids are untouched.
        EXPECT_EQ(base[8].x, -7.f);
    }

    TEST(RenderCloudGpu, gridRoundTripsThroughTheHeader)
    {
        std::vector<vsg::vec4> header(headerTotalVec4s);
        std::vector<float> a(gridSize * gridSize), b(gridSize * gridSize);
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            a[i] = static_cast<float>(i) / 5000.f;
            b[i] = 1.f - a[i];
        }
        writeGrid(header.data(), 0, a.data());
        writeGrid(header.data(), 1, b.data());
        for (std::size_t i = 0; i < a.size(); i += 37)
        {
            EXPECT_EQ(readGridCell(header.data(), 0, i), a[i]);
            EXPECT_EQ(readGridCell(header.data(), 1, i), b[i]);
        }
        writeGrid(header.data(), 1, nullptr);
        EXPECT_EQ(readGridCell(header.data(), 1, 100), 0.f);
        EXPECT_EQ(readGridCell(header.data(), 0, 100), a[100]);
    }
}
