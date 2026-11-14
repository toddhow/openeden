// Task 0135 stage S4: a floor seam or bevel under the capsule's centre is not a steep slope (found in game: the actors of
// the Bannered Mare stopped dead on a 54 degree edge of a floor mesh that is flat on either side).
#include "apps/openmw/mwphysics/neutralmover.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace
{
    using namespace MWPhysics::NeutralMover;

    // A flat floor for the capsule's own sweeps (the drop lands at half the depth, like the test above), and a ground
    // probe at the centre whose normal and distance the test chooses.
    class SeamFloor final : public Sweeper
    {
    public:
        SeamFloor(float normalZ, float groundBelowRest)
            : mNormalZ(normalZ)
            , mBelow(groundBelowRest)
        {
        }
        Sweep sweep(const vsg::vec3&, const vsg::vec3& delta) const override
        {
            Sweep s;
            if (delta.z < 0.f)
            {
                s.mHit = true;
                // Task 0159 (approved edit): rests level, where the capsule started (the old 0.5 rested 0.125 higher,
                // which the new rule reads as a climb over a rim).
                s.mFraction = (kStepUp + kSkin) / (kStepUp + kSkin + kStepDown);
            }
            return s;
        }
        Sweep probeDown(const vsg::vec3&, float depth) const override
        {
            Sweep s;
            s.mHit = true;
            // The probe starts kProbeLift above the resting feet: the ground is mBelow under them.
            s.mFraction = (kProbeLift + mBelow) / depth;
            const float nx = std::sqrt(1.f - mNormalZ * mNormalZ);
            s.mNormal = vsg::vec3(nx, 0.f, mNormalZ);
            return s;
        }

    private:
        float mNormalZ;
        float mBelow;
    };

    Result walkOnce(const Sweeper& world)
    {
        Request r;
        r.mVelX = 100.f;
        return step(vsg::vec3(0.f, 0.f, 0.f), r, 1.f / 60.f, world);
    }

    TEST(NeutralMoverBevel, aSteepNormalWithTheGroundJustBelowTheRestingCapsuleIsASeamAndIsWalked)
    {
        const SeamFloor world(0.587f, 1.8f); // the measured case: normal z 0.587, ground 1.8 under the feet
        const Result r = walkOnce(world);
        EXPECT_TRUE(r.mMoved);
        EXPECT_FALSE(r.mBlocked);
        EXPECT_GT(r.mFeet.x, 0.f);
    }

    TEST(NeutralMoverBevel, aSteepNormalWithTheGroundFarBelowIsASlopeAndStillRefuses)
    {
        for (const float below : { kBevelSlack + 0.5f, 6.f, 12.f })
        {
            const SeamFloor world(0.6f, below);
            const Result r = walkOnce(world);
            EXPECT_FALSE(r.mMoved) << below;
            EXPECT_TRUE(r.mBlocked) << below;
            EXPECT_EQ(r.mFeet.x, 0.f) << below;
        }
    }

    TEST(NeutralMoverBevel, aWalkableNormalIsWalkedWhereverTheCentreGroundIs)
    {
        for (const float below : { 0.f, 1.f, 10.f })
        {
            const SeamFloor world(0.9f, below);
            EXPECT_TRUE(walkOnce(world).mMoved) << below;
        }
    }

    // A world whose floor is at z = 0 and reports any sweep that starts in contact with it (z <= 0) as a hit at fraction 0,
    // in every direction, as Jolt does for a capsule that rests on a mesh.
    class ContactFloor final : public Sweeper
    {
    public:
        Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override
        {
            Sweep s;
            if (delta.z < 0.f)
            {
                const float t = from.z / -delta.z;
                if (t <= 1.f)
                {
                    s.mHit = true;
                    s.mFraction = std::max(0.f, t);
                }
            }
            else if (from.z <= 0.f)
            {
                s.mHit = true;
                s.mFraction = 0.f;
            }
            return s;
        }
        Sweep probeDown(const vsg::vec3&, float) const override
        {
            Sweep s;
            s.mHit = true;
            s.mFraction = kProbeLift / (kProbeLift + kStepDown);
            return s;
        }
    };

    TEST(NeutralMoverContact, anActorRestingInContactWithTheFloorIsNotStuckByTheFractionZeroHit)
    {
        const ContactFloor world;
        vsg::vec3 pos(0.f, 0.f, 0.f);
        Request r;
        r.mVelX = 100.f;
        for (int i = 0; i < 30; ++i)
        {
            const Result res = step(pos, r, 1.f / 60.f, world);
            ASSERT_TRUE(res.mMoved) << i;
            EXPECT_FALSE(res.mBlocked) << i;
            EXPECT_NEAR(res.mFeet.z, 0.f, 1e-3f) << i; // back on the floor, not floating by the lift
            pos = res.mFeet;
        }
        EXPECT_NEAR(pos.x, 100.f * 30.f / 60.f, 0.01f);
    }
}
