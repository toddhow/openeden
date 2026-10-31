#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <components/misc/vsgmath.hpp>

#include <osg/BoundingBox>
#include <osg/Matrixd>
#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Vec2d>
#include <osg/Vec2f>
#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osg/Vec4d>
#include <osg/Vec4f>

#include <cmath>
#include <limits>

// The helpers added to components/misc/vsgmath.hpp after a review of the first set (task 0013), each against OSG's own result.
namespace
{
    using namespace MathConventions;
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
            EXPECT_TRUE(vecExact<2>(osg::Vec2f(f, -f) / fs, VM::divide(vsg::vec2(f, -f), fs)));
            EXPECT_TRUE(vecExact<3>(osg::Vec3f(f, -f, 1.f) / fs, VM::divide(vsg::vec3(f, -f, 1.f), fs)));
            EXPECT_TRUE(vecExact<4>(osg::Vec4f(f, -f, 1.f, 7.f) / fs, VM::divide(vsg::vec4(f, -f, 1.f, 7.f), fs)));

            const double d = static_cast<double>(i) * 0.37;
            const double ds = 3.0 + static_cast<double>(i) * 0.01;
            EXPECT_TRUE(vecExact<2>(osg::Vec2d(d, -d) / ds, VM::divide(vsg::dvec2(d, -d), ds)));
            EXPECT_TRUE(vecExact<3>(osg::Vec3d(d, -d, 1.0) / ds, VM::divide(vsg::dvec3(d, -d, 1.0), ds)));
            EXPECT_TRUE(vecExact<4>(osg::Vec4d(d, -d, 1.0, 7.0) / ds, VM::divide(vsg::dvec4(d, -d, 1.0, 7.0), ds)));
        }
    }

    TEST(VsgMathReview, divideNarrowsADoubleDivisorForAFloatVector)
    {
        const double s = opaque(3.3);
        EXPECT_TRUE(vecExact<3>(osg::Vec3f(1.f, 2.f, 3.f) / s, VM::divide(vsg::vec3(1.f, 2.f, 3.f), s)));
    }

    void expectSlerpSame(double t, const osg::Quat& from, const osg::Quat& to)
    {
        osg::Quat expected;
        expected.slerp(t, from, to);
        const vsg::dquat actual = VM::slerp(t, vsg::dquat(from.x(), from.y(), from.z(), from.w()),
            vsg::dquat(to.x(), to.y(), to.z(), to.w()));
        EXPECT_TRUE(vecExact<4>(expected, actual)) << "t " << t;
    }

    TEST(VsgMathReview, slerpMatchesOsg)
    {
        const osg::Quat a(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0));
        const osg::Quat b(opaque(-1.9), osg::Vec3d(-2.0, 0.5, 1.0));
        for (const double t : { 0.0, 0.25, 0.5, 0.9, 1.0 })
        {
            expectSlerpSame(t, a, b);
            expectSlerpSame(t, b, a);
            // Opposite sign: the path goes through -to.
            expectSlerpSame(t, a, osg::Quat(-b.x(), -b.y(), -b.z(), -b.w()));
        }
    }

    // Close enough for OSG's linear branch (1 - cos below 1e-5), though far above machine epsilon, where vsg::mix would not be.
    TEST(VsgMathReview, slerpUsesOsgsThreshold)
    {
        const osg::Quat a(opaque(0.7), osg::Vec3d(0.0, 0.0, 1.0));
        const osg::Quat b(opaque(0.701), osg::Vec3d(0.0, 0.0, 1.0));
        expectSlerpSame(0.3, a, b);
        const osg::Quat c(opaque(0.7 + 0.01), osg::Vec3d(0.0, 0.0, 1.0));
        expectSlerpSame(0.3, a, c);
    }

    template <class OsgMatrix, class VsgMatrix>
    void expectOrthoNormalizeSame(const OsgMatrix& source)
    {
        OsgMatrix expected;
        expected.orthoNormalize(source);
        VsgMatrix input;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                input[i][j] = static_cast<typename VsgMatrix::value_type>(source(i, j));
        const VsgMatrix actual = VM::orthoNormalize(input);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                EXPECT_EQ(expected(i, j), actual[i][j]) << i << ", " << j;
    }

    TEST(VsgMathReview, orthoNormalizeMatchesOsg)
    {
        const osg::Quat q(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0));
        osg::Matrixd scaled = osg::Matrixd::scale(2.0, 3.0, 0.5) * osg::Matrixd::rotate(q);
        scaled.setTrans(osg::Vec3d(10.0, -20.0, 30.0));
        expectOrthoNormalizeSame<osg::Matrixd, vsg::dmat4>(scaled);
        expectOrthoNormalizeSame<osg::Matrixf, vsg::mat4>(osg::Matrixf(scaled));

        expectOrthoNormalizeSame<osg::Matrixd, vsg::dmat4>(osg::Matrixd::rotate(q));
        expectOrthoNormalizeSame<osg::Matrixd, vsg::dmat4>(osg::Matrixd::scale(0.0, 1.0, 2.0));

        // A projective last column is copied, not touched.
        osg::Matrixd projective = scaled;
        projective(0, 3) = 0.5;
        projective(3, 3) = 2.0;
        expectOrthoNormalizeSame<osg::Matrixd, vsg::dmat4>(projective);
    }

    TEST(VsgMathReview, intersectMatchesOsgOnNanAndNegativeZero)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double values[] = { 0.0, -0.0, 1.0, nan };
        for (const double l : values)
            for (const double r : values)
            {
                const osg::BoundingBoxd oa(l, l, l, 2.0, 2.0, 2.0);
                const osg::BoundingBoxd ob(r, r, r, 3.0, 3.0, 3.0);
                const osg::BoundingBoxd expected = oa.intersect(ob);
                const vsg::dbox actual = VM::intersect(
                    vsg::dbox(vsg::dvec3(l, l, l), vsg::dvec3(2.0, 2.0, 2.0)),
                    vsg::dbox(vsg::dvec3(r, r, r), vsg::dvec3(3.0, 3.0, 3.0)));
                const double e = expected.xMin();
                const double a = actual.min.x;
                EXPECT_EQ(std::isnan(e), std::isnan(a)) << l << ", " << r;
                if (!std::isnan(e))
                {
                    EXPECT_EQ(e, a) << l << ", " << r;
                    EXPECT_EQ(std::signbit(e), std::signbit(a)) << l << ", " << r;
                }
            }
    }

    TEST(VsgMathReview, getLookAtOnAFloatMatrixNarrowsTheDistance)
    {
        const osg::Quat q(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0));
        osg::Matrixf om = osg::Matrixf::rotate(q);
        om.setTrans(osg::Vec3f(1000000.f, -2.f, 3.f));
        vsg::mat4 vm;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                vm[i][j] = om(i, j);

        const double distance = opaque(0.1); // Not exact in a float, so a double vector shows the narrowing.
        osg::Vec3d oEye, oCenter, oUp;
        om.getLookAt(oEye, oCenter, oUp, static_cast<float>(distance));
        vsg::dvec3 vEye, vCenter, vUp;
        VM::getLookAt(vm, vEye, vCenter, vUp, distance);
        EXPECT_TRUE(vecExact<3>(oEye, vEye));
        EXPECT_TRUE(vecExact<3>(oCenter, vCenter));
        EXPECT_TRUE(vecExact<3>(oUp, vUp));
    }
}

#endif
