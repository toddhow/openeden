// Task 0133 (Skyrim tree sway): what flatten does with a Tree_Anim shape (shader flags 2 bit 29) when
// Limits::mTreeSway is on: the shape is marked (MaterialKey::mSway), it is not merged with a still shape, and its vertex
// alpha (the file's per-vertex flexibility, see docs/tasks/0133-skyrim-tree-sway.md) is kept whatever the Vertex_Alpha
// flag says. Off (the default) nothing changes. Scenes are built by hand; no game asset is read.
#include <gtest/gtest.h>

#include <array>
#include <string>

#include <components/nifscene/flatten.hpp>

namespace
{
    using namespace NifScene::Flat;
    using NifScene::Scene;

    constexpr std::uint32_t treeAnim = 1u << 29; // flags 2

    struct Spec
    {
        std::string diffuse = "textures\\leaf_d.dds";
        std::uint32_t flags1 = ShaderFlags::sVertexAlpha;
        std::uint32_t flags2 = ShaderFlags::sVertexColors | treeAnim;
        bool colours = true;
        bool lighting = true;
        std::array<float, 4> alphas{ 0.f, 0.25f, 0.5f, 1.f };
    };

    struct Builder
    {
        Scene scene;

        Builder()
        {
            NifScene::Node n;
            n.mName = "root";
            scene.mNodes.push_back(n);
            scene.mRoots.push_back(0);
        }

        void quad(const Spec& spec, float z = 0.f)
        {
            NifScene::ShaderMaterial m;
            m.mKind = spec.lighting ? NifScene::ShaderKind::Lighting : NifScene::ShaderKind::Effect;
            m.mFlags1 = spec.flags1;
            m.mFlags2 = spec.flags2;
            NifScene::TextureSet set;
            set.mTextures = { spec.diffuse };
            scene.mTextureSets.push_back(set);
            m.mTextureSet = static_cast<std::uint32_t>(scene.mTextureSets.size() - 1);
            scene.mMaterials.push_back(m);

            NifScene::Node n;
            n.mName = "quad";
            n.mParent = 0;
            scene.mNodes.push_back(n);
            NifScene::Geometry g;
            g.mName = "quad";
            g.mNode = static_cast<std::uint32_t>(scene.mNodes.size() - 1);
            g.mPositions = { { 0, 0, z }, { 1, 0, z }, { 1, 0, z + 2 }, { 0, 0, z + 2 } };
            g.mNormals = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, -1, 0 }, { 0, -1, 0 } };
            g.mUV0 = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            if (spec.colours)
                for (int i = 0; i < 4; ++i)
                    g.mColors.push_back({ 0.5f, 0.6f, 0.7f, spec.alphas[static_cast<std::size_t>(i)] });
            g.mTriangles = { 0, 1, 2, 0, 2, 3 };
            g.mMaterial = static_cast<std::uint32_t>(scene.mMaterials.size() - 1);
            scene.mGeometries.push_back(g);
            scene.mNodes.back().mGeometry = static_cast<std::uint32_t>(scene.mGeometries.size() - 1);
        }
    };

    Limits on()
    {
        Limits limits;
        limits.mTreeSway = true;
        return limits;
    }
}

TEST(NifSceneFlattenTreeSway, the_flag_bit_is_flags_2_bit_29)
{
    EXPECT_EQ(ShaderFlags::sTreeAnim, 1u << 29);
}

TEST(NifSceneFlattenTreeSway, off_by_default_a_tree_shape_is_what_it_was)
{
    Builder b;
    Spec s;
    s.flags1 = 0; // no Vertex_Alpha: the old rule drops the alpha
    b.quad(s);
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FALSE(model.mShapes[0].mKey.mSway);
    ASSERT_EQ(model.mShapes[0].mColors.size(), 4u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mColors[1][3], 1.f);
    EXPECT_EQ(model.mReport.mSwayShapes, 0u);
}

TEST(NifSceneFlattenTreeSway, on_a_tree_anim_shape_is_marked_and_keeps_the_files_alpha)
{
    Builder b;
    Spec s;
    s.flags1 = 0; // alpha is kept although the Vertex_Alpha flag is clear
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    const Shape& shape = model.mShapes[0];
    EXPECT_TRUE(shape.mKey.mSway);
    ASSERT_EQ(shape.mColors.size(), 4u);
    EXPECT_FLOAT_EQ(shape.mColors[0][3], 0.f);
    EXPECT_FLOAT_EQ(shape.mColors[1][3], 0.25f);
    EXPECT_FLOAT_EQ(shape.mColors[2][3], 0.5f);
    EXPECT_FLOAT_EQ(shape.mColors[3][3], 1.f);
    EXPECT_FLOAT_EQ(shape.mColors[1][0], 0.5f); // the colour itself is untouched
    EXPECT_EQ(model.mReport.mSwayShapes, 1u);
}

TEST(NifSceneFlattenTreeSway, a_shape_without_the_flag_is_not_marked_and_is_not_merged_with_one_that_has_it)
{
    Builder b;
    Spec still;
    still.flags2 = ShaderFlags::sVertexColors;
    b.quad(still);
    b.quad(Spec{}, 3.f);
    Spec sameStill = still;
    b.quad(sameStill, 6.f);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 2u);
    int swaying = 0, stillShapes = 0, stillMerged = 0;
    for (const Shape& shape : model.mShapes)
    {
        if (shape.mKey.mSway)
            ++swaying;
        else
        {
            ++stillShapes;
            stillMerged = static_cast<int>(shape.mMergedFrom);
        }
    }
    EXPECT_EQ(swaying, 1);
    EXPECT_EQ(stillShapes, 1);
    EXPECT_EQ(stillMerged, 2); // the two still quads merged as before
}

TEST(NifSceneFlattenTreeSway, a_tree_shape_without_vertex_colours_is_not_marked)
{
    Builder b;
    Spec s;
    s.colours = false;
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FALSE(model.mShapes[0].mKey.mSway);
    EXPECT_EQ(model.mReport.mSwayShapes, 0u);
}

TEST(NifSceneFlattenTreeSway, a_tree_shape_whose_shader_does_not_use_vertex_colours_is_not_marked)
{
    Builder b;
    Spec s;
    s.flags2 = treeAnim; // Vertex_Colors clear: the colours are not used by the shader
    b.quad(s);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_FALSE(model.mShapes[0].mKey.mSway);
}

TEST(NifSceneFlattenTreeSway, the_model_height_is_the_z_extent_of_everything_drawn)
{
    Builder b;
    b.quad(Spec{}, 10.f);
    b.quad(Spec{}, 40.f);
    const FlatModel model = flatten(b.scene, on());
    EXPECT_FLOAT_EQ(model.mBoundsMax[2] - model.mBoundsMin[2], 32.f);
}

TEST(NifSceneFlattenTreeSway, the_option_changes_nothing_for_a_model_with_no_tree_shape)
{
    Builder b;
    Spec s;
    s.flags2 = ShaderFlags::sVertexColors;
    b.quad(s);
    const FlatModel off = flatten(b.scene);
    const FlatModel onModel = flatten(b.scene, on());
    ASSERT_EQ(off.mShapes.size(), onModel.mShapes.size());
    EXPECT_EQ(off.mShapes[0].mKey, onModel.mShapes[0].mKey);
    EXPECT_EQ(off.mShapes[0].mColors, onModel.mShapes[0].mColors);
}
