#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec2i>
#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osg/Vec4f>

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
// Sources: extern/osg/osg/Vec3f, Vec3d, Vec2f, Vec2i, Vec4f, Math; VSG v1.1.16 vsg/maths/vec2.h, vec3.h, vec4.h, common.h.
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
//  - vsg::t_vec3 converts between precisions only explicitly (vec3.h:67), while osg::Vec3d converts implicitly both ways
//    (Vec3d:44, 46). See mixedPrecision below for what the implicit form computed.
//  - vec3 / 2.0 does not compile: operator/ is a template on T (vec3.h:224). vec3 * 2.0 does, through the friend (vec3.h:118).
namespace
{
    using namespace MathConventions;

    const osg::Vec3f oA(1.5f, -2.25f, 3.75f);
    const osg::Vec3f oB(-0.1f, 7.3f, 0.7f);
    const osg::Vec3f oC(11.f, 0.3f, -4.1f);
    const vsg::vec3 vA(1.5f, -2.25f, 3.75f);
    const vsg::vec3 vB(-0.1f, 7.3f, 0.7f);
    const vsg::vec3 vC(11.f, 0.3f, -4.1f);

    const osg::Vec3d odA(1.5, -2.25, 3.75);
    const osg::Vec3d odB(-0.1, 7.3, 0.7);
    const vsg::dvec3 vdA(1.5, -2.25, 3.75);
    const vsg::dvec3 vdB(-0.1, 7.3, 0.7);

    // OSG a * b equals VSG dot(a, b). Vec3f:94, vec3.h:256.
    TEST(VectorConventions, starBetweenOsgVectorsIsVsgDot)
    {
        EXPECT_EQ(oA * oB, vsg::dot(vA, vB));
        EXPECT_EQ(odA * odB, vsg::dot(vdA, vdB));
    }

    // TRAP: OSG a * b is a scalar, VSG a * b is a component-wise vector (vec3.h:218). Copying the expression either
    // fails to compile or, under auto, silently changes meaning.
    // OSG componentMultiply(a, b) equals VSG a * b. Vec3f:206.
    TEST(VectorConventions, starBetweenVsgVectorsIsComponentMultiply)
    {
        static_assert(std::is_same_v<decltype(oA * oB), float>);
        static_assert(std::is_same_v<decltype(vA * vB), vsg::vec3>);
        EXPECT_TRUE(vecExact<3>(osg::componentMultiply(oA, oB), vA * vB));
        EXPECT_TRUE(vecExact<3>(osg::componentMultiply(odA, odB), vdA * vdB));
    }

    // OSG componentDivide(a, b) equals VSG vec3(a.x / b.x, a.y / b.y, a.z / b.z); the helper must do this. Vec3f:212.
    TEST(VectorConventions, componentDivideHasNoVsgOperator)
    {
        EXPECT_TRUE(vecExact<3>(osg::componentDivide(oA, oB), vsg::vec3(vA.x / vB.x, vA.y / vB.y, vA.z / vB.z)));
    }

    // OSG a ^ b equals VSG cross(a, b). Vec3f:100, vec3.h:262.
    TEST(VectorConventions, caretIsVsgCross)
    {
        EXPECT_TRUE(vecExact<3>(oA ^ oB, vsg::cross(vA, vB)));
        EXPECT_TRUE(vecExact<3>(odA ^ odB, vsg::cross(vdA, vdB)));
    }

    // TRAP: ^ binds more loosely than + - * / in C++, so OSG a ^ b + c means a ^ (b + c), not (a ^ b) + c.
    // A line that reads like cross(a, b) + c converts to cross(a, b + c). components/vsgadapters/osg/Quat.cpp:200 is
    // written this way: sourceVector ^ targetVector / (2.0 * s).
    TEST(VectorConventions, caretPrecedenceDecidesTheCrossArguments)
    {
        EXPECT_TRUE(vecExact<3>(oA ^ oB + oC, vsg::cross(vA, vB + vC)));
        const osg::Vec3f naive = (oA ^ oB) + oC;
        EXPECT_FALSE(vecNear<3>(naive, vsg::cross(vA, vB + vC)));
        EXPECT_TRUE(vecExact<3>(naive, vsg::cross(vA, vB) + vC));
    }

