// Task 0110 (neutral world stage N3a-4): the pure half of drawing a Skyrim SE scene in the game
// (components/nifscene/flatten.*): node transforms baked into vertices, material reduced to a key, shapes
// merged by key, and every hostile input counted and survived. Scenes are built by hand; no game asset is read.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include <components/nifscene/flatten.hpp>
#include <components/nifscene/header.hpp>

namespace
{
    using namespace NifScene::Flat;
    using NifScene::Scene;

    constexpr float notANumber = std::numeric_limits<float>::quiet_NaN();

    struct Builder
    {
        Scene scene;

        // A transform node; parent sNone makes it a root. Returns its index.
        std::uint32_t node(const std::string& name, std::uint32_t parent = NifScene::sNone,
            std::array<float, 3> translation = { 0, 0, 0 }, float scale = 1.f, std::uint32_t flags = 0)
        {
            NifScene::Node n;
            n.mName = name;
            n.mParent = parent;
            n.mFlags = flags;
            n.mTransform.mTranslation = translation;
            n.mTransform.mScale = scale;
            scene.mNodes.push_back(n);
            const auto index = static_cast<std::uint32_t>(scene.mNodes.size() - 1);
            if (parent == NifScene::sNone)
                scene.mRoots.push_back(index);
            return index;
        }

        std::uint32_t textureSet(const std::string& diffuse)
        {
            scene.mTextureSets.push_back({ { diffuse } });
            return static_cast<std::uint32_t>(scene.mTextureSets.size() - 1);
        }

        std::uint32_t material(const std::string& diffuse, std::uint32_t flags1 = 0, std::uint32_t flags2 = 0)
        {
            NifScene::ShaderMaterial m;
            m.mKind = NifScene::ShaderKind::Lighting;
            m.mFlags1 = flags1;
            m.mFlags2 = flags2;
            if (!diffuse.empty())
                m.mTextureSet = textureSet(diffuse);
            scene.mMaterials.push_back(m);
            return static_cast<std::uint32_t>(scene.mMaterials.size() - 1);
        }

        // A one-quad shape (two triangles in the XY plane, corners (0,0), (1,0), (1,1), (0,1)) on a new node under
        // `parent`. Returns the geometry index.
        std::uint32_t quad(std::uint32_t parent, std::uint32_t materialIndex, const std::string& name = "quad")
        {
            const std::uint32_t nodeIndex = node(name, parent);
            NifScene::Geometry g;
            g.mName = name;
            g.mNode = nodeIndex;
            g.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
            g.mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
            g.mUV0 = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            g.mTriangles = { 0, 1, 2, 0, 2, 3 };
            g.mMaterial = materialIndex;
            scene.mGeometries.push_back(g);
            const auto index = static_cast<std::uint32_t>(scene.mGeometries.size() - 1);
            scene.mNodes[nodeIndex].mGeometry = index;
            return index;
        }

        NifScene::Geometry& geometry(std::uint32_t index) { return scene.mGeometries[index]; }
    };

    bool near(float a, float b, float eps = 1e-5f)
    {
        return std::fabs(a - b) <= eps;
    }
}

TEST(NifSceneFlatten, a_plain_quad_becomes_one_shape_with_its_streams)
{
    Builder b;
    const auto root = b.node("root");
    b.quad(root, b.material("textures\\a.dds"));
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    const Shape& s = model.mShapes[0];
    EXPECT_EQ(s.mPositions.size(), 4u);
    EXPECT_EQ(s.mNormals.size(), 4u);
    EXPECT_EQ(s.mUV.size(), 4u);
    EXPECT_EQ(s.mIndices, (std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 }));
    EXPECT_EQ(s.mKey.mDiffuse, "textures\\a.dds");
    EXPECT_EQ(model.mReport.mGeometries, 1u);
    EXPECT_EQ(model.mReport.mDrawn, 1u);
    EXPECT_EQ(model.mReport.mVertices, 4u);
    EXPECT_EQ(model.mReport.mTriangles, 2u);
    EXPECT_EQ(model.mReport.mTexturesNamed, 1u);
}

