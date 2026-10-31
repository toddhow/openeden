#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <osg/Matrixd>
#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Vec3d>
#include <osg/Vec3f>

#include <vsg/maths/quat.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec3.h>

#include <cmath>
#include <type_traits>

// How to convert osg::Quat to VSG. Every test names the mapping: "OSG X equals VSG Y".
//
// Sources: extern/osg/osg/Quat; components/vsgadapters/osg/Quat.cpp, Matrix_implementation.cpp; VSG v1.1.16
// vsg/maths/quat.h, transform.h, and src/vsg/maths/maths_transform.cpp.
//
// TRAP (type): osg::Quat is DOUBLE (extern/osg/osg/Config:34 leaves OSG_USE_FLOAT_QUAT undefined; Quat:38). The matching
// type is vsg::dquat. The spec's goal line names vsg::quat, which is float and would change every composed rotation.
//
// HELPERS components/misc/vsgmath.hpp MUST PROVIDE (the implementer writes them; these tests do not use them):
//  - makeRotate(from, to): OSG's algorithm (Quat.cpp:118-206), computed in double. vsg::dquat(from, to) can return NaN and
//    picks a different axis for opposite vectors.
//  - makeRotateFromAxis(angle, axis) with OSG's 1e-7 axis threshold (Quat.cpp:51-58). VSG's threshold is machine epsilon.
//  - length2(quat): VSG has length and dot for quats but no length2 (quat.h:241, 254).
//  - inverse(quat): OSG's conj / length2 (Quat:322). vsg::inverse divides by length (quat.h:260-265).
//  - negate(quat): OSG's -q negates all four (Quat:298). VSG's -q is the conjugate (quat.h:190-193).
//  - getRotate(q, angle, axis): OSG's atan2 form with axis (0, 0, 1) for no rotation (Quat.cpp:289-306).
//  - rotationOf(matrix): OSG's Matrix::getRotate (Matrix_implementation.cpp:151-208). VSG only offers decompose, which
//    removes scale and can return the opposite sign.
//  - toMatrix(q): OSG's Matrix::rotate(q), which handles non-unit and zero quats (Matrix_implementation.cpp:62-143).
//  - In-place operators: VSG quats have no *=, /=, +=, -= (quat.h), so q *= r becomes q = q * r.
//
// Compile-time differences: q.x() becomes q.x; vsg::dquat(angle, vec3) does not compile, because the axis must be dvec3
// and the conversion is explicit (quat.h:53, vec3.h:67); q * vec3 with a dquat does not compile (quat.h:215).
namespace
{
    using namespace MathConventions;

    const osg::Vec3d kOsgAxis1(1.0, 2.0, 3.0);
    const osg::Vec3d kOsgAxis2(-2.0, 0.5, 1.0);
    const vsg::dvec3 kVsgAxis1(1.0, 2.0, 3.0);
    const vsg::dvec3 kVsgAxis2(-2.0, 0.5, 1.0);
    // Read through volatile so the compiler cannot fold cos and sin here while OSG calls them at run time in its own
    // translation unit; a folded value can differ in the last bit from the C library's.
    double opaque(double value)
    {
        volatile double stored = value;
        return stored;
    }

    const double kAngle1 = opaque(0.7);
    const double kAngle2 = opaque(-1.9);

    const osg::Quat oQ1(kAngle1, kOsgAxis1);
    const osg::Quat oQ2(kAngle2, kOsgAxis2);
    const vsg::dquat vQ1(kAngle1, kVsgAxis1);
    const vsg::dquat vQ2(kAngle2, kVsgAxis2);

    // TEST: osg::Quat stores double; vsg::dquat is the type with the same arithmetic.
    TEST(QuatConventions, osgQuatIsDouble)
    {
        static_assert(std::is_same_v<osg::Quat::value_type, double>);
        static_assert(std::is_same_v<vsg::dquat::value_type, double>);
    }

