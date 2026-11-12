// Task 0121 (neutral world stage N5-2): the skinned half of a Skyrim SE scene (components/nifscene/skinned.*) and the
// skeleton plan (components/nifscene/skeleton.*). Scenes are built by hand; no game asset is read.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include <components/nifscene/skeleton.hpp>
#include <components/nifscene/skinned.hpp>

namespace
{
    using namespace NifScene;
    constexpr float notANumber = std::numeric_limits<float>::quiet_NaN();

    struct Builder
    {
        Scene scene;

        std::uint32_t node(const std::string& name, std::uint32_t parent = sNone,
            std::array<float, 3> translation = { 0, 0, 0 }, std::uint32_t flags = 0)
        {
            Node n;
            n.mName = name;
            n.mParent = parent;
            n.mFlags = flags;
            n.mTransform.mTranslation = translation;
            scene.mNodes.push_back(n);
            const auto index = static_cast<std::uint32_t>(scene.mNodes.size() - 1);
            if (parent == sNone)
                scene.mRoots.push_back(index);
            return index;
        }

        std::uint32_t material(const std::string& diffuse, std::uint32_t flags1 = 0, std::uint32_t flags2 = 0)
        {
            ShaderMaterial m;
            m.mFlags1 = flags1;
            m.mFlags2 = flags2;
            if (!diffuse.empty())
            {
                scene.mTextureSets.push_back({ { diffuse } });
                m.mTextureSet = static_cast<std::uint32_t>(scene.mTextureSets.size() - 1);
            }
            scene.mMaterials.push_back(m);
            return static_cast<std::uint32_t>(scene.mMaterials.size() - 1);
        }

        // A skinned quad with two bones: vertices 0 and 3 follow bone 0, vertices 1 and 2 follow bone 1 (weight 0.75)
        // and bone 0 (weight 0.25). Returns the geometry index.
        std::uint32_t skinnedQuad(std::uint32_t parent, std::uint32_t materialIndex, const std::string& name = "body")
        {
            const std::uint32_t nodeIndex = node(name, parent);
            Skin skin;
            skin.mBoneNames = { "Pelvis", "Spine" };
            skin.mBoneNodes = { sNone, sNone };
            skin.mBindTransforms.resize(2);
            skin.mBindTransforms[1].mTranslation = { 0.f, 0.f, -5.f };
            scene.mSkins.push_back(skin);
            Geometry g;
            g.mName = name;
            g.mNode = nodeIndex;
            g.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
            g.mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
            g.mUV0 = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            g.mTriangles = { 0, 1, 2, 0, 2, 3 };
            g.mMaterial = materialIndex;
            g.mSkin = static_cast<std::uint32_t>(scene.mSkins.size() - 1);
            g.mInfluenceStart = { 0, 1, 3, 5, 6 };
            g.mInfluences = { { 0, 1.f }, { 1, 0.75f }, { 0, 0.25f }, { 1, 0.75f }, { 0, 0.25f }, { 0, 1.f } };
            scene.mGeometries.push_back(g);
            const auto index = static_cast<std::uint32_t>(scene.mGeometries.size() - 1);
            scene.mNodes[nodeIndex].mGeometry = index;
            return index;
        }

        Geometry& geometry(std::uint32_t index) { return scene.mGeometries[index]; }
    };
}

TEST(NifSceneSkinned, a_skinned_quad_keeps_its_vertices_in_shape_space_with_four_influences_at_most)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    b.skinnedQuad(root, b.material("textures\\a.dds"));
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    const Skinned::Shape& s = model.mShapes[0];
    EXPECT_EQ(s.mPositions.size(), 4u);
    EXPECT_EQ(s.mIndices.size(), 6u);
    EXPECT_EQ(s.mKey.mDiffuse, "textures\\a.dds");
    ASSERT_EQ(s.mBones.size(), 2u);
    EXPECT_EQ(s.mBones[0].mName, "Pelvis");
    EXPECT_FLOAT_EQ(s.mBones[1].mInverseBind.mTranslation[2], -5.f);
    ASSERT_EQ(s.mBoneIndices.size(), 4u);
    ASSERT_EQ(s.mBoneWeights.size(), 4u);
    // vertex 1: bone 1 (0.75) first, bone 0 (0.25) second
    EXPECT_EQ(s.mBoneIndices[1][0], 1u);
    EXPECT_FLOAT_EQ(s.mBoneWeights[1][0], 0.75f);
    EXPECT_EQ(s.mBoneIndices[1][1], 0u);
    EXPECT_FLOAT_EQ(s.mBoneWeights[1][1], 0.25f);
    EXPECT_FLOAT_EQ(s.mBoneWeights[1][2], 0.f);
    EXPECT_EQ(model.mReport.mDrawn, 1u);
}

