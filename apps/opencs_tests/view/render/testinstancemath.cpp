#include <gtest/gtest.h>

#include <cmath>

#include <vsg/maths/transform.h>

#include <apps/opencs/view/render/instancemath.hpp>

// Written from the OSG editor's instancemode.cpp before the port: quatToEuler, eulerToQuat, roundFloatToMult,
// calculateSnapPositionRelativeToTarget, getProjectionSpaceCoords, getMousePlaneCoords, getScreenCoords, and drag()'s
// group rotation. osg::Quat's a * b means "a, then b", and osg::Matrix uses row vectors.
namespace
{
    namespace Math = CSVRender::InstanceMath;

    constexpr float sTolerance = 1e-4f;

    void expectNear(const vsg::vec3& actual, const vsg::vec3& expected, float tolerance = sTolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }

    TEST(InstanceMathTest, eulerToQuatTurnsByTheNegativeAngle)
    {
        // osg::Quat(-euler[2], z) is what an ESM rotation means: a positive z turns clockwise seen from above.
        const float angle = 0.5f;
        const vsg::dvec3 turned = Math::eulerToQuat(vsg::vec3(0, 0, angle)) * vsg::dvec3(1, 0, 0);
        EXPECT_NEAR(turned.x, std::cos(angle), sTolerance);
        EXPECT_NEAR(turned.y, -std::sin(angle), sTolerance);
        EXPECT_NEAR(turned.z, 0, sTolerance);
    }

    TEST(InstanceMathTest, eulerToQuatAppliesZThenYThenX)
    {
        // The old zr * yr * xr: osg's order means z first, then y, then x.
        const vsg::dvec3 turned = Math::eulerToQuat(vsg::vec3(0.5f, 0, 0.5f)) * vsg::dvec3(1, 0, 0);
        // z alone gives (cos, -sin, 0); x then turns y about x by -0.5.
        const double c = std::cos(0.5);
        const double s = std::sin(0.5);
        EXPECT_NEAR(turned.x, c, sTolerance);
        EXPECT_NEAR(turned.y, -s * c, sTolerance);
        EXPECT_NEAR(turned.z, s * s, sTolerance);
    }

    TEST(InstanceMathTest, eulerRoundTrips)
    {
        for (const vsg::vec3 euler : { vsg::vec3(0, 0, 0), vsg::vec3(0.3f, 0, 0), vsg::vec3(0, 0.4f, 0),
                 vsg::vec3(0, 0, 1.2f), vsg::vec3(0.3f, -0.4f, 1.2f), vsg::vec3(-1.f, 0.2f, -2.f) })
            expectNear(Math::quatToEuler(Math::eulerToQuat(euler)), euler);
    }

    TEST(InstanceMathTest, quatToEulerAtTheGimbalLock)
    {
        // test >= 1: the old branch, y is -pi/2 after the sign flip and z is 0.
        const vsg::vec3 euler = Math::quatToEuler(Math::eulerToQuat(vsg::vec3(0, 1.5707964f, 0)));
        EXPECT_NEAR(euler.y, 1.5707964f, 1e-3f);
        EXPECT_FALSE(std::isnan(euler.x));
        EXPECT_FALSE(std::isnan(euler.z));
    }

    TEST(InstanceMathTest, roundToMultiple)
    {
        EXPECT_FLOAT_EQ(Math::roundToMult(17.f, 8.0), 16.f);
        EXPECT_FLOAT_EQ(Math::roundToMult(20.f, 8.0), 24.f);
        EXPECT_FLOAT_EQ(Math::roundToMult(-20.f, 8.0), -24.f);
        // A multiple of 0 leaves the value, as the old code guards the division.
        EXPECT_FLOAT_EQ(Math::roundToMult(17.3f, 0.0), 17.3f);
    }

    TEST(InstanceMathTest, snapRelativeToAnUnrotatedTargetSnapsTheOffset)
    {
        const vsg::vec3 result
            = Math::snapRelativeToTarget({ 103, 0, 0 }, { 100, 0, 0 }, { 0, 0, 0 }, { 10, 0, 0 }, 8.0);
        // Local x: 3 + 10 = 13, rounded to 16, so 116 in the world.
        expectNear(result, { 116, 0, 0 });
    }

