// Task 0091, stage c: picking of overlay data, pure maths. Written from the spec alone, against a stub.
//
// Pinned: a ray at a point within the pixel tolerance picks it (tolerance grows with distance along the ray); the
// nearest hit along the ray wins whatever else is closer to the ray's line; equal distances are decided by the item
// index; segments and triangles are measured the same way; hostile input (NaN, zero direction) picks nothing.
#include "apps/opencs/model/module/overlaypick.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

namespace
{
    using namespace CSMModule;
    using openeden::editor::Vec3;

    PickRay alongX(double y = 0, double z = 0)
    {
        return { { 0, y, z }, { 1, 0, 0 } };
    }

    PickItem point(Vec3 p)
    {
        PickItem item;
        item.mKind = PickItem::Kind::Point;
        item.mA = p;
        return item;
    }

    PickItem segment(Vec3 a, Vec3 b)
    {
        PickItem item;
        item.mKind = PickItem::Kind::Segment;
        item.mA = a;
        item.mB = b;
        return item;
    }

    PickItem triangle(Vec3 a, Vec3 b, Vec3 c)
    {
        PickItem item;
        item.mKind = PickItem::Kind::Triangle;
        item.mA = a;
        item.mB = b;
        item.mC = c;
        return item;
    }

    TEST(CSMOverlayPick, theSlopeIsPixelsTimesTwoTanHalfFovOverHeight)
    {
        const double fov = std::numbers::pi / 3.0; // 60 degrees
        EXPECT_NEAR(pickSlope(5.0, fov, 600.0), 5.0 * 2.0 * std::tan(fov / 2.0) / 600.0, 1e-12);
        EXPECT_EQ(pickSlope(5.0, fov, 0.0), 0.0);
        EXPECT_EQ(pickSlope(5.0, 0.0, 600.0), 0.0);
        EXPECT_EQ(pickSlope(5.0, std::numbers::pi, 600.0), 0.0);
        EXPECT_EQ(pickSlope(-1.0, fov, 600.0), 0.0);
    }

