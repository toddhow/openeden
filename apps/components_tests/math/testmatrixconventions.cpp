#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <osg/Math>
#include <osg/Matrix>
#include <osg/Matrixd>
#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <vsg/maths/mat4.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <cmath>
#include <type_traits>

// How to convert osg::Matrixf/d to VSG. Every test names the mapping: "OSG X equals VSG Y".
//
// THE ONE RULE, from which most tests below follow. OSG and VSG store a matrix with the SAME 16 numbers in the SAME order
// and index them the same way, m(i, j). A translation sits in element (3, 0..2) in both. Only the reading differs: OSG
// calls i the row and multiplies row vectors, VSG calls i the column and multiplies column vectors. So:
//   - numbers carry over unchanged: never transpose when converting a stored matrix or a constructor call;
//   - products flip: OSG A * B equals VSG B * A;
//   - vector products flip: OSG v * M equals VSG M * v, and OSG M * v equals VSG v * M.
//
// Sources: extern/osg/osg/Matrixf, Matrixd, Matrix; components/vsgadapters/osg/Matrix_implementation.cpp,
// MatrixDecomposition.cpp; VSG v1.1.16 vsg/maths/mat4.h, transform.h, src/vsg/maths/maths_transform.cpp.
//
// HELPERS components/misc/vsgmath.hpp MUST PROVIDE (the implementer writes them; these tests do not use them):
//  - getTrans(m): dvec3 for either matrix type; setTrans(m, v). OSG always returns Vec3d (Matrixf:348).
//  - getScale(m): OSG's form, which reads across the matrix (Matrixf:350-355). NOT length(m[i].xyz).
//  - transform3x3(v, m) and transform3x3(m, v) (Matrixf:769-793).
//  - makeRotate(angle, axis) returning a matrix, normalizing the axis (Matrix_implementation.cpp:394-409).
//  - preMultTranslate/postMultTranslate and the Scale and Rotate forms, or call sites written as the products below.
//  - invert4x3(m) for components/lua/utilpackage.cpp:327, keeping OSG's branch on m(3, 3) (Matrix_implementation.cpp:656).
//    Neither vsg::inverse nor vsg::inverse_4x3 matches it on every input.
//  - decompose(m, t, r, s, so) with OSG's polar decomposition if negative scale must survive (MatrixDecomposition.cpp:799).
//  - getLookAt(m, eye, center, up, distance) (Matrix_implementation.cpp:1069-1085).
//  - makeOpenGlPerspective and makeOpenGlOrtho, only if a caller remains that needs OSG's clip space.
//
// Compile-time differences: VSG matrices have no *=, + or scalar * or /. Mixed-precision vector products do not compile.
// m.invert(n) returned bool; vsg::inverse returns a matrix and no status.
namespace
{
    using namespace MathConventions;

    // Sixteen distinct values, including a non-trivial last column, so that any swap of indices shows.
    osg::Matrixf osgGeneral()
    {
        return osg::Matrixf(0.5f, 1.5f, -2.f, 0.25f, 3.f, -0.75f, 4.5f, -0.125f, -1.f, 6.f, 0.3f, 0.1f, 7.f, -8.f, 9.f, 2.f);
    }

    vsg::mat4 vsgGeneral()
    {
        return vsg::mat4(0.5f, 1.5f, -2.f, 0.25f, 3.f, -0.75f, 4.5f, -0.125f, -1.f, 6.f, 0.3f, 0.1f, 7.f, -8.f, 9.f, 2.f);
    }

    osg::Matrixf osgOther()
    {
        return osg::Matrixf(1.1f, -0.2f, 0.3f, 0.f, 0.4f, 0.9f, -0.6f, 0.f, 0.7f, 0.8f, 1.3f, 0.f, -5.f, 2.5f, 1.25f, 1.f);
    }

    vsg::mat4 vsgOther()
    {
        return vsg::mat4(1.1f, -0.2f, 0.3f, 0.f, 0.4f, 0.9f, -0.6f, 0.f, 0.7f, 0.8f, 1.3f, 0.f, -5.f, 2.5f, 1.25f, 1.f);
    }

    const osg::Vec3f oV(1.5f, -2.25f, 3.75f);
    const vsg::vec3 vV(1.5f, -2.25f, 3.75f);
    const osg::Vec3d odV(1.5, -2.25, 3.75);
    const vsg::dvec3 vdV(1.5, -2.25, 3.75);

    const osg::Quat oQ(0.7, osg::Vec3d(1.0, 2.0, 3.0));
    const vsg::dquat vQ(0.7, vsg::dvec3(1.0, 2.0, 3.0));

    // TRAP (type): osg::Matrix is Matrixd, double (Matrix:27; Config leaves OSG_USE_FLOAT_MATRIX undefined). Converting
    // the unsized name to vsg::mat4 would lose precision. OSG Matrix equals VSG dmat4.
    TEST(MatrixConventions, unsizedMatrixIsDouble)
    {
        static_assert(std::is_same_v<osg::Matrix, osg::Matrixd>);
    }

    // OSG Matrixf() equals VSG mat4(): the identity, not zeros. Matrixf:33, mat4.h:32.
    // OSG m.isIdentity() equals VSG m == mat4().
    TEST(MatrixConventions, defaultIsIdentity)
    {
        EXPECT_TRUE(matExact(osg::Matrixf(), vsg::mat4()));
        EXPECT_TRUE(matExact(osg::Matrixd(), vsg::dmat4()));
        EXPECT_TRUE(osg::Matrixf().isIdentity());
        EXPECT_TRUE(vsg::mat4() == vsg::mat4());
    }

