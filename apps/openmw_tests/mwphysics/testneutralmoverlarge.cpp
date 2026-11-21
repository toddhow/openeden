// Task 0280 (dragons, second slice): a creature whose capsule is as wide as the dragon's (radius 150) has the long legs
// of a mount without anybody sitting on it: `limitsFor(height, radius, false)`. Found by running a dragon on the
// Whiterun plains (docs/tasks/0280-skyrim-dragons-2.md): its direct walk stopped at "blocked edge" after 440 units at a
// 47 unit drop, which the humanoid's 24 unit step down refuses; the navigator was not the cause. Written before the
// change (the first run was against `limitsFor` as it was: ridden only). A rule on the height alone would also change
// the mammoth and the giant and fails the pinned `NeutralMoverLimits.onlyTheRiddenActorGetsTheLimitsOfAMount`.
// Same exact stand-in world of boxes as testneutralmoverlimits.cpp.
#include "apps/openmw/mwphysics/neutralmover.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace
{
    using namespace MWPhysics::NeutralMover;

    constexpr float kDt = 1.f / 60.f;
    constexpr float kHalf = 22.f;
    constexpr float kBodyHeight = 128.f;
    constexpr float kDragonHeight = 414.f; // creatures.cpp, the dragon row
    constexpr float kMammothHeight = 376.2f;
    constexpr float kBearHeight = 191.8f;
    constexpr float kHorseHeight = 169.f;
    constexpr float kDragonRadius = 150.f; // the profile's capsule radius (creatures.cpp)
    constexpr float kMammothRadius = 100.f;

    struct Box
    {
        float mMin[3];
        float mMax[3];
    };

    class World final : public Sweeper
    {
    public:
        std::vector<Box> mBoxes;
        void add(float x0, float y0, float z0, float x1, float y1, float z1)
        {
            mBoxes.push_back({ { x0, y0, z0 }, { x1, y1, z1 } });
        }
        Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override
        {
            return cast(from, delta, kHalf, kBodyHeight);
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
                        if (!(start[a] > lo[a] && start[a] < hi[a]))
                            miss = true;
                        continue;
                    }
                    float t1 = (lo[a] - start[a]) / d[a];
                    float t2 = (hi[a] - start[a]) / d[a];
                    float sign = -1.f;
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

    vsg::vec3 walk(const World& w, const Limits& limits, vsg::vec3 pos, float vx, int frames, bool* blocked = nullptr)
    {
        Request r;
        r.mVelX = vx;
        for (int i = 0; i < frames; ++i)
        {
            const Result res = step(pos, r, kDt, w, limits);
            pos = res.mFeet;
            if (blocked != nullptr)
                *blocked = *blocked || res.mBlocked;
        }
        return pos;
    }

    // A floor at z = 0 ending at x = 100 and a bank `drop` units lower beyond it.
    World bank(float drop)
    {
        World w;
        w.add(-5000, -5000, -400, 100, 5000, 0);
        w.add(100, -5000, -400, 5000, 5000, -drop);
        return w;
    }

    TEST(NeutralMoverLarge, theLargeCreatureRadiusIsBetweenTheMammothAndTheDragon)
    {
        EXPECT_GT(kLargeCreatureRadius, kMammothRadius);
        EXPECT_LE(kLargeCreatureRadius, kDragonRadius);
    }

    TEST(NeutralMoverLarge, aCapsuleAsWideAsTheDragonsHasTheMountsLimitsWithNobodyRidingIt)
    {
        const Limits large = limitsFor(kDragonHeight, kDragonRadius, false);
        const Limits mount = limitsForHeight(kDragonHeight);
        EXPECT_FLOAT_EQ(large.mStepUp, mount.mStepUp);
        EXPECT_FLOAT_EQ(large.mStepDown, mount.mStepDown);
        EXPECT_FLOAT_EQ(large.mMinFloorNormalZ, kLargeCreatureMinFloorNormalZ)
            << "a rim on a ridge: the centre may be on a steep face";
        EXPECT_LT(large.mMinFloorNormalZ, mount.mMinFloorNormalZ);
        EXPECT_GT(large.mStepUp, 70.f);
        EXPECT_NEAR(large.mStepDown, kDragonHeight / 2.f, 1e-3f);
    }

    TEST(NeutralMoverLarge, theBoundaryIsTheLargeCreatureRadiusItselfAndNothingBelowIt)
    {
        const Limits plain;
        const Limits below = limitsFor(kDragonHeight, std::nextafter(kLargeCreatureRadius, 0.f), false);
        EXPECT_FLOAT_EQ(below.mStepUp, plain.mStepUp);
        EXPECT_FLOAT_EQ(below.mStepDown, plain.mStepDown);
        EXPECT_FLOAT_EQ(below.mMinFloorNormalZ, plain.mMinFloorNormalZ);
        const Limits at = limitsFor(kDragonHeight, kLargeCreatureRadius, false);
        EXPECT_GT(at.mStepDown, plain.mStepDown);
    }

    TEST(NeutralMoverLarge, aMammothABearAHorseAndAPersonKeepTheHumanoidLimitsUnlessRidden)
    {
        const Limits plain;
        const std::pair<float, float> sizes[] = { { kBodyHeight, 22.f }, { kBearHeight, 68.f }, { kHorseHeight, 32.f },
            { kMammothHeight, kMammothRadius } };
        for (const auto& [height, radius] : sizes)
        {
            const Limits limits = limitsFor(height, radius, false);
            EXPECT_FLOAT_EQ(limits.mStepUp, plain.mStepUp) << height;
            EXPECT_FLOAT_EQ(limits.mStepDown, plain.mStepDown) << height;
            EXPECT_FLOAT_EQ(limits.mMinFloorNormalZ, plain.mMinFloorNormalZ) << height;
        }
        EXPECT_FLOAT_EQ(limitsFor(kHorseHeight, 32.f, true).mStepDown, limitsForHeight(kHorseHeight).mStepDown);
    }

    TEST(NeutralMoverLarge, aRadiusThatIsNotANumberIsNoRadiusAndTheTwoArgumentFormIsUnchanged)
    {
        const Limits plain;
        for (const float radius : { std::numeric_limits<float>::quiet_NaN(), -150.f, 0.f })
        {
            EXPECT_FLOAT_EQ(limitsFor(kDragonHeight, radius, false).mStepDown, plain.mStepDown);
            EXPECT_FLOAT_EQ(limitsFor(kDragonHeight, radius, false).mStepUp, plain.mStepUp);
        }
        // task 0255's form: only a ridden actor, whatever its height
        EXPECT_FLOAT_EQ(limitsFor(kDragonHeight, false).mStepDown, plain.mStepDown);
        EXPECT_FLOAT_EQ(limitsFor(kDragonHeight, true).mStepDown, limitsForHeight(kDragonHeight).mStepDown);
    }

    TEST(NeutralMoverLarge, aDropOfOneHundredFiftyUnitsStopsAPersonAndNotADragon)
    {
        const World w = bank(150.f);
        bool personBlocked = false;
        const vsg::vec3 person
            = walk(w, limitsFor(kBodyHeight, 22.f, false), vsg::vec3(0, 0, 0), 150.f, 120, &personBlocked);
        EXPECT_TRUE(personBlocked);
        EXPECT_LT(person.x, 101.f);

        bool dragonBlocked = false;
        const vsg::vec3 dragon
            = walk(w, limitsFor(kDragonHeight, kDragonRadius, false), vsg::vec3(0, 0, 0), 150.f, 120, &dragonBlocked);
        EXPECT_FALSE(dragonBlocked);
        EXPECT_NEAR(dragon.z, -150.f, 0.01f);
        EXPECT_GT(dragon.x, 200.f);
    }

    TEST(NeutralMoverLarge, aDropDeeperThanHalfTheDragonsHeightStillStopsIt)
    {
        const World w = bank(250.f);
        bool blocked = false;
        const vsg::vec3 dragon
            = walk(w, limitsFor(kDragonHeight, kDragonRadius, false), vsg::vec3(0, 0, 0), 150.f, 120, &blocked);
        EXPECT_TRUE(blocked);
        EXPECT_NEAR(dragon.z, 0.f, 0.01f);
    }

    TEST(NeutralMoverLarge, aRiserOfSixtyUnitsStopsAPersonAndNotADragon)
    {
        World w;
        w.add(-5000, -5000, -400, 5000, 5000, 0);
        w.add(100, -5000, -400, 5000, 5000, 60.f);
        bool personBlocked = false;
        (void)walk(w, limitsFor(kBodyHeight, 22.f, false), vsg::vec3(0, 0, 0), 150.f, 120, &personBlocked);
        EXPECT_TRUE(personBlocked);
        const vsg::vec3 dragon
            = walk(w, limitsFor(kDragonHeight, kDragonRadius, false), vsg::vec3(0, 0, 0), 150.f, 120);
        EXPECT_NEAR(dragon.z, 60.f, 0.01f);
        EXPECT_GT(dragon.x, 200.f);
    }

    // ---- what a large creature walks over ---------------------------------------------------------------------------

    Limits large()
    {
        return limitsFor(kDragonHeight, kDragonRadius, false);
    }

    TEST(NeutralMoverTrample, aLargeCreatureTramplesWhatIsLowAndSmallAndNothingElse)
    {
        const Limits l = large();
        EXPECT_FLOAT_EQ(l.mTrampleHeight, kLargeCreatureTrampleHeight);
        EXPECT_FLOAT_EQ(l.mTrampleWidth, kLargeCreatureTrampleWidth);
        const float feet = 1000.f;
        EXPECT_TRUE(tramplesBody(l, feet, feet + 100.f, 80.f, 60.f)) << "a rock";
        EXPECT_TRUE(tramplesBody(l, feet, feet + kLargeCreatureTrampleHeight, kLargeCreatureTrampleWidth, 10.f))
            << "the limits themselves are inside";
        EXPECT_FALSE(tramplesBody(l, feet, feet + 600.f, 80.f, 80.f)) << "a tall tree";
        EXPECT_FALSE(tramplesBody(l, feet, feet + 100.f, 800.f, 30.f)) << "a long wall";
        EXPECT_FALSE(tramplesBody(l, feet, feet + 100.f, 30.f, 800.f)) << "a long wall the other way";
        EXPECT_FALSE(tramplesBody(l, feet, feet + kLargeCreatureTrampleHeight + 1.f, 80.f, 80.f));
        EXPECT_FALSE(tramplesBody(l, feet, feet + 100.f, kLargeCreatureTrampleWidth + 1.f, 80.f));
    }

    TEST(NeutralMoverTrample, aPersonAndAMountTrampleNothing)
    {
        for (const Limits& l : { limitsFor(kBodyHeight, 22.f, false), limitsFor(kHorseHeight, 32.f, true), Limits() })
        {
            EXPECT_FLOAT_EQ(l.mTrampleHeight, 0.f);
            EXPECT_FLOAT_EQ(l.mTrampleWidth, 0.f);
            EXPECT_FALSE(tramplesBody(l, 0.f, 10.f, 10.f, 10.f));
        }
    }

    TEST(NeutralMoverTrample, aNumberThatIsNotFiniteTramplesNothing)
    {
        const Limits l = large();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(tramplesBody(l, nan, 10.f, 10.f, 10.f));
        EXPECT_FALSE(tramplesBody(l, 0.f, nan, 10.f, 10.f));
        EXPECT_FALSE(tramplesBody(l, 0.f, 10.f, nan, 10.f));
        EXPECT_FALSE(tramplesBody(l, 0.f, 10.f, 10.f, std::numeric_limits<float>::infinity()));
    }
}
