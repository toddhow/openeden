#ifndef OPENEDEN_APPS_COMPONENTS_TESTS_MATH_CONVENTIONS_H
#define OPENEDEN_APPS_COMPONENTS_TESTS_MATH_CONVENTIONS_H

// Shared comparisons for the OSG to VSG convention tests (docs/tasks/0013-vulkan-v5-vsg-math.md, Design item 1).
//
// Tolerances follow the spec's "No behaviour change" constraint:
//  - exact (vecExact, matExact) where both libraries do the same arithmetic in the same order;
//  - 1e-6 relative (vecNear, matNear) where VSG computes differently. "Relative" means relative to the largest
//    element of the object being compared, so an element that should be 0 is not held to a tolerance of 0.
//
// Exact comparisons rely on IEEE arithmetic without contraction into FMA, which holds for CI's x86-64 GCC and Clang
// builds and for MSVC's default /fp:precise.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace MathConventions
{
    inline constexpr double kRelativeTolerance = 1e-6;

    // Works for anything with operator[](index): osg::Vec*, osg::Quat, osg::Plane, vsg::t_vec*, vsg::t_quat, vsg::t_plane.
    template <int N, class A, class B>
    ::testing::AssertionResult vecExact(const A& a, const B& b)
    {
        for (int i = 0; i < N; ++i)
        {
            const double x = static_cast<double>(a[i]);
            const double y = static_cast<double>(b[i]);
            if (x != y)
                return ::testing::AssertionFailure() << "element " << i << ": " << x << " != " << y;
        }
        return ::testing::AssertionSuccess();
    }

    template <int N, class A, class B>
    ::testing::AssertionResult vecNear(const A& a, const B& b, double relative = kRelativeTolerance)
    {
        double scale = 0.0;
        for (int i = 0; i < N; ++i)
            scale = std::max({ scale, std::abs(static_cast<double>(a[i])), std::abs(static_cast<double>(b[i])) });
        for (int i = 0; i < N; ++i)
        {
            const double x = static_cast<double>(a[i]);
            const double y = static_cast<double>(b[i]);
            if (!(std::abs(x - y) <= relative * scale))
                return ::testing::AssertionFailure() << "element " << i << ": " << x << " vs " << y;
        }
        return ::testing::AssertionSuccess();
    }

    // osg::Matrix(i, j) reads _mat[i][j] (extern/osg/osg/Matrixf:53) and vsg::t_mat4(i, j) reads value[i][j]
    // (vsg/maths/mat4.h:85), so the same indices address the same storage. See the matrix tests for what that means.
    template <class A, class B>
    ::testing::AssertionResult matExact(const A& a, const B& b)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                const double x = static_cast<double>(a(i, j));
                const double y = static_cast<double>(b(i, j));
                if (x != y)
                    return ::testing::AssertionFailure() << "(" << i << "," << j << "): " << x << " != " << y;
            }
        return ::testing::AssertionSuccess();
    }

    template <class A, class B>
    ::testing::AssertionResult matNear(const A& a, const B& b, double relative = kRelativeTolerance)
    {
        double scale = 0.0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                scale = std::max(
                    { scale, std::abs(static_cast<double>(a(i, j))), std::abs(static_cast<double>(b(i, j))) });
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                const double x = static_cast<double>(a(i, j));
                const double y = static_cast<double>(b(i, j));
                if (!(std::abs(x - y) <= relative * scale))
                    return ::testing::AssertionFailure() << "(" << i << "," << j << "): " << x << " vs " << y;
            }
        return ::testing::AssertionSuccess();
    }
}

#endif
