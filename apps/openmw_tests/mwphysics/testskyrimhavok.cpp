// Task 0144 (stage c): Jolt shapes from the bhk data of a Skyrim SE model (apps/openmw/mwphysics/skyrimhavok.*). The
// scenes are built by hand (NifScene::Scene with a Havok::Collision); no game asset is read. Rays and sweeps go
// through Jolt itself.

#include "ragdolltestworld.hpp"

#include <apps/openmw/mwphysics/skyrimhavok.hpp>
#include <apps/openmw/mwphysics/skyrimshape.hpp>

#include <components/nifscene/havok.hpp>
#include <components/resource/physicsshape.hpp>

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using namespace MWPhysicsTest;
    namespace H = NifScene::Havok;
    using MWPhysics::SkyrimHavok::Outcome;

    constexpr float k = H::kHavokToGame;

    NifScene::Scene sceneWith(H::Collision collision, NifScene::Transform nodeTransform = {})
    {
        NifScene::Scene scene;
        NifScene::Node root;
        root.mName = "Root";
        root.mRecordIndex = 0;
        root.mTransform = nodeTransform;
        scene.mNodes.push_back(root);
        scene.mRoots.push_back(0);
        scene.mHavok = std::move(collision);
        return scene;
    }

    H::Body body(std::uint32_t shape, std::uint8_t layer = 1)
    {
        H::Body b;
        b.mNodeRecord = 0;
        b.mShape = shape;
        b.mLayer = layer;
        b.mResponseType = 1;
        return b;
    }

    H::Shape box(float x, float y, float z)
    {
        H::Shape s;
        s.mKind = H::ShapeKind::Box;
        s.mTypeName = "bhkBoxShape";
        s.mHalfExtents = { x, y, z };
        return s;
    }

    struct Made
    {
        Outcome mOutcome;
        vsg::ref_ptr<Resource::PhysicsShape> mShape;
        MWPhysics::SkyrimHavok::BuildReport mReport;
        std::string mWhy;
    };

    Made make(const NifScene::Scene& scene)
    {
        ensureJoltInitialized();
        Made m;
        m.mOutcome = MWPhysics::SkyrimHavok::makeShape(scene, m.mShape, m.mReport, m.mWhy);
        return m;
    }

    // Height of the first hit of a ray from straight above, or -1e9. Jolt's shape queries are in the shape's centre of
    // mass space (a body does the conversion), so the ray is moved by the centre of mass and the answer moved back.
    double rayDown(const JPH::Shape& shape, double x, double y, double startZ)
    {
        const JPH::Vec3 com = shape.GetCenterOfMass();
        const JPH::RayCast ray(
            JPH::Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(startZ)) - com,
            JPH::Vec3(0.f, 0.f, -static_cast<float>(startZ + 10000.0)));
        JPH::RayCastResult hit;
        if (!shape.CastRay(ray, JPH::SubShapeIDCreator(), hit))
            return -1.0e9;
        return startZ - hit.mFraction * (startZ + 10000.0);
    }
}

TEST(SkyrimHavok, a_box_is_scaled_from_havok_units_and_its_top_is_where_it_should_be)
{
    H::Collision c;
    c.mShapes.push_back(box(1.f, 2.f, 0.5f));
    c.mBodies.push_back(body(0));
    const Made m = make(sceneWith(c));
    ASSERT_EQ(m.mOutcome, Outcome::Shape) << m.mWhy;
    EXPECT_NEAR(m.mShape->mCollisionBox.mExtents.x, 1.f * k, 0.01);
    EXPECT_NEAR(m.mShape->mCollisionBox.mExtents.z, 0.5f * k, 0.01);
    EXPECT_NEAR(rayDown(*m.mShape->mCollisionShape, 0.0, 0.0, 500.0), 0.5 * k, 0.05);
}

TEST(SkyrimHavok, a_rigid_body_t_places_the_shape_and_a_plain_body_does_not)
{
    H::Collision c;
    c.mShapes.push_back(box(1.f, 1.f, 1.f));
    H::Body t = body(0);
    t.mHasTransform = true;
    t.mTranslation = { 10.f, 0.f, 0.f };
    c.mBodies.push_back(t);
    const Made placed = make(sceneWith(c));
    ASSERT_EQ(placed.mOutcome, Outcome::Shape) << placed.mWhy;
    EXPECT_NEAR(rayDown(*placed.mShape->mCollisionShape, 10.0 * k, 0.0, 500.0), k, 0.05);
    EXPECT_LT(rayDown(*placed.mShape->mCollisionShape, 0.0, 0.0, 500.0), -1e8);

    c.mBodies[0].mHasTransform = false; // the same translation, now ignored (provenance item U7)
    const Made plain = make(sceneWith(c));
    ASSERT_EQ(plain.mOutcome, Outcome::Shape) << plain.mWhy;
    EXPECT_NEAR(rayDown(*plain.mShape->mCollisionShape, 0.0, 0.0, 500.0), k, 0.05);
}

