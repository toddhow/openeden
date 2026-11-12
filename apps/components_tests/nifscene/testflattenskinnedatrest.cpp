// Task 0136 (skinned trunks): Limits::mSkinnedAtRest. A skinned shape of a static model (a tree's trunk and branches under
// a BSTreeNode) is drawn as a static shape in its rest pose: each vertex is the weighted mean of
// boneWorld_i * bind_i * vertex over its influences, with the bone nodes where the file's own node tree has them. The
// survey (docs/tasks/0136-skyrim-tree-sway.md, stage log) found that for 37 of the 110 tree shapes the stored vertices
// are NOT in the model frame (the lowest vertex 428 units below the ground on average) and the bone rule puts them on the
// ground, so the node-transform-only rule is wrong. The skin data's own transform must NOT be applied (task a8203e2cfb).
// Scenes are built by hand; no game asset is read.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <string>

#include <components/nifscene/flatten.hpp>

namespace
{
    using namespace NifScene::Flat;
    using NifScene::Scene;

    constexpr std::uint32_t treeAnim = 1u << 29; // flags 2

    // nodes: 0 root at (10, 0, 0); 1 "boneA" child of root (local 0); 2 "boneB" child of root (local 0). The shape node
    // is a child of the root at (0, 0, 5). A bind that agrees with the node tree is inverse(bone world) * shape world,
    // which for these bones (world (10, 0, 0)) and this shape node (world (10, 0, 5)) is a translation of (0, 0, 5).
    struct Builder
    {
        Scene scene;

        Builder()
        {
            NifScene::Node root;
            root.mName = "root";
            root.mTransform.mTranslation = { 10.f, 0.f, 0.f };
            scene.mNodes.push_back(root);
            scene.mRoots.push_back(0);
            for (const char* name : { "boneA", "boneB" })
            {
                NifScene::Node bone;
                bone.mName = name;
                bone.mParent = 0;
                scene.mNodes.push_back(bone);
            }
        }

        // A quad; skinned shapes use `bones` (node index, bind translation) and `weights` (per vertex: weight on bone 0,
        // the rest on bone 1 when there are two bones).
        std::uint32_t quad(std::uint32_t flags2 = ShaderFlags::sVertexColors, bool effect = false,
            const std::string& diffuse = "textures\\bark_d.dds")
        {
            NifScene::ShaderMaterial m;
            m.mKind = effect ? NifScene::ShaderKind::Effect : NifScene::ShaderKind::Lighting;
            m.mFlags1 = ShaderFlags::sVertexAlpha;
            m.mFlags2 = flags2;
            NifScene::TextureSet set;
            set.mTextures = { diffuse };
            scene.mTextureSets.push_back(set);
            m.mTextureSet = static_cast<std::uint32_t>(scene.mTextureSets.size() - 1);
            scene.mMaterials.push_back(m);

            NifScene::Node n;
            n.mName = "shape";
            n.mParent = 0;
            n.mTransform.mTranslation = { 0.f, 0.f, 5.f };
            scene.mNodes.push_back(n);
            NifScene::Geometry g;
            g.mName = "quad";
            g.mNode = static_cast<std::uint32_t>(scene.mNodes.size() - 1);
            g.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 2 }, { 0, 0, 2 } };
            g.mNormals = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, -1, 0 }, { 0, -1, 0 } };
            g.mUV0 = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            for (int i = 0; i < 4; ++i)
                g.mColors.push_back({ 0.5f, 0.6f, 0.7f, 0.25f * static_cast<float>(i) });
            g.mTriangles = { 0, 1, 2, 0, 2, 3 };
            g.mMaterial = static_cast<std::uint32_t>(scene.mMaterials.size() - 1);
            scene.mGeometries.push_back(g);
            scene.mNodes.back().mGeometry = static_cast<std::uint32_t>(scene.mGeometries.size() - 1);
            return static_cast<std::uint32_t>(scene.mNodes.size() - 1);
        }

        // Skin the last geometry to the bone nodes given, every vertex weight 1 on bone 0 unless `weights` says.
        void skin(std::vector<std::uint32_t> boneNodes, std::vector<std::array<float, 3>> binds,
            std::array<float, 4> weightOnFirst = { 1.f, 1.f, 1.f, 1.f })
        {
            NifScene::Skin s;
            s.mBoneNodes = boneNodes;
            for (auto t : binds)
            {
                NifScene::Transform bind;
                bind.mTranslation = t;
                s.mBindTransforms.push_back(bind);
                s.mBoneNames.push_back("b");
            }
            // The skin data's own transform: deliberately wild. If it is applied the vertices move 100 units.
            s.mSkinTransform.mTranslation = { 0.f, 0.f, 100.f };
            scene.mSkins.push_back(s);
            NifScene::Geometry& g = scene.mGeometries.back();
            g.mSkin = static_cast<std::uint32_t>(scene.mSkins.size() - 1);
            g.mInfluenceStart = { 0 };
            for (int v = 0; v < 4; ++v)
            {
                const float w = weightOnFirst[static_cast<std::size_t>(v)];
                g.mInfluences.push_back({ 0, w });
                if (w < 1.f && boneNodes.size() > 1)
                    g.mInfluences.push_back({ 1, 1.f - w });
                g.mInfluenceStart.push_back(static_cast<std::uint32_t>(g.mInfluences.size()));
            }
        }
    };

    Limits on()
    {
        Limits limits;
        limits.mSkinnedAtRest = true;
        return limits;
    }

    constexpr std::array<float, 3> consistentBind{ 0.f, 0.f, 5.f };
}

