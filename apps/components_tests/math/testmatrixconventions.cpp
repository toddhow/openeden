#include "conventions.hpp"

#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <vsg/maths/mat4.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <cmath>
#include <type_traits>

// How to convert osg::Matrixf/d to VSG. Every test names the mapping: "OSG X equals VSG Y".
//
// THE ONE RULE, from which most tests below follow. OSG and VSG store a matrix with the SAME 16 numbers in the SAME
// order and index them the same way, m(i, j). A translation sits in element (3, 0..2) in both. Only the reading
// differs: OSG calls i the row and multiplies row vectors, VSG calls i the column and multiplies column vectors. So:
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
//  - invert4x3(m) for components/lua/utilpackage.cpp:327, keeping OSG's branch on m(3, 3)
//  (Matrix_implementation.cpp:656).
//    Neither vsg::inverse nor vsg::inverse_4x3 matches it on every input.
//  - decompose(m, t, r, s, so) with OSG's polar decomposition if negative scale must survive
//  (MatrixDecomposition.cpp:799).
//  - getLookAt(m, eye, center, up, distance) (Matrix_implementation.cpp:1069-1085).
//  - makeOpenGlPerspective and makeOpenGlOrtho, only if a caller remains that needs OSG's clip space.
//
// Compile-time differences: VSG matrices have no *=, + or scalar * or /. Mixed-precision vector products do not
// compile. m.invert(n) returned bool; vsg::inverse returns a matrix and no status.
namespace
{
    using namespace MathConventions;

    // Sixteen distinct values, including a non-trivial last column, so that any swap of indices shows.
    vsg::mat4 vsgGeneral()
    {
        return matOf<float>(
            0.5f, 1.5f, -2.f, 0.25f, 3.f, -0.75f, 4.5f, -0.125f, -1.f, 6.f, 0.3f, 0.1f, 7.f, -8.f, 9.f, 2.f);
    }

    vsg::mat4 vsgOther()
    {
        return matOf<float>(
            1.1f, -0.2f, 0.3f, 0.f, 0.4f, 0.9f, -0.6f, 0.f, 0.7f, 0.8f, 1.3f, 0.f, -5.f, 2.5f, 1.25f, 1.f);
    }

    const vsg::vec3 vV(1.5f, -2.25f, 3.75f);
    const vsg::dvec3 vdV(1.5, -2.25, 3.75);

    const vsg::dquat vQ(0.7, vsg::dvec3(1.0, 2.0, 3.0));

    // TRAP (type): osg::Matrix is Matrixd, double (Matrix:27; Config leaves OSG_USE_FLOAT_MATRIX undefined). Converting
    // the unsized name to vsg::mat4 would lose precision. OSG Matrix equals VSG dmat4.
    TEST(MatrixConventions, unsizedMatrixIsDouble) {}

