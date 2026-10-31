/* The helpers below are ported from OpenSceneGraph, so that OSG's exact results survive the move to VSG's math types.
 *
 * OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
 */
#ifndef OPENEDEN_COMPONENTS_MISC_VSGMATH_H
#define OPENEDEN_COMPONENTS_MISC_VSGMATH_H

// Helpers for the operations OSG's math types had and VSG's lack or compute differently (task 0013, Track V stage V5).
// There are no wrapper types here: the engine uses vsg::vec3, vsg::dmat4 and so on directly, and calls these functions
// where VSG's own answer would differ from OSG's.
//
// Sources, which task 0013 deletes at the end of V5, so this file includes neither:
//  - extern/osg/osg: Vec2f, Vec2d, Vec2i, Vec3f, Vec3d, Vec4f, Vec4d, Math, Quat, Matrixf, Matrixd, BoundingBox,
//    BoundingSphere, Plane;
//  - components/vsgadapters/osg: Quat.cpp, Matrix_implementation.cpp. MatrixDecomposition.cpp moved to vsgmath.cpp.
//
// Reading a matrix: VSG's m[i][j] is the same stored number as OSG's m(i, j). VSG calls i a column and OSG calls it a row,
// but the sixteen numbers stay where they are and are never transposed (apps/components_tests/math/testmatrixconventions.cpp).
//
// The comparisons and the order of each sum keep OSG's, because results must stay bit-identical where they can. Please do
// not tidy them. The tests are in apps/components_tests/math/testvsgmath.cpp.

#include <vsg/maths/box.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/plane.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/sphere.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <cmath>
#include <limits>

// OSG's a * b between vectors is a dot product; VSG's is a component-wise product. So that a converted file cannot silently
// change meaning, a file including this header cannot use VSG's product at all: it writes dot(a, b) or componentMultiply(a, b).
//
// How this works: when a plain function and a template fit a call equally well, C++ picks the plain function
// ([over.match.best]), and if that function is deleted the call does not compile. The scalar forms are not affected: VSG's
// friend operators take (vector, T) and (T, vector), and these do not fit a call with a scalar, as a scalar does not convert
// to a vector.
//
// vsg::ivec2 stays out on purpose: osg::Vec2i's own * is component-wise as well, so it has nothing to trap.
namespace vsg
{
    vec2 operator*(const vec2&, const vec2&) = delete;
    dvec2 operator*(const dvec2&, const dvec2&) = delete;
    vec3 operator*(const vec3&, const vec3&) = delete;
    dvec3 operator*(const dvec3&, const dvec3&) = delete;
    vec4 operator*(const vec4&, const vec4&) = delete;
    dvec4 operator*(const dvec4&, const dvec4&) = delete;

// OSG multiplies matrices in the opposite order and reads vectors as rows, so each product in a file being converted has to
// be looked at. Such a file defines this macro before including this header, and removes it once every product is checked.
// The macro goes away at the end of V5. Nothing in this header uses these operators, so it compiles either way.
#ifdef OPENEDEN_VSGMATH_CONVERTING
    mat4 operator*(const mat4&, const mat4&) = delete;
    dmat4 operator*(const dmat4&, const dmat4&) = delete;
    vec3 operator*(const vec3&, const mat4&) = delete;
    vec3 operator*(const mat4&, const vec3&) = delete;
    dvec3 operator*(const dvec3&, const dmat4&) = delete;
    dvec3 operator*(const dmat4&, const dvec3&) = delete;
    vec4 operator*(const vec4&, const mat4&) = delete;
    vec4 operator*(const mat4&, const vec4&) = delete;
    dvec4 operator*(const dvec4&, const dmat4&) = delete;
    dvec4 operator*(const dmat4&, const dvec4&) = delete;
#endif
}

namespace Misc::VsgMath
{
    // VSG's own functions, reachable by the same name as the overloads below.
    using vsg::cross;
    using vsg::dot;
    using vsg::length2;

    // ------------------------------------------------------------------------------------------------- scalars (osg/Math)

    inline constexpr double PI = 3.14159265358979323846;
    inline constexpr double PI_2 = 1.57079632679489661923;
    inline constexpr double PI_4 = 0.78539816339744830962;
    inline constexpr float PIf = 3.14159265358979323846f;
    inline constexpr float PI_2f = 1.57079632679489661923f;
    inline constexpr float PI_4f = 0.78539816339744830962f;

    // sign(0) is 1, and so is sign(-0), where std::copysign would give -1.
    template <class T>
    inline T sign(T v)
    {
        return v < T(0) ? T(-1) : T(1);
    }

    template <class T>
    inline T signOrZero(T v)
    {
        return v < T(0) ? T(-1) : (v > T(0) ? T(1) : T(0));
    }

    // An absolute difference, not a relative one; false for NaN and for two equal infinities.
    inline bool equivalent(float lhs, float rhs, float epsilon = static_cast<float>(1e-6))
    {
        const float delta = rhs - lhs;
        return delta < 0.0f ? delta >= -epsilon : delta <= epsilon;
    }

    inline bool equivalent(double lhs, double rhs, double epsilon = 1e-6)
    {
        const double delta = rhs - lhs;
        return delta < 0.0 ? delta >= -epsilon : delta <= epsilon;
    }

    // Not std::round: at the value just below 0.5, v + 0.5 rounds up to 1.
    inline float round(float v)
    {
        return v >= 0.0f ? std::floor(v + 0.5f) : std::ceil(v - 0.5f);
    }

    inline double round(double v)
    {
        return v >= 0.0 ? std::floor(v + 0.5) : std::ceil(v - 0.5);
    }

    inline float inDegrees(float angle)
    {
        return angle * static_cast<float>(PI) / 180.0f;
    }

    inline double inDegrees(double angle)
    {
        return angle * PI / 180.0;
    }