TEST(NifSceneFlattenSkinnedAtRest, off_by_default_a_skinned_shape_stays_out)
{
    Builder b;
    b.quad();
    b.skin({ 1 }, { consistentBind });
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedSkinned, 1u);
    EXPECT_EQ(model.mReport.mSkinnedAtRest, 0u);
}

TEST(NifSceneFlattenSkinnedAtRest, a_bind_that_agrees_with_the_node_tree_gives_the_unskinned_placement)
{
    Builder skinned;
    skinned.quad();
    skinned.skin({ 1 }, { consistentBind });
    Builder plain;
    plain.quad();
    const FlatModel a = flatten(skinned.scene, on());
    const FlatModel b = flatten(plain.scene, on());
    ASSERT_EQ(a.mShapes.size(), 1u);
    ASSERT_EQ(b.mShapes.size(), 1u);
    EXPECT_EQ(a.mReport.mSkinnedAtRest, 1u);
    EXPECT_EQ(a.mReport.mSkippedSkinned, 0u);
    EXPECT_EQ(a.mReport.mDrawn, 1u);
    // root (10, 0, 0) + shape node (0, 0, 5): the vertex (1, 0, 2) is at (11, 0, 7)
    EXPECT_FLOAT_EQ(a.mShapes[0].mPositions[2][0], 11.f);
    EXPECT_FLOAT_EQ(a.mShapes[0].mPositions[2][2], 7.f);
    EXPECT_EQ(a.mShapes[0].mPositions, b.mShapes[0].mPositions);
    EXPECT_EQ(a.mShapes[0].mNormals, b.mShapes[0].mNormals);
    EXPECT_EQ(a.mShapes[0].mUV, b.mShapes[0].mUV);
    EXPECT_EQ(a.mShapes[0].mIndices, b.mShapes[0].mIndices);
    EXPECT_EQ(a.mBoundsMin, b.mBoundsMin);
    EXPECT_EQ(a.mBoundsMax, b.mBoundsMax);
}

TEST(NifSceneFlattenSkinnedAtRest, a_bone_off_its_bind_pose_carries_its_vertices)
{
    // The bone node sits 3 units above where the bind expects it: the whole shape is 3 units higher than its node says.
    Builder b;
    b.quad();
    b.scene.mNodes[1].mTransform.mTranslation = { 0.f, 0.f, 3.f };
    b.skin({ 1 }, { consistentBind });
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mPositions[2][0], 11.f);
    EXPECT_FLOAT_EQ(model.mShapes[0].mPositions[2][2], 10.f);
}

