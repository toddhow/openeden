#include <apps/opencs/model/mesh/scenemodel.hpp>

#include <components/nifscene/build.hpp>

#include <gtest/gtest.h>

#include <apps/components_tests/nifscene/ssebuilder.hpp>

#include <cmath>
#include <memory>
#include <sstream>
#include <string>

// Task 0086 stage e: the pure adapter from Nif::Scene to the mesh viewer's data. Every number is written by the test
// that expects it.
namespace
{
    using namespace CSMMesh;

    NifScene::Node makeNode(const std::string& name, std::uint32_t parent, std::array<float, 3> translation = {},
        float scale = 1.f, std::uint32_t geometry = NifScene::sNone)
    {
        NifScene::Node node;
        node.mName = name;
        node.mParent = parent;
        node.mTransform.mTranslation = translation;
        node.mTransform.mScale = scale;
        node.mGeometry = geometry;
        return node;
    }

    NifScene::Geometry makeQuad(const std::string& name, std::uint32_t node)
    {
        NifScene::Geometry geometry;
        geometry.mName = name;
        geometry.mNode = node;
        geometry.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
        geometry.mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
        geometry.mTriangles = { 0, 1, 2, 0, 2, 3 };
        return geometry;
    }

    // Root (10, 0, 0) scale 2, with the quad node (0, 5, 0) below it.
    NifScene::Scene quadScene()
    {
        NifScene::Scene scene;
        scene.mNodes.push_back(makeNode("Root", NifScene::sNone, { 10.f, 0.f, 0.f }, 2.f));
        scene.mNodes.push_back(makeNode("Quad", 0, { 0.f, 5.f, 0.f }, 1.f, 0));
        scene.mGeometries.push_back(makeQuad("Quad", 1));
        scene.mRoots = { 0 };
        return scene;
    }

    // ---- which reader ----

    TEST(MeshSceneModelReader, skyrim_se_header_takes_the_scene_path)
    {
        EXPECT_EQ(chooseNifReader("Gamebryo File Format, Version 20.2.0.7\nrest"), NifReader::Scene);
    }

    TEST(MeshSceneModelReader, a_header_cut_before_the_newline_still_names_its_version)
    {
        EXPECT_EQ(chooseNifReader("Gamebryo File Format, Version 20.2.0.7"), NifReader::Scene);
    }

    TEST(MeshSceneModelReader, morrowind_header_keeps_the_engine_reader)
    {
        EXPECT_EQ(chooseNifReader("NetImmerse File Format, Version 4.0.0.2\n"), NifReader::Engine);
    }

    TEST(MeshSceneModelReader, other_versions_keep_the_engine_reader)
    {
        EXPECT_EQ(chooseNifReader("Gamebryo File Format, Version 20.0.0.5\n"), NifReader::Engine);
        EXPECT_EQ(chooseNifReader("Gamebryo File Format, Version 20.2.0.70\n"), NifReader::Engine);
        EXPECT_EQ(chooseNifReader("Gamebryo File Format, Version 20.2.0\n"), NifReader::Engine);
    }

    TEST(MeshSceneModelReader, text_without_a_version_keeps_the_engine_reader)
    {
        EXPECT_EQ(chooseNifReader(""), NifReader::Engine);
        EXPECT_EQ(chooseNifReader("not a nif at all"), NifReader::Engine);
        EXPECT_EQ(chooseNifReader(std::string("\0\0\0\0Version 20.2.0.7", 20)), NifReader::Engine);
    }

    // ---- material colour ----

    TEST(MeshSceneModelColour, no_material_is_neutral_grey)
    {
        EXPECT_EQ(materialColour(nullptr), (std::array<float, 4>{ 0.72f, 0.72f, 0.72f, 1.f }));
    }

    TEST(MeshSceneModelColour, skin_tint_shader_uses_the_skin_tint)
    {
        NifScene::ShaderMaterial material;
        material.mShaderType = 5;
        material.mSkinTintColor = { 0.9f, 0.6f, 0.5f };
        EXPECT_EQ(materialColour(&material), (std::array<float, 4>{ 0.9f, 0.6f, 0.5f, 1.f }));
    }

