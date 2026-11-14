// Task 0135 (decision 0017 stage N5-3): the capsule and the standing-on-the-floor rule of a neutral actor's physics
// body. Written from the header before the implementation; the first run was against a stub (see the task's stage log).
#include <components/misc/constants.hpp>
#include <components/resource/physicsshape.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

#include "ragdolltestworld.hpp"

#include "apps/openmw/mwphysics/neutralactor.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace MWPhysics::NeutralActor;

    constexpr float kG = Constants::GravityConst * Constants::UnitsPerMeter;
    constexpr float kDt = 1.f / 60.f;

    Fall at(float z, State state = State::Waiting)
    {
        Fall f;
        f.mZ = z;
        f.mState = state;
        return f;
    }

    FloorProbe floorAt(float z)
    {
        return { true, z };
    }

    // Runs the rule to a rest or a cap on frames; the floor never moves.
    Fall settle(Fall f, float floorZ, int maxFrames = 2000, int* framesOut = nullptr)
    {
        int frames = 0;
        while (f.mState != State::Grounded && f.mState != State::Lost && frames < maxFrames)
        {
            f = stepFall(f, kDt, floorAt(floorZ));
            ++frames;
        }
        if (framesOut != nullptr)
            *framesOut = frames;
        return f;
    }
}

TEST(NeutralActorCapsule, a_standard_actor_is_the_standing_height_and_radius)
{
    const Capsule c = capsuleFor(1.f);
    EXPECT_FLOAT_EQ(c.mRadius, kRadius);
    EXPECT_FLOAT_EQ(c.mHalfCylinder, kStandingHeight * 0.5f - kRadius);
    EXPECT_FLOAT_EQ(c.mCentreZ, kStandingHeight * 0.5f);
}

TEST(NeutralActorCapsule, the_height_follows_the_scale)
{
    const Capsule c = capsuleFor(1.5f);
    EXPECT_FLOAT_EQ(c.mCentreZ, kStandingHeight * 1.5f * 0.5f);
    EXPECT_FLOAT_EQ(2.f * (c.mHalfCylinder + c.mRadius), kStandingHeight * 1.5f);
}

TEST(NeutralActorCapsule, a_very_short_actor_gets_the_minimum_height_and_a_radius_that_still_fits)
{
    const Capsule c = capsuleFor(0.01f);
    EXPECT_FLOAT_EQ(2.f * (c.mHalfCylinder + c.mRadius), kMinHeight);
    EXPECT_LE(c.mRadius, kMinHeight * 0.25f);
    EXPECT_GT(c.mHalfCylinder, 0.f);
}

TEST(NeutralActorCapsule, a_huge_actor_is_cut_to_the_maximum_height)
{
    const Capsule c = capsuleFor(1000.f);
    EXPECT_FLOAT_EQ(2.f * (c.mHalfCylinder + c.mRadius), kMaxHeight);
}