TEST(NifSceneFlattenSkinnedAtRest, two_bones_blend_by_weight)
{
    Builder b;
    b.quad();
    b.scene.mNodes[2].mTransform.mTranslation = { 4.f, 0.f, 0.f }; // boneB 4 units off its bind
    b.skin({ 1, 2 }, { consistentBind, consistentBind }, { 1.f, 0.5f, 0.25f, 0.f });
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    const auto& p = model.mShapes[0].mPositions;
    // vertex 0 at (10, 0, 5): full weight on boneA, no move; vertex 1 half, 2 a quarter on boneA: moved 2 and 3; 3 all B
    EXPECT_FLOAT_EQ(p[0][0], 10.f);
    EXPECT_FLOAT_EQ(p[1][0], 11.f + 2.f);
    EXPECT_FLOAT_EQ(p[2][0], 11.f + 3.f);
    EXPECT_FLOAT_EQ(p[3][0], 10.f + 4.f);
}

TEST(NifSceneFlattenSkinnedAtRest, weights_that_do_not_sum_to_one_are_renormalised)
{
    Builder b;
    b.quad();
    b.skin({ 1 }, { consistentBind });
    for (auto& influence : b.scene.mGeometries.back().mInfluences)
        influence.mWeight = 0.5f; // a sum of 0.5 would pull every vertex halfway to the origin without normalising
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mPositions[2][0], 11.f);
    EXPECT_FLOAT_EQ(model.mShapes[0].mPositions[2][2], 7.f);
}

TEST(NifSceneFlattenSkinnedAtRest, the_skin_datas_own_transform_is_not_applied)
{
    // Builder::skin sets a skin transform of (0, 0, 100). The result of a_bind_that_agrees... would be 100 higher.
    Builder b;
    b.quad();
    b.skin({ 1 }, { consistentBind });
    ASSERT_FLOAT_EQ(b.scene.mSkins[0].mSkinTransform.mTranslation[2], 100.f);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_LT(model.mShapes[0].mPositions[2][2], 8.f);
}

TEST(NifSceneFlattenSkinnedAtRest, a_vertex_without_influence_keeps_its_node_pose)
{
    Builder b;
    b.quad();
    b.skin({ 1 }, { { 0.f, 0.f, 50.f } }); // a wrong bind that would throw every skinned vertex 45 units up
    auto& g = b.scene.mGeometries.back();
    g.mInfluences = { { 0, 1.f }, { 0, 1.f }, { 0, 1.f } };
    g.mInfluenceStart = { 0, 1, 2, 3, 3 }; // vertex 3 has none
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mPositions[3][2], 7.f); // (0, 0, 2) + node (10, 0, 5)
    EXPECT_FLOAT_EQ(model.mShapes[0].mPositions[0][2], 50.f); // skinned: the bind's 50 on top of the bone at z 0
}

TEST(NifSceneFlattenSkinnedAtRest, an_influence_on_a_bone_the_skin_lacks_is_ignored)
{
    Builder b;
    b.quad();
    b.skin({ 1 }, { consistentBind });
    b.scene.mGeometries.back().mInfluences[0].mBone = 9;
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mPositions[0][2], 5.f); // no usable influence: the node pose
}

TEST(NifSceneFlattenSkinnedAtRest, the_normal_turns_with_the_bone)
{
    Builder b;
    b.quad();
    // the bone is rotated 90 degrees about Z (row-major: x' = -y, y' = x); the bind undoes nothing, so the shape turns
    b.scene.mNodes[1].mTransform.mRotation = { 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
    b.skin({ 1 }, { consistentBind });
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    const auto& n = model.mShapes[0].mNormals[0];
    EXPECT_NEAR(n[0], 1.f, 1e-5);
    EXPECT_NEAR(n[1], 0.f, 1e-5);
    EXPECT_NEAR(n[2], 0.f, 1e-5);
}

TEST(NifSceneFlattenSkinnedAtRest, a_skinned_shape_with_a_missing_bone_node_stays_out)
{
    Builder b;
    b.quad();
    b.skin({ NifScene::sNone }, { consistentBind });
    const FlatModel model = flatten(b.scene, on());
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedSkinned, 1u);
    EXPECT_EQ(model.mReport.mSkinnedAtRest, 0u);
}

TEST(NifSceneFlattenSkinnedAtRest, a_skin_with_a_different_number_of_binds_and_bones_stays_out)
{
    Builder b;
    b.quad();
    b.skin({ 1, 2 }, { consistentBind });
    const FlatModel model = flatten(b.scene, on());
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedSkinned, 1u);
}

