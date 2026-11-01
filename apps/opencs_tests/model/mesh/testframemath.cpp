#include "apps/opencs/model/mesh/framemath.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace CSMMesh
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        using Vec3 = std::array<double, 3>;

        Aabb makeBox(const Vec3& lo, const Vec3& hi)
        {
            Aabb box;
            box.min = lo;
            box.max = hi;
            box.valid = true;
            return box;
        }

        void expectVec(const Vec3& actual, const Vec3& expected, double tolerance = 1e-9)
        {
            EXPECT_NEAR(actual[0], expected[0], tolerance);
            EXPECT_NEAR(actual[1], expected[1], tolerance);
            EXPECT_NEAR(actual[2], expected[2], tolerance);
        }

        double distanceBetween(const Vec3& a, const Vec3& b)
        {
            return std::sqrt(
                (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
        }

        Vec3 normalized(const Vec3& v)
        {
            const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            return { v[0] / length, v[1] / length, v[2] / length };
        }

        double expectedDistance(double radiusValue, double fovYDegrees, double aspect, double margin)
        {
            const double fovY = fovYDegrees * kPi / 180.0;
            const double fovX = 2.0 * std::atan(std::tan(fovY / 2.0) * aspect);
            return margin * radiusValue / std::sin(std::min(fovY, fovX) / 2.0);
        }

        TEST(CSMMeshFrameMathTest, defaultBoxIsInvalid)
        {
            const Aabb box;
            EXPECT_FALSE(box.valid);
        }

        TEST(CSMMeshFrameMathTest, expandingAnInvalidBoxMakesAPointBox)
        {
            Aabb box;
            expand(box, { 1.0, 2.0, 3.0 });
            EXPECT_TRUE(box.valid);
            expectVec(box.min, { 1.0, 2.0, 3.0 });
            expectVec(box.max, { 1.0, 2.0, 3.0 });
        }

        TEST(CSMMeshFrameMathTest, expandGrowsTheBoxToIncludeEachPoint)
        {
            Aabb box;
            expand(box, { 0.0, 0.0, 0.0 });
            expand(box, { 2.0, -1.0, 5.0 });
            expand(box, { 1.0, 1.0, 1.0 }); // inside, changes nothing
            expectVec(box.min, { 0.0, -1.0, 0.0 });
            expectVec(box.max, { 2.0, 1.0, 5.0 });
            EXPECT_TRUE(box.valid);
        }

        TEST(CSMMeshFrameMathTest, mergedIgnoresInvalidBoxes)
        {
            const Aabb valid = makeBox({ 0, 0, 0 }, { 1, 1, 1 });
            const Aabb invalid;

            const Aabb a = merged(valid, invalid);
            EXPECT_TRUE(a.valid);
            expectVec(a.min, { 0, 0, 0 });
            expectVec(a.max, { 1, 1, 1 });

            const Aabb b = merged(invalid, valid);
            EXPECT_TRUE(b.valid);
            expectVec(b.min, { 0, 0, 0 });
            expectVec(b.max, { 1, 1, 1 });

            EXPECT_FALSE(merged(invalid, invalid).valid);
        }

        TEST(CSMMeshFrameMathTest, mergedCoversBothBoxes)
        {
            const Aabb a = makeBox({ 0, 0, 0 }, { 1, 1, 1 });
            const Aabb b = makeBox({ -2, 0.5, 0.5 }, { 0.5, 3, 0.75 });
            const Aabb m = merged(a, b);
            EXPECT_TRUE(m.valid);
            expectVec(m.min, { -2, 0, 0 });
            expectVec(m.max, { 1, 3, 1 });
        }

        TEST(CSMMeshFrameMathTest, transformedByIdentityKeepsTheBox)
        {
            const std::array<double, 16> identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            const Aabb box = makeBox({ -1, -2, -3 }, { 4, 5, 6 });
            const Aabb t = transformed(box, identity);
            EXPECT_TRUE(t.valid);
            expectVec(t.min, box.min);
            expectVec(t.max, box.max);
        }

        TEST(CSMMeshFrameMathTest, transformedUsesColumnMajorTranslation)
        {
            // translation lives in elements 12, 13, 14
            const std::array<double, 16> move{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1 };
            const Aabb t = transformed(makeBox({ 0, 0, 0 }, { 1, 1, 1 }), move);
            expectVec(t.min, { 10, 20, 30 });
            expectVec(t.max, { 11, 21, 31 });
        }

        TEST(CSMMeshFrameMathTest, transformedTakesTheBoundsOfAllEightRotatedCorners)
        {
            // a quarter turn about Z: (x, y, z) -> (-y, x, z); columns are (0,1,0), (-1,0,0), (0,0,1)
            const std::array<double, 16> quarterTurn{ 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            const Aabb t = transformed(makeBox({ 0, 0, 0 }, { 2, 1, 1 }), quarterTurn);
            EXPECT_TRUE(t.valid);
            expectVec(t.min, { -1, 0, 0 });
            expectVec(t.max, { 0, 2, 1 });
        }

        TEST(CSMMeshFrameMathTest, transformedByScaleScalesTheBox)
        {
            const std::array<double, 16> scale{ 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 1 };
            const Aabb t = transformed(makeBox({ -1, -1, -1 }, { 1, 1, 1 }), scale);
            expectVec(t.min, { -2, -3, -4 });
            expectVec(t.max, { 2, 3, 4 });
        }

        TEST(CSMMeshFrameMathTest, transformedInvalidStaysInvalid)
        {
            const std::array<double, 16> identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            EXPECT_FALSE(transformed(Aabb{}, identity).valid);
        }

        TEST(CSMMeshFrameMathTest, centreIsTheMidpoint)
        {
            expectVec(centre(makeBox({ 0, -2, 4 }, { 2, 2, 8 })), { 1, 0, 6 });
        }

        TEST(CSMMeshFrameMathTest, radiusIsHalfTheDiagonalAndZeroWhenInvalid)
        {
            EXPECT_NEAR(radius(makeBox({ 0, 0, 0 }, { 3, 4, 0 })), 2.5, 1e-12);
            EXPECT_NEAR(radius(makeBox({ -1, -1, -1 }, { 1, 1, 1 })), std::sqrt(3.0), 1e-12);
            EXPECT_DOUBLE_EQ(radius(Aabb{}), 0.0);
            EXPECT_DOUBLE_EQ(radius(makeBox({ 1, 1, 1 }, { 1, 1, 1 })), 0.0);
        }

        TEST(CSMMeshFrameMathTest, frameCameraLooksAtTheCentreWithZUp)
        {
            const Aabb box = makeBox({ 10, 20, 30 }, { 12, 24, 36 });
            const FramePose pose = frameCamera(box, 60.0, 1.0);
            expectVec(pose.centre, { 11, 22, 33 });
            expectVec(pose.up, { 0, 0, 1 });
        }

        TEST(CSMMeshFrameMathTest, frameCameraDistanceFollowsTheFormulaForASquareView)
        {
            const Aabb box = makeBox({ -1, -1, -1 }, { 1, 1, 1 });
            const Vec3 dir{ 0.5, -1.0, 0.35 };
            const FramePose pose = frameCamera(box, 60.0, 1.0);

            const double d = expectedDistance(std::sqrt(3.0), 60.0, 1.0, 1.15);
            EXPECT_NEAR(distanceBetween(pose.eye, pose.centre), d, 1e-9);

            // eye = centre - normalize(dir) * distance
            const Vec3 n = normalized(dir);
            expectVec(pose.eye, { -n[0] * d, -n[1] * d, -n[2] * d }, 1e-9);
        }

        TEST(CSMMeshFrameMathTest, frameCameraUsesTheNarrowerFieldOfViewForTallWindows)
        {
            const Aabb box = makeBox({ -1, -1, -1 }, { 1, 1, 1 });
            const FramePose wide = frameCamera(box, 50.0, 2.0);
            const FramePose tall = frameCamera(box, 50.0, 0.5);

            // wide window: fovY is the smaller one
            EXPECT_NEAR(
                distanceBetween(wide.eye, wide.centre), expectedDistance(std::sqrt(3.0), 50.0, 2.0, 1.15), 1e-9);
            // tall window: fovX is the smaller one, so the camera must be further away
            EXPECT_NEAR(
                distanceBetween(tall.eye, tall.centre), expectedDistance(std::sqrt(3.0), 50.0, 0.5, 1.15), 1e-9);
            EXPECT_GT(distanceBetween(tall.eye, tall.centre), distanceBetween(wide.eye, wide.centre));
        }

        TEST(CSMMeshFrameMathTest, frameCameraHonoursCustomDirectionAndMargin)
        {
            const Aabb box = makeBox({ 0, 0, 0 }, { 2, 2, 2 });
            const Vec3 dir{ 0.0, 0.0, -2.0 }; // looking straight down
            const FramePose pose = frameCamera(box, 45.0, 1.5, dir, 2.0);

            const double d = expectedDistance(std::sqrt(3.0), 45.0, 1.5, 2.0);
            expectVec(pose.centre, { 1, 1, 1 });
            expectVec(pose.eye, { 1, 1, 1 + d }, 1e-9); // eye above the centre
        }

        TEST(CSMMeshFrameMathTest, frameCameraDirectionIsNormalisedSoItsLengthDoesNotMatter)
        {
            const Aabb box = makeBox({ -1, -1, -1 }, { 1, 1, 1 });
            const FramePose a = frameCamera(box, 60.0, 1.0, { 0.0, -1.0, 0.0 });
            const FramePose b = frameCamera(box, 60.0, 1.0, { 0.0, -10.0, 0.0 });
            expectVec(a.eye, b.eye, 1e-9);
        }

        TEST(CSMMeshFrameMathTest, frameCameraGivesInvalidAndZeroRadiusBoxesARadiusOfOne)
        {
            const double d = expectedDistance(1.0, 60.0, 1.0, 1.15);

            const FramePose fromInvalid = frameCamera(Aabb{}, 60.0, 1.0);
            EXPECT_NEAR(distanceBetween(fromInvalid.eye, fromInvalid.centre), d, 1e-9);

            const FramePose fromPoint = frameCamera(makeBox({ 5, 6, 7 }, { 5, 6, 7 }), 60.0, 1.0);
            expectVec(fromPoint.centre, { 5, 6, 7 });
            EXPECT_NEAR(distanceBetween(fromPoint.eye, fromPoint.centre), d, 1e-9);
        }

        TEST(CSMMeshFrameMathTest, frameCameraWithZeroDirectionThrows)
        {
            const Aabb box = makeBox({ 0, 0, 0 }, { 1, 1, 1 });
            EXPECT_THROW(frameCamera(box, 60.0, 1.0, { 0.0, 0.0, 0.0 }), std::invalid_argument);
        }

        TEST(CSMMeshFrameMathTest, frameCameraIsDeterministic)
        {
            const Aabb box = makeBox({ -3, 1, 2 }, { 8, 9, 4 });
            const FramePose a = frameCamera(box, 55.0, 1.7);
            const FramePose b = frameCamera(box, 55.0, 1.7);
            EXPECT_EQ(a.eye, b.eye);
            EXPECT_EQ(a.centre, b.centre);
            EXPECT_EQ(a.up, b.up);
        }
    }
}