TEST(NifSceneFlatten, a_node_translation_and_scale_are_baked_into_the_positions)
{
    Builder b;
    const auto root = b.node("root", NifScene::sNone, { 10, 20, 30 }, 2.f);
    b.quad(root, b.material(""));
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const Shape& s = model.mShapes[0];
    // (1, 1, 0) scaled by 2 and moved by (10, 20, 30)
    EXPECT_TRUE(near(s.mPositions[2][0], 12.f));
    EXPECT_TRUE(near(s.mPositions[2][1], 22.f));
    EXPECT_TRUE(near(s.mPositions[2][2], 30.f));
}

TEST(NifSceneFlatten, the_parent_chain_is_composed_parent_after_child)
{
    Builder b;
    const auto root = b.node("root", NifScene::sNone, { 100, 0, 0 });
    // The child turns 90 degrees about Z (row-major rotation, x -> y) and moves by (0, 5, 0) in its parent's frame.
    const auto child = b.node("child", root, { 0, 5, 0 });
    b.scene.mNodes[child].mTransform.mRotation = { 0, -1, 0, 1, 0, 0, 0, 0, 1 };
    b.quad(child, b.material(""));
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const Shape& s = model.mShapes[0];
    // vertex 1 = (1, 0, 0): rotated to (0, 1, 0), + (0, 5, 0) = (0, 6, 0), + (100, 0, 0) = (100, 6, 0)
    EXPECT_TRUE(near(s.mPositions[1][0], 100.f));
    EXPECT_TRUE(near(s.mPositions[1][1], 6.f));
    EXPECT_TRUE(near(s.mPositions[1][2], 0.f));
    // the normal (0, 0, 1) is unchanged by a turn about Z and stays unit length
    EXPECT_TRUE(near(s.mNormals[1][2], 1.f));
}

TEST(NifSceneFlatten, normals_are_rotated_but_not_scaled_or_moved)
{
    Builder b;
    const auto root = b.node("root", NifScene::sNone, { 7, 7, 7 }, 3.f);
    b.scene.mNodes[root].mTransform.mRotation = { 1, 0, 0, 0, 0, -1, 0, 1, 0 };
    const auto g = b.quad(root, b.material(""));
    b.geometry(g).mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const Shape& s = model.mShapes[0];
    // (0, 0, 1) through the matrix rows (1,0,0) (0,0,-1) (0,1,0) = (0, -1, 0)
    EXPECT_TRUE(near(s.mNormals[0][0], 0.f));
    EXPECT_TRUE(near(s.mNormals[0][1], -1.f));
    EXPECT_TRUE(near(s.mNormals[0][2], 0.f));
}

TEST(NifSceneFlatten, missing_normals_are_made_from_the_triangles)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material(""));
    b.geometry(g).mNormals.clear();
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const Shape& s = model.mShapes[0];
    ASSERT_EQ(s.mNormals.size(), 4u);
    for (const auto& n : s.mNormals)
    {
        EXPECT_TRUE(near(n[0], 0.f));
        EXPECT_TRUE(near(n[1], 0.f));
        EXPECT_TRUE(near(n[2], 1.f)); // counter-clockwise seen from +Z
    }
}

TEST(NifSceneFlatten, normals_that_are_not_numbers_are_replaced_by_made_ones)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material(""));
    b.geometry(g).mNormals[2] = { notANumber, 0, 0 };
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const Shape& s = model.mShapes[0];
    for (const auto& n : s.mNormals)
    {
        EXPECT_TRUE(std::isfinite(n[0]) && std::isfinite(n[1]) && std::isfinite(n[2]));
        EXPECT_TRUE(near(n[0] * n[0] + n[1] * n[1] + n[2] * n[2], 1.f));
    }
}

TEST(NifSceneFlatten, the_uv_offset_and_scale_are_baked)
{
    Builder b;
    const auto root = b.node("root");
    const auto m = b.material("t.dds");
    b.scene.mMaterials[m].mUVOffset = { 0.5f, 0.25f };
    b.scene.mMaterials[m].mUVScale = { 2.f, 4.f };
    b.quad(root, m);
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const Shape& s = model.mShapes[0];
    ASSERT_EQ(s.mUV.size(), 4u);
    EXPECT_TRUE(near(s.mUV[2][0], 2.5f)); // 1 * 2 + 0.5
    EXPECT_TRUE(near(s.mUV[2][1], 4.25f)); // 1 * 4 + 0.25
    EXPECT_TRUE(near(s.mUV[0][0], 0.5f));
}

