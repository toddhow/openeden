// Task 0130 (Skyrim material mapping v1): what flatten keeps of a lighting shader when Limits::mSurfaceMaps is on: the
// normal map with tangents, the glow map, specular and emissive values, the shader type. Scenes are built by hand; no
// game asset is read. The flag bits are named in components/nifscene/flatten.hpp and checked against the installed
// meshes by testskyrimmaterialsreal.cpp.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include <components/nifscene/flatten.hpp>

namespace
{
    using namespace NifScene::Flat;
    using NifScene::Scene;

    constexpr float notANumber = std::numeric_limits<float>::quiet_NaN();

    struct Spec
    {
        std::string diffuse = "textures\\a_d.dds";
        std::string normal;
        std::string glow;
        std::uint32_t flags1 = 0;
        std::uint32_t flags2 = 0;
        std::uint32_t shaderType = 0;
        std::array<float, 3> specular{ 1.f, 1.f, 1.f };
        float strength = 1.f;
        float glossiness = 80.f;
        std::array<float, 3> emissive{ 0.f, 0.f, 0.f };
        float emissiveMultiple = 1.f;
        bool tangents = true;
        bool uvs = true;
    };

    struct Builder
    {
        Scene scene;
        std::uint32_t root = 0;

        Builder()
        {
            NifScene::Node n;
            n.mName = "root";
            scene.mNodes.push_back(n);
            scene.mRoots.push_back(0);
        }

        // A quad in the XY plane with +X tangents. The node can carry a rotation of 90 degrees about Z.
        std::uint32_t quad(const Spec& spec, bool rotated = false)
        {
            NifScene::ShaderMaterial m;
            m.mKind = NifScene::ShaderKind::Lighting;
            m.mShaderType = spec.shaderType;
            m.mFlags1 = spec.flags1;
            m.mFlags2 = spec.flags2;
            m.mSpecularColor = spec.specular;
            m.mSpecularStrength = spec.strength;
            m.mGlossiness = spec.glossiness;
            m.mEmissiveColor = spec.emissive;
            m.mEmissiveMultiple = spec.emissiveMultiple;
            NifScene::TextureSet set;
            set.mTextures = { spec.diffuse, spec.normal, spec.glow };
            scene.mTextureSets.push_back(set);
            m.mTextureSet = static_cast<std::uint32_t>(scene.mTextureSets.size() - 1);
            scene.mMaterials.push_back(m);

            NifScene::Node n;
            n.mName = "quad";
            n.mParent = 0;
            if (rotated)
                n.mTransform.mRotation = { 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f }; // +90 degrees about Z
            scene.mNodes.push_back(n);
            NifScene::Geometry g;
            g.mName = "quad";
            g.mNode = static_cast<std::uint32_t>(scene.mNodes.size() - 1);
            g.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
            g.mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
            if (spec.uvs)
                g.mUV0 = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            if (spec.tangents)
                g.mTangents = { { 1, 0, 0, 1 }, { 1, 0, 0, 1 }, { 1, 0, 0, 1 }, { 1, 0, 0, 1 } };
            g.mTriangles = { 0, 1, 2, 0, 2, 3 };
            g.mMaterial = static_cast<std::uint32_t>(scene.mMaterials.size() - 1);
            scene.mGeometries.push_back(g);
            scene.mNodes.back().mGeometry = static_cast<std::uint32_t>(scene.mGeometries.size() - 1);
            return static_cast<std::uint32_t>(scene.mGeometries.size() - 1);
        }
    };

    Limits on()
    {
        Limits limits;
        limits.mSurfaceMaps = true;
        return limits;
    }

    bool near(float a, float b, float eps = 1e-5f)
    {
        return std::fabs(a - b) <= eps;
    }
}

TEST(NifSceneFlattenMaterials, with_the_option_off_nothing_new_is_kept)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_n.dds";
    s.glow = "textures\\a_g.dds";
    s.flags2 = ShaderFlags::sGlowMap;
    s.flags1 = ShaderFlags::sSpecular;
    s.emissive = { 1.f, 1.f, 1.f };
    s.shaderType = 2;
    b.quad(s);
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    const Shape& shape = model.mShapes[0];
    EXPECT_TRUE(shape.mTangents.empty());
    EXPECT_EQ(shape.mKey, MaterialKey{ [] {
        MaterialKey k;
        k.mDiffuse = "textures\\a_d.dds";
        return k;
    }() });
    EXPECT_EQ(model.mReport.mNormalMaps, 0u);
    EXPECT_EQ(model.mReport.mGlowMaps, 0u);
}

