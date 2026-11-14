// Task 0157: a walker climbing stairs rests its capsule's rim on the next step's nosing, and the ground under its
// centre is the lower tread, more than kStepDown below the resting feet (found in game: a sleeper of the Bannered Mare
// stood for ever at the foot of the stairs with "blocked edge"; the nosing it rested on was 4.8 units above its feet
// and the ground under its centre 28.8 below the rest). A rim that carries the capsule UP is support; the same distance
// below a capsule that is level or going down is still a ledge. Written before the change; the first run was against
// the unchanged mover (the climbing case failed).
#include "apps/openmw/mwphysics/neutralmover.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace MWPhysics::NeutralMover;

    // Free air everywhere except: a downward sweep rests on a rim at `rimZ`, and the ground under the centre is at
    // `groundZ`.
    class NosingWorld final : public Sweeper
    {
    public:
        NosingWorld(float rimZ, float groundZ)
            : mRim(rimZ)
            , mGround(groundZ)
        {
        }
        Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override
        {
            Sweep s;
            if (delta.z < 0.f)
            {
                const float t = (from.z - mRim) / -delta.z;
                if (t >= 0.f && t <= 1.f)
                {
                    s.mHit = true;
                    s.mFraction = t;
                    s.mNormal = vsg::vec3(0.f, -0.67f, 0.74f); // the rim of a corner, tilted but walkable
                }
            }
            return s;
        }
        Sweep probeDown(const vsg::vec3& from, float depth) const override
        {
            Sweep s;
            const float t = (from.z - mGround) / depth;
            if (t >= 0.f && t <= 1.f)
            {
                s.mHit = true;
                s.mFraction = t;
            }
            return s;
        }

    private:
        float mRim;
        float mGround;
    };

    Result walk(const Sweeper& world)
    {
        Request r;
        r.mVelX = 100.f;
        return step(vsg::vec3(0.f, 0.f, 0.f), r, 1.f / 60.f, world);
    }
}

TEST(NeutralMoverRim, a_rim_that_carries_the_capsule_up_is_support_though_the_centre_ground_is_far_below)
{
    // rest 4.8 up, ground 28.8 below the rest: beyond the usual probe (kProbeLift + kStepDown = 28), within kRimReach
    // more
    const NosingWorld world(4.8f, 4.8f - 28.8f);
    const Result r = walk(world);
    EXPECT_TRUE(r.mMoved);
    EXPECT_FALSE(r.mEdge);
    EXPECT_NEAR(r.mFeet.z, 4.8f, 1e-3f);
}

TEST(NeutralMoverRim, the_same_distance_below_a_level_capsule_is_still_a_ledge)
{
    const NosingWorld world(0.f, -30.f);
    const Result r = walk(world);
    EXPECT_FALSE(r.mMoved);
    EXPECT_TRUE(r.mEdge);
}

TEST(NeutralMoverRim, going_down_onto_a_rim_is_still_a_ledge)
{
    const NosingWorld world(-5.f, -40.f);
    EXPECT_TRUE(walk(world).mEdge);
}

TEST(NeutralMoverRim, a_climb_over_a_deep_drop_is_still_a_ledge)
{
    const NosingWorld world(4.8f, 4.8f - 28.f - kRimReach - 5.f);
    EXPECT_TRUE(walk(world).mEdge);
}

TEST(NeutralMoverRim, a_rise_of_a_hair_is_not_a_climb)
{
    const NosingWorld world(kRimMinClimb * 0.5f, kRimMinClimb * 0.5f - 30.f);
    EXPECT_TRUE(walk(world).mEdge);
}

TEST(NeutralMoverRim, ground_within_the_usual_probe_is_unchanged)
{
    const NosingWorld world(4.8f, 4.8f - 10.f);
    const Result r = walk(world);
    EXPECT_TRUE(r.mMoved);
    EXPECT_FALSE(r.mEdge);
}