    // OSG Matrixf(a00, ..., a33) equals VSG mat4(a00, ..., a33) with the arguments IN THE SAME ORDER, although OSG's
    // parameters are named as rows (Matrixf:40) and VSG's are commented as columns (mat4.h:44). Do not transpose.
    TEST(MatrixConventions, constructorArgumentsKeepTheirOrder)
    {
        EXPECT_TRUE(matExact(osgGeneral(), vsgGeneral()));
    }

    // OSG m(i, j) equals VSG m(i, j) and m[i][j]. Do not swap the indices. Matrixf:53, mat4.h:82, 85.
    TEST(MatrixConventions, elementAccessKeepsItsIndices)
    {
        const osg::Matrixf o = osgGeneral();
        const vsg::mat4 v = vsgGeneral();
        EXPECT_EQ(o(3, 0), v[3][0]);
        EXPECT_EQ(o(0, 3), v[0][3]);
        EXPECT_EQ(o(1, 2), v(1, 2));
        EXPECT_EQ(o(3, 0), 7.f);
    }

    // OSG Matrixf::translate(t) equals VSG translate(t), in the same storage. Matrix_implementation.cpp:374, transform.h:71.
    // m.makeTranslate(t) replaces the whole matrix: m = translate(t).
    TEST(MatrixConventions, translate)
    {
        EXPECT_TRUE(matExact(osg::Matrixf::translate(oV), vsg::translate(vV)));
        EXPECT_TRUE(matExact(osg::Matrixd::translate(odV), vsg::translate(vdV)));
        EXPECT_TRUE(matExact(osg::Matrixf::translate(1.f, 2.f, 3.f), vsg::translate(1.f, 2.f, 3.f)));

        osg::Matrixf o = osgGeneral();
        o.makeTranslate(oV);
        EXPECT_TRUE(matExact(o, vsg::translate(vV)));
    }

    // OSG Matrixf::scale(s) equals VSG scale(s). m.makeScale(s) replaces the whole matrix.
    // Matrix_implementation.cpp:351, transform.h:98.
    TEST(MatrixConventions, scale)
    {
        EXPECT_TRUE(matExact(osg::Matrixf::scale(oV), vsg::scale(vV)));
        EXPECT_TRUE(matExact(osg::Matrixd::scale(odV), vsg::scale(vdV)));
        EXPECT_TRUE(matExact(osg::Matrixf::scale(2.f, 3.f, 4.f), vsg::scale(2.f, 3.f, 4.f)));

        osg::Matrixf o = osgGeneral();
        o.makeScale(oV);
        EXPECT_TRUE(matExact(o, vsg::scale(vV)));
    }

    // OSG Matrixd::rotate(angle, unitAxis) equals VSG rotate(angle, unitAxis) to tolerance. OSG goes through a quat
    // (Matrix_implementation.cpp:402-409); VSG uses sin and cos of the full angle (transform.h:45-56).
    TEST(MatrixConventions, rotateAboutUnitAxis)
    {
        EXPECT_TRUE(matNear(osg::Matrixd::rotate(0.7, osg::Vec3d(0.0, 0.0, 1.0)), vsg::rotate(0.7, vsg::dvec3(0.0, 0.0, 1.0))));
        EXPECT_TRUE(matNear(osg::Matrixd::rotate(-1.9, osg::Vec3d(1.0, 0.0, 0.0)), vsg::rotate(-1.9, vsg::dvec3(1.0, 0.0, 0.0))));
    }

    // TRAP: OSG normalizes the axis; VSG assumes it is unit length and returns a wrong, non-rotation matrix otherwise.
    // OSG Matrixd::rotate(angle, anyAxis) equals VSG rotate(dquat(angle, anyAxis)), not rotate(angle, anyAxis).
    TEST(MatrixConventions, rotateAboutNonUnitAxis)
    {
        const osg::Matrixd o = osg::Matrixd::rotate(0.7, osg::Vec3d(0.0, 0.0, 2.0));
        EXPECT_TRUE(matNear(o, vsg::rotate(vsg::dquat(0.7, vsg::dvec3(0.0, 0.0, 2.0)))));
        EXPECT_FALSE(matNear(o, vsg::rotate(0.7, vsg::dvec3(0.0, 0.0, 2.0))));
    }

    // TRAP: a zero axis gives OSG the identity (Quat.cpp:54-58) and VSG cos(angle) on the diagonal (transform.h:52-54).
    TEST(MatrixConventions, rotateAboutZeroAxis)
    {
        EXPECT_TRUE(matExact(osg::Matrixd::rotate(0.7, osg::Vec3d(0.0, 0.0, 0.0)), vsg::dmat4()));
        EXPECT_NEAR(vsg::rotate(0.7, vsg::dvec3(0.0, 0.0, 0.0))(0, 0), std::cos(0.7), 1e-12);
        EXPECT_TRUE(matExact(vsg::rotate(vsg::dquat(0.7, vsg::dvec3(0.0, 0.0, 0.0))), vsg::dmat4()));
    }