    // OSG Quat() equals VSG dquat(). Both are the identity with w last. Quat:46, quat.h:47.
    TEST(QuatConventions, defaultIsIdentity)
    {
        EXPECT_TRUE(vecExact<4>(osg::Quat(), vsg::dquat()));
        EXPECT_TRUE(vecExact<4>(osg::Quat(), vsg::dquat(0.0, 0.0, 0.0, 1.0)));
    }

    // OSG Quat(x, y, z, w) equals VSG dquat(x, y, z, w). Quat:48, quat.h:51.
    TEST(QuatConventions, componentOrderIsXyzw)
    {
        EXPECT_TRUE(vecExact<4>(osg::Quat(0.1, 0.2, 0.3, 0.4), vsg::dquat(0.1, 0.2, 0.3, 0.4)));
    }

    // OSG Quat(angle, axis) equals VSG dquat(angle, axis). Same formula: Quat.cpp:61-68, quat.h:104-111.
    TEST(QuatConventions, fromAngleAndAxis)
    {
        EXPECT_TRUE(vecExact<4>(oQ1, vQ1));
        EXPECT_TRUE(vecExact<4>(oQ2, vQ2));
    }

    // A float axis is widened first in OSG (Quat.cpp:71-74).
    // OSG Quat(angle, Vec3f) equals VSG dquat(angle, dvec3(vec3)).
    TEST(QuatConventions, fromAngleAndFloatAxis)
    {
        const osg::Vec3f oAxis(0.1f, 0.7f, -0.3f);
        const vsg::vec3 vAxis(0.1f, 0.7f, -0.3f);
        EXPECT_TRUE(vecExact<4>(osg::Quat(kAngle1, oAxis), vsg::dquat(kAngle1, vsg::dvec3(vAxis))));
    }

    // TRAP: a very short axis. OSG gives the identity below length 1e-7 (Quat.cpp:51-58); VSG's limit for a dquat is
    // machine epsilon, about 2e-16 (quat.h:97, numbers.h:30), so it normalizes the short axis and rotates.
    // The helper keeps OSG's threshold.
    TEST(QuatConventions, shortAxisThresholdDiffers)
    {
        const osg::Vec3d oAxis(1e-8, 0.0, 0.0);
        const vsg::dvec3 vAxis(1e-8, 0.0, 0.0);
        EXPECT_TRUE(vecExact<4>(osg::Quat(kAngle1, oAxis), vsg::dquat()));

        const vsg::dquat rotated(kAngle1, vAxis);
        EXPECT_NEAR(rotated.x, std::sin(kAngle1 * 0.5), 1e-12);
        EXPECT_NEAR(rotated.w, std::cos(kAngle1 * 0.5), 1e-12);
    }

    // A zero axis gives the identity in both.
    TEST(QuatConventions, zeroAxisIsIdentity)
    {
        EXPECT_TRUE(vecExact<4>(osg::Quat(kAngle1, osg::Vec3d(0.0, 0.0, 0.0)), vsg::dquat()));
        EXPECT_TRUE(vecExact<4>(vsg::dquat(kAngle1, vsg::dvec3(0.0, 0.0, 0.0)), vsg::dquat()));
    }

    // OSG a * b equals VSG a * b: the same formula, in the same order. Do NOT swap the operands the way matrix products
    // must be swapped. Quat:208, quat.h:203.
    TEST(QuatConventions, multiplicationIsTheSameExpression)
    {
        EXPECT_TRUE(vecExact<4>(oQ1 * oQ2, vQ1 * vQ2));
        EXPECT_TRUE(vecExact<4>(oQ2 * oQ1, vQ2 * vQ1));
        EXPECT_FALSE(vecNear<4>(oQ1 * oQ2, vQ2 * vQ1));
    }