TEST(NifSceneSkinned, the_node_transform_of_the_shape_is_not_baked_into_the_vertices)
{
    Builder b;
    const std::uint32_t root = b.node("root", sNone, { 100.f, 0.f, 0.f });
    b.skinnedQuad(root, b.material("textures\\a.dds"));
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mPositions[1][0], 1.f);
}

TEST(NifSceneSkinned, weights_are_sorted_largest_first_and_renormalised)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    const std::uint32_t g = b.skinnedQuad(root, b.material("t.dds"));
    // Vertex 0 gets five influences; the smallest is dropped and the rest renormalised.
    auto& geometry = b.geometry(g);
    b.scene.mSkins[0].mBoneNames = { "a", "b", "c", "d", "e" };
    b.scene.mSkins[0].mBoneNodes.assign(5, sNone);
    b.scene.mSkins[0].mBindTransforms.resize(5);
    geometry.mInfluenceStart = { 0, 5, 5, 5, 5 };
    geometry.mInfluences = { { 0, 0.1f }, { 1, 0.4f }, { 2, 0.2f }, { 3, 0.2f }, { 4, 0.05f } };
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    const auto& w = model.mShapes[0].mBoneWeights[0];
    const auto& i = model.mShapes[0].mBoneIndices[0];
    EXPECT_EQ(i[0], 1u);
    EXPECT_GE(w[0], w[1]);
    EXPECT_GE(w[1], w[2]);
    EXPECT_GE(w[2], w[3]);
    EXPECT_NEAR(w[0] + w[1] + w[2] + w[3], 1.f, 1e-5f);
    // The dropped influence is bone 4 (0.05), not bone 0 (0.1).
    for (int k = 0; k < 4; ++k)
        EXPECT_NE(i[k], 4u);
}

TEST(NifSceneSkinned, a_vertex_with_no_influence_follows_bone_zero_and_is_counted)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    const std::uint32_t g = b.skinnedQuad(root, b.material("t.dds"));
    b.geometry(g).mInfluenceStart = { 0, 1, 1, 1, 1 }; // only vertex 0 has one
    b.geometry(g).mInfluences = { { 1, 1.f } };
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mBoneIndices[2][0], 0u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mBoneWeights[2][0], 1.f);
    EXPECT_EQ(model.mReport.mVerticesWithoutInfluence, 3u);
}

TEST(NifSceneSkinned, an_influence_on_a_bone_outside_the_list_or_with_a_bad_weight_is_dropped_and_counted)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    const std::uint32_t g = b.skinnedQuad(root, b.material("t.dds"));
    b.geometry(g).mInfluenceStart = { 0, 3, 4, 5, 6 };
    b.geometry(g).mInfluences = { { 0, 0.5f }, { 9, 0.5f }, { 1, notANumber }, { 0, 1.f }, { 0, 1.f }, { 1, 1.f } };
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mReport.mBadInfluences, 2u);
    const auto& w = model.mShapes[0].mBoneWeights[0];
    EXPECT_NEAR(w[0] + w[1] + w[2] + w[3], 1.f, 1e-5f);
    for (const auto& weights : model.mShapes[0].mBoneWeights)
        for (float v : weights)
            EXPECT_TRUE(std::isfinite(v));
}

TEST(NifSceneSkinned, unskinned_hidden_effect_and_materialless_shapes_are_left_out_and_counted)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    b.skinnedQuad(root, b.material("t.dds")); // drawn
    const std::uint32_t hiddenParent = b.node("hidden", root, { 0, 0, 0 }, 1u);
    b.skinnedQuad(hiddenParent, b.material("t.dds"), "hiddenbody");
    b.skinnedQuad(root, sNone, "nomaterial");
    // An effect (refraction) material.
    b.skinnedQuad(root, b.material("t.dds", Flat::ShaderFlags::sRefraction), "refract");
    // An unskinned shape: the static path's.
    const std::uint32_t g = b.skinnedQuad(root, b.material("t.dds"), "static");
    b.geometry(g).mSkin = sNone;
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    EXPECT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mReport.mSkippedHidden, 1u);
    EXPECT_EQ(model.mReport.mSkippedNoMaterial, 1u);
    EXPECT_EQ(model.mReport.mSkippedEffect, 1u);
    EXPECT_EQ(model.mReport.mSkippedUnskinned, 1u);
}

