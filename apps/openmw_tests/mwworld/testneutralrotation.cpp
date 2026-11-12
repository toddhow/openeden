#include <apps/openmw/mwworld/neutralrotation.hpp>

#include <gtest/gtest.h>

#include <cmath>

// Task 0148: a neutral actor that turns must stay a rigid, upright body: its rotation is a unit quaternion about Z.
namespace
{
    ESM::Position headed(float heading)
    {
        ESM::Position p;
        p.pos[0] = p.pos[1] = p.pos[2] = 0.f;
        p.rot[0] = p.rot[1] = 0.f;
        p.rot[2] = heading;
        return p;
    }

    double length(const vsg::dquat& q)
    {
        return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    }
}

TEST(NeutralRotation, a_heading_gives_a_unit_quaternion)
{
    for (float h : { 0.f, 0.098f, 0.846f, 1.5708f, -0.588f, -1.496f, 3.0f })
        EXPECT_NEAR(length(MWWorld::neutralActorQuaternion(headed(h))), 1.0, 1e-9) << h;
}

TEST(NeutralRotation, up_stays_up_and_the_turn_is_the_heading)
{
    for (float h : { 0.098f, 0.846f, 1.5708f, -0.588f, -1.496f, 2.5f })
    {
        const vsg::dquat q = MWWorld::neutralActorQuaternion(headed(h));
        const vsg::dvec3 up = q * vsg::dvec3(0, 0, 1);
        EXPECT_NEAR(up.x, 0.0, 1e-9) << h;
        EXPECT_NEAR(up.y, 0.0, 1e-9) << h;
        EXPECT_NEAR(up.z, 1.0, 1e-9) << h;
        const vsg::dvec3 side = q * vsg::dvec3(1, 0, 0);
        EXPECT_NEAR(side.x, std::cos(h), 1e-6) << h; // the angle between X and its image is the heading
        EXPECT_NEAR(std::abs(side.y), std::abs(std::sin(h)), 1e-6) << h;
        EXPECT_NEAR(side.z, 0.0, 1e-9) << h;
    }
}