    inline float radiansToDegrees(float angle)
    {
        return angle * 180.0f / static_cast<float>(PI);
    }

    inline double radiansToDegrees(double angle)
    {
        return angle * 180.0 / PI;
    }

    // -------------------------------------------------------------------------------------------------------------- vectors

    // OSG's v.normalize(): changes v and returns its previous length. A zero vector stays zero, where vsg::normalize makes NaN.
    template <class T>
    inline T normalizeInPlace(vsg::t_vec2<T>& v)
    {
        const T norm = std::sqrt(v[0] * v[0] + v[1] * v[1]);
        if (norm > T(0))
        {
            const T inv = T(1) / norm;
            v[0] *= inv;
            v[1] *= inv;
        }
        return norm;
    }

    template <class T>
    inline T normalizeInPlace(vsg::t_vec3<T>& v)
    {
        const T norm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (norm > T(0))
        {
            const T inv = T(1) / norm;
            v[0] *= inv;
            v[1] *= inv;
            v[2] *= inv;
        }
        return norm;
    }

    template <class T>
    inline T normalizeInPlace(vsg::t_vec4<T>& v)
    {
        const T norm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
        if (norm > T(0))
        {
            const T inv = T(1) / norm;
            v[0] *= inv;
            v[1] *= inv;
            v[2] *= inv;
            v[3] *= inv;
        }
        return norm;
    }

