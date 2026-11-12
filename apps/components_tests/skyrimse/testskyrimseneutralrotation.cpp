// Task 0151: the ORDER of Skyrim's three stored placement angles. The floating MountainTrim01_LightSN slab
// (skyrimse:b197124 at Tamriel 104403,18631) was placed with R = Rz * Ry * Rx; every object with a tilt and a yaw tipped
// the wrong way. Skyrim turns a vector about Z first (R = Rx * Ry * Rz), every angle negated. Hand-built values only.
#include <game_skyrimse/neutral_world.hpp>

#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace
{
    using namespace game_skyrimse;
    namespace sdkgame = openeden::sdk::game;

    using Vec = std::array<double, 3>;

    // The active rotation q v q^-1.
    Vec rotate(const sdkgame::Quatd& q, const Vec& v)
    {
        const Vec u{ q.x, q.y, q.z };
        const auto cross = [](const Vec& a, const Vec& b) {
            return Vec{ a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
        };
        const Vec t0 = cross(u, v);
        const Vec t{ 2 * t0[0], 2 * t0[1], 2 * t0[2] };
        const Vec c = cross(u, t);
        return { v[0] + q.w * t[0] + c[0], v[1] + q.w * t[1] + c[1], v[2] + q.w * t[2] + c[2] };
    }

    // Independent reference: turn about Z by -z, then about Y by -y, then about X by -x (world axes, column vectors).
    Vec reference(double x, double y, double z, Vec v)
    {
        const auto rx = [](double a, const Vec& p) {
            return Vec{ p[0], std::cos(a) * p[1] - std::sin(a) * p[2], std::sin(a) * p[1] + std::cos(a) * p[2] };
        };
        const auto ry = [](double a, const Vec& p) {
            return Vec{ std::cos(a) * p[0] + std::sin(a) * p[2], p[1], -std::sin(a) * p[0] + std::cos(a) * p[2] };
        };
        const auto rz = [](double a, const Vec& p) {
            return Vec{ std::cos(a) * p[0] - std::sin(a) * p[1], std::sin(a) * p[0] + std::cos(a) * p[1], p[2] };
        };
        return rx(-x, ry(-y, rz(-z, v)));
    }

    void expectNear(const Vec& a, const Vec& b, double tolerance)
    {
        for (int i = 0; i < 3; ++i)
            EXPECT_NEAR(a[i], b[i], tolerance) << "component " << i;
    }

    TEST(SkyrimNeutralRotationOrder, aVectorIsTurnedAboutZFirstThenYThenX)
    {
        const float angles[] = { 0.0f, 0.4f, -1.1f, 2.9f };
        for (float x : angles)
            for (float y : angles)
                for (float z : angles)
                {
                    const sdkgame::Quatd q = eulerToQuat({ x, y, z });
                    for (const Vec v : { Vec{ 1, 0, 0 }, Vec{ 0, 1, 0 }, Vec{ 0, 0, 1 }, Vec{ 3, -4, 5 } })
                        expectNear(rotate(q, v), reference(x, y, z, v), 1e-9);
                }
    }

    TEST(SkyrimNeutralRotationOrder, theUpAxisOfATiltedObjectDoesNotDependOnItsYaw)
    {
        // A placement that follows a slope has a free yaw; the tilt is about the world axes, so where the object's up
        // points must not change with the yaw.
        const sdkgame::Quatd noYaw = eulerToQuat({ 0.073f, 0.382f, 0.0f });
        const Vec expected = rotate(noYaw, { 0, 0, 1 });
        for (float z : { 0.5f, 1.7f, 2.943f, -2.0f })
            expectNear(rotate(eulerToQuat({ 0.073f, 0.382f, z }), { 0, 0, 1 }), expected, 1e-6);
    }

    TEST(SkyrimNeutralRotationOrder, theMountainTrimPlacementTipsItsUpAxisDownhill)
    {
        // skyrimse:b197124 (Skyrim.esm 0x000A7D22): angles 0.073, 0.382, 2.943 on land that rises to the east and
        // falls to the north (heights round 104403,18631: +0.44 per unit east, -0.16 per unit north).
        // A positive Y angle tips up toward -X, a positive X angle toward +Y (task 0086), whatever the yaw: the up axis
        // leans west (x < 0) and north (y > 0), as the land's normal does. With the old order the yaw of 2.943 turned
        // the lean to the east.
        const Vec up = rotate(eulerToQuat({ 0.073f, 0.382f, 2.943f }), { 0, 0, 1 });
        EXPECT_LT(up[0], -0.3);
        EXPECT_GT(up[1], 0.0);
        EXPECT_GT(up[2], 0.85);
    }
}