    // TRAP: both libraries define a * b so that a applies FIRST: (a * b) * v equals b * (a * v). This is the reverse of
    // Hamilton's product and of what vsg::rotate(a) * vsg::rotate(b) means.
    // OSG (a * b) * v equals VSG (a * b) * v equals b * (a * v).
    TEST(QuatConventions, leftOperandAppliesFirst)
    {
        const vsg::dvec3 v(1.0, 2.0, 3.0);
        EXPECT_TRUE(vecNear<3>((vQ1 * vQ2) * v, vQ2 * (vQ1 * v)));
        EXPECT_FALSE(vecNear<3>((vQ1 * vQ2) * v, vQ1 * (vQ2 * v)));

        const osg::Vec3d o(1.0, 2.0, 3.0);
        EXPECT_TRUE(vecNear<3>((oQ1 * oQ2) * o, oQ2 * (oQ1 * o)));
    }

    // TRAP: quat products keep their order but the matching matrix product flips.
    // VSG rotate(a * b) equals VSG rotate(b) * rotate(a); OSG Matrix(a * b) equals OSG Matrix(a) * Matrix(b).
    TEST(QuatConventions, quatOrderAgainstMatrixOrder)
    {
        EXPECT_TRUE(matNear(vsg::rotate(vQ1 * vQ2), vsg::rotate(vQ2) * vsg::rotate(vQ1)));
        EXPECT_FALSE(matNear(vsg::rotate(vQ1 * vQ2), vsg::rotate(vQ1) * vsg::rotate(vQ2)));
        EXPECT_TRUE(matNear(osg::Matrixd::rotate(oQ1 * oQ2), osg::Matrixd::rotate(oQ1) * osg::Matrixd::rotate(oQ2)));
    }

    // OSG Quat(a1, x1, a2, x2, a3, x3) equals VSG dquat(a1, x1) * dquat(a2, x2) * dquat(a3, x3). Quat.cpp:96.
    TEST(QuatConventions, threeAngleConstructorHasNoVsgForm)
    {
        const osg::Vec3d oX(1.0, 0.0, 0.0);
        const osg::Vec3d oY(0.0, 1.0, 0.0);
        const osg::Vec3d oZ(0.0, 0.0, 1.0);
        const vsg::dvec3 vX(1.0, 0.0, 0.0);
        const vsg::dvec3 vY(0.0, 1.0, 0.0);
        const vsg::dvec3 vZ(0.0, 0.0, 1.0);
        const double a1 = opaque(0.3);
        const double a2 = opaque(0.5);
        const double a3 = opaque(0.7);
        EXPECT_TRUE(vecExact<4>(osg::Quat(a1, oX, a2, oY, a3, oZ),
            vsg::dquat(a1, vX) * vsg::dquat(a2, vY) * vsg::dquat(a3, vZ)));
    }

    // OSG q * Vec3d equals VSG q * dvec3. Same formula (Quat:391, quat.h:215).
    TEST(QuatConventions, rotateDoubleVector)
    {
        EXPECT_TRUE(vecExact<3>(oQ1 * osg::Vec3d(1.5, -2.25, 3.75), vQ1 * vsg::dvec3(1.5, -2.25, 3.75)));
    }

    // TRAP: OSG q * Vec3f narrows the quat to float and does the whole rotation in float (Quat:378-388). VSG dquat * vec3
    // does not compile; the tempting vec3(q * dvec3(v)) computes in double and gives different low bits.
    // OSG q * Vec3f equals VSG quat(q) * vec3 exactly, and vec3(q * dvec3(v)) only to tolerance.
    TEST(QuatConventions, rotateFloatVector)
    {
        const osg::Vec3f o(1.5f, -2.25f, 3.75f);
        const vsg::vec3 v(1.5f, -2.25f, 3.75f);
        EXPECT_TRUE(vecExact<3>(oQ1 * o, vsg::quat(vQ1) * v));
        EXPECT_TRUE(vecNear<3>(oQ1 * o, vsg::vec3(vQ1 * vsg::dvec3(v))));
    }

