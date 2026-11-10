// Tests for the sky environment image, Render::Gi (task 0036, stage d). Written from the task spec before the
// implementation: a small octahedral map of the current sky that exterior surfaces reflect.

#include <components/render/reflectiongpu.hpp>
#include <components/render/reflectionmath.hpp>
#include <components/render/skyenvmath.hpp>
#include <components/render/skyvismath.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using namespace Render::Gi;

    const vsg::vec3 zenith(0.2f, 0.4f, 0.9f);
    const vsg::vec3 horizon(0.8f, 0.7f, 0.5f);
    const vsg::vec3 ground(0.1f, 0.1f, 0.08f);

    void expectNear(const vsg::vec3& a, const vsg::vec3& b, float tol)
    {
        EXPECT_NEAR(a.x, b.x, tol);
        EXPECT_NEAR(a.y, b.y, tol);
        EXPECT_NEAR(a.z, b.z, tol);
    }
}

TEST(SkyEnv, RadianceGoesFromHorizonToZenithAndBelowIsGround)
{
    expectNear(skyRadiance(zenith, horizon, ground, vsg::vec3(0, 0, 1)), zenith, 1e-6f);
    expectNear(skyRadiance(zenith, horizon, ground, vsg::vec3(1, 0, 0)), horizon, 1e-6f);
    expectNear(skyRadiance(zenith, horizon, ground, vsg::vec3(0, 0, -1)), ground, 1e-6f);
    expectNear(skyRadiance(zenith, horizon, ground, vsg::vec3(0, 3, 4)), horizon + (zenith - horizon) * 0.8f, 1e-5f);
    // Any length of direction works, and a zero one does not break it.
    expectNear(skyRadiance(zenith, horizon, ground, vsg::vec3(0, 0, 5)), zenith, 1e-6f);
    const vsg::vec3 z = skyRadiance(zenith, horizon, ground, vsg::vec3(0, 0, 0));
    EXPECT_TRUE(std::isfinite(z.x) && std::isfinite(z.y) && std::isfinite(z.z));
}

TEST(SkyEnv, RadianceMatchesTheSkyHarmonics)
{
    // The same sky as projectSky: the constant term of the map's average is the SH constant term.
    const auto probe = makeSkyEnvironmentProbe(zenith, horizon, ground, 32);
    ASSERT_EQ(probe.texels.size(), 32u * 32u);
    // The mean of the texels is not the mean over the sphere (an octahedral map is not equal-area), but each texel
    // must equal the radiance in its direction.
    for (std::uint32_t y = 0; y < 32; y += 5)
        for (std::uint32_t x = 0; x < 32; x += 7)
            expectNear(probe.texels[y * 32 + x],
                skyRadiance(zenith, horizon, ground, reflectionTexelDirection(32, x, y)), 1e-5f);
}

TEST(SkyEnv, TheMapReadsBackThroughTheReflectionMaths)
{
    const auto probe = makeSkyEnvironmentProbe(zenith, horizon, ground, 32);
    const auto chain = buildReflectionChain(probe.texels, 32);
    // Mirror sharp: straight up is the zenith, straight down the ground (the octahedral fold is exact there, within
    // the bilinear read of a texel).
    expectNear(sampleReflectionChain(chain.data(), 32, vsg::vec3(0, 0, 1), 0.f), zenith, 0.05f);
    expectNear(sampleReflectionChain(chain.data(), 32, vsg::vec3(0, 0, -1), 0.f), ground, 0.05f);
    // Rough: blurrier, still between the colours.
    const vsg::vec3 rough = sampleReflectionChain(chain.data(), 32, vsg::vec3(1, 0, 0), 1.f);
    EXPECT_GT(rough.x, ground.x);
    EXPECT_LT(rough.x, horizon.x + 0.01f);
}

TEST(SkyEnv, AUniformSkyIsAUniformMap)
{
    const vsg::vec3 c(0.3f, 0.3f, 0.3f);
    const auto probe = makeSkyEnvironmentProbe(c, c, c, 16);
    for (const vsg::vec3& t : probe.texels)
        expectNear(t, c, 1e-6f);
}

TEST(SkyEnv, TheProbeIsValidAndPacksAsOneEnabledProbe)
{
    ReflectionSet set;
    set.cell = CellId{ false, "", 0, 0 };
    set.samples = 1;
    set.resolution = 32;
    set.ambient = vsg::vec3(0.1f, 0.1f, 0.1f);
    set.probes.push_back(makeSkyEnvironmentProbe(zenith, horizon, ground, 32));
    EXPECT_EQ(validate(set), "");
    const auto packed = packReflections(set, 0.8f);
    EXPECT_EQ(packed.size(), reflectionPackedSize(1, 32));
    EXPECT_EQ(packed[0].x, 1.f);
    EXPECT_EQ(packed[0].w, 1.f);
    EXPECT_EQ(packed[1].x, 0.8f);
}