    TEST(MeshSceneModelColour, hair_tint_shader_uses_the_hair_tint)
    {
        NifScene::ShaderMaterial material;
        material.mShaderType = 6;
        material.mHairTintColor = { 0.2f, 0.1f, 0.05f };
        EXPECT_EQ(materialColour(&material), (std::array<float, 4>{ 0.2f, 0.1f, 0.05f, 1.f }));
    }

    TEST(MeshSceneModelColour, effect_shader_uses_its_base_colour)
    {
        NifScene::ShaderMaterial material;
        material.mKind = NifScene::ShaderKind::Effect;
        material.mBaseColor = { 0.1f, 0.4f, 0.8f, 0.5f };
        EXPECT_EQ(materialColour(&material), (std::array<float, 4>{ 0.1f, 0.4f, 0.8f, 1.f }));
    }

    TEST(MeshSceneModelColour, default_type_a_zero_tint_and_a_nan_tint_are_neutral)
    {
        const std::array<float, 4> grey{ 0.72f, 0.72f, 0.72f, 1.f };
        NifScene::ShaderMaterial plain;
        plain.mSkinTintColor = { 1.f, 0.f, 0.f };
        EXPECT_EQ(materialColour(&plain), grey);
        NifScene::ShaderMaterial zero;
        zero.mShaderType = 5;
        EXPECT_EQ(materialColour(&zero), grey);
        NifScene::ShaderMaterial bad;
        bad.mShaderType = 5;
        bad.mSkinTintColor = { std::nanf(""), 1.f, 1.f };
        EXPECT_EQ(materialColour(&bad), grey);
    }

    // ---- bind pose bone position ----

    TEST(MeshSceneModelBind, identity_rotation_negates_the_translation)
    {
        NifScene::Transform bind;
        bind.mTranslation = { 1.f, 2.f, 3.f };
        EXPECT_EQ(bindBonePosition(bind), (std::array<float, 3>{ -1.f, -2.f, -3.f }));
    }

    TEST(MeshSceneModelBind, scale_divides)
    {
        NifScene::Transform bind;
        bind.mTranslation = { 1.f, 2.f, 3.f };
        bind.mScale = 2.f;
        EXPECT_EQ(bindBonePosition(bind), (std::array<float, 3>{ -0.5f, -1.f, -1.5f }));
    }