    // TRAP: a float quat in a composition is not the same thing. Storing vsg::quat instead of dquat drops precision that
    // OSG kept between products.
    // OSG (a * b) equals VSG dquat a * b exactly; VSG quat(a) * quat(b) is a different number.
    TEST(QuatConventions, floatQuatChangesCompositionResults)
    {
        const vsg::quat narrowed = vsg::quat(vQ1) * vsg::quat(vQ2);
        const osg::Quat exact = oQ1 * oQ2;
        EXPECT_FALSE(vecExact<4>(exact, narrowed));
    }

    // OSG q * s and q + r equal the same VSG expressions. q / s equals q / s: both multiply by a reciprocal.
    // Quat:192, 232, 263, 280; quat.h:178, 196, 228, 234.
    TEST(QuatConventions, scalarAndComponentOperators)
    {
        EXPECT_TRUE(vecExact<4>(oQ1 * 0.3, vQ1 * 0.3));
        EXPECT_TRUE(vecExact<4>(oQ1 / 0.3, vQ1 / 0.3));
        EXPECT_TRUE(vecExact<4>(oQ1 + oQ2, vQ1 + vQ2));
        EXPECT_TRUE(vecExact<4>(oQ1 - oQ2, vQ1 - vQ2));
    }

    // OSG q *= r equals VSG q = q * r. VSG quats have no compound assignment. Quat:217.
    TEST(QuatConventions, compoundMultiply)
    {
        osg::Quat o = oQ1;
        o *= oQ2;
        vsg::dquat v = vQ1;
        v = v * vQ2;
        EXPECT_TRUE(vecExact<4>(o, v));
    }

    // TRAP: unary minus. OSG -q negates all four elements, the same rotation (Quat:298). VSG -q is the conjugate, the
    // INVERSE rotation (quat.h:190-193).
    // OSG -q equals VSG q * -1.0; OSG q.conj() equals VSG -q and conjugate(q).
    TEST(QuatConventions, unaryMinusIsNegationInOsgAndConjugateInVsg)
    {
        EXPECT_TRUE(vecExact<4>(-oQ1, vQ1 * -1.0));
        EXPECT_TRUE(vecExact<4>(oQ1.conj(), -vQ1));
        EXPECT_TRUE(vecExact<4>(oQ1.conj(), vsg::conjugate(vQ1)));
        EXPECT_FALSE(vecNear<4>(-oQ1, -vQ1));
    }

    // OSG q.length() equals VSG length(q). OSG q.length2() equals VSG dot(q, q). Quat:304, 310, quat.h:241, 254.
    TEST(QuatConventions, length)
    {
        const osg::Quat o(1.0, 2.0, 3.0, 4.0);
        const vsg::dquat v(1.0, 2.0, 3.0, 4.0);
        EXPECT_EQ(o.length(), vsg::length(v));
        EXPECT_EQ(o.length2(), vsg::dot(v, v));
    }

    // TRAP: inverse of a non-unit quat. OSG divides the conjugate by length2 (Quat:322); VSG by length (quat.h:260-265),
    // which is not the inverse. They agree only for unit quats.
    // OSG q.inverse() equals VSG conjugate(q) / dot(q, q) exactly.
    TEST(QuatConventions, inverse)
    {
        const osg::Quat oNonUnit(0.0, 0.0, 1.0, 1.0);
        const vsg::dquat vNonUnit(0.0, 0.0, 1.0, 1.0);

        EXPECT_TRUE(vecExact<4>(oNonUnit.inverse(), vsg::conjugate(vNonUnit) / vsg::dot(vNonUnit, vNonUnit)));
        EXPECT_TRUE(vecExact<4>(oNonUnit.inverse(), vsg::dquat(0.0, 0.0, -0.5, 0.5)));
        EXPECT_FALSE(vecNear<4>(oNonUnit.inverse(), vsg::inverse(vNonUnit)));

        EXPECT_TRUE(vecNear<4>(oQ1.inverse(), vsg::inverse(vQ1)));
        EXPECT_TRUE(vecNear<4>(oQ1 * oQ1.inverse(), vsg::dquat()));
    }

