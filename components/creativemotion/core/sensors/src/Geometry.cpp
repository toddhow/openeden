#include "opas/sensors/Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace opas::sensors {
namespace {

// > 0 when o -> a -> b turns counter-clockwise.
double cross(Point2 o, Point2 a, Point2 b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

double distanceToSegment(Point2 p, Point2 a, Point2 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    double t = 0.0;
    if (lengthSquared > 0.0) {
        t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0, 1.0);
    }
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

} // namespace

void convexHull(std::span<Point2> points, std::vector<Point2>& hull)
{
    hull.clear();
    std::sort(points.begin(), points.end(),
              [](Point2 a, Point2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    // Lower hull left to right, then upper hull right to left; a point that
    // does not turn counter-clockwise is dropped (collinear ones too).
    for (const Point2 p : points) {
        while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0.0) {
            hull.pop_back();
        }
        if (hull.empty() || !(hull.back() == p)) {
            hull.push_back(p);
        }
    }
    const std::size_t lowerSize = hull.size();
    for (std::size_t i = points.size(); i-- > 0;) {
        const Point2 p = points[i];
        while (hull.size() > lowerSize && cross(hull[hull.size() - 2], hull.back(), p) <= 0.0) {
            hull.pop_back();
        }
        hull.push_back(p);
    }
    // The last point repeats the first (or, for a single point, everything
    // repeats); trim to distinct points.
    if (hull.size() > 1) {
        hull.pop_back();
    }
    if (lowerSize <= 1 && hull.size() > 1) {
        hull.resize(1);
    }
}

double signedDistance(Point2 p, std::span<const Point2> hull)
{
    // No hull, or a point that is not a number (a capture point from an
    // unstable state): never report it as inside, which would read as
    // balanced. NaN fails every comparison, so it is caught here explicitly.
    if (hull.empty() || !std::isfinite(p.x) || !std::isfinite(p.y)) {
        return std::numeric_limits<double>::infinity();
    }
    if (hull.size() == 1) {
        return std::hypot(p.x - hull[0].x, p.y - hull[0].y);
    }
    double nearest = std::numeric_limits<double>::infinity();
    bool inside = hull.size() >= 3;
    for (std::size_t i = 0; i < hull.size(); ++i) {
        const Point2 a = hull[i];
        const Point2 b = hull[(i + 1) % hull.size()];
        nearest = std::min(nearest, distanceToSegment(p, a, b));
        if (cross(a, b, p) < 0.0) {
            inside = false; // right of a counter-clockwise edge: outside
        }
    }
    return inside ? -nearest : nearest;
}

Point2 capturePoint(Point2 centreOfMass, Point2 velocityMps, double heightM, double gravityMps2)
{
    if (!(heightM > 0.0) || !(gravityMps2 > 0.0)) {
        return centreOfMass;
    }
    const double timeConstantS = std::sqrt(heightM / gravityMps2);
    return {centreOfMass.x + velocityMps.x * timeConstantS,
            centreOfMass.y + velocityMps.y * timeConstantS};
}

} // namespace opas::sensors
