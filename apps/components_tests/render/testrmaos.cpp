// Tests for the True PBR `_rmaos` layout of the tier 3 material map (task 0044): the channel decode, the scales, and
// the two new keys of the `openeden.material` file. Written from the spec
// (docs/tasks/0044-rendering-pbr-rmaos-textures.md) before the implementation. No GPU, window or game data.

#include <components/render/materialmath.hpp>
#include <components/render/materialoverrides.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

namespace
{
    using namespace Render::Pbr;

    TEST(RenderRmaosTest, the_suffix_and_default_level_are_the_layouts)
    {
        EXPECT_EQ(std::string(rmaosMapSuffix), "_rmaos");
        EXPECT_FLOAT_EQ(defaultSpecularLevel, 0.04f);
        // The proposal of task 0027 is still there, unchanged.
        EXPECT_EQ(std::string(materialMapSuffix), "_orm");
    }

    TEST(RenderRmaosTest, an_rmaos_texel_is_roughness_metalness_occlusion_specular)
    {
        const MapTexel t = decodeMapTexel(MapLayout::Rmaos, vsg::vec4(0.8f, 0.25f, 0.5f, 0.75f));
        EXPECT_FLOAT_EQ(t.roughness, 0.8f);
        EXPECT_FLOAT_EQ(t.metalness, 0.25f);
        EXPECT_FLOAT_EQ(t.occlusion, 0.5f);
        EXPECT_FLOAT_EQ(t.specular, 0.75f);
    }

    TEST(RenderRmaosTest, an_orm_texel_is_occlusion_roughness_metalness_and_has_full_specular)
    {
        const MapTexel t = decodeMapTexel(MapLayout::Orm, vsg::vec4(0.5f, 0.8f, 0.25f, 0.1f));
        EXPECT_FLOAT_EQ(t.occlusion, 0.5f);
        EXPECT_FLOAT_EQ(t.roughness, 0.8f);
        EXPECT_FLOAT_EQ(t.metalness, 0.25f);
        EXPECT_FLOAT_EQ(t.specular, 1.f);
    }

    TEST(RenderRmaosTest, the_decode_clamps_roughness_and_unit_values)
    {
        const MapTexel t = decodeMapTexel(MapLayout::Rmaos, vsg::vec4(0.f, 2.f, -1.f, 5.f));
        EXPECT_FLOAT_EQ(t.roughness, minRoughness);
        EXPECT_FLOAT_EQ(t.metalness, 1.f);
        EXPECT_FLOAT_EQ(t.occlusion, 0.f);
        EXPECT_FLOAT_EQ(t.specular, 1.f);
    }