TEST(NifSceneFlattenMaterials, a_normal_map_with_tangents_is_kept_and_the_tangents_ride_with_the_vertices)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_n.dds";
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    const Shape& shape = model.mShapes[0];
    EXPECT_EQ(shape.mKey.mNormal, "textures\\a_n.dds");
    ASSERT_EQ(shape.mTangents.size(), 4u);
    EXPECT_TRUE(
        near(shape.mTangents[2][0], 1.f) && near(shape.mTangents[2][1], 0.f) && near(shape.mTangents[2][3], 1.f));
    EXPECT_EQ(model.mReport.mNormalMaps, 1u);
}

TEST(NifSceneFlattenMaterials, tangents_turn_with_the_node_and_keep_their_sign)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_n.dds";
    b.scene.mGeometries.reserve(1);
    b.quad(s, true);
    b.scene.mGeometries[0].mTangents = { { 1, 0, 0, -1 }, { 1, 0, 0, -1 }, { 1, 0, 0, -1 }, { 1, 0, 0, -1 } };
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    ASSERT_EQ(model.mShapes[0].mTangents.size(), 4u);
    const auto& t = model.mShapes[0].mTangents[0];
    // +90 degrees about Z turns +X into +Y; the handedness sign is not a direction and stays.
    EXPECT_TRUE(near(t[0], 0.f)) << t[0];
    EXPECT_TRUE(near(t[1], 1.f)) << t[1];
    EXPECT_TRUE(near(t[2], 0.f)) << t[2];
    EXPECT_TRUE(near(t[3], -1.f)) << t[3];
}

TEST(NifSceneFlattenMaterials, a_normal_map_without_tangents_is_not_used_and_is_counted)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_n.dds";
    s.tangents = false;
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mKey.mNormal.empty());
    EXPECT_TRUE(model.mShapes[0].mTangents.empty());
    EXPECT_EQ(model.mReport.mNormalMapsNoTangents, 1u);
    EXPECT_EQ(model.mReport.mNormalMaps, 0u);
}

TEST(NifSceneFlattenMaterials, a_non_finite_tangent_means_no_normal_map_for_that_geometry)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_n.dds";
    b.quad(s);
    b.scene.mGeometries[0].mTangents[1][0] = notANumber;
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mKey.mNormal.empty());
    EXPECT_TRUE(model.mShapes[0].mTangents.empty());
    EXPECT_EQ(model.mReport.mNormalMapsNoTangents, 1u);
}

TEST(NifSceneFlattenMaterials, model_space_normals_are_not_used_as_tangent_space_normals)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_msn.dds";
    s.flags1 = ShaderFlags::sModelSpaceNormals;
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mKey.mNormal.empty());
    EXPECT_TRUE(model.mShapes[0].mTangents.empty());
    EXPECT_EQ(model.mReport.mNormalMapsModelSpace, 1u);
}

TEST(NifSceneFlattenMaterials, a_shape_without_texture_coordinates_gets_no_normal_map)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_n.dds";
    s.uvs = false;
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mKey.mNormal.empty());
    EXPECT_TRUE(model.mShapes[0].mTangents.empty());
}

TEST(NifSceneFlattenMaterials, the_glow_map_needs_the_glow_flag_and_a_name)
{
    {
        Builder b;
        Spec s;
        s.glow = "textures\\a_g.dds";
        s.flags2 = ShaderFlags::sGlowMap;
        b.quad(s);
        const FlatModel model = flatten(b.scene, on());
        ASSERT_EQ(model.mShapes.size(), 1u);
        EXPECT_EQ(model.mShapes[0].mKey.mGlow, "textures\\a_g.dds");
        EXPECT_EQ(model.mReport.mGlowMaps, 1u);
    }
    {
        Builder b; // slot 2 named, flag clear: another meaning of the slot (skin detail, inner layer), not a glow map
        Spec s;
        s.glow = "textures\\a_g.dds";
        b.quad(s);
        const FlatModel model = flatten(b.scene, on());
        ASSERT_EQ(model.mShapes.size(), 1u);
        EXPECT_TRUE(model.mShapes[0].mKey.mGlow.empty());
        EXPECT_EQ(model.mReport.mGlowMaps, 0u);
    }
    {
        Builder b; // flag set, slot empty
        Spec s;
        s.flags2 = ShaderFlags::sGlowMap;
        b.quad(s);
        const FlatModel model = flatten(b.scene, on());
        ASSERT_EQ(model.mShapes.size(), 1u);
        EXPECT_TRUE(model.mShapes[0].mKey.mGlow.empty());
    }
}

