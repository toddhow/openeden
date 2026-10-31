#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <components/misc/vsgmath.hpp>

#include <osg/BoundingBox>
#include <osg/BoundingSphere>
#include <osg/Math>
#include <osg/Matrixd>
#include <osg/Matrixf>
#include <osg/Plane>
#include <osg/Quat>
#include <osg/Vec2d>
#include <osg/Vec2f>
#include <osg/Vec2i>
#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osg/Vec4d>
#include <osg/Vec4f>

#include <vsg/maths/box.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/plane.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/sphere.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <type_traits>

// Tests for components/misc/vsgmath.hpp (docs/tasks/0013-vulkan-v5-vsg-math.md), the helpers listed in the "HELPERS" comment
// at the top of testvectorconventions.cpp, testmatrixconventions.cpp, testquatconventions.cpp and testboundsconventions.cpp.
// Each test runs the OSG expression and the helper on the same inputs.
//
// Exact comparison is the rule: these helpers exist so that OSG's result survives where VSG's own function differs. The few
// tolerances each say why: they are the ones that end in a whole matrix product, whose last bit differs between GCC and MSVC
// even with no VSG involved (task 0013, note 3; testmatrixconventions.cpp, productOrderFlips), or that OSG computes through
// its general inverse.
//
// Not covered: makeOpenGlPerspective and makeOpenGlOrtho, which testmatrixconventions.cpp asks for only "if a caller remains
// that needs OSG's clip space"; and the "valid" half of isNaN, since OSG's v.valid() is just !v.isNaN() (Vec3f:89).
namespace
{
    using namespace MathConventions;
    namespace VM = Misc::VsgMath;

    // Like vecExact and matExact, but two NaNs count as the same result. OSG's own answer is NaN on several edge cases below.
    bool sameValue(double x, double y)
    {
        return x == y || (std::isnan(x) && std::isnan(y));
    }

    template <int N, class A, class B>
    ::testing::AssertionResult vecSame(const A& a, const B& b)
    {
        for (int i = 0; i < N; ++i)
        {
            const double x = static_cast<double>(a[i]);
            const double y = static_cast<double>(b[i]);
            if (!sameValue(x, y))
                return ::testing::AssertionFailure() << "element " << i << ": " << x << " != " << y;
        }
        return ::testing::AssertionSuccess();
    }