    // Rotation direction agrees: 90 degrees about Z takes +X to +Y in both, in the same storage.
    TEST(MatrixConventions, rotationDirection)
    {
        const osg::Matrixd o = osg::Matrixd::rotate(osg::PI_2, osg::Vec3d(0.0, 0.0, 1.0));
        EXPECT_TRUE(vecNear<3>(osg::Vec3d(1.0, 0.0, 0.0) * o, osg::Vec3d(0.0, 1.0, 0.0)));
        const vsg::dmat4 v = vsg::rotate(vsg::PI / 2.0, vsg::dvec3(0.0, 0.0, 1.0));
        EXPECT_TRUE(vecNear<3>(v * vsg::dvec3(1.0, 0.0, 0.0), vsg::dvec3(0.0, 1.0, 0.0)));
    }

    // TRAP, the main one. OSG A * B equals VSG B * A exactly. Same terms in the same order:
    // Matrix_implementation.cpp:33-35, 462-477 against mat4.h:161-167, 170-176.
    TEST(MatrixConventions, productOrderFlips)
    {
        // Near, not exact: GCC and MSVC round these products differently in the last bit.
        EXPECT_TRUE(matNear(osgGeneral() * osgOther(), vsgOther() * vsgGeneral()));
        EXPECT_TRUE(matNear(osgOther() * osgGeneral(), vsgGeneral() * vsgOther()));
    }

    // Keeping the operand order gives a different matrix.
    TEST(MatrixConventions, productInTheSameOrderIsWrong)
    {
        EXPECT_FALSE(matNear(osgGeneral() * osgOther(), vsgGeneral() * vsgOther()));
    }

    // The usual transform chain reverses as a whole.
    // OSG scale(s) * rotate(q) * translate(t) equals VSG translate(t) * rotate(q) * scale(s), to tolerance. The grouping
    // that keeps every term in place is translate(t) * (rotate(q) * scale(s)), because C++ groups from the left.
    TEST(MatrixConventions, transformChainReverses)
    {
        const osg::Vec3d oT(10.0, -20.0, 30.0);
        const osg::Vec3d oS(2.0, 3.0, 4.0);
        const vsg::dvec3 vT(10.0, -20.0, 30.0);
        const vsg::dvec3 vS(2.0, 3.0, 4.0);

        const osg::Matrixd o = osg::Matrixd::scale(oS) * osg::Matrixd::rotate(oQ) * osg::Matrixd::translate(oT);
        const vsg::dmat4 v = vsg::translate(vT) * vsg::rotate(vQ) * vsg::scale(vS);
        EXPECT_TRUE(matNear(o, v));

        // Applied to a point: scale first, then rotate, then translate, in both.
        const osg::Vec3d oPoint = odV * o;
        const vsg::dvec3 vPoint = v * vdV;
        EXPECT_TRUE(vecNear<3>(oPoint, vPoint));
        EXPECT_TRUE(vecNear<3>(oPoint, (vQ * (vS * vdV)) + vT));
    }

    // OSG m.preMult(n), which is m = n * m, equals VSG m = m * n. Matrix_implementation.cpp:480.
    TEST(MatrixConventions, preMultMatrix)
    {
        osg::Matrixf o = osgGeneral();
        o.preMult(osgOther());
        EXPECT_TRUE(matExact(o, vsgGeneral() * vsgOther()));
    }

    // OSG m.postMult(n), which is m = m * n, equals VSG m = n * m. Matrix_implementation.cpp:501.
    TEST(MatrixConventions, postMultMatrix)
    {
        osg::Matrixf o = osgGeneral();
        o.postMult(osgOther());
        EXPECT_TRUE(matNear(o, vsgOther() * vsgGeneral()));
    }

    // TRAP: OSG m *= n is postMult (Matrixf:393). VSG has no *=, so it becomes m = n * m, with n on the LEFT.
    // Used at components/nifjolt/joltnifloader.cpp:370.
    TEST(MatrixConventions, compoundMultiply)
    {
        osg::Matrixf o = osgGeneral();
        o *= osgOther();
        EXPECT_TRUE(matNear(o, vsgOther() * vsgGeneral()));
    }

    // TRAP: OSG v * M equals VSG M * v, exactly, including the divide by the projective term.
    // Matrixf:725-731, 898 against mat4.h:227-233.
    TEST(MatrixConventions, rowVectorTimesMatrixIsVsgMatrixTimesVector)
    {
        const osg::Matrixf o = osgGeneral();
        const vsg::mat4 v = vsgGeneral();
        EXPECT_TRUE(vecExact<3>(oV * o, v * vV));
        EXPECT_TRUE(vecExact<3>(o.preMult(oV), v * vV));
    }

    // TRAP: OSG M * v equals VSG v * M, exactly. Matrixf:710-716, 915 against mat4.h:236-242.
    TEST(MatrixConventions, matrixTimesVectorIsVsgVectorTimesMatrix)
    {
        const osg::Matrixf o = osgGeneral();
        const vsg::mat4 v = vsgGeneral();
        EXPECT_TRUE(vecExact<3>(o * oV, vV * v));
        EXPECT_TRUE(vecExact<3>(o.postMult(oV), vV * v));
    }

    // The same for double matrices with double vectors. Matrixd:600-640, 793-800.
    TEST(MatrixConventions, doubleVectorProducts)
    {
        const osg::Matrixd o(osgGeneral());
        const vsg::dmat4 v(vsgGeneral());
        EXPECT_TRUE(vecExact<3>(odV * o, v * vdV));
        EXPECT_TRUE(vecExact<3>(o * odV, vdV * v));
    }