    TEST(MeshSceneModelBind, rotation_is_undone_by_its_transpose)
    {
        // v_bone = R v_skin + t with R a quarter turn about Z (row-major 0 -1 0 / 1 0 0 / 0 0 1) and t = (1, 0, 0):
        // the skin-space point (0, 1, 0) maps to (-1, 0, 0) + t = the bone's origin.
        NifScene::Transform bind;
        bind.mRotation = { 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
        bind.mTranslation = { 1.f, 0.f, 0.f };
        const auto p = bindBonePosition(bind);
        EXPECT_NEAR(p[0], 0.f, 1e-6f);
        EXPECT_NEAR(p[1], 1.f, 1e-6f);
        EXPECT_NEAR(p[2], 0.f, 1e-6f);
    }

    TEST(MeshSceneModelBind, a_zero_scale_gives_the_origin)
    {
        NifScene::Transform bind;
        bind.mTranslation = { 1.f, 2.f, 3.f };
        bind.mScale = 0.f;
        EXPECT_EQ(bindBonePosition(bind), (std::array<float, 3>{ 0.f, 0.f, 0.f }));
    }

    // ---- the model ----

    TEST(MeshSceneModel, an_empty_scene_is_an_empty_model)
    {
        const SceneModel model = buildSceneModel(NifScene::Scene());
        EXPECT_TRUE(model.nodes.empty());
        EXPECT_TRUE(model.shapes.empty());
        EXPECT_FALSE(model.bounds.valid);
    }

    TEST(MeshSceneModel, counts_nodes_shapes_vertices_and_triangles)
    {
        const SceneModel model = buildSceneModel(quadScene());
        ASSERT_EQ(model.nodes.size(), 2u);
        ASSERT_EQ(model.shapes.size(), 1u);
        EXPECT_EQ(model.nodes[0].name, "Root");
        EXPECT_EQ(model.nodes[0].type, "Transform");
        EXPECT_EQ(model.nodes[1].type, "Geometry");
        EXPECT_EQ(model.nodes[1].parent, 0);
        EXPECT_EQ(model.nodes[1].shape, 0);
        EXPECT_EQ(model.nodes[1].vertexCount, 4u);
        EXPECT_EQ(model.nodes[1].triangleCount, 2u);
        EXPECT_EQ(model.shapes[0].node, 1);
        EXPECT_EQ(model.shapes[0].mesh.indices.size(), 6u);
        EXPECT_EQ(model.shapes[0].mesh.positions.size(), 4u);
    }

    TEST(MeshSceneModel, an_unskinned_shape_is_placed_by_its_nodes_world_transform)
    {
        const SceneModel model = buildSceneModel(quadScene());
        // (1, 0, 0) + (0, 5, 0) in the quad node, then scale 2 and (10, 0, 0) in the root: (12, 10, 0).
        EXPECT_EQ(model.shapes[0].mesh.positions[1], (std::array<float, 3>{ 12.f, 10.f, 0.f }));
        EXPECT_EQ(model.shapes[0].mesh.positions[0], (std::array<float, 3>{ 10.f, 10.f, 0.f }));
        EXPECT_EQ(model.nodes[1].translation, (std::array<double, 3>{ 10.0, 10.0, 0.0 }));
        EXPECT_DOUBLE_EQ(model.nodes[1].scale[0], 2.0);
        EXPECT_FALSE(model.shapes[0].skinned);
    }

    TEST(MeshSceneModel, normals_are_rotated_and_stay_unit_length)
    {
        NifScene::Scene scene = quadScene();
        // A quarter turn about X on the root: the +Z normal becomes -Y.
        scene.mNodes[0].mTransform.mRotation = { 1.f, 0.f, 0.f, 0.f, 0.f, -1.f, 0.f, 1.f, 0.f };
        const SceneModel model = buildSceneModel(scene);
        ASSERT_EQ(model.shapes[0].mesh.normals.size(), 4u);
        const auto& n = model.shapes[0].mesh.normals[0];
        EXPECT_NEAR(n[0], 0.f, 1e-6f);
        EXPECT_NEAR(n[1], -1.f, 1e-6f);
        EXPECT_NEAR(n[2], 0.f, 1e-6f);
    }

    TEST(MeshSceneModel, a_skinned_shape_and_its_bone_points_are_placed_by_the_shape_nodes_world_transform)
    {
        // Real Skyrim bodies: the bind transforms are bone-from-shape-local, so the vertices and the bone origins are
        // both in the shape node's frame, and its world transform puts both on the skeleton (measured on the
        // installed malebody_0.nif: the shape node sits at head height and the bone origins are below it).
        NifScene::Scene scene = quadScene();
        scene.mGeometries[0].mSkin = 0;
        NifScene::Skin skin;
        skin.mBoneNames = { "A", "B" };
        skin.mBindTransforms.resize(2);
        skin.mBindTransforms[0].mTranslation = { -1.f, 0.f, 0.f };
        skin.mBindTransforms[1].mTranslation = { 0.f, 0.f, -2.f };
        scene.mSkins.push_back(skin);
        const SceneModel model = buildSceneModel(scene);
        EXPECT_TRUE(model.shapes[0].skinned);
        EXPECT_EQ(model.shapes[0].mesh.positions[1], (std::array<float, 3>{ 12.f, 10.f, 0.f }));
        ASSERT_EQ(model.bonePoints.size(), 2u);
        // Bone 0 origin (1, 0, 0) in the shape frame: (1, 5, 0) in the root, scale 2 and (10, 0, 0): (12, 10, 0).
        EXPECT_EQ(model.bonePoints[0], (std::array<float, 3>{ 12.f, 10.f, 0.f }));
        EXPECT_EQ(model.bonePoints[1], (std::array<float, 3>{ 10.f, 10.f, 4.f }));
        EXPECT_NE(model.nodes[1].flags.find("skinned"), std::string::npos);
    }

    TEST(MeshSceneModel, bounds_cover_the_placed_vertices)
    {
        const SceneModel model = buildSceneModel(quadScene());
        ASSERT_TRUE(model.bounds.valid);
        EXPECT_DOUBLE_EQ(model.bounds.min[0], 10.0);
        EXPECT_DOUBLE_EQ(model.bounds.max[0], 12.0);
        EXPECT_DOUBLE_EQ(model.bounds.min[1], 10.0);
        EXPECT_DOUBLE_EQ(model.bounds.max[1], 12.0);
    }

    TEST(MeshSceneModel, several_shapes_each_get_their_own_node_and_shape)
    {
        NifScene::Scene scene = quadScene();
        scene.mNodes.push_back(makeNode("Second", 0, { 0.f, 0.f, 3.f }, 1.f, 1));
        scene.mGeometries.push_back(makeQuad("Second", 2));
        const SceneModel model = buildSceneModel(scene);
        ASSERT_EQ(model.shapes.size(), 2u);
        EXPECT_EQ(model.nodes[model.shapes[1].node].name, "Second");
        EXPECT_EQ(model.nodes[model.shapes[1].node].shape, 1);
        // (0, 0, 0) + (0, 0, 3), scale 2, + (10, 0, 0).
        EXPECT_EQ(model.shapes[1].mesh.positions[0], (std::array<float, 3>{ 10.f, 0.f, 6.f }));
    }

    TEST(MeshSceneModel, parents_come_before_children_whatever_the_record_order)
    {
        NifScene::Scene scene;
        scene.mNodes.push_back(makeNode("Child", 1, { 0.f, 0.f, 1.f }));
        scene.mNodes.push_back(makeNode("Parent", NifScene::sNone, { 0.f, 0.f, 4.f }));
        const SceneModel model = buildSceneModel(scene);
        ASSERT_EQ(model.nodes.size(), 2u);
        EXPECT_EQ(model.nodes[0].name, "Parent");
        EXPECT_EQ(model.nodes[1].name, "Child");
        EXPECT_EQ(model.nodes[1].parent, 0);
        EXPECT_EQ(model.nodes[1].translation, (std::array<double, 3>{ 0.0, 0.0, 5.0 }));
    }

    TEST(MeshSceneModel, a_parent_cycle_does_not_hang_and_is_reported)
    {
        NifScene::Scene scene;
        scene.mNodes.push_back(makeNode("A", 1));
        scene.mNodes.push_back(makeNode("B", 0));
        const SceneModel model = buildSceneModel(scene);
        EXPECT_EQ(model.nodes.size(), 2u);
        EXPECT_FALSE(model.warnings.empty());
    }

    TEST(MeshSceneModel, a_skeleton_gives_one_line_per_parent_child_pair_of_plain_nodes)
    {
        NifScene::Scene scene;
        scene.mNodes.push_back(makeNode("Root", NifScene::sNone));
        scene.mNodes.push_back(makeNode("A", 0, { 0.f, 0.f, 1.f }));
        scene.mNodes.push_back(makeNode("B", 1, { 0.f, 0.f, 1.f }));
        const SceneModel model = buildSceneModel(scene);
        ASSERT_EQ(model.hierarchyLines.size(), 2u);
        EXPECT_EQ(model.hierarchyLines[0][0], (std::array<float, 3>{ 0.f, 0.f, 0.f }));
        EXPECT_EQ(model.hierarchyLines[0][1], (std::array<float, 3>{ 0.f, 0.f, 1.f }));
        EXPECT_EQ(model.hierarchyLines[1][0], (std::array<float, 3>{ 0.f, 0.f, 1.f }));
        EXPECT_EQ(model.hierarchyLines[1][1], (std::array<float, 3>{ 0.f, 0.f, 2.f }));
        EXPECT_TRUE(model.shapes.empty());
        ASSERT_TRUE(model.bounds.valid);
        EXPECT_DOUBLE_EQ(model.bounds.max[2], 2.0);
    }

    TEST(MeshSceneModel, shape_nodes_draw_no_hierarchy_line)
    {
        const SceneModel model = buildSceneModel(quadScene());
        EXPECT_TRUE(model.hierarchyLines.empty());
    }

    TEST(MeshSceneModel, a_shape_with_no_vertices_is_skipped_with_a_warning)
    {
        NifScene::Scene scene = quadScene();
        scene.mGeometries[0].mPositions.clear();
        scene.mGeometries[0].mNormals.clear();
        scene.mGeometries[0].mNoVertexData = true;
        const SceneModel model = buildSceneModel(scene);
        EXPECT_TRUE(model.shapes.empty());
        EXPECT_EQ(model.nodes.size(), 2u);
        ASSERT_FALSE(model.warnings.empty());
        EXPECT_NE(model.warnings.back().find("Quad"), std::string::npos);
    }

    TEST(MeshSceneModel, material_colour_and_texture_names_come_from_the_shape_property)
    {
        NifScene::Scene scene = quadScene();
        NifScene::ShaderMaterial material;
        material.mShaderType = 5;
        material.mSkinTintColor = { 0.9f, 0.6f, 0.5f };
        material.mTextureSet = 0;
        scene.mMaterials.push_back(material);
        scene.mTextureSets.push_back({ { "textures\\actors\\body_d.dds", "textures\\actors\\body_n.dds" } });
        scene.mGeometries[0].mMaterial = 0;
        scene.mNodes.push_back(makeNode("Second", 0, {}, 1.f, 1));
        scene.mGeometries.push_back(makeQuad("Second", 2));
        scene.mGeometries[1].mMaterial = 0;
        const SceneModel model = buildSceneModel(scene);
        EXPECT_EQ(model.shapes[0].colour, (std::array<float, 4>{ 0.9f, 0.6f, 0.5f, 1.f }));
        ASSERT_EQ(model.textureNames.size(), 1u);
        EXPECT_EQ(model.textureNames[0], "textures\\actors\\body_d.dds");
        EXPECT_EQ(model.nodes[1].textures, (std::vector<std::string>{ "textures\\actors\\body_d.dds" }));
    }

    TEST(MeshSceneModel, vertex_colours_are_kept_when_there_is_one_per_vertex)
    {
        NifScene::Scene scene = quadScene();
        scene.mGeometries[0].mColors = { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 1, 1 } };
        EXPECT_EQ(buildSceneModel(scene).shapes[0].mesh.colours.size(), 4u);
        scene.mGeometries[0].mColors.pop_back();
        EXPECT_TRUE(buildSceneModel(scene).shapes[0].mesh.colours.empty());
    }