TEST(NifSceneFlatten, a_uv_that_is_not_a_number_becomes_zero)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("t.dds"));
    b.geometry(g).mUV0[1] = { notANumber, 1.f };
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const Shape& s = model.mShapes[0];
    EXPECT_TRUE(near(s.mUV[1][0], 0.f));
    EXPECT_TRUE(std::isfinite(s.mUV[1][1]));
}

TEST(NifSceneFlatten, a_shape_with_a_texture_but_no_uv_loses_the_texture_name)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("t.dds"));
    b.geometry(g).mUV0.clear();
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mUV.empty());
    EXPECT_TRUE(model.mShapes[0].mKey.mDiffuse.empty());
    EXPECT_EQ(model.mReport.mTexturesNamed, 0u);
}

TEST(NifSceneFlatten, a_shape_without_a_texture_set_is_untextured)
{
    Builder b;
    const auto root = b.node("root");
    b.quad(root, b.material(""));
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mKey.mDiffuse.empty());
    EXPECT_EQ(model.mReport.mTexturesNamed, 0u);
}

TEST(NifSceneFlatten, a_texture_set_index_outside_the_scene_is_untextured)
{
    Builder b;
    const auto root = b.node("root");
    const auto m = b.material("t.dds");
    b.scene.mMaterials[m].mTextureSet = 99;
    b.quad(root, m);
    EXPECT_TRUE(flatten(b.scene).mShapes.at(0).mKey.mDiffuse.empty());
}

TEST(NifSceneFlatten, a_texture_path_longer_than_the_limit_is_ignored)
{
    Builder b;
    const auto root = b.node("root");
    b.quad(root, b.material(std::string(1000, 'a')));
    EXPECT_TRUE(flatten(b.scene).mShapes.at(0).mKey.mDiffuse.empty());
}

TEST(NifSceneFlatten, the_alpha_property_gives_blend_and_test_settings)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("t.dds"));
    NifScene::AlphaProperty a;
    a.mBlend = true;
    a.mTest = true;
    a.mThreshold = 77;
    a.mSourceBlend = 6;
    a.mDestinationBlend = 7;
    a.mTestMode = 4;
    a.mNoSorter = true;
    b.scene.mAlphaProperties.push_back(a);
    b.geometry(g).mAlpha = 0;
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const MaterialKey& key = model.mShapes[0].mKey;
    EXPECT_TRUE(key.mBlend);
    EXPECT_TRUE(key.mTest);
    EXPECT_EQ(key.mThreshold, 77);
    EXPECT_EQ(key.mTestMode, 4);
    EXPECT_EQ(key.mSourceBlend, 6);
    EXPECT_EQ(key.mDestinationBlend, 7);
    EXPECT_TRUE(key.mNoSorter);
}

TEST(NifSceneFlatten, no_alpha_property_is_opaque)
{
    Builder b;
    const auto root = b.node("root");
    b.quad(root, b.material("t.dds"));
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const MaterialKey& key = model.mShapes[0].mKey;
    EXPECT_FALSE(key.mBlend);
    EXPECT_FALSE(key.mTest);
}

TEST(NifSceneFlatten, an_alpha_index_outside_the_scene_is_opaque)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("t.dds"));
    b.geometry(g).mAlpha = 12;
    EXPECT_FALSE(flatten(b.scene).mShapes.at(0).mKey.mBlend);
}

TEST(NifSceneFlatten, the_double_sided_flag_comes_from_the_second_flag_word)
{
    Builder b;
    const auto root = b.node("root");
    b.quad(root, b.material("a.dds", 0, ShaderFlags::sDoubleSided));
    b.quad(root, b.material("b.dds", ShaderFlags::sDoubleSided, 0)); // the same bit in the FIRST word means nothing
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 2u);
    EXPECT_TRUE(model.mShapes[0].mKey.mDoubleSided);
    EXPECT_FALSE(model.mShapes[1].mKey.mDoubleSided);
}