TEST(NifSceneSkinned, triangles_with_an_index_outside_the_shape_are_dropped)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    const std::uint32_t g = b.skinnedQuad(root, b.material("t.dds"));
    b.geometry(g).mTriangles = { 0, 1, 2, 0, 2, 77 };
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mIndices.size(), 3u);
    EXPECT_EQ(model.mReport.mDroppedTriangles, 1u);
}

TEST(NifSceneSkinned, a_non_finite_vertex_drops_its_triangles)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    const std::uint32_t g = b.skinnedQuad(root, b.material("t.dds"));
    b.geometry(g).mPositions[3] = { notANumber, 0.f, 0.f };
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mIndices.size(), 3u);
    EXPECT_EQ(model.mReport.mBadVertices, 1u);
    for (const auto& p : model.mShapes[0].mPositions)
        for (float v : p)
            EXPECT_TRUE(std::isfinite(v));
}

TEST(NifSceneSkinned, a_skin_transform_that_is_not_the_identity_is_carried)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    b.skinnedQuad(root, b.material("t.dds"));
    b.scene.mSkins[0].mSkinTransform.mTranslation = { 0.f, 2.f, 0.f };
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mHasSkinTransform);
    EXPECT_FLOAT_EQ(model.mShapes[0].mSkinTransform.mTranslation[1], 2.f);
}

TEST(NifSceneSkinned, the_caps_bound_shapes_and_bones)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    for (int i = 0; i < 5; ++i)
        b.skinnedQuad(root, b.material("t.dds"), "s" + std::to_string(i));
    Skinned::Limits limits;
    limits.mMaxShapes = 2;
    EXPECT_EQ(Skinned::flattenSkinned(b.scene, limits).mShapes.size(), 2u);
    limits = {};
    limits.mMaxBonesPerShape = 1; // the quads have two bones
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene, limits);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedOverCap, 5u);
}

TEST(NifSceneSkinned, a_scene_with_no_geometry_gives_an_empty_model)
{
    Scene scene;
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(scene);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mGeometries, 0u);
}

// ---- the skeleton plan ----------------------------------------------------------------------------------------

TEST(NifSceneSkeleton, parents_come_before_children_with_their_local_transforms)
{
    Builder b;
    const std::uint32_t root = b.node("NPC Root [Root]");
    const std::uint32_t pelvis = b.node("NPC Pelvis [Pelv]", root, { 0, 0, 68 });
    b.node("NPC Spine [Spn0]", pelvis, { 0, 0, 6 });
    b.node("NPC L Thigh [LThg]", pelvis, { -9, 0, -3 });
    const Skeleton::Plan plan = Skeleton::plan(b.scene);
    ASSERT_EQ(plan.mNodes.size(), 4u);
    EXPECT_EQ(plan.mNodes[0].mParent, sNone);
    for (std::uint32_t i = 0; i < plan.mNodes.size(); ++i)
        if (plan.mNodes[i].mParent != sNone)
            EXPECT_LT(plan.mNodes[i].mParent, i);
    const std::uint32_t pelvisAt = Skeleton::find(plan, "npc pelvis [pelv]");
    ASSERT_NE(pelvisAt, sNone);
    EXPECT_FLOAT_EQ(plan.mNodes[pelvisAt].mLocal.mTranslation[2], 68.f);
    EXPECT_EQ(plan.mNodes[Skeleton::find(plan, "NPC Spine [Spn0]")].mParent, pelvisAt);
}

TEST(NifSceneSkeleton, children_listed_before_their_parent_in_the_file_still_follow_it)
{
    Builder b;
    // The child (record 0) names a parent that comes later (record 1).
    Node child;
    child.mName = "child";
    child.mParent = 1;
    Node parent;
    parent.mName = "parent";
    b.scene.mNodes = { child, parent };
    b.scene.mRoots = { 1 };
    const Skeleton::Plan plan = Skeleton::plan(b.scene);
    ASSERT_EQ(plan.mNodes.size(), 2u);
    EXPECT_EQ(plan.mNodes[0].mName, "parent");
    EXPECT_EQ(plan.mNodes[1].mName, "child");
    EXPECT_EQ(plan.mNodes[1].mParent, 0u);
}