    // OSG q.makeRotate(from, to) equals VSG dquat(from, to) within 1e-6 for vectors at a clear angle, normalized or not.
    // Different methods: half-angle without acos (Quat.cpp:199-204) against acos (quat.h:130-139).
    TEST(QuatConventions, fromToAtAClearAngle)
    {
        const osg::Vec3d oFrom(1.0, 2.0, 3.0);
        const osg::Vec3d oTo(-2.0, 0.5, 4.0);
        const vsg::dvec3 vFrom(1.0, 2.0, 3.0);
        const vsg::dvec3 vTo(-2.0, 0.5, 4.0);

        osg::Quat o;
        o.makeRotate(oFrom, oTo);
        EXPECT_TRUE(vecNear<4>(o, vsg::dquat(vFrom, vTo)));
    }

    // TRAP: OSG makeRotate(Vec3f, Vec3f) widens and works in double (Quat.cpp:99-102). VSG's vsg::quat(vec3, vec3) works in
    // float. OSG result equals VSG dquat(dvec3(from), dvec3(to)) to tolerance, not quat(from, to).
    TEST(QuatConventions, fromToWithFloatVectorsUsesDouble)
    {
        const osg::Vec3f oFrom(0.1f, 0.7f, -0.3f);
        const osg::Vec3f oTo(-0.9f, 0.2f, 0.4f);
        const vsg::vec3 vFrom(0.1f, 0.7f, -0.3f);
        const vsg::vec3 vTo(-0.9f, 0.2f, 0.4f);

        osg::Quat o;
        o.makeRotate(oFrom, oTo);
        EXPECT_TRUE(vecNear<4>(o, vsg::dquat(vsg::dvec3(vFrom), vsg::dvec3(vTo))));
    }

    // TRAP: parallel vectors can make dquat(from, to) NaN. Rounding can leave dot / div just above 1, and quat.h:130 takes
    // acos of it without a clamp. OSG returns a near-identity quat. The input below gives dot / div - 1 = 2.2e-16.
    // Real call sites pass an unknown direction, for example apps/openmw/mwworld/weather.cpp:711 (storm direction) and
    // apps/openmw/mwrender/actorikcontroller.cpp:527 (ground normal). The helper uses OSG's algorithm.
    TEST(QuatConventions, parallelVectorsCanMakeVsgNan)
    {
        const double x = 0.1;
        const double y = 1.0 / 7.0;
        const double z = 1.0;
        const double k = 1.7;

        osg::Quat o;
        o.makeRotate(osg::Vec3d(x, y, z), osg::Vec3d(x * k, y * k, z * k));
        EXPECT_TRUE(vecNear<4>(o, vsg::dquat()));

        const vsg::dquat v(vsg::dvec3(x, y, z), vsg::dvec3(x * k, y * k, z * k));
        EXPECT_TRUE(std::isnan(v.w));
    }

    // Equal vectors stay safe in both: sqrt(x * x) is exactly x, so VSG's acos sees exactly 1.
    TEST(QuatConventions, equalVectorsAreIdentity)
    {
        osg::Quat o;
        o.makeRotate(osg::Vec3d(1.0, 2.0, 3.0), osg::Vec3d(1.0, 2.0, 3.0));
        EXPECT_TRUE(vecNear<4>(o, vsg::dquat()));
        EXPECT_TRUE(vecNear<4>(vsg::dquat(vsg::dvec3(1.0, 2.0, 3.0), vsg::dvec3(1.0, 2.0, 3.0)), vsg::dquat()));
    }

