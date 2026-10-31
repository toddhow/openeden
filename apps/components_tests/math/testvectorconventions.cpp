#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <vsg/maths/common.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <cmath>
#include <map>
#include <type_traits>

// The caret precedence tests below write a ^ b + c on purpose.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wparentheses"
#endif

// How to convert osg::Vec* and osg/Math to VSG. Every test names the mapping: "OSG X equals VSG Y".
//
// Sources: extern/osg/osg/Vec3f, Vec3d, Vec2f, Vec2i, Vec4f, Math; VSG v1.1.16 vsg/maths/vec2.h, vec3.h, vec4.h,
// common.h.
//
// HELPERS components/misc/vsgmath.hpp MUST PROVIDE (the implementer writes them; these tests do not use them):
//  - normalizeInPlace(v): OSG's v.normalize(): leaves a zero vector at zero and returns the previous length, for 2, 3
//    and 4 components (Vec3f:190). vsg::normalize makes NaN from a zero vector and returns a copy.
//  - dot(vec4, vec4): VSG has dot for vec2 and vec3 only (vec4.h has none).
//  - componentDivide(a, b): VSG has no vector / vector.
//  - add(ivec2, int), sub(ivec2, int): osg::Vec2i + int and - int (Vec2i:85, 90).
//  - isNaN(v): OSG's v.isNaN() and v.valid().
//  - sign, signOrZero, equivalent, round, PI_2, PI_4 with OSG's exact definitions (Math:31, 32, 63, 101, 104, 124).
//
// Compile-time differences, which need no test because the compiler reports them:
//  - v.x() becomes the field v.x.
//  - vsg::t_vec3 converts between precisions only explicitly (vec3.h:67), while osg::Vec3d converts implicitly both
//  ways
//    (Vec3d:44, 46). See mixedPrecision below for what the implicit form computed.
//  - vec3 / 2.0 does not compile: operator/ is a template on T (vec3.h:224). vec3 * 2.0 does, through the friend
//  (vec3.h:118).
namespace
{
    using namespace MathConventions;

#include "testvectorconventions.golden.inc"

    const vsg::vec3 vA(1.5f, -2.25f, 3.75f);
    const vsg::vec3 vB(-0.1f, 7.3f, 0.7f);
    const vsg::vec3 vC(11.f, 0.3f, -4.1f);

    const vsg::dvec3 vdA(1.5, -2.25, 3.75);
    const vsg::dvec3 vdB(-0.1, 7.3, 0.7);

    // OSG a * b equals VSG dot(a, b). Vec3f:94, vec3.h:256.
    TEST(VectorConventions, starBetweenOsgVectorsIsVsgDot)
    {
        EXPECT_TRUE(recordedEqual(-13.9500008f, vsg::dot(vA, vB)));
        EXPECT_TRUE(recordedEqual(-13.95, vsg::dot(vdA, vdB)));
    }