TEST(NifSceneFlatten, the_shader_flag_bits_are_the_ones_measured_on_the_installed_meshes)
{
    // Flags 1: bit 3 vertex alpha, 15 refraction, 16 fire refraction (the inn's heat planes read 0x02418309); flags 2:
    // bit 4 double sided, bit 5 vertex colours (the same planes read 0x31). Pinned as numbers so that a renamed or
    // shifted constant cannot pass by changing the test and the code together.
    EXPECT_EQ(ShaderFlags::sVertexAlpha, 0x8u);
    EXPECT_EQ(ShaderFlags::sRefraction, 0x8000u);
    EXPECT_EQ(ShaderFlags::sFireRefraction, 0x10000u);
    EXPECT_EQ(ShaderFlags::sDoubleSided, 0x10u);
    EXPECT_EQ(ShaderFlags::sVertexColors, 0x20u);
}

TEST(NifSceneFlatten, the_clamp_mode_and_alpha_value_come_from_the_shader)
{
    Builder b;
    const auto root = b.node("root");
    const auto m = b.material("a.dds");
    b.scene.mMaterials[m].mClampMode = 0;
    b.scene.mMaterials[m].mAlpha = 0.5f;
    b.quad(root, m);
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const MaterialKey& key = model.mShapes[0].mKey;
    EXPECT_EQ(key.mClampMode, 0);
    EXPECT_TRUE(near(key.mAlpha, 0.5f));
}

TEST(NifSceneFlatten, vertex_colours_are_used_only_when_the_shader_asks_for_them)
{
    Builder b;
    const auto root = b.node("root");
    const auto off = b.quad(root, b.material("a.dds", 0, 0), "off");
    const auto on = b.quad(root, b.material("b.dds", 0, ShaderFlags::sVertexColors), "on");
    for (auto g : { off, on })
        b.geometry(g).mColors = { { 1, 0.5f, 0.25f, 0.5f }, { 1, 0.5f, 0.25f, 0.5f }, { 1, 0.5f, 0.25f, 0.5f },
            { 1, 0.5f, 0.25f, 0.5f } };
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 2u);
    EXPECT_TRUE(model.mShapes[0].mColors.empty());
    EXPECT_FALSE(model.mShapes[0].mKey.mVertexColors);
    ASSERT_EQ(model.mShapes[1].mColors.size(), 4u);
    EXPECT_TRUE(model.mShapes[1].mKey.mVertexColors);
    // the file's alpha is ignored without the vertex alpha flag
    EXPECT_TRUE(near(model.mShapes[1].mColors[0][3], 1.f));
    EXPECT_TRUE(near(model.mShapes[1].mColors[0][1], 0.5f));
}

TEST(NifSceneFlatten, vertex_alpha_is_kept_with_its_flag_and_colours_are_clamped)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("b.dds", ShaderFlags::sVertexAlpha, ShaderFlags::sVertexColors));
    b.geometry(g).mColors
        = { { 2.f, -1.f, notANumber, 0.5f }, { 1, 1, 1, 0.5f }, { 1, 1, 1, 0.5f }, { 1, 1, 1, 0.5f } };
    const FlatModel model = flatten(b.scene);
    ASSERT_FALSE(model.mShapes.empty());
    const Shape& s = model.mShapes[0];
    ASSERT_EQ(s.mColors.size(), 4u);
    EXPECT_TRUE(near(s.mColors[0][0], 1.f));
    EXPECT_TRUE(near(s.mColors[0][1], 0.f));
    EXPECT_TRUE(near(s.mColors[0][2], 1.f)); // not a number: white
    EXPECT_TRUE(near(s.mColors[1][3], 0.5f));
}

TEST(NifSceneFlatten, colours_of_the_wrong_length_are_not_used)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("b.dds", 0, ShaderFlags::sVertexColors));
    b.geometry(g).mColors = { { 1, 1, 1, 1 } };
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mColors.empty());
    EXPECT_FALSE(model.mShapes[0].mKey.mVertexColors);
}

TEST(NifSceneFlatten, hidden_nodes_and_their_subtrees_are_left_out)
{
    Builder b;
    const auto root = b.node("root");
    const auto hidden = b.node("hidden", root, { 0, 0, 0 }, 1.f, 1u);
    b.quad(hidden, b.material("a.dds"), "inside");
    b.quad(root, b.material("b.dds"), "visible");
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mKey.mDiffuse, "b.dds");
    EXPECT_EQ(model.mReport.mSkippedHidden, 1u);
}

