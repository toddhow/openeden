// Task 0157: the ground probe under a walker's centre falls into the thin gaps between the planks of a stair or floor mesh
// (found in game: a sleeper of the Bannered Mare stopped on the stairs with "ground miss", the probe 0.25 units beside a
// tread that was there). The probe now takes the highest of a few rays round the centre. Written before the change; the
// first run was against the single-ray probe (the gap case failed).
#include <components/misc/constants.hpp>
#include <components/resource/physicsshape.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include "ragdolltestworld.hpp"

#include "apps/openmw/mwphysics/neutralactor.hpp"
#include "apps/openmw/mwphysics/neutralmover.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace
{
    using namespace MWPhysics::NeutralMover;

    class NeutralMoverGap : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            MWPhysicsTest::ensureJoltInitialized();
            mWorld = std::make_unique<MWPhysicsTest::GameLikeWorld>(false);
            mShape = MWPhysics::NeutralActor::makeShape(MWPhysics::NeutralActor::capsuleFor(1.f));
            ASSERT_TRUE(mShape != nullptr);
        }

        void addBox(JPH::Vec3 half, JPH::RVec3 centre)
        {
            const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(half);
            const JPH::BodyCreationSettings settings(
                shape, centre, JPH::Quat::sIdentity(), JPH::EMotionType::Static, MWPhysics::Layers::WORLD);
            mWorld->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        }

        void addTiltedBox(JPH::Vec3 half, JPH::RVec3 centre, float tiltDegrees)
        {
            const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(half);
            const JPH::BodyCreationSettings settings(shape, centre,
                JPH::Quat::sRotation(JPH::Vec3::sAxisY(), tiltDegrees * JPH::JPH_PI / 180.f), JPH::EMotionType::Static,
                MWPhysics::Layers::WORLD);
            mWorld->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        }

        JoltSweeper sweeper() const
        {
            return JoltSweeper(mWorld->system(), mShape->mCollisionShape, JPH::BodyID(), MWPhysics::Layers::WORLD);
        }

        std::unique_ptr<MWPhysicsTest::GameLikeWorld> mWorld;
        vsg::ref_ptr<Resource::PhysicsShape> mShape;
    };
}

TEST_F(NeutralMoverGap, a_ray_over_a_floor_without_gaps_finds_it_where_it_always_did)
{
    addBox(JPH::Vec3(100, 100, 5), JPH::RVec3(0, 0, -5)); // top at z = 0
    const Sweep s = sweeper().probeDown(vsg::vec3(0, 0, 4), 28.f);
    ASSERT_TRUE(s.mHit);
    EXPECT_NEAR(4.f - 28.f * s.mFraction, 0.f, 0.01f);
    EXPECT_GT(s.mNormal.z, 0.99f);
}

TEST_F(NeutralMoverGap, a_thin_gap_between_two_planks_under_the_centre_is_not_a_hole)
{
    // two planks with a 0.4 unit gap at x = 0
    addBox(JPH::Vec3(50, 100, 5), JPH::RVec3(-50.2, 0, -5));
    addBox(JPH::Vec3(50, 100, 5), JPH::RVec3(50.2, 0, -5));
    const Sweep s = sweeper().probeDown(vsg::vec3(0, 0, 4), 28.f);
    ASSERT_TRUE(s.mHit);
    EXPECT_NEAR(4.f - 28.f * s.mFraction, 0.f, 0.01f);
}

TEST_F(NeutralMoverGap, a_real_hole_is_still_a_hole)
{
    // a 40 unit wide hole: the rays round the centre (a few units away) all miss
    addBox(JPH::Vec3(50, 100, 5), JPH::RVec3(-70, 0, -5));
    addBox(JPH::Vec3(50, 100, 5), JPH::RVec3(70, 0, -5));
    EXPECT_FALSE(sweeper().probeDown(vsg::vec3(0, 0, 4), 28.f).mHit);
}

TEST_F(NeutralMoverGap, the_highest_surface_round_the_centre_is_the_ground)
{
    // a low floor, and a stair tread whose nosing is just beside the centre
    addBox(JPH::Vec3(100, 100, 5), JPH::RVec3(0, 0, -25)); // top at -20
    addBox(JPH::Vec3(20, 100, 5), JPH::RVec3(23.5, 0, -5)); // top at 0, edge at x = 3.5
    const Sweep s = sweeper().probeDown(vsg::vec3(0, 0, 4), 40.f);
    ASSERT_TRUE(s.mHit);
    EXPECT_NEAR(4.f - 40.f * s.mFraction, 0.f, 0.01f);
}

TEST_F(NeutralMoverGap, a_steep_bevel_at_the_centre_beside_a_walkable_shelf_reports_the_shelfs_normal)
{
    // the dirt floor of the Bannered Mare's cellar: shelves 3 to 10 units apart joined by steep bevels; a ray at the centre
    // can land on a bevel (normal z 0.42) with a flat shelf at the same height a few units away
    addBox(JPH::Vec3(100, 100, 5), JPH::RVec3(0, 0, -5)); // flat, top at z = 0
    // a 65 degree face covering the centre column only: its top face is steep
    addTiltedBox(JPH::Vec3(2, 100, 0.5f), JPH::RVec3(0, 0, 0.3f), -65.f);
    const Sweep s = sweeper().probeDown(vsg::vec3(0, 0, 4), 28.f);
    ASSERT_TRUE(s.mHit);
    EXPECT_GT(s.mNormal.z, 0.9f);
}

TEST_F(NeutralMoverGap, a_slope_that_is_steep_all_round_the_centre_stays_steep)
{
    addTiltedBox(JPH::Vec3(100, 100, 5), JPH::RVec3(0, 0, -5), -60.f);
    const Sweep s = sweeper().probeDown(vsg::vec3(0, 0, 4), 60.f);
    ASSERT_TRUE(s.mHit);
    EXPECT_LT(s.mNormal.z, 0.6f);
}
