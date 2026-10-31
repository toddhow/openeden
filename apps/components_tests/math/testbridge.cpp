// Under conversion on purpose, to prove that vsgmath.hpp's own helpers compile with the matrix products deleted.
#define OPENEDEN_VSGMATH_CONVERTING

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <components/misc/vsgmath.hpp>

#include <osg/BoundingBox>
#include <osg/BoundingSphere>
#include <osg/Matrixd>
#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Vec2d>
#include <osg/Vec2f>
#include <osg/Vec2i>
#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osg/Vec4d>
#include <osg/Vec4f>

#include <type_traits>

// Tests for two parts of task 0013's foundation (docs/tasks/0013-vulkan-v5-vsg-math.md):
//  - the transition bridge in extern/osg/osg, with which one area can move to VSG types while its callers still use OSG's;
//  - the deleted operators in components/misc/vsgmath.hpp, as compile-time checks: a requires expression is false when
//    overload resolution picks a deleted function or finds an ambiguity, so these fail the build if the rules change.
namespace
{
    template <class A, class B>
    concept Multipliable = requires(const A& a, const B& b) { a * b; };

    // The trap itself: VSG's component-wise product does not compile.
    static_assert(!Multipliable<vsg::vec2, vsg::vec2>);
    static_assert(!Multipliable<vsg::dvec2, vsg::dvec2>);
    static_assert(!Multipliable<vsg::vec3, vsg::vec3>);
    static_assert(!Multipliable<vsg::dvec3, vsg::dvec3>);
    static_assert(!Multipliable<vsg::vec4, vsg::vec4>);
    static_assert(!Multipliable<vsg::dvec4, vsg::dvec4>);

    // The scalar friends still win; the deleted overloads do not fit a scalar, so nothing is ambiguous. Both orders, and a
    // scalar of the other precision, which the friend converts (vec3.h:118).
    static_assert(std::is_same_v<decltype(vsg::vec3() * 2.f), vsg::vec3>);
    static_assert(std::is_same_v<decltype(2.f * vsg::vec3()), vsg::vec3>);
    static_assert(std::is_same_v<decltype(vsg::vec3() * 2.0), vsg::vec3>);
    static_assert(std::is_same_v<decltype(vsg::dvec3() * 2.0), vsg::dvec3>);
    static_assert(std::is_same_v<decltype(2.0 * vsg::dvec3()), vsg::dvec3>);
    static_assert(std::is_same_v<decltype(vsg::dvec3() * 2.f), vsg::dvec3>);
    static_assert(std::is_same_v<decltype(vsg::vec2() * 2.f), vsg::vec2>);
    static_assert(std::is_same_v<decltype(vsg::dvec2() * 2.0), vsg::dvec2>);
    static_assert(std::is_same_v<decltype(vsg::vec4() * 2.f), vsg::vec4>);
    static_assert(std::is_same_v<decltype(vsg::dvec4() * 2.0), vsg::dvec4>);

    // osg::Vec2i's own * is component-wise, so VSG's is the same meaning and stays (task 0013, note 5).
    static_assert(std::is_same_v<decltype(vsg::ivec2() * vsg::ivec2()), vsg::ivec2>);

    // Under OPENEDEN_VSGMATH_CONVERTING every matrix product has to be written by hand.
    static_assert(!Multipliable<vsg::mat4, vsg::mat4>);
    static_assert(!Multipliable<vsg::dmat4, vsg::dmat4>);
    static_assert(!Multipliable<vsg::vec3, vsg::mat4>);
    static_assert(!Multipliable<vsg::mat4, vsg::vec3>);
    static_assert(!Multipliable<vsg::dvec3, vsg::dmat4>);
    static_assert(!Multipliable<vsg::dmat4, vsg::dvec3>);
    static_assert(!Multipliable<vsg::vec4, vsg::mat4>);
    static_assert(!Multipliable<vsg::mat4, vsg::vec4>);
    static_assert(!Multipliable<vsg::dvec4, vsg::dmat4>);
    static_assert(!Multipliable<vsg::dmat4, vsg::dvec4>);

    // The bridge leaves OSG's own operators alone: * between OSG vectors is still OSG's dot product.
    static_assert(std::is_same_v<decltype(osg::Vec3f() * osg::Vec3f()), float>);
    static_assert(std::is_same_v<decltype(osg::Vec3d() * osg::Vec3d()), double>);
    static_assert(std::is_same_v<decltype(osg::Vec3d() * osg::Matrixd()), osg::Vec3d>);
    static_assert(std::is_same_v<decltype(osg::Matrixd() * osg::Matrixd()), osg::Matrixd>);

    // * across the bridge does not compile, as it could mean either library's product. Convert one side explicitly.
    static_assert(!Multipliable<osg::Vec3f, vsg::vec3>);
    static_assert(!Multipliable<vsg::vec3, osg::Vec3f>);
    static_assert(!Multipliable<osg::Vec3d, vsg::dvec3>);
    static_assert(!Multipliable<vsg::dvec3, osg::Vec3d>);