TEST(NifSceneFlatten, a_hidden_shape_node_itself_is_left_out)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("a.dds"));
    b.scene.mNodes[b.geometry(g).mNode].mFlags = 1u;
    EXPECT_TRUE(flatten(b.scene).mShapes.empty());
}

TEST(NifSceneFlatten, editor_markers_are_left_out)
{
    Builder b;
    const auto root = b.node("root");
    const auto marker = b.node("EditorMarker01", root);
    b.quad(marker, b.material("a.dds"), "inside");
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedHidden, 1u);
}

TEST(NifSceneFlatten, editor_marker_names)
{
    EXPECT_TRUE(isEditorMarkerName("EditorMarker"));
    EXPECT_TRUE(isEditorMarkerName("editormarker_1"));
    EXPECT_TRUE(isEditorMarkerName("EDITORMARKERX"));
    EXPECT_FALSE(isEditorMarkerName("Marker"));
    EXPECT_FALSE(isEditorMarkerName("my EditorMarker"));
    EXPECT_FALSE(isEditorMarkerName(""));
    EXPECT_FALSE(isEditorMarkerName("Editor"));
}

TEST(NifSceneFlatten, a_refraction_shape_is_left_out_as_an_effect)
{
    // Heat shimmer planes over fires use the refraction flags (flags 1 bits 15 and 16) and a normal map as their
    // texture: drawn as a plain surface they show as a rainbow slab (seen in the Whiterun inn, task 0110).
    Builder b;
    const auto root = b.node("root");
    b.quad(root, b.material("shimmer_n.dds", ShaderFlags::sRefraction, 0), "refraction");
    b.quad(root, b.material("shimmer2_n.dds", ShaderFlags::sFireRefraction, 0), "fire");
    b.quad(root, b.material("wall_d.dds", ShaderFlags::sVertexAlpha, 0), "wall");
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mKey.mDiffuse, "wall_d.dds");
    EXPECT_EQ(model.mReport.mSkippedEffect, 2u);
}

TEST(NifSceneFlatten, skinned_shapes_are_left_out_and_counted)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("a.dds"));
    b.geometry(g).mSkin = 0;
    b.scene.mSkins.emplace_back();
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedSkinned, 1u);
}

TEST(NifSceneFlatten, a_shape_with_no_shader_property_is_left_out)
{
    Builder b;
    const auto root = b.node("root");
    b.quad(root, NifScene::sNone);
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedNoMaterial, 1u);
}

TEST(NifSceneFlatten, a_material_index_outside_the_scene_counts_as_no_shader_property)
{
    Builder b;
    const auto root = b.node("root");
    b.quad(root, 77);
    EXPECT_EQ(flatten(b.scene).mReport.mSkippedNoMaterial, 1u);
}

TEST(NifSceneFlatten, an_effect_shader_shape_is_left_out_and_counted)
{
    Builder b;
    const auto root = b.node("root");
    const auto m = b.material("a.dds");
    b.scene.mMaterials[m].mKind = NifScene::ShaderKind::Effect;
    b.quad(root, m);
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedEffect, 1u);
}

TEST(NifSceneFlatten, shapes_with_the_same_key_are_merged_with_offset_indices)
{
    Builder b;
    const auto root = b.node("root");
    const auto m = b.material("same.dds");
    b.quad(root, m, "one");
    const auto second = b.quad(root, m, "two");
    // shift the second quad's node so the merged positions are distinguishable
    b.scene.mNodes[b.geometry(second).mNode].mTransform.mTranslation = { 5, 0, 0 };
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    const Shape& s = model.mShapes[0];
    EXPECT_EQ(s.mMergedFrom, 2u);
    EXPECT_EQ(s.mPositions.size(), 8u);
    EXPECT_EQ(s.mUV.size(), 8u);
    EXPECT_EQ(s.mNormals.size(), 8u);
    ASSERT_EQ(s.mIndices.size(), 12u);
    EXPECT_EQ(s.mIndices[6], 4u);
    EXPECT_EQ(s.mIndices[11], 7u);
    EXPECT_TRUE(near(s.mPositions[5][0], 6.f));
    EXPECT_EQ(model.mReport.mDrawn, 2u);
    EXPECT_EQ(model.mReport.mVertices, 8u);
    EXPECT_EQ(model.mReport.mTriangles, 4u);
}