    // TRAP: the same precedence with a scalar division. OSG a ^ b / s equals VSG cross(a, b / s), to tolerance because
    // of the divide (see scalarDivide).
    TEST(VectorConventions, caretPrecedenceWithDivide)
    {
        EXPECT_TRUE(vecNear<3>(oA ^ oB / 3.f, vsg::cross(vA, vB / 3.f)));
    }

    // OSG v * s equals VSG v * s; OSG has no s * v. Vec3f:108, vec3.h:118, 123.
    TEST(VectorConventions, scalarMultiply)
    {
        EXPECT_TRUE(vecExact<3>(oA * 0.3f, vA * 0.3f));
        EXPECT_TRUE(vecExact<3>(oA * 0.3f, 0.3f * vA));
        EXPECT_TRUE(vecExact<3>(odA * 0.3, vdA * 0.3));
        osg::Vec3f o = oA;
        vsg::vec3 v = vA;
        o *= 0.3f;
        v *= 0.3f;
        EXPECT_TRUE(vecExact<3>(o, v));
    }

    // OSG a + b, a - b, -a, a += b, a -= b equal the same VSG expressions. Vec3f:138-172, vec3.h:94, 102, 200, 206, 212.
    TEST(VectorConventions, addSubtractNegate)
    {
        EXPECT_TRUE(vecExact<3>(oA + oB, vA + vB));
        EXPECT_TRUE(vecExact<3>(oA - oB, vA - vB));
        EXPECT_TRUE(vecExact<3>(-oA, -vA));
        osg::Vec3f o = oA;
        vsg::vec3 v = vA;
        o += oB;
        v += vB;
        EXPECT_TRUE(vecExact<3>(o, v));
        o -= oC;
        v -= vC;
        EXPECT_TRUE(vecExact<3>(o, v));
    }

    // TRAP: OSG v / s divides each component (Vec3f:123); VSG multiplies by 1 / s (vec3.h:226-229, /= at vec3.h:138-143).
    // The results differ in the last bit on some inputs, so a conversion of / is not bit-identical.
    // OSG v / s equals VSG v / s within 1e-6 relative, and equals VSG vec3(v.x / s, v.y / s, v.z / s) exactly.
    TEST(VectorConventions, scalarDivide)
    {
        int bitDifferences = 0;
        for (int i = 1; i <= 1000; ++i)
        {
            const float x = static_cast<float>(i);
            const osg::Vec3f o = osg::Vec3f(x, x + 0.5f, -x) / 3.f;
            const vsg::vec3 v = vsg::vec3(x, x + 0.5f, -x) / 3.f;
            const vsg::vec3 divided(x / 3.f, (x + 0.5f) / 3.f, -x / 3.f);
            EXPECT_TRUE(vecNear<3>(o, v));
            EXPECT_TRUE(vecExact<3>(o, divided));
            if (!vecExact<3>(o, v))
                ++bitDifferences;
        }
        EXPECT_GT(bitDifferences, 0);
    }

    // OSG v.length() and v.length2() equal VSG length(v) and length2(v). Vec3f:176, 182, Vec3d:183, 187, vec3.h:238, 244.
    TEST(VectorConventions, length)
    {
        EXPECT_EQ(oA.length(), vsg::length(vA));
        EXPECT_EQ(oA.length2(), vsg::length2(vA));
        EXPECT_EQ(odA.length(), vsg::length(vdA));
        EXPECT_EQ(odA.length2(), vsg::length2(vdA));
    }

    // OSG float len = v.normalize() equals VSG float len = length(v); v = normalize(v);
    // Same arithmetic for a non-zero vector: inv = 1 / length, then multiply. Vec3f:190, vec3.h:226, 250.
    // Used at apps/openmw/mwworld/projectilemanager.cpp:151 as if (right.normalize() < 1e-4f).
    TEST(VectorConventions, normalizeReturnsPreviousLengthInOsgAndACopyInVsg)
    {
        osg::Vec3f o = oA;
        const float oLength = o.normalize();

        vsg::vec3 v = vA;
        const float vLength = vsg::length(v);
        v = vsg::normalize(v);

        EXPECT_EQ(oLength, vLength);
        EXPECT_TRUE(vecExact<3>(o, v));

        osg::Vec3d od = odA;
        const double odLength = od.normalize();
        EXPECT_EQ(odLength, vsg::length(vdA));
        EXPECT_TRUE(vecExact<3>(od, vsg::normalize(vdA)));
    }