    // VSG has dot for vec2 and vec3 (brought in above), not for vec4.
    template <class T>
    inline T dot(const vsg::t_vec4<T>& lhs, const vsg::t_vec4<T>& rhs)
    {
        return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2] + lhs[3] * rhs[3];
    }

    template <class T>
    inline vsg::t_vec2<T> componentMultiply(const vsg::t_vec2<T>& lhs, const vsg::t_vec2<T>& rhs)
    {
        return vsg::t_vec2<T>(lhs[0] * rhs[0], lhs[1] * rhs[1]);
    }

    template <class T>
    inline vsg::t_vec3<T> componentMultiply(const vsg::t_vec3<T>& lhs, const vsg::t_vec3<T>& rhs)
    {
        return vsg::t_vec3<T>(lhs[0] * rhs[0], lhs[1] * rhs[1], lhs[2] * rhs[2]);
    }

    template <class T>
    inline vsg::t_vec4<T> componentMultiply(const vsg::t_vec4<T>& lhs, const vsg::t_vec4<T>& rhs)
    {
        return vsg::t_vec4<T>(lhs[0] * rhs[0], lhs[1] * rhs[1], lhs[2] * rhs[2], lhs[3] * rhs[3]);
    }

    template <class T>
    inline vsg::t_vec2<T> componentDivide(const vsg::t_vec2<T>& lhs, const vsg::t_vec2<T>& rhs)
    {
        return vsg::t_vec2<T>(lhs[0] / rhs[0], lhs[1] / rhs[1]);
    }

    template <class T>
    inline vsg::t_vec3<T> componentDivide(const vsg::t_vec3<T>& lhs, const vsg::t_vec3<T>& rhs)
    {
        return vsg::t_vec3<T>(lhs[0] / rhs[0], lhs[1] / rhs[1], lhs[2] / rhs[2]);
    }

    template <class T>
    inline vsg::t_vec4<T> componentDivide(const vsg::t_vec4<T>& lhs, const vsg::t_vec4<T>& rhs)
    {
        return vsg::t_vec4<T>(lhs[0] / rhs[0], lhs[1] / rhs[1], lhs[2] / rhs[2], lhs[3] / rhs[3]);
    }

    // osg::Vec2i + int and - int.
    inline vsg::ivec2 add(const vsg::ivec2& v, int rhs)
    {
        return vsg::ivec2(v[0] + rhs, v[1] + rhs);
    }

    inline vsg::ivec2 sub(const vsg::ivec2& v, int rhs)
    {
        return vsg::ivec2(v[0] - rhs, v[1] - rhs);
    }

    // Any one NaN element counts; infinity does not. OSG's v.valid() is !isNaN(v).
    template <class T>
    inline bool isNaN(const vsg::t_vec2<T>& v)
    {
        return std::isnan(v[0]) || std::isnan(v[1]);
    }

    template <class T>
    inline bool isNaN(const vsg::t_vec3<T>& v)
    {
        return std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2]);
    }

    template <class T>
    inline bool isNaN(const vsg::t_vec4<T>& v)
    {
        return std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2]) || std::isnan(v[3]);
    }

    // ---------------------------------------------------------------------------------------------------------- quaternions

    // VSG has length and dot for quats, but no length2.
    template <class T>
    inline T length2(const vsg::t_quat<T>& q)
    {
        return q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    }

    // OSG's conjugate / length2. vsg::inverse divides by length, so it is wrong for a non-unit quat.
    // A zero quat gives NaN, as OSG's does. Not inline: MSVC warns of the division when it can see a literal zero.
    vsg::dquat inverse(const vsg::dquat& q);

    // OSG's -q negates all four, which is the same rotation. VSG's -q is the conjugate, which is the opposite rotation.
    inline vsg::dquat negate(const vsg::dquat& q)
    {
        return vsg::dquat(-q[0], -q[1], -q[2], -q[3]);
    }

    // OSG's Quat(angle, axis), Quat.cpp makeRotate. The threshold on the axis is OSG's; VSG's own is machine epsilon.
    template <class T>
    inline vsg::dquat makeRotateFromAxis(double angle, const vsg::t_vec3<T>& axis)
    {
        const double x = axis[0];
        const double y = axis[1];
        const double z = axis[2];
        const double epsilon = 0.0000001;
        const double length = std::sqrt(x * x + y * y + z * z);
        if (length < epsilon)
            return vsg::dquat();

        const double inversenorm = 1.0 / length;
        const double coshalfangle = std::cos(0.5 * angle);
        const double sinhalfangle = std::sin(0.5 * angle);
        return vsg::dquat(x * sinhalfangle * inversenorm, y * sinhalfangle * inversenorm, z * sinhalfangle * inversenorm,
            coshalfangle);
    }

    // The rotation taking one vector to another: OSG's Quat::makeRotate(from, to) by Nicolas Brodu, in Quat.cpp. It works
    // for vectors of any length. vsg::dquat(from, to) can make NaN from parallel vectors, and picks another axis for opposite
    // ones. A zero vector gives NaN here, as it does in OSG.
    inline vsg::dquat makeRotate(const vsg::dvec3& from, const vsg::dvec3& to)
    {
        vsg::dvec3 sourceVector = from;
        vsg::dvec3 targetVector = to;

        const double fromLen2 = vsg::length2(from);
        double fromLen;
        // Normalize only when necessary, epsilon test.
        if ((fromLen2 < 1.0 - 1e-7) || (fromLen2 > 1.0 + 1e-7))
        {
            fromLen = std::sqrt(fromLen2);
            sourceVector = vsg::dvec3(sourceVector[0] / fromLen, sourceVector[1] / fromLen, sourceVector[2] / fromLen);
        }
        else
            fromLen = 1.0;

        const double toLen2 = vsg::length2(to);
        // Normalize only when necessary, epsilon test.
        if ((toLen2 < 1.0 - 1e-7) || (toLen2 > 1.0 + 1e-7))
        {
            double toLen;
            // Reuse fromLen when mapping two vectors of the same length.
            if ((toLen2 > fromLen2 - 1e-7) && (toLen2 < fromLen2 + 1e-7))
                toLen = fromLen;
            else
                toLen = std::sqrt(toLen2);
            targetVector = vsg::dvec3(targetVector[0] / toLen, targetVector[1] / toLen, targetVector[2] / toLen);
        }

        // "Dot product plus one" as the test, as it is reused later on.
        const double dotProdPlus1 = 1.0 + vsg::dot(sourceVector, targetVector);

        // The degenerate case of a full u-turn. Take a vector orthogonal to the source as the axis, with an angle of pi. At
        // least one element of a unit vector is over 0.6, so one of these branches avoids a tiny norm.
        if (dotProdPlus1 < 1e-7)
        {
            if (std::fabs(sourceVector[0]) < 0.6)
            {
                const double norm = std::sqrt(1.0 - sourceVector[0] * sourceVector[0]);
                return vsg::dquat(0.0, sourceVector[2] / norm, -sourceVector[1] / norm, 0.0);
            }
            if (std::fabs(sourceVector[1]) < 0.6)
            {
                const double norm = std::sqrt(1.0 - sourceVector[1] * sourceVector[1]);
                return vsg::dquat(-sourceVector[2] / norm, 0.0, sourceVector[0] / norm, 0.0);
            }
            const double norm = std::sqrt(1.0 - sourceVector[2] * sourceVector[2]);
            return vsg::dquat(sourceVector[1] / norm, -sourceVector[0] / norm, 0.0, 0.0);
        }

        // The shortest rotation between the two normalized vectors; still valid when they are parallel. The division comes
        // before the cross product: in OSG's source ^ target / (2.0 * s), / binds tighter than ^.
        const double s = std::sqrt(0.5 * dotProdPlus1);
        const double divisor = 2.0 * s;
        const vsg::dvec3 scaledTarget(targetVector[0] / divisor, targetVector[1] / divisor, targetVector[2] / divisor);
        const vsg::dvec3 tmp = vsg::cross(sourceVector, scaledTarget);
        return vsg::dquat(tmp[0], tmp[1], tmp[2], s);
    }

    // Float vectors are widened, and the work is in double. vsg::quat(vec3, vec3) would work in float.
    inline vsg::dquat makeRotate(const vsg::vec3& from, const vsg::vec3& to)
    {
        return makeRotate(vsg::dvec3(from), vsg::dvec3(to));
    }

    // OSG's q.getRotate(angle, axis), with the axis (0, 0, 1) for no rotation. A float axis narrows.
    template <class T>
    inline void getRotate(const vsg::dquat& q, double& angle, vsg::t_vec3<T>& axis)
    {
        const double sinhalfangle = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);

        angle = 2.0 * std::atan2(sinhalfangle, q[3]);
        if (sinhalfangle != 0.0)
        {
            axis[0] = static_cast<T>(q[0] / sinhalfangle);
            axis[1] = static_cast<T>(q[1] / sinhalfangle);
            axis[2] = static_cast<T>(q[2] / sinhalfangle);
        }
        else
        {
            axis[0] = T(0);
            axis[1] = T(0);
            axis[2] = T(1);
        }
    }

    // The rotation matrix of a quat: OSG's Matrix::rotate(q), Matrix_implementation::setRotate. It copes with non-unit quats,
    // and a zero quat gives a zero rotation block, where vsg::rotate assumes unit length and gives the identity for zero.
    // For OSG's Matrixf::rotate(q), narrow the result: vsg::mat4(toMatrix(q)).
    inline vsg::dmat4 toMatrix(const vsg::dquat& q)
    {
        vsg::dmat4 m;
        const double length2OfQ = length2(q);
        if (std::fabs(length2OfQ) <= std::numeric_limits<double>::min())
        {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    m[i][j] = 0.0;
            return m;
        }

        // Normalizing needs no square root, as every coefficient below is a product of two elements of q.
        const double rlength2 = length2OfQ != 1.0 ? 2.0 / length2OfQ : 2.0;

        const double x2 = rlength2 * q[0];
        const double y2 = rlength2 * q[1];
        const double z2 = rlength2 * q[2];

        const double xx = q[0] * x2;
        const double xy = q[0] * y2;
        const double xz = q[0] * z2;

        const double yy = q[1] * y2;
        const double yz = q[1] * z2;
        const double zz = q[2] * z2;

        const double wx = q[3] * x2;
        const double wy = q[3] * y2;
        const double wz = q[3] * z2;

        m[0][0] = 1.0 - (yy + zz);
        m[1][0] = xy - wz;
        m[2][0] = xz + wy;

        m[0][1] = xy + wz;
        m[1][1] = 1.0 - (xx + zz);
        m[2][1] = yz - wx;

        m[0][2] = xz - wy;
        m[1][2] = yz + wx;
        m[2][2] = 1.0 - (xx + yy);
        return m;
    }

    // OSG's Matrix::getRotate. The scale is not removed, unlike vsg::decompose, and nothing checks the last row or column.
    // In a float matrix the traces are computed in float.
    template <class T>
    inline vsg::dquat rotationOf(const vsg::t_mat4<T>& m)
    {
        T tq[4];
        // Use tq to store the largest trace.
        tq[0] = 1 + m[0][0] + m[1][1] + m[2][2];
        tq[1] = 1 + m[0][0] - m[1][1] - m[2][2];
        tq[2] = 1 - m[0][0] + m[1][1] - m[2][2];
        tq[3] = 1 - m[0][0] - m[1][1] + m[2][2];

        int j = 0;
        for (int i = 1; i < 4; ++i)
            j = (tq[i] > tq[j]) ? i : j;

        vsg::dquat q;
        if (j == 0)
        {
            q.w = tq[0];
            q.x = m[1][2] - m[2][1];
            q.y = m[2][0] - m[0][2];
            q.z = m[0][1] - m[1][0];
        }
        else if (j == 1)
        {
            q.w = m[1][2] - m[2][1];
            q.x = tq[1];
            q.y = m[0][1] + m[1][0];
            q.z = m[2][0] + m[0][2];
        }
        else if (j == 2)
        {
            q.w = m[2][0] - m[0][2];
            q.x = m[0][1] + m[1][0];
            q.y = tq[2];
            q.z = m[1][2] + m[2][1];
        }
        else
        {
            q.w = m[0][1] - m[1][0];
            q.x = m[2][0] + m[0][2];
            q.y = m[1][2] + m[2][1];
            q.z = tq[3];
        }

        const T s = static_cast<T>(std::sqrt(0.25 / tq[j]));
        q.w *= s;
        q.x *= s;
        q.y *= s;
        q.z *= s;
        return q;
    }

    // ------------------------------------------------------------------------------------------------------------- matrices

    namespace Detail
    {
        // Small products in OSG's own storage order, so that no helper here relies on VSG's operators, which a file under
        // conversion deletes (see the top of this file).
        template <class T>
        inline T innerProduct(const vsg::t_mat4<T>& a, const vsg::t_mat4<T>& b, int r, int c)
        {
            return a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c] + a[r][3] * b[3][c];
        }

        // OSG's m.preMult(other).
        template <class T>
        inline void preMult(vsg::t_mat4<T>& m, const vsg::t_mat4<T>& other)
        {
            T t[4];
            for (int col = 0; col < 4; ++col)
            {
                t[0] = innerProduct(other, m, 0, col);
                t[1] = innerProduct(other, m, 1, col);
                t[2] = innerProduct(other, m, 2, col);
                t[3] = innerProduct(other, m, 3, col);
                m[0][col] = t[0];
                m[1][col] = t[1];
                m[2][col] = t[2];
                m[3][col] = t[3];
            }
        }

        // OSG's m.postMult(other).
        template <class T>
        inline void postMult(vsg::t_mat4<T>& m, const vsg::t_mat4<T>& other)
        {
            T t[4];
            for (int row = 0; row < 4; ++row)
            {
                t[0] = innerProduct(m, other, row, 0);
                t[1] = innerProduct(m, other, row, 1);
                t[2] = innerProduct(m, other, row, 2);
                t[3] = innerProduct(m, other, row, 3);
                m[row][0] = t[0];
                m[row][1] = t[1];
                m[row][2] = t[2];
                m[row][3] = t[3];
            }
        }

        // OSG's v * m for a point, Matrix::preMult(Vec3): divides by the projective term.
        template <class T, class U>
        inline vsg::t_vec3<U> transformPoint(const vsg::t_vec3<U>& v, const vsg::t_mat4<T>& m)
        {
            const T d = static_cast<T>(T(1) / (m[0][3] * v[0] + m[1][3] * v[1] + m[2][3] * v[2] + m[3][3]));
            return vsg::t_vec3<U>(static_cast<U>((m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2] + m[3][0]) * d),
                static_cast<U>((m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2] + m[3][1]) * d),
                static_cast<U>((m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2] + m[3][2]) * d));
        }

        // vsgmath.cpp: OSG's polar decomposition, on the matrix's numbers as doubles, in OSG's (i, j) order.
        void decompose(const double m[4][4], vsg::dvec3& t, vsg::dquat& r, vsg::dvec3& s, vsg::dquat& so);
    }

    // OSG returns a double vector from both matrix types.
    template <class T>
    inline vsg::dvec3 getTrans(const vsg::t_mat4<T>& m)
    {
        return vsg::dvec3(m[3][0], m[3][1], m[3][2]);
    }

    // Only the three elements change, and the last column stays.
    template <class T, class U>
    inline void setTrans(vsg::t_mat4<T>& m, const vsg::t_vec3<U>& v)
    {
        m[3][0] = static_cast<T>(v[0]);
        m[3][1] = static_cast<T>(v[1]);
        m[3][2] = static_cast<T>(v[2]);
    }

    // OSG's m.getScale() reads across the storage, not along m[i]. With a rotation and a non-uniform scale its answer is not
    // the scale at all, and that is what callers got and must keep.
    template <class T>
    inline vsg::dvec3 getScale(const vsg::t_mat4<T>& m)
    {
        const vsg::dvec3 x(m[0][0], m[1][0], m[2][0]);
        const vsg::dvec3 y(m[0][1], m[1][1], m[2][1]);
        const vsg::dvec3 z(m[0][2], m[1][2], m[2][2]);
        return vsg::dvec3(vsg::length(x), vsg::length(y), vsg::length(z));
    }

    // The argument order keeps OSG's meaning: (v, m) is OSG's v * M and (m, v) is OSG's M * v, on the 3x3 part only.
    template <class T, class U>
    inline vsg::t_vec3<U> transform3x3(const vsg::t_vec3<U>& v, const vsg::t_mat4<T>& m)
    {
        return vsg::t_vec3<U>(static_cast<U>(m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2]),
            static_cast<U>(m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2]),
            static_cast<U>(m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2]));
    }

    template <class T, class U>
    inline vsg::t_vec3<U> transform3x3(const vsg::t_mat4<T>& m, const vsg::t_vec3<U>& v)
    {
        return vsg::t_vec3<U>(static_cast<U>(m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2]),
            static_cast<U>(m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2]),
            static_cast<U>(m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]));
    }

    // OSG's in-place forms. A zero component of a translation is skipped, so an infinity in the matching row stays out of the
    // result, where a plain product would make NaN from 0 * infinity.
    template <class T, class U>
    inline void preMultTranslate(vsg::t_mat4<T>& m, const vsg::t_vec3<U>& v)
    {
        for (int i = 0; i < 3; ++i)
        {
            const U tmp = v[i];
            if (tmp == U(0))
                continue;
            m[3][0] = static_cast<T>(m[3][0] + tmp * m[i][0]);
            m[3][1] = static_cast<T>(m[3][1] + tmp * m[i][1]);
            m[3][2] = static_cast<T>(m[3][2] + tmp * m[i][2]);
            m[3][3] = static_cast<T>(m[3][3] + tmp * m[i][3]);
        }
    }

    template <class T, class U>
    inline void postMultTranslate(vsg::t_mat4<T>& m, const vsg::t_vec3<U>& v)
    {
        for (int i = 0; i < 3; ++i)
        {
            const U tmp = v[i];
            if (tmp == U(0))
                continue;
            m[0][i] = static_cast<T>(m[0][i] + tmp * m[0][3]);
            m[1][i] = static_cast<T>(m[1][i] + tmp * m[1][3]);
            m[2][i] = static_cast<T>(m[2][i] + tmp * m[2][3]);
            m[3][i] = static_cast<T>(m[3][i] + tmp * m[3][3]);
        }
    }

    template <class T, class U>
    inline void preMultScale(vsg::t_mat4<T>& m, const vsg::t_vec3<U>& v)
    {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 4; ++j)
                m[i][j] = static_cast<T>(m[i][j] * v[i]);
    }

    template <class T, class U>
    inline void postMultScale(vsg::t_mat4<T>& m, const vsg::t_vec3<U>& v)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 3; ++j)
                m[i][j] = static_cast<T>(m[i][j] * v[j]);
    }

    // OSG returns early on the identity quat, so the matrix stays exactly as it was, including any infinity a product would
    // spread. The rotation is narrowed first in a float matrix, as OSG's Matrixf::setRotate does.
    template <class T>
    inline void preMultRotate(vsg::t_mat4<T>& m, const vsg::dquat& q)
    {
        if (q[0] == 0.0 && q[1] == 0.0 && q[2] == 0.0 && q[3] == 1.0)
            return;
        Detail::preMult(m, vsg::t_mat4<T>(toMatrix(q)));
    }

    template <class T>
    inline void postMultRotate(vsg::t_mat4<T>& m, const vsg::dquat& q)
    {
        if (q[0] == 0.0 && q[1] == 0.0 && q[2] == 0.0 && q[3] == 1.0)
            return;
        Detail::postMult(m, vsg::t_mat4<T>(toMatrix(q)));
    }

    // OSG's m.invert_4x3(mat), Matrix_implementation.cpp. It keeps OSG's branch on m[3][3]: when that is close to 1 the last
    // column is ignored, even if it is not (0, 0, 0, 1), and the result has 1 there. Otherwise the full inverse is computed.
    // Neither vsg::inverse nor vsg::inverse_4x3 matches this on every input. A singular matrix gives infinity and NaN.
    template <class T>
    inline vsg::t_mat4<T> invert4x3(const vsg::t_mat4<T>& mat)
    {
        vsg::t_mat4<T> m;

        T r00 = mat[0][0];
        T r01 = mat[0][1];
        T r02 = mat[0][2];
        T r10 = mat[1][0];
        T r11 = mat[1][1];
        T r12 = mat[1][2];
        T r20 = mat[2][0];
        T r21 = mat[2][1];
        T r22 = mat[2][2];

        // Partially compute the inverse of the rotation.
        m[0][0] = r11 * r22 - r12 * r21;
        m[0][1] = r02 * r21 - r01 * r22;
        m[0][2] = r01 * r12 - r02 * r11;

        // The determinant of the rotation, from the three elements just computed.
        const T oneOverDet = static_cast<T>(1.0 / (r00 * m[0][0] + r10 * m[0][1] + r20 * m[0][2]));
        r00 *= oneOverDet;
        r10 *= oneOverDet;
        r20 *= oneOverDet;

        // Finish the inverse of the rotation.
        m[0][0] *= oneOverDet;
        m[0][1] *= oneOverDet;
        m[0][2] *= oneOverDet;
        m[0][3] = T(0);
        m[1][0] = r12 * r20 - r10 * r22;
        m[1][1] = r00 * r22 - r02 * r20;
        m[1][2] = r02 * r10 - r00 * r12;
        m[1][3] = T(0);
        m[2][0] = r10 * r21 - r11 * r20;
        m[2][1] = r01 * r20 - r00 * r21;
        m[2][2] = r00 * r11 - r01 * r10;
        m[2][3] = T(0);
        m[3][3] = T(1);

        const T d = mat[3][3];
        const double dMinusOne = d - 1.0;

        T tx = mat[3][0];
        T ty = mat[3][1];
        T tz = mat[3][2];

        if (dMinusOne * dMinusOne > 1.0e-6) // Involves perspective, so compute the full inverse.
        {
            m[3][0] = m[3][1] = m[3][2] = T(0);

            const T a = mat[0][3];
            const T b = mat[1][3];
            const T c = mat[2][3];
            const T px = m[0][0] * a + m[0][1] * b + m[0][2] * c;
            const T py = m[1][0] * a + m[1][1] * b + m[1][2] * c;
            const T pz = m[2][0] * a + m[2][1] * b + m[2][2] * c;

            const T oneOverS = static_cast<T>(1.0 / (d - (tx * px + ty * py + tz * pz)));

            tx *= oneOverS;
            ty *= oneOverS;
            tz *= oneOverS;

            // The inverse of trans * corr, in the comment at OSG's invert_4x3.
            vsg::t_mat4<T> tpInverse;
            tpInverse[0][0] = static_cast<T>(tx * px + 1.0);
            tpInverse[0][1] = ty * px;
            tpInverse[0][2] = tz * px;
            tpInverse[0][3] = -px * oneOverS;
            tpInverse[1][0] = tx * py;
            tpInverse[1][1] = static_cast<T>(ty * py + 1.0);
            tpInverse[1][2] = tz * py;
            tpInverse[1][3] = -py * oneOverS;
            tpInverse[2][0] = tx * pz;
            tpInverse[2][1] = ty * pz;
            tpInverse[2][2] = static_cast<T>(tz * pz + 1.0);
            tpInverse[2][3] = -pz * oneOverS;
            tpInverse[3][0] = -tx;
            tpInverse[3][1] = -ty;
            tpInverse[3][2] = -tz;
            tpInverse[3][3] = oneOverS;

            Detail::preMult(m, tpInverse);
        }
        else // The rightmost column counts as (0, 0, 0, 1), so it can be ignored.
        {
            m[3][0] = -(tx * m[0][0] + ty * m[1][0] + tz * m[2][0]);
            m[3][1] = -(tx * m[0][1] + ty * m[1][1] + tz * m[2][1]);
            m[3][2] = -(tx * m[0][2] + ty * m[1][2] + tz * m[2][2]);
        }

        return m;
    }

    // OSG's m.invert(mat): invert4x3 when the last column is exactly (0, 0, 0, 1), and otherwise OSG's invert_4x4 (Gauss-Jordan
    // elimination with full pivoting). Returns false for a matrix it cannot invert, leaving result part-way, as OSG does.
    template <class T>
    inline bool invert(const vsg::t_mat4<T>& mat, vsg::t_mat4<T>& result)
    {
        if (mat[0][3] == T(0) && mat[1][3] == T(0) && mat[2][3] == T(0) && mat[3][3] == T(1))
        {
            result = invert4x3(mat);
            return true;
        }

        vsg::t_mat4<T> m = mat;

        unsigned int indxc[4], indxr[4], ipiv[4];
        unsigned int icol = 0;
        unsigned int irow = 0;

        for (unsigned int j = 0; j < 4; ++j)
            ipiv[j] = 0;

        for (unsigned int i = 0; i < 4; ++i)
        {
            double big = 0.0;
            for (unsigned int j = 0; j < 4; ++j)
                if (ipiv[j] != 1)
                    for (unsigned int k = 0; k < 4; ++k)
                    {
                        if (ipiv[k] == 0)
                        {
                            const T magnitude = m[j][k] >= T(0) ? m[j][k] : -m[j][k];
                            if (magnitude >= big)
                            {
                                big = magnitude;
                                irow = j;
                                icol = k;
                            }
                        }
                        else if (ipiv[k] > 1)
                        {
                            result = m;
                            return false;
                        }
                    }
            ++ipiv[icol];
            if (irow != icol)
                for (unsigned int l = 0; l < 4; ++l)
                {
                    const T temp = m[irow][l];
                    m[irow][l] = m[icol][l];
                    m[icol][l] = temp;
                }

            indxr[i] = irow;
            indxc[i] = icol;
            if (m[icol][icol] == T(0))
            {
                result = m;
                return false;
            }

            const double pivinv = 1.0 / m[icol][icol];
            m[icol][icol] = T(1);
            for (unsigned int l = 0; l < 4; ++l)
                m[icol][l] = static_cast<T>(m[icol][l] * pivinv);
            for (unsigned int ll = 0; ll < 4; ++ll)
                if (ll != icol)
                {
                    const double dum = m[ll][icol];
                    m[ll][icol] = T(0);
                    for (unsigned int l = 0; l < 4; ++l)
                        m[ll][l] = static_cast<T>(m[ll][l] - m[icol][l] * dum);
                }
        }
        for (int lx = 4; lx > 0; --lx)
        {
            if (indxr[lx - 1] != indxc[lx - 1])
                for (unsigned int k = 0; k < 4; ++k)
                {
                    const T temp = m[k][indxr[lx - 1]];
                    m[k][indxr[lx - 1]] = m[k][indxc[lx - 1]];
                    m[k][indxc[lx - 1]] = temp;
                }
        }

        result = m;
        return true;
    }

    // OSG's polar decomposition, m.decompose(t, r, s, so), from Graphics Gems IV (vsgmath.cpp). Where vsg::decompose can fail or
    // loses a mirror, this always gives parts that rebuild the matrix. A negative scale comes back negative.
    template <class T, class U>
    inline void decompose(
        const vsg::t_mat4<T>& m, vsg::t_vec3<U>& t, vsg::dquat& r, vsg::t_vec3<U>& s, vsg::dquat& so)
    {
        double values[4][4];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                values[i][j] = m[i][j];

        vsg::dvec3 translation;
        vsg::dvec3 scale;
        Detail::decompose(values, translation, r, scale, so);
        t = vsg::t_vec3<U>(translation);
        s = vsg::t_vec3<U>(scale);
    }

    // OSG's m.getLookAt(eye, center, up, lookDistance).
    template <class T, class U>
    inline void getLookAt(const vsg::t_mat4<T>& m, vsg::t_vec3<U>& eye, vsg::t_vec3<U>& center, vsg::t_vec3<U>& up,
        double lookDistance = 1.0)
    {
        vsg::t_mat4<T> inv;
        invert(m, inv);

        // e and c, because eye and center are references that may point to the same variable.
        const vsg::t_vec3<U> e = Detail::transformPoint(vsg::t_vec3<U>(U(0), U(0), U(0)), inv);
        up = transform3x3(m, vsg::t_vec3<U>(U(0), U(1), U(0)));
        vsg::t_vec3<U> c = transform3x3(m, vsg::t_vec3<U>(U(0), U(0), U(-1)));
        normalizeInPlace(c);

        const U distance = static_cast<U>(lookDistance);
        eye = e;
        center = vsg::t_vec3<U>(e[0] + c[0] * distance, e[1] + c[1] * distance, e[2] + c[2] * distance);
    }

    // The matrix form of makeRotate; the from, to form above returns a quat. OSG's Matrix::rotate(angle, axis) goes through a
    // quat: a zero axis gives the identity, where vsg::rotate(angle, axis) gives cos(angle) on the diagonal.
    template <class T>
    inline vsg::dmat4 makeRotate(double angle, const vsg::t_vec3<T>& axis)
    {
        return toMatrix(makeRotateFromAxis(angle, axis));
    }

    // ---------------------------------------------------------------------------------------------------------------- boxes

    // All three axes. vsg::t_box::valid checks X only.
    template <class T>
    inline bool valid(const vsg::t_box<T>& box)
    {
        return box.max.x >= box.min.x && box.max.y >= box.min.y && box.max.z >= box.min.z;
    }

    // These run on invalid boxes too, with no check, as in OSG.
    template <class T>
    inline vsg::t_vec3<T> center(const vsg::t_box<T>& box)
    {
        return vsg::t_vec3<T>(box.min.x + box.max.x, box.min.y + box.max.y, box.min.z + box.max.z) * T(0.5);
    }

    template <class T>
    inline T radius2(const vsg::t_box<T>& box)
    {
        const T dx = box.max.x - box.min.x;
        const T dy = box.max.y - box.min.y;
        const T dz = box.max.z - box.min.z;
        return static_cast<T>(0.25 * (dx * dx + dy * dy + dz * dz));
    }

    template <class T>
    inline T radius(const vsg::t_box<T>& box)
    {
        return std::sqrt(radius2(box));
    }

    // Bit 0 selects X max, bit 1 Y max, bit 2 Z max.
    template <class T>
    inline vsg::t_vec3<T> corner(const vsg::t_box<T>& box, unsigned int pos)
    {
        return vsg::t_vec3<T>((pos & 1) ? box.max.x : box.min.x, (pos & 2) ? box.max.y : box.min.y,
            (pos & 4) ? box.max.z : box.min.z);
    }

    // Inclusive at the faces; false for an invalid box.
    template <class T>
    inline bool contains(const vsg::t_box<T>& box, const vsg::t_vec3<T>& v)
    {
        return valid(box) && (v.x >= box.min.x && v.x <= box.max.x) && (v.y >= box.min.y && v.y <= box.max.y)
            && (v.z >= box.min.z && v.z <= box.max.z);
    }

    template <class T>
    inline bool contains(const vsg::t_box<T>& box, const vsg::t_vec3<T>& v, T epsilon)
    {
        return valid(box) && ((v.x + epsilon) >= box.min.x && (v.x - epsilon) <= box.max.x)
            && ((v.y + epsilon) >= box.min.y && (v.y - epsilon) <= box.max.y)
            && ((v.z + epsilon) >= box.min.z && (v.z - epsilon) <= box.max.z);
    }

    // Per axis, with no validity check: two boxes that do not meet give an inverted box.
    template <class T>
    inline vsg::t_box<T> intersect(const vsg::t_box<T>& a, const vsg::t_box<T>& b)
    {
        const auto maximum = [](T lhs, T rhs) { return lhs < rhs ? rhs : lhs; };
        const auto minimum = [](T lhs, T rhs) { return lhs < rhs ? lhs : rhs; };
        return vsg::t_box<T>(
            vsg::t_vec3<T>(maximum(a.min.x, b.min.x), maximum(a.min.y, b.min.y), maximum(a.min.z, b.min.z)),
            vsg::t_vec3<T>(minimum(a.max.x, b.max.x), minimum(a.max.y, b.max.y), minimum(a.max.z, b.max.z)));
    }

    // An invalid box is skipped as a whole. vsg::t_box::add merges each axis on its own and has no validity check.
    template <class T>
    inline void expandBy(vsg::t_box<T>& box, const vsg::t_box<T>& other)
    {
        if (!valid(other))
            return;
        box.add(other);
    }

    // -------------------------------------------------------------------------------------------------------------- spheres

    template <class T>
    inline bool valid(const vsg::t_sphere<T>& sphere)
    {
        return sphere.radius >= T(0);
    }

    // An invalid sphere (negative radius) is skipped; radius 0 is valid.
    template <class T, class R>
    inline void expandBy(vsg::t_box<T>& box, const vsg::t_sphere<R>& sphere)
    {
        if (!valid(sphere))
            return;

        for (int i = 0; i < 3; ++i)
        {
            if (sphere.center[i] - sphere.radius < box.min[i])
                box.min[i] = static_cast<T>(sphere.center[i] - sphere.radius);
            if (sphere.center[i] + sphere.radius > box.max[i])
                box.max[i] = static_cast<T>(sphere.center[i] + sphere.radius);
        }
    }

    // An unset sphere becomes the point with radius 0. Outside the sphere, the centre moves to minimise the increase. Inside,
    // nothing changes. The point is widened or narrowed to the sphere's type first.
    template <class T, class R>
    inline void expandBy(vsg::t_sphere<T>& sphere, const vsg::t_vec3<R>& point)
    {
        const vsg::t_vec3<T> p(point);
        if (!valid(sphere))
        {
            sphere.center = p;
            sphere.radius = T(0);
            return;
        }

        const vsg::t_vec3<T> dv = p - sphere.center;
        const T r = vsg::length(dv);
        if (r > sphere.radius)
        {
            const T dr = static_cast<T>((r - sphere.radius) * 0.5);
            sphere.center += dv * (dr / r);
            sphere.radius += dr;
        }
    }

    // As expandBy, but the centre stays and only the radius grows.
    template <class T, class R>
    inline void expandRadiusBy(vsg::t_sphere<T>& sphere, const vsg::t_vec3<R>& point)
    {
        const vsg::t_vec3<T> p(point);
        if (!valid(sphere))
        {
            sphere.center = p;
            sphere.radius = T(0);
            return;
        }

        const T r = vsg::length(p - sphere.center);
        if (r > sphere.radius)
            sphere.radius = r;
    }

    // The distance and ratio are double even for a float sphere, as in OSG.
    template <class T>
    inline void expandBy(vsg::t_sphere<T>& sphere, const vsg::t_sphere<T>& other)
    {
        // Ignore an invalid incoming sphere.
        if (!valid(other))
            return;

        // This sphere is not set, so use the incoming one.
        if (!valid(sphere))
        {
            sphere.center = other.center;
            sphere.radius = other.radius;
            return;
        }

        const double d = vsg::length(sphere.center - other.center);

        // The incoming sphere is already inside this one.
        if (d + other.radius <= sphere.radius)
            return;

        // The incoming sphere completely contains this one.
        if (d + sphere.radius <= other.radius)
        {
            sphere.center = other.center;
            sphere.radius = other.radius;
            return;
        }

        // The centre lies halfway along the line between the furthest points on the edges of the two spheres.
        const double newRadius = (sphere.radius + d + other.radius) * 0.5;
        const double ratio = (newRadius - sphere.radius) / d;

        sphere.center[0] = static_cast<T>(sphere.center[0] + (other.center[0] - sphere.center[0]) * ratio);
        sphere.center[1] = static_cast<T>(sphere.center[1] + (other.center[1] - sphere.center[1]) * ratio);
        sphere.center[2] = static_cast<T>(sphere.center[2] + (other.center[2] - sphere.center[2]) * ratio);

        sphere.radius = static_cast<T>(newRadius);
    }

    // Not the sphere around the union: OSG adds to a box the point of this sphere opposite each corner, then takes that box's
    // centre and radius. A box with any axis inverted is skipped.
    template <class T, class R>
    inline void expandBy(vsg::t_sphere<T>& sphere, const vsg::t_box<R>& box)
    {
        if (!valid(box))
            return;

        const vsg::t_box<T> source(box);
        if (!valid(sphere))
        {
            sphere.center = center(source);
            sphere.radius = radius(source);
            return;
        }

        vsg::t_box<T> newBox(source);
        for (unsigned int c = 0; c < 8; ++c)
        {
            vsg::t_vec3<T> v = corner(source, c) - sphere.center; // The direction from the centre to the corner.
            normalizeInPlace(v);
            v *= -sphere.radius; // The opposite direction, at the distance of the radius.
            v += sphere.center; // Back to an absolute position.
            newBox.add(v);
        }

        sphere.center = center(newBox);
        sphere.radius = radius(newBox);
    }

    // --------------------------------------------------------------------------------------------------------------- planes

    // OSG's three-point constructor. VSG has none. Points too close to a line (a cross product at most 1e-6 long) give a zero
    // normal and d of 0, not NaN.
    template <class T>
    inline vsg::dplane makePlane(const vsg::t_vec3<T>& p1, const vsg::t_vec3<T>& p2, const vsg::t_vec3<T>& p3)
    {
        const vsg::dvec3 v1(p1);
        const vsg::dvec3 v2(p2);
        const vsg::dvec3 v3(p3);
        vsg::dvec3 norm = vsg::cross(v2 - v1, v3 - v2);
        const double length = vsg::length(norm);
        if (length > 1e-6)
            norm = vsg::dvec3(norm[0] / length, norm[1] / length, norm[2] / length);
        else
            norm = vsg::dvec3(0.0, 0.0, 0.0);
        return vsg::dplane(norm[0], norm[1], norm[2], -vsg::dot(v1, norm));
    }

    // The order is OSG's, normal first; VSG's own constructor has them the other way round (plane.h). The normal is not
    // normalized.
    template <class T>
    inline vsg::dplane makePlane(const vsg::t_vec3<T>& normal, const vsg::t_vec3<T>& point)
    {
        const vsg::dvec3 norm(normal);
        const vsg::dvec3 p(point);
        return vsg::dplane(norm[0], norm[1], norm[2], -norm[0] * p[0] - norm[1] * p[1] - norm[2] * p[2]);
    }

    // In place, as in OSG; all four are negated.
    template <class T>
    inline void flip(vsg::t_plane<T>& plane)
    {
        plane[0] = -plane[0];
        plane[1] = -plane[1];
        plane[2] = -plane[2];
        plane[3] = -plane[3];
    }

    // In place, as in OSG; all four are scaled, so d is too. A zero normal gives NaN, and infinity for a non-zero d.
    template <class T>
    inline void makeUnitLength(vsg::t_plane<T>& plane)
    {
        const T invLength = static_cast<T>(1.0 / std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]));
        plane[0] *= invLength;
        plane[1] *= invLength;
        plane[2] *= invLength;
        plane[3] *= invLength;
    }
}

#endif
