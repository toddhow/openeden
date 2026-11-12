// Task 0130: the numbers the Skyrim material mapping puts in the material descriptor (SkyrimMaterial::makeMaterial).
// Hand-derived expectations; no game asset is read.
#include <apps/openmw/mwrender/skyrimmaterial.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

namespace
{
    using NifScene::Flat::MaterialKey;
    using namespace MWRender;

    TEST(SkyrimMaterial, a_key_without_the_new_data_gives_the_default_material_with_the_shader_alpha)
    {
        MaterialKey key;
        key.mAlpha = 0.5f;
        Pipeline::Data::Material expected = Pipeline::Material::createDefault();
        expected.diffuse.a = 0.5f;
        const Pipeline::Data::Material made = SkyrimMaterial::makeMaterial(key);
        EXPECT_EQ(std::memcmp(&made, &expected, sizeof(made)), 0);
    }

    TEST(SkyrimMaterial, specular_becomes_pbr_roughness_and_reflectance)
    {
        MaterialKey key;
        key.mSpecularColor = { 0.5f, 0.25f, 0.f };
        key.mGlossiness = 78.f;
        const auto m = SkyrimMaterial::makeMaterial(key);
        // roughness = (2 / (78 + 2))^(1/4) = 0.025^0.25
        EXPECT_NEAR(m.pbr.x, 0.39764f, 1e-4f);
        // reflectance = 0.5 + 0.5 * (0.2126 * 0.5 + 0.7152 * 0.25)
        EXPECT_NEAR(m.pbr.z, 0.64255f, 1e-4f);
    }

    TEST(SkyrimMaterial, no_specular_leaves_the_derive_markers_of_the_default_material)
    {
        MaterialKey key;
        key.mGlossiness = 80.f; // a glossiness without a specular colour is nothing
        const auto m = SkyrimMaterial::makeMaterial(key);
        EXPECT_LT(m.pbr.x, 0.f);
        EXPECT_LT(m.pbr.z, 0.f);
    }

    TEST(SkyrimMaterial, a_specular_colour_above_one_counts_as_one_for_the_reflectance)
    {
        MaterialKey key;
        key.mSpecularColor = { 3.f, 3.f, 3.f };
        key.mGlossiness = 10.f;
        const auto m = SkyrimMaterial::makeMaterial(key);
        EXPECT_NEAR(m.pbr.z, 1.f, 1e-5f);
    }

    TEST(SkyrimMaterial, zero_glossiness_with_a_specular_colour_is_fully_rough)
    {
        MaterialKey key;
        key.mSpecularColor = { 1.f, 1.f, 1.f };
        key.mGlossiness = 0.f;
        EXPECT_FLOAT_EQ(SkyrimMaterial::makeMaterial(key).pbr.x, 1.f);
    }

    TEST(SkyrimMaterial, the_emissive_colour_is_the_materials_emission)
    {
        MaterialKey key;
        key.mEmissive = { 1.5f, 0.5f, 0.f };
        const auto m = SkyrimMaterial::makeMaterial(key);
        EXPECT_FLOAT_EQ(m.emissive.x, 1.5f);
        EXPECT_FLOAT_EQ(m.emissive.y, 0.5f);
        EXPECT_FLOAT_EQ(m.emissive.z, 0.f);
    }
}