    // TRAP: opposite vectors. Each library maps from to to, but picks a different half-turn axis (Quat.cpp:169-192 against
    // vec3.h:279-294 through quat.h:121), so other vectors end up in different places.
    // OSG makeRotate((0,0,1), (0,0,-1)) is (0, 1, 0, 0); VSG dquat gives (-1, 0, 0, ~0). The helper keeps OSG's axis.
    TEST(QuatConventions, oppositeVectorsChooseDifferentAxes)
    {
        const osg::Vec3d oFrom(0.0, 0.0, 1.0);
        const osg::Vec3d oTo(0.0, 0.0, -1.0);
        const vsg::dvec3 vFrom(0.0, 0.0, 1.0);
        const vsg::dvec3 vTo(0.0, 0.0, -1.0);

        osg::Quat o;
        o.makeRotate(oFrom, oTo);
        const vsg::dquat v(vFrom, vTo);

        EXPECT_TRUE(vecNear<4>(o, vsg::dquat(0.0, 1.0, 0.0, 0.0)));
        EXPECT_TRUE(vecNear<4>(v, vsg::dquat(-1.0, 0.0, 0.0, 0.0)));

        EXPECT_TRUE(vecNear<3>(o * oFrom, oTo));
        EXPECT_TRUE(vecNear<3>(v * vFrom, vTo));

        const osg::Vec3d oOther = o * osg::Vec3d(1.0, 0.0, 0.0);
        const vsg::dvec3 vOther = v * vsg::dvec3(1.0, 0.0, 0.0);
        EXPECT_FALSE(vecNear<3>(oOther, vOther));
    }

    // Pins for the helper's getRotate. OSG q.getRotate(angle, axis) (Quat.cpp:289-306) equals, in VSG primitives,
    //   sinHalf = length(dvec3(q.x, q.y, q.z)); angle = 2 * atan2(sinHalf, q.w); axis = dvec3(q.x, q.y, q.z) / sinHalf,
    // with axis (0, 0, 1) when sinHalf is 0. Angle comes out in [0, 2 pi): a negative input angle returns a flipped axis.
    TEST(QuatConventions, getRotatePins)
    {
        double angle = 0.0;
        osg::Vec3d axis;

        osg::Quat(1.25, osg::Vec3d(0.0, 2.0, 0.0)).getRotate(angle, axis);
        EXPECT_NEAR(angle, 1.25, 1e-12);
        EXPECT_TRUE(vecNear<3>(axis, vsg::dvec3(0.0, 1.0, 0.0)));

        osg::Quat(-1.25, osg::Vec3d(0.0, 1.0, 0.0)).getRotate(angle, axis);
        EXPECT_NEAR(angle, 1.25, 1e-12);
        EXPECT_TRUE(vecNear<3>(axis, vsg::dvec3(0.0, -1.0, 0.0)));

        osg::Quat().getRotate(angle, axis);
        EXPECT_EQ(angle, 0.0);
        EXPECT_TRUE(vecExact<3>(axis, vsg::dvec3(0.0, 0.0, 1.0)));

        const double sinHalf = vsg::length(vsg::dvec3(vQ1.x, vQ1.y, vQ1.z));
        oQ1.getRotate(angle, axis);
        EXPECT_EQ(angle, 2.0 * std::atan2(sinHalf, vQ1.w));
        EXPECT_TRUE(vecExact<3>(axis, vsg::dvec3(vQ1.x / sinHalf, vQ1.y / sinHalf, vQ1.z / sinHalf)));
    }

    // TRAP: argument order. OSG result.slerp(t, from, to) equals VSG result = mix(from, to, t), to tolerance.
    // Thresholds for the near-parallel fallback to lerp differ: 1e-5 in OSG (Quat.cpp:315), epsilon in VSG (quat.h:280).
    TEST(QuatConventions, slerp)
    {
        for (const double t : { 0.0, 0.25, 0.5, 0.9, 1.0 })
        {
            osg::Quat o;
            o.slerp(t, oQ1, oQ2);
            EXPECT_TRUE(vecNear<4>(o, vsg::mix(vQ1, vQ2, t))) << "t = " << t;
        }
    }