    // OSG v4 * M equals VSG M * v4, OSG M * v4 equals VSG v4 * M. No divide for 4-vectors.
    // Matrixf:740-761, mat4.h:179-185, 204-210.
    TEST(MatrixConventions, vec4Products)
    {
        const osg::Matrixf o = osgGeneral();
        const vsg::mat4 v = vsgGeneral();
        const osg::Vec4f oU(1.5f, -2.25f, 3.75f, 0.5f);
        const vsg::vec4 vU(1.5f, -2.25f, 3.75f, 0.5f);
        EXPECT_TRUE(vecExact<4>(oU * o, v * vU));
        EXPECT_TRUE(vecExact<4>(o * oU, vU * v));
    }

    // TRAP: VSG also defines v * M, so keeping OSG's expression still compiles. It reads the translation as a projective
    // term and divides by it (mat4.h:236-242; guide, "vsgopenmw predates VSG 1.1").
    // VSG (1, 2, 3) * translate(10, 20, 30) is (1, 2, 3) / 141, while OSG v * translate gives (11, 22, 33).
    TEST(MatrixConventions, keepingTheOsgExpressionCompilesAndDividesByTheTranslation)
    {
        const osg::Vec3f oPoint(1.f, 2.f, 3.f);
        const vsg::vec3 vPoint(1.f, 2.f, 3.f);

        EXPECT_TRUE(vecExact<3>(oPoint * osg::Matrixf::translate(10.f, 20.f, 30.f), vsg::vec3(11.f, 22.f, 33.f)));
        EXPECT_TRUE(vecExact<3>(vsg::translate(10.f, 20.f, 30.f) * vPoint, vsg::vec3(11.f, 22.f, 33.f)));

        const vsg::vec3 wrong = vPoint * vsg::translate(10.f, 20.f, 30.f);
        EXPECT_TRUE(vecNear<3>(wrong, vsg::vec3(1.f / 141.f, 2.f / 141.f, 3.f / 141.f)));
    }

    // TRAP: OSG Vec3f * Matrixd computes in double and narrows the result (Matrixd:618-624). VSG has no mixed product, and
    // the tempting mat4(m) * v computes in float.
    // OSG v * Md equals VSG vec3(Md * dvec3(v)) exactly. Translation 16777217 is the first integer a float cannot hold.
    TEST(MatrixConventions, floatVectorTimesDoubleMatrix)
    {
        const osg::Matrixd o = osg::Matrixd::translate(16777217.0, 0.0, 0.0);
        const vsg::dmat4 v = vsg::translate(16777217.0, 0.0, 0.0);
        const osg::Vec3f oPoint(1.f, 0.f, 0.f);
        const vsg::vec3 vPoint(1.f, 0.f, 0.f);

        const osg::Vec3f oResult = oPoint * o;
        EXPECT_TRUE(vecExact<3>(oResult, vsg::vec3(v * vsg::dvec3(vPoint))));
        EXPECT_EQ(oResult.x(), 16777218.f);

        const vsg::vec3 narrowed = vsg::mat4(v) * vPoint;
        EXPECT_NE(oResult.x(), narrowed.x);
    }

    // OSG Vec3d * Matrixf computes in double from the float elements (Matrixf:732-738).
    // For a matrix with last column (0, 0, 0, 1), OSG v * Mf equals VSG dmat4(Mf) * v exactly.
    TEST(MatrixConventions, doubleVectorTimesFloatMatrix)
    {
        const osg::Matrixf o = osgOther();
        const vsg::dmat4 v(vsgOther());
        EXPECT_TRUE(vecExact<3>(odV * o, v * vdV));
    }

    // OSG m.getTrans() equals VSG m[3].xyz, widened: OSG returns Vec3d from both matrix types. Matrixf:348, vec4.h union.
    // TRAP: m[3] is the "column 3" of VSG's naming and the "row 3" of OSG's: the same numbers.
    TEST(MatrixConventions, getTrans)
    {
        static_assert(std::is_same_v<decltype(osg::Matrixf().getTrans()), osg::Vec3d>);
        const osg::Matrixf o = osgGeneral();
        const vsg::mat4 v = vsgGeneral();
        EXPECT_TRUE(vecExact<3>(o.getTrans(), vsg::dvec3(v[3].xyz)));
        EXPECT_TRUE(vecExact<3>(o.getTrans(), vsg::dvec3(7.0, -8.0, 9.0)));
    }

    // OSG m.setTrans(t) equals VSG m[3].xyz = t: only the three elements change. Matrix_implementation.cpp:320.
    TEST(MatrixConventions, setTrans)
    {
        osg::Matrixf o = osgGeneral();
        vsg::mat4 v = vsgGeneral();
        o.setTrans(oV);
        v[3].xyz = vV;
        EXPECT_TRUE(matExact(o, v));
    }

