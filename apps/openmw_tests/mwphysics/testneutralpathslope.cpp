// Task 0336: ground a navmesh path vouches for. The navmesh passes lumpy rock faces whose probed normal is a little
// steeper than the mover's limit (normal z 0.628 against 0.64: a Riverwood guard stopped 400 units short of a path).
// While a path is followed the limit is lower. Written before the change; the first run was against a stub.
#include "apps/openmw/mwphysics/neutralmover.hpp"
#include "apps/openmw/mwphysics/neutralpathfollower.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace MWPhysics::NeutralMover;
    namespace Path = MWPhysics::NeutralPath;

    // Free air; the capsule comes to rest where it started; the ground under the centre has normal z `normalZ`.
    class Face final : public Sweeper
    {
    public:
        explicit Face(float normalZ)
            : mNormalZ(normalZ)
        {
        }
        Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override
        {
            Sweep s;
            if (delta.z < 0.f)
            {
                const float t = from.z / -delta.z; // the capsule comes to rest at z = 0, level with where it started
                if (t >= 0.f && t <= 1.f)
                {
                    s.mHit = true;
                    s.mFraction = t;
                }
            }
            return s;
        }
        Sweep probeDown(const vsg::vec3& from, float depth) const override
        {
            Sweep s;
            s.mHit = true;
            s.mFraction = (from.z + 6.f) / depth; // the ground is 6 units below the resting capsule: more than kBevelSlack
            s.mNormal = vsg::vec3(0.8f, 0.f, mNormalZ);
            return s;
        }

    private:
        float mNormalZ;
    };

    Result walk(const Sweeper& world, const Limits& limits)
    {
        Request r;
        r.mVelX = 100.f;
        return step(vsg::vec3(0.f, 0.f, 0.f), r, 1.f / 60.f, world, limits);
    }
}

TEST(NeutralPathSlope, theFloorLimitOnAPathIsLowerThanTheBase)
{
    EXPECT_FLOAT_EQ(Path::pathMinFloorNormalZ(kMinFloorNormalZ), Path::kPathMinFloorNormalZ);
    EXPECT_LT(Path::kPathMinFloorNormalZ, kMinFloorNormalZ);
}

TEST(NeutralPathSlope, aBaseLimitThatIsAlreadyLowerIsKept)
{
    EXPECT_FLOAT_EQ(Path::pathMinFloorNormalZ(0.3f), 0.3f);
}

TEST(NeutralPathSlope, aNonFiniteBaseGivesTheBase)
{
    EXPECT_FALSE(std::isfinite(Path::pathMinFloorNormalZ(std::numeric_limits<float>::quiet_NaN())));
}

TEST(NeutralPathSlope, aFaceSteeperThanTheMoversLimitRefusesTheWalkAsBefore)
{
    const Result r = walk(Face(0.628f), Limits());
    EXPECT_TRUE(r.mBlocked);
    EXPECT_EQ(r.mRefusal, Result::Refusal::Slope);
    EXPECT_FALSE(r.mMoved);
}

TEST(NeutralPathSlope, theSameFaceIsWalkedWithThePathLimit)
{
    Limits limits;
    limits.mMinFloorNormalZ = Path::pathMinFloorNormalZ(limits.mMinFloorNormalZ);
    const Result r = walk(Face(0.628f), limits);
    EXPECT_TRUE(r.mMoved);
    EXPECT_FALSE(r.mBlocked);
}

TEST(NeutralPathSlope, aCliffIsStillRefusedWithThePathLimit)
{
    Limits limits;
    limits.mMinFloorNormalZ = Path::pathMinFloorNormalZ(limits.mMinFloorNormalZ);
    EXPECT_TRUE(walk(Face(0.2f), limits).mBlocked);
}