    // ---- from a generated file ----

    TEST(MeshSceneModelFile, a_generated_skyrim_file_is_read_and_converted)
    {
        using namespace NifScene::Testing;
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "Root", { 1 }, { 1.f, 2.f, 3.f }, 2.f, "BSFadeNode"));
        ShapeSpec spec;
        spec.mName = "Quad";
        spec.mFlags = VF_Vertex | VF_UVs | VF_Normals;
        Vertex a;
        a.mPosition = { 0.f, 0.f, 0.f };
        Vertex b;
        b.mPosition = { 1.f, 0.f, 0.f };
        Vertex c;
        c.mPosition = { 1.f, 1.f, 0.f };
        spec.mVertices = { a, b, c };
        spec.mTriangles = { { 0, 1, 2 } };
        spec.mTranslation = { 0.f, 5.f, 0.f };
        file.mBlocks.push_back(shape(file, spec));

        auto input = std::make_unique<std::istringstream>(file.build());
        input->exceptions(std::ios::failbit | std::ios::badbit);
        const NifScene::Scene scene = NifScene::readScene(
            std::move(input), VFS::Path::NormalizedView("viewer-test.nif"), NifScene::SceneOptions{});

        EXPECT_EQ(chooseNifReader(file.build()), NifReader::Scene);
        const SceneModel model = buildSceneModel(scene);
        ASSERT_EQ(model.shapes.size(), 1u);
        ASSERT_EQ(model.shapes[0].mesh.positions.size(), 3u);
        // Quad node (0, 5, 0), then scale 2 and (1, 2, 3) of the root.
        EXPECT_EQ(model.shapes[0].mesh.positions[0], (std::array<float, 3>{ 1.f, 12.f, 3.f }));
        EXPECT_EQ(model.shapes[0].mesh.positions[1], (std::array<float, 3>{ 3.f, 12.f, 3.f }));
        EXPECT_EQ(model.nodes[model.shapes[0].node].triangleCount, 1u);
    }
}