    // TRAP: OSG getScale measures (m(0, i), m(1, i), m(2, i)) (Matrixf:350-355), which is NOT what vsg::decompose measures
    // (length(m[i].xyz), maths_transform.cpp:244-246). They agree without rotation or with uniform scale; with a rotation
    // and a non-uniform scale OSG's answer is not the scale at all, and that behaviour is what must be kept. Used for
    // physics at apps/openmw/mwphysics/object.cpp:248.
    // OSG m.getScale() equals VSG dvec3(length(dvec3(m[0][i], m[1][i], m[2][i]))...), and scale(2,3,4) then 90 degrees
    // about Z gives (3, 2, 4) rather than (2, 3, 4).
    TEST(MatrixConventions, getScaleReadsAcrossTheStorage)
    {
        const osg::Matrixd o
            = osg::Matrixd::scale(2.0, 3.0, 4.0) * osg::Matrixd::rotate(osg::PI_2, osg::Vec3d(0.0, 0.0, 1.0));
        const vsg::dmat4 v
            = vsg::rotate(vsg::dquat(vsg::PI / 2.0, vsg::dvec3(0.0, 0.0, 1.0))) * vsg::scale(2.0, 3.0, 4.0);

        EXPECT_TRUE(vecNear<3>(o.getScale(), vsg::dvec3(3.0, 2.0, 4.0)));

        const vsg::dvec3 correct(vsg::length(vsg::dvec3(v[0][0], v[1][0], v[2][0])),
            vsg::length(vsg::dvec3(v[0][1], v[1][1], v[2][1])), vsg::length(vsg::dvec3(v[0][2], v[1][2], v[2][2])));
        EXPECT_TRUE(vecNear<3>(o.getScale(), correct));

        const vsg::dvec3 naive(vsg::length(v[0].xyz), vsg::length(v[1].xyz), vsg::length(v[2].xyz));
        EXPECT_TRUE(vecNear<3>(naive, vsg::dvec3(2.0, 3.0, 4.0)));
        EXPECT_FALSE(vecNear<3>(o.getScale(), naive));
    }

    // OSG transform3x3(v, m) (v * M without translation) equals VSG (m * vec4(v, 0)).xyz exactly. Matrixf:769.
    TEST(MatrixConventions, transform3x3VectorFirst)
    {
        const osg::Matrixf o = osgGeneral();
        const vsg::mat4 v = vsgGeneral();
        const vsg::vec3 result = (v * vsg::vec4(vV, 0.f)).xyz;
        EXPECT_TRUE(vecExact<3>(osg::Matrixf::transform3x3(oV, o), result));
    }

    // TRAP: OSG transform3x3(m, v) (M * v) equals VSG (vec4(v, 0) * m).xyz exactly, so the flip applies here too. Matrixf:782.
    TEST(MatrixConventions, transform3x3MatrixFirst)
    {
        const osg::Matrixf o = osgGeneral();
        const vsg::mat4 v = vsgGeneral();
        const vsg::vec3 result = (vsg::vec4(vV, 0.f) * v).xyz;
        EXPECT_TRUE(vecExact<3>(osg::Matrixf::transform3x3(o, oV), result));
    }

    // OSG m.preMultTranslate(t) equals VSG m = m * translate(t), to tolerance: OSG adds the terms in a different order
    // (Matrixf:809-821). Hot-path call sites may want the helper's in-place form to avoid the full product.
    TEST(MatrixConventions, preMultTranslate)
    {
        osg::Matrixf o = osgOther();
        o.preMultTranslate(oV);
        EXPECT_TRUE(matNear(o, vsgOther() * vsg::translate(vV)));
    }

    // OSG m.postMultTranslate(t) equals VSG m = translate(t) * m, to tolerance. Matrixf:837-849.
    TEST(MatrixConventions, postMultTranslate)
    {
        osg::Matrixf o = osgOther();
        o.postMultTranslate(oV);
        EXPECT_TRUE(matNear(o, vsg::translate(vV) * vsgOther()));
    }

    // OSG m.preMultScale(s) equals VSG m = m * scale(s); postMultScale equals scale(s) * m. Matrixf:858, 872.
    TEST(MatrixConventions, preAndPostMultScale)
    {
        osg::Matrixf pre = osgGeneral();
        pre.preMultScale(oV);
        EXPECT_TRUE(matNear(pre, vsgGeneral() * vsg::scale(vV)));

        osg::Matrixf post = osgGeneral();
        post.postMultScale(oV);
        EXPECT_TRUE(matNear(post, vsg::scale(vV) * vsgGeneral()));
    }

    // OSG m.preMultRotate(q) equals VSG m = m * rotate(q); postMultRotate equals rotate(q) * m. Matrixf:880-896.
    TEST(MatrixConventions, preAndPostMultRotate)
    {
        const osg::Matrixd base(osgOther());
        const vsg::dmat4 vBase(vsgOther());

        osg::Matrixd pre = base;
        pre.preMultRotate(oQ);
        EXPECT_TRUE(matNear(pre, vBase * vsg::rotate(vQ)));

        osg::Matrixd post = base;
        post.postMultRotate(oQ);
        EXPECT_TRUE(matNear(post, vsg::rotate(vQ) * vBase));
    }

    // OSG t.transpose(m) equals VSG t = transpose(m), exactly. Matrix_implementation.cpp:748, transform.h:126.
    TEST(MatrixConventions, transpose)
    {
        osg::Matrixf o;
        EXPECT_TRUE(o.transpose(osgGeneral()));
        EXPECT_TRUE(matExact(o, vsg::transpose(vsgGeneral())));
    }