TEST(SkyEnv, ChangeIsNoticedOnlyBeyondTheTolerance)
{
    EXPECT_FALSE(skyEnvironmentChanged(zenith, horizon, zenith, horizon, 0.004f));
    EXPECT_FALSE(skyEnvironmentChanged(zenith, horizon, zenith + vsg::vec3(0.003f, 0.f, 0.f), horizon, 0.004f));
    EXPECT_TRUE(skyEnvironmentChanged(zenith, horizon, zenith + vsg::vec3(0.f, 0.f, 0.005f), horizon, 0.004f));
    EXPECT_TRUE(skyEnvironmentChanged(zenith, horizon, zenith, horizon + vsg::vec3(0.f, -0.01f, 0.f), 0.004f));
}

// The glaze fix: the sky environment image is a one-probe picture of the whole sky, so it must only light a surface that
// can see the sky in the mirror direction, and a rough dielectric must get little of it. The GLSL and Slang twins are
// skyReflectionFactor in lib/view/skyvis.{glsl,slang}.
TEST(SkyReflection, RoughnessWeightKeepsSmoothAndMetalAndDropsRoughDielectrics)
{
    EXPECT_FLOAT_EQ(skyReflectionRoughnessWeight(0.f, 0.f), 1.f);
    EXPECT_NEAR(skyReflectionRoughnessWeight(0.5f, 0.f), 0.25f, 1e-6f);
    EXPECT_NEAR(skyReflectionRoughnessWeight(1.f, 0.f), 0.f, 1e-6f);
    EXPECT_NEAR(skyReflectionRoughnessWeight(0.9f, 0.f), 0.01f, 1e-5f);
    // A metal keeps its reflection at any roughness; half a metal is half way.
    EXPECT_FLOAT_EQ(skyReflectionRoughnessWeight(0.9f, 1.f), 1.f);
    EXPECT_NEAR(skyReflectionRoughnessWeight(1.f, 0.5f), 0.5f, 1e-6f);
    // Out of range inputs are clamped.
    EXPECT_FLOAT_EQ(skyReflectionRoughnessWeight(-1.f, 0.f), 1.f);
    EXPECT_FLOAT_EQ(skyReflectionRoughnessWeight(2.f, 2.f), 1.f);
}

TEST(SkyReflection, DirectionVisibilityIsOpenGroundOneAndBlockedZero)
{
    const VisSh open = hemisphereVisibility();
    EXPECT_NEAR(skyDirectionVisibility(open, vsg::vec3(0.f, 0.f, 1.f)), 1.f, 1e-4f);
    EXPECT_NEAR(skyDirectionVisibility(open, vsg::vec3(1.f, 0.f, 0.f)), 1.f, 1e-3f);
    EXPECT_NEAR(skyDirectionVisibility(blockedVisibility(), vsg::vec3(0.f, 0.f, 1.f)), 0.f, 1e-6f);
    EXPECT_NEAR(skyDirectionVisibility(blockedVisibility(), vsg::vec3(1.f, 0.f, 0.f)), 0.f, 1e-6f);
    // A zero direction falls back to the omnidirectional share.
    EXPECT_NEAR(skyDirectionVisibility(open, vsg::vec3(0.f, 0.f, 0.f)), 1.f, 1e-4f);
}

TEST(SkyReflection, SheltersFromTheRoofAbove)
{
    // Sees the sky only below the horizon plane tilt: V = 1 for z < 0 (a roof over the point): looking up gives ~0.
    VisSh roofed{};
    roofed[0] = 1.7724539f;
    roofed[2] = -0.488603f * 3.14159265f;
    EXPECT_LT(skyDirectionVisibility(roofed, vsg::vec3(0.f, 0.f, 1.f)), 0.05f);
}

TEST(SkyReflection, FactorCombinesVisibilityDefaultAndRoughness)
{
    // Baked data fully present: visibility times the roughness weight.
    EXPECT_NEAR(skyReflectionFactor(0.8f, 1.f, 0.f, 0.f), 0.8f, 1e-6f);
    EXPECT_NEAR(skyReflectionFactor(0.8f, 1.f, 0.5f, 0.f), 0.2f, 1e-6f);
    EXPECT_NEAR(skyReflectionFactor(0.8f, 1.f, 1.f, 1.f), 0.8f, 1e-6f);
    // No baked data at all: the conservative default, not the old behaviour of 1.
    EXPECT_NEAR(skyReflectionFactor(1.f, 0.f, 0.f, 0.f), skyReflectionUnbakedVisibility, 1e-6f);
    EXPECT_LT(skyReflectionUnbakedVisibility, 0.5f);
    // Half the data: half way between the default and the sampled value.
    EXPECT_NEAR(skyReflectionFactor(1.f, 0.5f, 0.f, 0.f), 0.5f * (1.f + skyReflectionUnbakedVisibility), 1e-6f);
    // A rough dark stone wall in an overcast open yard keeps almost none of the sky image.
    EXPECT_LT(skyReflectionFactor(1.f, 1.f, 0.85f, 0.f), 0.03f);
}