    // Both directions convert implicitly, each pair at the matching precision only.
    template <class O, class V>
    constexpr bool bridged = std::is_convertible_v<O, V> && std::is_convertible_v<V, O>;

    static_assert(bridged<osg::Vec2f, vsg::vec2>);
    static_assert(bridged<osg::Vec2d, vsg::dvec2>);
    static_assert(bridged<osg::Vec2i, vsg::ivec2>);
    static_assert(bridged<osg::Vec3f, vsg::vec3>);
    static_assert(bridged<osg::Vec3d, vsg::dvec3>);
    static_assert(bridged<osg::Vec4f, vsg::vec4>);
    static_assert(bridged<osg::Vec4d, vsg::dvec4>);
    static_assert(bridged<osg::Quat, vsg::dquat>);
    static_assert(bridged<osg::Matrixf, vsg::mat4>);
    static_assert(bridged<osg::Matrixd, vsg::dmat4>);
    static_assert(bridged<osg::BoundingBoxf, vsg::box>);
    static_assert(bridged<osg::BoundingBoxd, vsg::dbox>);
    static_assert(bridged<osg::BoundingSpheref, vsg::sphere>);
    static_assert(bridged<osg::BoundingSphered, vsg::dsphere>);

    static_assert(!std::is_convertible_v<osg::Vec3f, vsg::dvec3>);
    static_assert(!std::is_convertible_v<osg::Vec3d, vsg::vec3>);
    static_assert(!std::is_convertible_v<vsg::vec3, osg::Vec3d>);
    static_assert(!std::is_convertible_v<vsg::dvec3, osg::Vec3f>);

    // Each conversion, called as a converted area and its OSG caller would: through a function parameter.
    template <class V, class O>
    V takeVsg(const V& value, const O&)
    {
        return value;
    }

    template <int N, class O, class V>
    void expectSameElements(const O& o, const V& v)
    {
        for (int i = 0; i < N; ++i)
            EXPECT_EQ(o[i], v[i]) << i;
    }

    template <int N, class O, class V>
    void expectVectorRoundTrip(const O& o)
    {
        const V v = o;
        expectSameElements<N>(o, v);
        const O back = v;
        expectSameElements<N>(o, back);
    }

    TEST(OsgVsgBridge, vectors)
    {
        expectVectorRoundTrip<2, osg::Vec2f, vsg::vec2>(osg::Vec2f(0.1f, -2.5f));
        expectVectorRoundTrip<2, osg::Vec2d, vsg::dvec2>(osg::Vec2d(0.1, -2.5));
        expectVectorRoundTrip<2, osg::Vec2i, vsg::ivec2>(osg::Vec2i(7, -7));
        expectVectorRoundTrip<3, osg::Vec3f, vsg::vec3>(osg::Vec3f(0.1f, -2.5f, 3.75f));
        expectVectorRoundTrip<3, osg::Vec3d, vsg::dvec3>(osg::Vec3d(0.1, -2.5, 3.75));
        expectVectorRoundTrip<4, osg::Vec4f, vsg::vec4>(osg::Vec4f(0.1f, -2.5f, 3.75f, 9.f));
        expectVectorRoundTrip<4, osg::Vec4d, vsg::dvec4>(osg::Vec4d(0.1, -2.5, 3.75, 9.0));
    }

    // Double values that a trip through float would change.
    TEST(OsgVsgBridge, doublesStayDouble)
    {
        const osg::Vec3d o(0.1, 1.0 / 3.0, 1e300);
        const vsg::dvec3 v = takeVsg<vsg::dvec3>(o, o);
        EXPECT_EQ(v.x, 0.1);
        EXPECT_EQ(v.y, 1.0 / 3.0);
        EXPECT_EQ(v.z, 1e300);
    }

    // Existing OSG code goes on working, including the conversion between OSG's own precisions.
    TEST(OsgVsgBridge, osgOwnConversionsStillWork)
    {
        const osg::Vec3f f(1.f, 2.f, 3.f);
        const osg::Vec3d d = f;
        const osg::Vec3f narrowed = d;
        EXPECT_EQ(f, narrowed);

        const osg::Vec3f fromVsg = vsg::vec3(1.f, 2.f, 3.f);
        EXPECT_EQ(fromVsg, f);
        EXPECT_TRUE(f == vsg::vec3(1.f, 2.f, 3.f));
        EXPECT_EQ(f + vsg::vec3(1.f, 0.f, 0.f), osg::Vec3f(2.f, 2.f, 3.f));
    }

    TEST(OsgVsgBridge, quat)
    {
        const osg::Quat o(0.1, -0.2, 0.3, 0.4);
        const vsg::dquat v = o;
        expectSameElements<4>(o, v);
        const osg::Quat back = v;
        expectSameElements<4>(o, back);

        const vsg::dquat identity = osg::Quat();
        EXPECT_EQ(identity.w, 1.0);
    }

