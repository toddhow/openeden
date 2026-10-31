#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <components/misc/vsgmath.hpp>

#include <type_traits>

// The deleted operators in components/misc/vsgmath.hpp, as compile-time checks: a requires expression is false when overload
// resolution picks a deleted function or finds an ambiguity, so these fail the build if the rules change.
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

    // VSG's ivec2 product is component-wise, as OSG's Vec2i's was, so it stays (task 0013, note 5).
    static_assert(std::is_same_v<decltype(vsg::ivec2() * vsg::ivec2()), vsg::ivec2>);

    // Matrix products are VSG's own now: M * v, with the order of a product flipped from OSG's.
    static_assert(std::is_same_v<decltype(vsg::dmat4() * vsg::dmat4()), vsg::dmat4>);
    static_assert(std::is_same_v<decltype(vsg::dmat4() * vsg::dvec3()), vsg::dvec3>);
}

#endif
