#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <osg/BoundingBox>
#include <osg/BoundingSphere>
#include <osg/Plane>
#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <vsg/maths/box.h>
#include <vsg/maths/plane.h>
#include <vsg/maths/sphere.h>
#include <vsg/maths/vec3.h>

#include <cfloat>
#include <cmath>
#include <limits>
#include <type_traits>

// How to convert osg::BoundingBox, osg::BoundingSphere and osg::Plane to VSG. Every test names the mapping.
//
// Sources: extern/osg/osg/BoundingBox, BoundingSphere, Plane, Config; VSG v1.1.16 vsg/maths/box.h, sphere.h, plane.h.
//
// TRAP (types): OSG's unsized names are DOUBLE here: extern/osg/osg/Config:27-34 leaves OSG_USE_FLOAT_PLANE,
// OSG_USE_FLOAT_BOUNDINGBOX and OSG_USE_FLOAT_BOUNDINGSPHERE undefined. So osg::BoundingBox maps to vsg::dbox,
// osg::BoundingSphere to vsg::dsphere, osg::Plane to vsg::dplane. The spec's goal line names vsg::box and vsg::plane,
// which are float. osg::BoundingBoxf and osg::BoundingSpheref (NIF data) map to vsg::box and vsg::sphere.
//
// HELPERS components/misc/vsgmath.hpp MUST PROVIDE (the implementer writes them; these tests do not use them):
//  - valid(box): all three axes, not only X.
//  - center(box), radius(box), radius2(box), corner(box, i) with OSG's bit order, contains(box, v) and contains(box, v, eps),
//    intersect(box, box).
//  - expandBy(box, box) skipping an invalid box as a whole; expandBy(box, sphere).
//  - expandBy(sphere, point), expandBy(sphere, sphere), expandBy(sphere, box), expandRadiusBy(sphere, point).
//  - makePlane(v1, v2, v3): OSG's three-point constructor (Plane:113-120). VSG has none. Terrain height uses it:
//    components/esmterrain/storage.cpp:631, 633.
//  - makePlane(normal, point) to hide the swapped VSG argument order; flip(plane), makeUnitLength(plane).
//
// Compile-time differences: _min and _max become min and max; xMin() becomes min.x; expandBy(point) becomes add(point).
namespace
{
    using namespace MathConventions;

    TEST(BoundsConventions, unsizedOsgNamesAreDouble)
    {
        // Not osg::BoundingBox and osg::BoundingSphere: on Windows <osg/Config> can resolve to vcpkg's copy, which defines
        // OSG_USE_FLOAT_BOUNDINGBOX, so those two names are float or double depending on the include path (task 0013).
        static_assert(std::is_same_v<osg::BoundingBoxd::value_type, double>);
        static_assert(std::is_same_v<osg::BoundingSphered::value_type, double>);
        static_assert(std::is_same_v<osg::Plane::value_type, double>);
    }

    // OSG default box is invalid in both. For the float box the sentinels are equal too.
    // OSG BoundingBoxf() equals VSG box(). BoundingBox:45-52, box.h:28-29.
    TEST(BoxConventions, defaultFloatBox)
    {
        const osg::BoundingBoxf o;
        const vsg::box v{};
        EXPECT_FALSE(o.valid());
        EXPECT_FALSE(v.valid());
        EXPECT_TRUE(vecExact<3>(o._min, v.min));
        EXPECT_TRUE(vecExact<3>(o._max, v.max));
    }

    // TRAP: a default osg::BoundingBoxd uses FLT_MAX as its sentinel, even though it is double (BoundingBox:45-52). VSG's
    // dbox uses the double limit (box.h:28-29). Any code reading an empty box's numbers, or comparing to a default box
    // across the two, sees a change.
    TEST(BoxConventions, defaultDoubleBoxSentinelsDiffer)
    {
        const osg::BoundingBoxd o;
        const vsg::dbox v{};
        EXPECT_EQ(o._min.x(), static_cast<double>(FLT_MAX));
        EXPECT_EQ(o._max.x(), -static_cast<double>(FLT_MAX));
        EXPECT_EQ(v.min.x, std::numeric_limits<double>::max());
        EXPECT_EQ(v.max.x, std::numeric_limits<double>::lowest());
    }

