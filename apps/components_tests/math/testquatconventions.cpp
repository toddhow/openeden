#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

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
// TRAP (type): osg::Quat is DOUBLE (extern/osg/osg/Config:34 leaves OSG_USE_FLOAT_QUAT undefined; Quat:38). The
// matching type is vsg::dquat. The spec's goal line names vsg::quat, which is float and would change every composed
// rotation.
//
// HELPERS components/misc/vsgmath.hpp MUST PROVIDE (the implementer writes them; these tests do not use them):
//  - makeRotate(from, to): OSG's algorithm (Quat.cpp:118-206), computed in double. vsg::dquat(from, to) can return NaN
//  and
//    picks a different axis for opposite vectors.
//  - makeRotateFromAxis(angle, axis) with OSG's 1e-7 axis threshold (Quat.cpp:51-58). VSG's threshold is machine
//  epsilon.
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

#include "testquatconventions.golden.inc"

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

    const vsg::dquat vQ1(kAngle1, kVsgAxis1);
    const vsg::dquat vQ2(kAngle2, kVsgAxis2);

    // TEST: osg::Quat stores double; vsg::dquat is the type with the same arithmetic.
    TEST(QuatConventions, osgQuatIsDouble)
    {
        static_assert(std::is_same_v<vsg::dquat::value_type, double>);
    }

    // OSG Quat() equals VSG dquat(). Both are the identity with w last. Quat:46, quat.h:47.
    TEST(QuatConventions, defaultIsIdentity)
    {
        EXPECT_TRUE(vecExact<4>(vsg::dquat(0.0, 0.0, 0.0, 1.0), vsg::dquat()));
        EXPECT_TRUE(vecExact<4>(vsg::dquat(0.0, 0.0, 0.0, 1.0), vsg::dquat(0.0, 0.0, 0.0, 1.0)));
    }

    // OSG Quat(x, y, z, w) equals VSG dquat(x, y, z, w). Quat:48, quat.h:51.
    TEST(QuatConventions, componentOrderIsXyzw)
    {
        EXPECT_TRUE(vecExact<4>(vsg::dquat(0.1, 0.2, 0.3, 0.4), vsg::dquat(0.1, 0.2, 0.3, 0.4)));
    }

    // OSG Quat(angle, axis) equals VSG dquat(angle, axis). Same formula: Quat.cpp:61-68, quat.h:104-111.
    TEST(QuatConventions, fromAngleAndAxis)
    {
        EXPECT_TRUE(vecExact<4>(
            vsg::dquat(0.0916432938695913, 0.1832865877391826, 0.2749298816087739, 0.9393727128473789), vQ1));
        EXPECT_TRUE(vecExact<4>(
            vsg::dquat(0.7100072613615276, -0.1775018153403819, -0.3550036306807638, 0.5816830894638836), vQ2));
    }

    // A float axis is widened first in OSG (Quat.cpp:71-74).
    // OSG Quat(angle, Vec3f) equals VSG dquat(angle, dvec3(vec3)).
    TEST(QuatConventions, fromAngleAndFloatAxis)
    {
        const vsg::vec3 vAxis(0.1f, 0.7f, -0.3f);
        EXPECT_TRUE(
            vecExact<4>(vsg::dquat(0.04464149314506467, 0.31249044203730153, -0.13392448276124438, 0.9393727128473789),
                vsg::dquat(kAngle1, vsg::dvec3(vAxis))));
    }

    // TRAP: a very short axis. OSG gives the identity below length 1e-7 (Quat.cpp:51-58); VSG's limit for a dquat is
    // machine epsilon, about 2e-16 (quat.h:97, numbers.h:30), so it normalizes the short axis and rotates.
    // The helper keeps OSG's threshold.
    TEST(QuatConventions, shortAxisThresholdDiffers)
    {
        const vsg::dvec3 vAxis(1e-8, 0.0, 0.0);
        EXPECT_TRUE(vecExact<4>(vsg::dquat(0.0, 0.0, 0.0, 1.0), vsg::dquat()));

        const vsg::dquat rotated(kAngle1, vAxis);
        EXPECT_NEAR(rotated.x, std::sin(kAngle1 * 0.5), 1e-12);
        EXPECT_NEAR(rotated.w, std::cos(kAngle1 * 0.5), 1e-12);
    }

    // A zero axis gives the identity in both.
    TEST(QuatConventions, zeroAxisIsIdentity)
    {
        EXPECT_TRUE(vecExact<4>(vsg::dquat(0.0, 0.0, 0.0, 1.0), vsg::dquat()));
        EXPECT_TRUE(vecExact<4>(vsg::dquat(kAngle1, vsg::dvec3(0.0, 0.0, 0.0)), vsg::dquat()));
    }

    // OSG a * b equals VSG a * b: the same formula, in the same order. Do NOT swap the operands the way matrix products
    // must be swapped. Quat:208, quat.h:203.
    TEST(QuatConventions, multiplicationIsTheSameExpression)
    {
        EXPECT_TRUE(
            vecExact<4>(vsg::dquat(0.7365356525788512, -0.28786156755695186, -0.027157001472505826, 0.6114846258696311),
                vQ1 * vQ2));
        EXPECT_TRUE(vecExact<4>(
            vsg::dquat(0.7040019505276021, 0.1676102611605355, -0.31996031993374763, 0.6114846258696311), vQ2 * vQ1));
        EXPECT_FALSE(
            vecNear<4>(vsg::dquat(0.7365356525788512, -0.28786156755695186, -0.027157001472505826, 0.6114846258696311),
                vQ2 * vQ1));
    }

    // TRAP: both libraries define a * b so that a applies FIRST: (a * b) * v equals b * (a * v). This is the reverse of
    // Hamilton's product and of what vsg::rotate(a) * vsg::rotate(b) means.
    // OSG (a * b) * v equals VSG (a * b) * v equals b * (a * v).
    TEST(QuatConventions, leftOperandAppliesFirst)
    {
        const vsg::dvec3 v(1.0, 2.0, 3.0);
        EXPECT_TRUE(vecNear<3>((vQ1 * vQ2) * v, vQ2 * (vQ1 * v)));
        EXPECT_FALSE(vecNear<3>((vQ1 * vQ2) * v, vQ1 * (vQ2 * v)));

        EXPECT_TRUE(vecNear<3>(vsg::dvec3(-1.125010580570415, -3.2855184994479107, 1.3927380885831253),
            vsg::dvec3(-1.1250105805704145, -3.2855184994479094, 1.3927380885831258)));
    }

    // TRAP: quat products keep their order but the matching matrix product flips.
    // VSG rotate(a * b) equals VSG rotate(b) * rotate(a); OSG Matrix(a * b) equals OSG Matrix(a) * Matrix(b).
    TEST(QuatConventions, quatOrderAgainstMatrixOrder)
    {
        EXPECT_TRUE(matNear(vsg::rotate(vQ1 * vQ2), vsg::rotate(vQ2) * vsg::rotate(vQ1)));
        EXPECT_FALSE(matNear(vsg::rotate(vQ1 * vQ2), vsg::rotate(vQ1) * vsg::rotate(vQ2)));
        EXPECT_TRUE(
            matNear(matOf<double>(0.8327964303893536, -0.45725279279617365, 0.3120416462763427, 0.0,
                        -0.39082843725554867, -0.08644454049746364, 0.9163953699416908, 0.0, -0.39205004548289024,
                        -0.8851255418856029, -0.25069809919219943, 0.0, 0.0, 0.0, 0.0, 1.0),
                matOf<double>(0.8327964303893536, -0.45725279279617337, 0.3120416462763427, 0.0, -0.39082843725554856,
                    -0.0864445404974635, 0.9163953699416906, 0.0, -0.39205004548289013, -0.885125541885603,
                    -0.2506980991921992, 0.0, 0.0, 0.0, 0.0, 1.0)));
    }

    // OSG Quat(a1, x1, a2, x2, a3, x3) equals VSG dquat(a1, x1) * dquat(a2, x2) * dquat(a3, x3). Quat.cpp:96.
    TEST(QuatConventions, threeAngleConstructorHasNoVsgForm)
    {
        const vsg::dvec3 vX(1.0, 0.0, 0.0);
        const vsg::dvec3 vY(0.0, 1.0, 0.0);
        const vsg::dvec3 vZ(0.0, 0.0, 1.0);
        const double a1 = opaque(0.3);
        const double a2 = opaque(0.5);
        const double a3 = opaque(0.7);
        EXPECT_TRUE(
            vecExact<4>(vsg::dquat(0.052132410889547995, 0.2794438940784743, 0.29377717233096856, 0.9126271389863014),
                vsg::dquat(a1, vX) * vsg::dquat(a2, vY) * vsg::dquat(a3, vZ)));
    }

    // OSG q * Vec3d equals VSG q * dvec3. Same formula (Quat:391, quat.h:215).
    TEST(QuatConventions, rotateDoubleVector)
    {
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(3.739323893494264, -1.3146138277534245, 2.3799679206708615),
            vQ1 * vsg::dvec3(1.5, -2.25, 3.75)));
    }

    // TRAP: OSG q * Vec3f narrows the quat to float and does the whole rotation in float (Quat:378-388). VSG dquat *
    // vec3 does not compile; the tempting vec3(q * dvec3(v)) computes in double and gives different low bits. OSG q *
    // Vec3f equals VSG quat(q) * vec3 exactly, and vec3(q * dvec3(v)) only to tolerance.
    TEST(QuatConventions, rotateFloatVector)
    {
        const vsg::vec3 v(1.5f, -2.25f, 3.75f);
        EXPECT_TRUE(vecExact<3>(vsg::vec3(3.73932409f, -1.31461382f, 2.37996793f), vsg::quat(vQ1) * v));
        EXPECT_TRUE(vecNear<3>(vsg::vec3(3.73932409f, -1.31461382f, 2.37996793f), vsg::vec3(vQ1 * vsg::dvec3(v))));
    }

    // TRAP: a float quat in a composition is not the same thing. Storing vsg::quat instead of dquat drops precision
    // that OSG kept between products. OSG (a * b) equals VSG dquat a * b exactly; VSG quat(a) * quat(b) is a different
    // number.
    TEST(QuatConventions, floatQuatChangesCompositionResults)
    {
        const vsg::quat narrowed = vsg::quat(vQ1) * vsg::quat(vQ2);
        EXPECT_FALSE(vecBitExact<4>(
            vsg::dquat(0.7365356525788512, -0.28786156755695186, -0.027157001472505826, 0.6114846258696311), narrowed));
    }

    // OSG q * s and q + r equal the same VSG expressions. q / s equals q / s: both multiply by a reciprocal.
    // Quat:192, 232, 263, 280; quat.h:178, 196, 228, 234.
    TEST(QuatConventions, scalarAndComponentOperators)
    {
        EXPECT_TRUE(vecExact<4>(
            vsg::dquat(0.02749298816087739, 0.05498597632175478, 0.08247896448263217, 0.28181181385421367), vQ1 * 0.3));
        EXPECT_TRUE(vecExact<4>(
            vsg::dquat(0.30547764623197105, 0.6109552924639421, 0.916432938695913, 3.1312423761579296), vQ1 / 0.3));
        EXPECT_TRUE(vecExact<4>(
            vsg::dquat(0.8016505552311188, 0.005784772398800708, -0.08007374907198989, 1.5210558023112624), vQ1 + vQ2));
        EXPECT_TRUE(vecExact<4>(
            vsg::dquat(-0.6183639674919363, 0.3607884030795645, 0.6299335122895378, 0.3576896233834953), vQ1 - vQ2));
    }

    // OSG q *= r equals VSG q = q * r. VSG quats have no compound assignment. Quat:217.
    TEST(QuatConventions, compoundMultiply)
    {
        vsg::dquat v = vQ1;
        v = v * vQ2;
        EXPECT_TRUE(vecExact<4>(
            vsg::dquat(0.7365356525788512, -0.28786156755695186, -0.027157001472505826, 0.6114846258696311), v));
    }

    // TRAP: unary minus. OSG -q negates all four elements, the same rotation (Quat:298). VSG -q is the conjugate, the
    // INVERSE rotation (quat.h:190-193).
    // OSG -q equals VSG q * -1.0; OSG q.conj() equals VSG -q and conjugate(q).
    TEST(QuatConventions, unaryMinusIsNegationInOsgAndConjugateInVsg)
    {
        EXPECT_TRUE(
            vecExact<4>(vsg::dquat(-0.0916432938695913, -0.1832865877391826, -0.2749298816087739, -0.9393727128473789),
                vQ1 * -1.0));
        EXPECT_TRUE(vecExact<4>(
            vsg::dquat(-0.0916432938695913, -0.1832865877391826, -0.2749298816087739, 0.9393727128473789), -vQ1));
        EXPECT_TRUE(
            vecExact<4>(vsg::dquat(-0.0916432938695913, -0.1832865877391826, -0.2749298816087739, 0.9393727128473789),
                vsg::conjugate(vQ1)));
        EXPECT_FALSE(vecNear<4>(
            vsg::dquat(-0.0916432938695913, -0.1832865877391826, -0.2749298816087739, -0.9393727128473789), -vQ1));
    }

    // OSG q.length() equals VSG length(q). OSG q.length2() equals VSG dot(q, q). Quat:304, 310, quat.h:241, 254.
    TEST(QuatConventions, length)
    {
        const vsg::dquat v(1.0, 2.0, 3.0, 4.0);
        EXPECT_TRUE(recordedEqual(5.477225575051661, vsg::length(v)));
        EXPECT_TRUE(recordedEqual(30.0, vsg::dot(v, v)));
    }

    // TRAP: inverse of a non-unit quat. OSG divides the conjugate by length2 (Quat:322); VSG by length
    // (quat.h:260-265), which is not the inverse. They agree only for unit quats. OSG q.inverse() equals VSG
    // conjugate(q) / dot(q, q) exactly.
    TEST(QuatConventions, inverse)
    {
        const vsg::dquat vNonUnit(0.0, 0.0, 1.0, 1.0);

        EXPECT_TRUE(
            vecExact<4>(vsg::dquat(-0.0, -0.0, -0.5, 0.5), vsg::conjugate(vNonUnit) / vsg::dot(vNonUnit, vNonUnit)));
        EXPECT_TRUE(vecExact<4>(vsg::dquat(-0.0, -0.0, -0.5, 0.5), vsg::dquat(0.0, 0.0, -0.5, 0.5)));
        EXPECT_FALSE(vecNear<4>(vsg::dquat(-0.0, -0.0, -0.5, 0.5), vsg::inverse(vNonUnit)));

        EXPECT_TRUE(
            vecNear<4>(vsg::dquat(-0.0916432938695913, -0.1832865877391826, -0.2749298816087739, 0.9393727128473789),
                vsg::inverse(vQ1)));
        EXPECT_TRUE(vecNear<4>(vsg::dquat(0.0, -6.938893903907228e-18, 0.0, 1.0), vsg::dquat()));
    }

    // OSG q.makeRotate(from, to) equals VSG dquat(from, to) within 1e-6 for vectors at a clear angle, normalized or
    // not. Different methods: half-angle without acos (Quat.cpp:199-204) against acos (quat.h:130-139).
    TEST(QuatConventions, fromToAtAClearAngle)
    {
        const vsg::dvec3 vFrom(1.0, 2.0, 3.0);
        const vsg::dvec3 vTo(-2.0, 0.5, 4.0);

        EXPECT_TRUE(
            vecNear<4>(vsg::dquat(0.2122977071448034, -0.3266118571458514, 0.14697533571563312, 0.909204393414415),
                vsg::dquat(vFrom, vTo)));
    }

    // TRAP: OSG makeRotate(Vec3f, Vec3f) widens and works in double (Quat.cpp:99-102). VSG's vsg::quat(vec3, vec3)
    // works in float. OSG result equals VSG dquat(dvec3(from), dvec3(to)) to tolerance, not quat(from, to).
    TEST(QuatConventions, fromToWithFloatVectorsUsesDouble)
    {
        const vsg::vec3 vFrom(0.1f, 0.7f, -0.3f);
        const vsg::vec3 vTo(-0.9f, 0.2f, 0.4f);

        EXPECT_TRUE(
            vecNear<4>(vsg::dquat(0.3266020179998497, 0.22093665978249097, 0.6243861801912336, 0.6742848153330921),
                vsg::dquat(vsg::dvec3(vFrom), vsg::dvec3(vTo))));
    }

    // TRAP: parallel vectors can make dquat(from, to) NaN. Rounding can leave dot / div just above 1, and quat.h:130
    // takes acos of it without a clamp. OSG returns a near-identity quat. The input below gives dot / div - 1
    // = 2.2e-16. Real call sites pass an unknown direction, for example apps/openmw/mwworld/weather.cpp:711 (storm
    // direction) and apps/openmw/mwrender/actorikcontroller.cpp:527 (ground normal). The helper uses OSG's algorithm.
    TEST(QuatConventions, parallelVectorsCanMakeVsgNan)
    {
        const double x = 0.1;
        const double y = 1.0 / 7.0;
        const double z = 1.0;
        const double k = 1.7;

        EXPECT_TRUE(vecNear<4>(
            vsg::dquat(1.3877787807814457e-17, 6.938893903907228e-18, -8.673617379884035e-19, 1.0), vsg::dquat()));

        const vsg::dquat v(vsg::dvec3(x, y, z), vsg::dvec3(x * k, y * k, z * k));
        EXPECT_TRUE(std::isnan(v.w));
    }

    // Equal vectors stay safe in both: sqrt(x * x) is exactly x, so VSG's acos sees exactly 1.
    TEST(QuatConventions, equalVectorsAreIdentity)
    {
        EXPECT_TRUE(vecNear<4>(vsg::dquat(0.0, 0.0, 0.0, 1.0), vsg::dquat()));
        EXPECT_TRUE(vecNear<4>(vsg::dquat(vsg::dvec3(1.0, 2.0, 3.0), vsg::dvec3(1.0, 2.0, 3.0)), vsg::dquat()));
    }

    // TRAP: opposite vectors. Each library maps from to to, but picks a different half-turn axis (Quat.cpp:169-192
    // against vec3.h:279-294 through quat.h:121), so other vectors end up in different places. OSG makeRotate((0,0,1),
    // (0,0,-1)) is (0, 1, 0, 0); VSG dquat gives (-1, 0, 0, ~0). The helper keeps OSG's axis.
    TEST(QuatConventions, oppositeVectorsChooseDifferentAxes)
    {
        const vsg::dvec3 vFrom(0.0, 0.0, 1.0);
        const vsg::dvec3 vTo(0.0, 0.0, -1.0);

        const vsg::dquat v(vFrom, vTo);

        EXPECT_TRUE(vecNear<4>(vsg::dquat(0.0, 1.0, -0.0, 0.0), vsg::dquat(0.0, 1.0, 0.0, 0.0)));
        EXPECT_TRUE(vecNear<4>(v, vsg::dquat(-1.0, 0.0, 0.0, 0.0)));

        EXPECT_TRUE(vecNear<3>(vsg::dvec3(0.0, 0.0, -1.0), vsg::dvec3(0.0, 0.0, -1.0)));
        EXPECT_TRUE(vecNear<3>(v * vFrom, vTo));

        const vsg::dvec3 vOther = v * vsg::dvec3(1.0, 0.0, 0.0);
        EXPECT_FALSE(vecNear<3>(vsg::dvec3(-1.0, 0.0, 0.0), vOther));
    }

    // Pins for the helper's getRotate. OSG q.getRotate(angle, axis) (Quat.cpp:289-306) equals, in VSG primitives,
    //   sinHalf = length(dvec3(q.x, q.y, q.z)); angle = 2 * atan2(sinHalf, q.w); axis = dvec3(q.x, q.y, q.z) / sinHalf,
    // with axis (0, 0, 1) when sinHalf is 0. Angle comes out in [0, 2 pi): a negative input angle returns a flipped
    // axis.
    TEST(QuatConventions, getRotatePins)
    {

        EXPECT_NEAR(1.25, 1.25, 1e-12);
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(0.0, 1.0, 0.0), vsg::dvec3(0.0, 1.0, 0.0)));

        EXPECT_NEAR(1.25, 1.25, 1e-12);
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(-0.0, -1.0, -0.0), vsg::dvec3(0.0, -1.0, 0.0)));

        EXPECT_TRUE(recordedEqual(0.0, 0.0));
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(0.0, 0.0, 1.0), vsg::dvec3(0.0, 0.0, 1.0)));

        const double sinHalf = vsg::length(vsg::dvec3(vQ1.x, vQ1.y, vQ1.z));
        EXPECT_TRUE(recordedEqual(0.7, 2.0 * std::atan2(sinHalf, vQ1.w)));
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(0.2672612419124244, 0.5345224838248488, 0.8017837257372732),
            vsg::dvec3(vQ1.x / sinHalf, vQ1.y / sinHalf, vQ1.z / sinHalf)));
    }

    // TRAP: argument order. OSG result.slerp(t, from, to) equals VSG result = mix(from, to, t), to tolerance.
    // Thresholds for the near-parallel fallback to lerp differ: 1e-5 in OSG (Quat.cpp:315), epsilon in VSG
    // (quat.h:280).
    TEST(QuatConventions, slerp)
    {
        for (const double t : { 0.0, 0.25, 0.5, 0.9, 1.0 })
        {
            EXPECT_TRUE(vecNear<4>(MathConventions::golden(kGolden52), vsg::mix(vQ1, vQ2, t))) << "t = " << t;
        }
    }

    // Both flip the end quat to take the short way round when the dot is negative. Quat.cpp:323, quat.h:271.
    TEST(QuatConventions, slerpTakesTheShortWay)
    {
        const vsg::dquat vTo = vQ1 * vsg::dquat(0.2, vsg::dvec3(0.0, 0.0, 1.0));
        const vsg::dquat vNegatedTo = vTo * -1.0;

        EXPECT_TRUE(
            vecNear<4>(vsg::dquat(0.08424048040022897, 0.18680473232303296, 0.3122748563676359, 0.9276270519579682),
                vsg::mix(vQ1, vNegatedTo, 0.4)));
        EXPECT_GT(vsg::dot(vsg::mix(vQ1, vNegatedTo, 0.4), vQ1), 0.0);
    }

    // Within OSG's lerp band (1 - cos < 1e-5) the two methods still agree to tolerance.
    TEST(QuatConventions, slerpOfNearlyEqualQuats)
    {
        const vsg::dquat vTo = vQ1 * vsg::dquat(1e-3, vsg::dvec3(0.0, 0.0, 1.0));

        EXPECT_TRUE(
            vecNear<4>(vsg::dquat(0.09161579744595252, 0.18330032735944338, 0.2750707771999596, 0.9393314381403799),
                vsg::mix(vQ1, vTo, 0.3)));
    }

    // OSG Matrixd::rotate(q) equals VSG rotate(q) for a unit quat, in the same storage. The same rotation applied as
    // OSG v * M, VSG M * v, and q * v in both. Matrix_implementation.cpp:120-130, transform.h:37-40.
    TEST(QuatConventions, toMatrix)
    {
        EXPECT_TRUE(matNear(matOf<double>(0.781639173907025, 0.5501172307043584, -0.29395787843858057, 0.0,
                                -0.4829292842142122, 0.8320301337746345, 0.27295633888831433, 0.0, 0.3947397981737998,
                                -0.07139249941787584, 0.9160150668873173, 0.0, 0.0, 0.0, 0.0, 1.0),
            vsg::rotate(vQ1)));

        const vsg::dvec3 v(1.5, -2.25, 3.75);
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(3.739323893494264, -1.3146138277534245, 2.379967920670862),
            vsg::dvec3(3.739323893494264, -1.3146138277534245, 2.3799679206708615)));
        EXPECT_TRUE(vecNear<3>(vsg::rotate(vQ1) * v, vQ1 * v));
    }

    // OSG Matrixf::rotate(q) computes in double and narrows (Matrix_implementation.cpp:79-130).
    // It equals VSG mat4(rotate(dquat)) to tolerance. rotate(quat(q)) also works but computes in float.
    TEST(QuatConventions, toFloatMatrix)
    {
        EXPECT_TRUE(
            matNear(matOf<float>(0.781639159f, 0.550117254f, -0.293957889f, 0.0f, -0.482929289f, 0.832030118f,
                        0.272956342f, 0.0f, 0.394739807f, -0.0713924989f, 0.916015089f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                vsg::mat4(vsg::rotate(vQ1))));
    }

    // TRAP: OSG normalizes a non-unit quat when making a matrix (2 / length2, Matrix_implementation.cpp:83-86).
    // vsg::rotate assumes unit length (transform.h:37-40).
    // OSG Matrixd::rotate(q) equals VSG rotate(normalize(q)), not rotate(q).
    TEST(QuatConventions, nonUnitQuatToMatrix)
    {
        const vsg::dquat v(0.0, 0.0, 1.0, 1.0);

        EXPECT_TRUE(
            matNear(matOf<double>(0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
                vsg::rotate(vsg::normalize(v))));
        EXPECT_FALSE(
            matNear(matOf<double>(0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
                vsg::rotate(v)));
    }

    // Pin for the helper. A zero quat gives OSG a zero rotation block (Matrix_implementation.cpp:65-75) and VSG the
    // identity (transform.h:37-40); vsg::normalize of it is NaN.
    TEST(QuatConventions, zeroQuatToMatrix)
    {
        EXPECT_TRUE(recordedEqual(0.0, 0.0));
        EXPECT_TRUE(recordedEqual(1.0, 1.0));
        EXPECT_EQ(vsg::rotate(vsg::dquat(0.0, 0.0, 0.0, 0.0))(0, 0), 1.0);
    }

    // TRAP: matrix to quat. OSG Matrix::getRotate picks the sign from the largest of four traces
    // (Matrix_implementation.cpp:160-205); VSG's only route, decompose, makes w positive when the trace is positive
    // (maths_transform.cpp:258-265). The result is the same rotation with opposite signs for this input, which matters
    // to anything reading components (Lua, slerp's start).
    // OSG M.getRotate() of (0.8, 0, 0, -0.6) is (0.8, 0, 0, -0.6); VSG decompose gives (-0.8, 0, 0, 0.6).
    TEST(QuatConventions, matrixToQuatSign)
    {
        const vsg::dquat vInput(0.8, 0.0, 0.0, -0.6);

        EXPECT_TRUE(vecNear<4>(vsg::dquat(0.8000000000000002, 0.0, 0.0, -0.6), vInput));

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

        EXPECT_TRUE(vecNear<4>(
            vsg::dquat(0.10000000000000003, 0.20000000000000004, 0.30000000000000004, 0.9273618495495705), input));

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
        EXPECT_TRUE(
            vecNear<4>(vsg::dquat(0.0, 0.0, 0.0, 1.3228756555322951), vsg::dquat(0.0, 0.0, 0.0, std::sqrt(7.0) / 2.0)));

        vsg::dvec3 translation;
        vsg::dvec3 scale;
        vsg::dquat v;
        ASSERT_TRUE(vsg::decompose(vsg::scale(2.0, 2.0, 2.0), translation, v, scale));
        EXPECT_TRUE(vecNear<4>(v, vsg::dquat()));
    }
}

#endif
