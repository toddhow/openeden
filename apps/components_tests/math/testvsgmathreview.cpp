#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/transform.h>

#include <cmath>
#include <limits>

// The helpers added to components/misc/vsgmath.hpp after a review of the first set (task 0013), each against OSG's own
// result.
namespace
{
    using namespace MathConventions;

#include "testvsgmathreview.golden.inc"
    namespace VM = Misc::VsgMath;

    double opaque(double value)
    {
        volatile double stored = value;
        return stored;
    }

    TEST(VsgMathReview, divideMatchesOsgInEveryForm)
    {
        // A divisor whose inverse is inexact, over many numerators, so that multiplying by the inverse would show.
        for (int i = 1; i < 200; ++i)
        {
            const float f = static_cast<float>(i) * 0.37f;
            const float fs = 3.f + static_cast<float>(i) * 0.01f;
            EXPECT_TRUE(vecExact<2>(MathConventions::golden(kGolden0), VM::divide(vsg::vec2(f, -f), fs)));
            EXPECT_TRUE(vecExact<3>(MathConventions::golden(kGolden1), VM::divide(vsg::vec3(f, -f, 1.f), fs)));
            EXPECT_TRUE(vecExact<4>(MathConventions::golden(kGolden2), VM::divide(vsg::vec4(f, -f, 1.f, 7.f), fs)));

            const double d = static_cast<double>(i) * 0.37;
            const double ds = 3.0 + static_cast<double>(i) * 0.01;
            EXPECT_TRUE(vecExact<2>(MathConventions::golden(kGolden3), VM::divide(vsg::dvec2(d, -d), ds)));
            EXPECT_TRUE(vecExact<3>(MathConventions::golden(kGolden4), VM::divide(vsg::dvec3(d, -d, 1.0), ds)));
            EXPECT_TRUE(vecExact<4>(MathConventions::golden(kGolden5), VM::divide(vsg::dvec4(d, -d, 1.0, 7.0), ds)));
        }
    }

    TEST(VsgMathReview, divideNarrowsADoubleDivisorForAFloatVector)
    {
        const double s = opaque(3.3);
        EXPECT_TRUE(
            vecExact<3>(vsg::vec3(0.303030312f, 0.606060624f, 0.909090936f), VM::divide(vsg::vec3(1.f, 2.f, 3.f), s)));
    }

    void expectSlerpSame(double t, const vsg::dquat& from, const vsg::dquat& to)
    {
        const vsg::dquat actual
            = VM::slerp(t, vsg::dquat(from.x, from.y, from.z, from.w), vsg::dquat(to.x, to.y, to.z, to.w));
        EXPECT_TRUE(vecExact<4>(MathConventions::golden(kGolden7), actual)) << "t " << t;
    }

    TEST(VsgMathReview, slerpMatchesOsg)
    {
        const vsg::dquat a(opaque(0.7), vsg::dvec3(1.0, 2.0, 3.0));
        const vsg::dquat b(opaque(-1.9), vsg::dvec3(-2.0, 0.5, 1.0));
        for (const double t : { 0.0, 0.25, 0.5, 0.9, 1.0 })
        {
            expectSlerpSame(t, a, b);
            expectSlerpSame(t, b, a);
            // Opposite sign: the path goes through -to.
            expectSlerpSame(t, a, vsg::dquat(-b.x, -b.y, -b.z, -b.w));
        }
    }

    // Close enough for OSG's linear branch (1 - cos below 1e-5), though far above machine epsilon, where vsg::mix would
    // not be.
    TEST(VsgMathReview, slerpUsesOsgsThreshold)
    {
        const vsg::dquat a(opaque(0.7), vsg::dvec3(0.0, 0.0, 1.0));
        const vsg::dquat b(opaque(0.701), vsg::dvec3(0.0, 0.0, 1.0));
        expectSlerpSame(0.3, a, b);
        const vsg::dquat c(opaque(0.7 + 0.01), vsg::dvec3(0.0, 0.0, 1.0));
        expectSlerpSame(0.3, a, c);
    }

    template <class OsgMatrix, class VsgMatrix>
    void expectOrthoNormalizeSame(const OsgMatrix& source)
    {
        VsgMatrix input;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                input[i][j] = static_cast<typename VsgMatrix::value_type>(source(i, j));
        const VsgMatrix actual = VM::orthoNormalize(input);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                EXPECT_TRUE(recordedEqual(MathConventions::golden(kGolden8).real(), actual[i][j])) << i << ", " << j;
    }