    // OSG box.expandBy(point) equals VSG box.add(point). BoundingBox:156-166, box.h:64-77.
    TEST(BoxConventions, expandByPoint)
    {
        osg::BoundingBoxd o;
        vsg::dbox v;
        const double points[][3] = { { 1.0, -2.0, 3.0 }, { -4.0, 5.0, 0.5 }, { 0.0, 0.0, 10.0 } };
        for (const auto& p : points)
        {
            o.expandBy(osg::Vec3d(p[0], p[1], p[2]));
            v.add(vsg::dvec3(p[0], p[1], p[2]));
            EXPECT_TRUE(vecExact<3>(o._min, v.min));
            EXPECT_TRUE(vecExact<3>(o._max, v.max));
        }
        EXPECT_TRUE(o.valid());
        EXPECT_TRUE(v.valid());
    }

    // OSG box.expandBy(other) equals VSG box.add(other) for valid boxes, and for a fully empty other. box.h:80-88.
    TEST(BoxConventions, expandByBox)
    {
        osg::BoundingBoxd o(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        vsg::dbox v(vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(1.0, 1.0, 1.0));

        o.expandBy(osg::BoundingBoxd(-1.0, 0.5, 0.25, 0.5, 2.0, 0.75));
        v.add(vsg::dbox(vsg::dvec3(-1.0, 0.5, 0.25), vsg::dvec3(0.5, 2.0, 0.75)));
        EXPECT_TRUE(vecExact<3>(o._min, v.min));
        EXPECT_TRUE(vecExact<3>(o._max, v.max));

        o.expandBy(osg::BoundingBoxd());
        v.add(vsg::dbox());
        EXPECT_TRUE(vecExact<3>(o._min, v.min));
        EXPECT_TRUE(vecExact<3>(o._max, v.max));
    }

    // TRAP: OSG valid() checks all three axes (BoundingBox:86-89); VSG only X (box.h:50). A box inverted on Y alone is
    // invalid in OSG and valid in VSG.
    TEST(BoxConventions, validChecksAllAxesInOsgAndOnlyXInVsg)
    {
        const osg::BoundingBoxd o(0.0, 5.0, 0.0, 1.0, 1.0, 1.0);
        const vsg::dbox v(vsg::dvec3(0.0, 5.0, 0.0), vsg::dvec3(1.0, 1.0, 1.0));
        EXPECT_FALSE(o.valid());
        EXPECT_TRUE(v.valid());
    }

    // TRAP: for the same reason, merging such a box does nothing in OSG (BoundingBox:187) and changes the box in VSG.
    TEST(BoxConventions, mergingAPartlyInvertedBox)
    {
        osg::BoundingBoxd o(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        vsg::dbox v(vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(1.0, 1.0, 1.0));

        o.expandBy(osg::BoundingBoxd(-1.0, 5.0, 0.0, 2.0, 1.0, 1.0));
        v.add(vsg::dbox(vsg::dvec3(-1.0, 5.0, 0.0), vsg::dvec3(2.0, 1.0, 1.0)));

        EXPECT_EQ(o._min.x(), 0.0);
        EXPECT_EQ(v.min.x, -1.0);
    }

    // OSG box.center() equals VSG (min + max) * 0.5, and radius() equals length(max - min) * 0.5 to tolerance.
    // BoundingBox:126-142.
    TEST(BoxConventions, centerAndRadius)
    {
        const osg::BoundingBoxf o(-1.f, 2.f, 3.f, 4.f, 5.5f, 7.f);
        const vsg::box v(vsg::vec3(-1.f, 2.f, 3.f), vsg::vec3(4.f, 5.5f, 7.f));

        EXPECT_TRUE(vecExact<3>(o.center(), (v.min + v.max) * 0.5f));
        EXPECT_NEAR(o.radius(), vsg::length(v.max - v.min) * 0.5f, 1e-6 * o.radius());
        EXPECT_NEAR(o.radius2(), vsg::length2(v.max - v.min) * 0.25f, 1e-6 * o.radius2());
    }

    // Pin for the helper. Bit 0 selects X max, bit 1 Y max, bit 2 Z max. BoundingBox:149-152.
    TEST(BoxConventions, cornerBitOrderPin)
    {
        const osg::BoundingBoxf o(1.f, 2.f, 3.f, 4.f, 5.f, 6.f);
        EXPECT_TRUE(vecExact<3>(o.corner(0), vsg::vec3(1.f, 2.f, 3.f)));
        EXPECT_TRUE(vecExact<3>(o.corner(1), vsg::vec3(4.f, 2.f, 3.f)));
        EXPECT_TRUE(vecExact<3>(o.corner(2), vsg::vec3(1.f, 5.f, 3.f)));
        EXPECT_TRUE(vecExact<3>(o.corner(4), vsg::vec3(1.f, 2.f, 6.f)));
        EXPECT_TRUE(vecExact<3>(o.corner(7), vsg::vec3(4.f, 5.f, 6.f)));
    }

    // Pin for the helper. Inclusive at the faces, false for an invalid box, epsilon widens both sides. BoundingBox:233-248.
    TEST(BoxConventions, containsPin)
    {
        const osg::BoundingBoxf o(0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
        EXPECT_TRUE(o.contains(osg::Vec3f(1.f, 1.f, 1.f)));
        EXPECT_FALSE(o.contains(osg::Vec3f(1.5f, 0.f, 0.f)));
        EXPECT_TRUE(o.contains(osg::Vec3f(1.5f, 0.f, 0.f), 0.5f));
        EXPECT_FALSE(osg::BoundingBoxf().contains(osg::Vec3f(0.f, 0.f, 0.f)));
    }

    // OSG box.expandBy(sphere) equals VSG box.add(center - r); box.add(center + r). BoundingSphere is skipped when invalid.
    // BoundingBox:202-214.
    TEST(BoxConventions, expandBySphere)
    {
        osg::BoundingBoxd o(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        o.expandBy(osg::BoundingSphered(osg::Vec3d(5.0, 0.0, 0.0), 2.0));

        vsg::dbox v(vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(1.0, 1.0, 1.0));
        const vsg::dsphere sphere(vsg::dvec3(5.0, 0.0, 0.0), 2.0);
        const vsg::dvec3 radius(sphere.radius, sphere.radius, sphere.radius);
        v.add(sphere.center - radius);
        v.add(sphere.center + radius);

        EXPECT_TRUE(vecExact<3>(o._min, v.min));
        EXPECT_TRUE(vecExact<3>(o._max, v.max));
    }

    // OSG BoundingSphere() equals VSG dsphere(): centre 0, radius -1, invalid. BoundingSphere:44, sphere.h:57.
    // OSG sphere.radius() and center() are VSG's fields radius and center.
    TEST(SphereConventions, defaultAndFields)
    {
        const osg::BoundingSphered o;
        const vsg::dsphere v;
        EXPECT_FALSE(o.valid());
        EXPECT_FALSE(v.valid());
        EXPECT_EQ(o.radius(), v.radius);
        EXPECT_TRUE(vecExact<3>(o.center(), v.center));

        const osg::BoundingSpheref of(osg::Vec3f(1.f, 2.f, 3.f), 4.f);
        const vsg::sphere vf(vsg::vec3(1.f, 2.f, 3.f), 4.f);
        EXPECT_TRUE(of.valid());
        EXPECT_TRUE(vf.valid());
        EXPECT_EQ(of.radius(), vf.radius);
        EXPECT_TRUE(vecExact<3>(of.center(), vf.center));
    }

    // Pins for the helper: VSG spheres have no expand functions. BoundingSphere:150-168, 172-185.
    // expandBy(point) on an unset sphere: centre is the point, radius 0. Outside: the centre moves to minimise the increase.
    TEST(SphereConventions, expandByPointPins)
    {
        osg::BoundingSphered o;
        o.expandBy(osg::Vec3d(3.0, 0.0, 0.0));
        EXPECT_TRUE(vecExact<3>(o.center(), vsg::dvec3(3.0, 0.0, 0.0)));
        EXPECT_EQ(o.radius(), 0.0);

        osg::BoundingSphered moved(osg::Vec3d(0.0, 0.0, 0.0), 1.0);
        moved.expandBy(osg::Vec3d(3.0, 0.0, 0.0));
        EXPECT_TRUE(vecExact<3>(moved.center(), vsg::dvec3(1.0, 0.0, 0.0)));
        EXPECT_EQ(moved.radius(), 2.0);

        osg::BoundingSphered inside(osg::Vec3d(0.0, 0.0, 0.0), 5.0);
        inside.expandBy(osg::Vec3d(3.0, 0.0, 0.0));
        EXPECT_TRUE(vecExact<3>(inside.center(), vsg::dvec3(0.0, 0.0, 0.0)));
        EXPECT_EQ(inside.radius(), 5.0);

        osg::BoundingSphered radiusOnly(osg::Vec3d(0.0, 0.0, 0.0), 1.0);
        radiusOnly.expandRadiusBy(osg::Vec3d(3.0, 0.0, 0.0));
        EXPECT_TRUE(vecExact<3>(radiusOnly.center(), vsg::dvec3(0.0, 0.0, 0.0)));
        EXPECT_EQ(radiusOnly.radius(), 3.0);
    }

    // Pin for the helper. OSG BoundingSphere(box) takes the box's centre and radius (BoundingSphere:53, 280-284).
    TEST(SphereConventions, fromBoxPin)
    {
        const osg::BoundingBoxd box(0.0, 0.0, 0.0, 2.0, 0.0, 0.0);
        const osg::BoundingSphered o(box);
        EXPECT_TRUE(vecExact<3>(o.center(), vsg::dvec3(1.0, 0.0, 0.0)));
        EXPECT_EQ(o.radius(), 1.0);
    }

    // OSG Plane(a, b, c, d) equals VSG dplane(a, b, c, d). Plane:63, plane.h:63.
    TEST(PlaneConventions, fromCoefficients)
    {
        EXPECT_TRUE(vecExact<4>(osg::Plane(0.1, 0.2, 0.3, 0.4), vsg::dplane(0.1, 0.2, 0.3, 0.4)));
    }

    // OSG Plane(Vec4f) equals VSG dplane(dvec4(v)): widened, not normalized. NIF clip and collision planes use this:
    // components/nif/effect.cpp:68, components/nif/node.cpp:127, components/nif/physics.cpp:715. Plane:68.
    TEST(PlaneConventions, fromFloatVec4)
    {
        const osg::Vec4f oV(0.f, 0.f, 2.f, -6.f);
        const vsg::vec4 vV(0.f, 0.f, 2.f, -6.f);
        EXPECT_TRUE(vecExact<4>(osg::Plane(oV), vsg::dplane(vsg::dvec4(vV))));
    }

    // OSG Plane(normal, d) equals VSG dplane(normal, d). Plane:79, plane.h:66.
    TEST(PlaneConventions, fromNormalAndDistance)
    {
        EXPECT_TRUE(vecExact<4>(osg::Plane(osg::Vec3d(0.0, 0.0, 1.0), -5.0), vsg::dplane(vsg::dvec3(0.0, 0.0, 1.0), -5.0)));
    }

    // TRAP: both libraries take two vectors, and the ORDER IS SWAPPED, with no compile error.
    // OSG Plane(normal, point) (Plane:96, 122) equals VSG dplane(point, normal) (plane.h:69). Keeping the order builds a
    // different plane.
    TEST(PlaneConventions, fromNormalAndPointSwapsArguments)
    {
        const osg::Vec3d oNormal(0.0, 0.0, 1.0);
        const osg::Vec3d oPoint(1.0, 2.0, 5.0);
        const vsg::dvec3 vNormal(0.0, 0.0, 1.0);
        const vsg::dvec3 vPoint(1.0, 2.0, 5.0);

        const osg::Plane o(oNormal, oPoint);
        EXPECT_TRUE(vecNear<4>(o, vsg::dplane(vPoint, vNormal)));
        EXPECT_TRUE(vecNear<4>(o, vsg::dplane(0.0, 0.0, 1.0, -5.0)));
        EXPECT_FALSE(vecNear<4>(o, vsg::dplane(vNormal, vPoint)));
    }

    // VSG has no three-point constructor. OSG Plane(v1, v2, v3) (Plane:113-120) equals, in VSG primitives:
    //   n = cross(v2 - v1, v3 - v2); if (length(n) > 1e-6) n /= length(n); else n = 0; dplane(n, -dot(v1, n))
    // To tolerance, because /= multiplies by a reciprocal. The winding gives the normal's sign.
    TEST(PlaneConventions, fromThreePoints)
    {
        const osg::Vec3d o1(0.0, 0.0, 1.0);
        const osg::Vec3d o2(2.0, 0.0, 1.5);
        const osg::Vec3d o3(0.0, 3.0, 2.0);
        const vsg::dvec3 v1(0.0, 0.0, 1.0);
        const vsg::dvec3 v2(2.0, 0.0, 1.5);
        const vsg::dvec3 v3(0.0, 3.0, 2.0);

        vsg::dvec3 n = vsg::cross(v2 - v1, v3 - v2);
        n /= vsg::length(n);
        EXPECT_TRUE(vecNear<4>(osg::Plane(o1, o2, o3), vsg::dplane(n, -vsg::dot(v1, n))));
    }

    // Pin for the helper: three collinear points give a zero normal and d of 0, not NaN. Plane:117-119.
    TEST(PlaneConventions, fromCollinearPointsPin)
    {
        const osg::Plane o(osg::Vec3d(0.0, 0.0, 0.0), osg::Vec3d(1.0, 0.0, 0.0), osg::Vec3d(2.0, 0.0, 0.0));
        EXPECT_TRUE(vecNear<4>(o, vsg::dplane(0.0, 0.0, 0.0, 0.0)));
    }

    // OSG plane.distance(v) equals VSG distance(plane, v). Same sign convention: dot(n, v) + d. Plane:200, 210, plane.h:143.
    // Argument order differs by being a free function.
    TEST(PlaneConventions, distance)
    {
        const osg::Plane o(0.1, 0.2, 0.3, 0.4);
        const vsg::dplane v(0.1, 0.2, 0.3, 0.4);
        EXPECT_EQ(o.distance(osg::Vec3d(1.5, -2.25, 3.75)), vsg::distance(v, vsg::dvec3(1.5, -2.25, 3.75)));
    }

    // TRAP: OSG distance(Vec3f) computes in double and returns FLOAT (Plane:200). VSG distance(dplane, vec3) returns DOUBLE
    // (plane.h:149). OSG o.distance(f) equals VSG static_cast<float>(distance(p, f)).
    TEST(PlaneConventions, distanceOfFloatVector)
    {
        const osg::Plane o(0.1, 0.2, 0.3, 0.4);
        const vsg::dplane v(0.1, 0.2, 0.3, 0.4);
        const osg::Vec3f oPoint(1.5f, -2.25f, 3.7f);
        const vsg::vec3 vPoint(1.5f, -2.25f, 3.7f);

        static_assert(std::is_same_v<decltype(o.distance(oPoint)), float>);
        static_assert(std::is_same_v<decltype(vsg::distance(v, vPoint)), double>);
        EXPECT_EQ(o.distance(oPoint), static_cast<float>(vsg::distance(v, vPoint)));
    }

    // OSG p.getNormal() equals VSG p.n; OSG p[3] equals VSG p.p or p[3]; OSG dotProductNormal(v) equals VSG dot(p.n, v).
    // Plane:192, 195, 227.
    TEST(PlaneConventions, normalAndD)
    {
        const osg::Plane o(0.1, 0.2, 0.3, 0.4);
        const vsg::dplane v(0.1, 0.2, 0.3, 0.4);
        EXPECT_TRUE(vecExact<3>(o.getNormal(), v.n));
        EXPECT_EQ(o[3], v.p);
        EXPECT_EQ(o[3], v[3]);
        EXPECT_EQ(o.dotProductNormal(osg::Vec3d(1.5, -2.25, 3.75)), vsg::dot(v.n, vsg::dvec3(1.5, -2.25, 3.75)));
    }

    // Pin for the helper: flip negates all four. makeUnitLength scales all four, so d is scaled too. Plane:129, 140.
    TEST(PlaneConventions, flipAndMakeUnitLengthPins)
    {
        osg::Plane flipped(0.0, 0.0, 2.0, -6.0);
        flipped.flip();
        EXPECT_TRUE(vecExact<4>(flipped, vsg::dplane(0.0, 0.0, -2.0, 6.0)));

        osg::Plane unit(0.0, 0.0, 2.0, -6.0);
        unit.makeUnitLength();
        EXPECT_TRUE(vecNear<4>(unit, vsg::dplane(0.0, 0.0, 1.0, -3.0)));
    }
}

#endif