    // OSG Matrixf::inverse(m) equals VSG inverse(m) in the same storage, to tolerance. No flip is needed: the transposed
    // reading of an inverse is the inverse of the transposed reading. Both pick 4x3 by the same test: Matrixf:235,
    // maths_transform.cpp:172.
    TEST(MatrixConventions, inverseOfAffineFloat)
    {
        const osg::Matrixf o = osg::Matrixf::scale(2.f, 2.f, 2.f) * osg::Matrixf::translate(1.f, 2.f, 3.f);
        const vsg::mat4 v = vsg::translate(1.f, 2.f, 3.f) * vsg::scale(2.f, 2.f, 2.f);
        ASSERT_TRUE(matExact(o, v));
        EXPECT_TRUE(matNear(osg::Matrixf::inverse(o), vsg::inverse(v)));
    }

    TEST(MatrixConventions, inverseOfAffineDouble)
    {
        const osg::Matrixd o(osgOther());
        const vsg::dmat4 v(vsgOther());
        EXPECT_TRUE(matNear(osg::Matrixd::inverse(o), vsg::inverse(v)));
    }

    // OSG uses Gauss-Jordan (Matrix_implementation.cpp:794) and VSG cofactors (maths_transform.cpp:112).
    TEST(MatrixConventions, inverseOfGeneralMatrix)
    {
        const osg::Matrixd o(osgGeneral());
        const vsg::dmat4 v(vsgGeneral());
        EXPECT_TRUE(matNear(osg::Matrixd::inverse(o), vsg::inverse(v)));
    }

    // OSG t.invert(m) writes through the object and returns a status. OSG t.invert(m) equals VSG t = inverse(m).
    TEST(MatrixConventions, invertIntoAnotherMatrix)
    {
        osg::Matrixd o;
        const osg::Matrixd input(osgOther());
        EXPECT_TRUE(o.invert(input));
        EXPECT_TRUE(matNear(o, vsg::inverse(vsg::dmat4(vsgOther()))));
    }

    // TRAP: a singular matrix. OSG's 4x3 inverse reports success and fills the result with Inf and NaN through
    // one_over_det (Matrix_implementation.cpp:630-638); VSG returns NaN on the diagonal and 0 elsewhere
    // (maths_transform.cpp:81, mat4.h:38). The Lua binding at components/lua/utilpackage.cpp:327 tests OSG's return value,
    // which is always true on this path, so VSG offers nothing to replace that test with and its numbers differ.
    TEST(MatrixConventions, inverseOfSingularMatrix)
    {
        const osg::Matrixf input = osg::Matrixf::scale(0.f, 1.f, 1.f);
        osg::Matrixf o;
        EXPECT_TRUE(o.invert_4x3(input));
        EXPECT_TRUE(std::isinf(o(0, 0)));
        EXPECT_TRUE(std::isnan(o(0, 1)));

        const vsg::mat4 v = vsg::inverse(vsg::scale(0.f, 1.f, 1.f));
        EXPECT_TRUE(std::isnan(v[0][0]));
        EXPECT_EQ(v[0][1], 0.f);
        EXPECT_TRUE(std::isnan(v[3][3]));
    }

    // TRAP: OSG's invert_4x3 is not VSG's inverse_4x3. When m(3, 3) differs from 1, OSG still computes the full inverse
    // (Matrix_implementation.cpp:656); VSG's assumes 1 (maths_transform.cpp:105-108). Here OSG matches inverse, not
    // inverse_4x3. components/lua/utilpackage.cpp:327 calls invert_4x3 directly.
    TEST(MatrixConventions, invert4x3WithHomogeneousScale)
    {
        const osg::Matrixf input(1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 2.f);
        const vsg::mat4 vInput(1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 2.f);

        osg::Matrixf o;
        o.invert_4x3(input);
        EXPECT_EQ(o(3, 3), 0.5f);
        EXPECT_TRUE(matNear(o, vsg::inverse(vInput)));
        EXPECT_FALSE(matNear(o, vsg::inverse_4x3(vInput)));
    }

    // TRAP, the other side. When m(3, 3) is 1 but the rest of OSG's last column is not 0, OSG's invert_4x3 ignores that
    // column, matching VSG's inverse_4x3 and NOT vsg::inverse, which sees the column and switches to the full inverse
    // (maths_transform.cpp:172-179). So no single VSG call matches OSG's invert_4x3 on every input.
    TEST(MatrixConventions, invert4x3IgnoresTheLastColumnWhenWIsOne)
    {
        const osg::Matrixf input(1.f, 0.f, 0.f, 0.5f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f);
        const vsg::mat4 vInput(1.f, 0.f, 0.f, 0.5f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f);

        osg::Matrixf o;
        o.invert_4x3(input);
        EXPECT_TRUE(matNear(o, vsg::inverse_4x3(vInput)));
        EXPECT_FALSE(matNear(o, vsg::inverse(vInput)));
    }

