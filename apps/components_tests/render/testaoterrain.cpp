// Tests for the terrain's ambient occlusion maths, Render::Ao::terrainAmbientPart and useTerrainAo (task 0048, stage a).
// Written from the task spec before the implementation.

#include <components/render/aomath.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace Render::Ao
{
    namespace
    {
        constexpr vsg::vec3 black{ 0.f, 0.f, 0.f };

        // The terrain shader's lighting multiply, as CPU code: texture colour times (ambient light times the ambient
        // colour term times the ambient scale plus the diffuse light times the diffuse colour times the diffuse scale).
        // This is what materialTermsScaled adds up (files/shaders/lib/material/terms.glsl), without the clamp.
        float litChannel(float texture, float ambientLight, float ambientColour, float ambientScale, float diffuseLight,
            float diffuseColour, float diffuseScale)
        {
            return texture * (ambientLight * ambientColour * ambientScale + diffuseLight * diffuseColour * diffuseScale);
        }

        vsg::vec3 lit(const vsg::vec3& texture, const vsg::vec3& ambientLight, const vsg::vec3& ambientColour,
            float ambientScale, const vsg::vec3& diffuseLight, const vsg::vec3& diffuseColour, float diffuseScale)
        {
            return vsg::vec3(
                litChannel(texture.x, ambientLight.x, ambientColour.x, ambientScale, diffuseLight.x, diffuseColour.x,
                    diffuseScale),
                litChannel(texture.y, ambientLight.y, ambientColour.y, ambientScale, diffuseLight.y, diffuseColour.y,
                    diffuseScale),
                litChannel(texture.z, ambientLight.z, ambientColour.z, ambientScale, diffuseLight.z, diffuseColour.z,
                    diffuseScale));
        }

        void expectNear(const vsg::vec3& a, const vsg::vec3& b, float tolerance)
        {
            EXPECT_NEAR(a.x, b.x, tolerance);
            EXPECT_NEAR(a.y, b.y, tolerance);
            EXPECT_NEAR(a.z, b.z, tolerance);
        }

        // A small deterministic generator, so that the tests need no seed from the clock.
        struct Lcg
        {
            unsigned int state = 12345u;
            float next()
            {
                state = state * 1664525u + 1013904223u;
                return static_cast<float>((state >> 8) & 0xffffu) / 65535.f;
            }
            vsg::vec3 vec() { return vsg::vec3(next(), next(), next()); }
        };
    }

    TEST(RenderAoTerrainTest, the_part_is_the_product_of_its_factors)
    {
        const vsg::vec3 part
            = terrainAmbientPart({ 0.5f, 0.4f, 0.2f }, { 0.2f, 0.3f, 0.4f }, { 1.f, 0.5f, 0.25f }, 0.5f, 1.f);
        expectNear(part, { 0.5f * 0.2f * 1.f * 0.5f, 0.4f * 0.3f * 0.5f * 0.5f, 0.2f * 0.4f * 0.25f * 0.5f }, 1e-6f);
    }

    TEST(RenderAoTerrainTest, plain_path_part_is_the_ambient_summand_of_the_lighting)
    {
        // The path without the material model: materialTerms (scene ambient, both scales 1).
        Lcg rng;
        for (int i = 0; i < 100; ++i)
        {
            const vsg::vec3 texture = rng.vec(), ambientLight = rng.vec(), ambientColour = rng.vec();
            const vsg::vec3 diffuseLight = rng.vec(), diffuseColour = rng.vec();
            const vsg::vec3 withAmbient
                = lit(texture, ambientLight, ambientColour, 1.f, diffuseLight, diffuseColour, 1.f);
            const vsg::vec3 noAmbient = lit(texture, black, ambientColour, 1.f, diffuseLight, diffuseColour, 1.f);
            const vsg::vec3 part = terrainAmbientPart(texture, ambientLight, ambientColour, 1.f, 1.f);
            expectNear(withAmbient - noAmbient, part, 1e-5f);
        }
    }

    TEST(RenderAoTerrainTest, sky_visibility_path_part_uses_the_fields_ambient)
    {
        // The path with the exterior sky-visibility field: materialTermsAmbient with the field's ambient, scale 1.
        Lcg rng;
        for (int i = 0; i < 100; ++i)
        {
            const vsg::vec3 texture = rng.vec(), skyAmbient = rng.vec() * 0.3f, ambientColour = rng.vec();
            const vsg::vec3 diffuseLight = rng.vec(), diffuseColour = rng.vec();
            const vsg::vec3 withAmbient
                = lit(texture, skyAmbient, ambientColour, 1.f, diffuseLight, diffuseColour, 1.f);
            const vsg::vec3 noAmbient = lit(texture, black, ambientColour, 1.f, diffuseLight, diffuseColour, 1.f);
            expectNear(
                withAmbient - noAmbient, terrainAmbientPart(texture, skyAmbient, ambientColour, 1.f, 1.f), 1e-5f);
        }
    }

    TEST(RenderAoTerrainTest, pbr_path_part_carries_the_diffuse_scale_and_the_occlusion)
    {
        // The material model path: materialTermsScaled(ambient, diffuse, colour, diffuseScale * occlusion, diffuseScale).
        Lcg rng;
        for (int i = 0; i < 100; ++i)
        {
            const vsg::vec3 texture = rng.vec(), ambientLight = rng.vec(), ambientColour = rng.vec();
            const vsg::vec3 diffuseLight = rng.vec(), diffuseColour = rng.vec();
            const float diffuseScale = 1.f - rng.next();
            const float occlusion = rng.next();
            const float ambientScale = diffuseScale * occlusion;
            const vsg::vec3 withAmbient
                = lit(texture, ambientLight, ambientColour, ambientScale, diffuseLight, diffuseColour, diffuseScale);
            const vsg::vec3 noAmbient
                = lit(texture, black, ambientColour, ambientScale, diffuseLight, diffuseColour, diffuseScale);
            expectNear(withAmbient - noAmbient,
                terrainAmbientPart(texture, ambientLight, ambientColour, ambientScale, 1.f), 1e-5f);
        }
    }

    TEST(RenderAoTerrainTest, a_fully_fogged_pixel_has_no_ambient_part)
    {
        const vsg::vec3 part = terrainAmbientPart({ 0.5f, 0.5f, 0.5f }, { 0.4f, 0.4f, 0.4f }, { 1.f, 1.f, 1.f }, 1.f, 0.f);
        expectNear(part, black, 0.f);
    }

    TEST(RenderAoTerrainTest, the_fog_share_scales_the_part_and_is_limited_to_zero_to_one)
    {
        const vsg::vec3 texture(0.5f, 0.5f, 0.5f), light(0.4f, 0.4f, 0.4f), colour(1.f, 1.f, 1.f);
        const vsg::vec3 full = terrainAmbientPart(texture, light, colour, 1.f, 1.f);
        expectNear(terrainAmbientPart(texture, light, colour, 1.f, 0.25f), full * 0.25f, 1e-6f);
        expectNear(terrainAmbientPart(texture, light, colour, 1.f, 3.f), full, 0.f);
        expectNear(terrainAmbientPart(texture, light, colour, 1.f, -2.f), black, 0.f);
    }

    TEST(RenderAoTerrainTest, a_negative_scale_counts_as_zero)
    {
        expectNear(terrainAmbientPart({ 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, -0.5f, 1.f), black, 0.f);
    }

    TEST(RenderAoTerrainTest, not_a_number_scale_or_fog_share_gives_zero_not_a_number)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const vsg::vec3 a = terrainAmbientPart({ 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, nan, 1.f);
        const vsg::vec3 b = terrainAmbientPart({ 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, 1.f, nan);
        for (const vsg::vec3& v : { a, b })
        {
            EXPECT_FALSE(std::isnan(v.x));
            EXPECT_FALSE(std::isnan(v.y));
            EXPECT_FALSE(std::isnan(v.z));
            EXPECT_EQ(v.x, 0.f);
        }
    }

    TEST(RenderAoTerrainTest, removing_all_occlusion_leaves_the_colour_without_ambient_light)
    {
        Lcg rng;
        for (int i = 0; i < 100; ++i)
        {
            const vsg::vec3 texture = rng.vec(), ambientLight = rng.vec(), ambientColour = rng.vec();
            const vsg::vec3 diffuseLight = rng.vec(), diffuseColour = rng.vec();
            const float scale = rng.next();
            const vsg::vec3 colour = lit(texture, ambientLight, ambientColour, scale, diffuseLight, diffuseColour, 1.f);
            const vsg::vec3 part = terrainAmbientPart(texture, ambientLight, ambientColour, scale, 1.f);
            const vsg::vec3 expected = lit(texture, black, ambientColour, scale, diffuseLight, diffuseColour, 1.f);
            expectNear(removeOcclusion(colour, part, 0.f), expected, 1e-5f);
            // With ao 1 nothing is removed.
            expectNear(removeOcclusion(colour, part, 1.f), colour, 1e-6f);
        }
    }

    TEST(RenderAoTerrainTest, removal_never_goes_negative_even_if_the_part_exceeds_the_colour)
    {
        // Clamped lighting may hold less than the part; the tonemap's operation still stays at or above zero.
        const vsg::vec3 out = removeOcclusion({ 0.1f, 0.1f, 0.1f }, { 0.5f, 0.5f, 0.5f }, 0.f);
        EXPECT_GE(out.x, 0.f);
        EXPECT_GE(out.y, 0.f);
        EXPECT_GE(out.z, 0.f);
    }

    TEST(RenderAoTerrainTest, zero_strength_leaves_the_terrain_pixel_unchanged)
    {
        // aoCurve at strength 0 is 1: "ao = 1" removes nothing from the terrain either.
        const vsg::vec3 colour(0.3f, 0.4f, 0.5f);
        const vsg::vec3 part
            = terrainAmbientPart({ 0.5f, 0.5f, 0.5f }, { 0.3f, 0.3f, 0.3f }, { 1.f, 1.f, 1.f }, 1.f, 1.f);
        expectNear(removeOcclusion(colour, part, aoCurve(0.2f, 0.f)), colour, 0.f);
    }

    TEST(RenderAoTerrainTest, terrain_ao_needs_the_pass_and_its_own_setting)
    {
        EXPECT_TRUE(useTerrainAo(true, true, true, true));
        EXPECT_FALSE(useTerrainAo(true, false, true, true));
        EXPECT_FALSE(useTerrainAo(false, true, true, true));
        EXPECT_FALSE(useTerrainAo(true, true, false, true));
        EXPECT_FALSE(useTerrainAo(true, true, true, false));
        EXPECT_FALSE(useTerrainAo(false, false, false, false));
    }
}