    TEST(InstanceMathTest, snapRelativeToARotatedTargetUsesItsAxes)
    {
        // Turned a quarter of a turn about z by osg::Quat(pi/2, z), which takes the world's x to its y.
        const float quarter = 1.5707964f;
        const vsg::vec3 result = Math::snapRelativeToTarget({ 0, 3, 0 }, { 0, 0, 0 }, { 0, 0, quarter }, { 0, 0, 0 }, 8.0);
        // Local: (0, 3, 0) rotated to (-3, 0, 0), rounded to (0 or -8): -3 / 8 rounds to 0.
        expectNear(result, { 0, 0, 0 }, 1e-3f);
        const vsg::vec3 far = Math::snapRelativeToTarget({ 0, 5, 0 }, { 0, 0, 0 }, { 0, 0, quarter }, { 0, 0, 0 }, 8.0);
        // Local (-5, 0, 0) rounds to (-8, 0, 0), and the inverse rotation gives world (0, 8, 0).
        expectNear(far, { 0, 8, 0 }, 1e-3f);
    }

    TEST(InstanceMathTest, groupRotationTurnsAroundTheCentre)
    {
        const vsg::dquat rotation = Misc::VsgMath::makeRotateFromAxis(1.5707963267948966, vsg::dvec3(0, 0, 1));
        vsg::vec3 position{ 110, 0, 5 };
        vsg::vec3 euler{ 0, 0, 0 };
        Math::rotateAroundCentre(position, euler, { 100, 0, 5 }, rotation);
        expectNear(position, { 100, 10, 5 }, 1e-3f);
        // Turning by +90 degrees about z is -90 degrees in ESM's clockwise rotations.
        expectNear(euler, { 0, 0, -1.5707964f }, 1e-3f);
    }

    TEST(InstanceMathTest, mousePlanePointRoundTripsThroughTheProjection)
    {
        // VSG's perspective, whose picture has y going down and whose depth is reversed.
        const vsg::dmat4 projection = vsg::perspective(vsg::radians(60.0), 16.0 / 9.0, 1.0, 10000.0);
        const vsg::dmat4 view = vsg::lookAt(vsg::dvec3(0, -500, 200), vsg::dvec3(0, 0, 0), vsg::dvec3(0, 0, 1));
        const vsg::vec3 world{ 40, 30, 20 };

        const vsg::vec3 ndc = Math::projectionCoords(projection, view, world);
        // Where that point is in a window of 1280 by 720, as a fraction from the top left.
        const double x = (ndc.x + 1.0) / 2.0;
        const double y = (ndc.y + 1.0) / 2.0;
        expectNear(Math::mousePlaneCoords(projection, view, x, y, ndc.z), world, 1e-2f);
    }

    TEST(InstanceMathTest, mousePlaneTopOfTheWindowIsUpInTheWorld)
    {
        const vsg::dmat4 projection = vsg::perspective(vsg::radians(60.0), 1.0, 1.0, 10000.0);
        const vsg::dmat4 view = vsg::lookAt(vsg::dvec3(0, -500, 0), vsg::dvec3(0, 0, 0), vsg::dvec3(0, 0, 1));
        const float depth = Math::projectionCoords(projection, view, { 0, 0, 0 }).z;
        const vsg::vec3 top = Math::mousePlaneCoords(projection, view, 0.5, 0.25, depth);
        const vsg::vec3 bottom = Math::mousePlaneCoords(projection, view, 0.5, 0.75, depth);
        EXPECT_GT(top.z, 0.f);
        EXPECT_LT(bottom.z, 0.f);
        EXPECT_NEAR(top.x, 0.f, 1e-3f);
    }

    TEST(InstanceMathTest, windowCoordsCountYFromTheBottom)
    {
        // The old getScreenCoords went through OSG's window matrix, and drag() compares it with height - pos.y().
        const vsg::dmat4 projection = vsg::perspective(vsg::radians(60.0), 2.0, 1.0, 10000.0);
        const vsg::dmat4 view = vsg::lookAt(vsg::dvec3(0, -500, 0), vsg::dvec3(0, 0, 0), vsg::dvec3(0, 0, 1));
        const vsg::vec3 centre = Math::windowCoords(projection, view, { 0, 0, 0 }, 800, 400);
        EXPECT_NEAR(centre.x, 400.f, 1e-2f);
        EXPECT_NEAR(centre.y, 200.f, 1e-2f);
        const vsg::vec3 above = Math::windowCoords(projection, view, { 0, 0, 100 }, 800, 400);
        EXPECT_GT(above.y, 200.f);
        const vsg::vec3 right = Math::windowCoords(projection, view, { 100, 0, 0 }, 800, 400);
        EXPECT_GT(right.x, 400.f);
    }
}