    // OSG m.decompose(t, r, s, so) equals VSG decompose(m, t, r, s) for translate, rotate and positive scale, with so the
    // identity. ARGUMENT ORDER differs by the extra so. A quat and its negative are the same rotation, so r is compared by
    // the dot product. MatrixDecomposition.cpp:799, maths_transform.cpp:238.
    TEST(MatrixConventions, decomposeOfTranslateRotateScale)
    {
        const osg::Vec3d oT(10.0, -20.0, 30.0);
        const vsg::dvec3 vT(10.0, -20.0, 30.0);

        const osg::Matrixd o
            = osg::Matrixd::scale(2.0, 3.0, 4.0) * osg::Matrixd::rotate(oQ) * osg::Matrixd::translate(oT);
        const vsg::dmat4 v = vsg::translate(vT) * vsg::rotate(vQ) * vsg::scale(2.0, 3.0, 4.0);

        osg::Vec3d oTranslation;
        osg::Vec3d oScale;
        osg::Quat oRotation;
        osg::Quat oScaleOrientation;
        o.decompose(oTranslation, oRotation, oScale, oScaleOrientation);

        vsg::dvec3 vTranslation;
        vsg::dvec3 vScale;
        vsg::dquat vRotation;
        ASSERT_TRUE(vsg::decompose(v, vTranslation, vRotation, vScale));

        EXPECT_TRUE(vecNear<3>(oTranslation, vTranslation));
        EXPECT_TRUE(vecNear<3>(oScale, vScale));
        const double rotationDot = oRotation.x() * vRotation.x + oRotation.y() * vRotation.y
            + oRotation.z() * vRotation.z + oRotation.w() * vRotation.w;
        EXPECT_NEAR(std::abs(rotationDot), 1.0, kRelativeTolerance);
    }

    // TRAP: negative scale. OSG keeps it: a mirror comes back as negative scale plus a rotation
    // (MatrixDecomposition.cpp:829-833) and the parts rebuild the input. VSG returns positive scale, a non-unit quat, and
    // true (maths_transform.cpp:244-265), and its parts rebuild the identity: the mirror is lost.
    // Callers: components/lua/shapes/box.cpp:15, components/lua/utilpackage.cpp:302, apps/openmw/mwrender/bonepose.cpp:59,
    // apps/openmw/mwrender/actorikcontroller.cpp:196.
    TEST(MatrixConventions, decomposeOfNegativeScale)
    {
        const osg::Matrixd o = osg::Matrixd::scale(-1.0, 1.0, 1.0);

        osg::Vec3d oTranslation;
        osg::Vec3d oScale;
        osg::Quat oRotation;
        osg::Quat oScaleOrientation;
        o.decompose(oTranslation, oRotation, oScale, oScaleOrientation);
        EXPECT_TRUE(vecNear<3>(oScale, vsg::dvec3(-1.0, -1.0, -1.0)));

        const osg::Matrixd rebuilt = osg::Matrixd::rotate(oScaleOrientation.inverse()) * osg::Matrixd::scale(oScale)
            * osg::Matrixd::rotate(oScaleOrientation) * osg::Matrixd::rotate(oRotation)
            * osg::Matrixd::translate(oTranslation);
        EXPECT_TRUE(matNear(rebuilt, o));

        vsg::dvec3 vTranslation;
        vsg::dvec3 vScale;
        vsg::dquat vRotation;
        ASSERT_TRUE(vsg::decompose(vsg::scale(-1.0, 1.0, 1.0), vTranslation, vRotation, vScale));
        EXPECT_TRUE(vecNear<3>(vScale, vsg::dvec3(1.0, 1.0, 1.0)));
        EXPECT_NEAR(vsg::length(vRotation), std::sqrt(0.5), 1e-12);

        const vsg::dmat4 vRebuilt = vsg::translate(vTranslation) * vsg::rotate(vRotation) * vsg::scale(vScale);
        EXPECT_TRUE(matNear(vRebuilt, vsg::dmat4()));
    }

    // TRAP: zero scale. VSG returns false and leaves the rotation as it was (maths_transform.cpp:250). The callers above
    // ignore OSG's void return, so they must handle this when converted.
    TEST(MatrixConventions, decomposeOfZeroScaleFailsInVsg)
    {
        vsg::dvec3 translation;
        vsg::dvec3 scale;
        vsg::dquat rotation(0.1, 0.2, 0.3, 0.4);
        EXPECT_FALSE(vsg::decompose(vsg::scale(0.0, 1.0, 1.0), translation, rotation, scale));
        EXPECT_TRUE(vecExact<4>(rotation, vsg::dquat(0.1, 0.2, 0.3, 0.4)));
    }

    // TRAP: fovy is in DEGREES in OSG (Matrix_implementation.cpp:984) and in RADIANS in VSG (transform.h:144), and Y is
    // flipped for Vulkan. Same aspect handling.
    // OSG perspective(deg, a, n, f)(0, 0) equals VSG perspective(radians(deg), a, n, f)[0][0], and (1, 1) is negated.
    TEST(MatrixConventions, perspectiveUnitsAndYFlip)
    {
        const osg::Matrixd o = osg::Matrixd::perspective(60.0, 1.5, 1.0, 1000.0);
        const vsg::dmat4 v = vsg::perspective(vsg::radians(60.0), 1.5, 1.0, 1000.0);
        EXPECT_NEAR(o(0, 0), v[0][0], 1e-12);
        EXPECT_NEAR(o(1, 1), -v[1][1], 1e-12);
        EXPECT_EQ(o(2, 3), v[2][3]);

        const vsg::dmat4 degreesPassedAsIs = vsg::perspective(60.0, 1.5, 1.0, 1000.0);
        EXPECT_GT(std::abs(o(0, 0) - degreesPassedAsIs[0][0]), 0.01);
    }

