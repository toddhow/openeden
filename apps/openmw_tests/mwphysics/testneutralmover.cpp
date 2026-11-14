// Task 0135 (decision 0017 stage S2): the movement of a neutral actor's capsule body. Written from the header before
// the implementation (the first run was against a stub, see the task's stage log). The pure stepping is tested in a
// world of axis-aligned boxes (the actor is a box of the capsule's footprint and height, which is exact for the
// logic: step up, wall, slide, floor snap), the Jolt sweeper against a real Jolt system with the capsule shape.
#include <components/misc/constants.hpp>
#include <components/resource/physicsshape.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include "ragdolltestworld.hpp"

#include "apps/openmw/mwphysics/neutralactor.hpp"
#include "apps/openmw/mwphysics/neutralmover.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace
{
    using namespace MWPhysics::NeutralMover;

    constexpr float kDt = 1.f / 60.f;
    constexpr float kHalf = 22.f; // half the footprint of the test actor
    constexpr float kHeight = 128.f;

    struct Box
    {
        float mMin[3];
        float mMax[3];
    };

    /// A world of static boxes, swept by a box the size of the actor (feet at the position given).
    class BoxWorld final : public Sweeper
    {
    public:
        std::vector<Box> mBoxes;

        void add(float x0, float y0, float z0, float x1, float y1, float z1)
        {
            mBoxes.push_back({ { x0, y0, z0 }, { x1, y1, z1 } });
        }

        Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override
        {
            return cast(from, delta, kHalf, kHeight);
        }

        Sweep probeDown(const vsg::vec3& from, float depth) const override
        {
            return cast(from, vsg::vec3(0.f, 0.f, -depth), 0.f, 0.f);
        }

    private:
        Sweep cast(const vsg::vec3& from, const vsg::vec3& delta, float half, float height) const
        {
            Sweep best;
            const float start[3] = { from.x, from.y, from.z };
            const float d[3] = { delta.x, delta.y, delta.z };
            for (const Box& b : mBoxes)
            {
                // The actor's box grown into the obstacle: a point (the feet) against the Minkowski sum.
                const float lo[3] = { b.mMin[0] - half, b.mMin[1] - half, b.mMin[2] - height };
                const float hi[3] = { b.mMax[0] + half, b.mMax[1] + half, b.mMax[2] };
                float entry = -std::numeric_limits<float>::infinity();
                float exit = std::numeric_limits<float>::infinity();
                int entryAxis = 0;
                float entrySign = 1.f;
                bool miss = false;
                for (int a = 0; a < 3 && !miss; ++a)
                {
                    if (std::fabs(d[a]) < 1e-9f)
                    {
                        // Touching a face is not inside.
                        if (!(start[a] > lo[a] && start[a] < hi[a]))
                            miss = true;
                        continue;
                    }
                    float t1 = (lo[a] - start[a]) / d[a];
                    float t2 = (hi[a] - start[a]) / d[a];
                    float sign = -1.f; // entering through the low face: the normal points to -axis
                    if (t1 > t2)
                    {
                        std::swap(t1, t2);
                        sign = 1.f;
                    }
                    if (t1 > entry)
                    {
                        entry = t1;
                        entryAxis = a;
                        entrySign = sign;
                    }
                    exit = std::min(exit, t2);
                }
                if (miss || entry >= exit || entry > 1.f || exit <= 0.f)
                    continue;
                const float fraction = std::max(entry, 0.f);
                if (!best.mHit || fraction < best.mFraction)
                {
                    best.mHit = true;
                    best.mFraction = fraction;
                    best.mNormal = vsg::vec3(0.f, 0.f, 0.f);
                    (&best.mNormal.x)[entryAxis] = entrySign;
                }
            }
            return best;
        }
    };

    BoxWorld flatFloor()
    {
        BoxWorld w;
        w.add(-5000, -5000, -100, 5000, 5000, 0);
        return w;
    }

    vsg::vec3 walk(const BoxWorld& w, vsg::vec3 pos, float vx, float vy, int frames, bool* blocked = nullptr)
    {
        Request r;
        r.mVelX = vx;
        r.mVelY = vy;
        for (int i = 0; i < frames; ++i)
        {
            const Result res = step(pos, r, kDt, w);
            pos = res.mFeet;
            if (blocked != nullptr)
                *blocked = *blocked || res.mBlocked;
        }
        return pos;
    }

    TEST(NeutralMoverStep, on_a_flat_floor_the_actor_covers_the_distance_and_stays_at_the_floor_height)
    {
        const BoxWorld w = flatFloor();
        const vsg::vec3 end = walk(w, vsg::vec3(0, 0, 0), 100.f, 0.f, 60);
        EXPECT_NEAR(end.x, 100.f, 0.01f);
        EXPECT_NEAR(end.y, 0.f, 0.01f);
        EXPECT_NEAR(end.z, 0.f, 0.01f);
    }

    TEST(NeutralMoverStep, the_diagonal_speed_is_the_length_of_the_velocity)
    {
        const BoxWorld w = flatFloor();
        const vsg::vec3 end = walk(w, vsg::vec3(0, 0, 0), 60.f, 80.f, 60);
        EXPECT_NEAR(end.x, 60.f, 0.01f);
        EXPECT_NEAR(end.y, 80.f, 0.01f);
    }

    TEST(NeutralMoverStep, a_step_within_the_step_up_height_is_climbed)
    {
        BoxWorld w = flatFloor();
        w.add(60, -1000, -100, 1000, 1000, 16); // a 16 unit riser at x = 60
        bool blocked = false;
        const vsg::vec3 end = walk(w, vsg::vec3(0, 0, 0), 100.f, 0.f, 120, &blocked);
        EXPECT_NEAR(end.z, 16.f, 0.01f);
        EXPECT_GT(end.x, 150.f); // well past the riser at 100 units a second over two seconds
        EXPECT_FALSE(blocked);
    }

    TEST(NeutralMoverStep, a_ledge_higher_than_the_step_up_height_stops_the_actor_at_its_foot)
    {
        BoxWorld w = flatFloor();
        w.add(100, -1000, -100, 1000, 1000, 40); // 40 > 24
        bool blocked = false;
        const vsg::vec3 end = walk(w, vsg::vec3(0, 0, 0), 100.f, 0.f, 120, &blocked);
        EXPECT_NEAR(end.z, 0.f, 0.01f);
        EXPECT_LT(end.x, 100.f - kHalf + 0.01f); // the footprint stops at the ledge
        EXPECT_GT(end.x, 100.f - kHalf - 1.f);
        EXPECT_TRUE(blocked);
    }

    TEST(NeutralMoverStep, a_wall_is_slid_along_when_the_motion_is_diagonal)
    {
        BoxWorld w = flatFloor();
        w.add(100, -1000, 0, 120, 1000, 500); // a wall across the path
        const vsg::vec3 end = walk(w, vsg::vec3(0, 0, 0), 140.f, 140.f, 60);
        EXPECT_LT(end.x, 100.f - kHalf + 0.01f);
        EXPECT_GT(end.x, 100.f - kHalf - 1.f);
        EXPECT_GT(end.y, 130.f); // kept sliding north: after a second, nearly all of the 140 units
        EXPECT_NEAR(end.z, 0.f, 0.01f);
    }

    TEST(NeutralMoverStep, walking_straight_into_a_wall_stops_without_moving_through_it)
    {
        BoxWorld w = flatFloor();
        w.add(100, -1000, 0, 120, 1000, 500);
        bool blocked = false;
        const vsg::vec3 end = walk(w, vsg::vec3(0, 0, 0), 100.f, 0.f, 120, &blocked);
        EXPECT_LT(end.x, 100.f - kHalf + 0.01f);
        EXPECT_NEAR(end.y, 0.f, 0.01f);
        EXPECT_TRUE(blocked);
    }

    TEST(NeutralMoverStep, the_floor_is_followed_down_a_stair)
    {
        BoxWorld w;
        // Treads 60 long, each 16 lower than the last, starting at x = 0 with the top at z = 0.
        for (int i = 0; i < 6; ++i)
            w.add(i * 60.f, -1000, -1000, i * 60.f + 60.f, 1000, -16.f * i);
        const vsg::vec3 end = walk(w, vsg::vec3(10, 0, 0), 100.f, 0.f, 150); // 250 units
        EXPECT_GT(end.x, 250.f);
        // The footprint is over the tread under the feet or the next one; the feet are on one of the treads' tops.
        const int tread = static_cast<int>(std::floor((end.x - kHalf) / 60.f));
        const int lowest = std::min(tread, 5);
        EXPECT_NEAR(end.z, -16.f * lowest, 0.02f);
        EXPECT_LT(end.z, -40.f);
    }

    TEST(NeutralMoverStep, a_drop_beyond_the_step_down_height_is_not_walked_off)
    {
        BoxWorld w;
        w.add(-1000, -1000, -1000, 300, 1000, 0); // a ledge; 100 units below, the lower floor
        w.add(300, -1000, -1000, 2000, 1000, -100);
        bool blocked = false;
        Request r;
        r.mVelX = 100.f;
        bool edge = false;
        vsg::vec3 pos(0, 0, 0);
        for (int i = 0; i < 300; ++i)
        {
            const Result res = step(pos, r, kDt, w);
            pos = res.mFeet;
            blocked = blocked || res.mBlocked;
            edge = edge || res.mEdge;
        }
        EXPECT_TRUE(edge);
        EXPECT_TRUE(blocked);
        EXPECT_NEAR(pos.z, 0.f, 0.01f);
        // The centre must keep floor under it: the actor stops at the ledge's edge, not with its body hanging over.
        EXPECT_LT(pos.x, 300.f + 0.01f);
        EXPECT_GT(pos.x, 300.f - 2.f);
    }

    TEST(NeutralMoverStep, a_slope_steeper_than_the_floor_limit_is_not_climbed)
    {
        // The ground under the actor's centre is a very steep ramp (the capsule's own sweep lands flat).
        class SteepFloor final : public Sweeper
        {
        public:
            Sweep sweep(const vsg::vec3&, const vsg::vec3& delta) const override
            {
                Sweep s;
                if (delta.z < 0.f)
                {
                    s.mHit = true;
                    // Task 0159 (approved edit): the capsule lands level, where it started. The old 0.5 landed 0.125
                    // higher, which the new rule reads as a climb over a rim; the test is about a slope that is no
                    // climb.
                    s.mFraction = (kStepUp + kSkin) / (kStepUp + kSkin + kStepDown);
                }
                return s;
            }
            Sweep probeDown(const vsg::vec3&, float) const override
            {
                Sweep s;
                s.mHit = true;
                s.mFraction = 0.5f;
                s.mNormal = vsg::vec3(0.8f, 0.f, 0.6f);
                return s;
            }
        } world;
        Request r;
        r.mVelX = 50.f;
        const Result res = step(vsg::vec3(0, 0, 0), r, kDt, world);
        EXPECT_FALSE(res.mMoved);
        EXPECT_TRUE(res.mBlocked);
        EXPECT_EQ(res.mFeet.x, 0.f);
    }

    TEST(NeutralMoverStep, a_low_ceiling_limits_the_rise_so_a_ledge_is_not_climbed_under_it)
    {
        BoxWorld w = flatFloor();
        w.add(80, -1000, 0, 1000, 1000, 20); // a 20 high ledge, climbable with a rise of 20 or more
        const vsg::vec3 free = walk(w, vsg::vec3(0, 0, 0), 100.f, 0.f, 60);
        EXPECT_NEAR(free.z, 20.f, 0.01f);

        w.add(-1000, -1000, 140, 1000, 1000, 400); // a ceiling 12 above the head: the rise is under 12
        bool blocked = false;
        const vsg::vec3 under = walk(w, vsg::vec3(0, 0, 0), 100.f, 0.f, 60, &blocked);
        EXPECT_NEAR(under.z, 0.f, 0.01f);
        EXPECT_LT(under.x, 80.f - kHalf + 0.01f);
        EXPECT_TRUE(blocked);
    }

    TEST(NeutralMoverStep, input_that_is_not_a_move_changes_nothing)
    {
        const BoxWorld w = flatFloor();
        const vsg::vec3 p(5, 6, 0);
        Request r;
        r.mVelX = 100.f;
        EXPECT_EQ(step(p, r, 0.f, w).mFeet, p);
        EXPECT_EQ(step(p, r, -1.f, w).mFeet, p);
        EXPECT_EQ(step(p, r, std::numeric_limits<float>::quiet_NaN(), w).mFeet, p);
        EXPECT_EQ(step(p, Request{}, kDt, w).mFeet, p);
        Request nan;
        nan.mVelX = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(step(p, nan, kDt, w).mMoved);
        Request inf;
        inf.mVelY = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(step(p, inf, kDt, w).mMoved);
        EXPECT_FALSE(step(vsg::vec3(std::numeric_limits<float>::quiet_NaN(), 0, 0), r, kDt, w).mMoved);
    }

    TEST(NeutralMoverStep, a_request_faster_than_the_limit_is_cut_to_it)
    {
        const BoxWorld w = flatFloor();
        Request r;
        r.mVelX = 1.0e6f;
        const Result res = step(vsg::vec3(0, 0, 0), r, kDt, w);
        EXPECT_NEAR(res.mFeet.x, kMaxSpeed * kDt, 0.01f);
    }

    TEST(NeutralMoverStep, two_runs_of_the_same_walk_are_bit_identical)
    {
        BoxWorld w = flatFloor();
        w.add(100, -1000, 0, 120, 1000, 500);
        w.add(-1000, 200, 0, 1000, 220, 10);
        const vsg::vec3 a = walk(w, vsg::vec3(1, 2, 0), 53.f, 71.f, 400);
        const vsg::vec3 b = walk(w, vsg::vec3(1, 2, 0), 53.f, 71.f, 400);
        EXPECT_EQ(std::memcmp(&a, &b, sizeof(a)), 0);
    }

    TEST(NeutralMoverStep, the_heading_faces_the_direction_of_travel_in_the_games_convention)
    {
        EXPECT_NEAR(headingOf(0.f, 1.f), 0.f, 1e-6f); // +Y
        EXPECT_NEAR(headingOf(1.f, 0.f), 1.5707964f, 1e-5f); // +X
        EXPECT_NEAR(headingOf(0.f, -1.f), 3.1415927f, 1e-5f);
        EXPECT_NEAR(headingOf(-1.f, 0.f), -1.5707964f, 1e-5f);
        EXPECT_EQ(headingOf(0.f, 0.f), 0.f);
        EXPECT_EQ(headingOf(std::numeric_limits<float>::quiet_NaN(), 1.f), 0.f);
    }

    TEST(NeutralMoverStep, a_step_allocates_nothing)
    {
        BoxWorld w = flatFloor();
        w.add(100, -1000, 0, 120, 1000, 500);
        Request r;
        r.mVelX = 70.f;
        r.mVelY = 70.f;
        vsg::vec3 pos(0, 0, 0);
        TestSupport::AllocationCounter counter;
        for (int i = 0; i < 200; ++i)
            pos = step(pos, r, kDt, w).mFeet;
        EXPECT_EQ(counter.count(), 0u);
    }

    // ---- the Jolt sweeper ---------------------------------------------------------------------------------------

    class NeutralMoverJolt : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            MWPhysicsTest::ensureJoltInitialized();
            mWorld = std::make_unique<MWPhysicsTest::GameLikeWorld>(true);
            mCapsule = MWPhysics::NeutralActor::capsuleFor(1.f);
            mShape = MWPhysics::NeutralActor::makeShape(mCapsule);
            ASSERT_TRUE(mShape != nullptr);
        }

        JPH::BodyID addBox(JPH::Vec3 half, JPH::RVec3 centre)
        {
            const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(half);
            const JPH::BodyCreationSettings settings(
                shape, centre, JPH::Quat::sIdentity(), JPH::EMotionType::Static, MWPhysics::Layers::WORLD);
            return mWorld->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        }

        JoltSweeper sweeper(JPH::BodyID self = JPH::BodyID()) const
        {
            return JoltSweeper(mWorld->system(), mShape->mCollisionShape, self, MWPhysics::Layers::WORLD);
        }

        std::unique_ptr<MWPhysicsTest::GameLikeWorld> mWorld;
        MWPhysics::NeutralActor::Capsule mCapsule;
        vsg::ref_ptr<Resource::PhysicsShape> mShape;
    };

    TEST_F(NeutralMoverJolt, a_capsule_swept_down_onto_the_floor_stops_at_the_floor_with_an_upward_normal)
    {
        const JoltSweeper s = sweeper();
        const Sweep r = s.sweep(vsg::vec3(0, 0, 40), vsg::vec3(0, 0, -100));
        ASSERT_TRUE(r.mHit);
        EXPECT_NEAR(40.f - 100.f * r.mFraction, 0.f, 0.05f);
        EXPECT_GT(r.mNormal.z, 0.99f);
    }

    TEST_F(NeutralMoverJolt, a_capsule_swept_into_a_wall_reports_the_wall_normal_at_the_capsule_radius)
    {
        addBox(JPH::Vec3(10, 500, 200), JPH::RVec3(200, 0, 200)); // a wall, face at x = 190
        const JoltSweeper s = sweeper();
        const Sweep r = s.sweep(vsg::vec3(0, 0, 30), vsg::vec3(400, 0, 0));
        ASSERT_TRUE(r.mHit);
        EXPECT_NEAR(400.f * r.mFraction, 190.f - mCapsule.mRadius, 0.1f);
        EXPECT_LT(r.mNormal.x, -0.99f);
    }

    TEST_F(NeutralMoverJolt, a_sweep_in_free_space_hits_nothing)
    {
        const JoltSweeper s = sweeper();
        EXPECT_FALSE(s.sweep(vsg::vec3(0, 0, 40), vsg::vec3(300, 0, 0)).mHit);
    }

    TEST_F(NeutralMoverJolt, the_actors_own_body_is_ignored)
    {
        const JPH::BodyID self = addBox(JPH::Vec3(10, 10, 60), JPH::RVec3(100, 0, 60));
        EXPECT_TRUE(sweeper().sweep(vsg::vec3(0, 0, 30), vsg::vec3(300, 0, 0)).mHit);
        EXPECT_FALSE(sweeper(self).sweep(vsg::vec3(0, 0, 30), vsg::vec3(300, 0, 0)).mHit);
    }

    TEST_F(NeutralMoverJolt, walking_on_a_real_floor_keeps_the_feet_on_it_and_climbs_a_step)
    {
        addBox(JPH::Vec3(200, 500, 8), JPH::RVec3(300, 0, 8)); // a 16 high step from x = 100
        const JoltSweeper s = sweeper();
        Request r;
        r.mVelX = 100.f;
        vsg::vec3 pos(0, 0, 0);
        for (int i = 0; i < 90; ++i)
            pos = step(pos, r, kDt, s).mFeet;
        EXPECT_GT(pos.x, 140.f);
        EXPECT_NEAR(pos.z, 16.f, 0.3f);
    }

    TEST_F(NeutralMoverJolt, walking_into_a_tall_wall_stops_and_slides_along_it)
    {
        addBox(JPH::Vec3(10, 800, 200), JPH::RVec3(200, 0, 200));
        const JoltSweeper s = sweeper();
        Request r;
        r.mVelX = 70.f;
        r.mVelY = 70.f;
        vsg::vec3 pos(0, 0, 0);
        for (int i = 0; i < 120; ++i)
            pos = step(pos, r, kDt, s).mFeet;
        EXPECT_LT(pos.x, 190.f - mCapsule.mRadius + 0.5f);
        EXPECT_GT(pos.y, 100.f);
        EXPECT_NEAR(pos.z, 0.f, 0.3f);
    }

    TEST_F(NeutralMoverJolt, a_sweep_allocates_nothing)
    {
        addBox(JPH::Vec3(10, 800, 200), JPH::RVec3(200, 0, 200));
        const JoltSweeper s = sweeper();
        Request r;
        r.mVelX = 70.f;
        r.mVelY = 70.f;
        vsg::vec3 pos(0, 0, 0);
        pos = step(pos, r, kDt, s).mFeet; // warm
        TestSupport::AllocationCounter counter;
        for (int i = 0; i < 50; ++i)
            pos = step(pos, r, kDt, s).mFeet;
        EXPECT_EQ(counter.count(), 0u);
    }
}