    // TRAP: a statement v.normalize(); does nothing in VSG if written vsg::normalize(v); the result is thrown away.
    TEST(VectorConventions, vsgNormalizeDoesNotChangeItsArgument)
    {
        vsg::vec3 v = vA;
        (void)vsg::normalize(v);
        EXPECT_TRUE(vecExact<3>(oA, v));
    }

    // TRAP: OSG leaves a zero vector at zero and returns 0 (Vec3f:193); VSG divides by zero and returns NaN (vec3.h:252).
    TEST(VectorConventions, normalizingZeroKeepsZeroInOsgAndMakesNanInVsg)
    {
        osg::Vec3f o(0.f, 0.f, 0.f);
        EXPECT_EQ(o.normalize(), 0.f);
        EXPECT_TRUE(vecExact<3>(o, vsg::vec3(0.f, 0.f, 0.f)));

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
        const osg::Vec3f oF(0.1f, 0.2f, 0.3f);
        const osg::Vec3d oD(1000000000.5, 0.1, -3.3);
        const vsg::vec3 vF(0.1f, 0.2f, 0.3f);
        const vsg::dvec3 vD(1000000000.5, 0.1, -3.3);

        static_assert(std::is_same_v<std::decay_t<decltype(oF + oD)>, osg::Vec3f>);
        static_assert(std::is_same_v<std::decay_t<decltype(oD + oF)>, osg::Vec3d>);

        EXPECT_TRUE(vecExact<3>(oF + oD, vF + vsg::vec3(vD)));
        EXPECT_TRUE(vecExact<3>(oD + oF, vD + vsg::dvec3(vF)));
        EXPECT_EQ(oF * oD, vsg::dot(vF, vsg::vec3(vD)));
        EXPECT_EQ(oD * oF, vsg::dot(vD, vsg::dvec3(vF)));

        // Promoting instead of following the left operand gives a different number.
        EXPECT_NE(static_cast<double>((oF + oD)[0]), (vsg::dvec3(vF) + vD).x);
    }

    // Assigning between precisions narrows the same way. OSG Vec3f f = d equals VSG vec3 f(d). Vec3d:46, vec3.h:67.
    TEST(VectorConventions, narrowing)
    {
        const osg::Vec3f o = odB;
        const vsg::vec3 v(vdB);
        EXPECT_TRUE(vecExact<3>(o, v));
    }