TEST(NifSceneFlattenSkinnedAtRest, a_skinned_and_an_unskinned_shape_with_one_material_merge)
{
    Builder b;
    b.quad();
    b.skin({ 1 }, { consistentBind });
    b.quad();
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mPositions.size(), 8u);
    EXPECT_EQ(model.mReport.mDrawn, 2u);
    EXPECT_EQ(model.mReport.mSkinnedAtRest, 1u);
}

TEST(NifSceneFlattenSkinnedAtRest, hidden_and_effect_and_materialless_skinned_shapes_stay_out)
{
    Builder b;
    const std::uint32_t hiddenNode = b.quad();
    b.skin({ 1 }, { consistentBind });
    b.scene.mNodes[hiddenNode].mFlags = 1u;
    b.quad(ShaderFlags::sVertexColors, true); // effect shader
    b.skin({ 1 }, { consistentBind });
    b.quad();
    b.skin({ 1 }, { consistentBind });
    b.scene.mGeometries.back().mMaterial = NifScene::sNone;
    const FlatModel model = flatten(b.scene, on());
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedHidden, 1u);
    EXPECT_EQ(model.mReport.mSkippedEffect, 1u);
    EXPECT_EQ(model.mReport.mSkippedNoMaterial, 1u);
    EXPECT_EQ(model.mReport.mSkinnedAtRest, 0u);
}

TEST(NifSceneFlattenSkinnedAtRest, a_skinned_tree_anim_shape_is_marked_for_sway_when_sway_is_on)
{
    Builder b;
    b.quad(ShaderFlags::sVertexColors | treeAnim);
    b.skin({ 1 }, { consistentBind });
    Limits limits = on();
    limits.mTreeSway = true;
    const FlatModel model = flatten(b.scene, limits);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mKey.mSway);
    EXPECT_EQ(model.mReport.mSwayShapes, 1u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mColors[3][3], 0.75f); // the file's alpha, the vertex's flexibility
}

TEST(NifSceneFlattenSkinnedAtRest, a_skinned_tree_anim_shape_is_not_marked_when_sway_is_off)
{
    Builder b;
    b.quad(ShaderFlags::sVertexColors | treeAnim);
    b.skin({ 1 }, { consistentBind });
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FALSE(model.mShapes[0].mKey.mSway);
}

TEST(NifSceneFlattenSkinnedAtRest, a_skinned_shape_gets_no_normal_map_with_surface_maps_on_but_an_unskinned_one_does)
{
    for (bool skinned : { false, true })
    {
        Builder b;
        b.quad();
        b.scene.mTextureSets.back().mTextures.push_back("textures\bark_n.dds");
        if (skinned)
            b.skin({ 1 }, { consistentBind });
        b.scene.mGeometries.back().mTangents.assign(4, { 1.f, 0.f, 0.f, 1.f });
        Limits limits = on();
        limits.mSurfaceMaps = true;
        const FlatModel model = flatten(b.scene, limits);
        ASSERT_EQ(model.mShapes.size(), 1u) << skinned;
        EXPECT_EQ(model.mShapes[0].mTangents.empty(), skinned) << skinned; // the control: unskinned keeps them
        EXPECT_EQ(model.mShapes[0].mKey.mNormal.empty(), skinned) << skinned;
    }
}

TEST(NifSceneFlattenSkinnedAtRest, a_negative_weight_is_not_an_influence)
{
    Builder b;
    b.quad();
    b.scene.mNodes[1].mTransform.mTranslation = { 0.f, 0.f, 3.f }; // boneA 3 above its bind
    b.skin({ 1, 2 }, { consistentBind, consistentBind });
    auto& g = b.scene.mGeometries.back();
    g.mInfluences = { { 0, 1.f }, { 1, -1.f }, { 0, 1.f }, { 0, 1.f }, { 0, 1.f } };
    g.mInfluenceStart = { 0, 2, 3, 4, 5 };
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mPositions[0][2], 8.f); // boneA only: (10, 0, 3) + bind (0, 0, 5)
}

TEST(NifSceneFlattenSkinnedAtRest, an_out_of_range_skin_index_is_still_an_unskinned_shape)
{
    Builder b;
    b.quad();
    b.scene.mGeometries.back().mSkin = 7;
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mReport.mSkinnedAtRest, 0u);
}
