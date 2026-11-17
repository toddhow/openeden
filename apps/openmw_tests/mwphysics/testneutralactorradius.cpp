// Task 0246: the capsule of a neutral actor with a radius of its own (a mammoth is wider than a person, a wolf is
// slimmer). Written from the header before the implementation; the first run was against a stub (see the task's log).
#include <components/resource/physicsshape.hpp>

#include "apps/openmw/mwphysics/neutralactor.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace MWPhysics::NeutralActor;

    float totalHeight(const Capsule& c)
    {
        return 2.f * (c.mHalfCylinder + c.mRadius);
    }

    TEST(NeutralActorRadius, noRadiusMeansThePersonsRadiusAsBefore)
    {
        for (const float scale : { 1.f, 0.5f, 2.f })
        {
            const Capsule plain = capsuleFor(scale);
            const Capsule none = capsuleFor(scale, 0.f);
            EXPECT_FLOAT_EQ(none.mRadius, plain.mRadius);
            EXPECT_FLOAT_EQ(none.mHalfCylinder, plain.mHalfCylinder);
            EXPECT_FLOAT_EQ(none.mCentreZ, plain.mCentreZ);
        }
    }

    TEST(NeutralActorRadius, aRadiusIsUsedAndTheHeightStaysTheHeight)
    {
        const Capsule c = capsuleFor(376.f / kStandingHeight, 100.f); // a mammoth
        EXPECT_FLOAT_EQ(c.mRadius, 100.f);
        EXPECT_NEAR(totalHeight(c), 376.f, 1e-2f);
        EXPECT_NEAR(c.mCentreZ, 188.f, 1e-2f);
        EXPECT_GT(c.mHalfCylinder, 0.f);
    }

    TEST(NeutralActorRadius, aSlimRadiusIsUsedToo)
    {
        const Capsule c = capsuleFor(80.f / kStandingHeight, 16.f); // a wolf
        EXPECT_FLOAT_EQ(c.mRadius, 16.f);
        EXPECT_NEAR(totalHeight(c), 80.f, 1e-2f);
    }

    TEST(NeutralActorRadius, aRadiusWiderThanTheBodyIsCutSoTheCapsuleStaysACapsule)
    {
        const Capsule c = capsuleFor(98.f / kStandingHeight, 500.f);
        EXPECT_LE(c.mRadius, totalHeight(c) * kMaxRadiusFraction + 1e-3f);
        EXPECT_GT(c.mHalfCylinder, 0.f);
        EXPECT_NEAR(totalHeight(c), 98.f, 1e-2f);
    }

    TEST(NeutralActorRadius, aBadRadiusMeansThePersonsRadius)
    {
        const Capsule plain = capsuleFor(1.f);
        for (const float bad :
            { -5.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            const Capsule c = capsuleFor(1.f, bad);
            EXPECT_FLOAT_EQ(c.mRadius, plain.mRadius) << bad;
            EXPECT_FLOAT_EQ(c.mHalfCylinder, plain.mHalfCylinder) << bad;
        }
    }

    TEST(NeutralActorRadius, everyCreatureRadiusOfTheTableBuildsAShape)
    {
        struct Row
        {
            float mHeight;
            float mRadius;
        };
        const Row rows[] = { { 80.f, 16.f }, { 192.f, 68.f }, { 376.f, 100.f }, { 98.f, 44.f }, { 124.f, 46.f },
            { 57.f, 16.f }, { 145.f, 18.f } };
        for (const Row& row : rows)
        {
            const Capsule c = capsuleFor(row.mHeight / kStandingHeight, row.mRadius);
            const auto shape = makeShape(c);
            ASSERT_NE(shape, nullptr) << row.mHeight;
            EXPECT_FLOAT_EQ(c.mRadius, row.mRadius);
        }
    }
}
