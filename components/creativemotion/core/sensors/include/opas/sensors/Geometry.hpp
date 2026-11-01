#pragma once

// Ground-plane geometry for the balance sensors (M5a, docs/tasks/0017): the
// support area as a convex hull, distances to it, and the capture point.
// Pure functions; nothing here allocates when its output buffer is reserved.

#include <cstddef>
#include <span>
#include <vector>

namespace opas::sensors
{

    // A point on the ground plane (world x, y), metres.
    struct Point2
    {
        double x = 0.0;
        double y = 0.0;
        friend bool operator==(const Point2&, const Point2&) = default;
    };

    // Convex hull of `points` (Andrew's monotone chain), counter-clockwise,
    // without repeated or collinear points. `points` is sorted in place. `hull`
    // is cleared and filled; it allocates nothing if its capacity is at least
    // points.size() + 1. Fewer than three distinct points give those points.
    // Every point must be finite (sorting NaN is undefined behaviour).
    void convexHull(std::span<Point2> points, std::vector<Point2>& hull);

    // Signed distance from `p` to the area a counter-clockwise hull encloses:
    // negative inside (minus the distance to the nearest edge), positive
    // outside. A hull of two points is a segment and one point is a point (the
    // distance is never negative then); an empty hull, or a point that is not
    // finite, gives +infinity (never "inside").
    double signedDistance(Point2 p, std::span<const Point2> hull);

    // The instantaneous capture point of the linear inverted pendulum,
    // p = c + v * sqrt(h / g) (Pratt, Carff, Drakunov, Goswami 2006, "Capture
    // Point: A Step toward Humanoid Push Recovery"): where the centre of mass
    // would have to be over to come to rest. `heightM` is the centre of mass
    // above the support surface and `gravityMps2` the magnitude of gravity, both
    // > 0; otherwise the centre of mass itself is returned.
    Point2 capturePoint(Point2 centreOfMass, Point2 velocityMps, double heightM, double gravityMps2);

} // namespace opas::sensors