TEST(NifSceneFlattenMaterials, specular_is_colour_times_strength_only_with_the_specular_flag)
{
    Builder b;
    Spec s;
    s.flags1 = ShaderFlags::sSpecular;
    s.specular = { 1.f, 0.5f, 0.f };
    s.strength = 0.5f;
    s.glossiness = 40.f;
    b.quad(s);
    FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(near(model.mShapes[0].mKey.mSpecularColor[0], 0.5f));
    EXPECT_TRUE(near(model.mShapes[0].mKey.mSpecularColor[1], 0.25f));
    EXPECT_TRUE(near(model.mShapes[0].mKey.mSpecularColor[2], 0.f));
    EXPECT_TRUE(near(model.mShapes[0].mKey.mGlossiness, 40.f));

    Builder c;
    s.flags1 = 0;
    c.quad(s);
    model = flatten(c.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mKey.mSpecularColor, (std::array<float, 3>{ 0.f, 0.f, 0.f }));
    EXPECT_TRUE(near(model.mShapes[0].mKey.mGlossiness, 0.f));
}

TEST(NifSceneFlattenMaterials, emissive_is_colour_times_multiple_and_survives_bad_numbers)
{
    Builder b;
    Spec s;
    s.emissive = { 0.5f, 1.f, 0.f };
    s.emissiveMultiple = 3.f;
    b.quad(s);
    FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(near(model.mShapes[0].mKey.mEmissive[0], 1.5f));
    EXPECT_TRUE(near(model.mShapes[0].mKey.mEmissive[1], 3.f));
    EXPECT_TRUE(near(model.mShapes[0].mKey.mEmissive[2], 0.f));

    Builder c;
    s.emissive = { notANumber, 1.f, 1.f };
    s.emissiveMultiple = 1.f;
    s.strength = notANumber;
    s.flags1 = ShaderFlags::sSpecular;
    c.quad(s);
    model = flatten(c.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    for (float v : model.mShapes[0].mKey.mEmissive)
        EXPECT_TRUE(std::isfinite(v));
    for (float v : model.mShapes[0].mKey.mSpecularColor)
        EXPECT_TRUE(std::isfinite(v));
    EXPECT_TRUE(std::isfinite(model.mShapes[0].mKey.mGlossiness));
}

TEST(NifSceneFlattenMaterials, the_shader_type_is_part_of_the_key)
{
    Builder b;
    Spec s;
    s.shaderType = 2;
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mKey.mShaderType, 2u);
}

TEST(NifSceneFlattenMaterials, shapes_with_the_same_normal_map_merge_and_keep_their_tangents_in_step)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_n.dds";
    b.quad(s);
    b.quad(s, true);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mPositions.size(), 8u);
    EXPECT_EQ(model.mShapes[0].mTangents.size(), 8u);
}

TEST(NifSceneFlattenMaterials, shapes_with_different_normal_maps_do_not_merge)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_n.dds";
    b.quad(s);
    s.normal = "textures\\b_n.dds";
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    EXPECT_EQ(model.mShapes.size(), 2u);
}

TEST(NifSceneFlattenMaterials, a_shape_with_and_a_shape_without_a_normal_map_do_not_merge)
{
    Builder b;
    Spec s;
    s.normal = "textures\\a_n.dds";
    b.quad(s);
    s.normal.clear();
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 2u);
    for (const Shape& shape : model.mShapes)
        EXPECT_EQ(shape.mTangents.size(), shape.mKey.mNormal.empty() ? 0u : shape.mPositions.size());
}

TEST(NifSceneFlattenMaterials, a_glow_mapped_shape_gets_its_emission_from_the_map_not_from_the_colour)
{
    Builder b;
    Spec s;
    s.glow = "textures\\a_g.dds";
    s.flags2 = ShaderFlags::sGlowMap;
    s.emissive = { 1.f, 1.f, 1.f };
    s.emissiveMultiple = 2.f;
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mKey.mGlow, "textures\\a_g.dds");
    EXPECT_EQ(model.mShapes[0].mKey.mEmissive, (std::array<float, 3>{ 0.f, 0.f, 0.f }));
}
