// Tests for Render::Pbr (task 0027, "Interfaces (pure parts)"), the G-buffer's material output and the new debug views.
//
// Written from the spec alone, without reading the implementation.

#include <components/pipeline/material.hpp>
#include <components/render/gbuffermath.hpp>
#include <components/render/materialmath.hpp>
#include <components/render/tonemap.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace Render::Pbr;

    constexpr float pi = 3.14159265358979f;

    TEST(RenderMaterialMathTest, roughnessFromShininess_matches_the_blinn_phong_relation)
    {
        // (2 / (n + 2))^(1/4), checked against the formula directly.
        EXPECT_NEAR(roughnessFromShininess(14.f), std::pow(2.f / 16.f, 0.25f), 1e-5f);
        EXPECT_NEAR(roughnessFromShininess(126.f), std::pow(2.f / 128.f, 0.25f), 1e-5f);
    }

    TEST(RenderMaterialMathTest, roughnessFromShininess_is_clamped_and_monotonic)
    {
        EXPECT_FLOAT_EQ(roughnessFromShininess(0.f), 1.f);
        EXPECT_FLOAT_EQ(roughnessFromShininess(-5.f), 1.f);
        EXPECT_FLOAT_EQ(roughnessFromShininess(1e9f), minRoughness);
        float previous = 2.f;
        for (float n = 1.f; n < 255.f; n += 7.f)
        {
            const float r = roughnessFromShininess(n);
            EXPECT_LE(r, previous);
            EXPECT_GE(r, minRoughness);
            EXPECT_LE(r, 1.f);
            previous = r;
        }
    }

    TEST(RenderMaterialMathTest, reflectanceFromSpecular_ranges_from_half_to_one)
    {
        EXPECT_FLOAT_EQ(reflectanceFromSpecular(vsg::vec3(0.f, 0.f, 0.f)), 0.5f);
        EXPECT_NEAR(reflectanceFromSpecular(vsg::vec3(1.f, 1.f, 1.f)), 1.f, 1e-5f);
        EXPECT_NEAR(reflectanceFromSpecular(vsg::vec3(1.f, 0.f, 0.f)), 0.5f + 0.5f * 0.2126f, 1e-5f);
        EXPECT_NEAR(reflectanceFromSpecular(vsg::vec3(0.f, 1.f, 0.f)), 0.5f + 0.5f * 0.7152f, 1e-5f);
        // Channels are clamped: negative light adds nothing, values above one count as one.
        EXPECT_FLOAT_EQ(reflectanceFromSpecular(vsg::vec3(-3.f, -1.f, -1.f)), 0.5f);
        EXPECT_NEAR(reflectanceFromSpecular(vsg::vec3(9.f, 9.f, 9.f)), 1.f, 1e-5f);
    }

    TEST(RenderMaterialMathTest, f0_from_reflectance)
    {
        EXPECT_NEAR(f0FromReflectance(0.5f), 0.04f, 1e-6f);
        EXPECT_NEAR(f0FromReflectance(1.f), 0.16f, 1e-6f);
        EXPECT_FLOAT_EQ(f0FromReflectance(0.f), 0.f);
    }

    TEST(RenderMaterialMathTest, specularF0_blends_towards_the_base_colour_for_metals)
    {
        const vsg::vec3 base(0.9f, 0.6f, 0.2f);
        const auto dielectric = specularF0(base, 0.f, 0.5f);
        EXPECT_NEAR(dielectric.x, 0.04f, 1e-6f);
        EXPECT_NEAR(dielectric.y, 0.04f, 1e-6f);
        const auto metal = specularF0(base, 1.f, 0.5f);
        EXPECT_NEAR(metal.x, 0.9f, 1e-6f);
        EXPECT_NEAR(metal.z, 0.2f, 1e-6f);
        const auto half = specularF0(base, 0.5f, 0.5f);
        EXPECT_NEAR(half.x, 0.5f * (0.04f + 0.9f), 1e-6f);
    }

    TEST(RenderMaterialMathTest, diffuseAlbedo_vanishes_for_metals)
    {
        const vsg::vec3 base(0.4f, 0.5f, 0.6f);
        EXPECT_NEAR(diffuseAlbedo(base, 0.f).y, 0.5f, 1e-6f);
        EXPECT_NEAR(diffuseAlbedo(base, 1.f).y, 0.f, 1e-6f);
        EXPECT_NEAR(diffuseAlbedo(base, 0.25f).z, 0.45f, 1e-6f);
    }

    TEST(RenderMaterialMathTest, distributionGGX_normalises_over_the_hemisphere)
    {
        // The integral of D(h) * (n.h) over the hemisphere is 1 for any roughness.
        for (float roughness : { 0.3f, 0.6f, 1.f })
        {
            double sum = 0;
            const int steps = 200000;
            for (int i = 0; i < steps; ++i)
            {
                const double theta = (i + 0.5) / steps * (pi / 2.0);
                const double noh = std::cos(theta);
                sum += distributionGGX(static_cast<float>(noh), roughness) * noh * std::sin(theta) * 2.0 * pi
                    * (pi / 2.0 / steps);
            }
            EXPECT_NEAR(sum, 1.0, 5e-3) << "roughness " << roughness;
        }
    }

    TEST(RenderMaterialMathTest, distributionGGX_peaks_at_the_normal_and_widens_with_roughness)
    {
        EXPECT_GT(distributionGGX(1.f, 0.2f), distributionGGX(0.9f, 0.2f));
        EXPECT_GT(distributionGGX(1.f, 0.2f), distributionGGX(1.f, 0.5f));
        EXPECT_GT(distributionGGX(0.7f, 0.8f), distributionGGX(0.7f, 0.2f));
        EXPECT_TRUE(std::isfinite(distributionGGX(1.f, minRoughness)));
    }

    TEST(RenderMaterialMathTest, visibilitySmithGGX_is_positive_finite_and_symmetric)
    {
        for (float noV : { 0.05f, 0.3f, 0.9f, 1.f })
            for (float noL : { 0.05f, 0.3f, 0.9f, 1.f })
            {
                const float v = visibilitySmithGGX(noV, noL, 0.5f);
                EXPECT_TRUE(std::isfinite(v));
                EXPECT_GT(v, 0.f);
                EXPECT_NEAR(v, visibilitySmithGGX(noL, noV, 0.5f), 1e-5f);
            }
        // At normal incidence and near-zero roughness the visibility is about 1 / (4 NoV NoL) = 0.25.
        EXPECT_NEAR(visibilitySmithGGX(1.f, 1.f, minRoughness), 0.25f, 0.01f);
    }

    TEST(RenderMaterialMathTest, fresnelSchlick_limits)
    {
        const vsg::vec3 f0(0.04f, 0.5f, 1.f);
        const auto normal = fresnelSchlick(1.f, f0);
        EXPECT_NEAR(normal.x, 0.04f, 1e-6f);
        EXPECT_NEAR(normal.y, 0.5f, 1e-6f);
        const auto grazing = fresnelSchlick(0.f, f0);
        EXPECT_NEAR(grazing.x, 1.f, 1e-6f);
        EXPECT_NEAR(grazing.y, 1.f, 1e-6f);
        EXPECT_NEAR(fresnelSchlick(0.5f, f0).x, 0.04f + 0.96f * std::pow(0.5f, 5.f), 1e-6f);
    }

    TEST(RenderMaterialMathTest, fresnel_never_exceeds_one)
    {
        for (float voh : { 0.05f, 0.2f, 0.5f, 0.8f, 1.f })
        {
            const float f = fresnelSchlick(voh, vsg::vec3(0.04f, 0.04f, 0.04f)).x;
            EXPECT_LE(f, 1.f);
            EXPECT_GE(f, 0.04f - 1e-6f);
        }
    }

    TEST(RenderMaterialMathTest, clampSurface_forces_ranges_and_replaces_nan)
    {
        Surface s;
        s.roughness = -1.f;
        s.metalness = 2.f;
        s.reflectance = std::numeric_limits<float>::quiet_NaN();
        s.occlusion = -0.5f;
        const Surface c = clampSurface(s);
        EXPECT_FLOAT_EQ(c.roughness, minRoughness);
        EXPECT_FLOAT_EQ(c.metalness, 1.f);
        EXPECT_FLOAT_EQ(c.reflectance, defaultReflectance);
        EXPECT_FLOAT_EQ(c.occlusion, 0.f);
        Surface big;
        big.roughness = 7.f;
        EXPECT_FLOAT_EQ(clampSurface(big).roughness, 1.f);
    }

    TEST(RenderMaterialMathTest, defaults_are_the_documented_tier_one_values)
    {
        const Surface s;
        EXPECT_FLOAT_EQ(s.roughness, 0.8f);
        EXPECT_FLOAT_EQ(s.metalness, 0.f);
        EXPECT_FLOAT_EQ(s.reflectance, 0.5f);
        EXPECT_FLOAT_EQ(s.occlusion, 1.f);
        EXPECT_FALSE(s.subsurface);
        EXPECT_FLOAT_EQ(terrainRoughness, 0.95f);
        EXPECT_EQ(materialMapSuffix, "_orm");
    }

    TEST(RenderMaterialMathTest, pack_and_unpack_round_trip)
    {
        Surface s;
        s.roughness = 0.37f;
        s.metalness = 0.25f;
        s.reflectance = 0.9f;
        s.occlusion = 0.6f;
        s.subsurface = true;
        const vsg::vec4 packed = packMaterial(s);
        EXPECT_FLOAT_EQ(packed.x, 0.25f);
        EXPECT_FLOAT_EQ(packed.y, 0.9f);
        EXPECT_FLOAT_EQ(packed.z, 0.6f);
        EXPECT_FLOAT_EQ(packed.w, 1.f);
        const Surface back = unpackMaterial(s.roughness, packed);
        EXPECT_FLOAT_EQ(back.roughness, 0.37f);
        EXPECT_FLOAT_EQ(back.metalness, 0.25f);
        EXPECT_TRUE(back.subsurface);
        EXPECT_FLOAT_EQ(packMaterial(Surface{}).w, 0.f);
    }

    TEST(RenderMaterialMathTest, packMaterial_stays_in_unit_range)
    {
        Surface s;
        s.metalness = 5.f;
        s.occlusion = -2.f;
        const vsg::vec4 packed = packMaterial(s);
        EXPECT_FLOAT_EQ(packed.x, 1.f);
        EXPECT_FLOAT_EQ(packed.z, 0.f);
    }

    TEST(RenderMaterialMathTest, parseLook_and_gates)
    {
        EXPECT_EQ(*parseLook("vanilla"), Look::Vanilla);
        EXPECT_EQ(*parseLook("physical"), Look::Physical);
        EXPECT_FALSE(parseLook("").has_value());
        EXPECT_FALSE(parseLook("Vanilla").has_value());
        EXPECT_TRUE(usePbr(true, true));
        EXPECT_FALSE(usePbr(true, false));
        EXPECT_FALSE(usePbr(false, true));
        EXPECT_TRUE(useMaterialOutput(true, true));
        EXPECT_FALSE(useMaterialOutput(true, false));
        EXPECT_FALSE(useMaterialOutput(false, true));
    }

    TEST(RenderMaterialMathTest, extraColorFormats_appends_the_material_output)
    {
        using namespace Render::GBuffer;
        EXPECT_EQ(extraColorFormats(true).size(), 3u);
        EXPECT_EQ(extraColorFormats(false).size(), 2u);
        const auto withMaterial = extraColorFormats(true, true);
        ASSERT_EQ(withMaterial.size(), 4u);
        EXPECT_EQ(withMaterial[3], VK_FORMAT_R8G8B8A8_UNORM);
        EXPECT_EQ(withMaterial[0], normalFormat);
        EXPECT_EQ(withMaterial[2], objectMotionFormat);
        EXPECT_EQ(materialFormat, VK_FORMAT_R8G8B8A8_UNORM);
    }

    TEST(RenderMaterialMathTest, new_debug_views_parse_and_number_in_enum_order)
    {
        using namespace Render::GBuffer;
        EXPECT_EQ(*parseDebugView("roughness"), DebugView::Roughness);
        EXPECT_EQ(*parseDebugView("material"), DebugView::Material);
        EXPECT_EQ(*parseDebugView("subsurface"), DebugView::Subsurface);
        EXPECT_EQ(*parseDebugView("velocity"), DebugView::Velocity);
        EXPECT_EQ(static_cast<int>(DebugView::Roughness), static_cast<int>(DebugView::Velocity) + 1);
        EXPECT_EQ(static_cast<int>(DebugView::Subsurface), static_cast<int>(DebugView::Velocity) + 3);
        const auto push = Render::Tonemap::debugPushConstants(DebugView::Material, 1.f, 2.f);
        EXPECT_FLOAT_EQ(push.x, static_cast<float>(static_cast<int>(DebugView::Material)));
    }

    TEST(PipelineMaterialDataTest, default_material_has_the_derive_markers)
    {
        const auto m = Pipeline::Material::createDefault();
        EXPECT_LT(m.pbr.x, 0.f); // roughness: derive
        EXPECT_FLOAT_EQ(m.pbr.y, 0.f); // metalness
        EXPECT_LT(m.pbr.z, 0.f); // reflectance: derive
        EXPECT_FLOAT_EQ(m.pbr.w, 1.f); // occlusion
        EXPECT_FLOAT_EQ(m.subsurface, 0.f);
        EXPECT_TRUE(Pipeline::Material::isDefault(m));
        auto changed = m;
        changed.pbr.y = 1.f;
        EXPECT_FALSE(Pipeline::Material::isDefault(changed));
        auto skin = m;
        skin.subsurface = 1.f;
        EXPECT_FALSE(Pipeline::Material::isDefault(skin));
        EXPECT_EQ(sizeof(Pipeline::Data::Material), 96u);
    }
}
