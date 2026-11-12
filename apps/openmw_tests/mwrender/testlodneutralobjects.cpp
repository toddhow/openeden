// Task 0133 stage 2: the decisions the neutral branch of the far object cluster bake adds. Written from the spec alone
// (which objects, which matrix, which pieces), before the code; no game data.
#include <apps/openmw/lodneutralobjects.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace OMW::NeutralLod;
    using openeden::sdk::game::ObjectKind;
    using openeden::sdk::game::WorldTransform;
    using NifScene::Flat::FlatModel;
    using NifScene::Flat::Shape;

    TEST(NeutralLodKinds, only_statics_and_trees_may_stand_in_a_far_cluster)
    {
        EXPECT_TRUE(isFarStaticKind(ObjectKind::Static));
        EXPECT_TRUE(isFarStaticKind(ObjectKind::Tree));
        for (const ObjectKind kind : { ObjectKind::Unknown, ObjectKind::MovableStatic, ObjectKind::Furniture,
                 ObjectKind::Door, ObjectKind::Activator, ObjectKind::Container, ObjectKind::Flora, ObjectKind::Light,
                 ObjectKind::Item, ObjectKind::Actor, ObjectKind::LeveledList, ObjectKind::Other })
            EXPECT_FALSE(isFarStaticKind(kind)) << static_cast<int>(kind);
    }

    TEST(NeutralLodKinds, a_marker_model_is_recognised_by_its_prefix_in_any_case)
    {
        EXPECT_TRUE(isMarkerModel("Marker_Light.nif"));
        EXPECT_TRUE(isMarkerModel("MARKERX.nif"));
        EXPECT_TRUE(isMarkerModel("marker"));
        EXPECT_FALSE(isMarkerModel("Architecture\\marker.nif"));
        EXPECT_FALSE(isMarkerModel(""));
        EXPECT_FALSE(isMarkerModel("mark"));
    }

    WorldTransform transformOf(double x, double y, double z, double qx, double qy, double qz, double qw, double scale)
    {
        WorldTransform t;
        t.position = { x, y, z };
        t.rotation = { qx, qy, qz, qw };
        t.scale = scale;
        return t;
    }

    TEST(NeutralLodMatrix, the_identity_with_a_position)
    {
        std::array<double, 12> m{};
        ASSERT_TRUE(placementMatrix(transformOf(10, -20, 30, 0, 0, 0, 1, 1), m));
        const std::array<double, 12> expected = { 1, 0, 0, 10, 0, 1, 0, -20, 0, 0, 1, 30 };
        for (int i = 0; i < 12; ++i)
            EXPECT_NEAR(m[i], expected[i], 1e-12) << i;
    }

    TEST(NeutralLodMatrix, a_quarter_turn_about_z_takes_x_to_y_and_the_scale_multiplies_the_columns)
    {
        const double s = std::sqrt(0.5);
        std::array<double, 12> m{};
        ASSERT_TRUE(placementMatrix(transformOf(1, 2, 3, 0, 0, s, s, 2), m));
        // R = [[0 -1 0][1 0 0][0 0 1]], times 2
        const std::array<double, 12> expected = { 0, -2, 0, 1, 2, 0, 0, 2, 0, 0, 2, 3 };
        for (int i = 0; i < 12; ++i)
            EXPECT_NEAR(m[i], expected[i], 1e-12) << i;
    }

    TEST(NeutralLodMatrix, a_half_turn_about_x_flips_y_and_z)
    {
        std::array<double, 12> m{};
        ASSERT_TRUE(placementMatrix(transformOf(0, 0, 0, 1, 0, 0, 0, 1), m));
        EXPECT_NEAR(m[0], 1, 1e-12);
        EXPECT_NEAR(m[5], -1, 1e-12);
        EXPECT_NEAR(m[10], -1, 1e-12);
    }

    TEST(NeutralLodMatrix, a_general_rotation_is_orthonormal_times_the_scale)
    {
        // 60 degrees about the axis (1, 1, 0) / sqrt 2
        const double a = 3.14159265358979323846 / 3, k = std::sin(a / 2) / std::sqrt(2.0);
        std::array<double, 12> m{};
        ASSERT_TRUE(placementMatrix(transformOf(0, 0, 0, k, k, 0, std::cos(a / 2), 1.5), m));
        for (int col = 0; col < 3; ++col)
        {
            double length = 0;
            for (int row = 0; row < 3; ++row)
                length += m[row * 4 + col] * m[row * 4 + col];
            EXPECT_NEAR(std::sqrt(length), 1.5, 1e-9);
        }
        // Rodrigues: R[2][0] for axis n=(1,1,0)/sqrt2, angle 60: -n_y * sin(a) ... = 1/2 * (1 - cos a) * 0 + (-n_y) sin a
        // R = I cos a + sin a [n]x + (1 - cos a) n n^T; entry (2,0) = sin a * (-n_y) = -sin(60)/sqrt2
        EXPECT_NEAR(m[8] / 1.5, -std::sin(a) / std::sqrt(2.0), 1e-9);
    }

    TEST(NeutralLodMatrix, a_transform_the_bake_cannot_trust_is_refused_and_leaves_the_output_alone)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        const std::array<double, 12> sentinel = { 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7 };
        const WorldTransform bad[] = {
            transformOf(nan, 0, 0, 0, 0, 0, 1, 1),
            transformOf(0, inf, 0, 0, 0, 0, 1, 1),
            transformOf(1e8, 0, 0, 0, 0, 0, 1, 1),
            transformOf(0, 0, 0, 0, 0, 0, 1, 0),
            transformOf(0, 0, 0, 0, 0, 0, 1, -1),
            transformOf(0, 0, 0, 0, 0, 0, 1, 1000),
            transformOf(0, 0, 0, 0, 0, 0, 1, nan),
            transformOf(0, 0, 0, 0, 0, 0, 0, 1), // zero length
            transformOf(0, 0, 0, 0, 0, 0, 2, 1), // not a unit quaternion
            transformOf(0, 0, 0, nan, 0, 0, 1, 1),
        };
        for (const WorldTransform& t : bad)
        {
            std::array<double, 12> m = sentinel;
            EXPECT_FALSE(placementMatrix(t, m));
            EXPECT_EQ(m, sentinel);
        }
        std::array<double, 12> m{};
        EXPECT_TRUE(placementMatrix(transformOf(9.9e7, 0, 0, 0, 0, 0, 1, 999.f), m));
    }

    TEST(NeutralLodPaths, a_mesh_path_becomes_the_vfs_path)
    {
        EXPECT_EQ(meshVfsPath("Architecture\\Whiterun\\WRBuilding01.nif"), "meshes/architecture/whiterun/wrbuilding01.nif");
        EXPECT_EQ(meshVfsPath("a.nif"), "meshes/a.nif");
        EXPECT_EQ(meshVfsPath(""), "");
    }

    TEST(NeutralLodPaths, a_texture_path_becomes_the_vfs_path)
    {
        EXPECT_EQ(textureVfsPath("textures\\Architecture\\X_d.dds"), "textures/architecture/x_d.dds");
        EXPECT_EQ(textureVfsPath(""), "");
    }

    // One triangle shape with a diffuse texture and the given alpha state.
    Shape triangle(const std::string& texture, float x0 = 0.f)
    {
        Shape shape;
        shape.mKey.mDiffuse = texture;
        shape.mPositions = { { x0, 0, 0 }, { x0 + 10, 0, 0 }, { x0, 10, 0 } };
        shape.mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
        shape.mUV = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
        shape.mIndices = { 0, 1, 2 };
        return shape;
    }

    FlatModel modelOf(std::vector<Shape> shapes)
    {
        FlatModel model;
        model.mShapes = std::move(shapes);
        return model;
    }

    TEST(NeutralLodFlatten, an_opaque_shape_is_one_piece_with_its_vertices_and_texture)
    {
        Shape shape = triangle("textures\\wall_d.dds");
        shape.mUV = { { 0.25f, 0.5f }, { 1, 0 }, { 0, 1 } };
        const Lod::FlattenedModel out = toFlattenedModel(modelOf({ shape }), true);
        ASSERT_EQ(out.mPieces.size(), 1u);
        const Lod::SourcePiece& piece = out.mPieces[0];
        EXPECT_EQ(piece.mTexture, "textures\\wall_d.dds");
        EXPECT_EQ(piece.mFlags, 0u);
        EXPECT_EQ(piece.mAlphaCutoff, 0.f);
        ASSERT_EQ(piece.mVertices.size(), 3u);
        EXPECT_FLOAT_EQ(piece.mVertices[1].mPosition[0], 10.f);
        EXPECT_FLOAT_EQ(piece.mVertices[0].mNormal[2], 1.f);
        EXPECT_FLOAT_EQ(piece.mVertices[0].mUv[0], 0.25f);
        EXPECT_FLOAT_EQ(piece.mVertices[0].mUv[1], 0.5f);
        EXPECT_EQ(piece.mIndices, (std::vector<std::uint32_t>{ 0, 1, 2 }));
        EXPECT_EQ(out.mTraits.mTriangleCount, 1u);
        EXPECT_EQ(out.mTraits.mKind, Lod::ObjectKind::Static);
        EXPECT_FALSE(out.mTraits.mAlphaBlended);
        EXPECT_FALSE(out.mTraits.mAnimated || out.mTraits.mSkinned || out.mTraits.mHasParticles);
    }

    TEST(NeutralLodFlatten, vertex_colours_are_bytes_and_white_when_the_shape_has_none)
    {
        Shape plain = triangle("a.dds");
        Shape coloured = triangle("b.dds");
        coloured.mColors = { { 1.f, 0.f, 0.5f, 1.f }, { 0, 0, 0, 0 }, { 2.f, -1.f, 0.f, 1.f } };
        const Lod::FlattenedModel out = toFlattenedModel(modelOf({ plain, coloured }), true);
        ASSERT_EQ(out.mPieces.size(), 2u);
        for (const Lod::SourceVertex& v : out.mPieces[0].mVertices)
            for (int k = 0; k < 4; ++k)
                EXPECT_EQ(v.mColor[k], 255);
        const auto& c = out.mPieces[1].mVertices;
        EXPECT_EQ(c[0].mColor[0], 255);
        EXPECT_EQ(c[0].mColor[1], 0);
        EXPECT_EQ(c[0].mColor[2], 128); // 0.5 * 255 rounded
        EXPECT_EQ(c[1].mColor[3], 0);
        EXPECT_EQ(c[2].mColor[0], 255); // clamped
        EXPECT_EQ(c[2].mColor[1], 0);
    }

    TEST(NeutralLodFlatten, two_sided_and_alpha_test_set_the_flags_and_the_cutoff)
    {
        Shape shape = triangle("leaf_d.dds");
        shape.mKey.mDoubleSided = true;
        shape.mKey.mTest = true;
        shape.mKey.mThreshold = 51;
        const Lod::FlattenedModel out = toFlattenedModel(modelOf({ shape }), true);
        ASSERT_EQ(out.mPieces.size(), 1u);
        EXPECT_EQ(out.mPieces[0].mFlags, Lod::clusterFlagAlphaTest | Lod::clusterFlagTwoSided);
        EXPECT_FLOAT_EQ(out.mPieces[0].mAlphaCutoff, 51.f / 255.f);
        EXPECT_FALSE(out.mTraits.mAlphaBlended);
    }

    TEST(NeutralLodFlatten, a_test_wins_over_a_blend_on_the_same_shape)
    {
        Shape shape = triangle("leaf_d.dds");
        shape.mKey.mBlend = true;
        shape.mKey.mTest = true;
        shape.mKey.mThreshold = 100;
        shape.mKey.mAlpha = 0.5f; // does not matter when a test is there
        const Lod::FlattenedModel out = toFlattenedModel(modelOf({ shape }), true);
        ASSERT_EQ(out.mPieces.size(), 1u);
        EXPECT_EQ(out.mPieces[0].mFlags & Lod::clusterFlagAlphaTest, Lod::clusterFlagAlphaTest);
        EXPECT_FLOAT_EQ(out.mPieces[0].mAlphaCutoff, 100.f / 255.f);
        EXPECT_FALSE(out.mTraits.mAlphaBlended);
    }

    TEST(NeutralLodFlatten, an_ordinary_blend_becomes_a_cutout_at_the_foliage_threshold)
    {
        Shape shape = triangle("textures\\branch_d.dds");
        shape.mKey.mBlend = true; // codes 6 and 7 are the defaults
        const Lod::FlattenedModel out = toFlattenedModel(modelOf({ shape }), true);
        ASSERT_EQ(out.mPieces.size(), 1u);
        EXPECT_EQ(out.mPieces[0].mFlags & Lod::clusterFlagAlphaTest, Lod::clusterFlagAlphaTest);
        EXPECT_FLOAT_EQ(out.mPieces[0].mAlphaCutoff, static_cast<float>(Lod::blendCutoutThreshold) / 255.f);
        EXPECT_FALSE(out.mTraits.mAlphaBlended);
    }

    TEST(NeutralLodFlatten, every_blend_that_cannot_be_a_cutout_leaves_the_shape_out_and_marks_the_model)
    {
        const auto blended = [](const std::string& texture) {
            Shape s = triangle(texture);
            s.mKey.mBlend = true;
            return s;
        };
        Shape additive = blended("fx_d.dds");
        additive.mKey.mSourceBlend = 6;
        additive.mKey.mDestinationBlend = 0; // not the ordinary blend
        Shape faint = blended("branch_d.dds");
        faint.mKey.mAlpha = 0.5f;
        Shape untextured = blended("");
        const Shape glass = blended("textures\\glass_window_d.dds"); // looks translucent
        for (const Shape& s : { additive, faint, untextured, glass })
        {
            const Lod::FlattenedModel out = toFlattenedModel(modelOf({ s }), true);
            EXPECT_TRUE(out.mPieces.empty()) << s.mKey.mDiffuse;
            EXPECT_TRUE(out.mTraits.mAlphaBlended) << s.mKey.mDiffuse;
        }
        // With the option off even the ordinary blend is left out.
        const Lod::FlattenedModel off = toFlattenedModel(modelOf({ blended("branch_d.dds") }), false);
        EXPECT_TRUE(off.mPieces.empty());
        EXPECT_TRUE(off.mTraits.mAlphaBlended);
    }

    TEST(NeutralLodFlatten, a_blended_shape_beside_an_opaque_one_keeps_the_opaque_piece_and_marks_the_model)
    {
        Shape glass = triangle("textures\\glass_d.dds");
        glass.mKey.mBlend = true;
        const Lod::FlattenedModel out = toFlattenedModel(modelOf({ triangle("wall_d.dds"), glass }), true);
        EXPECT_EQ(out.mPieces.size(), 1u);
        EXPECT_TRUE(out.mTraits.mAlphaBlended);
    }

    TEST(NeutralLodFlatten, the_radius_holds_every_vertex_round_the_centre_of_the_box)
    {
        // x from 0 to 10 and y from 0 to 10 in one triangle; a second far shape stretches the box to x in [0, 110]
        Shape far = triangle("b.dds", 100.f); // x in [100, 110]
        const Lod::FlattenedModel out = toFlattenedModel(modelOf({ triangle("a.dds"), far }), true);
        // box: x [0,110], y [0,10], z [0,0]; centre (55, 5, 0); farthest vertex (0, 0, 0) or (110, 0, 0): sqrt(55^2+5^2)
        // and (0, 10) or (110, 10): the same.
        EXPECT_NEAR(out.mTraits.mRadius, std::sqrt(55.f * 55.f + 5.f * 5.f), 1e-3);
        EXPECT_EQ(out.mTraits.mTriangleCount, 2u);
    }

    TEST(NeutralLodFlatten, a_shape_with_no_triangles_or_a_bad_index_adds_no_piece)
    {
        Shape empty = triangle("a.dds");
        empty.mIndices.clear();
        Shape bad = triangle("b.dds");
        bad.mIndices = { 0, 1, 7 };
        Shape partial = triangle("c.dds");
        partial.mIndices = { 0, 1, 2, 0 }; // not a multiple of three
        const Lod::FlattenedModel out = toFlattenedModel(modelOf({ empty, bad, partial }), true);
        EXPECT_TRUE(out.mPieces.empty());
        EXPECT_EQ(out.mTraits.mTriangleCount, 0u);
        EXPECT_EQ(out.mTraits.mRadius, 0.f);
    }

    TEST(NeutralLodFlatten, an_empty_model_is_an_empty_static)
    {
        const Lod::FlattenedModel out = toFlattenedModel(FlatModel{}, true);
        EXPECT_TRUE(out.mPieces.empty());
        EXPECT_EQ(out.mTraits.mKind, Lod::ObjectKind::Static);
        EXPECT_FALSE(out.mTraits.mAlphaBlended);
    }

    TEST(NeutralLodFlatten, missing_normals_or_uvs_fall_back_to_the_pieces_defaults)
    {
        Shape shape = triangle("a.dds");
        shape.mNormals.clear();
        shape.mUV.clear();
        const Lod::FlattenedModel out = toFlattenedModel(modelOf({ shape }), true);
        ASSERT_EQ(out.mPieces.size(), 1u);
        EXPECT_FLOAT_EQ(out.mPieces[0].mVertices[0].mNormal[2], 1.f);
        EXPECT_FLOAT_EQ(out.mPieces[0].mVertices[0].mUv[0], 0.f);
    }
}