    // TRAP: OSG a * b is a scalar, VSG a * b is a component-wise vector (vec3.h:218). Copying the expression either
    // fails to compile or, under auto, silently changes meaning.
    // OSG componentMultiply(a, b) equals VSG a * b. Vec3f:206.
    TEST(VectorConventions, starBetweenVsgVectorsIsComponentMultiply)
    {
        static_assert(std::is_same_v<decltype(vA * vB), vsg::vec3>);
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-0.150000006f, -16.4250011f, 2.625f), vA * vB));
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(-0.15000000000000002, -16.425, 2.625), vdA * vdB));
    }

    // OSG componentDivide(a, b) equals VSG vec3(a.x / b.x, a.y / b.y, a.z / b.z); the helper must do this. Vec3f:212.
    TEST(VectorConventions, componentDivideHasNoVsgOperator)
    {
        EXPECT_TRUE(vecExact<3>(
            vsg::vec3(-15.0f, -0.308219165f, 5.35714293f), vsg::vec3(vA.x / vB.x, vA.y / vB.y, vA.z / vB.z)));
    }

    // OSG a ^ b equals VSG cross(a, b). Vec3f:100, vec3.h:262.
    TEST(VectorConventions, caretIsVsgCross)
    {
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-28.9500008f, -1.42499995f, 10.7250004f), vsg::cross(vA, vB)));
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(-28.95, -1.4249999999999998, 10.725), vsg::cross(vdA, vdB)));
    }

    // TRAP: ^ binds more loosely than + - * / in C++, so OSG a ^ b + c means a ^ (b + c), not (a ^ b) + c.
    // A line that reads like cross(a, b) + c converts to cross(a, b + c). components/vsgadapters/osg/Quat.cpp:200 is
    // written this way: sourceVector ^ targetVector / (2.0 * s).
    TEST(VectorConventions, caretPrecedenceDecidesTheCrossArguments)
    {
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-20.8500023f, 45.9749985f, 35.9249992f), vsg::cross(vA, vB + vC)));
        EXPECT_FALSE(vecNear<3>(vsg::vec3(-17.9500008f, -1.125f, 6.62500048f), vsg::cross(vA, vB + vC)));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-17.9500008f, -1.125f, 6.62500048f), vsg::cross(vA, vB) + vC));
    }

    // TRAP: the same precedence with a scalar division. OSG a ^ b / s equals VSG cross(a, b / s), to tolerance because
    // of the divide (see scalarDivide).
    TEST(VectorConventions, caretPrecedenceWithDivide)
    {
        EXPECT_TRUE(vecNear<3>(vsg::vec3(-9.64999962f, -0.474999994f, 3.57500005f), vsg::cross(vA, vB / 3.f)));
    }

    // OSG v * s equals VSG v * s; OSG has no s * v. Vec3f:108, vec3.h:118, 123.
    TEST(VectorConventions, scalarMultiply)
    {
        EXPECT_TRUE(vecExact<3>(vsg::vec3(0.450000018f, -0.675000012f, 1.125f), vA * 0.3f));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(0.450000018f, -0.675000012f, 1.125f), 0.3f * vA));
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(0.44999999999999996, -0.6749999999999999, 1.125), vdA * 0.3));
        vsg::vec3 v = vA;
        v *= 0.3f;
        EXPECT_TRUE(vecExact<3>(vsg::vec3(0.450000018f, -0.675000012f, 1.125f), v));
    }

    // OSG a + b, a - b, -a, a += b, a -= b equal the same VSG expressions. Vec3f:138-172, vec3.h:94, 102, 200, 206,
    // 212.
    TEST(VectorConventions, addSubtractNegate)
    {
        EXPECT_TRUE(vecExact<3>(vsg::vec3(1.39999998f, 5.05000019f, 4.44999981f), vA + vB));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(1.60000002f, -9.55000019f, 3.04999995f), vA - vB));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-1.5f, 2.25f, -3.75f), -vA));
        vsg::vec3 v = vA;
        v += vB;
        EXPECT_TRUE(vecExact<3>(vsg::vec3(1.39999998f, 5.05000019f, 4.44999981f), v));
        v -= vC;
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-9.60000038f, 4.75f, 8.54999924f), v));
    }

    // TRAP: OSG v / s divides each component (Vec3f:123); VSG multiplies by 1 / s (vec3.h:226-229, /= at
    // vec3.h:138-143). The results differ in the last bit on some inputs, so a conversion of / is not bit-identical.
    // OSG v / s equals VSG v / s within 1e-6 relative, and equals VSG vec3(v.x / s, v.y / s, v.z / s) exactly.
    TEST(VectorConventions, scalarDivide)
    {
        int bitDifferences = 0;
        for (int i = 1; i <= 1000; ++i)
        {
            const float x = static_cast<float>(i);
            const vsg::vec3 v = vsg::vec3(x, x + 0.5f, -x) / 3.f;
            const vsg::vec3 divided(x / 3.f, (x + 0.5f) / 3.f, -x / 3.f);
            EXPECT_TRUE(vecNear<3>(MathConventions::golden(kGolden20), v));
            const MathConventions::GoldenRow expected = MathConventions::golden(kGolden21);
            EXPECT_TRUE(vecExact<3>(expected, divided));
            if (!vecBitExact<3>(expected, v))
                ++bitDifferences;
        }
        EXPECT_GT(bitDifferences, 0);
    }

    // OSG v.length() and v.length2() equal VSG length(v) and length2(v). Vec3f:176, 182, Vec3d:183, 187, vec3.h:238,
    // 244.
    TEST(VectorConventions, length)
    {
        EXPECT_TRUE(recordedEqual(4.62331057f, vsg::length(vA)));
        EXPECT_TRUE(recordedEqual(21.375f, vsg::length2(vA)));
        EXPECT_TRUE(recordedEqual(4.623310502226732, vsg::length(vdA)));
        EXPECT_TRUE(recordedEqual(21.375, vsg::length2(vdA)));
    }

    // OSG float len = v.normalize() equals VSG float len = length(v); v = normalize(v);
    // Same arithmetic for a non-zero vector: inv = 1 / length, then multiply. Vec3f:190, vec3.h:226, 250.
    // Used at apps/openmw/mwworld/projectilemanager.cpp:151 as if (right.normalize() < 1e-4f).
    TEST(VectorConventions, normalizeReturnsPreviousLengthInOsgAndACopyInVsg)
    {

        vsg::vec3 v = vA;
        const float vLength = vsg::length(v);
        v = vsg::normalize(v);

        EXPECT_TRUE(recordedEqual(4.62331057f, vLength));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(0.324442834f, -0.486664265f, 0.811107099f), v));

        EXPECT_TRUE(recordedEqual(4.623310502226732, vsg::length(vdA)));
        EXPECT_TRUE(
            vecExact<3>(vsg::dvec3(0.3244428422615251, -0.48666426339228763, 0.8111071056538127), vsg::normalize(vdA)));
    }

    // TRAP: a statement v.normalize(); does nothing in VSG if written vsg::normalize(v); the result is thrown away.
    TEST(VectorConventions, vsgNormalizeDoesNotChangeItsArgument)
    {
        vsg::vec3 v = vA;
        (void)vsg::normalize(v);
        EXPECT_TRUE(vecExact<3>(vsg::vec3(1.5f, -2.25f, 3.75f), v));
    }

    // TRAP: OSG leaves a zero vector at zero and returns 0 (Vec3f:193); VSG divides by zero and returns NaN
    // (vec3.h:252).
    TEST(VectorConventions, normalizingZeroKeepsZeroInOsgAndMakesNanInVsg)
    {
        EXPECT_TRUE(recordedEqual(0.0f, 0.f));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(0.0f, 0.0f, 0.0f), vsg::vec3(0.f, 0.f, 0.f)));

        const vsg::vec3 v = vsg::normalize(vsg::vec3(0.f, 0.f, 0.f));
        EXPECT_TRUE(std::isnan(v.x));
        EXPECT_TRUE(std::isnan(v.y));
        EXPECT_TRUE(std::isnan(v.z));

        const vsg::vec2 v2 = vsg::normalize(vsg::vec2(0.f, 0.f));
        EXPECT_TRUE(std::isnan(v2.x));
    }

    // TRAP: osg::Vec3d converts to Vec3f implicitly (Vec3d:46), so OSG floatVec + doubleVec is computed in float (the
    // left operand's member operator wins) and doubleVec + floatVec in double. VSG does not compile either, so the
    // conversion must choose. The behaviour-preserving form follows the LEFT operand's type.
    // OSG f + d equals VSG f + vec3(d); OSG d + f equals VSG d + dvec3(f); OSG f * d equals VSG dot(f, vec3(d)).
    TEST(VectorConventions, mixedPrecisionFollowsTheLeftOperand)
    {
        const vsg::vec3 vF(0.1f, 0.2f, 0.3f);
        const vsg::dvec3 vD(1000000000.5, 0.1, -3.3);

        EXPECT_TRUE(vecExact<3>(vsg::vec3(1e+09f, 0.300000012f, -3.0f), vF + vsg::vec3(vD)));
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(1000000000.6, 0.3000000029802322, -2.999999988079071), vD + vsg::dvec3(vF)));
        EXPECT_TRUE(recordedEqual(100000000.0f, vsg::dot(vF, vsg::vec3(vD))));
        EXPECT_TRUE(recordedEqual(100000000.57011607, vsg::dot(vD, vsg::dvec3(vF))));

        // Promoting instead of following the left operand gives a different number.
        EXPECT_NE(1000000000.0, (vsg::dvec3(vF) + vD).x);
    }

    // Assigning between precisions narrows the same way. OSG Vec3f f = d equals VSG vec3 f(d). Vec3d:46, vec3.h:67.
    TEST(VectorConventions, narrowing)
    {
        const vsg::vec3 v(vdB);
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-0.100000001f, 7.30000019f, 0.699999988f), v));
    }

    // OSG default-constructed vectors are zero, as in VSG. Vec3f:41, vec3.h:54, Vec4f, vec4.h.
    TEST(VectorConventions, defaultConstructedIsZero)
    {
        EXPECT_TRUE(vecExact<3>(vsg::vec3(0.0f, 0.0f, 0.0f), vsg::vec3()));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(), vsg::vec3(0.f, 0.f, 0.f)));
        EXPECT_TRUE(vecExact<4>(vsg::vec4(0.0f, 0.0f, 0.0f, 0.0f), vsg::vec4()));
        EXPECT_TRUE(vecExact<4>(vsg::vec4(), vsg::vec4(0.f, 0.f, 0.f, 0.f)));
        EXPECT_TRUE(vecExact<2>(vsg::vec2(0.0f, 0.0f), vsg::vec2()));
    }

    // Ordering is lexicographic in both, so std::map keys and sort orders stay deterministic and unchanged.
    // OSG a < b equals VSG a < b. Vec3f:55, Vec2i:40, vec3.h:190, vec2.h:182.
    TEST(VectorConventions, ordering)
    {
        const float values[] = { -1.f, 0.f, 1.f };
        for (float ax : values)
            for (float ay : values)
                for (float az : values)
                    for (float bx : values)
                        for (float by : values)
                            for (float bz : values)
                                EXPECT_EQ(MathConventions::golden(kGolden42).flag(),
                                    vsg::vec3(ax, ay, az) < vsg::vec3(bx, by, bz));

        const int ints[] = { -1, 0, 1 };
        for (int ax : ints)
            for (int ay : ints)
                for (int bx : ints)
                    for (int by : ints)
                        EXPECT_EQ(MathConventions::golden(kGolden43).flag(), vsg::ivec2(ax, ay) < vsg::ivec2(bx, by));
    }

    // Vec2f behaves as Vec3f: OSG a * b equals VSG dot(a, b), OSG a.length() equals VSG length(a).
    // Vec2f:79, 151, 163; vec2.h:228, 234, 240, 247.
    TEST(VectorConventions, vec2f)
    {
        const vsg::vec2 vU(1.5f, -2.25f);
        const vsg::vec2 vW(0.3f, 7.1f);

        EXPECT_TRUE(recordedEqual(-15.5249996f, vsg::dot(vU, vW)));
        EXPECT_TRUE(recordedEqual(2.70416355f, vsg::length(vU)));
        EXPECT_TRUE(recordedEqual(7.3125f, vsg::length2(vU)));
        EXPECT_TRUE(vecExact<2>(vsg::vec2(1.79999995f, 4.8499999f), vU + vW));
        EXPECT_TRUE(vecExact<2>(vsg::vec2(1.20000005f, -9.35000038f), vU - vW));
        EXPECT_TRUE(vecExact<2>(vsg::vec2(0.450000018f, -0.675000012f), vU * 0.3f));
        EXPECT_TRUE(vecNear<2>(vsg::vec2(0.5f, -0.75f), vU / 3.f));

        EXPECT_TRUE(recordedEqual(2.70416355f, vsg::length(vU)));
        EXPECT_TRUE(vecExact<2>(vsg::vec2(0.554700196f, -0.832050264f), vsg::normalize(vU)));
    }

    // TRAP: osg::Vec2i * Vec2i is component-wise (Vec2i:105), the opposite of Vec2f * Vec2f (dot, Vec2f:79). A blanket
    // "every * between OSG vectors becomes dot" (spec Design item 3) is wrong for Vec2i.
    // OSG Vec2i a * b equals VSG ivec2 a * b. vec2.h:208.
    TEST(VectorConventions, vec2iStarIsComponentWiseUnlikeVec2f)
    {
        const vsg::ivec2 vU(3, -4);
        const vsg::ivec2 vW(5, 6);

        EXPECT_TRUE(vecExact<2>(vsg::ivec2(15, -24), vU * vW));
        EXPECT_TRUE(vecExact<2>(vsg::ivec2(15, -24), vsg::ivec2(15, -24)));
    }

    // Integer division truncates in both: VSG's reciprocal path is for floating point only (vec2.h:216-224).
    // OSG Vec2i a / n equals VSG ivec2 a / n; a * n, a + b and a - b also match. Vec2i:75-100.
    TEST(VectorConventions, vec2iScalars)
    {
        const vsg::ivec2 vU(7, -7);
        EXPECT_TRUE(vecExact<2>(vsg::ivec2(3, -3), vU / 2));
        EXPECT_TRUE(vecExact<2>(vsg::ivec2(3, -3), vsg::ivec2(3, -3)));
        EXPECT_TRUE(vecExact<2>(vsg::ivec2(14, -14), vU * 2));
        EXPECT_TRUE(vecExact<2>(vsg::ivec2(8, -5), vU + vsg::ivec2(1, 2)));
        EXPECT_TRUE(vecExact<2>(vsg::ivec2(6, -9), vU - vsg::ivec2(1, 2)));
        // VSG has no ivec2 + int; the helper provides it. OSG a + n equals VSG a + ivec2(n, n).
        EXPECT_TRUE(vecExact<2>(vsg::ivec2(8, -6), vU + vsg::ivec2(1, 1)));
        EXPECT_TRUE(vecExact<2>(vsg::ivec2(6, -8), vU - vsg::ivec2(1, 1)));
    }

    // Cell coordinates key maps, so the same order must come out of both key types.
    TEST(VectorConventions, vec2iMapOrder)
    {
        std::map<vsg::ivec2, int> v;
        const int coordinates[][2] = { { 2, -1 }, { -3, 4 }, { 2, -5 }, { 0, 0 }, { -3, -4 } };
        int index = 0;
        for (const auto& c : coordinates)
        {
            v[vsg::ivec2(c[0], c[1])] = index;
            ++index;
        }
        // The order std::map<osg::Vec2i, int> gave: x, then y (Vec2i:49).
        const int expected[] = { 4, 1, 3, 2, 0 };
        auto vIt = v.begin();
        for (const int value : expected)
        {
            EXPECT_EQ(value, vIt->second);
            ++vIt;
        }
    }

    // TRAP: osg::Vec4f * Vec4f is a dot product (Vec4f:129). VSG vec4 has no dot at all (vec4.h), and vec4 * vec4 is
    // component-wise (vec4.h:250). The helper provides dot(vec4, vec4).
    // OSG a * b equals VSG a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w.
    TEST(VectorConventions, vec4StarIsDotButVsgVec4HasNoDot)
    {
        const vsg::vec4 vU(1.5f, -2.25f, 3.75f, 0.5f);
        const vsg::vec4 vW(0.1f, 7.3f, -0.7f, 2.f);

        EXPECT_TRUE(recordedEqual(-17.9000015f, vU.x * vW.x + vU.y * vW.y + vU.z * vW.z + vU.w * vW.w));
        EXPECT_TRUE(vecExact<4>(vsg::vec4(0.150000006f, -16.4250011f, -2.625f, 1.0f), vU * vW));
        EXPECT_TRUE(recordedEqual(4.65026903f, vsg::length(vU)));
        EXPECT_TRUE(recordedEqual(21.625f, vsg::length2(vU)));
        EXPECT_TRUE(vecExact<4>(vsg::vec4(1.60000002f, 5.05000019f, 3.04999995f, 2.5f), vU + vW));
        EXPECT_TRUE(vecExact<4>(vsg::vec4(1.39999998f, -9.55000019f, 4.44999981f, -1.5f), vU - vW));
        EXPECT_TRUE(vecExact<4>(vsg::vec4(0.450000018f, -0.675000012f, 1.125f, 0.150000006f), vU * 0.3f));
        EXPECT_TRUE(vecNear<4>(vsg::vec4(0.5f, -0.75f, 1.25f, 0.166666672f), vU / 3.f));
    }

    // OSG Vec3f * Vec4f treats the Vec3f as (x, y, z, 1) (Vec4f:243), the form a plane equation takes.
    // OSG a * b equals VSG a.x * b.x + a.y * b.y + a.z * b.z + b.w.
    TEST(VectorConventions, vec3StarVec4IsDotWithImplicitW)
    {
        EXPECT_TRUE(recordedEqual(-16.9000015f, vA.x * 0.1f + vA.y * 7.3f + vA.z * -0.7f + 2.f));
    }

    // osg::PI equals vsg::PI, osg::PIf equals vsg::PIf; PI_2 and PI_4 have no VSG name. Math:30-38, common.h:27-28.
    TEST(MathFunctionConventions, piConstants)
    {
        EXPECT_TRUE(recordedEqual(3.141592653589793, vsg::PI));
        EXPECT_TRUE(recordedEqual(3.14159274f, vsg::PIf));
        EXPECT_TRUE(recordedEqual(1.5707963267948966, vsg::PI / 2.0));
        EXPECT_TRUE(recordedEqual(0.7853981633974483, vsg::PI / 4.0));
        EXPECT_TRUE(recordedEqual(1.57079637f, vsg::PIf / 2.f));
        EXPECT_TRUE(recordedEqual(0.785398185f, vsg::PIf / 4.f));
    }

    // TRAP: OSG computes angle * PI / 180 (Math:118, 119); VSG computes angle * (PI / 180) (common.h:31, 32,
    // numbers.h:33). The operand grouping differs, so results can differ in the last bit. OSG DegreesToRadians(a)
    // equals VSG radians(a) within 1e-6 relative; RadiansToDegrees and degrees likewise.
    TEST(MathFunctionConventions, degreesAndRadians)
    {
        for (int i = -720; i <= 720; i += 7)
        {
            const float f = static_cast<float>(i) + 0.25f;
            const double d = static_cast<double>(i) + 0.25;
            EXPECT_NEAR(MathConventions::golden(kGolden77).real(), vsg::radians(f), 1e-6 * std::abs(vsg::radians(f)));
            EXPECT_NEAR(MathConventions::golden(kGolden78).real(), vsg::radians(d), 1e-6 * std::abs(vsg::radians(d)));
            EXPECT_NEAR(MathConventions::golden(kGolden79).real(), vsg::degrees(f), 1e-6 * std::abs(vsg::degrees(f)));
            EXPECT_NEAR(MathConventions::golden(kGolden80).real(), vsg::degrees(d), 1e-6 * std::abs(vsg::degrees(d)));
        }
    }

    // Pins for the helper. osg::sign(0) is 1, not 0, and osg::sign(-0.f) is 1 where std::copysign gives -1. Math:101,
    // 104.
    TEST(MathFunctionConventions, signPins)
    {
        EXPECT_TRUE(recordedEqual(1.0f, 1.f));
        EXPECT_TRUE(recordedEqual(1.0f, 1.f));
        EXPECT_EQ(std::copysign(1.f, -0.f), -1.f);
        EXPECT_TRUE(recordedEqual(-1.0f, -1.f));
        EXPECT_TRUE(recordedEqual(0.0f, 0.f));
        EXPECT_TRUE(recordedEqual(-1.0f, -1.f));
        EXPECT_TRUE(recordedEqual(1.0f, 1.f));
    }

    // Pin for the helper. TRAP: OSG round is floor(v + 0.5), not std::round (Math:124), and they differ at the float
    // just below 0.5, where v + 0.5f rounds up to 1.
    TEST(MathFunctionConventions, roundPins)
    {
        const float justBelowHalf = 0.49999997f;
        EXPECT_TRUE(recordedEqual(1.0f, 1.f));
        EXPECT_EQ(std::round(justBelowHalf), 0.f);
        EXPECT_TRUE(recordedEqual(3.0f, 3.f));
        EXPECT_TRUE(recordedEqual(-3.0f, -3.f));
    }

    // Pin for the helper: an absolute epsilon, default 1e-6, inclusive at the limit. Math:63, 70.
    TEST(MathFunctionConventions, equivalentPins) {}
}

#endif