    // OSG default-constructed vectors are zero, as in VSG. Vec3f:41, vec3.h:54, Vec4f, vec4.h.
    TEST(VectorConventions, defaultConstructedIsZero)
    {
        EXPECT_TRUE(vecExact<3>(osg::Vec3f(), vsg::vec3()));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(), vsg::vec3(0.f, 0.f, 0.f)));
        EXPECT_TRUE(vecExact<4>(osg::Vec4f(), vsg::vec4()));
        EXPECT_TRUE(vecExact<4>(vsg::vec4(), vsg::vec4(0.f, 0.f, 0.f, 0.f)));
        EXPECT_TRUE(vecExact<2>(osg::Vec2f(), vsg::vec2()));
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
                                EXPECT_EQ(osg::Vec3f(ax, ay, az) < osg::Vec3f(bx, by, bz),
                                    vsg::vec3(ax, ay, az) < vsg::vec3(bx, by, bz));

        const int ints[] = { -1, 0, 1 };
        for (int ax : ints)
            for (int ay : ints)
                for (int bx : ints)
                    for (int by : ints)
                        EXPECT_EQ(osg::Vec2i(ax, ay) < osg::Vec2i(bx, by), vsg::ivec2(ax, ay) < vsg::ivec2(bx, by));
    }

    // Vec2f behaves as Vec3f: OSG a * b equals VSG dot(a, b), OSG a.length() equals VSG length(a).
    // Vec2f:79, 151, 163; vec2.h:228, 234, 240, 247.
    TEST(VectorConventions, vec2f)
    {
        const osg::Vec2f oU(1.5f, -2.25f);
        const osg::Vec2f oW(0.3f, 7.1f);
        const vsg::vec2 vU(1.5f, -2.25f);
        const vsg::vec2 vW(0.3f, 7.1f);

        EXPECT_EQ(oU * oW, vsg::dot(vU, vW));
        EXPECT_EQ(oU.length(), vsg::length(vU));
        EXPECT_EQ(oU.length2(), vsg::length2(vU));
        EXPECT_TRUE(vecExact<2>(oU + oW, vU + vW));
        EXPECT_TRUE(vecExact<2>(oU - oW, vU - vW));
        EXPECT_TRUE(vecExact<2>(oU * 0.3f, vU * 0.3f));
        EXPECT_TRUE(vecNear<2>(oU / 3.f, vU / 3.f));

        osg::Vec2f normalized = oU;
        EXPECT_EQ(normalized.normalize(), vsg::length(vU));
        EXPECT_TRUE(vecExact<2>(normalized, vsg::normalize(vU)));
    }

    // TRAP: osg::Vec2i * Vec2i is component-wise (Vec2i:105), the opposite of Vec2f * Vec2f (dot, Vec2f:79). A blanket
    // "every * between OSG vectors becomes dot" (spec Design item 3) is wrong for Vec2i.
    // OSG Vec2i a * b equals VSG ivec2 a * b. vec2.h:208.
    TEST(VectorConventions, vec2iStarIsComponentWiseUnlikeVec2f)
    {
        const osg::Vec2i oU(3, -4);
        const osg::Vec2i oW(5, 6);
        const vsg::ivec2 vU(3, -4);
        const vsg::ivec2 vW(5, 6);

        static_assert(std::is_same_v<decltype(oU * oW), osg::Vec2i>);
        EXPECT_TRUE(vecExact<2>(oU * oW, vU * vW));
        EXPECT_TRUE(vecExact<2>(oU * oW, vsg::ivec2(15, -24)));
    }

    // Integer division truncates in both: VSG's reciprocal path is for floating point only (vec2.h:216-224).
    // OSG Vec2i a / n equals VSG ivec2 a / n; a * n, a + b and a - b also match. Vec2i:75-100.
    TEST(VectorConventions, vec2iScalars)
    {
        const osg::Vec2i oU(7, -7);
        const vsg::ivec2 vU(7, -7);
        EXPECT_TRUE(vecExact<2>(oU / 2, vU / 2));
        EXPECT_TRUE(vecExact<2>(oU / 2, vsg::ivec2(3, -3)));
        EXPECT_TRUE(vecExact<2>(oU * 2, vU * 2));
        EXPECT_TRUE(vecExact<2>(oU + osg::Vec2i(1, 2), vU + vsg::ivec2(1, 2)));
        EXPECT_TRUE(vecExact<2>(oU - osg::Vec2i(1, 2), vU - vsg::ivec2(1, 2)));
        // VSG has no ivec2 + int; the helper provides it. OSG a + n equals VSG a + ivec2(n, n).
        EXPECT_TRUE(vecExact<2>(oU + 1, vU + vsg::ivec2(1, 1)));
        EXPECT_TRUE(vecExact<2>(oU - 1, vU - vsg::ivec2(1, 1)));
    }

    // Cell coordinates key maps, so the same order must come out of both key types.
    TEST(VectorConventions, vec2iMapOrder)
    {
        std::map<osg::Vec2i, int> o;
        std::map<vsg::ivec2, int> v;
        const int coordinates[][2] = { { 2, -1 }, { -3, 4 }, { 2, -5 }, { 0, 0 }, { -3, -4 } };
        int index = 0;
        for (const auto& c : coordinates)
        {
            o[osg::Vec2i(c[0], c[1])] = index;
            v[vsg::ivec2(c[0], c[1])] = index;
            ++index;
        }
        auto vIt = v.begin();
        for (const auto& [key, value] : o)
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
        const osg::Vec4f oU(1.5f, -2.25f, 3.75f, 0.5f);
        const osg::Vec4f oW(0.1f, 7.3f, -0.7f, 2.f);
        const vsg::vec4 vU(1.5f, -2.25f, 3.75f, 0.5f);
        const vsg::vec4 vW(0.1f, 7.3f, -0.7f, 2.f);

        EXPECT_EQ(oU * oW, vU.x * vW.x + vU.y * vW.y + vU.z * vW.z + vU.w * vW.w);
        EXPECT_TRUE(vecExact<4>(osg::componentMultiply(oU, oW), vU * vW));
        EXPECT_EQ(oU.length(), vsg::length(vU));
        EXPECT_EQ(oU.length2(), vsg::length2(vU));
        EXPECT_TRUE(vecExact<4>(oU + oW, vU + vW));
        EXPECT_TRUE(vecExact<4>(oU - oW, vU - vW));
        EXPECT_TRUE(vecExact<4>(oU * 0.3f, vU * 0.3f));
        EXPECT_TRUE(vecNear<4>(oU / 3.f, vU / 3.f));
    }

    // OSG Vec3f * Vec4f treats the Vec3f as (x, y, z, 1) (Vec4f:243), the form a plane equation takes.
    // OSG a * b equals VSG a.x * b.x + a.y * b.y + a.z * b.z + b.w.
    TEST(VectorConventions, vec3StarVec4IsDotWithImplicitW)
    {
        const osg::Vec4f oW(0.1f, 7.3f, -0.7f, 2.f);
        EXPECT_EQ(oA * oW, vA.x * 0.1f + vA.y * 7.3f + vA.z * -0.7f + 2.f);
    }

    // osg::PI equals vsg::PI, osg::PIf equals vsg::PIf; PI_2 and PI_4 have no VSG name. Math:30-38, common.h:27-28.
    TEST(MathFunctionConventions, piConstants)
    {
        EXPECT_EQ(osg::PI, vsg::PI);
        EXPECT_EQ(osg::PIf, vsg::PIf);
        EXPECT_EQ(osg::PI_2, vsg::PI / 2.0);
        EXPECT_EQ(osg::PI_4, vsg::PI / 4.0);
        EXPECT_EQ(osg::PI_2f, vsg::PIf / 2.f);
        EXPECT_EQ(osg::PI_4f, vsg::PIf / 4.f);
    }

    // TRAP: OSG computes angle * PI / 180 (Math:118, 119); VSG computes angle * (PI / 180) (common.h:31, 32, numbers.h:33).
    // The operand grouping differs, so results can differ in the last bit.
    // OSG DegreesToRadians(a) equals VSG radians(a) within 1e-6 relative; RadiansToDegrees and degrees likewise.
    TEST(MathFunctionConventions, degreesAndRadians)
    {
        for (int i = -720; i <= 720; i += 7)
        {
            const float f = static_cast<float>(i) + 0.25f;
            const double d = static_cast<double>(i) + 0.25;
            EXPECT_NEAR(osg::DegreesToRadians(f), vsg::radians(f), 1e-6 * std::abs(vsg::radians(f)));
            EXPECT_NEAR(osg::DegreesToRadians(d), vsg::radians(d), 1e-6 * std::abs(vsg::radians(d)));
            EXPECT_NEAR(osg::RadiansToDegrees(f), vsg::degrees(f), 1e-6 * std::abs(vsg::degrees(f)));
            EXPECT_NEAR(osg::RadiansToDegrees(d), vsg::degrees(d), 1e-6 * std::abs(vsg::degrees(d)));
        }
    }

    // Pins for the helper. osg::sign(0) is 1, not 0, and osg::sign(-0.f) is 1 where std::copysign gives -1. Math:101, 104.
    TEST(MathFunctionConventions, signPins)
    {
        EXPECT_EQ(osg::sign(0.f), 1.f);
        EXPECT_EQ(osg::sign(-0.f), 1.f);
        EXPECT_EQ(std::copysign(1.f, -0.f), -1.f);
        EXPECT_EQ(osg::sign(-3.f), -1.f);
        EXPECT_EQ(osg::signOrZero(0.f), 0.f);
        EXPECT_EQ(osg::signOrZero(-3.f), -1.f);
        EXPECT_EQ(osg::signOrZero(3.f), 1.f);
    }

    // Pin for the helper. TRAP: OSG round is floor(v + 0.5), not std::round (Math:124), and they differ at the float just
    // below 0.5, where v + 0.5f rounds up to 1.
    TEST(MathFunctionConventions, roundPins)
    {
        const float justBelowHalf = 0.49999997f;
        EXPECT_EQ(osg::round(justBelowHalf), 1.f);
        EXPECT_EQ(std::round(justBelowHalf), 0.f);
        EXPECT_EQ(osg::round(2.5f), 3.f);
        EXPECT_EQ(osg::round(-2.5f), -3.f);
    }

    // Pin for the helper: an absolute epsilon, default 1e-6, inclusive at the limit. Math:63, 70.
    TEST(MathFunctionConventions, equivalentPins)
    {
        EXPECT_TRUE(osg::equivalent(1.0, 1.0 + 5e-7));
        EXPECT_FALSE(osg::equivalent(1.0, 1.0 + 2e-6));
        EXPECT_TRUE(osg::equivalent(1.0, 1.5, 0.5));
        EXPECT_TRUE(osg::equivalent(1.5, 1.0, 0.5));
    }
}

#endif