    TEST(VsgMathReview, orthoNormalizeMatchesOsg)
    {
        const vsg::dquat q(opaque(0.7), vsg::dvec3(1.0, 2.0, 3.0));
        // OSG's scale(2, 3, 0.5) * rotate(q), as it came out.
        vsg::dmat4 scaled = matOf<double>(1.56327834781405, 1.1002344614087167, -0.58791575687716113, 0.,
            -1.4487878526426368, 2.4960904013239036, 0.81886901666494305, 0., 0.19736989908689989,
            -0.035696249708937922, 0.45800753344365863, 0., 10., -20., 30., 1.);
        expectOrthoNormalizeSame<vsg::dmat4, vsg::dmat4>(scaled);
        expectOrthoNormalizeSame<vsg::mat4, vsg::mat4>(vsg::mat4(scaled));

        expectOrthoNormalizeSame<vsg::dmat4, vsg::dmat4>(matOf<double>(0.78163917390702498, 0.55011723070435836,
            -0.29395787843858057, 0., -0.48292928421421222, 0.83203013377463453, 0.27295633888831433, 0.,
            0.39473979817379978, -0.071392499417875843, 0.91601506688731726, 0., 0., 0., 0., 1.));
        expectOrthoNormalizeSame<vsg::dmat4, vsg::dmat4>(vsg::scale(0.0, 1.0, 2.0));

        // A projective last column is copied, not touched.
        vsg::dmat4 projective = scaled;
        projective(0, 3) = 0.5;
        projective(3, 3) = 2.0;
        expectOrthoNormalizeSame<vsg::dmat4, vsg::dmat4>(projective);
    }

    TEST(VsgMathReview, intersectMatchesOsgOnNanAndNegativeZero)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double values[] = { 0.0, -0.0, 1.0, nan };
        for (const double l : values)
            for (const double r : values)
            {
                const vsg::dbox actual = VM::intersect(vsg::dbox(vsg::dvec3(l, l, l), vsg::dvec3(2.0, 2.0, 2.0)),
                    vsg::dbox(vsg::dvec3(r, r, r), vsg::dvec3(3.0, 3.0, 3.0)));
                const double a = actual.min.x;
                EXPECT_EQ(MathConventions::golden(kGolden9).flag(), std::isnan(a)) << l << ", " << r;
                if (!std::isnan(a))
                {
                    EXPECT_TRUE(recordedEqual(MathConventions::golden(kGolden10).real(), a)) << l << ", " << r;
                    EXPECT_EQ(MathConventions::golden(kGolden11).flag(), std::signbit(a)) << l << ", " << r;
                }
            }
    }

    TEST(VsgMathReview, compareEqualMatchesOsgsMatrixEquality)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float values[] = { 0.f, -0.f, 1.f, nan };
        for (const float l : values)
            for (const float r : values)
            {
                vsg::mat4 oLeft, oRight;
                oLeft(2, 3) = l;
                oRight(2, 3) = r;
                vsg::mat4 vLeft, vRight;
                vLeft[2][3] = l;
                vRight[2][3] = r;
                EXPECT_EQ(MathConventions::golden(kGolden12).flag(), VM::compareEqual(vLeft, vRight)) << l << ", " << r;
            }

        // The reason it exists: a NaN counts as equal to anything, where VSG's == says it differs even from itself.
        vsg::mat4 withNaN;
        withNaN[0][1] = nan;
        EXPECT_TRUE(VM::compareEqual(withNaN, vsg::mat4()));
        EXPECT_FALSE(withNaN == withNaN);

        vsg::dmat4 dLeft, dRight;
        dRight[3][3] = 2.0;
        EXPECT_FALSE(VM::compareEqual(dLeft, dRight));
    }

    TEST(VsgMathReview, getLookAtOnAFloatMatrixNarrowsTheDistance)
    {
        const vsg::dquat q(opaque(0.7), vsg::dvec3(1.0, 2.0, 3.0));
        vsg::mat4 om = vsg::mat4(matOf<double>(0.78163917390702498, 0.55011723070435836, -0.29395787843858057, 0.,
            -0.48292928421421222, 0.83203013377463453, 0.27295633888831433, 0., 0.39473979817379978,
            -0.071392499417875843, 0.91601506688731726, 0., 1000000., -2., 3., 1.));
        vsg::mat4 vm;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                vm[i][j] = om(i, j);

        const double distance = opaque(0.1); // Not exact in a float, so a double vector shows the narrowing.
        vsg::dvec3 oEye, oCenter, oUp;
        vsg::dvec3 vEye, vCenter, vUp;
        VM::getLookAt(vm, vEye, vCenter, vUp, distance);
        EXPECT_TRUE(vecFromFloats<3>(vsg::dvec3(-781637.1875, 482930.1875, -394742.71875), vEye));
        EXPECT_TRUE(vecFromFloats<3>(vsg::dvec3(-781637.1581042113, 482930.1602043661, -394742.81035150803), vCenter));
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(0.5501172542572021, 0.8320301175117493, -0.0713924989104271), vUp));
    }
}

#endif