    template <class O, class V>
    void expectMatrixRoundTrip()
    {
        using T = typename O::value_type;
        O o;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                o(i, j) = static_cast<T>(i * 4 + j + 0.5);

        const V v = o;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                EXPECT_EQ(o(i, j), v[i][j]) << i << " " << j;

        const O back = v;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                EXPECT_EQ(o(i, j), back(i, j)) << i << " " << j;
    }

    // The same sixteen numbers in the same order, never transposed (testmatrixconventions.cpp, "THE ONE RULE").
    TEST(OsgVsgBridge, matrices)
    {
        expectMatrixRoundTrip<osg::Matrixf, vsg::mat4>();
        expectMatrixRoundTrip<osg::Matrixd, vsg::dmat4>();
    }

    // A translation sits at the same indices in both: OSG's row 3 and VSG's column 3.
    TEST(OsgVsgBridge, translationCarriesOver)
    {
        const vsg::dmat4 v = osg::Matrixd::translate(1.0, 2.0, 3.0);
        EXPECT_EQ(v[3][0], 1.0);
        EXPECT_EQ(v[3][1], 2.0);
        EXPECT_EQ(v[3][2], 3.0);
        EXPECT_EQ(Misc::VsgMath::getTrans(v).z, 3.0);
    }

    template <class O, class V>
    void expectBoxRoundTrip(const O& o)
    {
        const V v = o;
        expectSameElements<3>(o._min, v.min);
        expectSameElements<3>(o._max, v.max);
        const O back = v;
        expectSameElements<3>(o._min, back._min);
        expectSameElements<3>(o._max, back._max);
    }

    TEST(OsgVsgBridge, boxes)
    {
        expectBoxRoundTrip<osg::BoundingBoxf, vsg::box>(
            osg::BoundingBoxf(osg::Vec3f(1.f, 2.f, 3.f), osg::Vec3f(4.f, 5.f, 6.f)));
        expectBoxRoundTrip<osg::BoundingBoxd, vsg::dbox>(
            osg::BoundingBoxd(osg::Vec3d(0.1, 2.0, 3.0), osg::Vec3d(4.0, 5.0, 6.0)));
    }

    // A default box stays invalid through the bridge, and in both directions, with the sentinels kept.
    TEST(OsgVsgBridge, defaultBoxesStayInvalid)
    {
        expectBoxRoundTrip<osg::BoundingBoxf, vsg::box>(osg::BoundingBoxf());
        expectBoxRoundTrip<osg::BoundingBoxd, vsg::dbox>(osg::BoundingBoxd());

        const vsg::dbox fromOsg = osg::BoundingBoxd();
        EXPECT_FALSE(Misc::VsgMath::valid(fromOsg));
        const osg::BoundingBoxd fromVsg = vsg::dbox();
        EXPECT_FALSE(fromVsg.valid());
    }

    template <class O, class V>
    void expectSphereRoundTrip(const O& o)
    {
        const V v = o;
        expectSameElements<3>(o._center, v.center);
        EXPECT_EQ(o._radius, v.radius);
        const O back = v;
        expectSameElements<3>(o._center, back._center);
        EXPECT_EQ(o._radius, back._radius);
    }

    TEST(OsgVsgBridge, spheres)
    {
        expectSphereRoundTrip<osg::BoundingSpheref, vsg::sphere>(osg::BoundingSpheref(osg::Vec3f(1.f, 2.f, 3.f), 4.f));
        expectSphereRoundTrip<osg::BoundingSphered, vsg::dsphere>(osg::BoundingSphered(osg::Vec3d(0.1, 2.0, 3.0), 4.0));
        expectSphereRoundTrip<osg::BoundingSpheref, vsg::sphere>(osg::BoundingSpheref());
        expectSphereRoundTrip<osg::BoundingSphered, vsg::dsphere>(osg::BoundingSphered());

        const vsg::dsphere unset = osg::BoundingSphered();
        EXPECT_FALSE(Misc::VsgMath::valid(unset));
    }

    // The helpers take OSG values through the bridge only where the template deduces, so a call converts by hand, and the
    // helper's result goes back to OSG code on its own.
    TEST(OsgVsgBridge, helperResultsReachOsgCode)
    {
        const osg::Vec3d o(3.0, 0.0, 4.0);
        vsg::dvec3 v = o;
        const double length = Misc::VsgMath::normalizeInPlace(v);
        EXPECT_EQ(length, 5.0);
        const osg::Vec3d back = v;
        osg::Vec3d expected = o;
        expected.normalize();
        EXPECT_EQ(back, expected);

        const osg::Quat q = Misc::VsgMath::makeRotate(vsg::dvec3(1.0, 0.0, 0.0), vsg::dvec3(1.0, 0.0, 0.0));
        EXPECT_TRUE(q.zeroRotation());
    }
}

#endif