    TEST(RenderRmaosTest, the_decode_replaces_nan_by_the_tier_one_values)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const MapTexel t = decodeMapTexel(MapLayout::Rmaos, vsg::vec4(nan, nan, nan, nan));
        EXPECT_FLOAT_EQ(t.roughness, defaultRoughness);
        EXPECT_FLOAT_EQ(t.metalness, 0.f);
        EXPECT_FLOAT_EQ(t.occlusion, 1.f);
        EXPECT_FLOAT_EQ(t.specular, 1.f);
    }

    TEST(RenderRmaosTest, the_roughness_scale_multiplies_and_clamps)
    {
        EXPECT_FLOAT_EQ(scaledRoughness(0.5f, 1.f), 0.5f);
        EXPECT_FLOAT_EQ(scaledRoughness(0.5f, 0.5f), 0.25f);
        EXPECT_FLOAT_EQ(scaledRoughness(0.5f, 4.f), 1.f);
        EXPECT_FLOAT_EQ(scaledRoughness(0.01f, 1.f), minRoughness);
    }

    TEST(RenderRmaosTest, a_scale_of_zero_or_less_or_nan_means_no_scale)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FLOAT_EQ(scaledRoughness(0.5f, 0.f), 0.5f);
        EXPECT_FLOAT_EQ(scaledRoughness(0.5f, -2.f), 0.5f);
        EXPECT_FLOAT_EQ(scaledRoughness(0.5f, nan), 0.5f);
        EXPECT_FLOAT_EQ(scaledRoughness(nan, 2.f), defaultRoughness);
    }

    TEST(RenderRmaosTest, a_dielectric_f0_is_the_level_times_the_texture_alpha)
    {
        EXPECT_FLOAT_EQ(dielectricF0(0.04f, 1.f), 0.04f);
        EXPECT_FLOAT_EQ(dielectricF0(0.04f, 0.5f), 0.02f);
        EXPECT_FLOAT_EQ(dielectricF0(0.08f, 0.5f), 0.04f);
        EXPECT_FLOAT_EQ(dielectricF0(0.04f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(dielectricF0(2.f, 2.f), 1.f);
        EXPECT_FLOAT_EQ(dielectricF0(std::numeric_limits<float>::quiet_NaN(), 1.f), 0.04f);
        EXPECT_FLOAT_EQ(dielectricF0(0.04f, std::numeric_limits<float>::quiet_NaN()), 0.04f);
    }

    TEST(RenderRmaosTest, reflectance_from_f0_inverts_the_engines_f0_from_reflectance)
    {
        EXPECT_NEAR(reflectanceFromF0(0.04f), 0.5f, 1e-6f);
        EXPECT_NEAR(reflectanceFromF0(0.16f), 1.f, 1e-6f);
        EXPECT_FLOAT_EQ(reflectanceFromF0(0.f), 0.f);
        EXPECT_FLOAT_EQ(reflectanceFromF0(5.f), 1.f);
        EXPECT_FLOAT_EQ(reflectanceFromF0(-1.f), 0.f);
        EXPECT_NEAR(f0FromReflectance(reflectanceFromF0(0.09f)), 0.09f, 1e-6f);
    }

    // The two new keys of the override file.
    const std::string scaleSample = R"({
  "format": "openeden.material",
  "version": 1,
  "materials": [
    {
      "match": {
        "texture": "textures/tx_bc_moss.dds"
      },
      "roughnessScale": 1.5,
      "specularLevel": 0.04
    },
    {
      "match": {
        "model": "meshes/x/test.nif"
      },
      "roughness": 0.4,
      "subsurface": false,
      "roughnessScale": 2
    }
  ]
}
)";

    TEST(RenderRmaosOverridesTest, the_scale_keys_parse)
    {
        OverrideError error;
        const auto set = parseOverrides(scaleSample, "t.json", error);
        ASSERT_TRUE(set.has_value()) << error.describe();
        ASSERT_EQ(set->entries.size(), 2u);
        EXPECT_DOUBLE_EQ(*set->entries[0].values.roughnessScale, 1.5);
        EXPECT_DOUBLE_EQ(*set->entries[0].values.specularLevel, 0.04);
        EXPECT_FALSE(set->entries[0].values.roughness.has_value());
        EXPECT_DOUBLE_EQ(*set->entries[1].values.roughnessScale, 2.0);
        EXPECT_FALSE(set->entries[1].values.specularLevel.has_value());
    }

    TEST(RenderRmaosOverridesTest, the_canonical_writer_round_trips_byte_for_byte)
    {
        OverrideError error;
        const auto set = parseOverrides(scaleSample, "t.json", error);
        ASSERT_TRUE(set.has_value()) << error.describe();
        EXPECT_EQ(writeCanonical(*set), scaleSample);
    }

    TEST(RenderRmaosOverridesTest, an_entry_with_only_a_scale_is_enough)
    {
        OverrideError error;
        const auto set = parseOverrides(
            R"({"format":"openeden.material","version":1,"materials":[{"match":{"texture":"a.dds"},"specularLevel":0.5}]})",
            "t.json", error);
        EXPECT_TRUE(set.has_value()) << error.describe();
    }

    TEST(RenderRmaosOverridesTest, out_of_range_scales_are_located_errors)
    {
        const auto bad = [](const std::string& entry) {
            OverrideError error;
            const auto set = parseOverrides(
                R"({"format":"openeden.material","version":1,"materials":[{"match":{"texture":"a.dds"},)" + entry
                    + "}]}",
                "t.json", error);
            EXPECT_FALSE(set.has_value()) << entry;
            return error;
        };
        EXPECT_EQ(bad(R"("roughnessScale": 0)").field, "materials[0].roughnessScale");
        EXPECT_EQ(bad(R"("roughnessScale": -1)").field, "materials[0].roughnessScale");
        EXPECT_EQ(bad(R"("roughnessScale": 100)").field, "materials[0].roughnessScale");
        EXPECT_EQ(bad(R"("roughnessScale": "x")").field, "materials[0].roughnessScale");
        EXPECT_EQ(bad(R"("specularLevel": 1.5)").field, "materials[0].specularLevel");
        EXPECT_EQ(bad(R"("specularLevel": -0.1)").field, "materials[0].specularLevel");
        EXPECT_EQ(bad(R"("specularLevel": true)").field, "materials[0].specularLevel");
    }

    TEST(RenderRmaosOverridesTest, scales_merge_like_the_other_values_with_textures_after_models)
    {
        OverrideSet models, textures;
        OverrideEntry m;
        m.kind = OverrideEntry::Kind::Model;
        m.path = "meshes/a.nif";
        m.values.roughnessScale = 2.0;
        m.values.specularLevel = 0.1;
        models.entries.push_back(m);
        OverrideEntry t;
        t.kind = OverrideEntry::Kind::Texture;
        t.path = "textures/a.dds";
        t.values.roughnessScale = 0.5;
        textures.entries.push_back(t);
        OverrideTable table;
        table.add(models);
        table.add(textures);
        const OverrideValues v = table.resolveValues("meshes/a.nif", "textures/a.dds");
        EXPECT_DOUBLE_EQ(*v.roughnessScale, 0.5); // the texture is more specific
        EXPECT_DOUBLE_EQ(*v.specularLevel, 0.1); // falls through
        const OverrideValues none = table.resolveValues("meshes/b.nif", "textures/b.dds");
        EXPECT_FALSE(none.roughnessScale.has_value());
        EXPECT_FALSE(none.specularLevel.has_value());
    }
}