TEST(NeutralActorCapsule, a_scale_that_is_not_a_positive_number_means_the_standard_one)
{
    const Capsule standard = capsuleFor(1.f);
    for (const float bad :
        { 0.f, -2.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        const Capsule c = capsuleFor(bad);
        EXPECT_FLOAT_EQ(c.mCentreZ, standard.mCentreZ) << bad;
        EXPECT_FLOAT_EQ(c.mRadius, standard.mRadius) << bad;
    }
}

TEST(NeutralActorShape, the_shape_stands_on_the_feet_and_reaches_the_head)
{
    MWPhysicsTest::ensureJoltInitialized();
    const Capsule c = capsuleFor(1.f);
    const auto shape = makeShape(c);
    ASSERT_TRUE(shape);
    ASSERT_TRUE(shape->mCollisionShape);
    // Jolt reports local bounds relative to the shape's centre of mass; the feet are the shape's own origin.
    const JPH::Vec3 com = shape->mCollisionShape->GetCenterOfMass();
    const JPH::AABox b(
        shape->mCollisionShape->GetLocalBounds().mMin + com, shape->mCollisionShape->GetLocalBounds().mMax + com);
    EXPECT_NEAR(b.mMin.GetZ(), 0.f, 1e-2f);
    EXPECT_NEAR(b.mMax.GetZ(), kStandingHeight, 1e-2f);
    EXPECT_NEAR(b.mMax.GetX(), kRadius, 1e-2f);
    EXPECT_NEAR(b.mMin.GetY(), -kRadius, 1e-2f);
}

TEST(NeutralActorShape, the_shape_is_marked_so_that_the_navigation_mesh_leaves_it_out)
{
    MWPhysicsTest::ensureJoltInitialized();
    const auto shape = makeShape(capsuleFor(1.f));
    ASSERT_TRUE(shape);
    EXPECT_EQ(shape->mVisualCollisionType, Resource::VisualCollisionType::Camera);
}

TEST(NeutralActorShape, a_ray_down_the_axis_hits_the_top_of_the_head)
{
    MWPhysicsTest::ensureJoltInitialized();
    const auto shape = makeShape(capsuleFor(1.f));
    ASSERT_TRUE(shape);
    JPH::RayCastResult hit;
    // Shape::CastRay works in centre-of-mass space.
    const JPH::Vec3 com = shape->mCollisionShape->GetCenterOfMass();
    const JPH::RayCast ray{ JPH::Vec3(0, 0, 300) - com, JPH::Vec3(0, 0, -300) };
    ASSERT_TRUE(shape->mCollisionShape->CastRay(ray, JPH::SubShapeIDCreator(), hit));
    EXPECT_NEAR(300.f - 300.f * hit.mFraction, kStandingHeight, 0.1f);
}

TEST(NeutralActorFall, a_floor_at_the_feet_grounds_the_actor_at_once)
{
    const Fall f = stepFall(at(100.f), kDt, floorAt(100.f));
    EXPECT_EQ(f.mState, State::Grounded);
    EXPECT_FLOAT_EQ(f.mZ, 100.f);
    EXPECT_FLOAT_EQ(f.mVelocity, 0.f);
}

TEST(NeutralActorFall, a_floor_a_little_below_snaps_the_actor_onto_it_without_a_fall)
{
    const Fall f = stepFall(at(110.f), kDt, floorAt(100.f));
    EXPECT_EQ(f.mState, State::Grounded);
    EXPECT_FLOAT_EQ(f.mZ, 100.f);
}

TEST(NeutralActorFall, the_snap_distance_is_exactly_the_settle_distance)
{
    EXPECT_EQ(stepFall(at(100.f + kSettleDistance), kDt, floorAt(100.f)).mState, State::Grounded);
    EXPECT_EQ(stepFall(at(100.f + kSettleDistance + 0.5f), kDt, floorAt(100.f)).mState, State::Falling);
}

TEST(NeutralActorFall, a_floor_just_above_the_feet_steps_the_actor_up)
{
    const Fall f = stepFall(at(100.f), kDt, floorAt(100.f + kStepUp));
    EXPECT_EQ(f.mState, State::Grounded);
    EXPECT_FLOAT_EQ(f.mZ, 100.f + kStepUp);
}

TEST(NeutralActorFall, a_far_floor_starts_a_fall_with_the_engines_gravity)
{
    const Fall f = stepFall(at(600.f), kDt, floorAt(0.f));
    EXPECT_EQ(f.mState, State::Falling);
    EXPECT_NEAR(f.mVelocity, -kG * kDt, 1e-3f);
    EXPECT_NEAR(f.mZ, 600.f - kG * kDt * kDt, 1e-3f);
}

TEST(NeutralActorFall, a_fall_goes_down_every_frame_and_lands_exactly_on_the_floor)
{
    Fall f = at(600.f);
    float last = f.mZ;
    int frames = 0;
    while (f.mState != State::Grounded && frames < 1000)
    {
        f = stepFall(f, kDt, floorAt(37.f));
        ++frames;
        EXPECT_LE(f.mZ, last);
        EXPECT_GE(f.mZ, 37.f);
        last = f.mZ;
    }
    EXPECT_EQ(f.mState, State::Grounded);
    EXPECT_FLOAT_EQ(f.mZ, 37.f);
    EXPECT_FLOAT_EQ(f.mVelocity, 0.f);
    // 563 units at about 627 units/s^2 take about 1.34 s: 80 frames, give or take the frame it lands in.
    EXPECT_GT(frames, 70);
    EXPECT_LT(frames, 95);
}

TEST(NeutralActorFall, the_fall_is_deterministic)
{
    int a = 0;
    int b = 0;
    const Fall x = settle(at(900.f), 0.f, 2000, &a);
    const Fall y = settle(at(900.f), 0.f, 2000, &b);
    EXPECT_EQ(a, b);
    EXPECT_EQ(x.mZ, y.mZ);
    EXPECT_EQ(x.mFrames, y.mFrames);
}

TEST(NeutralActorFall, the_speed_never_passes_the_terminal_speed)
{
    Fall f = at(100000.f);
    for (int i = 0; i < 600; ++i)
    {
        f = stepFall(f, kDt, floorAt(0.f));
        EXPECT_GE(f.mVelocity, -kTerminalSpeed);
    }
    EXPECT_FLOAT_EQ(f.mVelocity, -kTerminalSpeed);
}

TEST(NeutralActorFall, a_long_frame_does_not_carry_the_actor_through_the_floor)
{
    Fall f = at(500.f);
    for (int i = 0; i < 10 && f.mState != State::Grounded; ++i)
        f = stepFall(f, 2.f, floorAt(0.f));
    EXPECT_EQ(f.mState, State::Grounded);
    EXPECT_FLOAT_EQ(f.mZ, 0.f);
}

TEST(NeutralActorFall, with_no_floor_the_actor_waits_in_place_and_is_lost_after_the_wait_limit)
{
    Fall f = at(50.f);
    for (std::uint32_t i = 1; i < kMaxWaitFrames; ++i)
    {
        f = stepFall(f, kDt, {});
        ASSERT_EQ(f.mState, State::Waiting) << i;
        ASSERT_FLOAT_EQ(f.mZ, 50.f);
    }
    f = stepFall(f, kDt, {});
    EXPECT_EQ(f.mState, State::Lost);
    EXPECT_FLOAT_EQ(f.mZ, 50.f);
}

TEST(NeutralActorFall, a_floor_that_appears_while_waiting_is_used)
{
    Fall f = at(50.f);
    for (int i = 0; i < 5; ++i)
        f = stepFall(f, kDt, {});
    ASSERT_EQ(f.mState, State::Waiting);
    f = stepFall(f, kDt, floorAt(40.f));
    EXPECT_EQ(f.mState, State::Grounded);
    EXPECT_FLOAT_EQ(f.mZ, 40.f);
}

TEST(NeutralActorFall, a_floor_that_vanishes_during_a_fall_loses_the_actor_where_it_is)
{
    Fall f = stepFall(at(600.f), kDt, floorAt(0.f));
    ASSERT_EQ(f.mState, State::Falling);
    const float z = f.mZ;
    f = stepFall(f, kDt, {});
    EXPECT_EQ(f.mState, State::Lost);
    EXPECT_FLOAT_EQ(f.mZ, z);
}

TEST(NeutralActorFall, a_grounded_or_lost_actor_is_never_changed)
{
    Fall g = at(10.f, State::Grounded);
    EXPECT_EQ(stepFall(g, kDt, floorAt(-500.f)).mZ, 10.f);
    EXPECT_EQ(stepFall(g, kDt, floorAt(-500.f)).mState, State::Grounded);
    Fall l = at(10.f, State::Lost);
    EXPECT_EQ(stepFall(l, kDt, floorAt(5.f)).mState, State::Lost);
}

TEST(NeutralActorFall, a_time_step_that_is_not_positive_and_finite_changes_nothing)
{
    const Fall f = at(600.f);
    for (const float dt :
        { 0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        const Fall r = stepFall(f, dt, floorAt(0.f));
        EXPECT_EQ(r.mState, State::Waiting) << dt;
        EXPECT_FLOAT_EQ(r.mZ, 600.f) << dt;
        EXPECT_EQ(r.mFrames, 0u) << dt;
    }
}

TEST(NeutralActorFall, a_floor_height_that_is_not_a_number_counts_as_no_floor)
{
    const Fall f = stepFall(at(50.f), kDt, { true, std::numeric_limits<float>::quiet_NaN() });
    EXPECT_EQ(f.mState, State::Waiting);
    EXPECT_FLOAT_EQ(f.mZ, 50.f);
}