TEST(NifSceneSkeleton, geometry_nodes_are_left_out_and_counted)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    b.skinnedQuad(root, b.material("t.dds"));
    const Skeleton::Plan plan = Skeleton::plan(b.scene);
    EXPECT_EQ(plan.mNodes.size(), 1u);
    EXPECT_EQ(plan.mSkippedGeometry, 1u);
}

TEST(NifSceneSkeleton, a_hidden_bone_is_still_a_bone)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    b.node("hiddenbone", root, { 0, 0, 0 }, 1u);
    EXPECT_NE(Skeleton::find(Skeleton::plan(b.scene), "hiddenbone"), sNone);
}

TEST(NifSceneSkeleton, a_parent_cycle_is_cut_and_its_nodes_counted_as_unreachable)
{
    Builder b;
    b.node("root");
    Node a;
    a.mName = "a";
    a.mParent = 2;
    Node c;
    c.mName = "c";
    c.mParent = 1;
    b.scene.mNodes.push_back(a);
    b.scene.mNodes.push_back(c);
    const Skeleton::Plan plan = Skeleton::plan(b.scene);
    EXPECT_EQ(plan.mNodes.size(), 1u);
    EXPECT_EQ(plan.mUnreachable, 2u);
}

TEST(NifSceneSkeleton, a_parent_outside_the_scene_makes_a_root_and_a_self_parent_is_cut)
{
    Builder b;
    Node a;
    a.mName = "a";
    a.mParent = 99;
    Node self;
    self.mName = "self";
    self.mParent = 1;
    b.scene.mNodes = { a, self };
    const Skeleton::Plan plan = Skeleton::plan(b.scene);
    EXPECT_NE(Skeleton::find(plan, "a"), sNone);
    EXPECT_EQ(plan.mNodes[Skeleton::find(plan, "a")].mParent, sNone);
    EXPECT_EQ(Skeleton::find(plan, "self"), sNone);
    EXPECT_EQ(plan.mUnreachable, 1u);
}

TEST(NifSceneSkeleton, a_non_finite_transform_becomes_the_identity_and_is_counted)
{
    Builder b;
    const std::uint32_t root = b.node("root", sNone, { notANumber, 0, 0 });
    (void)root;
    const Skeleton::Plan plan = Skeleton::plan(b.scene);
    ASSERT_EQ(plan.mNodes.size(), 1u);
    EXPECT_FLOAT_EQ(plan.mNodes[0].mLocal.mTranslation[0], 0.f);
    EXPECT_EQ(plan.mNonFiniteTransforms, 1u);
}

TEST(NifSceneSkeleton, duplicate_names_are_counted_and_the_first_is_found)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    b.node("Bone", root, { 1, 0, 0 });
    b.node("bone", root, { 2, 0, 0 });
    const Skeleton::Plan plan = Skeleton::plan(b.scene);
    EXPECT_EQ(plan.mDuplicateNames, 1u);
    EXPECT_FLOAT_EQ(plan.mNodes[Skeleton::find(plan, "BONE")].mLocal.mTranslation[0], 1.f);
}

TEST(NifSceneSkeleton, the_node_cap_bounds_the_plan_and_counts_the_rest)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    for (int i = 0; i < 10; ++i)
        b.node("n" + std::to_string(i), root);
    Skeleton::Limits limits;
    limits.mMaxNodes = 4;
    const Skeleton::Plan plan = Skeleton::plan(b.scene, limits);
    EXPECT_EQ(plan.mNodes.size(), 4u);
    EXPECT_EQ(plan.mUnreachable, 7u);
}

TEST(NifSceneSkeleton, a_long_name_is_cut)
{
    Builder b;
    b.node(std::string(1000, 'x'));
    Skeleton::Limits limits;
    limits.mMaxNameBytes = 16;
    EXPECT_EQ(Skeleton::plan(b.scene, limits).mNodes[0].mName.size(), 16u);
}

// ---- Task 0125: the facegen tint texture ---------------------------------------------------------------------------