TEST(SkyrimHavok, the_node_transform_moves_scales_and_rotates_the_shape)
{
    H::Collision c;
    c.mShapes.push_back(box(1.f, 1.f, 1.f));
    c.mBodies.push_back(body(0));
    NifScene::Transform node;
    node.mTranslation = { 100.f, 0.f, 50.f };
    node.mScale = 2.f;
    const Made m = make(sceneWith(c, node));
    ASSERT_EQ(m.mOutcome, Outcome::Shape) << m.mWhy;
    // half height 2k centred at z 50: the top is 50 + 2k; x spans 100 +- 2k
    EXPECT_NEAR(rayDown(*m.mShape->mCollisionShape, 100.0, 0.0, 5000.0), 50.0 + 2.0 * k, 0.1);
    EXPECT_LT(rayDown(*m.mShape->mCollisionShape, 100.0 + 2.0 * k + 5.0, 0.0, 5000.0), -1e8);
}

TEST(SkyrimHavok, bodies_on_a_non_solid_layer_are_left_out_and_a_model_of_only_those_has_no_collision)
{
    H::Collision c;
    c.mShapes.push_back(box(1.f, 1.f, 1.f));
    c.mBodies.push_back(body(0, 12)); // trigger
    c.mBodies.push_back(body(0, 15)); // non collidable
    const Made none = make(sceneWith(c));
    EXPECT_EQ(none.mOutcome, Outcome::NoCollision);
    EXPECT_EQ(none.mReport.mBodiesNotSolid, 2u);

    c.mBodies.push_back(body(0, 1));
    const Made some = make(sceneWith(c));
    ASSERT_EQ(some.mOutcome, Outcome::Shape) << some.mWhy;
    EXPECT_EQ(some.mReport.mBodies, 1u);
}

TEST(SkyrimHavok, a_body_with_no_response_is_not_solid)
{
    H::Collision c;
    c.mShapes.push_back(box(1.f, 1.f, 1.f));
    H::Body b = body(0);
    b.mResponseType = 3;
    c.mBodies.push_back(b);
    EXPECT_EQ(make(sceneWith(c)).mOutcome, Outcome::NoCollision);
}

TEST(SkyrimHavok, an_unsupported_shape_makes_the_whole_model_unusable_so_the_fallback_runs)
{
    H::Collision c;
    H::Shape unknown;
    unknown.mTypeName = "bhkNiTriStripsShape";
    c.mShapes.push_back(unknown);
    c.mBodies.push_back(body(0));
    const Made m = make(sceneWith(c));
    EXPECT_EQ(m.mOutcome, Outcome::Unusable);
    EXPECT_NE(m.mWhy.find("bhkNiTriStripsShape"), std::string::npos);
}

TEST(SkyrimHavok, a_model_without_bodies_or_over_a_limit_is_unusable)
{
    EXPECT_EQ(make(sceneWith(H::Collision{})).mOutcome, Outcome::Unusable);
    H::Collision c;
    c.mShapes.push_back(box(1, 1, 1));
    c.mBodies.push_back(body(0));
    c.mReport.mOverCap = true;
    EXPECT_EQ(make(sceneWith(c)).mOutcome, Outcome::Unusable);
}

TEST(SkyrimHavok, a_box_thinner_than_a_convex_radius_still_builds)
{
    H::Collision c;
    c.mShapes.push_back(box(1.f, 1.f, 0.00001f)); // half height 0.0007 units, under Jolt's default convex radius
    c.mBodies.push_back(body(0));
    const Made m = make(sceneWith(c));
    EXPECT_EQ(m.mOutcome, Outcome::Shape) << m.mWhy;
}

TEST(SkyrimHavok, a_box_with_a_side_of_no_length_is_refused_not_asserted)
{
    H::Collision c;
    c.mShapes.push_back(box(1.f, 0.f, 1.f));
    c.mBodies.push_back(body(0));
    EXPECT_EQ(make(sceneWith(c)).mOutcome, Outcome::Unusable);
}

