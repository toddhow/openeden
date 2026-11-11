// Task 0110 (neutral world stage N3a-4b): the pure half of collision from a Skyrim SE model's visual triangles
// (components/nifscene/collision.*). FlatModels are built by hand; no game asset is read.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

#include <components/nifscene/collision.hpp>

namespace
{
    using namespace NifScene;
    using Flat::FlatModel;
    using Flat::Shape;

    constexpr float notANumber = std::numeric_limits<float>::quiet_NaN();

    // A shape of one quad in the XY plane at height z (two triangles).
    Shape quadShape(float z = 0.f, float size = 100.f)
    {
        Shape s;
        s.mPositions = { { 0, 0, z }, { size, 0, z }, { size, size, z }, { 0, size, z } };
        s.mNormals.assign(4, { 0, 0, 1 });
        s.mIndices = { 0, 1, 2, 0, 2, 3 };
        return s;
    }

    FlatModel model(std::vector<Shape> shapes)
    {
        FlatModel m;
        m.mShapes = std::move(shapes);
        return m;
    }
}

TEST(NifSceneCollision, a_quad_gives_two_triangles_and_four_vertices)
{
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ quadShape() }), {}, &report);
    EXPECT_EQ(mesh.mVertices.size(), 4u);
    ASSERT_EQ(mesh.mTriangles.size(), 2u);
    EXPECT_EQ(report.mShapesUsed, 1u);
    EXPECT_EQ(report.mTrianglesKept, 2u);
    EXPECT_FALSE(report.mOverCap);
    EXPECT_FALSE(mesh.empty());
}

TEST(NifSceneCollision, an_empty_model_gives_an_empty_mesh)
{
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(FlatModel{}, {}, &report);
    EXPECT_TRUE(mesh.empty());
    EXPECT_TRUE(mesh.mVertices.empty());
    EXPECT_EQ(report.mShapesUsed, 0u);
}

TEST(NifSceneCollision, the_vertices_keep_their_positions_through_the_triangles)
{
    const Shape s = quadShape(7.f, 10.f);
    const Collision::Mesh mesh = Collision::buildMesh(model({ s }));
    ASSERT_EQ(mesh.mTriangles.size(), 2u);
    for (std::size_t t = 0; t < 2; ++t)
        for (std::size_t k = 0; k < 3; ++k)
            EXPECT_EQ(mesh.mVertices[mesh.mTriangles[t][k]], s.mPositions[s.mIndices[t * 3 + k]]);
}

TEST(NifSceneCollision, a_blended_shape_without_alpha_test_has_no_collision)
{
    Shape glass = quadShape();
    glass.mKey.mBlend = true;
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ glass }), {}, &report);
    EXPECT_TRUE(mesh.empty());
    EXPECT_EQ(report.mShapesBlended, 1u);
    EXPECT_EQ(report.mShapesUsed, 0u);
    EXPECT_EQ(report.mTrianglesIn, 0u) << "a left-out shape is not looked at";
}

TEST(NifSceneCollision, a_cutout_collides_even_when_it_also_blends)
{
    Shape leaf = quadShape();
    leaf.mKey.mBlend = true;
    leaf.mKey.mTest = true;
    Shape plainCutout = quadShape(5.f);
    plainCutout.mKey.mTest = true;
    const Collision::Mesh mesh = Collision::buildMesh(model({ leaf, plainCutout }));
    EXPECT_EQ(mesh.mTriangles.size(), 4u);
}

TEST(NifSceneCollision, a_triangle_with_an_index_outside_the_shape_is_dropped_and_counted)
{
    Shape s = quadShape();
    s.mIndices = { 0, 1, 2, 0, 2, 4, 0, 2, 0xFFFFFFFFu };
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ s }), {}, &report);
    EXPECT_EQ(mesh.mTriangles.size(), 1u);
    EXPECT_EQ(report.mTrianglesBadIndex, 2u);
    EXPECT_EQ(report.mTrianglesIn, 3u);
}

TEST(NifSceneCollision, a_triangle_with_a_repeated_corner_is_dropped_and_counted)
{
    Shape s = quadShape();
    s.mIndices = { 0, 1, 2, 1, 1, 3, 2, 3, 2 };
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ s }), {}, &report);
    EXPECT_EQ(mesh.mTriangles.size(), 1u);
    EXPECT_EQ(report.mTrianglesRepeatedCorner, 2u);
}

TEST(NifSceneCollision, a_corner_that_is_not_a_number_drops_the_triangles_that_use_it)
{
    Shape s = quadShape();
    s.mPositions[2] = { notANumber, 0, 0 };
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ s }), {}, &report);
    EXPECT_TRUE(mesh.empty()) << "both triangles of the quad use corner 2";
    EXPECT_EQ(report.mTrianglesBadVertex, 2u);
    s.mPositions[2] = { std::numeric_limits<float>::infinity(), 0, 0 };
    EXPECT_TRUE(Collision::buildMesh(model({ s })).empty());
}

TEST(NifSceneCollision, a_sliver_with_no_area_is_dropped_and_counted)
{
    Shape s;
    s.mPositions = { { 0, 0, 0 }, { 50, 0, 0 }, { 100, 0, 0 }, { 0, 100, 0 } };
    s.mIndices = { 0, 1, 2, 0, 2, 3 }; // the first three are on one line
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ s }), {}, &report);
    EXPECT_EQ(mesh.mTriangles.size(), 1u);
    EXPECT_EQ(report.mTrianglesTooThin, 1u);
}