TEST(NifSceneFlatten, shapes_with_different_keys_stay_apart_in_file_order)
{
    Builder b;
    const auto root = b.node("root");
    b.quad(root, b.material("a.dds"), "a");
    b.quad(root, b.material("b.dds"), "b");
    b.quad(root, b.material("a.dds"), "a2");
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 2u);
    EXPECT_EQ(model.mShapes[0].mKey.mDiffuse, "a.dds");
    EXPECT_EQ(model.mShapes[0].mMergedFrom, 2u);
    EXPECT_EQ(model.mShapes[1].mKey.mDiffuse, "b.dds");
    EXPECT_EQ(model.mReport.mTexturesNamed, 2u);
}

TEST(NifSceneFlatten, a_shape_with_uvs_and_one_without_are_not_merged)
{
    Builder b;
    const auto root = b.node("root");
    const auto m = b.material("");
    b.quad(root, m, "with");
    const auto without = b.quad(root, m, "without");
    b.geometry(without).mUV0.clear();
    EXPECT_EQ(flatten(b.scene).mShapes.size(), 2u);
}

TEST(NifSceneFlatten, a_merged_shape_never_grows_past_the_limit)
{
    Builder b;
    const auto root = b.node("root");
    const auto m = b.material("same.dds");
    for (int i = 0; i < 5; ++i)
        b.quad(root, m);
    Limits limits;
    limits.mMaxMergedVertices = 10; // two quads (8 vertices) fit, a third does not
    const FlatModel model = flatten(b.scene, limits);
    ASSERT_EQ(model.mShapes.size(), 3u);
    EXPECT_EQ(model.mShapes[0].mMergedFrom, 2u);
    EXPECT_EQ(model.mShapes[1].mMergedFrom, 2u);
    EXPECT_EQ(model.mShapes[2].mMergedFrom, 1u);
    for (const Shape& s : model.mShapes)
        EXPECT_LE(s.mPositions.size(), 10u);
}

TEST(NifSceneFlatten, triangles_with_an_index_outside_the_shape_are_dropped_and_counted)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("a.dds"));
    b.geometry(g).mTriangles = { 0, 1, 2, 0, 2, 9 };
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mIndices, (std::vector<std::uint32_t>{ 0, 1, 2 }));
    EXPECT_EQ(model.mReport.mDroppedTriangles, 1u);
}

TEST(NifSceneFlatten, a_trailing_partial_triangle_is_dropped)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("a.dds"));
    b.geometry(g).mTriangles = { 0, 1, 2, 0, 2 };
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mIndices.size(), 3u);
    EXPECT_EQ(model.mReport.mDroppedTriangles, 1u);
}

TEST(NifSceneFlatten, a_vertex_that_is_not_a_number_drops_its_triangles_and_nothing_else)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("a.dds"));
    b.geometry(g).mPositions[3] = { notANumber, 0, 0 }; // used only by the second triangle
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mIndices, (std::vector<std::uint32_t>{ 0, 1, 2 }));
    EXPECT_EQ(model.mReport.mBadVertices, 1u);
    EXPECT_EQ(model.mReport.mDroppedTriangles, 1u);
    for (const auto& p : model.mShapes[0].mPositions)
        EXPECT_TRUE(std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]));
}

TEST(NifSceneFlatten, a_coordinate_beyond_the_limit_is_a_bad_vertex)
{
    Builder b;
    const auto root = b.node("root");
    const auto g = b.quad(root, b.material("a.dds"));
    b.geometry(g).mPositions[0] = { 1e9f, 0, 0 };
    const FlatModel model = flatten(b.scene);
    EXPECT_EQ(model.mReport.mBadVertices, 1u);
    EXPECT_EQ(model.mReport.mDroppedTriangles, 2u); // both triangles use vertex 0
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedNoGeometry, 1u);
}

TEST(NifSceneFlatten, a_node_scale_that_blows_coordinates_up_makes_bad_vertices)
{
    Builder b;
    const auto root = b.node("root", NifScene::sNone, { 0, 0, 0 }, 1e30f);
    b.quad(root, b.material("a.dds"));
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_GT(model.mReport.mBadVertices, 0u);
}