    TEST(CSMOverlayPick, aPointOnTheRayIsHitWithItsDistance)
    {
        const std::vector<PickItem> items{ point({ 10, 0, 0 }) };
        const auto hit = pickNearest(alongX(), items, 0.01);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->mIndex, 0u);
        EXPECT_NEAR(hit->mDistance, 10.0, 1e-9);
        EXPECT_NEAR(hit->mMiss, 0.0, 1e-9);
    }

    TEST(CSMOverlayPick, aPointWithinTheToleranceIsHitAndOneBeyondItIsNot)
    {
        const double slope = 0.01; // at distance 10 the tolerance is 0.1
        EXPECT_TRUE(pickNearest(alongX(0.09), { point({ 10, 0, 0 }) }, slope).has_value());
        EXPECT_FALSE(pickNearest(alongX(0.11), { point({ 10, 0, 0 }) }, slope).has_value());
        // The same offset is a miss nearer and a hit farther: the tolerance grows with the distance.
        EXPECT_FALSE(pickNearest(alongX(0.09), { point({ 5, 0, 0 }) }, slope).has_value());
        EXPECT_TRUE(pickNearest(alongX(0.09), { point({ 20, 0, 0 }) }, slope).has_value());
    }

    TEST(CSMOverlayPick, aPointBehindTheRayOriginIsNotHit)
    {
        EXPECT_FALSE(pickNearest(alongX(), { point({ -10, 0, 0 }) }, 0.5).has_value());
    }

    TEST(CSMOverlayPick, theDirectionNeedNotBeAUnitVector)
    {
        PickRay ray{ { 0, 0, 0 }, { 5, 0, 0 } };
        const auto hit = pickNearest(ray, { point({ 10, 0, 0 }) }, 0.01);
        ASSERT_TRUE(hit.has_value());
        EXPECT_NEAR(hit->mDistance, 10.0, 1e-9);
    }

    TEST(CSMOverlayPick, theNearestAlongTheRayWinsEvenIfAFartherOneIsCloserToTheLine)
    {
        const std::vector<PickItem> items{ point({ 20, 0.0, 0 }), point({ 10, 0.05, 0 }) };
        const auto hit = pickNearest(alongX(), items, 0.01);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->mIndex, 1u);
        EXPECT_NEAR(hit->mDistance, 10.0, 1e-9);
    }

    TEST(CSMOverlayPick, equalDistancesAreDecidedByTheLowerIndex)
    {
        const std::vector<PickItem> items{ point({ 10, 0.0, 0.02 }), point({ 10, 0.02, 0 }), point({ 10, 0, 0 }) };
        const auto hit = pickNearest(alongX(), items, 0.01);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->mIndex, 0u);
        // Whatever the order, the lowest index among the tied wins.
        const std::vector<PickItem> reversed{ items[2], items[1], items[0] };
        EXPECT_EQ(pickNearest(alongX(), reversed, 0.01)->mIndex, 0u);
    }

    TEST(CSMOverlayPick, noItemsNothingHit)
    {
        EXPECT_FALSE(pickNearest(alongX(), {}, 0.01).has_value());
    }

    TEST(CSMOverlayPick, aSegmentCrossingTheRayIsHitAtTheCrossing)
    {
        const std::vector<PickItem> items{ segment({ 10, -1, 0.0 }, { 10, 1, 0.0 }) };
        const auto hit = pickNearest(alongX(), items, 0.0);
        ASSERT_TRUE(hit.has_value());
        EXPECT_NEAR(hit->mDistance, 10.0, 1e-9);
        EXPECT_NEAR(hit->mMiss, 0.0, 1e-9);
    }

    TEST(CSMOverlayPick, aSegmentNearTheRayIsHitWithinTheToleranceAndItsEndsClamp)
    {
        double t = 0;
        // Crosses at height 0.05 above the ray: a miss of 0.05 at distance 10.
        EXPECT_NEAR(raySegmentMiss(alongX(), { 10, -1, 0.05 }, { 10, 1, 0.05 }, t), 0.05, 1e-9);
        EXPECT_NEAR(t, 10.0, 1e-9);
        EXPECT_TRUE(pickNearest(alongX(), { segment({ 10, -1, 0.05 }, { 10, 1, 0.05 }) }, 0.01).has_value());
        EXPECT_FALSE(pickNearest(alongX(), { segment({ 10, -1, 0.2 }, { 10, 1, 0.2 }) }, 0.01).has_value());

        // The segment ends at y = 2: the closest point is its end, 2 away from the ray.
        EXPECT_NEAR(raySegmentMiss(alongX(), { 10, 2, 0 }, { 10, 5, 0 }, t), 2.0, 1e-9);
    }

    TEST(CSMOverlayPick, aSegmentParallelToTheRayIsMeasuredWithoutDividingByZero)
    {
        double t = 0;
        const double miss = raySegmentMiss(alongX(), { 5, 1, 0 }, { 9, 1, 0 }, t);
        EXPECT_TRUE(std::isfinite(miss));
        EXPECT_NEAR(miss, 1.0, 1e-9);
        EXPECT_TRUE(std::isfinite(t));
    }

    TEST(CSMOverlayPick, aDegenerateSegmentIsAPoint)
    {
        double t = 0;
        EXPECT_NEAR(raySegmentMiss(alongX(), { 10, 0.5, 0 }, { 10, 0.5, 0 }, t), 0.5, 1e-9);
        EXPECT_NEAR(t, 10.0, 1e-9);
    }

    TEST(CSMOverlayPick, aTriangleIsHitExactlyOnBothSides)
    {
        const PickItem tri = triangle({ 10, -1, -1 }, { 10, 1, -1 }, { 10, 0, 1 });
        const auto front = pickNearest(alongX(), { tri }, 0.0);
        ASSERT_TRUE(front.has_value());
        EXPECT_NEAR(front->mDistance, 10.0, 1e-9);
        EXPECT_EQ(front->mMiss, 0.0);
        // The other winding is hit as well.
        const PickItem flipped = triangle({ 10, -1, -1 }, { 10, 0, 1 }, { 10, 1, -1 });
        EXPECT_TRUE(pickNearest(alongX(), { flipped }, 0.0).has_value());
        // Outside by a little: a miss, whatever the slope (a triangle has no tolerance).
        EXPECT_FALSE(pickNearest(alongX(0, 1.2), { tri }, 0.5).has_value());
        // Behind the origin and parallel: misses.
        EXPECT_FALSE(
            pickNearest(alongX(), { triangle({ -10, -1, -1 }, { -10, 1, -1 }, { -10, 0, 1 }) }, 0.0).has_value());
        EXPECT_FALSE(rayTriangleDistance(alongX(), { 10, -1, -1 }, { 11, -1, -1 }, { 10, -1, 1 }).has_value());
    }

    TEST(CSMOverlayPick, aNearerTriangleBeatsAFarHandle)
    {
        const std::vector<PickItem> items{ point({ 20, 0, 0 }), triangle({ 5, -1, -1 }, { 5, 1, -1 }, { 5, 0, 1 }) };
        const auto hit = pickNearest(alongX(), items, 0.01);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->mIndex, 1u);
    }

    TEST(CSMOverlayPick, hostileInputPicksNothingAndNeverThrows)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        EXPECT_FALSE(pickNearest(alongX(), { point({ nan, 0, 0 }) }, 0.1).has_value());
        EXPECT_FALSE(pickNearest(alongX(), { segment({ inf, 0, 0 }, { 10, 0, 0 }) }, 0.1).has_value());
        EXPECT_FALSE(pickNearest(alongX(), { triangle({ 1, nan, 0 }, { 2, 0, 0 }, { 3, 1, 0 }) }, 0.1).has_value());
        const PickRay zero{ { 0, 0, 0 }, { 0, 0, 0 } };
        EXPECT_FALSE(pickNearest(zero, { point({ 10, 0, 0 }) }, 0.1).has_value());
        const PickRay bad{ { nan, 0, 0 }, { 1, 0, 0 } };
        EXPECT_FALSE(pickNearest(bad, { point({ 10, 0, 0 }) }, 0.1).has_value());
        // A good item next to a bad one is still found, with its own index.
        const auto hit = pickNearest(alongX(), { point({ nan, 0, 0 }), point({ 10, 0, 0 }) }, 0.1);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->mIndex, 1u);
    }

    TEST(CSMOverlayPick, aRayMeetsAPlaneInFrontAndNotWhenParallelOrBehind)
    {
        const PickRay ray{ { 0, -10, 0 }, { 0, 1, 0 } };
        const auto hit = rayPlaneHit(ray, { 1, 0, 2 }, { 0, -1, 0 });
        ASSERT_TRUE(hit.has_value());
        EXPECT_NEAR(hit->x, 0.0, 1e-12);
        EXPECT_NEAR(hit->y, 0.0, 1e-12);
        EXPECT_NEAR(hit->z, 0.0, 1e-12);
        EXPECT_FALSE(rayPlaneHit(ray, { 0, 0, 0 }, { 1, 0, 0 }).has_value()); // parallel
        EXPECT_FALSE(rayPlaneHit(ray, { 0, -20, 0 }, { 0, 1, 0 }).has_value()); // behind
    }
}