    template <class A, class B>
    ::testing::AssertionResult matSame(const A& a, const B& b)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                const double x = static_cast<double>(a(i, j));
                const double y = static_cast<double>(b(i, j));
                if (!sameValue(x, y))
                    return ::testing::AssertionFailure() << "(" << i << "," << j << "): " << x << " != " << y;
            }
        return ::testing::AssertionSuccess();
    }

    // Read through volatile so the compiler cannot fold sin, cos or atan2 here while OSG calls them at run time in its own
    // translation unit; a folded value can differ in the last bit from the C library's (as in testquatconventions.cpp).
    double opaque(double value)
    {
        volatile double stored = value;
        return stored;
    }

    template <class T>
    struct Osg;

    template <>
    struct Osg<float>
    {
        using Vec2 = osg::Vec2f;
        using Vec3 = osg::Vec3f;
        using Vec4 = osg::Vec4f;
        using Matrix = osg::Matrixf;
        using Box = osg::BoundingBoxf;
        using Sphere = osg::BoundingSpheref;
    };

    template <>
    struct Osg<double>
    {
        using Vec2 = osg::Vec2d;
        using Vec3 = osg::Vec3d;
        using Vec4 = osg::Vec4d;
        using Matrix = osg::Matrixd;
        using Box = osg::BoundingBoxd;
        using Sphere = osg::BoundingSphered;
    };

    template <class T>
    using OVec2 = typename Osg<T>::Vec2;
    template <class T>
    using OVec3 = typename Osg<T>::Vec3;
    template <class T>
    using OVec4 = typename Osg<T>::Vec4;
    template <class T>
    using OMatrix = typename Osg<T>::Matrix;
    template <class T>
    using OBox = typename Osg<T>::Box;
    template <class T>
    using OSphere = typename Osg<T>::Sphere;

    // The same numbers in the same storage: never transposed (testmatrixconventions.cpp, "THE ONE RULE").
    vsg::vec2 toVsg(const osg::Vec2f& v)
    {
        return vsg::vec2(v.x(), v.y());
    }
    vsg::dvec2 toVsg(const osg::Vec2d& v)
    {
        return vsg::dvec2(v.x(), v.y());
    }
    vsg::vec3 toVsg(const osg::Vec3f& v)
    {
        return vsg::vec3(v.x(), v.y(), v.z());
    }
    vsg::dvec3 toVsg(const osg::Vec3d& v)
    {
        return vsg::dvec3(v.x(), v.y(), v.z());
    }
    vsg::vec4 toVsg(const osg::Vec4f& v)
    {
        return vsg::vec4(v.x(), v.y(), v.z(), v.w());
    }
    vsg::dvec4 toVsg(const osg::Vec4d& v)
    {
        return vsg::dvec4(v.x(), v.y(), v.z(), v.w());
    }
    vsg::dquat toVsg(const osg::Quat& q)
    {
        return vsg::dquat(q.x(), q.y(), q.z(), q.w());
    }
    vsg::mat4 toVsg(const osg::Matrixf& m)
    {
        vsg::mat4 result;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result[i][j] = m(i, j);
        return result;
    }
    vsg::dmat4 toVsg(const osg::Matrixd& m)
    {
        vsg::dmat4 result;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result[i][j] = m(i, j);
        return result;
    }
    // Keeps OSG's FLT_MAX sentinel in a default double box (BoundingBox:45-52): the numbers carry over, as with matrices.
    vsg::box toVsg(const osg::BoundingBoxf& b)
    {
        return vsg::box(toVsg(b._min), toVsg(b._max));
    }
    vsg::dbox toVsg(const osg::BoundingBoxd& b)
    {
        return vsg::dbox(toVsg(b._min), toVsg(b._max));
    }
    vsg::sphere toVsg(const osg::BoundingSpheref& s)
    {
        return vsg::sphere(toVsg(s._center), s._radius);
    }
    vsg::dsphere toVsg(const osg::BoundingSphered& s)
    {
        return vsg::dsphere(toVsg(s._center), s._radius);
    }

    template <class T>
    OVec2<T> oVec2(double x, double y)
    {
        return OVec2<T>(static_cast<T>(x), static_cast<T>(y));
    }

    template <class T>
    OVec3<T> oVec3(double x, double y, double z)
    {
        return OVec3<T>(static_cast<T>(x), static_cast<T>(y), static_cast<T>(z));
    }

    template <class T>
    OVec4<T> oVec4(double x, double y, double z, double w)
    {
        return OVec4<T>(static_cast<T>(x), static_cast<T>(y), static_cast<T>(z), static_cast<T>(w));
    }

    template <class T>
    OBox<T> oBox(double xMin, double yMin, double zMin, double xMax, double yMax, double zMax)
    {
        return OBox<T>(oVec3<T>(xMin, yMin, zMin), oVec3<T>(xMax, yMax, zMax));
    }

    template <class T>
    OSphere<T> oSphere(double x, double y, double z, double radius)
    {
        return OSphere<T>(oVec3<T>(x, y, z), static_cast<T>(radius));
    }

    template <class T>
    OMatrix<T> oMatrix(double a00, double a01, double a02, double a03, double a10, double a11, double a12, double a13,
        double a20, double a21, double a22, double a23, double a30, double a31, double a32, double a33)
    {
        return OMatrix<T>(static_cast<T>(a00), static_cast<T>(a01), static_cast<T>(a02), static_cast<T>(a03),
            static_cast<T>(a10), static_cast<T>(a11), static_cast<T>(a12), static_cast<T>(a13), static_cast<T>(a20),
            static_cast<T>(a21), static_cast<T>(a22), static_cast<T>(a23), static_cast<T>(a30), static_cast<T>(a31),
            static_cast<T>(a32), static_cast<T>(a33));
    }

    // Sixteen distinct values with a non-trivial last column.
    template <class T>
    OMatrix<T> generalMatrix()
    {
        return oMatrix<T>(0.5, 1.5, -2., 0.25, 3., -0.75, 4.5, -0.125, -1., 6., 0.3, 0.1, 7., -8., 9., 2.);
    }

    // Affine: last column (0, 0, 0, 1).
    template <class T>
    OMatrix<T> affineMatrix()
    {
        return oMatrix<T>(1.1, -0.2, 0.3, 0., 0.4, 0.9, -0.6, 0., 0.7, 0.8, 1.3, 0., -5., 2.5, 1.25, 1.);
    }

    using Precisions = ::testing::Types<float, double>;

    // ------------------------------------------------------------------------------------------------------------- vectors

    template <class T>
    class VsgMathVector : public ::testing::Test
    {
    };
    TYPED_TEST_SUITE(VsgMathVector, Precisions);

    template <int N, class O, class V>
    void expectNormalizeInPlaceMatches(O o, V v)
    {
        static_assert(std::is_same_v<decltype(VM::normalizeInPlace(v)), typename V::value_type>);
        const auto oLength = o.normalize();
        const auto vLength = VM::normalizeInPlace(v);
        EXPECT_EQ(oLength, vLength);
        EXPECT_TRUE(vecExact<N>(o, v));
    }

    // OSG v.normalize(): changes v and returns the previous length. Vec2f:163, Vec3f:190, Vec4f:226.
    TYPED_TEST(VsgMathVector, normalizeInPlaceOfNonZero)
    {
        using T = TypeParam;
        for (int i = 1; i <= 100; ++i)
        {
            const double d = i * 0.37;
            const OVec2<T> u2 = oVec2<T>(d, 1.0 - d);
            const OVec3<T> u3 = oVec3<T>(d, 1.0 - d, d * d);
            const OVec4<T> u4 = oVec4<T>(d, 1.0 - d, d * d, -3.0);
            expectNormalizeInPlaceMatches<2>(u2, toVsg(u2));
            expectNormalizeInPlaceMatches<3>(u3, toVsg(u3));
            expectNormalizeInPlaceMatches<4>(u4, toVsg(u4));
        }
    }

    // TRAP: vsg::normalize makes NaN from a zero vector (vec3.h:252); OSG leaves it at zero and returns 0 (Vec3f:193).
    TYPED_TEST(VsgMathVector, normalizeInPlaceOfZeroStaysZero)
    {
        using T = TypeParam;
        const OVec2<T> u2 = oVec2<T>(0.0, 0.0);
        const OVec3<T> u3 = oVec3<T>(0.0, 0.0, 0.0);
        const OVec4<T> u4 = oVec4<T>(0.0, 0.0, 0.0, 0.0);
        expectNormalizeInPlaceMatches<2>(u2, toVsg(u2));
        expectNormalizeInPlaceMatches<3>(u3, toVsg(u3));
        expectNormalizeInPlaceMatches<4>(u4, toVsg(u4));

        auto v = toVsg(u3);
        EXPECT_EQ(VM::normalizeInPlace(v), T(0));
        EXPECT_TRUE(vecExact<3>(v, u3));
    }

    // A vector so short that its squared length underflows measures 0, so it too stays as it was, non-zero.
    TEST(VsgMathVector, normalizeInPlaceWhenTheSquaredLengthUnderflows)
    {
        const osg::Vec3f o(1e-30f, 0.f, 0.f);
        expectNormalizeInPlaceMatches<3>(o, toVsg(o));
        vsg::vec3 v = toVsg(o);
        EXPECT_EQ(VM::normalizeInPlace(v), 0.f);
        EXPECT_EQ(v.x, 1e-30f);
    }

    // OSG a * b for Vec4 is a dot product (Vec4f:129); VSG has no dot for vec4.
    TYPED_TEST(VsgMathVector, dotOfVec4)
    {
        using T = TypeParam;
        static_assert(std::is_same_v<decltype(VM::dot(vsg::t_vec4<T>(), vsg::t_vec4<T>())), T>);
        for (int i = 1; i <= 100; ++i)
        {
            const double d = i * 0.37;
            const OVec4<T> a = oVec4<T>(d, -2.25, 1.0 / d, 0.1);
            const OVec4<T> b = oVec4<T>(0.7, d * d, -0.3, d);
            EXPECT_EQ(a * b, VM::dot(toVsg(a), toVsg(b)));
        }
    }

    // OSG's free function osg::componentMultiply. Vec2f:178, Vec2d:181, Vec3f:206, Vec3d:212, Vec4f:255, Vec4d:286.
    TYPED_TEST(VsgMathVector, componentMultiply)
    {
        using T = TypeParam;
        const OVec2<T> a2 = oVec2<T>(1.5, -0.3);
        const OVec2<T> b2 = oVec2<T>(0.7, 3.1);
        const OVec3<T> a3 = oVec3<T>(1.5, -0.3, 11.0);
        const OVec3<T> b3 = oVec3<T>(0.7, 3.1, 0.1);
        const OVec4<T> a4 = oVec4<T>(1.5, -0.3, 11.0, 0.9);
        const OVec4<T> b4 = oVec4<T>(0.7, 3.1, 0.1, -1.7);

        static_assert(std::is_same_v<decltype(VM::componentMultiply(toVsg(a3), toVsg(b3))), vsg::t_vec3<T>>);
        EXPECT_TRUE(vecExact<2>(osg::componentMultiply(a2, b2), VM::componentMultiply(toVsg(a2), toVsg(b2))));
        EXPECT_TRUE(vecExact<3>(osg::componentMultiply(a3, b3), VM::componentMultiply(toVsg(a3), toVsg(b3))));
        EXPECT_TRUE(vecExact<4>(osg::componentMultiply(a4, b4), VM::componentMultiply(toVsg(a4), toVsg(b4))));
    }

    // OSG's free function osg::componentDivide. Vec2f:184, Vec2d:187, Vec3f:212, Vec3d:218, Vec4f:261, Vec4d:292.
    // Over many inputs, so a reciprocal in the helper shows (see scalarDivide in testvectorconventions.cpp).
    TYPED_TEST(VsgMathVector, componentDivide)
    {
        using T = TypeParam;
        for (int i = 1; i <= 1000; ++i)
        {
            const double d = i;
            const OVec2<T> a2 = oVec2<T>(d, d + 0.5);
            const OVec2<T> b2 = oVec2<T>(3.0, 7.0);
            const OVec3<T> a3 = oVec3<T>(d, d + 0.5, -d);
            const OVec3<T> b3 = oVec3<T>(3.0, 7.0, 0.3);
            const OVec4<T> a4 = oVec4<T>(d, d + 0.5, -d, d * 0.1);
            const OVec4<T> b4 = oVec4<T>(3.0, 7.0, 0.3, 11.0);
            EXPECT_TRUE(vecExact<2>(osg::componentDivide(a2, b2), VM::componentDivide(toVsg(a2), toVsg(b2))));
            EXPECT_TRUE(vecExact<3>(osg::componentDivide(a3, b3), VM::componentDivide(toVsg(a3), toVsg(b3))));
            EXPECT_TRUE(vecExact<4>(osg::componentDivide(a4, b4), VM::componentDivide(toVsg(a4), toVsg(b4))));
        }
    }

    // Division by a zero component gives infinity, and 0 / 0 gives NaN, per element.
    TYPED_TEST(VsgMathVector, componentDivideByZero)
    {
        using T = TypeParam;
        const OVec3<T> a = oVec3<T>(1.0, 0.0, -1.0);
        const OVec3<T> b = oVec3<T>(0.0, 0.0, 0.0);
        EXPECT_TRUE(vecSame<3>(osg::componentDivide(a, b), VM::componentDivide(toVsg(a), toVsg(b))));
    }

    // osg::Vec2i + int and - int. Vec2i:85, 90.
    TEST(VsgMathVector, addAndSubtractIntOnIvec2)
    {
        for (const int n : { -3, 0, 1, 7 })
        {
            const osg::Vec2i o(7, -7);
            const vsg::ivec2 v(7, -7);
            EXPECT_TRUE(vecExact<2>(o + n, VM::add(v, n)));
            EXPECT_TRUE(vecExact<2>(o - n, VM::sub(v, n)));
        }
    }

    template <int N, class O, class V>
    void expectIsNaNMatches()
    {
        using T = typename V::value_type;
        O o;
        V v;
        EXPECT_FALSE(o.isNaN());
        EXPECT_FALSE(VM::isNaN(v));
        for (int i = 0; i < N; ++i)
        {
            O oInfinite;
            V vInfinite;
            oInfinite[i] = std::numeric_limits<T>::infinity();
            vInfinite[i] = std::numeric_limits<T>::infinity();
            EXPECT_EQ(oInfinite.isNaN(), VM::isNaN(vInfinite));
            EXPECT_FALSE(VM::isNaN(vInfinite));

            O oNan;
            V vNan;
            oNan[i] = std::numeric_limits<T>::quiet_NaN();
            vNan[i] = std::numeric_limits<T>::quiet_NaN();
            EXPECT_EQ(oNan.isNaN(), VM::isNaN(vNan));
            EXPECT_TRUE(VM::isNaN(vNan));
        }
    }

    // Any one NaN element counts; infinity does not. Vec2f:76, Vec3f:91, Vec4f:126.
    TYPED_TEST(VsgMathVector, isNaN)
    {
        using T = TypeParam;
        expectIsNaNMatches<2, OVec2<T>, vsg::t_vec2<T>>();
        expectIsNaNMatches<3, OVec3<T>, vsg::t_vec3<T>>();
        expectIsNaNMatches<4, OVec4<T>, vsg::t_vec4<T>>();
    }

    // ------------------------------------------------------------------------------------------------- scalar functions

    template <class T>
    class VsgMathScalar : public ::testing::Test
    {
    };
    TYPED_TEST_SUITE(VsgMathScalar, Precisions);

    // TRAP: sign(0) and sign(-0) are 1, where std::copysign gives -1 for -0. NaN compares false, so it gives 1. Math:101.
    TYPED_TEST(VsgMathScalar, sign)
    {
        using T = TypeParam;
        const T values[] = { T(0), -T(0), T(3), T(-3), std::numeric_limits<T>::infinity(),
            -std::numeric_limits<T>::infinity(), std::numeric_limits<T>::quiet_NaN() };
        for (const T value : values)
        {
            EXPECT_EQ(osg::sign(value), VM::sign(value));
            static_assert(std::is_same_v<decltype(VM::sign(value)), T>);
        }
    }

    // signOrZero of 0, -0 and NaN is 0. Math:104.
    TYPED_TEST(VsgMathScalar, signOrZero)
    {
        using T = TypeParam;
        const T values[] = { T(0), -T(0), T(3), T(-3), std::numeric_limits<T>::infinity(),
            -std::numeric_limits<T>::infinity(), std::numeric_limits<T>::quiet_NaN() };
        for (const T value : values)
        {
            EXPECT_EQ(osg::signOrZero(value), VM::signOrZero(value));
            static_assert(std::is_same_v<decltype(VM::signOrZero(value)), T>);
        }
    }

    // osg::sign is a template, and integer callers exist for it as for any other type. Math:100-104.
    TEST(VsgMathScalar, signOfIntegers)
    {
        for (const int value : { -5, 0, 5 })
        {
            EXPECT_EQ(osg::sign(value), VM::sign(value));
            EXPECT_EQ(osg::signOrZero(value), VM::signOrZero(value));
        }
    }

    // An absolute epsilon, default 1e-6, inclusive at the limit, and false for NaN or two equal infinities, whose difference
    // is NaN. Math:63, 70.
    TYPED_TEST(VsgMathScalar, equivalent)
    {
        using T = TypeParam;
        const T infinity = std::numeric_limits<T>::infinity();
        const T nan = std::numeric_limits<T>::quiet_NaN();
        const T values[] = { T(0), T(1), T(1) + T(5e-7), T(1) + T(2e-6), T(1.5), T(-1), infinity, nan };
        for (const T a : values)
            for (const T b : values)
            {
                EXPECT_EQ(osg::equivalent(a, b), VM::equivalent(a, b));
                EXPECT_EQ(osg::equivalent(a, b, T(0.5)), VM::equivalent(a, b, T(0.5)));
            }
        EXPECT_TRUE(VM::equivalent(T(1), T(1.5), T(0.5)));
        EXPECT_TRUE(VM::equivalent(T(1.5), T(1), T(0.5)));
        EXPECT_FALSE(VM::equivalent(infinity, infinity));
        EXPECT_FALSE(VM::equivalent(nan, nan));
    }

    // TRAP: floor(v + 0.5) for v >= 0, ceil(v - 0.5) below, not std::round (Math:124). They differ at the value just below
    // 0.5, where v + 0.5 rounds up to 1.
    TEST(VsgMathScalar, roundFloat)
    {
        const float values[] = { 0.f, -0.f, 0.5f, -0.5f, 0.49999997f, -0.49999997f, 2.5f, -2.5f, 3.4f, -3.6f, 8388609.f,
            1e30f };
        for (const float value : values)
            EXPECT_EQ(osg::round(value), VM::round(value)) << value;
        EXPECT_EQ(VM::round(0.49999997f), 1.f);
        EXPECT_EQ(VM::round(-0.49999997f), -1.f);
        EXPECT_TRUE(std::isnan(VM::round(std::numeric_limits<float>::quiet_NaN())));
    }

    TEST(VsgMathScalar, roundDouble)
    {
        const double values[] = { 0.0, -0.0, 0.5, -0.5, 0.49999999999999994, -0.49999999999999994, 2.5, -2.5, 3.4, -3.6,
            4503599627370497.0, 1e300 };
        for (const double value : values)
            EXPECT_EQ(osg::round(value), VM::round(value)) << value;
        EXPECT_EQ(VM::round(0.49999999999999994), 1.0);
        EXPECT_TRUE(std::isnan(VM::round(std::numeric_limits<double>::quiet_NaN())));
    }

    // Math:31, 32, 36, 37.
    TEST(VsgMathScalar, piConstants)
    {
        EXPECT_EQ(osg::PI_2, VM::PI_2);
        EXPECT_EQ(osg::PI_4, VM::PI_4);
        EXPECT_EQ(osg::PI_2f, VM::PI_2f);
        EXPECT_EQ(osg::PI_4f, VM::PI_4f);
        static_assert(std::is_same_v<std::decay_t<decltype(VM::PI_2)>, double>);
        static_assert(std::is_same_v<std::decay_t<decltype(VM::PI_2f)>, float>);
    }

    // ------------------------------------------------------------------------------------------------------------ matrices

    template <class T>
    class VsgMathMatrix : public ::testing::Test
    {
    };
    TYPED_TEST_SUITE(VsgMathMatrix, Precisions);

    // OSG returns Vec3d from both matrix types. Matrixf:348, Matrixd:346.
    TYPED_TEST(VsgMathMatrix, getTransIsDouble)
    {
        using T = TypeParam;
        const OMatrix<T> o = generalMatrix<T>();
        const vsg::t_mat4<T> v = toVsg(o);
        static_assert(std::is_same_v<decltype(VM::getTrans(v)), vsg::dvec3>);
        EXPECT_TRUE(vecExact<3>(o.getTrans(), VM::getTrans(v)));
    }

    template <class T, class U>
    void expectSetTransMatches()
    {
        OMatrix<T> o = generalMatrix<T>();
        vsg::t_mat4<T> v = toVsg(o);
        const OVec3<U> t = oVec3<U>(1.1, -2.2, 3.3);
        o.setTrans(t);
        VM::setTrans(v, toVsg(t));
        EXPECT_TRUE(matExact(o, v));
    }

    // Only the three elements change, and the last column stays. Matrix_implementation.cpp:320. All four precision mixes.
    TEST(VsgMathMatrix, setTrans)
    {
        expectSetTransMatches<float, float>();
        expectSetTransMatches<float, double>();
        expectSetTransMatches<double, float>();
        expectSetTransMatches<double, double>();
    }

    // TRAP: OSG getScale reads across the storage (Matrixf:350-355), not length(m[i].xyz). With a rotation and a non-uniform
    // scale its answer is not the scale at all, and that is what must be kept (apps/openmw/mwphysics/object.cpp:248).
    TYPED_TEST(VsgMathMatrix, getScale)
    {
        using T = TypeParam;
        const OMatrix<T> inputs[] = {
            generalMatrix<T>(),
            affineMatrix<T>(),
            OMatrix<T>::scale(T(2), T(3), T(4)) * OMatrix<T>::rotate(T(osg::PI_2), oVec3<T>(0.0, 0.0, 1.0)),
            OMatrix<T>::scale(T(-2), T(3), T(4)),
            OMatrix<T>::scale(T(0), T(1), T(1)),
        };
        for (const OMatrix<T>& o : inputs)
        {
            static_assert(std::is_same_v<decltype(VM::getScale(toVsg(o))), vsg::dvec3>);
            EXPECT_TRUE(vecExact<3>(o.getScale(), VM::getScale(toVsg(o))));
        }
    }

    template <class T, class U>
    void expectTransform3x3Matches()
    {
        const OMatrix<T> o = generalMatrix<T>();
        const vsg::t_mat4<T> v = toVsg(o);
        const OVec3<U> oV = oVec3<U>(1.5, -2.25, 3.75);
        const vsg::t_vec3<U> vV = toVsg(oV);

        static_assert(std::is_same_v<decltype(VM::transform3x3(vV, v)), vsg::t_vec3<U>>);
        static_assert(std::is_same_v<decltype(VM::transform3x3(v, vV)), vsg::t_vec3<U>>);
        EXPECT_TRUE(vecExact<3>(OMatrix<T>::transform3x3(oV, o), VM::transform3x3(vV, v)));
        EXPECT_TRUE(vecExact<3>(OMatrix<T>::transform3x3(o, oV), VM::transform3x3(v, vV)));
    }

    // The order of the arguments keeps OSG's meaning: (v, m) is v * M and (m, v) is M * v, in OSG's reading. The result takes
    // the vector's type, computed with both operands' precision. Matrixf:769-793, Matrixd:665-690.
    TEST(VsgMathMatrix, transform3x3)
    {
        expectTransform3x3Matches<float, float>();
        expectTransform3x3Matches<float, double>();
        expectTransform3x3Matches<double, float>();
        expectTransform3x3Matches<double, double>();
    }

    template <class T, class U>
    void expectInPlaceTranslateAndScaleMatch(const OVec3<U>& t)
    {
        const OMatrix<T> base = generalMatrix<T>();

        OMatrix<T> o = base;
        vsg::t_mat4<T> v = toVsg(base);
        o.preMultTranslate(t);
        VM::preMultTranslate(v, toVsg(t));
        EXPECT_TRUE(matExact(o, v));

        o = base;
        v = toVsg(base);
        o.postMultTranslate(t);
        VM::postMultTranslate(v, toVsg(t));
        EXPECT_TRUE(matExact(o, v));

        o = base;
        v = toVsg(base);
        o.preMultScale(t);
        VM::preMultScale(v, toVsg(t));
        EXPECT_TRUE(matExact(o, v));

        o = base;
        v = toVsg(base);
        o.postMultScale(t);
        VM::postMultScale(v, toVsg(t));
        EXPECT_TRUE(matExact(o, v));
    }

    // The in-place forms add the terms in OSG's order, which is why they are exact where the products in
    // testmatrixconventions.cpp need a tolerance. Matrixf:795-879, Matrixd:691-773. The second input has a zero component.
    TEST(VsgMathMatrix, inPlaceTranslateAndScale)
    {
        expectInPlaceTranslateAndScaleMatch<float, float>(oVec3<float>(1.1, -2.2, 3.3));
        expectInPlaceTranslateAndScaleMatch<float, double>(oVec3<double>(1.1, -2.2, 3.3));
        expectInPlaceTranslateAndScaleMatch<double, float>(oVec3<float>(1.1, -2.2, 3.3));
        expectInPlaceTranslateAndScaleMatch<double, double>(oVec3<double>(1.1, -2.2, 3.3));
        expectInPlaceTranslateAndScaleMatch<float, float>(oVec3<float>(1.1, 0.0, 3.3));
        expectInPlaceTranslateAndScaleMatch<double, double>(oVec3<double>(1.1, 0.0, 3.3));
    }

    // TRAP: OSG's translate skips a zero component (if (tmp == 0) continue, Matrixd:696). So an infinity in the matching row
    // stays out of the result, where 0 * infinity in a plain product would make NaN.
    TEST(VsgMathMatrix, inPlaceTranslateSkipsAZeroComponent)
    {
        const double infinity = std::numeric_limits<double>::infinity();
        const osg::Matrixd base(1.0, 0.0, 0.0, 0.0, infinity, infinity, infinity, infinity, 0.0, 0.0, 1.0, 0.0, 5.0, 6.0,
            7.0, 1.0);
        const osg::Vec3d t(1.0, 0.0, 1.0);

        osg::Matrixd o = base;
        vsg::dmat4 v = toVsg(base);
        o.preMultTranslate(t);
        VM::preMultTranslate(v, toVsg(t));
        EXPECT_TRUE(matSame(o, v));
        EXPECT_TRUE(vecExact<3>(VM::getTrans(v), osg::Vec3d(6.0, 6.0, 8.0)));

        const osg::Matrixd lastColumn(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, infinity, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
        o = lastColumn;
        v = toVsg(lastColumn);
        o.postMultTranslate(osg::Vec3d(0.0, 0.0, 0.0));
        VM::postMultTranslate(v, vsg::dvec3(0.0, 0.0, 0.0));
        EXPECT_TRUE(matSame(o, v));
    }

    // To tolerance: OSG finishes these with a whole matrix product (Matrixf:880-896, Matrixd:775-791). See the top of the file.
    TYPED_TEST(VsgMathMatrix, inPlaceRotate)
    {
        using T = TypeParam;
        const osg::Quat q(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0));
        const OMatrix<T> inputs[] = { generalMatrix<T>(), affineMatrix<T>() };
        for (const OMatrix<T>& base : inputs)
        {
            OMatrix<T> o = base;
            vsg::t_mat4<T> v = toVsg(base);
            o.preMultRotate(q);
            VM::preMultRotate(v, toVsg(q));
            EXPECT_TRUE(matNear(o, v));

            o = base;
            v = toVsg(base);
            o.postMultRotate(q);
            VM::postMultRotate(v, toVsg(q));
            EXPECT_TRUE(matNear(o, v));
        }
    }

    // TRAP: OSG returns early on the identity quat (zeroRotation, Quat:181; Matrixd:777, 786), so the matrix stays exactly as
    // it was, including an infinity that a product would spread.
    TEST(VsgMathMatrix, inPlaceRotateByIdentityLeavesTheMatrixAlone)
    {
        const double infinity = std::numeric_limits<double>::infinity();
        const osg::Matrixd base(infinity, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0);

        osg::Matrixd o = base;
        vsg::dmat4 v = toVsg(base);
        o.preMultRotate(osg::Quat());
        VM::preMultRotate(v, vsg::dquat());
        EXPECT_TRUE(matSame(o, v));
        EXPECT_TRUE(matSame(base, v));

        v = toVsg(base);
        VM::postMultRotate(v, vsg::dquat());
        EXPECT_TRUE(matSame(base, v));
    }

    // A zero quat makes a zero rotation block (Matrix_implementation.cpp:65-75), so the product collapses in OSG's way.
    TEST(VsgMathMatrix, inPlaceRotateByZeroQuat)
    {
        const osg::Matrixd base = affineMatrix<double>();
        const osg::Quat zero(0.0, 0.0, 0.0, 0.0);

        osg::Matrixd o = base;
        vsg::dmat4 v = toVsg(base);
        o.preMultRotate(zero);
        VM::preMultRotate(v, toVsg(zero));
        EXPECT_TRUE(matNear(o, v));
    }

    template <class T>
    void expectInvert4x3Exact(const OMatrix<T>& input)
    {
        OMatrix<T> o;
        o.invert_4x3(input);
        static_assert(std::is_same_v<decltype(VM::invert4x3(toVsg(input))), vsg::t_mat4<T>>);
        EXPECT_TRUE(matSame(o, VM::invert4x3(toVsg(input))));
    }

    TYPED_TEST(VsgMathMatrix, invert4x3OfAffine)
    {
        using T = TypeParam;
        expectInvert4x3Exact<T>(affineMatrix<T>());
        expectInvert4x3Exact<T>(OMatrix<T>::translate(T(1), T(2), T(3)));
        expectInvert4x3Exact<T>(OMatrix<T>::scale(T(-2), T(3), T(4)));
    }

    // TRAP: m(3, 3) is 1 but the rest of the last column is not 0. OSG ignores that column, as vsg::inverse_4x3 does and
    // vsg::inverse does not (Matrix_implementation.cpp:656; maths_transform.cpp:172-179).
    TYPED_TEST(VsgMathMatrix, invert4x3IgnoresTheLastColumnWhenWIsOne)
    {
        using T = TypeParam;
        expectInvert4x3Exact<T>(oMatrix<T>(1., 0., 0., 0.5, 0., 1., 0., 0., 0., 0., 1., 0., 3., 4., 5., 1.));
    }

    // TRAP: the branch test is square(d - 1) > 1e-6, so d = 1.0005 still takes the short path, which writes m(3, 3) = 1 and
    // loses d. Matrix_implementation.cpp:656, 648.
    TYPED_TEST(VsgMathMatrix, invert4x3JustInsideTheBranchThreshold)
    {
        using T = TypeParam;
        const OMatrix<T> input = oMatrix<T>(1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 3., 4., 5., 1.0005);
        expectInvert4x3Exact<T>(input);
        EXPECT_EQ(VM::invert4x3(toVsg(input))(3, 3), T(1));
    }

    // TRAP: m(3, 3) differs from 1: OSG computes the full inverse (Matrix_implementation.cpp:656); vsg::inverse_4x3 assumes 1
    // (maths_transform.cpp:105-108). To tolerance because this path ends in a matrix product (Matrix_implementation.cpp:711).
    TYPED_TEST(VsgMathMatrix, invert4x3WithHomogeneousScale)
    {
        using T = TypeParam;
        const OMatrix<T> inputs[] = {
            oMatrix<T>(1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 2.),
            oMatrix<T>(1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 3., 4., 5., 1.002),
            generalMatrix<T>(),
        };
        for (const OMatrix<T>& input : inputs)
        {
            OMatrix<T> o;
            o.invert_4x3(input);
            EXPECT_TRUE(matNear(o, VM::invert4x3(toVsg(input))));
        }
        EXPECT_EQ(VM::invert4x3(toVsg(inputs[0]))(3, 3), T(0.5));
        EXPECT_FALSE(matNear(VM::invert4x3(toVsg(inputs[0])), vsg::inverse_4x3(toVsg(inputs[0]))));
    }

    // TRAP: a singular matrix. OSG fills the result with infinity and NaN through one_over_det
    // (Matrix_implementation.cpp:630-638), and VSG's numbers differ (maths_transform.cpp:81).
    TYPED_TEST(VsgMathMatrix, invert4x3OfSingular)
    {
        using T = TypeParam;
        expectInvert4x3Exact<T>(OMatrix<T>::scale(T(0), T(1), T(1)));
        const vsg::t_mat4<T> result = VM::invert4x3(toVsg(OMatrix<T>::scale(T(0), T(1), T(1))));
        EXPECT_TRUE(std::isinf(result(0, 0)));
        EXPECT_TRUE(std::isnan(result(0, 1)));
    }

    template <class T, class U>
    void expectDecomposeMatches(const OMatrix<T>& input)
    {
        OVec3<U> oTranslation;
        OVec3<U> oScale;
        osg::Quat oRotation;
        osg::Quat oScaleOrientation;
        input.decompose(oTranslation, oRotation, oScale, oScaleOrientation);

        vsg::t_vec3<U> vTranslation;
        vsg::t_vec3<U> vScale;
        vsg::dquat vRotation;
        vsg::dquat vScaleOrientation;
        VM::decompose(toVsg(input), vTranslation, vRotation, vScale, vScaleOrientation);

        EXPECT_TRUE(vecSame<3>(oTranslation, vTranslation));
        EXPECT_TRUE(vecSame<4>(oRotation, vRotation));
        EXPECT_TRUE(vecSame<3>(oScale, vScale));
        EXPECT_TRUE(vecSame<4>(oScaleOrientation, vScaleOrientation));
    }

    // OSG's polar decomposition: t, r, s, so, all exact, for both matrix types and output precisions.
    // MatrixDecomposition.cpp:799-835. m(3, 3) divides the translation there (parts.t's W), and the singular input goes
    // through do_rank2 (MatrixDecomposition.cpp:450-452), where vsg::decompose returns false.
    TEST(VsgMathMatrix, decompose)
    {
        const osg::Quat q(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0));

        const osg::Matrixd trs
            = osg::Matrixd::scale(2.0, 3.0, 4.0) * osg::Matrixd::rotate(q) * osg::Matrixd::translate(10.0, -20.0, 30.0);
        osg::Matrixd homogeneous = trs;
        homogeneous(3, 3) = 2.0;
        const osg::Matrixd sheared = osg::Matrixd(1.0, 0.5, 0.0, 0.0, 0.0, 1.0, 0.25, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 2.0, 3.0, 1.0);

        const osg::Matrixd inputs[] = {
            osg::Matrixd(),
            trs,
            osg::Matrixd::scale(-1.0, 1.0, 1.0),
            osg::Matrixd::scale(-2.0, 3.0, -4.0) * osg::Matrixd::rotate(q),
            osg::Matrixd::scale(0.0, 1.0, 1.0),
            homogeneous,
            sheared,
        };
        for (const osg::Matrixd& input : inputs)
        {
            expectDecomposeMatches<double, double>(input);
            expectDecomposeMatches<double, float>(input);
            const osg::Matrixf narrowed(input);
            expectDecomposeMatches<float, double>(narrowed);
            expectDecomposeMatches<float, float>(narrowed);
        }
    }

    // TRAP: a mirror survives. vsg::decompose returns positive scale and rebuilds the identity (testmatrixconventions.cpp,
    // decomposeOfNegativeScale); the helper's parts rebuild the input. The rebuild is OSG's product with its order flipped.
    TEST(VsgMathMatrix, decomposeKeepsAMirror)
    {
        const vsg::dmat4 input = vsg::scale(-1.0, 1.0, 1.0);

        vsg::dvec3 translation;
        vsg::dvec3 scale;
        vsg::dquat rotation;
        vsg::dquat scaleOrientation;
        VM::decompose(input, translation, rotation, scale, scaleOrientation);

        EXPECT_TRUE(vecNear<3>(scale, vsg::dvec3(-1.0, -1.0, -1.0)));

        const vsg::dmat4 rebuilt = vsg::translate(translation) * vsg::rotate(rotation) * vsg::rotate(scaleOrientation)
            * vsg::scale(scale) * vsg::rotate(VM::inverse(scaleOrientation));
        EXPECT_TRUE(matNear(rebuilt, input));
    }

    // up is exact: transform3x3(m, (0, 1, 0)). eye and center are to tolerance, because OSG gets them through its general
    // inverse (Matrix_implementation.cpp:1069-1085). The default look distance is 1, as in OSG (Matrixd:229-230).
    TEST(VsgMathMatrix, getLookAt)
    {
        const osg::Matrixd o
            = osg::Matrixd::lookAt(osg::Vec3d(1.0, 2.0, 3.0), osg::Vec3d(4.0, 6.0, 3.0), osg::Vec3d(0.0, 0.0, 2.0));
        const vsg::dmat4 v = toVsg(o);

        osg::Vec3d oEye;
        osg::Vec3d oCenter;
        osg::Vec3d oUp;
        o.getLookAt(oEye, oCenter, oUp, 2.5);

        vsg::dvec3 eye;
        vsg::dvec3 center;
        vsg::dvec3 up;
        VM::getLookAt(v, eye, center, up, 2.5);

        EXPECT_TRUE(vecNear<3>(oEye, eye));
        EXPECT_TRUE(vecNear<3>(oCenter, center));
        EXPECT_TRUE(vecExact<3>(oUp, up));

        o.getLookAt(oEye, oCenter, oUp);
        VM::getLookAt(v, eye, center, up);
        EXPECT_TRUE(vecNear<3>(oEye, eye));
        EXPECT_TRUE(vecNear<3>(oCenter, center));
        EXPECT_TRUE(vecExact<3>(oUp, up));
    }

    // The matrix form of makeRotate. OSG goes through a quat and normalizes the axis (Matrix_implementation.cpp:394-409).
    TEST(VsgMathMatrix, makeRotateFromAngleAndAxis)
    {
        const double angles[] = { opaque(0.7), opaque(-1.9), opaque(0.0), opaque(3.5) };
        const osg::Vec3d axes[] = { osg::Vec3d(0.0, 0.0, 1.0), osg::Vec3d(1.0, 2.0, 3.0), osg::Vec3d(0.0, 0.0, 2.0),
            osg::Vec3d(0.0, 0.0, 0.0), osg::Vec3d(1e-8, 0.0, 0.0), osg::Vec3d(1e-6, 0.0, 0.0) };
        for (const double angle : angles)
            for (const osg::Vec3d& axis : axes)
            {
                static_assert(std::is_same_v<decltype(VM::makeRotate(angle, toVsg(axis))), vsg::dmat4>);
                EXPECT_TRUE(matExact(osg::Matrixd::rotate(angle, axis), VM::makeRotate(angle, toVsg(axis))));
            }
    }

    // A float axis is widened first (Quat.cpp:71-74), and the result is still a double matrix.
    TEST(VsgMathMatrix, makeRotateFromAngleAndFloatAxis)
    {
        const double angle = opaque(0.7);
        const osg::Vec3f axis(0.1f, 0.7f, -0.3f);
        static_assert(std::is_same_v<decltype(VM::makeRotate(angle, toVsg(axis))), vsg::dmat4>);
        EXPECT_TRUE(matExact(osg::Matrixd::rotate(angle, axis), VM::makeRotate(angle, toVsg(axis))));
    }

    // TRAP: a zero axis gives OSG the identity (Quat.cpp:54-58) and vsg::rotate(angle, axis) cos(angle) on the diagonal.
    TEST(VsgMathMatrix, makeRotateAboutZeroAxisIsIdentity)
    {
        EXPECT_TRUE(matExact(VM::makeRotate(opaque(0.7), vsg::dvec3(0.0, 0.0, 0.0)), vsg::dmat4()));
    }

    // ---------------------------------------------------------------------------------------------------------- quaternions

    // OSG's algorithm, computed in double (Quat.cpp:118-206). Over many inputs, because OSG divides each component where VSG
    // multiplies by a reciprocal (Vec3d:128, 134), and that shows only on some.
    TEST(VsgMathQuat, makeRotateFromToOverAGrid)
    {
        const double values[] = { -3.0, -1.0, -0.3, 0.0, 0.1, 1.0, 1.0 / 7.0, 2.5 };
        for (const double ax : values)
            for (const double ay : values)
                for (const double az : values)
                    for (const double b : values)
                    {
                        const osg::Vec3d from(ax, ay, az);
                        const osg::Vec3d to(b, ax + 0.5, ay - az);
                        osg::Quat o;
                        o.makeRotate(from, to);
                        static_assert(std::is_same_v<decltype(VM::makeRotate(toVsg(from), toVsg(to))), vsg::dquat>);
                        EXPECT_TRUE(vecSame<4>(o, VM::makeRotate(toVsg(from), toVsg(to))));
                    }
    }

    void expectFromToMatches(const osg::Vec3d& from, const osg::Vec3d& to)
    {
        osg::Quat o;
        o.makeRotate(from, to);
        EXPECT_TRUE(vecSame<4>(o, VM::makeRotate(toVsg(from), toVsg(to))));
    }

    // TRAP: opposite vectors. OSG picks the half-turn axis in three branches on the size of x and y (Quat.cpp:169-192); VSG
    // picks another (testquatconventions.cpp, oppositeVectorsChooseDifferentAxes). The third pair is the last branch, the
    // next pairs are not unit length, and the last is close enough to opposite for the u-turn test (1e-7, Quat.cpp:162).
    TEST(VsgMathQuat, makeRotateFromToOppositeVectors)
    {
        expectFromToMatches(osg::Vec3d(0.0, 0.0, 1.0), osg::Vec3d(0.0, 0.0, -1.0));
        expectFromToMatches(osg::Vec3d(1.0, 0.0, 0.0), osg::Vec3d(-1.0, 0.0, 0.0));
        expectFromToMatches(osg::Vec3d(0.7, 0.7, 0.1), osg::Vec3d(-0.7, -0.7, -0.1));
        expectFromToMatches(osg::Vec3d(0.0, 2.0, 0.0), osg::Vec3d(0.0, -5.0, 0.0));
        expectFromToMatches(osg::Vec3d(3.0, 4.0, 12.0), osg::Vec3d(-3.0, -4.0, -12.0));
        expectFromToMatches(osg::Vec3d(0.0, 0.0, 1.0), osg::Vec3d(1e-5, 0.0, -1.0));

        EXPECT_TRUE(vecNear<4>(
            VM::makeRotate(vsg::dvec3(0.0, 0.0, 1.0), vsg::dvec3(0.0, 0.0, -1.0)), vsg::dquat(0.0, 1.0, 0.0, 0.0)));
    }

    // TRAP: parallel vectors can make vsg::dquat(from, to) NaN (testquatconventions.cpp, parallelVectorsCanMakeVsgNan). OSG's
    // result, and so the helper's, is a near-identity quat.
    TEST(VsgMathQuat, makeRotateFromToParallelVectors)
    {
        const double x = 0.1;
        const double y = 1.0 / 7.0;
        const double z = 1.0;
        const double k = 1.7;
        expectFromToMatches(osg::Vec3d(x, y, z), osg::Vec3d(x * k, y * k, z * k));
        expectFromToMatches(osg::Vec3d(1.0, 2.0, 3.0), osg::Vec3d(1.0, 2.0, 3.0));

        const vsg::dquat result = VM::makeRotate(vsg::dvec3(x, y, z), vsg::dvec3(x * k, y * k, z * k));
        EXPECT_FALSE(std::isnan(result.w));
    }

    // OSG skips normalizing within 1e-7 of unit squared length, and reuses from's length when the two squared lengths are
    // within 1e-7 of each other, even if they are not equal. Quat.cpp:134-154.
    TEST(VsgMathQuat, makeRotateFromToNormalizationShortcuts)
    {
        expectFromToMatches(osg::Vec3d(1.0 + 2e-8, 0.0, 0.0), osg::Vec3d(0.0, 1.0 - 2e-8, 0.0));
        expectFromToMatches(osg::Vec3d(3.0, 0.0, 0.0), osg::Vec3d(0.0, 3.0, 0.0));
        expectFromToMatches(osg::Vec3d(3.0, 0.0, 0.0), osg::Vec3d(0.0, 3.0 + 1e-9, 0.0));
        expectFromToMatches(osg::Vec3d(3.0, 0.0, 0.0), osg::Vec3d(0.0, 3.0 + 1e-3, 0.0));
    }

    // TRAP: a zero vector divides by its zero length (Quat.cpp:137, 154), and the NaN runs through to the result, with no
    // special case. Call sites relying on OSG's answer see the same.
    TEST(VsgMathQuat, makeRotateFromToZeroVectors)
    {
        const osg::Vec3d zero(0.0, 0.0, 0.0);
        const osg::Vec3d unit(1.0, 0.0, 0.0);
        expectFromToMatches(zero, unit);
        expectFromToMatches(unit, zero);
        expectFromToMatches(zero, zero);
    }

    // TRAP: float vectors are widened and the work is in double (Quat.cpp:99-102); vsg::quat(vec3, vec3) works in float.
    TEST(VsgMathQuat, makeRotateFromToFloatVectors)
    {
        const osg::Vec3f from(0.1f, 0.7f, -0.3f);
        const osg::Vec3f to(-0.9f, 0.2f, 0.4f);
        osg::Quat o;
        o.makeRotate(from, to);
        static_assert(std::is_same_v<decltype(VM::makeRotate(toVsg(from), toVsg(to))), vsg::dquat>);
        EXPECT_TRUE(vecExact<4>(o, VM::makeRotate(toVsg(from), toVsg(to))));
    }

    // Named apart from the matrix form above: makeRotate(angle, axis) returns a matrix, this returns a quat.
    // OSG Quat(angle, axis). Quat.cpp:51-68.
    TEST(VsgMathQuat, makeRotateFromAxis)
    {
        const double angles[] = { opaque(0.7), opaque(-1.9), opaque(0.0), opaque(3.5) };
        const osg::Vec3d axes[] = { osg::Vec3d(0.0, 0.0, 1.0), osg::Vec3d(1.0, 2.0, 3.0), osg::Vec3d(-2.0, 0.5, 1.0) };
        for (const double angle : angles)
            for (const osg::Vec3d& axis : axes)
            {
                static_assert(std::is_same_v<decltype(VM::makeRotateFromAxis(angle, toVsg(axis))), vsg::dquat>);
                EXPECT_TRUE(vecExact<4>(osg::Quat(angle, axis), VM::makeRotateFromAxis(angle, toVsg(axis))));
            }
    }

    // TRAP: OSG's threshold is axis length 1e-7 (Quat.cpp:51-58); VSG's is machine epsilon (quat.h:97).
    TEST(VsgMathQuat, makeRotateFromAxisThreshold)
    {
        const double angle = opaque(0.7);
        const osg::Vec3d axes[] = { osg::Vec3d(0.0, 0.0, 0.0), osg::Vec3d(1e-8, 0.0, 0.0), osg::Vec3d(9.9e-8, 0.0, 0.0),
            osg::Vec3d(1.1e-7, 0.0, 0.0), osg::Vec3d(1e-6, 0.0, 0.0) };
        for (const osg::Vec3d& axis : axes)
            EXPECT_TRUE(vecExact<4>(osg::Quat(angle, axis), VM::makeRotateFromAxis(angle, toVsg(axis))));

        EXPECT_TRUE(vecExact<4>(VM::makeRotateFromAxis(angle, vsg::dvec3(1e-8, 0.0, 0.0)), vsg::dquat()));
    }

    // A float axis is widened first. Quat.cpp:71-74.
    TEST(VsgMathQuat, makeRotateFromFloatAxis)
    {
        const double angle = opaque(0.7);
        const osg::Vec3f axis(0.1f, 0.7f, -0.3f);
        EXPECT_TRUE(vecExact<4>(osg::Quat(angle, axis), VM::makeRotateFromAxis(angle, toVsg(axis))));
    }

    // Quat:310. Zero is included.
    TEST(VsgMathQuat, length2)
    {
        const osg::Quat inputs[] = { osg::Quat(), osg::Quat(1.0, 2.0, 3.0, 4.0), osg::Quat(0.1, -0.2, 0.3, -0.4),
            osg::Quat(0.0, 0.0, 0.0, 0.0) };
        for (const osg::Quat& q : inputs)
        {
            static_assert(std::is_same_v<decltype(VM::length2(toVsg(q))), double>);
            EXPECT_EQ(q.length2(), VM::length2(toVsg(q)));
        }
    }

    // TRAP: OSG divides the conjugate by length2 (Quat:322); vsg::inverse by length (quat.h:260-265). A zero quat is NaN in
    // OSG: 0 * (1 / 0).
    TEST(VsgMathQuat, inverse)
    {
        const osg::Quat inputs[] = { osg::Quat(), osg::Quat(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0)),
            osg::Quat(0.0, 0.0, 1.0, 1.0), osg::Quat(1.0, 2.0, 3.0, 4.0), osg::Quat(0.1, -0.2, 0.3, -0.4),
            osg::Quat(0.0, 0.0, 0.0, 0.0) };
        for (const osg::Quat& q : inputs)
            EXPECT_TRUE(vecSame<4>(q.inverse(), VM::inverse(toVsg(q))));

        EXPECT_TRUE(vecExact<4>(VM::inverse(vsg::dquat(0.0, 0.0, 1.0, 1.0)), vsg::dquat(0.0, 0.0, -0.5, 0.5)));
        EXPECT_TRUE(std::isnan(VM::inverse(vsg::dquat(0.0, 0.0, 0.0, 0.0)).w));
    }

    // TRAP: OSG -q negates all four, the same rotation (Quat:298); VSG's -q is the conjugate (quat.h:190-193).
    TEST(VsgMathQuat, negate)
    {
        const osg::Quat q(0.1, -0.2, 0.3, -0.4);
        EXPECT_TRUE(vecExact<4>(-q, VM::negate(toVsg(q))));
        EXPECT_TRUE(vecExact<4>(VM::negate(vsg::dquat(1.0, 2.0, 3.0, 4.0)), vsg::dquat(-1.0, -2.0, -3.0, -4.0)));
    }

    // OSG's atan2 form with axis (0, 0, 1) for no rotation. Non-unit and zero quats have no special case: atan2(0, 0) is 0.
    // Quat.cpp:289-306. The float axis narrows. Quat:368, Quat.cpp:276-283.
    TEST(VsgMathQuat, getRotate)
    {
        const osg::Quat inputs[] = { osg::Quat(), osg::Quat(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0)),
            osg::Quat(opaque(-1.25), osg::Vec3d(0.0, 1.0, 0.0)), osg::Quat(opaque(0.3), opaque(0.2), opaque(0.1), opaque(5.0)),
            osg::Quat(0.0, 0.0, 0.0, 2.0), osg::Quat(0.0, 0.0, 0.0, -1.0), osg::Quat(0.0, 0.0, 0.0, 0.0),
            osg::Quat(0.0, 0.0, 3.0, 0.0) };
        for (const osg::Quat& input : inputs)
        {
            // Through opaque so that atan2 cannot be folded in the helper, as at the top of the file.
            const osg::Quat q(opaque(input.x()), opaque(input.y()), opaque(input.z()), opaque(input.w()));
            double oAngle = -1.0;
            osg::Vec3d oAxis;
            q.getRotate(oAngle, oAxis);
            double vAngle = -2.0;
            vsg::dvec3 vAxis;
            VM::getRotate(toVsg(q), vAngle, vAxis);
            EXPECT_EQ(oAngle, vAngle);
            EXPECT_TRUE(vecExact<3>(oAxis, vAxis));

            osg::Vec3f oFloatAxis;
            q.getRotate(oAngle, oFloatAxis);
            vsg::vec3 vFloatAxis;
            VM::getRotate(toVsg(q), vAngle, vFloatAxis);
            EXPECT_EQ(oAngle, vAngle);
            EXPECT_TRUE(vecExact<3>(oFloatAxis, vFloatAxis));
        }

        double angle = -1.0;
        vsg::dvec3 axis(9.0, 9.0, 9.0);
        VM::getRotate(vsg::dquat(0.0, 0.0, 0.0, 0.0), angle, axis);
        EXPECT_EQ(angle, 0.0);
        EXPECT_TRUE(vecExact<3>(axis, vsg::dvec3(0.0, 0.0, 1.0)));
    }

    // OSG Matrixd::rotate(q): handles non-unit quats with 2 / length2 (Matrix_implementation.cpp:83-86), where vsg::rotate
    // assumes unit length. Exactly 1 and just off it take the two sides of the length2 != 1.0 test.
    TEST(VsgMathQuat, toMatrix)
    {
        const osg::Quat inputs[] = { osg::Quat(), osg::Quat(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0)),
            osg::Quat(opaque(-1.9), osg::Vec3d(-2.0, 0.5, 1.0)), osg::Quat(0.0, 0.0, 1.0, 1.0), osg::Quat(1.0, 2.0, 3.0, 4.0),
            osg::Quat(0.0, 0.0, 0.0, 1.0 + 1e-15), osg::Quat(0.8, 0.0, 0.0, -0.6), osg::Quat(0.0, 0.0, 0.0, -1.0) };
        for (const osg::Quat& q : inputs)
        {
            static_assert(std::is_same_v<decltype(VM::toMatrix(toVsg(q))), vsg::dmat4>);
            EXPECT_TRUE(matExact(osg::Matrixd::rotate(q), VM::toMatrix(toVsg(q))));
        }
    }

    // TRAP: a zero quat gives OSG a zero rotation block with the last row and column of the identity
    // (Matrix_implementation.cpp:65-75); vsg::rotate gives the identity. So does a quat whose length2 underflows to zero.
    TEST(VsgMathQuat, toMatrixOfZeroQuat)
    {
        const osg::Quat zero(0.0, 0.0, 0.0, 0.0);
        const vsg::dmat4 result = VM::toMatrix(toVsg(zero));
        EXPECT_TRUE(matExact(osg::Matrixd::rotate(zero), result));
        EXPECT_EQ(result(0, 0), 0.0);
        EXPECT_EQ(result(3, 3), 1.0);

        const osg::Quat tiny(1e-200, 0.0, 0.0, 0.0);
        EXPECT_TRUE(matExact(osg::Matrixd::rotate(tiny), VM::toMatrix(toVsg(tiny))));
        EXPECT_EQ(VM::toMatrix(toVsg(tiny))(0, 0), 0.0);
    }

    // OSG Matrixf::rotate(q) computes in double and narrows (Matrix_implementation.cpp:79-130), so it is the narrowed helper.
    TEST(VsgMathQuat, toMatrixNarrowsToTheFloatForm)
    {
        const osg::Quat q(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0));
        EXPECT_TRUE(matExact(osg::Matrixf::rotate(q), vsg::mat4(VM::toMatrix(toVsg(q)))));
    }

    template <class T>
    class VsgMathRotationOf : public ::testing::Test
    {
    };
    TYPED_TEST_SUITE(VsgMathRotationOf, Precisions);

    // OSG's Matrix::getRotate, one branch for each of four traces (Matrix_implementation.cpp:151-208). The inputs reach each:
    // identity w, then x, y and z, and then (0.8, 0, 0, -0.6), where VSG's decompose returns the opposite sign. In a float
    // matrix the traces are computed in float, and the helper must do the same.
    TYPED_TEST(VsgMathRotationOf, branchesAndSign)
    {
        using T = TypeParam;
        const osg::Quat inputs[] = { osg::Quat(), osg::Quat(1.0, 0.0, 0.0, 0.0), osg::Quat(0.0, 1.0, 0.0, 0.0),
            osg::Quat(0.0, 0.0, 1.0, 0.0), osg::Quat(0.8, 0.0, 0.0, -0.6),
            osg::Quat(opaque(0.7), osg::Vec3d(1.0, 2.0, 3.0)), osg::Quat(opaque(-1.9), osg::Vec3d(-2.0, 0.5, 1.0)),
            osg::Quat(0.1, 0.2, 0.3, 0.9) };
        for (const osg::Quat& q : inputs)
        {
            const OMatrix<T> o = OMatrix<T>::rotate(q);
            static_assert(std::is_same_v<decltype(VM::rotationOf(toVsg(o))), vsg::dquat>);
            EXPECT_TRUE(vecExact<4>(o.getRotate(), VM::rotationOf(toVsg(o))));
        }
    }

    // TRAP: scale is not removed (documented at Matrixf:337-340), unlike vsg::decompose. Scale 2 gives (0, 0, 0, sqrt(7) / 2).
    // Nothing checks the last row or column either.
    TYPED_TEST(VsgMathRotationOf, keepsTheScaleError)
    {
        using T = TypeParam;
        const OMatrix<T> inputs[] = { OMatrix<T>::scale(T(2), T(2), T(2)), OMatrix<T>::scale(T(-1), T(1), T(1)),
            generalMatrix<T>(), affineMatrix<T>() };
        for (const OMatrix<T>& o : inputs)
            EXPECT_TRUE(vecExact<4>(o.getRotate(), VM::rotationOf(toVsg(o))));

        EXPECT_TRUE(vecNear<4>(VM::rotationOf(toVsg(inputs[0])), vsg::dquat(0.0, 0.0, 0.0, std::sqrt(7.0) / 2.0)));
    }

    // ---------------------------------------------------------------------------------------------------------------- boxes

    template <class T>
    class VsgMathBox : public ::testing::Test
    {
    };
    TYPED_TEST_SUITE(VsgMathBox, Precisions);

    template <class O, class V>
    ::testing::AssertionResult boxSame(const O& o, const V& v)
    {
        const ::testing::AssertionResult minimum = vecSame<3>(o._min, v.min);
        if (!minimum)
            return minimum;
        return vecSame<3>(o._max, v.max);
    }

    // TRAP: vsg::t_box::valid checks X only (box.h:50); OSG all three axes (BoundingBox:86-89). All eight inversions.
    TYPED_TEST(VsgMathBox, valid)
    {
        using T = TypeParam;
        for (int bits = 0; bits < 8; ++bits)
        {
            const OBox<T> o = oBox<T>((bits & 1) ? 5.0 : 0.0, (bits & 2) ? 5.0 : 0.0, (bits & 4) ? 5.0 : 0.0, 1.0, 1.0, 1.0);
            EXPECT_EQ(o.valid(), VM::valid(toVsg(o))) << bits;
        }
        EXPECT_FALSE(VM::valid(vsg::t_box<T>()));
        EXPECT_FALSE(VM::valid(toVsg(OBox<T>())));

        const OBox<T> point = oBox<T>(1.0, 1.0, 1.0, 1.0, 1.0, 1.0);
        EXPECT_TRUE(VM::valid(toVsg(point)));
    }

    // NaN compares false on any axis, so the box is invalid.
    TYPED_TEST(VsgMathBox, validWithNaN)
    {
        using T = TypeParam;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (int axis = 0; axis < 3; ++axis)
        {
            OBox<T> o = oBox<T>(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
            o._max[axis] = static_cast<T>(nan);
            EXPECT_EQ(o.valid(), VM::valid(toVsg(o)));
            EXPECT_FALSE(VM::valid(toVsg(o)));
        }
    }

    // center is (min + max) * 0.5; radius2 is 0.25 * length2(max - min). BoundingBox:126-142. They run on invalid boxes too,
    // with no check, including the sentinels of a default box.
    TYPED_TEST(VsgMathBox, centerAndRadius)
    {
        using T = TypeParam;
        const OBox<T> inputs[] = { oBox<T>(-1.0, 2.0, 3.0, 4.0, 5.5, 7.0), oBox<T>(0.1, 0.2, 0.3, 0.7, 1.1, 1.3),
            oBox<T>(1.0, 1.0, 1.0, 1.0, 1.0, 1.0), oBox<T>(5.0, 0.0, 0.0, 1.0, 1.0, 1.0), OBox<T>() };
        for (const OBox<T>& o : inputs)
        {
            const vsg::t_box<T> v = toVsg(o);
            static_assert(std::is_same_v<decltype(VM::center(v)), vsg::t_vec3<T>>);
            static_assert(std::is_same_v<decltype(VM::radius(v)), T>);
            static_assert(std::is_same_v<decltype(VM::radius2(v)), T>);
            EXPECT_TRUE(vecExact<3>(o.center(), VM::center(v)));
            EXPECT_EQ(o.radius(), VM::radius(v));
            EXPECT_EQ(o.radius2(), VM::radius2(v));
        }
    }

    // Bit 0 selects X max, bit 1 Y max, bit 2 Z max. BoundingBox:149-152.
    TYPED_TEST(VsgMathBox, corner)
    {
        using T = TypeParam;
        const OBox<T> o = oBox<T>(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
        for (unsigned i = 0; i < 8; ++i)
            EXPECT_TRUE(vecExact<3>(o.corner(i), VM::corner(toVsg(o), i))) << i;
    }

    // Inclusive at the faces, false for an invalid box, epsilon widens both sides. BoundingBox:233-248.
    TYPED_TEST(VsgMathBox, contains)
    {
        using T = TypeParam;
        const OBox<T> boxes[] = { oBox<T>(0.0, 0.0, 0.0, 1.0, 1.0, 1.0), oBox<T>(0.0, 5.0, 0.0, 1.0, 1.0, 1.0), OBox<T>() };
        const OVec3<T> points[] = { oVec3<T>(0.5, 0.5, 0.5), oVec3<T>(1.0, 1.0, 1.0), oVec3<T>(0.0, 0.0, 0.0),
            oVec3<T>(1.5, 0.0, 0.0), oVec3<T>(0.0, -0.5, 0.0), oVec3<T>(0.0, 0.0, 1.75) };
        for (const OBox<T>& o : boxes)
            for (const OVec3<T>& p : points)
            {
                EXPECT_EQ(o.contains(p), VM::contains(toVsg(o), toVsg(p)));
                EXPECT_EQ(o.contains(p, T(0.5)), VM::contains(toVsg(o), toVsg(p), T(0.5)));
            }
    }

    // Per axis, with no validity check: two boxes that do not meet give an inverted box. BoundingBox:218-221.
    TYPED_TEST(VsgMathBox, intersect)
    {
        using T = TypeParam;
        const OBox<T> a = oBox<T>(0.0, 0.0, 0.0, 2.0, 2.0, 2.0);
        const OBox<T> b = oBox<T>(1.0, -1.0, 0.5, 3.0, 1.0, 1.5);
        const OBox<T> disjoint = oBox<T>(5.0, 5.0, 5.0, 6.0, 6.0, 6.0);
        static_assert(std::is_same_v<decltype(VM::intersect(toVsg(a), toVsg(b))), vsg::t_box<T>>);
        EXPECT_TRUE(boxSame(a.intersect(b), VM::intersect(toVsg(a), toVsg(b))));
        EXPECT_TRUE(boxSame(a.intersect(disjoint), VM::intersect(toVsg(a), toVsg(disjoint))));
        EXPECT_FALSE(VM::valid(VM::intersect(toVsg(a), toVsg(disjoint))));
    }

    template <class T>
    void expectBoxExpandByBoxMatches(OBox<T> o, const OBox<T>& other)
    {
        vsg::t_box<T> v = toVsg(o);
        o.expandBy(other);
        VM::expandBy(v, toVsg(other));
        EXPECT_TRUE(boxSame(o, v));
    }

    // TRAP: OSG skips an invalid box as a whole (BoundingBox:187), checking all three axes. vsg::t_box::add merges any axis
    // on its own, and has no validity check (box.h:80-88).
    TYPED_TEST(VsgMathBox, expandByBox)
    {
        using T = TypeParam;
        const OBox<T> base = oBox<T>(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);

        expectBoxExpandByBoxMatches<T>(base, oBox<T>(-1.0, 0.5, 0.25, 0.5, 2.0, 0.75));
        expectBoxExpandByBoxMatches<T>(base, OBox<T>());
        expectBoxExpandByBoxMatches<T>(base, oBox<T>(-1.0, 5.0, 0.0, 2.0, 1.0, 1.0));
        expectBoxExpandByBoxMatches<T>(base, oBox<T>(5.0, -1.0, -1.0, 2.0, 2.0, 2.0));
        expectBoxExpandByBoxMatches<T>(base, oBox<T>(-1.0, -1.0, 5.0, 2.0, 2.0, 2.0));
        expectBoxExpandByBoxMatches<T>(OBox<T>(), oBox<T>(-1.0, 0.5, 0.25, 0.5, 2.0, 0.75));
        expectBoxExpandByBoxMatches<T>(OBox<T>(), OBox<T>());

        vsg::t_box<T> v = toVsg(base);
        VM::expandBy(v, vsg::t_box<T>(vsg::t_vec3<T>(T(-1), T(5), T(0)), vsg::t_vec3<T>(T(2), T(1), T(1))));
        EXPECT_EQ(v.min.x, T(0));
    }

    // A real VSG empty box, whose double sentinels differ from OSG's (testboundsconventions.cpp,
    // defaultDoubleBoxSentinelsDiffer), still takes on the merged box whole.
    TYPED_TEST(VsgMathBox, expandByBoxFromVsgDefault)
    {
        using T = TypeParam;
        const OBox<T> other = oBox<T>(-1.0, 0.5, 0.25, 0.5, 2.0, 0.75);
        vsg::t_box<T> v;
        VM::expandBy(v, toVsg(other));
        EXPECT_TRUE(boxSame(other, v));
    }

    template <class T>
    void expectBoxExpandBySphereMatches(OBox<T> o, const OSphere<T>& sphere)
    {
        vsg::t_box<T> v = toVsg(o);
        o.expandBy(sphere);
        VM::expandBy(v, toVsg(sphere));
        EXPECT_TRUE(boxSame(o, v));
    }

    // An invalid sphere (negative radius) is skipped; radius 0 is valid. BoundingBox:202-214.
    TYPED_TEST(VsgMathBox, expandBySphere)
    {
        using T = TypeParam;
        const OBox<T> base = oBox<T>(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        expectBoxExpandBySphereMatches<T>(base, oSphere<T>(5.0, 0.3, 0.1, 2.0));
        expectBoxExpandBySphereMatches<T>(base, oSphere<T>(0.5, 0.5, 0.5, 0.1));
        expectBoxExpandBySphereMatches<T>(base, oSphere<T>(9.0, 9.0, 9.0, 0.0));
        expectBoxExpandBySphereMatches<T>(base, OSphere<T>());
        expectBoxExpandBySphereMatches<T>(base, oSphere<T>(5.0, 5.0, 5.0, -1.0));
        expectBoxExpandBySphereMatches<T>(OBox<T>(), oSphere<T>(5.0, 0.3, 0.1, 2.0));
    }

    // -------------------------------------------------------------------------------------------------------------- spheres

    template <class T>
    class VsgMathSphere : public ::testing::Test
    {
    };
    TYPED_TEST_SUITE(VsgMathSphere, Precisions);

    template <class O, class V>
    ::testing::AssertionResult sphereSame(const O& o, const V& v)
    {
        const ::testing::AssertionResult centre = vecSame<3>(o._center, v.center);
        if (!centre)
            return centre;
        if (!sameValue(o._radius, v.radius))
            return ::testing::AssertionFailure() << "radius: " << o._radius << " != " << v.radius;
        return ::testing::AssertionSuccess();
    }

    template <class O, class P>
    void expectExpandByPointMatches(O o, const P& point)
    {
        auto v = toVsg(o);
        o.expandBy(point);
        VM::expandBy(v, toVsg(point));
        EXPECT_TRUE(sphereSame(o, v));
    }

    template <class O, class P>
    void expectExpandRadiusByPointMatches(O o, const P& point)
    {
        auto v = toVsg(o);
        o.expandRadiusBy(point);
        VM::expandRadiusBy(v, toVsg(point));
        EXPECT_TRUE(sphereSame(o, v));
    }

    // Unset: centre is the point, radius 0. Outside: the centre moves to minimise the increase. Inside: nothing. BoundingSphere,
    // template expandBy(const vector_type&).
    TYPED_TEST(VsgMathSphere, expandByPoint)
    {
        using T = TypeParam;
        const OSphere<T> spheres[] = { OSphere<T>(), oSphere<T>(0.0, 0.0, 0.0, 1.0), oSphere<T>(0.0, 0.0, 0.0, 5.0),
            oSphere<T>(0.1, -0.2, 0.3, 0.7), oSphere<T>(3.0, 0.0, 0.0, 0.0) };
        const OVec3<T> points[] = { oVec3<T>(3.0, 0.0, 0.0), oVec3<T>(0.3, 0.7, -1.1), oVec3<T>(-11.0, 0.1, 7.0) };
        for (const OSphere<T>& s : spheres)
            for (const OVec3<T>& p : points)
                expectExpandByPointMatches(s, p);
    }

    // Unset: as above. Otherwise the centre stays and only the radius grows. BoundingSphere, template expandRadiusBy.
    TYPED_TEST(VsgMathSphere, expandRadiusByPoint)
    {
        using T = TypeParam;
        const OSphere<T> spheres[] = { OSphere<T>(), oSphere<T>(0.0, 0.0, 0.0, 1.0), oSphere<T>(0.0, 0.0, 0.0, 5.0),
            oSphere<T>(0.1, -0.2, 0.3, 0.7) };
        const OVec3<T> points[] = { oVec3<T>(3.0, 0.0, 0.0), oVec3<T>(0.3, 0.7, -1.1), oVec3<T>(-11.0, 0.1, 7.0) };
        for (const OSphere<T>& s : spheres)
            for (const OVec3<T>& p : points)
                expectExpandRadiusByPointMatches(s, p);
    }

    // OSG's point functions are templates on the vector, widening or narrowing it to the sphere's type first.
    TEST(VsgMathSphere, expandByPointOfTheOtherPrecision)
    {
        const osg::Vec3f floatPoint(0.3f, 0.7f, -1.1f);
        const osg::Vec3d doublePoint(0.3, 0.7, -1.1);
        expectExpandByPointMatches(oSphere<double>(0.1, -0.2, 0.3, 0.7), floatPoint);
        expectExpandByPointMatches(oSphere<float>(0.1, -0.2, 0.3, 0.7), doublePoint);
        expectExpandByPointMatches(osg::BoundingSphered(), floatPoint);
        expectExpandByPointMatches(osg::BoundingSpheref(), doublePoint);
        expectExpandRadiusByPointMatches(oSphere<double>(0.1, -0.2, 0.3, 0.7), floatPoint);
        expectExpandRadiusByPointMatches(oSphere<float>(0.1, -0.2, 0.3, 0.7), doublePoint);
    }

    template <class T>
    void expectExpandBySphereMatches(OSphere<T> o, const OSphere<T>& other)
    {
        vsg::t_sphere<T> v = toVsg(o);
        o.expandBy(other);
        VM::expandBy(v, toVsg(other));
        EXPECT_TRUE(sphereSame(o, v));
    }

    // Each branch of BoundingSphere's expandBy(const BoundingSphereImpl&): other invalid, this invalid, other inside this,
    // this inside other, and the general case. The distance and ratio are double even for a float sphere.
    TYPED_TEST(VsgMathSphere, expandBySphere)
    {
        using T = TypeParam;
        const OSphere<T> base = oSphere<T>(0.1, 0.2, 0.3, 1.0);
        expectExpandBySphereMatches<T>(base, OSphere<T>());
        expectExpandBySphereMatches<T>(OSphere<T>(), base);
        expectExpandBySphereMatches<T>(OSphere<T>(), OSphere<T>());
        expectExpandBySphereMatches<T>(base, oSphere<T>(0.2, 0.2, 0.3, 0.1));
        expectExpandBySphereMatches<T>(base, oSphere<T>(0.2, 0.2, 0.3, 7.0));
        expectExpandBySphereMatches<T>(base, base);
        expectExpandBySphereMatches<T>(base, oSphere<T>(3.3, -1.7, 0.9, 0.7));
        expectExpandBySphereMatches<T>(oSphere<T>(0.0, 0.0, 0.0, 0.0), oSphere<T>(1.0, 2.0, 3.0, 0.0));
    }

    template <class T>
    void expectExpandByBoxMatches(OSphere<T> o, const OBox<T>& box)
    {
        vsg::t_sphere<T> v = toVsg(o);
        o.expandBy(box);
        VM::expandBy(v, toVsg(box));
        EXPECT_TRUE(sphereSame(o, v));
    }

    // Not the sphere around the union: OSG adds to a box the point of this sphere opposite each corner, then takes that box's
    // centre and radius. A box with any axis inverted is skipped. BoundingSphere's expandBy(const BoundingBoxImpl&).
    TYPED_TEST(VsgMathSphere, expandByBox)
    {
        using T = TypeParam;
        const OBox<T> box = oBox<T>(-1.0, 2.0, 3.0, 4.0, 5.5, 7.0);

        expectExpandByBoxMatches<T>(oSphere<T>(0.1, 0.2, 0.3, 1.0), box);
        expectExpandByBoxMatches<T>(oSphere<T>(1.5, 3.7, 5.0, 0.25), box);
        expectExpandByBoxMatches<T>(OSphere<T>(), box);
        expectExpandByBoxMatches<T>(oSphere<T>(0.1, 0.2, 0.3, 1.0), OBox<T>());
        expectExpandByBoxMatches<T>(oSphere<T>(0.1, 0.2, 0.3, 1.0), oBox<T>(0.0, 5.0, 0.0, 1.0, 1.0, 1.0));
        expectExpandByBoxMatches<T>(OSphere<T>(), OBox<T>());
        // The centre sits on a corner: that corner's direction normalizes as a zero vector, staying zero.
        expectExpandByBoxMatches<T>(oSphere<T>(0.0, 0.0, 0.0, 1.0), oBox<T>(0.0, 0.0, 0.0, 1.0, 1.0, 1.0));
    }

    // ---------------------------------------------------------------------------------------------------------------- planes

    // OSG's three-point constructor. Plane:113-120. VSG has none. Over many inputs, because OSG divides by the length with
    // /= (Vec3d:134), which vsg::t_vec3's /= does not (vec3.h:138-143).
    TEST(VsgMathPlane, makePlaneFromThreePoints)
    {
        const double values[] = { -3.0, -0.3, 0.0, 0.1, 1.0, 1.0 / 7.0, 2.5 };
        for (const double a : values)
            for (const double b : values)
                for (const double c : values)
                {
                    const osg::Vec3d v1(a, b, c);
                    const osg::Vec3d v2(b + 1.0, c - 0.5, a);
                    const osg::Vec3d v3(c, a + 2.0, b * 0.7);
                    static_assert(std::is_same_v<decltype(VM::makePlane(toVsg(v1), toVsg(v2), toVsg(v3))), vsg::dplane>);
                    EXPECT_TRUE(vecExact<4>(osg::Plane(v1, v2, v3), VM::makePlane(toVsg(v1), toVsg(v2), toVsg(v3))));
                }
    }

    // Collinear points give a zero normal and d of 0, not NaN, and so do points whose cross product is at most 1e-6 long, even
    // though they are not collinear. Plane:117-119.
    TEST(VsgMathPlane, makePlaneFromDegeneratePoints)
    {
        const osg::Vec3d zero(0.0, 0.0, 0.0);
        const osg::Vec3d a(1.0, 0.0, 0.0);
        const osg::Vec3d b(2.0, 0.0, 0.0);
        EXPECT_TRUE(vecExact<4>(osg::Plane(zero, a, b), VM::makePlane(toVsg(zero), toVsg(a), toVsg(b))));
        EXPECT_TRUE(vecExact<4>(VM::makePlane(toVsg(zero), toVsg(a), toVsg(b)), vsg::dplane(0.0, 0.0, 0.0, 0.0)));
        EXPECT_TRUE(vecExact<4>(osg::Plane(a, a, a), VM::makePlane(toVsg(a), toVsg(a), toVsg(a))));

        const osg::Vec3d small1(1e-4, 0.0, 0.0);
        const osg::Vec3d small2(0.0, 1e-4, 0.0);
        EXPECT_TRUE(vecExact<4>(osg::Plane(zero, small1, small2), VM::makePlane(toVsg(zero), toVsg(small1), toVsg(small2))));
        EXPECT_TRUE(
            vecExact<4>(VM::makePlane(toVsg(zero), toVsg(small1), toVsg(small2)), vsg::dplane(0.0, 0.0, 0.0, 0.0)));
    }

    // Hides VSG's swapped arguments (plane.h:69): the order here is OSG's, normal first. Plane:96, 122. The normal is not
    // normalized.
    TEST(VsgMathPlane, makePlaneFromNormalAndPoint)
    {
        const osg::Vec3d normals[] = { osg::Vec3d(0.0, 0.0, 1.0), osg::Vec3d(0.0, 0.0, 2.0), osg::Vec3d(0.3, -0.7, 1.1) };
        const osg::Vec3d points[] = { osg::Vec3d(1.0, 2.0, 5.0), osg::Vec3d(-0.1, 7.3, 0.7), osg::Vec3d(0.0, 0.0, 0.0) };
        for (const osg::Vec3d& normal : normals)
            for (const osg::Vec3d& point : points)
            {
                static_assert(std::is_same_v<decltype(VM::makePlane(toVsg(normal), toVsg(point))), vsg::dplane>);
                EXPECT_TRUE(vecExact<4>(osg::Plane(normal, point), VM::makePlane(toVsg(normal), toVsg(point))));
            }
        EXPECT_TRUE(vecExact<4>(VM::makePlane(vsg::dvec3(0.0, 0.0, 1.0), vsg::dvec3(1.0, 2.0, 5.0)), vsg::dplane(0.0, 0.0, 1.0, -5.0)));
    }

    // In place, as in OSG, and all four negated. Plane:129.
    TEST(VsgMathPlane, flip)
    {
        osg::Plane o(0.1, -0.2, 0.3, 0.4);
        vsg::dplane v(0.1, -0.2, 0.3, 0.4);
        o.flip();
        VM::flip(v);
        EXPECT_TRUE(vecExact<4>(o, v));
        EXPECT_TRUE(vecExact<4>(v, vsg::dplane(-0.1, 0.2, -0.3, -0.4)));
    }

    // In place, as in OSG; all four scaled by the reciprocal of the normal's length, so d is scaled too. A zero normal gives
    // NaN from 0 * infinity, and infinity for a non-zero d. Plane:140.
    TEST(VsgMathPlane, makeUnitLength)
    {
        const osg::Plane inputs[] = { osg::Plane(0.0, 0.0, 2.0, -6.0), osg::Plane(0.1, -0.2, 0.3, 0.4),
            osg::Plane(0.0, 0.0, 0.0, 1.0) };
        for (const osg::Plane& input : inputs)
        {
            osg::Plane o(input);
            vsg::dplane v(input[0], input[1], input[2], input[3]);
            o.makeUnitLength();
            VM::makeUnitLength(v);
            EXPECT_TRUE(vecSame<4>(o, v));
        }
    }
}

#endif