TEST(NifSceneFlatten, a_node_transform_that_is_not_a_number_is_treated_as_identity)
{
    Builder b;
    const auto root = b.node("root", NifScene::sNone, { notANumber, 0, 0 });
    b.quad(root, b.material("a.dds"));
    const FlatModel model = flatten(b.scene);
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(near(model.mShapes[0].mPositions[1][0], 1.f));
}

TEST(NifSceneFlatten, no_vertex_data_and_no_triangles_are_skipped)
{
    Builder b;
    const auto root = b.node("root");
    const auto g1 = b.quad(root, b.material("a.dds"), "novertices");
    b.geometry(g1).mNoVertexData = true;
    b.geometry(g1).mPositions.clear();
    const auto g2 = b.quad(root, b.material("b.dds"), "notriangles");
    b.geometry(g2).mTriangles.clear();
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedNoGeometry, 2u);
}

TEST(NifSceneFlatten, a_node_whose_parent_is_outside_the_scene_is_a_root)
{
    Builder b;
    const auto n = b.node("lost", 77);
    b.scene.mRoots.clear();
    b.quad(n, b.material("a.dds"));
    EXPECT_EQ(flatten(b.scene).mShapes.size(), 1u);
}

TEST(NifSceneFlatten, a_parent_cycle_does_not_hang_and_the_shapes_are_still_drawn)
{
    Builder b;
    const auto a = b.node("a");
    const auto c = b.node("c", a);
    b.scene.mNodes[a].mParent = c; // a <-> c
    b.quad(c, b.material("a.dds"));
    b.scene.mRoots.clear();
    const FlatModel model = flatten(b.scene);
    EXPECT_EQ(model.mShapes.size(), 1u);
}

TEST(NifSceneFlatten, a_node_that_is_its_own_parent_is_a_root)
{
    Builder b;
    const auto a = b.node("a");
    b.scene.mNodes[a].mParent = a;
    b.quad(a, b.material("a.dds"));
    EXPECT_EQ(flatten(b.scene).mShapes.size(), 1u);
}

TEST(NifSceneFlatten, a_node_with_a_geometry_index_outside_the_scene_draws_nothing)
{
    Builder b;
    const auto root = b.node("root");
    b.scene.mNodes[root].mGeometry = 50;
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(model.mShapes.empty());
}

TEST(NifSceneFlatten, an_empty_scene_gives_an_empty_model)
{
    const FlatModel model = flatten(Scene{});
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mGeometries, 0u);
    EXPECT_EQ(model.mBoundsMin, (std::array<float, 3>{ 0, 0, 0 }));
    EXPECT_EQ(model.mBoundsMax, (std::array<float, 3>{ 0, 0, 0 }));
}

TEST(NifSceneFlatten, the_bounds_cover_every_drawn_vertex)
{
    Builder b;
    const auto root = b.node("root", NifScene::sNone, { 10, -4, 2 }, 2.f);
    b.quad(root, b.material("a.dds"));
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(near(model.mBoundsMin[0], 10.f));
    EXPECT_TRUE(near(model.mBoundsMin[1], -4.f));
    EXPECT_TRUE(near(model.mBoundsMax[0], 12.f));
    EXPECT_TRUE(near(model.mBoundsMax[1], -2.f));
    EXPECT_TRUE(near(model.mBoundsMax[2], 2.f));
}

TEST(NifSceneFlatten, the_shape_cap_stops_drawing_and_counts_the_rest)
{
    Builder b;
    const auto root = b.node("root");
    for (int i = 0; i < 6; ++i)
        b.quad(root, b.material("t" + std::to_string(i) + ".dds"));
    Limits limits;
    limits.mMaxShapes = 4;
    const FlatModel model = flatten(b.scene, limits);
    EXPECT_EQ(model.mShapes.size(), 4u);
    EXPECT_EQ(model.mReport.mSkippedOverCap, 2u);
}