TEST(SkyrimHavok, a_mesh_is_a_mesh_a_list_is_a_compound_and_a_transform_shape_moves_its_child)
{
    H::Collision c;
    H::Shape mesh;
    mesh.mKind = H::ShapeKind::TriMesh;
    mesh.mTypeName = "bhkCompressedMeshShape";
    mesh.mVertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
    mesh.mTriangles = { { 0, 1, 2 }, { 0, 2, 3 } };
    c.mShapes.push_back(mesh); // 0
    H::Shape moved;
    moved.mKind = H::ShapeKind::Transform;
    moved.mTypeName = "bhkConvexTransformShape";
    moved.mTransform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 0, 0, 1 }; // translation in the last row
    moved.mChildren = { 2 };
    c.mShapes.push_back(moved); // 1
    c.mShapes.push_back(box(0.1f, 0.1f, 0.1f)); // 2
    H::Shape list;
    list.mKind = H::ShapeKind::List;
    list.mTypeName = "bhkListShape";
    list.mChildren = { 0, 1 };
    c.mShapes.push_back(list); // 3
    c.mBodies.push_back(body(3));
    const Made m = make(sceneWith(c));
    ASSERT_EQ(m.mOutcome, Outcome::Shape) << m.mWhy;
    EXPECT_NEAR(rayDown(*m.mShape->mCollisionShape, 0.5 * k, 0.5 * k, 500.0), 0.0, 0.05); // the mesh
    EXPECT_NEAR(rayDown(*m.mShape->mCollisionShape, 5.0 * k, 0.0, 500.0), 0.1 * k, 0.05); // the moved box
}

TEST(SkyrimHavok, a_capsule_between_two_points_is_laid_along_them)
{
    H::Collision c;
    H::Shape capsule;
    capsule.mKind = H::ShapeKind::Capsule;
    capsule.mTypeName = "bhkCapsuleShape";
    capsule.mPoint1 = { -1.f, 0.f, 0.f };
    capsule.mPoint2 = { 1.f, 0.f, 0.f };
    capsule.mRadius1 = capsule.mRadius2 = 0.25f;
    c.mShapes.push_back(capsule);
    c.mBodies.push_back(body(0));
    const Made m = make(sceneWith(c));
    ASSERT_EQ(m.mOutcome, Outcome::Shape) << m.mWhy;
    EXPECT_NEAR(m.mShape->mCollisionBox.mExtents.x, 1.25f * k, 0.1);
    EXPECT_NEAR(m.mShape->mCollisionBox.mExtents.y, 0.25f * k, 0.1);
    EXPECT_NEAR(rayDown(*m.mShape->mCollisionShape, 0.0, 0.0, 500.0), 0.25 * k, 0.1);
}

TEST(SkyrimHavok, a_convex_hull_of_a_unit_cube_is_a_cube)
{
    H::Collision c;
    H::Shape hull;
    hull.mKind = H::ShapeKind::ConvexHull;
    hull.mTypeName = "bhkConvexVerticesShape";
    for (int i = 0; i < 8; ++i)
        hull.mPoints.push_back({ i & 1 ? 1.f : -1.f, i & 2 ? 1.f : -1.f, i & 4 ? 1.f : -1.f });
    c.mShapes.push_back(hull);
    c.mBodies.push_back(body(0));
    const Made m = make(sceneWith(c));
    ASSERT_EQ(m.mOutcome, Outcome::Shape) << m.mWhy;
    EXPECT_NEAR(rayDown(*m.mShape->mCollisionShape, 0.0, 0.0, 500.0), k, 0.2);
}

TEST(SkyrimHavok, a_degenerate_hull_is_skipped_and_counted_not_fatal)
{
    H::Collision c;
    H::Shape hull;
    hull.mKind = H::ShapeKind::ConvexHull;
    hull.mTypeName = "bhkConvexVerticesShape";
    hull.mPoints = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, { 3, 0, 0 } }; // on one line
    c.mShapes.push_back(hull);
    c.mShapes.push_back(box(1, 1, 1));
    H::Shape list;
    list.mKind = H::ShapeKind::List;
    list.mTypeName = "bhkListShape";
    list.mChildren = { 0, 1 };
    c.mShapes.push_back(list);
    c.mBodies.push_back(body(2));
    const Made m = make(sceneWith(c));
    ASSERT_EQ(m.mOutcome, Outcome::Shape) << m.mWhy;
    EXPECT_EQ(m.mReport.mHullsRefused, 1u);
}

TEST(SkyrimHavok, a_transform_shape_rotates_by_the_row_vector_matrix_it_is_stored_as)
{
    // A long thin box along x, turned a quarter turn about z by the matrix whose rows are (0,1,0), (-1,0,0), (0,0,1):
    // the point (1,0,0) goes to (0,1,0), so the box ends up long in y.
    H::Collision c;
    c.mShapes.push_back(box(1.f, 0.1f, 0.1f)); // 0
    H::Shape turned;
    turned.mKind = H::ShapeKind::Transform;
    turned.mTypeName = "bhkConvexTransformShape";
    turned.mTransform = { 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
    turned.mChildren = { 0 };
    c.mShapes.push_back(turned); // 1
    c.mBodies.push_back(body(1));
    const Made m = make(sceneWith(c));
    ASSERT_EQ(m.mOutcome, Outcome::Shape) << m.mWhy;
    EXPECT_NEAR(m.mShape->mCollisionBox.mExtents.y, 1.f * k, 0.05);
    EXPECT_NEAR(m.mShape->mCollisionBox.mExtents.x, 0.1f * k, 0.05);
}