    // Both flip the end quat to take the short way round when the dot is negative. Quat.cpp:323, quat.h:271.
    TEST(QuatConventions, slerpTakesTheShortWay)
    {
        const osg::Quat oTo = oQ1 * osg::Quat(0.2, osg::Vec3d(0.0, 0.0, 1.0));
        const osg::Quat oNegatedTo = -oTo;
        const vsg::dquat vTo = vQ1 * vsg::dquat(0.2, vsg::dvec3(0.0, 0.0, 1.0));
        const vsg::dquat vNegatedTo = vTo * -1.0;

        osg::Quat o;
        o.slerp(0.4, oQ1, oNegatedTo);
        EXPECT_TRUE(vecNear<4>(o, vsg::mix(vQ1, vNegatedTo, 0.4)));
        EXPECT_GT(vsg::dot(vsg::mix(vQ1, vNegatedTo, 0.4), vQ1), 0.0);
    }

    // Within OSG's lerp band (1 - cos < 1e-5) the two methods still agree to tolerance.
    TEST(QuatConventions, slerpOfNearlyEqualQuats)
    {
        const osg::Quat oTo = oQ1 * osg::Quat(1e-3, osg::Vec3d(0.0, 0.0, 1.0));
        const vsg::dquat vTo = vQ1 * vsg::dquat(1e-3, vsg::dvec3(0.0, 0.0, 1.0));

        osg::Quat o;
        o.slerp(0.3, oQ1, oTo);
        EXPECT_TRUE(vecNear<4>(o, vsg::mix(vQ1, vTo, 0.3)));
    }

    // OSG Matrixd::rotate(q) equals VSG rotate(q) for a unit quat, in the same storage. The same rotation applied as OSG
    // v * M, VSG M * v, and q * v in both. Matrix_implementation.cpp:120-130, transform.h:37-40.
    TEST(QuatConventions, toMatrix)
    {
        EXPECT_TRUE(matNear(osg::Matrixd::rotate(oQ1), vsg::rotate(vQ1)));

        const osg::Vec3d o(1.5, -2.25, 3.75);
        const vsg::dvec3 v(1.5, -2.25, 3.75);
        EXPECT_TRUE(vecNear<3>(o * osg::Matrixd::rotate(oQ1), oQ1 * o));
        EXPECT_TRUE(vecNear<3>(vsg::rotate(vQ1) * v, vQ1 * v));
    }

    // OSG Matrixf::rotate(q) computes in double and narrows (Matrix_implementation.cpp:79-130).
    // It equals VSG mat4(rotate(dquat)) to tolerance. rotate(quat(q)) also works but computes in float.
    TEST(QuatConventions, toFloatMatrix)
    {
        EXPECT_TRUE(matNear(osg::Matrixf::rotate(oQ1), vsg::mat4(vsg::rotate(vQ1))));
    }

    // TRAP: OSG normalizes a non-unit quat when making a matrix (2 / length2, Matrix_implementation.cpp:83-86).
    // vsg::rotate assumes unit length (transform.h:37-40).
    // OSG Matrixd::rotate(q) equals VSG rotate(normalize(q)), not rotate(q).
    TEST(QuatConventions, nonUnitQuatToMatrix)
    {
        const osg::Quat o(0.0, 0.0, 1.0, 1.0);
        const vsg::dquat v(0.0, 0.0, 1.0, 1.0);

        EXPECT_TRUE(matNear(osg::Matrixd::rotate(o), vsg::rotate(vsg::normalize(v))));
        EXPECT_FALSE(matNear(osg::Matrixd::rotate(o), vsg::rotate(v)));
    }