namespace
{
    // A material whose texture set has the nine Skyrim slots and whose shader has the given type.
    std::uint32_t faceMaterial(Builder& b, std::uint32_t shaderType, std::size_t slots, const std::string& slot6)
    {
        TextureSet set;
        set.mTextures.assign(slots, std::string());
        set.mTextures[0] = "Actors\\Character\\Male\\MaleHead.dds";
        if (slots > 6)
            set.mTextures[6] = slot6;
        b.scene.mTextureSets.push_back(set);
        ShaderMaterial m;
        m.mShaderType = shaderType;
        m.mTextureSet = static_cast<std::uint32_t>(b.scene.mTextureSets.size() - 1);
        b.scene.mMaterials.push_back(m);
        return static_cast<std::uint32_t>(b.scene.mMaterials.size() - 1);
    }
}

TEST(NifSceneSkinned, a_face_tint_shader_reports_its_slot_six_texture)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    b.skinnedQuad(root,
        faceMaterial(b, 4, 9, "data\\Textures\\Actors\\Character\\FaceGenData\\FaceTint\\Skyrim.esm\\00013255.dds"));
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mShaderType, 4u);
    EXPECT_EQ(model.mShapes[0].mTintTexture,
        "data\\Textures\\Actors\\Character\\FaceGenData\\FaceTint\\Skyrim.esm\\00013255.dds");
    EXPECT_EQ(model.mShapes[0].mKey.mDiffuse, "Actors\\Character\\Male\\MaleHead.dds");
}

TEST(NifSceneSkinned, only_a_face_tint_shader_has_a_tint_texture)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    b.skinnedQuad(root, faceMaterial(b, 0, 9, "textures\\env.dds")); // slot 6 of a default shader is no tint
    b.skinnedQuad(root, faceMaterial(b, 5, 9, "textures\\env.dds")); // a skin tint shader is not a face
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 2u);
    for (const Skinned::Shape& s : model.mShapes)
        EXPECT_TRUE(s.mTintTexture.empty());
    EXPECT_EQ(model.mShapes[0].mShaderType, 0u);
    EXPECT_EQ(model.mShapes[1].mShaderType, 5u);
}

TEST(NifSceneSkinned, a_face_tint_shader_with_no_slot_six_or_a_too_long_path_has_no_tint)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    b.skinnedQuad(root, faceMaterial(b, 4, 3, "")); // a texture set of three slots
    b.skinnedQuad(root, faceMaterial(b, 4, 9, "")); // slot 6 empty
    b.skinnedQuad(root, faceMaterial(b, 4, 9, std::string(300, 'x'))); // over the path limit
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 3u);
    for (const Skinned::Shape& s : model.mShapes)
        EXPECT_TRUE(s.mTintTexture.empty());
}

TEST(NifSceneSkinned, a_hair_tint_shader_reports_its_colour)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    const std::uint32_t m = b.material("textures/hair.dds");
    b.scene.mMaterials[m].mShaderType = 6;
    b.scene.mMaterials[m].mHairTintColor = { 0.5f, 0.25f, 0.125f };
    b.skinnedQuad(root, m);
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mHasTintColor);
    EXPECT_FLOAT_EQ(model.mShapes[0].mTintColor[0], 0.5f);
    EXPECT_FLOAT_EQ(model.mShapes[0].mTintColor[1], 0.25f);
    EXPECT_FLOAT_EQ(model.mShapes[0].mTintColor[2], 0.125f);
}

TEST(NifSceneSkinned, only_a_hair_tint_shader_has_a_tint_colour_and_a_bad_colour_is_ignored)
{
    Builder b;
    const std::uint32_t root = b.node("root");
    const std::uint32_t plain = b.material("textures/a.dds");
    b.scene.mMaterials[plain].mHairTintColor = { 0.5f, 0.5f, 0.5f }; // stored, but the shader is not a hair tint
    b.skinnedQuad(root, plain);
    const std::uint32_t bad = b.material("textures/b.dds");
    b.scene.mMaterials[bad].mShaderType = 6;
    b.scene.mMaterials[bad].mHairTintColor = { notANumber, 0.5f, 0.5f };
    b.skinnedQuad(root, bad);
    const Skinned::SkinnedModel model = Skinned::flattenSkinned(b.scene);
    ASSERT_EQ(model.mShapes.size(), 2u);
    EXPECT_FALSE(model.mShapes[0].mHasTintColor);
    EXPECT_FALSE(model.mShapes[1].mHasTintColor);
}