TEST(NifSceneFlatten, the_vertex_cap_stops_drawing_and_counts_the_rest)
{
    Builder b;
    const auto root = b.node("root");
    for (int i = 0; i < 5; ++i)
        b.quad(root, b.material("t" + std::to_string(i) + ".dds"));
    Limits limits;
    limits.mMaxVertices = 10; // two quads
    const FlatModel model = flatten(b.scene, limits);
    EXPECT_EQ(model.mShapes.size(), 2u);
    EXPECT_EQ(model.mReport.mSkippedOverCap, 3u);
    EXPECT_LE(model.mReport.mVertices, 10u);
}

TEST(NifSceneFlatten, the_triangle_cap_stops_drawing_and_counts_the_rest)
{
    Builder b;
    const auto root = b.node("root");
    for (int i = 0; i < 5; ++i)
        b.quad(root, b.material("t" + std::to_string(i) + ".dds"));
    Limits limits;
    limits.mMaxTriangles = 5; // two quads have 4 triangles, a third would make 6
    const FlatModel model = flatten(b.scene, limits);
    EXPECT_EQ(model.mShapes.size(), 2u);
    EXPECT_EQ(model.mReport.mSkippedOverCap, 3u);
}

TEST(NifSceneFlatten, the_node_cap_is_reported)
{
    Builder b;
    const auto root = b.node("root");
    for (int i = 0; i < 20; ++i)
        b.node("n", root);
    b.quad(root, b.material("a.dds"));
    Limits limits;
    limits.mMaxNodes = 10;
    const FlatModel model = flatten(b.scene, limits);
    EXPECT_TRUE(model.mReport.mNodesOverCap);
}

TEST(NifSceneFlatten, the_same_scene_gives_the_same_model)
{
    Builder b;
    const auto root = b.node("root", NifScene::sNone, { 1, 2, 3 }, 1.5f);
    for (int i = 0; i < 8; ++i)
        b.quad(root, b.material(i % 2 ? "a.dds" : "b.dds"));
    const FlatModel first = flatten(b.scene);
    const FlatModel second = flatten(b.scene);
    ASSERT_EQ(first.mShapes.size(), second.mShapes.size());
    for (std::size_t i = 0; i < first.mShapes.size(); ++i)
    {
        EXPECT_EQ(first.mShapes[i].mPositions, second.mShapes[i].mPositions);
        EXPECT_EQ(first.mShapes[i].mIndices, second.mShapes[i].mIndices);
        EXPECT_EQ(first.mShapes[i].mKey, second.mShapes[i].mKey);
    }
}

TEST(NifSceneFlatten, a_large_hostile_scene_stays_within_the_caps)
{
    Builder b;
    const auto root = b.node("root");
    // many tiny shapes with different textures: no merging, the shape cap applies
    for (int i = 0; i < 300; ++i)
        b.quad(root, b.material("x" + std::to_string(i) + ".dds"));
    Limits limits;
    limits.mMaxShapes = 100;
    const FlatModel model = flatten(b.scene, limits);
    EXPECT_LE(model.mShapes.size(), 100u);
    EXPECT_EQ(model.mReport.mSkippedOverCap, 200u);
}

TEST(NifSceneHeader, only_version_20_2_0_7_is_a_skyrim_se_header)
{
    using NifScene::isSkyrimSeHeader;
    EXPECT_TRUE(isSkyrimSeHeader("Gamebryo File Format, Version 20.2.0.7\nrest"));
    EXPECT_TRUE(isSkyrimSeHeader("Gamebryo File Format, Version 20.2.0.7"));
    EXPECT_FALSE(isSkyrimSeHeader("NetImmerse File Format, Version 4.0.0.2\n"));
    EXPECT_FALSE(isSkyrimSeHeader("Gamebryo File Format, Version 20.0.0.5\n"));
    EXPECT_FALSE(isSkyrimSeHeader("Gamebryo File Format, Version 20.2.0.70\n"));
    EXPECT_FALSE(isSkyrimSeHeader("Gamebryo File Format, Version 20.2.0\n"));
    EXPECT_FALSE(isSkyrimSeHeader(""));
    EXPECT_FALSE(isSkyrimSeHeader(std::string("Gamebryo File Format, Version 20.2.0.7\x01\n", 40)));
    EXPECT_FALSE(isSkyrimSeHeader(std::string("\x89PNG\r\n\x1a\n", 8)));
}
