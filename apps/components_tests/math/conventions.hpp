#ifndef OPENEDEN_APPS_COMPONENTS_TESTS_MATH_CONVENTIONS_H
#define OPENEDEN_APPS_COMPONENTS_TESTS_MATH_CONVENTIONS_H

// Shared comparisons for the OSG to VSG convention tests (docs/tasks/0013-vulkan-v5-vsg-math.md, Design item 1).
// These tests ran OSG beside VSG until OSG left the engine; the OSG side is now OSG's recorded result, as a literal or
// from a *.golden.inc file, so "OSG X equals VSG Y" in a test's comment still says where each expected value came from.
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
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <vsg/maths/box.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/sphere.h>

namespace MathConventions
{
    inline constexpr double kRelativeTolerance = 1e-6;

    // For the expected values below.
    inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    inline constexpr double kInf = std::numeric_limits<double>::infinity();

    // The numbers in the order they are stored, i first: m[i][j] is the same stored number as OSG's m(i, j). VSG's
    // own sixteen-argument constructor takes them in another order.
    template <class T, class... Values>
    vsg::t_mat4<T> matOf(Values... values)
    {
        static_assert(sizeof...(values) == 16);
        const T numbers[] = { static_cast<T>(values)... };
        vsg::t_mat4<T> result;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result[i][j] = numbers[i * 4 + j];
        return result;
    }

    template <class... Values>
    std::array<double, sizeof...(Values)> arrayOf(Values... values)
    {
        return { static_cast<double>(values)... };
    }

    // Expected values for an assertion that runs many times, in a loop or for each type a test is instantiated with.
    // They are OSG's results, recorded in the *.golden.inc files beside the tests when OSG was deleted from the
    // engine (task 0013). Each row is one evaluation, in the order the test makes them, and floats are exact in a
    // double.
    struct GoldenTable
    {
        const char* mTest;
        const double* mValues;
        std::size_t mRowSize;
        std::size_t mRows;
    };

    class GoldenRow
    {
    public:
        GoldenRow(const double* values, std::size_t size)
            : mValues(values)
            , mSize(size)
        {
        }

        std::size_t size() const { return mSize; }
        double operator[](int i) const { return mValues[i]; }
        double operator()(int i, int j) const { return mValues[i * 4 + j]; }
        double real() const { return mValues[0]; }
        bool flag() const { return mValues[0] != 0.0; }
        int integer() const { return static_cast<int>(mValues[0]); }
        vsg::dbox box() const
        {
            return vsg::dbox(
                vsg::dvec3(mValues[0], mValues[1], mValues[2]), vsg::dvec3(mValues[3], mValues[4], mValues[5]));
        }
        vsg::dmat4 mat() const
        {
            vsg::dmat4 result;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    result[i][j] = mValues[i * 4 + j];
            return result;
        }
        vsg::dsphere sphere() const { return vsg::dsphere(mValues[0], mValues[1], mValues[2], mValues[3]); }

    private:
        const double* mValues;
        std::size_t mSize;
    };

    class GoldenSite
    {
    public:
        GoldenSite(std::initializer_list<GoldenTable> tables)
            : mTables(tables)
        {
        }

        // The next row for the running test, starting over when a test starts or runs again.
        GoldenRow next()
        {
            const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
            const std::string name = std::string(info->test_suite_name()) + "." + info->name();
            if (name != mRunning)
            {
                mRunning = name;
                mRow = 0;
            }
            for (const GoldenTable& table : mTables)
                if (name == table.mTest)
                {
                    const std::size_t row = mRow++ % table.mRows;
                    return GoldenRow(table.mValues + row * table.mRowSize, table.mRowSize);
                }
            ADD_FAILURE() << "No recorded values for " << name;
            static const double none[16] = {};
            return GoldenRow(none, 16);
        }

    private:
        std::vector<GoldenTable> mTables;
        std::string mRunning;
        std::size_t mRow = 0;
    };