    // TRAP: clip space differs, so a projection is NOT a mechanical conversion. OSG maps near to -1 and far to +1
    // (Matrix_implementation.cpp:926-931). VSG reverses depth: near to 1 and far to 0 (transform.h:146-149).
    TEST(MatrixConventions, perspectiveDepthRange)
    {
        const osg::Matrixd o = osg::Matrixd::perspective(60.0, 1.5, 1.0, 1000.0);
        const vsg::dmat4 v = vsg::perspective(vsg::radians(60.0), 1.5, 1.0, 1000.0);

        EXPECT_NEAR((osg::Vec3d(0.0, 0.0, -1.0) * o).z(), -1.0, 1e-9);
        EXPECT_NEAR((osg::Vec3d(0.0, 0.0, -1000.0) * o).z(), 1.0, 1e-9);
        EXPECT_NEAR((v * vsg::dvec3(0.0, 0.0, -1.0)).z, 1.0, 1e-9);
        EXPECT_NEAR((v * vsg::dvec3(0.0, 0.0, -1000.0)).z, 0.0, 1e-9);
    }

    // TRAP: the same for orthographic. OSG ortho(l, r, b, t, n, f) maps near to -1 and far to +1, Y up
    // (Matrix_implementation.cpp:870-876). VSG orthographic(l, r, b, t, n, f) maps near to 1, far to 0, Y flipped
    // (transform.h:172-175). The X row matches.
    TEST(MatrixConventions, orthographicDepthRangeAndYFlip)
    {
        const osg::Matrixd o = osg::Matrixd::ortho(-2.0, 2.0, -1.0, 3.0, 1.0, 100.0);
        const vsg::dmat4 v = vsg::orthographic(-2.0, 2.0, -1.0, 3.0, 1.0, 100.0);

        EXPECT_EQ(o(0, 0), v[0][0]);
        EXPECT_EQ(o(3, 0), v[3][0]);
        EXPECT_EQ(o(1, 1), -v[1][1]);
        EXPECT_EQ(o(3, 1), -v[3][1]);

        EXPECT_NEAR((osg::Vec3d(0.0, 0.0, -1.0) * o).z(), -1.0, 1e-12);
        EXPECT_NEAR((osg::Vec3d(0.0, 0.0, -100.0) * o).z(), 1.0, 1e-12);
        EXPECT_NEAR((v * vsg::dvec3(0.0, 0.0, -1.0)).z, 1.0, 1e-12);
        EXPECT_NEAR((v * vsg::dvec3(0.0, 0.0, -100.0)).z, 0.0, 1e-12);
    }

    // OSG Matrixd::lookAt(eye, center, up) equals VSG lookAt(eye, center, up), to tolerance, in the same storage. Unlike
    // projections, this view convention is shared. Matrix_implementation.cpp:1037-1049, transform.h:179-196.
    TEST(MatrixConventions, lookAt)
    {
        const osg::Matrixd o = osg::Matrixd::lookAt(
            osg::Vec3d(1.0, 2.0, 3.0), osg::Vec3d(4.0, 6.0, 3.0), osg::Vec3d(0.0, 0.0, 2.0));
        const vsg::dmat4 v = vsg::lookAt(
            vsg::dvec3(1.0, 2.0, 3.0), vsg::dvec3(4.0, 6.0, 3.0), vsg::dvec3(0.0, 0.0, 2.0));
        EXPECT_TRUE(matNear(o, v));
    }

    // Pins for the helper. OSG m.getLookAt(eye, center, up, d) equals, in VSG:
    //   eye = inverse(m) * dvec3(0, 0, 0)
    //   up = (dvec4(0, 1, 0, 0) * m).xyz
    //   center = eye + normalize((dvec4(0, 0, -1, 0) * m).xyz) * d
    // Note the v * m forms: OSG uses transform3x3(*this, v), which is its M * v. Matrix_implementation.cpp:1069-1085.
    TEST(MatrixConventions, getLookAt)
    {
        const osg::Matrixd o = osg::Matrixd::lookAt(
            osg::Vec3d(1.0, 2.0, 3.0), osg::Vec3d(4.0, 6.0, 3.0), osg::Vec3d(0.0, 0.0, 1.0));
        osg::Vec3d oEye;
        osg::Vec3d oCenter;
        osg::Vec3d oUp;
        o.getLookAt(oEye, oCenter, oUp, 2.0);

        const vsg::dmat4 v = vsg::lookAt(
            vsg::dvec3(1.0, 2.0, 3.0), vsg::dvec3(4.0, 6.0, 3.0), vsg::dvec3(0.0, 0.0, 1.0));
        const vsg::dvec3 eye = vsg::inverse(v) * vsg::dvec3(0.0, 0.0, 0.0);
        const vsg::dvec3 up = (vsg::dvec4(0.0, 1.0, 0.0, 0.0) * v).xyz;
        const vsg::dvec3 forward = (vsg::dvec4(0.0, 0.0, -1.0, 0.0) * v).xyz;
        const vsg::dvec3 center = eye + vsg::normalize(forward) * 2.0;

        EXPECT_TRUE(vecNear<3>(oEye, eye));
        EXPECT_TRUE(vecNear<3>(oUp, up));
        EXPECT_TRUE(vecNear<3>(oCenter, center));
        EXPECT_TRUE(vecNear<3>(oEye, vsg::dvec3(1.0, 2.0, 3.0)));
    }
}

#endif