TEST(NifSceneCollision, the_smallest_face_kept_is_set_by_the_limit)
{
    Shape s;
    s.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } }; // twice the area is 1
    s.mIndices = { 0, 1, 2 };
    Collision::Limits limits;
    limits.mMinTwiceArea = 0.9;
    EXPECT_EQ(Collision::buildMesh(model({ s }), limits).mTriangles.size(), 1u);
    limits.mMinTwiceArea = 1.1;
    EXPECT_TRUE(Collision::buildMesh(model({ s }), limits).empty());
}

TEST(NifSceneCollision, only_the_vertices_that_are_used_are_copied_in_order_of_first_use)
{
    Shape s;
    s.mPositions = { { 0, 0, 0 }, { 99, 99, 99 }, { 10, 0, 0 }, { 0, 10, 0 }, { 77, 77, 77 } };
    s.mIndices = { 3, 0, 2 };
    const Collision::Mesh mesh = Collision::buildMesh(model({ s }));
    ASSERT_EQ(mesh.mVertices.size(), 3u);
    EXPECT_EQ(mesh.mVertices[0], s.mPositions[3]);
    EXPECT_EQ(mesh.mVertices[1], s.mPositions[0]);
    EXPECT_EQ(mesh.mVertices[2], s.mPositions[2]);
    EXPECT_EQ(mesh.mTriangles[0], (std::array<std::uint32_t, 3>{ 0, 1, 2 }));
}

TEST(NifSceneCollision, two_shapes_are_offset_into_one_vertex_list)
{
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ quadShape(0.f), quadShape(50.f) }), {}, &report);
    EXPECT_EQ(mesh.mVertices.size(), 8u);
    EXPECT_EQ(mesh.mTriangles.size(), 4u);
    EXPECT_EQ(report.mShapesUsed, 2u);
    for (const auto& tri : mesh.mTriangles)
        for (std::uint32_t corner : tri)
            EXPECT_LT(corner, mesh.mVertices.size());
    // The second quad's triangles use only its own vertices (z = 50).
    for (std::size_t t = 2; t < 4; ++t)
        for (std::uint32_t corner : mesh.mTriangles[t])
            EXPECT_EQ(mesh.mVertices[corner][2], 50.f);
}

TEST(NifSceneCollision, over_the_triangle_cap_there_is_no_mesh_at_all)
{
    Collision::Limits limits;
    limits.mMaxTriangles = 3;
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ quadShape(0.f), quadShape(50.f) }), limits, &report);
    EXPECT_TRUE(mesh.empty()) << "a model cut at a count would leave holes";
    EXPECT_TRUE(mesh.mVertices.empty());
    EXPECT_TRUE(report.mOverCap);
    EXPECT_EQ(report.mTrianglesKept, 0u);
}

TEST(NifSceneCollision, exactly_the_cap_is_allowed)
{
    Collision::Limits limits;
    limits.mMaxTriangles = 4;
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ quadShape(0.f), quadShape(50.f) }), limits, &report);
    EXPECT_EQ(mesh.mTriangles.size(), 4u);
    EXPECT_FALSE(report.mOverCap);
}

TEST(NifSceneCollision, a_huge_hostile_shape_is_survived)
{
    Shape s;
    s.mPositions = { { 0, 0, 0 }, { 100, 0, 0 }, { 0, 100, 0 } };
    for (int i = 0; i < 200000; ++i)
        s.mIndices.insert(s.mIndices.end(), { 0, 1, 2 });
    Collision::Limits limits;
    limits.mMaxTriangles = 1000;
    Collision::Report report;
    const Collision::Mesh mesh = Collision::buildMesh(model({ s }), limits, &report);
    EXPECT_TRUE(mesh.empty());
    EXPECT_TRUE(report.mOverCap);
}

TEST(NifSceneCollision, a_trailing_partial_triangle_is_ignored)
{
    Shape s = quadShape();
    s.mIndices = { 0, 1, 2, 0, 2 };
    EXPECT_EQ(Collision::buildMesh(model({ s })).mTriangles.size(), 1u);
}

TEST(NifSceneCollision, the_same_model_gives_the_same_mesh)
{
    const FlatModel m = model({ quadShape(0.f), quadShape(50.f, 30.f) });
    const Collision::Mesh a = Collision::buildMesh(m);
    const Collision::Mesh b = Collision::buildMesh(m);
    EXPECT_EQ(a.mVertices, b.mVertices);
    EXPECT_EQ(a.mTriangles, b.mTriangles);
}

TEST(NifSceneCollision, a_flattened_scene_collides_where_it_draws)
{
    // The whole chain from a Scene: a node with a translation carrying a quad. The mesh must hold the baked positions.
    Scene scene;
    NifScene::Node n;
    n.mName = "floor";
    n.mTransform.mTranslation = { 10, 20, 30 };
    scene.mNodes.push_back(n);
    scene.mRoots.push_back(0);
    NifScene::ShaderMaterial m;
    m.mKind = NifScene::ShaderKind::Lighting;
    scene.mMaterials.push_back(m);
    NifScene::Geometry g;
    g.mName = "floor";
    g.mNode = 0;
    g.mPositions = { { 0, 0, 0 }, { 100, 0, 0 }, { 100, 100, 0 }, { 0, 100, 0 } };
    g.mNormals.assign(4, { 0, 0, 1 });
    g.mTriangles = { 0, 1, 2, 0, 2, 3 };
    g.mMaterial = 0;
    scene.mGeometries.push_back(g);
    scene.mNodes[0].mGeometry = 0;
    const Collision::Mesh mesh = Collision::buildMesh(Flat::flatten(scene));
    ASSERT_EQ(mesh.mVertices.size(), 4u);
    const std::set<std::array<float, 3>> got(mesh.mVertices.begin(), mesh.mVertices.end());
    EXPECT_TRUE(got.count({ 10, 20, 30 }));
    EXPECT_TRUE(got.count({ 110, 120, 30 }));
}