    // Pin for the helper. A zero quat gives OSG a zero rotation block (Matrix_implementation.cpp:65-75) and VSG the
    // identity (transform.h:37-40); vsg::normalize of it is NaN.
    TEST(QuatConventions, zeroQuatToMatrix)
    {
        const osg::Matrixd o = osg::Matrixd::rotate(osg::Quat(0.0, 0.0, 0.0, 0.0));
        EXPECT_EQ(o(0, 0), 0.0);
        EXPECT_EQ(o(3, 3), 1.0);
        EXPECT_EQ(vsg::rotate(vsg::dquat(0.0, 0.0, 0.0, 0.0))(0, 0), 1.0);
    }

    // TRAP: matrix to quat. OSG Matrix::getRotate picks the sign from the largest of four traces
    // (Matrix_implementation.cpp:160-205); VSG's only route, decompose, makes w positive when the trace is positive
    // (maths_transform.cpp:258-265). The result is the same rotation with opposite signs for this input, which matters
    // to anything reading components (Lua, slerp's start).
    // OSG M.getRotate() of (0.8, 0, 0, -0.6) is (0.8, 0, 0, -0.6); VSG decompose gives (-0.8, 0, 0, 0.6).
    TEST(QuatConventions, matrixToQuatSign)
    {
        const osg::Quat oInput(0.8, 0.0, 0.0, -0.6);
        const vsg::dquat vInput(0.8, 0.0, 0.0, -0.6);

        const osg::Quat oResult = osg::Matrixd::rotate(oInput).getRotate();
        EXPECT_TRUE(vecNear<4>(oResult, vInput));

        vsg::dvec3 translation;
        vsg::dvec3 scale;
        vsg::dquat vResult;
        ASSERT_TRUE(vsg::decompose(vsg::rotate(vInput), translation, vResult, scale));
        EXPECT_TRUE(vecNear<4>(vResult, vsg::dquat(-0.8, 0.0, 0.0, 0.6)));

        EXPECT_TRUE(matNear(vsg::rotate(vResult), vsg::rotate(vInput)));
    }

    // The common case round-trips in both: the largest-trace branch is w, and w is positive.
    TEST(QuatConventions, matrixToQuatRoundTrip)
    {
        const vsg::dquat input(0.1, 0.2, 0.3, std::sqrt(1.0 - 0.14));
        const osg::Quat oInput(0.1, 0.2, 0.3, std::sqrt(1.0 - 0.14));

        EXPECT_TRUE(vecNear<4>(osg::Matrixd::rotate(oInput).getRotate(), input));

        vsg::dvec3 translation;
        vsg::dvec3 scale;
        vsg::dquat result;
        ASSERT_TRUE(vsg::decompose(vsg::rotate(input), translation, result, scale));
        EXPECT_TRUE(vecNear<4>(result, input));
    }

    // TRAP: OSG getRotate on a scaled matrix does not remove the scale (documented at Matrixf:337-340), while
    // vsg::decompose does (maths_transform.cpp:244-255). Decompose is not a behaviour-preserving replacement for a call
    // site whose matrix carries scale. Pin: scale 2 with no rotation gives (0, 0, 0, sqrt(7) / 2), not the identity.
    TEST(QuatConventions, getRotateOfScaledMatrixKeepsTheScaleError)
    {
        const osg::Quat o = osg::Matrixd::scale(2.0, 2.0, 2.0).getRotate();
        EXPECT_TRUE(vecNear<4>(o, vsg::dquat(0.0, 0.0, 0.0, std::sqrt(7.0) / 2.0)));

        vsg::dvec3 translation;
        vsg::dvec3 scale;
        vsg::dquat v;
        ASSERT_TRUE(vsg::decompose(vsg::scale(2.0, 2.0, 2.0), translation, v, scale));
        EXPECT_TRUE(vecNear<4>(v, vsg::dquat()));
    }
}

#endif
