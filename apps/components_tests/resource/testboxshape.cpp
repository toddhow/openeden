// Task 0109 (neutral world, stage N3a-3): the box shape of an object that has bounds and no collision mesh yet
// (Resource::makeBoxShape). Written from the task spec before the implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <Jolt/Jolt.h>

#include <Jolt/Geometry/AABox.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>

#include <components/resource/physicsshape.hpp>

namespace
{
    // The shape's bounds in the frame it was made in (Jolt's local bounds are relative to the centre of mass).
    JPH::AABox boundsOf(const JPH::Shape& shape)
    {
        const JPH::AABox local = shape.GetLocalBounds();
        const JPH::Vec3 com = shape.GetCenterOfMass();
        return JPH::AABox(local.mMin + com, local.mMax + com);
    }

    TEST(BoxShape, has_the_bounds_it_was_asked_for)
    {
        const auto shape = Resource::makeBoxShape(vsg::vec3(10, 20, 30), vsg::vec3(0, 0, 0));
        ASSERT_TRUE(shape);
        ASSERT_TRUE(shape->mCollisionShape);
        const JPH::AABox b = boundsOf(*shape->mCollisionShape);
        EXPECT_NEAR(b.mMin.GetX(), -10.f, 1e-3);
        EXPECT_NEAR(b.mMax.GetX(), 10.f, 1e-3);
        EXPECT_NEAR(b.mMin.GetY(), -20.f, 1e-3);
        EXPECT_NEAR(b.mMax.GetY(), 20.f, 1e-3);
        EXPECT_NEAR(b.mMin.GetZ(), -30.f, 1e-3);
        EXPECT_NEAR(b.mMax.GetZ(), 30.f, 1e-3);
        EXPECT_NEAR(shape->mCollisionBox.mExtents.z, 30.f, 1e-3);
    }

    TEST(BoxShape, the_centre_moves_the_box_in_its_own_frame)
    {
        const auto shape = Resource::makeBoxShape(vsg::vec3(10, 10, 5), vsg::vec3(50, -40, 100));
        ASSERT_TRUE(shape);
        const JPH::AABox b = boundsOf(*shape->mCollisionShape);
        EXPECT_NEAR(b.mMin.GetX(), 40.f, 1e-3);
        EXPECT_NEAR(b.mMax.GetX(), 60.f, 1e-3);
        EXPECT_NEAR(b.mMin.GetY(), -50.f, 1e-3);
        EXPECT_NEAR(b.mMax.GetY(), -30.f, 1e-3);
        EXPECT_NEAR(b.mMin.GetZ(), 95.f, 1e-3);
        EXPECT_NEAR(b.mMax.GetZ(), 105.f, 1e-3);
        EXPECT_FLOAT_EQ(shape->mCollisionBox.mCenter.x, 50.f);
    }

    TEST(BoxShape, a_ray_down_onto_a_floor_slab_hits_at_the_top_of_the_slab)
    {
        // A floor 400 x 400 and 10 thick whose top is at z = 5 (centre 0).
        const auto shape = Resource::makeBoxShape(vsg::vec3(200, 200, 5), vsg::vec3(0, 0, 0));
        ASSERT_TRUE(shape);
        // The ray in the shape's frame (centre of mass at the origin for a centred box): from z = 105 down 200 units.
        JPH::RayCast ray;
        ray.mOrigin = JPH::Vec3(10, -20, 105);
        ray.mDirection = JPH::Vec3(0, 0, -200);
        JPH::RayCastResult hit;
        ASSERT_TRUE(shape->mCollisionShape->CastRay(ray, JPH::SubShapeIDCreator(), hit));
        const float hitZ = 105.f - 200.f * hit.mFraction;
        EXPECT_NEAR(hitZ, 5.f, 1e-3);
    }

    TEST(BoxShape, a_ray_beside_the_slab_misses)
    {
        const auto shape = Resource::makeBoxShape(vsg::vec3(200, 200, 5), vsg::vec3(0, 0, 0));
        ASSERT_TRUE(shape);
        JPH::RayCast ray;
        ray.mOrigin = JPH::Vec3(250, 0, 105);
        ray.mDirection = JPH::Vec3(0, 0, -200);
        JPH::RayCastResult hit;
        EXPECT_FALSE(shape->mCollisionShape->CastRay(ray, JPH::SubShapeIDCreator(), hit));
    }

    TEST(BoxShape, a_half_extent_below_the_minimum_is_raised_to_it)
    {
        const auto shape = Resource::makeBoxShape(vsg::vec3(100, 100, 0.001f), vsg::vec3(0, 0, 0));
        ASSERT_TRUE(shape);
        const JPH::AABox b = boundsOf(*shape->mCollisionShape);
        EXPECT_NEAR(b.mMax.GetZ(), Resource::kMinBoxHalfExtent, 1e-4);
        EXPECT_NEAR(b.mMin.GetZ(), -Resource::kMinBoxHalfExtent, 1e-4);
        EXPECT_NEAR(b.mMax.GetX(), 100.f, 1e-3);
    }

    TEST(BoxShape, a_zero_box_is_still_a_box)
    {
        const auto shape = Resource::makeBoxShape(vsg::vec3(0, 0, 0), vsg::vec3(0, 0, 0));
        ASSERT_TRUE(shape);
        const JPH::AABox b = boundsOf(*shape->mCollisionShape);
        EXPECT_NEAR(b.mMax.GetX(), Resource::kMinBoxHalfExtent, 1e-4);
    }

    TEST(BoxShape, negative_extents_are_taken_by_size)
    {
        const auto shape = Resource::makeBoxShape(vsg::vec3(-10, -20, -30), vsg::vec3(0, 0, 0));
        ASSERT_TRUE(shape);
        const JPH::AABox b = boundsOf(*shape->mCollisionShape);
        EXPECT_NEAR(b.mMax.GetY(), 20.f, 1e-3);
    }

    TEST(BoxShape, a_value_that_is_not_finite_gives_no_shape)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(Resource::makeBoxShape(vsg::vec3(nan, 1, 1), vsg::vec3(0, 0, 0)));
        EXPECT_FALSE(Resource::makeBoxShape(vsg::vec3(1, inf, 1), vsg::vec3(0, 0, 0)));
        EXPECT_FALSE(Resource::makeBoxShape(vsg::vec3(1, 1, 1), vsg::vec3(0, nan, 0)));
        EXPECT_FALSE(Resource::makeBoxShape(vsg::vec3(1, 1, 1), vsg::vec3(0, 0, -inf)));
    }

    TEST(BoxShape, an_instance_of_a_box_shape_can_be_made_and_scaled)
    {
        const auto shape = Resource::makeBoxShape(vsg::vec3(10, 10, 10), vsg::vec3(0, 0, 0));
        ASSERT_TRUE(shape);
        const auto instance = Resource::makeInstance(shape);
        ASSERT_TRUE(instance);
        EXPECT_EQ(instance->mCollisionShape.GetPtr(), shape->mCollisionShape.GetPtr());
        const JPH::Ref<JPH::Shape> scaled = instance->mCollisionShape->ScaleShape(JPH::Vec3::sReplicate(2.f)).Get();
        ASSERT_TRUE(scaled);
        EXPECT_NEAR(scaled->GetLocalBounds().GetExtent().GetX(), 20.f, 1e-3);
    }
}
