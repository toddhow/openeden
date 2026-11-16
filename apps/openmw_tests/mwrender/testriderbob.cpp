// Task 0255: the rider's body follows the saddle bone's animated position (apps/openmw/mwrender/riderbob.hpp). Written
// before the code (the first run was against a stub that returned no displacement).
#include "apps/openmw/mwrender/riderbob.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using MWRender::kMaxRiderBob;
    using MWRender::riderBobOffset;

    vsg::mat3 identity()
    {
        vsg::mat3 m;
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                m[c][r] = c == r ? 1.f : 0.f;
        return m;
    }

    // A turn of `angle` radians about Z: column 0 (the horse's right) goes to (cos, sin, 0), column 1 (forward) to
    // (-sin, cos, 0).
    vsg::mat3 about(float angle)
    {
        vsg::mat3 m = identity();
        m[0][0] = std::cos(angle);
        m[0][1] = std::sin(angle);
        m[1][0] = -std::sin(angle);
        m[1][1] = std::cos(angle);
        return m;
    }

    TEST(RiderBob, theBoneAtRestGivesNoDisplacement)
    {
        const vsg::vec3 rest(1.67f, -0.98f, 134.92f);
        const vsg::vec3 d = riderBobOffset(rest, rest, identity());
        EXPECT_EQ(d.x, 0.f);
        EXPECT_EQ(d.y, 0.f);
        EXPECT_EQ(d.z, 0.f);
    }

    TEST(RiderBob, aBoneThatRoseMovesTheBodyUpByTheSameAmount)
    {
        const vsg::vec3 rest(0.f, 0.f, 100.f);
        const vsg::vec3 d = riderBobOffset(vsg::vec3(0.f, 0.f, 106.f), rest, identity());
        EXPECT_NEAR(d.z, 6.f, 1e-5f);
        EXPECT_NEAR(d.x, 0.f, 1e-5f);
    }

    TEST(RiderBob, theDisplacementIsTurnedWithTheHorse)
    {
        const vsg::vec3 rest(0.f, 0.f, 100.f);
        // the saddle moved 10 units forward in the horse's frame; the horse faces +X (a quarter turn clockwise: forward
        // maps to (1, 0)): the column of forward is (1, 0, 0).
        vsg::mat3 m = identity();
        m[0][0] = 0.f;
        m[0][1] = -1.f;
        m[1][0] = 1.f;
        m[1][1] = 0.f;
        const vsg::vec3 d = riderBobOffset(vsg::vec3(0.f, 10.f, 100.f), rest, m);
        EXPECT_NEAR(d.x, 10.f, 1e-4f);
        EXPECT_NEAR(d.y, 0.f, 1e-4f);
        const vsg::vec3 e = riderBobOffset(vsg::vec3(0.f, 10.f, 100.f), rest, about(0.f));
        EXPECT_NEAR(e.y, 10.f, 1e-4f);
    }

    TEST(RiderBob, aDisplacementBeyondTheLimitIsCutToIt)
    {
        const vsg::vec3 rest(0.f, 0.f, 100.f);
        const vsg::vec3 d = riderBobOffset(vsg::vec3(0.f, 0.f, 500.f), rest, identity());
        EXPECT_NEAR(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), kMaxRiderBob, 1e-3f);
        EXPECT_GT(d.z, 0.f);
    }

    TEST(RiderBob, anInputThatIsNotFiniteGivesNoDisplacement)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const vsg::vec3 rest(0.f, 0.f, 100.f);
        for (const vsg::vec3& bone : { vsg::vec3(nan, 0.f, 100.f), vsg::vec3(0.f, nan, 101.f),
                 vsg::vec3(0.f, 0.f, std::numeric_limits<float>::infinity()) })
        {
            const vsg::vec3 d = riderBobOffset(bone, rest, identity());
            EXPECT_EQ(d.x, 0.f);
            EXPECT_EQ(d.y, 0.f);
            EXPECT_EQ(d.z, 0.f);
        }
        vsg::mat3 bad = identity();
        bad[1][1] = nan;
        const vsg::vec3 d = riderBobOffset(vsg::vec3(0.f, 5.f, 100.f), rest, bad);
        EXPECT_EQ(d.y, 0.f);
        EXPECT_EQ(d.z, 0.f);
    }
}