    inline GoldenRow golden(GoldenSite& site)
    {
        return site.next();
    }

    // The recorded values are OSG's results on MSVC, and exact there. GCC and glibc round some sums and sin, cos and
    // sqrt differently in the last bits with no VSG involved (task 0013, note 3), so on other compilers a recorded
    // value may be off by this many units in the last place of the type that was computed, relative to the largest
    // element compared.
    // Double gets more, as some recorded values are ill-conditioned (vectors near a half-turn, say).
    inline constexpr double kOtherCompilerUlpsFloat = 16.0;
    inline constexpr double kOtherCompilerUlpsDouble = 256.0;

    template <class A, class B>
    using ComputedIn
        = std::conditional_t<std::is_same_v<std::decay_t<A>, float> || std::is_same_v<std::decay_t<B>, float>, float,
            double>;

    template <class T>
    bool sameAsRecorded(double recorded, double actual, double scale)
    {
#ifdef _MSC_VER
        static_cast<void>(scale);
        return recorded == actual;
#else
        if (recorded == actual)
            return true;
        return std::abs(recorded - actual)
            <= (std::is_same_v<T, float> ? kOtherCompilerUlpsFloat : kOtherCompilerUlpsDouble)
            * std::numeric_limits<T>::epsilon() * scale;
#endif
    }

    template <class A, class B>
    ::testing::AssertionResult recordedEqual(const A& recorded, const B& actual)
    {
        const double x = static_cast<double>(recorded);
        const double y = static_cast<double>(actual);
        if (sameAsRecorded<ComputedIn<A, B>>(x, y, std::max(std::abs(x), std::abs(y))))
            return ::testing::AssertionSuccess();
        return ::testing::AssertionFailure() << x << " != " << y;
    }

    // Works for anything with operator[](index): osg::Vec*, osg::Quat, osg::Plane, vsg::t_vec*, vsg::t_quat,
    // vsg::t_plane.
    template <int N, class A, class B>
    ::testing::AssertionResult vecExact(const A& a, const B& b)
    {
        using T = ComputedIn<decltype(a[0]), decltype(b[0])>;
        double scale = 0.0;
        for (int i = 0; i < N; ++i)
            scale = std::max({ scale, std::abs(static_cast<double>(a[i])), std::abs(static_cast<double>(b[i])) });
        for (int i = 0; i < N; ++i)
        {
            const double x = static_cast<double>(a[i]);
            const double y = static_cast<double>(b[i]);
            if (!sameAsRecorded<T>(x, y, scale))
                return ::testing::AssertionFailure() << "element " << i << ": " << x << " != " << y;
        }
        return ::testing::AssertionSuccess();
    }

    // For a check that two results differ in their bits, where the tolerance above must not apply.
    template <int N, class A, class B>
    bool vecBitExact(const A& a, const B& b)
    {
        for (int i = 0; i < N; ++i)
            if (static_cast<double>(a[i]) != static_cast<double>(b[i]))
                return false;
        return true;
    }

    // For a double vector whose numbers come from float arithmetic, so that the last bits to differ are a float's.
    template <int N, class A, class B>
    ::testing::AssertionResult vecFromFloats(const A& a, const B& b)
    {
        double scale = 0.0;
        for (int i = 0; i < N; ++i)
            scale = std::max({ scale, std::abs(static_cast<double>(a[i])), std::abs(static_cast<double>(b[i])) });
        for (int i = 0; i < N; ++i)
            if (!sameAsRecorded<float>(static_cast<double>(a[i]), static_cast<double>(b[i]), scale))
                return ::testing::AssertionFailure() << "element " << i << ": " << a[i] << " != " << b[i];
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
        using T = ComputedIn<decltype(a(0, 0)), decltype(b(0, 0))>;
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
                if (!sameAsRecorded<T>(x, y, scale))
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
