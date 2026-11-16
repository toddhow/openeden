// Task 0255 (horse riding, second slice): the climb and drop limits of a neutral actor follow its capsule height, so a
// horse steps over a kerb and down a bank that stop a person. Written from the header before the implementation (the
// first run was against a stub that ignored the limits: see docs/tasks/0255-skyrim-horse-riding-2.md). The stepping is
// tested in a world of boxes (the same exact stand-in as testneutralmover.cpp).
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
    constexpr float kHeight = 128.f;
    constexpr float kHorseHeight = 169.f; // creatures.cpp, the horse row

    struct LimitsBox
    {
        float mMin[3];
        float mMax[3];
    };

    class LimitsWorld final : public Sweeper
    {
    public:
        std::vector<LimitsBox> mBoxes;

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
            for (const LimitsBox& b : mBoxes)
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

    vsg::vec3 walkWith(
        const LimitsWorld& w, const Limits& limits, vsg::vec3 pos, float vx, int frames, bool* blocked = nullptr)
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

    // A floor at z = 0 that ends at x = 100 and a bank `drop` units lower beyond it.
    LimitsWorld bank(float drop)
    {
        LimitsWorld w;
        w.add(-5000, -5000, -300, 100, 5000, 0);
        w.add(100, -5000, -300, 5000, 5000, -drop);
        return w;
    }

    TEST(NeutralMoverLimits, a_capsule_no_taller_than_a_person_keeps_the_humanoid_limits)
    {
        for (const float height : { 128.f, 100.f, 40.f })
        {
            const Limits limits = limitsForHeight(height);
            EXPECT_FLOAT_EQ(limits.mStepUp, kStepUp) << height;
            EXPECT_FLOAT_EQ(limits.mStepDown, kStepDown) << height;
        }
    }

    TEST(NeutralMoverLimits, a_height_that_is_not_a_number_or_not_positive_keeps_the_humanoid_limits)
    {
        for (const float height : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                 0.f, -5.f })
        {
            const Limits limits = limitsForHeight(height);
            EXPECT_FLOAT_EQ(limits.mStepUp, kStepUp);
            EXPECT_FLOAT_EQ(limits.mStepDown, kStepDown);
        }
    }

    TEST(NeutralMoverLimits, the_horse_steps_up_a_fifth_of_its_height_and_down_half_of_it)
    {
        const Limits limits = limitsForHeight(kHorseHeight);
        EXPECT_NEAR(limits.mStepUp, kHorseHeight * kStepUp / kReferenceHeight, 1e-3f); // 31.7
        EXPECT_NEAR(limits.mStepDown, kHorseHeight / 2.f, 1e-3f); // 84.5
    }

    TEST(NeutralMoverLimits, a_taller_animal_never_gets_less_than_a_shorter_one)
    {
        Limits previous = limitsForHeight(128.f);
        for (float height = 130.f; height <= 400.f; height += 10.f)
        {
            const Limits limits = limitsForHeight(height);
            EXPECT_GE(limits.mStepUp, previous.mStepUp) << height;
            EXPECT_GE(limits.mStepDown, previous.mStepDown) << height;
            previous = limits;
        }
    }

    TEST(NeutralMoverLimits, the_plain_step_is_the_step_with_the_humanoid_limits)
    {
        const LimitsWorld w = bank(20.f);
        Request r;
        r.mVelX = 100.f;
        vsg::vec3 a(0, 0, 0);
        vsg::vec3 b(0, 0, 0);
        for (int i = 0; i < 120; ++i)
        {
            a = step(a, r, kDt, w).mFeet;
            b = step(b, r, kDt, w, Limits()).mFeet;
        }
        EXPECT_EQ(a.x, b.x);
        EXPECT_EQ(a.y, b.y);
        EXPECT_EQ(a.z, b.z);
    }

    TEST(NeutralMoverLimits, a_bank_of_fifty_units_stops_a_person_and_not_a_horse)
    {
        const LimitsWorld w = bank(50.f);
        bool personBlocked = false;
        const vsg::vec3 person = walkWith(w, limitsForHeight(kHeight), vsg::vec3(0, 0, 0), 150.f, 120, &personBlocked);
        EXPECT_TRUE(personBlocked);
        EXPECT_NEAR(person.z, 0.f, 0.01f);
        EXPECT_LT(person.x, 101.f);

        bool horseBlocked = false;
        const vsg::vec3 horse
            = walkWith(w, limitsForHeight(kHorseHeight), vsg::vec3(0, 0, 0), 150.f, 120, &horseBlocked);
        EXPECT_FALSE(horseBlocked);
        EXPECT_NEAR(horse.z, -50.f, 0.01f);
        EXPECT_GT(horse.x, 200.f);
    }

    TEST(NeutralMoverLimits, a_bank_deeper_than_half_the_horses_height_still_stops_it)
    {
        const LimitsWorld w = bank(120.f);
        bool blocked = false;
        const vsg::vec3 horse = walkWith(w, limitsForHeight(kHorseHeight), vsg::vec3(0, 0, 0), 150.f, 120, &blocked);
        EXPECT_TRUE(blocked);
        EXPECT_NEAR(horse.z, 0.f, 0.01f);
        EXPECT_LT(horse.x, 101.f);
    }

    TEST(NeutralMoverLimits, a_riser_of_thirty_units_stops_a_person_and_not_a_horse)
    {
        LimitsWorld w;
        w.add(-5000, -5000, -300, 5000, 5000, 0);
        w.add(100, -5000, -300, 5000, 5000, 30.f);
        bool personBlocked = false;
        const vsg::vec3 person = walkWith(w, limitsForHeight(kHeight), vsg::vec3(0, 0, 0), 150.f, 120, &personBlocked);
        EXPECT_TRUE(personBlocked);
        EXPECT_NEAR(person.z, 0.f, 0.01f);

        const vsg::vec3 horse = walkWith(w, limitsForHeight(kHorseHeight), vsg::vec3(0, 0, 0), 150.f, 120);
        EXPECT_NEAR(horse.z, 30.f, 0.01f);
        EXPECT_GT(horse.x, 200.f);
    }

    TEST(NeutralMoverLimits, a_riser_above_the_horses_step_stops_it)
    {
        LimitsWorld w;
        w.add(-5000, -5000, -300, 5000, 5000, 0);
        w.add(100, -5000, -300, 5000, 5000, 50.f);
        bool blocked = false;
        const vsg::vec3 horse = walkWith(w, limitsForHeight(kHorseHeight), vsg::vec3(0, 0, 0), 150.f, 120, &blocked);
        EXPECT_TRUE(blocked);
        EXPECT_NEAR(horse.z, 0.f, 0.01f);
    }

    // Free air; the capsule's rim rests at z = 0 and the ground under its centre is 10 units lower with a slope of the
    // given normal (the shape of a hillside the horse's rim is on).
    class HillSide final : public Sweeper
    {
    public:
        explicit HillSide(float normalZ)
            : mNormalZ(normalZ)
        {
        }
        Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override
        {
            Sweep s;
            if (delta.z < 0.f)
            {
                const float t = from.z / -delta.z;
                if (t >= 0.f && t <= 1.f)
                {
                    s.mHit = true;
                    s.mFraction = t;
                    s.mNormal = vsg::vec3(0.f, -0.8f, mNormalZ);
                }
            }
            return s;
        }
        Sweep probeDown(const vsg::vec3& from, float depth) const override
        {
            Sweep s;
            const float t = (from.z + 10.f) / depth;
            if (t >= 0.f && t <= 1.f)
            {
                s.mHit = true;
                s.mFraction = t;
                s.mNormal = vsg::vec3(0.8f, 0.f, mNormalZ);
            }
            return s;
        }

    private:
        float mNormalZ;
    };

    Result walkHill(float normalZ, const Limits& limits)
    {
        Request r;
        r.mVelX = 100.f;
        return step(vsg::vec3(0.f, 0.f, 0.f), r, kDt, HillSide(normalZ), limits);
    }

    TEST(NeutralMoverLimits, ground_at_fifty_three_degrees_stops_a_person_and_not_a_horse)
    {
        // normal z 0.6 is 53 degrees from level: above the person's 50 degrees, below the animal's 60.
        const Result person = walkHill(0.6f, limitsForHeight(kHeight));
        EXPECT_FALSE(person.mMoved);
        EXPECT_TRUE(person.mBlocked);
        const Result horse = walkHill(0.6f, limitsForHeight(kHorseHeight));
        EXPECT_TRUE(horse.mMoved);
        EXPECT_FALSE(horse.mBlocked);
    }

    TEST(NeutralMoverLimits, ground_at_sixty_five_degrees_stops_a_horse)
    {
        const Result horse = walkHill(0.42f, limitsForHeight(kHorseHeight));
        EXPECT_FALSE(horse.mMoved);
        EXPECT_TRUE(horse.mBlocked);
    }

    TEST(NeutralMoverLimits, the_floor_normal_limit_follows_the_height_too)
    {
        EXPECT_FLOAT_EQ(limitsForHeight(kHeight).mMinFloorNormalZ, kMinFloorNormalZ);
        EXPECT_FLOAT_EQ(limitsForHeight(kHorseHeight).mMinFloorNormalZ, kAnimalMinFloorNormalZ);
        EXPECT_LT(kAnimalMinFloorNormalZ, kMinFloorNormalZ);
    }

    TEST(NeutralMoverLimits, onlyTheRiddenActorGetsTheLimitsOfAMount)
    {
        const Limits ridden = limitsFor(kHorseHeight, true);
        EXPECT_NEAR(ridden.mStepDown, kHorseHeight / 2.f, 1e-3f);
        EXPECT_FLOAT_EQ(ridden.mMinFloorNormalZ, kAnimalMinFloorNormalZ);
        // an animal or a person nobody sits on keeps the humanoid's, whatever its height
        for (const float height : { 40.f, 128.f, 147.f, kHorseHeight, 376.f })
        {
            const Limits free = limitsFor(height, false);
            EXPECT_FLOAT_EQ(free.mStepUp, kStepUp) << height;
            EXPECT_FLOAT_EQ(free.mStepDown, kStepDown) << height;
            EXPECT_FLOAT_EQ(free.mMinFloorNormalZ, kMinFloorNormalZ) << height;
        }
        // a ridden capsule no taller than a person has the humanoid's too
        EXPECT_FLOAT_EQ(limitsFor(100.f, true).mStepDown, kStepDown);
    }
}