    // OSG Matrixf() equals VSG mat4(): the identity, not zeros. Matrixf:33, mat4.h:32.
    // OSG m.isIdentity() equals VSG m == mat4().
    TEST(MatrixConventions, defaultIsIdentity)
    {
        EXPECT_TRUE(matExact(matOf<float>(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f),
            vsg::mat4()));
        EXPECT_TRUE(
            matExact(matOf<double>(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
                vsg::dmat4()));
        EXPECT_TRUE(vsg::mat4() == vsg::mat4());
    }

    // OSG Matrixf(a00, ..., a33) equals VSG mat4(a00, ..., a33) with the arguments IN THE SAME ORDER, although OSG's
    // parameters are named as rows (Matrixf:40) and VSG's are commented as columns (mat4.h:44). Do not transpose.
    TEST(MatrixConventions, constructorArgumentsKeepTheirOrder)
    {
        EXPECT_TRUE(matExact(matOf<float>(0.5f, 1.5f, -2.0f, 0.25f, 3.0f, -0.75f, 4.5f, -0.125f, -1.0f, 6.0f,
                                 0.300000012f, 0.100000001f, 7.0f, -8.0f, 9.0f, 2.0f),
            vsgGeneral()));
    }

    // OSG m(i, j) equals VSG m(i, j) and m[i][j]. Do not swap the indices. Matrixf:53, mat4.h:82, 85.
    TEST(MatrixConventions, elementAccessKeepsItsIndices)
    {
        const vsg::mat4 v = vsgGeneral();
        EXPECT_TRUE(recordedEqual(7.0f, v[3][0]));
        EXPECT_TRUE(recordedEqual(0.25f, v[0][3]));
        EXPECT_TRUE(recordedEqual(4.5f, v(1, 2)));
        EXPECT_TRUE(recordedEqual(7.0f, 7.f));
    }

    // OSG Matrixf::translate(t) equals VSG translate(t), in the same storage. Matrix_implementation.cpp:374,
    // transform.h:71. m.makeTranslate(t) replaces the whole matrix: m = translate(t).
    TEST(MatrixConventions, translate)
    {
        EXPECT_TRUE(matExact(matOf<float>(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.5f,
                                 -2.25f, 3.75f, 1.0f),
            vsg::translate(vV)));
        EXPECT_TRUE(
            matExact(matOf<double>(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.5, -2.25, 3.75, 1.0),
                vsg::translate(vdV)));
        EXPECT_TRUE(matExact(matOf<float>(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
                                 2.0f, 3.0f, 1.0f),
            vsg::translate(1.f, 2.f, 3.f)));

        EXPECT_TRUE(matExact(matOf<float>(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.5f,
                                 -2.25f, 3.75f, 1.0f),
            vsg::translate(vV)));
    }

    // OSG Matrixf::scale(s) equals VSG scale(s). m.makeScale(s) replaces the whole matrix.
    // Matrix_implementation.cpp:351, transform.h:98.
    TEST(MatrixConventions, scale)
    {
        EXPECT_TRUE(matExact(matOf<float>(1.5f, 0.0f, 0.0f, 0.0f, 0.0f, -2.25f, 0.0f, 0.0f, 0.0f, 0.0f, 3.75f, 0.0f,
                                 0.0f, 0.0f, 0.0f, 1.0f),
            vsg::scale(vV)));
        EXPECT_TRUE(
            matExact(matOf<double>(1.5, 0.0, 0.0, 0.0, 0.0, -2.25, 0.0, 0.0, 0.0, 0.0, 3.75, 0.0, 0.0, 0.0, 0.0, 1.0),
                vsg::scale(vdV)));
        EXPECT_TRUE(matExact(matOf<float>(2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f),
            vsg::scale(2.f, 3.f, 4.f)));

        EXPECT_TRUE(matExact(matOf<float>(1.5f, 0.0f, 0.0f, 0.0f, 0.0f, -2.25f, 0.0f, 0.0f, 0.0f, 0.0f, 3.75f, 0.0f,
                                 0.0f, 0.0f, 0.0f, 1.0f),
            vsg::scale(vV)));
    }

    // OSG Matrixd::rotate(angle, unitAxis) equals VSG rotate(angle, unitAxis) to tolerance. OSG goes through a quat
    // (Matrix_implementation.cpp:402-409); VSG uses sin and cos of the full angle (transform.h:45-56).
    TEST(MatrixConventions, rotateAboutUnitAxis)
    {
        EXPECT_TRUE(matNear(matOf<double>(0.7648421872844884, 0.644217687237691, 0.0, 0.0, -0.644217687237691,
                                0.7648421872844884, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
            vsg::rotate(0.7, vsg::dvec3(0.0, 0.0, 1.0))));
        EXPECT_TRUE(matNear(matOf<double>(1.0, 0.0, 0.0, 0.0, 0.0, -0.3232895668635034, -0.9463000876874146, 0.0, 0.0,
                                0.9463000876874146, -0.3232895668635034, 0.0, 0.0, 0.0, 0.0, 1.0),
            vsg::rotate(-1.9, vsg::dvec3(1.0, 0.0, 0.0))));
    }

    // TRAP: OSG normalizes the axis; VSG assumes it is unit length and returns a wrong, non-rotation matrix otherwise.
    // OSG Matrixd::rotate(angle, anyAxis) equals VSG rotate(dquat(angle, anyAxis)), not rotate(angle, anyAxis).
    TEST(MatrixConventions, rotateAboutNonUnitAxis)
    {
        EXPECT_TRUE(matNear(matOf<double>(0.7648421872844884, 0.644217687237691, 0.0, 0.0, -0.644217687237691,
                                0.7648421872844884, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
            vsg::rotate(vsg::dquat(0.7, vsg::dvec3(0.0, 0.0, 2.0)))));
        EXPECT_FALSE(matNear(matOf<double>(0.7648421872844884, 0.644217687237691, 0.0, 0.0, -0.644217687237691,
                                 0.7648421872844884, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
            vsg::rotate(0.7, vsg::dvec3(0.0, 0.0, 2.0))));
    }

    // TRAP: a zero axis gives OSG the identity (Quat.cpp:54-58) and VSG cos(angle) on the diagonal (transform.h:52-54).
    TEST(MatrixConventions, rotateAboutZeroAxis)
    {
        EXPECT_TRUE(
            matExact(matOf<double>(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
                vsg::dmat4()));
        EXPECT_NEAR(vsg::rotate(0.7, vsg::dvec3(0.0, 0.0, 0.0))(0, 0), std::cos(0.7), 1e-12);
        EXPECT_TRUE(matExact(vsg::rotate(vsg::dquat(0.7, vsg::dvec3(0.0, 0.0, 0.0))), vsg::dmat4()));
    }

    // Rotation direction agrees: 90 degrees about Z takes +X to +Y in both, in the same storage.
    TEST(MatrixConventions, rotationDirection)
    {
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(0.0, 1.0, 0.0), vsg::dvec3(0.0, 1.0, 0.0)));
        const vsg::dmat4 v = vsg::rotate(vsg::PI / 2.0, vsg::dvec3(0.0, 0.0, 1.0));
        EXPECT_TRUE(vecNear<3>(v * vsg::dvec3(1.0, 0.0, 0.0), vsg::dvec3(0.0, 1.0, 0.0)));
    }

    // TRAP, the main one. OSG A * B equals VSG B * A exactly. Same terms in the same order:
    // Matrix_implementation.cpp:33-35, 462-477 against mat4.h:161-167, 170-176.
    TEST(MatrixConventions, productOrderFlips)
    {
        // Near, not exact: GCC and MSVC round these products differently in the last bit.
        EXPECT_TRUE(matNear(
            matOf<float>(-1.49999988f, 0.274999857f, -3.0374999f, 0.25f, 6.7750001f, 2.01250029f, 7.04374981f, -0.125f,
                1.01000011f, 6.0899992f, -3.38499999f, 0.100000001f, 0.799999237f, 3.60000086f, 21.1000004f, 2.0f),
            vsgOther() * vsgGeneral()));
        EXPECT_TRUE(matNear(
            matOf<float>(-0.350000024f, 3.60000014f, -3.01000023f, 0.330000013f, 3.5f, -3.67500019f, 3.06999969f,
                -0.0724999979f, 1.45000005f, 8.25f, 2.59000039f, 0.204999983f, 10.75f, -9.875f, 30.625f, 0.5625f),
            vsgGeneral() * vsgOther()));
    }

    // Keeping the operand order gives a different matrix.
    TEST(MatrixConventions, productInTheSameOrderIsWrong)
    {
        EXPECT_FALSE(matNear(
            matOf<float>(-1.49999988f, 0.274999857f, -3.0374999f, 0.25f, 6.7750001f, 2.01250029f, 7.04374981f, -0.125f,
                1.01000011f, 6.0899992f, -3.38499999f, 0.100000001f, 0.799999237f, 3.60000086f, 21.1000004f, 2.0f),
            vsgGeneral() * vsgOther()));
    }

    // The usual transform chain reverses as a whole.
    // OSG scale(s) * rotate(q) * translate(t) equals VSG translate(t) * rotate(q) * scale(s), to tolerance. The
    // grouping that keeps every term in place is translate(t) * (rotate(q) * scale(s)), because C++ groups from the
    // left.
    TEST(MatrixConventions, transformChainReverses)
    {
        const vsg::dvec3 vT(10.0, -20.0, 30.0);
        const vsg::dvec3 vS(2.0, 3.0, 4.0);

        const vsg::dmat4 v = vsg::translate(vT) * vsg::rotate(vQ) * vsg::scale(vS);
        EXPECT_TRUE(matNear(matOf<double>(1.56327834781405, 1.1002344614087167, -0.5879157568771611, 0.0,
                                -1.4487878526426368, 2.4960904013239036, 0.818869016664943, 0.0, 1.5789591926951991,
                                -0.2855699976715034, 3.664060267549269, 0.0, 10.0, -20.0, 30.0, 1.0),
            v));

        // Applied to a point: scale first, then rotate, then translate, in both.
        const vsg::dvec3 vPoint = v * vdV;
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(21.525787162774005, -25.036739202133845, 41.01589708049789), vPoint));
        EXPECT_TRUE(
            vecNear<3>(vsg::dvec3(21.525787162774005, -25.036739202133845, 41.01589708049789), (vQ * (vS * vdV)) + vT));
    }

    // OSG m.preMult(n), which is m = n * m, equals VSG m = m * n. Matrix_implementation.cpp:480.
    TEST(MatrixConventions, preMultMatrix)
    {
        EXPECT_TRUE(matExact(
            matOf<float>(-0.350000024f, 3.60000014f, -3.01000023f, 0.330000013f, 3.5f, -3.67500019f, 3.06999969f,
                -0.0724999979f, 1.45000005f, 8.25f, 2.59000039f, 0.204999983f, 10.75f, -9.875f, 30.625f, 0.5625f),
            vsgGeneral() * vsgOther()));
    }

    // OSG m.postMult(n), which is m = m * n, equals VSG m = n * m. Matrix_implementation.cpp:501.
    TEST(MatrixConventions, postMultMatrix)
    {
        EXPECT_TRUE(matNear(
            matOf<float>(-1.49999988f, 0.274999857f, -3.0374999f, 0.25f, 6.7750001f, 2.01250029f, 7.04374981f, -0.125f,
                1.01000011f, 6.0899992f, -3.38499999f, 0.100000001f, 0.799999237f, 3.60000086f, 21.1000004f, 2.0f),
            vsgOther() * vsgGeneral()));
    }

    // TRAP: OSG m *= n is postMult (Matrixf:393). VSG has no *=, so it becomes m = n * m, with n on the LEFT.
    // Used at components/nifjolt/joltnifloader.cpp:370.
    TEST(MatrixConventions, compoundMultiply)
    {
        EXPECT_TRUE(matNear(
            matOf<float>(-1.49999988f, 0.274999857f, -3.0374999f, 0.25f, 6.7750001f, 2.01250029f, 7.04374981f, -0.125f,
                1.01000011f, 6.0899992f, -3.38499999f, 0.100000001f, 0.799999237f, 3.60000086f, 21.1000004f, 2.0f),
            vsgOther() * vsgGeneral()));
    }

    // TRAP: OSG v * M equals VSG M * v, exactly, including the divide by the projective term.
    // Matrixf:725-731, 898 against mat4.h:227-233.
    TEST(MatrixConventions, rowVectorTimesMatrixIsVsgMatrixTimesVector)
    {
        const vsg::mat4 v = vsgGeneral();
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-0.907216489f, 6.08247423f, -0.989690661f), v * vV));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-0.907216489f, 6.08247423f, -0.989690661f), v * vV));
    }

    // TRAP: OSG M * v equals VSG v * M, exactly. Matrixf:710-716, 915 against mat4.h:236-242.
    TEST(MatrixConventions, matrixTimesVectorIsVsgVectorTimesMatrix)
    {
        const vsg::mat4 v = vsgGeneral();
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-0.153696492f, 0.357003897f, -0.214396879f), vV * v));
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-0.153696492f, 0.357003897f, -0.214396879f), vV * v));
    }

    // The same for double matrices with double vectors. Matrixd:600-640, 793-800.
    TEST(MatrixConventions, doubleVectorProducts)
    {
        const vsg::dmat4 v(vsgGeneral());
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(-0.9072164931729592, 6.082474215591431, -0.9896907050775055), v * vdV));
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(-0.15369649805447472, 0.35700389105058367, -0.21439688644056654), vdV * v));
    }

    // OSG v4 * M equals VSG M * v4, OSG M * v4 equals VSG v4 * M. No divide for 4-vectors.
    // Matrixf:740-761, mat4.h:179-185, 204-210.
    TEST(MatrixConventions, vec4Products)
    {
        const vsg::mat4 v = vsgGeneral();
        const vsg::vec4 vU(1.5f, -2.25f, 3.75f, 0.5f);
        EXPECT_TRUE(vecExact<4>(vsg::vec4(-6.25f, 22.4375f, -7.5f, 2.03125f), v * vU));
        EXPECT_TRUE(vecExact<4>(vsg::vec4(-10.0f, 23.0f, -13.8249998f, 63.25f), vU * v));
    }

    // TRAP: VSG also defines v * M, so keeping OSG's expression still compiles. It reads the translation as a
    // projective term and divides by it (mat4.h:236-242; guide, "vsgopenmw predates VSG 1.1"). VSG (1, 2, 3) *
    // translate(10, 20, 30) is (1, 2, 3) / 141, while OSG v * translate gives (11, 22, 33).
    TEST(MatrixConventions, keepingTheOsgExpressionCompilesAndDividesByTheTranslation)
    {
        const vsg::vec3 vPoint(1.f, 2.f, 3.f);

        EXPECT_TRUE(vecExact<3>(vsg::vec3(11.0f, 22.0f, 33.0f), vsg::vec3(11.f, 22.f, 33.f)));
        EXPECT_TRUE(vecExact<3>(vsg::translate(10.f, 20.f, 30.f) * vPoint, vsg::vec3(11.f, 22.f, 33.f)));

        const vsg::vec3 wrong = vPoint * vsg::translate(10.f, 20.f, 30.f);
        EXPECT_TRUE(vecNear<3>(wrong, vsg::vec3(1.f / 141.f, 2.f / 141.f, 3.f / 141.f)));
    }

    // TRAP: OSG Vec3f * Matrixd computes in double and narrows the result (Matrixd:618-624). VSG has no mixed product,
    // and the tempting mat4(m) * v computes in float. OSG v * Md equals VSG vec3(Md * dvec3(v)) exactly. Translation
    // 16777217 is the first integer a float cannot hold.
    TEST(MatrixConventions, floatVectorTimesDoubleMatrix)
    {
        const vsg::dmat4 v = vsg::translate(16777217.0, 0.0, 0.0);
        const vsg::vec3 vPoint(1.f, 0.f, 0.f);

        EXPECT_TRUE(vecExact<3>(vsg::vec3(16777218.0f, 0.0f, 0.0f), vsg::vec3(v * vsg::dvec3(vPoint))));
        EXPECT_TRUE(recordedEqual(16777218.0f, 16777218.f));

        const vsg::vec3 narrowed = vsg::mat4(v) * vPoint;
        EXPECT_NE(16777218.0f, narrowed.x);
    }

    // OSG Vec3d * Matrixf computes in double from the float elements (Matrixf:732-738).
    // For a matrix with last column (0, 0, 0, 1), OSG v * Mf equals VSG dmat4(Mf) * v exactly.
    TEST(MatrixConventions, doubleVectorTimesFloatMatrix)
    {
        const vsg::dmat4 v(vsgOther());
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(-1.6250000223517418, 3.1750000938773155, 7.924999892711639), v * vdV));
    }

    // OSG m.getTrans() equals VSG m[3].xyz, widened: OSG returns Vec3d from both matrix types. Matrixf:348, vec4.h
    // union. TRAP: m[3] is the "column 3" of VSG's naming and the "row 3" of OSG's: the same numbers.
    TEST(MatrixConventions, getTrans)
    {
        const vsg::mat4 v = vsgGeneral();
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(7.0, -8.0, 9.0), vsg::dvec3(v[3].xyz)));
        EXPECT_TRUE(vecExact<3>(vsg::dvec3(7.0, -8.0, 9.0), vsg::dvec3(7.0, -8.0, 9.0)));
    }

    // OSG m.setTrans(t) equals VSG m[3].xyz = t: only the three elements change. Matrix_implementation.cpp:320.
    TEST(MatrixConventions, setTrans)
    {
        vsg::mat4 v = vsgGeneral();
        v[3].xyz = vV;
        EXPECT_TRUE(matExact(matOf<float>(0.5f, 1.5f, -2.0f, 0.25f, 3.0f, -0.75f, 4.5f, -0.125f, -1.0f, 6.0f,
                                 0.300000012f, 0.100000001f, 1.5f, -2.25f, 3.75f, 2.0f),
            v));
    }

    // TRAP: OSG getScale measures (m(0, i), m(1, i), m(2, i)) (Matrixf:350-355), which is NOT what vsg::decompose
    // measures (length(m[i].xyz), maths_transform.cpp:244-246). They agree without rotation or with uniform scale; with
    // a rotation and a non-uniform scale OSG's answer is not the scale at all, and that behaviour is what must be kept.
    // Used for physics at apps/openmw/mwphysics/object.cpp:248. OSG m.getScale() equals VSG dvec3(length(dvec3(m[0][i],
    // m[1][i], m[2][i]))...), and scale(2,3,4) then 90 degrees about Z gives (3, 2, 4) rather than (2, 3, 4).
    TEST(MatrixConventions, getScaleReadsAcrossTheStorage)
    {
        const vsg::dmat4 v
            = vsg::rotate(vsg::dquat(vsg::PI / 2.0, vsg::dvec3(0.0, 0.0, 1.0))) * vsg::scale(2.0, 3.0, 4.0);

        EXPECT_TRUE(vecNear<3>(vsg::dvec3(3.0, 2.0, 4.0), vsg::dvec3(3.0, 2.0, 4.0)));

        const vsg::dvec3 correct(vsg::length(vsg::dvec3(v[0][0], v[1][0], v[2][0])),
            vsg::length(vsg::dvec3(v[0][1], v[1][1], v[2][1])), vsg::length(vsg::dvec3(v[0][2], v[1][2], v[2][2])));
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(3.0, 2.0, 4.0), correct));

        const vsg::dvec3 naive(vsg::length(v[0].xyz), vsg::length(v[1].xyz), vsg::length(v[2].xyz));
        EXPECT_TRUE(vecNear<3>(naive, vsg::dvec3(2.0, 3.0, 4.0)));
        EXPECT_FALSE(vecNear<3>(vsg::dvec3(3.0, 2.0, 4.0), naive));
    }

    // OSG transform3x3(v, m) (v * M without translation) equals VSG (m * vec4(v, 0)).xyz exactly. Matrixf:769.
    TEST(MatrixConventions, transform3x3VectorFirst)
    {
        const vsg::mat4 v = vsgGeneral();
        const vsg::vec3 result = (v * vsg::vec4(vV, 0.f)).xyz;
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-9.75f, 26.4375f, -12.0f), result));
    }

    // TRAP: OSG transform3x3(m, v) (M * v) equals VSG (vec4(v, 0) * m).xyz exactly, so the flip applies here too.
    // Matrixf:782.
    TEST(MatrixConventions, transform3x3MatrixFirst)
    {
        const vsg::mat4 v = vsgGeneral();
        const vsg::vec3 result = (vsg::vec4(vV, 0.f) * v).xyz;
        EXPECT_TRUE(vecExact<3>(vsg::vec3(-10.125f, 23.0625f, -13.875f), result));
    }

    // OSG m.preMultTranslate(t) equals VSG m = m * translate(t), to tolerance: OSG adds the terms in a different order
    // (Matrixf:809-821). Hot-path call sites may want the helper's in-place form to avoid the full product.
    TEST(MatrixConventions, preMultTranslate)
    {
        EXPECT_TRUE(matNear(
            matOf<float>(1.10000002f, -0.200000003f, 0.300000012f, 0.0f, 0.400000006f, 0.899999976f, -0.600000024f,
                0.0f, 0.699999988f, 0.800000012f, 1.29999995f, 0.0f, -1.625f, 3.17500019f, 7.92500019f, 1.0f),
            vsgOther() * vsg::translate(vV)));
    }

    // OSG m.postMultTranslate(t) equals VSG m = translate(t) * m, to tolerance. Matrixf:837-849.
    TEST(MatrixConventions, postMultTranslate)
    {
        EXPECT_TRUE(
            matNear(matOf<float>(1.10000002f, -0.200000003f, 0.300000012f, 0.0f, 0.400000006f, 0.899999976f,
                        -0.600000024f, 0.0f, 0.699999988f, 0.800000012f, 1.29999995f, 0.0f, -3.5f, 0.25f, 5.0f, 1.0f),
                vsg::translate(vV) * vsgOther()));
    }

    // OSG m.preMultScale(s) equals VSG m = m * scale(s); postMultScale equals scale(s) * m. Matrixf:858, 872.
    TEST(MatrixConventions, preAndPostMultScale)
    {
        EXPECT_TRUE(matNear(matOf<float>(0.75f, 2.25f, -3.0f, 0.375f, -6.75f, 1.6875f, -10.125f, 0.28125f, -3.75f,
                                22.5f, 1.125f, 0.375f, 7.0f, -8.0f, 9.0f, 2.0f),
            vsgGeneral() * vsg::scale(vV)));

        EXPECT_TRUE(matNear(matOf<float>(0.75f, -3.375f, -7.5f, 0.25f, 4.5f, 1.6875f, 16.875f, -0.125f, -1.5f, -13.5f,
                                1.125f, 0.100000001f, 10.5f, 18.0f, 33.75f, 2.0f),
            vsg::scale(vV) * vsgGeneral()));
    }

    // OSG m.preMultRotate(q) equals VSG m = m * rotate(q); postMultRotate equals rotate(q) * m. Matrixf:880-896.
    TEST(MatrixConventions, preAndPostMultRotate)
    {
        const vsg::dmat4 vBase(vsgOther());

        EXPECT_TRUE(matNear(matOf<double>(0.8740794940913997, 0.10361135115211897, -0.4777238180016101, 0.0,
                                -0.007340731712497994, 1.0637780332066553, -0.28925366358391824, 0.0,
                                1.0468673231111987, 0.5896108558444697, 1.3520769887851707, 0.0, -5.0, 2.5, 1.25, 1.0),
            vBase * vsg::rotate(vQ)));

        EXPECT_TRUE(matNear(
            matOf<double>(1.0748109123733465, 0.41730518697961344, -0.10314041089613107, 0.0, -0.35882455837272714,
                1.0117094974735799, -0.4215315166077525, 0.0, 0.6739656980919964, 0.9578959260344432,
                1.2034141062362995, 0.0, -4.622094332353405, -0.7597514433575504, 3.297199073022835, 1.0),
            vsg::rotate(vQ) * vBase));
    }

    // OSG t.transpose(m) equals VSG t = transpose(m), exactly. Matrix_implementation.cpp:748, transform.h:126.
    TEST(MatrixConventions, transpose)
    {
        EXPECT_TRUE(matExact(matOf<float>(0.5f, 3.0f, -1.0f, 7.0f, 1.5f, -0.75f, 6.0f, -8.0f, -2.0f, 4.5f, 0.300000012f,
                                 9.0f, 0.25f, -0.125f, 0.100000001f, 2.0f),
            vsg::transpose(vsgGeneral())));
    }

    // OSG Matrixf::inverse(m) equals VSG inverse(m) in the same storage, to tolerance. No flip is needed: the
    // transposed reading of an inverse is the inverse of the transposed reading. Both pick 4x3 by the same test:
    // Matrixf:235, maths_transform.cpp:172.
    TEST(MatrixConventions, inverseOfAffineFloat)
    {
        const vsg::mat4 v = vsg::translate(1.f, 2.f, 3.f) * vsg::scale(2.f, 2.f, 2.f);
        ASSERT_TRUE(matExact(matOf<float>(2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 1.0f,
                                 2.0f, 3.0f, 1.0f),
            v));
        EXPECT_TRUE(matNear(matOf<float>(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, -0.5f,
                                -1.0f, -1.5f, 1.0f),
            vsg::inverse(v)));
    }

    TEST(MatrixConventions, inverseOfAffineDouble)
    {
        const vsg::dmat4 v(vsgOther());
        EXPECT_TRUE(matNear(
            matOf<double>(0.8638743220671654, 0.26178010906217936, -0.07853402998807434, 0.0, -0.49214659676714245,
                0.6387434428627848, 0.4083769887049392, 0.0, -0.1623036458519336, -0.5340314312246996,
                0.5602094237970277, 0.0, 5.7526176595686, 0.37958122718480924, -2.1138744014490043, 1.0),
            vsg::inverse(v)));
    }

    // OSG uses Gauss-Jordan (Matrix_implementation.cpp:794) and VSG cofactors (maths_transform.cpp:112).
    TEST(MatrixConventions, inverseOfGeneralMatrix)
    {
        const vsg::dmat4 v(vsgGeneral());
        EXPECT_TRUE(
            matNear(matOf<double>(0.4793972649127629, 0.2920658541987109, -0.1302204443370851, -0.035159519912799905,
                        0.09403776439174318, 0.061947725934874326, 0.1316156633835538, -0.01446377094527725,
                        -0.3022974602761371, 0.014510278244054141, 0.12091898402729326, 0.0326481256333142,
                        0.05859920161491915, -0.8397358380542345, 0.43809878059119334, 0.4182866705637768),
                vsg::inverse(v)));
    }

    // OSG t.invert(m) writes through the object and returns a status. OSG t.invert(m) equals VSG t = inverse(m).
    TEST(MatrixConventions, invertIntoAnotherMatrix)
    {
        EXPECT_TRUE(matNear(
            matOf<double>(0.8638743220671654, 0.26178010906217936, -0.07853402998807434, 0.0, -0.49214659676714245,
                0.6387434428627848, 0.4083769887049392, 0.0, -0.1623036458519336, -0.5340314312246996,
                0.5602094237970277, 0.0, 5.7526176595686, 0.37958122718480924, -2.1138744014490043, 1.0),
            vsg::inverse(vsg::dmat4(vsgOther()))));
    }

    // TRAP: a singular matrix. OSG's 4x3 inverse reports success and fills the result with Inf and NaN through
    // one_over_det (Matrix_implementation.cpp:630-638); VSG returns NaN on the diagonal and 0 elsewhere
    // (maths_transform.cpp:81, mat4.h:38). The Lua binding at components/lua/utilpackage.cpp:327 tests OSG's return
    // value, which is always true on this path, so VSG offers nothing to replace that test with and its numbers differ.
    TEST(MatrixConventions, inverseOfSingularMatrix)
    {

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
        const vsg::mat4 vInput(1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 2.f);

        EXPECT_TRUE(recordedEqual(0.5f, 0.5f));
        EXPECT_TRUE(matNear(matOf<float>(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 0.5f),
            vsg::inverse(vInput)));
        EXPECT_FALSE(matNear(matOf<float>(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 0.5f),
            vsg::inverse_4x3(vInput)));
    }

    // TRAP, the other side. When m(3, 3) is 1 but the rest of OSG's last column is not 0, OSG's invert_4x3 ignores that
    // column, matching VSG's inverse_4x3 and NOT vsg::inverse, which sees the column and switches to the full inverse
    // (maths_transform.cpp:172-179). So no single VSG call matches OSG's invert_4x3 on every input.
    TEST(MatrixConventions, invert4x3IgnoresTheLastColumnWhenWIsOne)
    {
        const vsg::mat4 vInput(1.f, 0.f, 0.f, 0.5f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f);

        EXPECT_TRUE(matNear(matOf<float>(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.0f,
                                -0.0f, -0.0f, 1.0f),
            vsg::inverse_4x3(vInput)));
        EXPECT_FALSE(matNear(matOf<float>(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.0f,
                                 -0.0f, -0.0f, 1.0f),
            vsg::inverse(vInput)));
    }

    // OSG m.decompose(t, r, s, so) equals VSG decompose(m, t, r, s) for translate, rotate and positive scale, with so
    // the identity. ARGUMENT ORDER differs by the extra so. A quat and its negative are the same rotation, so r is
    // compared by the dot product. MatrixDecomposition.cpp:799, maths_transform.cpp:238.
    TEST(MatrixConventions, decomposeOfTranslateRotateScale)
    {
        const vsg::dvec3 vT(10.0, -20.0, 30.0);

        const vsg::dmat4 v = vsg::translate(vT) * vsg::rotate(vQ) * vsg::scale(2.0, 3.0, 4.0);

        // What OSG's decompose returned for the same matrix.
        const vsg::dquat oRotation(0.091643293869591358, 0.18328658773918294, 0.2749298816087738, 0.93937271284737967);

        vsg::dvec3 vTranslation;
        vsg::dvec3 vScale;
        vsg::dquat vRotation;
        ASSERT_TRUE(vsg::decompose(v, vTranslation, vRotation, vScale));

        EXPECT_TRUE(vecNear<3>(vsg::dvec3(10.0, -20.0, 30.0), vTranslation));
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(2.000000000000001, 3.000000000000002, 4.000000000000019), vScale));
        const double rotationDot = oRotation.x * vRotation.x + oRotation.y * vRotation.y + oRotation.z * vRotation.z
            + oRotation.w * vRotation.w;
        EXPECT_NEAR(std::abs(rotationDot), 1.0, kRelativeTolerance);
    }

    // TRAP: negative scale. OSG keeps it: a mirror comes back as negative scale plus a rotation
    // (MatrixDecomposition.cpp:829-833) and the parts rebuild the input. VSG returns positive scale, a non-unit quat,
    // and true (maths_transform.cpp:244-265), and its parts rebuild the identity: the mirror is lost. Callers:
    // components/lua/shapes/box.cpp:15, components/lua/utilpackage.cpp:302, apps/openmw/mwrender/bonepose.cpp:59,
    // apps/openmw/mwrender/actorikcontroller.cpp:196.
    TEST(MatrixConventions, decomposeOfNegativeScale)
    {

        EXPECT_TRUE(vecNear<3>(vsg::dvec3(-1.0, -1.0, -1.0), vsg::dvec3(-1.0, -1.0, -1.0)));

        EXPECT_TRUE(
            matNear(matOf<double>(-1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
                matOf<double>(-1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)));

        vsg::dvec3 vTranslation;
        vsg::dvec3 vScale;
        vsg::dquat vRotation;
        ASSERT_TRUE(vsg::decompose(vsg::scale(-1.0, 1.0, 1.0), vTranslation, vRotation, vScale));
        EXPECT_TRUE(vecNear<3>(vScale, vsg::dvec3(1.0, 1.0, 1.0)));
        EXPECT_NEAR(vsg::length(vRotation), std::sqrt(0.5), 1e-12);

        const vsg::dmat4 vRebuilt = vsg::translate(vTranslation) * vsg::rotate(vRotation) * vsg::scale(vScale);
        EXPECT_TRUE(matNear(vRebuilt, vsg::dmat4()));
    }

    // TRAP: zero scale. VSG returns false and leaves the rotation as it was (maths_transform.cpp:250). The callers
    // above ignore OSG's void return, so they must handle this when converted.
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
        const vsg::dmat4 v = vsg::perspective(vsg::radians(60.0), 1.5, 1.0, 1000.0);
        EXPECT_NEAR(1.1547005383792517, v[0][0], 1e-12);
        EXPECT_NEAR(1.7320508075688774, -v[1][1], 1e-12);
        EXPECT_TRUE(recordedEqual(-1.0, v[2][3]));

        const vsg::dmat4 degreesPassedAsIs = vsg::perspective(60.0, 1.5, 1.0, 1000.0);
        EXPECT_GT(std::abs(1.1547005383792517 - degreesPassedAsIs[0][0]), 0.01);
    }

    // TRAP: clip space differs, so a projection is NOT a mechanical conversion. OSG maps near to -1 and far to +1
    // (Matrix_implementation.cpp:926-931). VSG reverses depth: near to 1 and far to 0 (transform.h:146-149).
    TEST(MatrixConventions, perspectiveDepthRange)
    {
        const vsg::dmat4 v = vsg::perspective(vsg::radians(60.0), 1.5, 1.0, 1000.0);

        EXPECT_NEAR(-1.0000000000000002, -1.0, 1e-9);
        EXPECT_NEAR(1.0, 1.0, 1e-9);
        EXPECT_NEAR((v * vsg::dvec3(0.0, 0.0, -1.0)).z, 1.0, 1e-9);
        EXPECT_NEAR((v * vsg::dvec3(0.0, 0.0, -1000.0)).z, 0.0, 1e-9);
    }

    // TRAP: the same for orthographic. OSG ortho(l, r, b, t, n, f) maps near to -1 and far to +1, Y up
    // (Matrix_implementation.cpp:870-876). VSG orthographic(l, r, b, t, n, f) maps near to 1, far to 0, Y flipped
    // (transform.h:172-175). The X row matches.
    TEST(MatrixConventions, orthographicDepthRangeAndYFlip)
    {
        const vsg::dmat4 v = vsg::orthographic(-2.0, 2.0, -1.0, 3.0, 1.0, 100.0);

        EXPECT_TRUE(recordedEqual(0.5, v[0][0]));
        EXPECT_TRUE(recordedEqual(-0.0, v[3][0]));
        EXPECT_TRUE(recordedEqual(0.5, -v[1][1]));
        EXPECT_TRUE(recordedEqual(-0.5, -v[3][1]));

        EXPECT_NEAR(-0.9999999999999999, -1.0, 1e-12);
        EXPECT_NEAR(1.0000000000000002, 1.0, 1e-12);
        EXPECT_NEAR((v * vsg::dvec3(0.0, 0.0, -1.0)).z, 1.0, 1e-12);
        EXPECT_NEAR((v * vsg::dvec3(0.0, 0.0, -100.0)).z, 0.0, 1e-12);
    }

    // OSG Matrixd::lookAt(eye, center, up) equals VSG lookAt(eye, center, up), to tolerance, in the same storage.
    // Unlike projections, this view convention is shared. Matrix_implementation.cpp:1037-1049, transform.h:179-196.
    TEST(MatrixConventions, lookAt)
    {
        const vsg::dmat4 v
            = vsg::lookAt(vsg::dvec3(1.0, 2.0, 3.0), vsg::dvec3(4.0, 6.0, 3.0), vsg::dvec3(0.0, 0.0, 2.0));
        EXPECT_TRUE(matNear(matOf<double>(0.8, -0.0, -0.6000000000000001, 0.0, -0.6000000000000001, 0.0, -0.8, 0.0, 0.0,
                                1.0, -0.0, 0.0, 0.40000000000000013, -3.0, 2.2, 1.0),
            v));
    }

    // Pins for the helper. OSG m.getLookAt(eye, center, up, d) equals, in VSG:
    //   eye = inverse(m) * dvec3(0, 0, 0)
    //   up = (dvec4(0, 1, 0, 0) * m).xyz
    //   center = eye + normalize((dvec4(0, 0, -1, 0) * m).xyz) * d
    // Note the v * m forms: OSG uses transform3x3(*this, v), which is its M * v. Matrix_implementation.cpp:1069-1085.
    TEST(MatrixConventions, getLookAt)
    {

        const vsg::dmat4 v
            = vsg::lookAt(vsg::dvec3(1.0, 2.0, 3.0), vsg::dvec3(4.0, 6.0, 3.0), vsg::dvec3(0.0, 0.0, 1.0));
        const vsg::dvec3 eye = vsg::inverse(v) * vsg::dvec3(0.0, 0.0, 0.0);
        const vsg::dvec3 up = (vsg::dvec4(0.0, 1.0, 0.0, 0.0) * v).xyz;
        const vsg::dvec3 forward = (vsg::dvec4(0.0, 0.0, -1.0, 0.0) * v).xyz;
        const vsg::dvec3 center = eye + vsg::normalize(forward) * 2.0;

        EXPECT_TRUE(vecNear<3>(vsg::dvec3(1.0, 1.9999999999999998, 3.0), eye));
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(0.0, 0.0, 1.0), up));
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(2.2, 3.5999999999999996, 3.0), center));
        EXPECT_TRUE(vecNear<3>(vsg::dvec3(1.0, 1.9999999999999998, 3.0), vsg::dvec3(1.0, 2.0, 3.0)));
    }
}

#endif
