// Task 0159: two things a real route of the Bannered Mare cellar needed (Lucia's way to her bed).
// (1) A capsule that climbs rests its rim on the nosing of a steep dirt step, and the ground under its centre is a steep
//     face (normal z 0.51): a pass that carries the capsule up is not refused for that steepness; a level one still is.
// (2) A trench narrower than the capsule (5 units wide, 34 deep) has the capsule's rim on both sides; recast's voxels
//     (6.8 units) average it away, so the navmesh crosses it. The centre probe sees only the drop. A gap with ground at
//     both ends of a short reach along the motion is bridged; a ledge (ground behind only) and a gap wider than the
//     capsule are not.
// Written before the changes; the first runs were against the old rules.
#include "apps/openmw/mwphysics/neutralmover.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using namespace MWPhysics::NeutralMover;

    // Free air; a downward sweep rests on a rim at rimZ; the ground under the centre is groundZ with a steep normal.
    class SteepStep final : public Sweeper
    {
    public:
        SteepStep(float rimZ, float groundZ)
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
                    s.mNormal = vsg::vec3(0.f, -0.67f, 0.74f);
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
                s.mNormal = vsg::vec3(0.86f, 0.f, 0.51f);
            }
            return s;
        }

    private:
        float mRim;
        float mGround;
    };

    Result walkOnce(const Sweeper& world)
    {
        Request r;
        r.mVelX = 100.f;
        return step(vsg::vec3(0.f, 0.f, 0.f), r, 1.f / 60.f, world);
    }

    TEST(NeutralMoverSteep, aClimbingPassOntoARimOverASteepFaceIsWalked)
    {
        const Result r = walkOnce(SteepStep(10.f, 0.f));
        EXPECT_TRUE(r.mMoved);
        EXPECT_FALSE(r.mBlocked);
        EXPECT_NEAR(r.mFeet.z, 10.f, 0.5f);
    }

    TEST(NeutralMoverSteep, aLevelPassOverASteepFaceFarBelowStillRefuses)
    {
        const Result r = walkOnce(SteepStep(0.f, -10.f));
        EXPECT_FALSE(r.mMoved);
        EXPECT_TRUE(r.mBlocked);
    }

    // Floor at z = 0 for x < gapStart and x > gapEnd, a deep floor (z = -deep) in the gap. The capsule (radius 22) rests at
    // z = 0 when any floor lies within its radius of its centre, else on the deep floor; the centre probe sees the floor
    // directly under it; nothing is ever in the way horizontally.
    class TrenchWorld final : public Sweeper
    {
    public:
        TrenchWorld(float gapStart, float gapEnd, float deep, bool floorBeyond = true)
            : mStart(gapStart)
            , mEnd(gapEnd)
            , mDeep(deep)
            , mBeyond(floorBeyond)
        {
        }
        float floorAt(float x) const
        {
            if (x < mStart)
                return 0.f;
            if (x <= mEnd)
                return -mDeep;
            return mBeyond ? 0.f : -mDeep;
        }
        Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override
        {
            Sweep s;
            if (delta.z >= 0.f)
                return s;
            float support = -mDeep;
            for (float dx = -22.f; dx <= 22.f; dx += 1.f)
                support = std::max(support, floorAt(from.x + dx));
            const float t = (from.z - support) / -delta.z;
            if (t >= 0.f && t <= 1.f)
            {
                s.mHit = true;
                s.mFraction = t;
            }
            return s;
        }
        Sweep probeDown(const vsg::vec3& from, float depth) const override
        {
            Sweep s;
            const float t = (from.z - floorAt(from.x)) / depth;
            if (t >= 0.f && t <= 1.f)
            {
                s.mHit = true;
                s.mFraction = t;
            }
            return s;
        }

    private:
        float mStart;
        float mEnd;
        float mDeep;
        bool mBeyond;
    };

    vsg::vec3 walkAcross(const Sweeper& world, float startX, int frames, bool* blocked = nullptr)
    {
        Request r;
        r.mVelX = 100.f;
        vsg::vec3 pos(startX, 0.f, 0.f);
        for (int i = 0; i < frames; ++i)
        {
            const Result res = step(pos, r, 1.f / 60.f, world);
            pos = res.mFeet;
            if (blocked)
                *blocked = *blocked || res.mBlocked;
        }
        return pos;
    }

    TEST(NeutralMoverSteep, aTrenchNarrowerThanTheCapsuleWithGroundBothSidesIsCrossed)
    {
        bool blocked = false;
        const vsg::vec3 end = walkAcross(TrenchWorld(100.f, 105.f, 34.f), 60.f, 120, &blocked);
        EXPECT_GT(end.x, 150.f);
        EXPECT_FALSE(blocked);
        EXPECT_NEAR(end.z, 0.f, 0.5f);
    }

    TEST(NeutralMoverSteep, aGapWiderThanTheCapsuleIsNotCrossed)
    {
        const vsg::vec3 end = walkAcross(TrenchWorld(100.f, 160.f, 34.f), 60.f, 120);
        EXPECT_LT(end.x, 100.f);
    }

    TEST(NeutralMoverSteep, aLedgeWithNoGroundBeyondIsNotBridged)
    {
        const vsg::vec3 end = walkAcross(TrenchWorld(100.f, 105.f, 34.f, false), 60.f, 120);
        EXPECT_LT(end.x, 100.f);
    }

    // A capsule perched on the corner of a raised object: it rests 15 above the floor, over a trench 20 wide whose two
    // sides are floor at z = 0. The ground 12 ahead and behind is 19 below the resting capsule: that is no support at the
    // capsule's own floor, so it must not be bridged (Lucia hung by her rim over the cellar trench, task 0159).
    class PerchWorld final : public Sweeper
    {
    public:
        static float floorAt(float x) { return (x >= 100.f && x <= 120.f) ? -34.f : 0.f; }
        Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override
        {
            Sweep s;
            if (delta.z >= 0.f)
                return s;
            const float support = (from.x >= 95.f && from.x <= 125.f) ? 15.f : 0.f; // the raised object's corner
            const float t = (from.z - support) / -delta.z;
            if (t >= 0.f && t <= 1.f)
            {
                s.mHit = true;
                s.mFraction = t;
            }
            return s;
        }
        Sweep probeDown(const vsg::vec3& from, float depth) const override
        {
            Sweep s;
            const float t = (from.z - floorAt(from.x)) / depth;
            if (t >= 0.f && t <= 1.f)
            {
                s.mHit = true;
                s.mFraction = t;
            }
            return s;
        }
    };

    TEST(NeutralMoverSteep, aCapsuleHangingByARimOverATrenchIsNotBridged)
    {
        Request r;
        r.mVelX = 100.f;
        const Result res = step(vsg::vec3(110.f, 0.f, 15.f), r, 1.f / 60.f, PerchWorld());
        EXPECT_FALSE(res.mMoved);
        EXPECT_TRUE(res.mEdge);
    }
}
