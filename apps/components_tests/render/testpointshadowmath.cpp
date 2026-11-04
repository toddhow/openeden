// Tests for the pure maths of the point-light shadows, Render::PointShadow (task 0077, stage a). Written from the task
// spec alone ("Design", "Interfaces" and "Acceptance criteria" of docs/tasks/0077-point-light-shadows.md), without
// seeing any implementation. Every expected value is worked out from the spec's formulas with the test's own helpers
// (a literal face table, its own face selection and projection), never from the code under test. Pure CPU maths: no
// device, window or game data.

#include <components/render/depthmath.hpp>
#include <components/render/pointshadowmath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

namespace
{
    using namespace Render::PointShadow;
    using V3 = vsg::dvec3;

    // --- Small vector helpers (the project forbids vec * vec, so the tests use component arithmetic) ----------------

    V3 v3(double x, double y, double z)
    {
        return V3(x, y, z);
    }

    V3 add3(const V3& a, const V3& b)
    {
        return V3(a.x + b.x, a.y + b.y, a.z + b.z);
    }

    V3 sub3(const V3& a, const V3& b)
    {
        return V3(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    V3 mul3(const V3& a, double s)
    {
        return V3(a.x * s, a.y * s, a.z * s);
    }

    double dot3(const V3& a, const V3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    V3 cross3(const V3& a, const V3& b)
    {
        return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }

    double len3(const V3& a)
    {
        return std::sqrt(dot3(a, a));
    }

    V3 unit3(const V3& a)
    {
        return mul3(a, 1.0 / len3(a));
    }

    void expectNear3(const V3& actual, const V3& expected, double tolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }

    // A fixed generator (std::mt19937_64 with a fixed seed) and our own uniform mapping, so the "random" inputs are the
    // same on every machine and every standard library.
    class Rng
    {
    public:
        explicit Rng(std::uint64_t seed)
            : mEngine(seed)
        {
        }

        double uniform(double low, double high)
        {
            const double unit = static_cast<double>(mEngine() >> 11) * (1.0 / 9007199254740992.0);
            return low + (high - low) * unit;
        }

        V3 point(double extent)
        {
            return V3(uniform(-extent, extent), uniform(-extent, extent), uniform(-extent, extent));
        }

        V3 direction()
        {
            for (;;)
            {
                const V3 candidate = point(1.0);
                const double length = len3(candidate);
                if (length > 0.1 && length <= 1.0)
                {
                    return mul3(candidate, 1.0 / length);
                }
            }
        }

    private:
        std::mt19937_64 mEngine;
    };

    // --- The spec's face table, written out literally -------------------------------------------------------------

    struct RefFace
    {
        V3 forward;
        V3 up;
        V3 right; // by hand: cross(forward, up)
    };

    const std::array<RefFace, 6>& refFaces()
    {
        static const std::array<RefFace, 6> faces = { {
            { v3(1, 0, 0), v3(0, 0, 1), v3(0, -1, 0) },
            { v3(-1, 0, 0), v3(0, 0, 1), v3(0, 1, 0) },
            { v3(0, 1, 0), v3(0, 0, 1), v3(1, 0, 0) },
            { v3(0, -1, 0), v3(0, 0, 1), v3(-1, 0, 0) },
            { v3(0, 0, 1), v3(0, -1, 0), v3(1, 0, 0) },
            { v3(0, 0, -1), v3(0, 1, 0), v3(1, 0, 0) },
        } };
        return faces;
    }

    const RefFace& refFace(int face)
    {
        return refFaces()[static_cast<std::size_t>(std::clamp(face, 0, 5))];
    }

    // The spec's face selection: the largest |component|, ties in the order x, y, z; the sign picks + or -; the zero
    // vector gives 0 (and so does a negative zero: it is not "< 0").
    int refFaceOf(const V3& d)
    {
        const double ax = std::fabs(d.x);
        const double ay = std::fabs(d.y);
        const double az = std::fabs(d.z);
        if (ax >= ay && ax >= az)
        {
            return d.x < 0.0 ? 1 : 0;
        }
        if (ay >= az)
        {
            return d.y < 0.0 ? 3 : 2;
        }
        return d.z < 0.0 ? 5 : 4;
    }

    double refTanHalf(double guard, double resolution)
    {
        return 1.0 + 2.0 * guard / (resolution - 2.0 * guard);
    }

    struct RefCoord
    {
        int face;
        double u;
        double v;
        double distance;
    };

    RefCoord refProject(const V3& d, double tanHalf)
    {
        const int face = refFaceOf(d);
        const RefFace& axes = refFace(face);
        const double distance = dot3(d, axes.forward);
        return { face, 0.5 + 0.5 * dot3(d, axes.right) / (distance * tanHalf),
            0.5 + 0.5 * dot3(d, axes.up) / (distance * tanHalf), distance };
    }

    double refTexel(double distance, double tanHalf, double resolution)
    {
        return 2.0 * distance * tanHalf / resolution;
    }

    // The world direction (not normalised, forward component 1) of the ray through a texel position of a face.
    V3 refRay(int face, double u, double v, double tanHalf)
    {
        const RefFace& axes = refFace(face);
        return add3(
            axes.forward, add3(mul3(axes.right, (2.0 * u - 1.0) * tanHalf), mul3(axes.up, (2.0 * v - 1.0) * tanHalf)));
    }

    // The centre of the texel that contains the position (clamp to edge, like the sampler of the spec).
    double texelCentre(double coordinate, double resolution)
    {
        const double index = std::clamp(std::floor(coordinate * resolution), 0.0, resolution - 1.0);
        return (index + 0.5) / resolution;
    }

    // --- The faces ---------------------------------------------------------------------------------------------------

    TEST(RenderPointShadowMath, theConstantsAreTheOnesOfTheSpec)
    {
        EXPECT_EQ(facesPerLight, 6);
        EXPECT_EQ(maxSlots, 8);
        EXPECT_EQ(maxTaps, 16);
        EXPECT_EQ(nearPlane, 4.0);
        EXPECT_EQ(farMargin, 1.1);
        EXPECT_EQ(defaultGuardTexels, 2.0);
    }

    TEST(RenderPointShadowMath, theLiteralFaceTableHasRightEqualToCrossOfForwardAndUp)
    {
        // Guards the test's own table against a slip: right = cross(forward, up) for every face.
        for (int face = 0; face < 6; ++face)
        {
            const RefFace& axes = refFace(face);
            expectNear3(cross3(axes.forward, axes.up), axes.right, 0.0);
        }
    }

    TEST(RenderPointShadowMath, faceAxesEqualTheTableOfTheSpec)
    {
        for (int face = 0; face < 6; ++face)
        {
            const FaceAxes axes = faceAxes(face);
            const RefFace& expected = refFace(face);
            expectNear3(axes.forward, expected.forward, 1e-12);
            expectNear3(axes.up, expected.up, 1e-12);
            expectNear3(axes.right, expected.right, 1e-12);
        }
    }

    TEST(RenderPointShadowMath, faceAxesAreOrthonormalAndRightHanded)
    {
        for (int face = 0; face < 6; ++face)
        {
            const FaceAxes axes = faceAxes(face);
            EXPECT_NEAR(len3(axes.right), 1.0, 1e-12) << face;
            EXPECT_NEAR(len3(axes.up), 1.0, 1e-12) << face;
            EXPECT_NEAR(len3(axes.forward), 1.0, 1e-12) << face;
            EXPECT_NEAR(dot3(axes.right, axes.up), 0.0, 1e-12) << face;
            EXPECT_NEAR(dot3(axes.right, axes.forward), 0.0, 1e-12) << face;
            EXPECT_NEAR(dot3(axes.up, axes.forward), 0.0, 1e-12) << face;
            // right = cross(forward, up), and the view space (x right, y up, z = -forward) is right-handed.
            expectNear3(axes.right, cross3(axes.forward, axes.up), 1e-12);
            expectNear3(cross3(axes.right, axes.up), mul3(axes.forward, -1.0), 1e-12);
        }
    }

    TEST(RenderPointShadowMath, aFaceOutsideZeroToFiveGivesTheAxesOfFaceZero)
    {
        const FaceAxes zero = faceAxes(0);
        for (const int face : { -1, 6, 7, 100, -100 })
        {
            const FaceAxes axes = faceAxes(face);
            expectNear3(axes.forward, zero.forward, 0.0);
            expectNear3(axes.up, zero.up, 0.0);
            expectNear3(axes.right, zero.right, 0.0);
        }
        expectNear3(zero.forward, v3(1, 0, 0), 0.0);
    }

    TEST(RenderPointShadowMath, theViewMatrixIsTheMatrixOfVsgLookAtWithTheTableAxes)
    {
        Rng rng(0x5eed0001);
        for (int face = 0; face < 6; ++face)
        {
            const RefFace& axes = refFace(face);
            for (int trial = 0; trial < 4; ++trial)
            {
                const V3 eye = trial == 0 ? v3(0, 0, 0) : rng.point(5000.0);
                const vsg::dmat4 expected = vsg::lookAt(eye, add3(eye, axes.forward), axes.up);
                const vsg::dmat4 actual = faceViewMatrix(face, eye);
                for (std::size_t column = 0; column < 4; ++column)
                {
                    for (std::size_t row = 0; row < 4; ++row)
                    {
                        EXPECT_NEAR(actual(column, row), expected(column, row), 1e-9)
                            << "face " << face << " column " << column << " row " << row;
                    }
                }
            }
        }
    }

    TEST(RenderPointShadowMath, theRotationOfTheViewMatrixHasTheFaceAxesAsRows)
    {
        // vsg::LookAt's matrix has the camera's right, up and -forward as its rows. faceAxes must be those axes.
        for (int face = 0; face < 6; ++face)
        {
            const vsg::dmat4 view = faceViewMatrix(face, v3(0, 0, 0));
            const FaceAxes axes = faceAxes(face);
            // vsg indexes (column, row): row 0 is (m(0,0), m(1,0), m(2,0)).
            expectNear3(v3(view(0, 0), view(1, 0), view(2, 0)), axes.right, 1e-12);
            expectNear3(v3(view(0, 1), view(1, 1), view(2, 1)), axes.up, 1e-12);
            expectNear3(v3(view(0, 2), view(1, 2), view(2, 2)), mul3(axes.forward, -1.0), 1e-12);
        }
    }

    TEST(RenderPointShadowMath, theViewMatrixSendsAPointToTheFaceSpace)
    {
        Rng rng(0x5eed0002);
        for (int face = 0; face < 6; ++face)
        {
            const RefFace& axes = refFace(face);
            for (int trial = 0; trial < 20; ++trial)
            {
                const V3 eye = rng.point(3000.0);
                const V3 point = add3(eye, rng.point(400.0));
                const vsg::dvec4 view = faceViewMatrix(face, eye) * vsg::dvec4(point.x, point.y, point.z, 1.0);
                const V3 relative = sub3(point, eye);
                EXPECT_NEAR(view.x, dot3(relative, axes.right), 1e-9);
                EXPECT_NEAR(view.y, dot3(relative, axes.up), 1e-9);
                EXPECT_NEAR(view.z, -dot3(relative, axes.forward), 1e-9);
                EXPECT_NEAR(view.w, 1.0, 1e-12);
            }
        }
    }

    // --- faceOf ------------------------------------------------------------------------------------------------------

    TEST(RenderPointShadowMath, faceOfPicksTheAxisAndSign)
    {
        EXPECT_EQ(faceOf(v3(5, 1, 2)), 0);
        EXPECT_EQ(faceOf(v3(-5, 1, 2)), 1);
        EXPECT_EQ(faceOf(v3(1, 5, 2)), 2);
        EXPECT_EQ(faceOf(v3(1, -5, 2)), 3);
        EXPECT_EQ(faceOf(v3(1, 2, 5)), 4);
        EXPECT_EQ(faceOf(v3(1, 2, -5)), 5);
        // Only the largest |component| counts, not the sign of the others.
        EXPECT_EQ(faceOf(v3(-1, -9, 8)), 3);
        EXPECT_EQ(faceOf(v3(0.001, 0.0, 0.0)), 0);
        EXPECT_EQ(faceOf(v3(0.0, 0.0, -1e-300)), 5);
    }

    TEST(RenderPointShadowMath, faceOfBreaksTiesInTheOrderXYZ)
    {
        EXPECT_EQ(faceOf(v3(1, 1, 0)), 0);
        EXPECT_EQ(faceOf(v3(-1, 1, 0)), 1);
        EXPECT_EQ(faceOf(v3(1, -1, 0)), 0);
        EXPECT_EQ(faceOf(v3(1, 0, 1)), 0);
        EXPECT_EQ(faceOf(v3(-1, 0, -1)), 1);
        EXPECT_EQ(faceOf(v3(0, 1, 1)), 2);
        EXPECT_EQ(faceOf(v3(0, -1, 1)), 3);
        EXPECT_EQ(faceOf(v3(0, 1, -1)), 2);
        // A corner: all three tie, x wins.
        EXPECT_EQ(faceOf(v3(1, 1, 1)), 0);
        EXPECT_EQ(faceOf(v3(-1, 1, 1)), 1);
        EXPECT_EQ(faceOf(v3(1, -1, -1)), 0);
        EXPECT_EQ(faceOf(v3(-1, -1, -1)), 1);
        // y and z tie below x.
        EXPECT_EQ(faceOf(v3(0.5, 1, 1)), 2);
        EXPECT_EQ(faceOf(v3(0.5, -1, -1)), 3);
        // Ties are decided on magnitudes, so the scale does not matter.
        EXPECT_EQ(faceOf(v3(300, 300, 300)), 0);
        EXPECT_EQ(faceOf(v3(0.25, 0.25, -0.25)), 0);
    }

    TEST(RenderPointShadowMath, aTinyDifferenceBeatsTheTieOrder)
    {
        EXPECT_EQ(faceOf(v3(1.0, 1.0 + 1e-9, 0.0)), 2);
        EXPECT_EQ(faceOf(v3(1.0, 1.0, 1.0 + 1e-9)), 4);
        EXPECT_EQ(faceOf(v3(1.0, -(1.0 + 1e-9), 0.0)), 3);
        EXPECT_EQ(faceOf(v3(1.0 + 1e-9, 1.0, 1.0)), 0);
        EXPECT_EQ(faceOf(v3(-1.0, 1.0, -(1.0 + 1e-9))), 5);
    }

    TEST(RenderPointShadowMath, faceOfTheZeroVectorIsFaceZero)
    {
        EXPECT_EQ(faceOf(v3(0, 0, 0)), 0);
        EXPECT_EQ(faceOf(v3(-0.0, -0.0, -0.0)), 0);
        EXPECT_EQ(faceOf(v3(-0.0, 0.0, -0.0)), 0);
    }

    TEST(RenderPointShadowMath, aNegativeZeroOnTheMajorAxisIsNotNegative)
    {
        // The other components are smaller in magnitude, so a -0 can only be the major one when all are zero (above);
        // here -0 sits on the minor axes and must not change the face.
        EXPECT_EQ(faceOf(v3(-0.0, 0.0, -2.0)), 5);
        EXPECT_EQ(faceOf(v3(-0.0, -3.0, -0.0)), 3);
        EXPECT_EQ(faceOf(v3(4.0, -0.0, -0.0)), 0);
    }

    TEST(RenderPointShadowMath, faceOfAgreesWithTheRuleOnRandomDirectionsAndScales)
    {
        Rng rng(0x5eed0003);
        for (int i = 0; i < 20000; ++i)
        {
            V3 d = rng.point(100.0);
            if (i % 5 == 0)
            {
                // Force exact ties on a random pair of axes.
                d.y = i % 2 == 0 ? d.x : -d.x;
            }
            if (i % 7 == 0)
            {
                d.z = i % 2 == 0 ? d.x : -d.x;
            }
            EXPECT_EQ(faceOf(d), refFaceOf(d)) << d.x << " " << d.y << " " << d.z;
            // The face does not depend on the length of the vector.
            EXPECT_EQ(faceOf(mul3(d, 8.0)), refFaceOf(d));
        }
    }

    TEST(RenderPointShadowMath, theChosenFaceOfADirectionHasANonNegativeForwardDistanceThatIsTheLargestComponent)
    {
        Rng rng(0x5eed0004);
        for (int i = 0; i < 2000; ++i)
        {
            const V3 d = rng.direction();
            const int face = faceOf(d);
            ASSERT_GE(face, 0);
            ASSERT_LE(face, 5);
            const double distance = dot3(d, faceAxes(face).forward);
            EXPECT_GT(distance, 0.0);
            EXPECT_GE(distance, std::fabs(d.x) - 1e-15);
            EXPECT_GE(distance, std::fabs(d.y) - 1e-15);
            EXPECT_GE(distance, std::fabs(d.z) - 1e-15);
        }
    }

    // --- tanHalfFov ----------------------------------------------------------------------------------------------

    TEST(RenderPointShadowMath, tanHalfFovIsOnePlusTwoGOverResolutionMinusTwoG)
    {
        // 1 + 2 g / (res - 2 g): 1 + 4 / 508 for the defaults.
        EXPECT_NEAR(tanHalfFov(2.0, 512.0), 1.0 + 4.0 / 508.0, 1e-12);
        EXPECT_NEAR(tanHalfFov(2.0, 128.0), 1.0 + 4.0 / 124.0, 1e-12);
        EXPECT_NEAR(tanHalfFov(2.0, 2048.0), 1.0 + 4.0 / 2044.0, 1e-12);
        EXPECT_NEAR(tanHalfFov(1.0, 256.0), 1.0 + 2.0 / 254.0, 1e-12);
        EXPECT_NEAR(tanHalfFov(0.5, 64.0), 1.0 + 1.0 / 63.0, 1e-12);
        // Identity: the border of the map is exactly g texels outside the 45 degree line, i.e. tanHalf = res / (res - 2
        // g).
        EXPECT_NEAR(tanHalfFov(3.0, 100.0), 100.0 / 94.0, 1e-12);
        EXPECT_GT(tanHalfFov(2.0, 512.0), 1.0);
    }

    TEST(RenderPointShadowMath, tanHalfFovDegenerateCasesGiveOne)
    {
        EXPECT_EQ(tanHalfFov(0.0, 512.0), 1.0);
        EXPECT_EQ(tanHalfFov(-1.0, 512.0), 1.0);
        EXPECT_EQ(tanHalfFov(-100.0, 8.0), 1.0);
        // res <= 4 g: the guard bands would eat the whole map.
        EXPECT_EQ(tanHalfFov(2.0, 8.0), 1.0);
        EXPECT_EQ(tanHalfFov(2.0, 4.0), 1.0);
        EXPECT_EQ(tanHalfFov(2.0, 0.0), 1.0);
        EXPECT_EQ(tanHalfFov(1.0, 3.0), 1.0);
        // Just above the limit the formula applies again: 1 + 4 / (9 - 4) = 1.8.
        EXPECT_NEAR(tanHalfFov(2.0, 9.0), 1.8, 1e-12);
    }

    // --- project -------------------------------------------------------------------------------------------------

    TEST(RenderPointShadowMath, aPointAtTheFaceCentreProjectsToTheCentreOfTheMap)
    {
        for (int face = 0; face < 6; ++face)
        {
            for (const double distance : { 4.0, 17.5, 900.0 })
            {
                const FaceCoord coord = project(mul3(refFace(face).forward, distance), 1.007874);
                EXPECT_EQ(coord.face, face);
                EXPECT_NEAR(coord.u, 0.5, 1e-12);
                EXPECT_NEAR(coord.v, 0.5, 1e-12);
                EXPECT_NEAR(coord.distance, distance, 1e-9);
            }
        }
    }

    TEST(RenderPointShadowMath, projectGivesTheHandComputedCoordinates)
    {
        // Face 0 (+X): right = (0,-1,0), up = (0,0,1). With tanHalf 1: u = 0.5 + 0.5 * (-y) / x, v = 0.5 + 0.5 * z / x.
        FaceCoord c = project(v3(2, 1, 0), 1.0);
        EXPECT_EQ(c.face, 0);
        EXPECT_NEAR(c.u, 0.25, 1e-12);
        EXPECT_NEAR(c.v, 0.5, 1e-12);
        EXPECT_NEAR(c.distance, 2.0, 1e-12);

        c = project(v3(4, -2, 1), 1.0);
        EXPECT_EQ(c.face, 0);
        EXPECT_NEAR(c.u, 0.5 + 0.5 * 2.0 / 4.0, 1e-12); // 0.75
        EXPECT_NEAR(c.v, 0.5 + 0.5 * 1.0 / 4.0, 1e-12); // 0.625
        EXPECT_NEAR(c.distance, 4.0, 1e-12);

        // The same point with tanHalf 2 is half as far from the centre.
        c = project(v3(4, -2, 1), 2.0);
        EXPECT_NEAR(c.u, 0.5 + 0.5 * 2.0 / 8.0, 1e-12); // 0.625
        EXPECT_NEAR(c.v, 0.5 + 0.5 * 1.0 / 8.0, 1e-12); // 0.5625

        // Face 1 (-X): right = (0,1,0), up = (0,0,1).
        c = project(v3(-10, 5, -2), 1.0);
        EXPECT_EQ(c.face, 1);
        EXPECT_NEAR(c.u, 0.5 + 0.5 * 5.0 / 10.0, 1e-12); // 0.75
        EXPECT_NEAR(c.v, 0.5 + 0.5 * -2.0 / 10.0, 1e-12); // 0.4
        EXPECT_NEAR(c.distance, 10.0, 1e-12);

        // Face 2 (+Y): right = (1,0,0), up = (0,0,1).
        c = project(v3(3, 6, 6), 1.0);
        EXPECT_EQ(c.face, 2); // y and z tie, y first
        EXPECT_NEAR(c.u, 0.5 + 0.5 * 3.0 / 6.0, 1e-12); // 0.75
        EXPECT_NEAR(c.v, 0.5 + 0.5 * 6.0 / 6.0, 1e-12); // 1.0
        EXPECT_NEAR(c.distance, 6.0, 1e-12);

        // Face 3 (-Y): right = (-1,0,0), up = (0,0,1).
        c = project(v3(3, -6, 0), 1.0);
        EXPECT_EQ(c.face, 3);
        EXPECT_NEAR(c.u, 0.5 + 0.5 * -3.0 / 6.0, 1e-12); // 0.25
        EXPECT_NEAR(c.v, 0.5, 1e-12);

        // Face 4 (+Z): right = (1,0,0), up = (0,-1,0).
        c = project(v3(1, 2, 8), 1.0);
        EXPECT_EQ(c.face, 4);
        EXPECT_NEAR(c.u, 0.5 + 0.5 * 1.0 / 8.0, 1e-12); // 0.5625
        EXPECT_NEAR(c.v, 0.5 + 0.5 * -2.0 / 8.0, 1e-12); // 0.375
        EXPECT_NEAR(c.distance, 8.0, 1e-12);

        // Face 5 (-Z): right = (1,0,0), up = (0,1,0).
        c = project(v3(-1, 2, -8), 1.0);
        EXPECT_EQ(c.face, 5);
        EXPECT_NEAR(c.u, 0.5 + 0.5 * -1.0 / 8.0, 1e-12); // 0.4375
        EXPECT_NEAR(c.v, 0.5 + 0.5 * 2.0 / 8.0, 1e-12); // 0.625
        EXPECT_NEAR(c.distance, 8.0, 1e-12);
    }

    TEST(RenderPointShadowMath, projectOfTheZeroVectorIsTheDefaultCoordinate)
    {
        const FaceCoord c = project(v3(0, 0, 0), 1.01);
        EXPECT_EQ(c.face, 0);
        EXPECT_EQ(c.u, 0.5);
        EXPECT_EQ(c.v, 0.5);
        EXPECT_EQ(c.distance, 0.0);
    }

    TEST(RenderPointShadowMath, projectAgreesWithTheSpecFormulaOnRandomDirections)
    {
        Rng rng(0x5eed0005);
        for (const double tanHalf : { 1.0, 1.007874, 1.2 })
        {
            for (int i = 0; i < 3000; ++i)
            {
                const V3 d = mul3(rng.direction(), rng.uniform(1.0, 500.0));
                const FaceCoord c = project(d, tanHalf);
                const RefCoord expected = refProject(d, tanHalf);
                EXPECT_EQ(c.face, expected.face);
                EXPECT_NEAR(c.u, expected.u, 1e-12);
                EXPECT_NEAR(c.v, expected.v, 1e-12);
                EXPECT_NEAR(c.distance, expected.distance, 1e-9);
            }
        }
    }

    TEST(RenderPointShadowMath, projectedCoordinatesStayInsideTheMapMinusTheGuardBand)
    {
        // The chosen face's major axis rule keeps |right.d| / distance <= 1, so u, v lie within 0.5 +- 0.5 / tanHalf,
        // which is exactly g texels inside the border of a map of res texels with the spec's tanHalf.
        const double resolution = 512.0;
        const double tanHalf = tanHalfFov(defaultGuardTexels, resolution);
        const double guardUv = defaultGuardTexels / resolution;
        Rng rng(0x5eed0006);
        for (int i = 0; i < 5000; ++i)
        {
            const FaceCoord c = project(rng.direction(), tanHalf);
            EXPECT_GE(c.u, guardUv - 1e-12);
            EXPECT_LE(c.u, 1.0 - guardUv + 1e-12);
            EXPECT_GE(c.v, guardUv - 1e-12);
            EXPECT_LE(c.v, 1.0 - guardUv + 1e-12);
        }
    }

    TEST(RenderPointShadowMath, aDirectionAtFortyFiveDegreesIsExactlyTheGuardWidthFromTheBorder)
    {
        for (const double resolution : { 128.0, 512.0, 2048.0 })
        {
            const double tanHalf = tanHalfFov(defaultGuardTexels, resolution);
            // (1, 1, 0) ties to face 0 (x first); on face 0 the right axis is -y, so u is the low side.
            const FaceCoord c = project(v3(1, 1, 0), tanHalf);
            EXPECT_EQ(c.face, 0);
            EXPECT_NEAR(c.u * resolution, defaultGuardTexels, 1e-9);
            EXPECT_NEAR(c.v * resolution, 0.5 * resolution, 1e-9);
        }
    }

    TEST(RenderPointShadowMath, aDirectionPastFortyFiveDegreesBelongsToTheNextFaceByTheMajorAxisRule)
    {
        const double tanHalf = refTanHalf(defaultGuardTexels, 512.0);
        const double toRadians = 3.14159265358979323846 / 180.0;

        // 44 degrees from +X towards +Y: still face 0, near its low-u border.
        V3 d = v3(std::cos(44.0 * toRadians), std::sin(44.0 * toRadians), 0.0);
        FaceCoord c = project(d, tanHalf);
        EXPECT_EQ(c.face, 0);
        EXPECT_NEAR(c.u, 0.5 - 0.5 * std::tan(44.0 * toRadians) / tanHalf, 1e-12);
        EXPECT_GE(c.u, 0.0);

        // 46 degrees: y is the major axis, so the point is on face 2 and inside [0, 1] there.
        d = v3(std::cos(46.0 * toRadians), std::sin(46.0 * toRadians), 0.0);
        c = project(d, tanHalf);
        EXPECT_EQ(c.face, 2);
        EXPECT_NEAR(c.u, 0.5 + 0.5 / std::tan(46.0 * toRadians) / tanHalf, 1e-12);
        EXPECT_NEAR(c.v, 0.5, 1e-12);
        EXPECT_GE(c.u, 0.0);
        EXPECT_LE(c.u, 1.0);
    }

    TEST(RenderPointShadowMath, theGuardBandOfAFaceCoversAboutAQuarterDegreeBeyondFortyFiveDegrees)
    {
        // Seen through face 0's matrices (not the major axis rule), the border u = 0 is at atan(tanHalf) = 45.22
        // degrees (45.2247 by atan(1 + 4 / 508)) for the default guard and resolution: a direction at 45.1 degrees is
        // still inside the map, 45.5 is outside.
        const double tanHalf = tanHalfFov(defaultGuardTexels, 512.0);
        const double far = 100.0;
        const double toRadians = 3.14159265358979323846 / 180.0;
        const vsg::dmat4 view = faceViewMatrix(0, v3(0, 0, 0));
        const vsg::dmat4 projection = faceProjectionMatrix(tanHalf, far);
        const auto uOf = [&](double degrees) {
            const vsg::dvec4 p(std::cos(degrees * toRadians) * 10.0, std::sin(degrees * toRadians) * 10.0, 0.0, 1.0);
            const vsg::dvec4 clip = projection * (view * p);
            return 0.5 + 0.5 * clip.x / clip.w;
        };
        EXPECT_NEAR(std::atan(tanHalf) / toRadians, 45.2247, 1e-3);
        EXPECT_GT(uOf(45.1), 0.0);
        EXPECT_LT(uOf(45.5), 0.0);
        EXPECT_GT(uOf(44.0), uOf(45.1));
    }

    // --- The matrices and project agree
    // --------------------------------------------------------------------------------

    TEST(RenderPointShadowMath, theClipPositionOfAPointThroughTheMatricesEqualsProject)
    {
        Rng rng(0x5eed0007);
        const std::array<double, 3> resolutions = { 128.0, 512.0, 2048.0 };
        for (int i = 0; i < 10000; ++i)
        {
            const double resolution = resolutions[static_cast<std::size_t>(i % 3)];
            const double tanHalf = tanHalfFov(defaultGuardTexels, resolution);
            const double far = rng.uniform(50.0, 5000.0);
            const V3 light = rng.point(20000.0);
            const V3 offset = mul3(rng.direction(), rng.uniform(nearPlane + 0.5, far));
            const V3 point = add3(light, offset);
            const int face = faceOf(sub3(point, light));

            const vsg::dvec4 viewPos = faceViewMatrix(face, light) * vsg::dvec4(point.x, point.y, point.z, 1.0);
            const vsg::dvec4 clip = faceProjectionMatrix(tanHalf, far) * viewPos;
            ASSERT_GT(clip.w, 0.0);
            const double u = 0.5 + 0.5 * clip.x / clip.w;
            const double v = 0.5 + 0.5 * clip.y / clip.w;
            const double depth = clip.z / clip.w;

            const FaceCoord coord = project(sub3(point, light), tanHalf);
            EXPECT_EQ(coord.face, face);
            const auto relativelyNear
                = [](double a, double b) { return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(b)); };
            EXPECT_TRUE(relativelyNear(u, coord.u)) << i << " u " << u << " vs " << coord.u;
            EXPECT_TRUE(relativelyNear(v, coord.v)) << i << " v " << v << " vs " << coord.v;
            EXPECT_TRUE(relativelyNear(depth, depthValue(far, coord.distance)))
                << i << " depth " << depth << " vs " << depthValue(far, coord.distance);
            // The clip w is the distance along the face's forward axis.
            EXPECT_TRUE(relativelyNear(clip.w, coord.distance)) << i;
        }
    }

    TEST(RenderPointShadowMath, theProjectionMatrixIsTheOneOfTheDesign)
    {
        // clip.x = x / tanHalf, clip.y = y / tanHalf, w = -z, clip.z = n (f + z) / (f - n), for a view-space point.
        const double tanHalf = 1.2;
        const double far = 220.0;
        const vsg::dmat4 projection = faceProjectionMatrix(tanHalf, far);
        const std::array<V3, 4> points
            = { v3(3.0, -2.0, -10.0), v3(0.0, 0.0, -4.0), v3(-50.0, 40.0, -100.0), v3(1.0, 1.0, -220.0) };
        for (const V3& p : points)
        {
            const vsg::dvec4 clip = projection * vsg::dvec4(p.x, p.y, p.z, 1.0);
            EXPECT_NEAR(clip.x, p.x / tanHalf, 1e-12);
            EXPECT_NEAR(clip.y, p.y / tanHalf, 1e-12);
            EXPECT_NEAR(clip.w, -p.z, 1e-12);
            EXPECT_NEAR(clip.z, nearPlane * (far + p.z) / (far - nearPlane), 1e-9);
        }
    }

    // --- depth, texel size, bias -------------------------------------------------------------------------------------

    TEST(RenderPointShadowMath, depthValueIsTheReversedDepthOfTheDepthMath)
    {
        const double far = 330.0;
        EXPECT_NEAR(depthValue(far, nearPlane), 1.0, 1e-12);
        EXPECT_NEAR(depthValue(far, far), 0.0, 1e-12);
        double previous = 2.0;
        for (const double distance : { 4.0, 5.0, 8.0, 16.0, 50.0, 100.0, 200.0, 330.0 })
        {
            const double value = depthValue(far, distance);
            EXPECT_NEAR(value, Render::DepthMath::ndcDepth(nearPlane, far, distance), 1e-14) << distance;
            EXPECT_LT(value, previous) << distance;
            previous = value;
        }
        // The degenerate inputs are the ones of ndcDepth.
        EXPECT_EQ(depthValue(far, 0.0), Render::DepthMath::ndcDepth(nearPlane, far, 0.0));
        EXPECT_EQ(depthValue(far, -5.0), Render::DepthMath::ndcDepth(nearPlane, far, -5.0));
        EXPECT_EQ(depthValue(nearPlane, 10.0), Render::DepthMath::ndcDepth(nearPlane, nearPlane, 10.0));
        EXPECT_EQ(depthValue(1.0, 10.0), Render::DepthMath::ndcDepth(nearPlane, 1.0, 10.0));
    }

    TEST(RenderPointShadowMath, depthValueHandComputedPoint)
    {
        // n (f - d) / ((f - n) d) with n = 4, f = 104, d = 20: 4 * 84 / (100 * 20) = 0.168.
        EXPECT_NEAR(depthValue(104.0, 20.0), 0.168, 1e-12);
        EXPECT_NEAR(depthValue(104.0, 8.0), 4.0 * 96.0 / (100.0 * 8.0), 1e-12); // 0.48
    }

    TEST(RenderPointShadowMath, texelWorldSizeIsTwoDistanceTanHalfOverResolution)
    {
        EXPECT_NEAR(texelWorldSize(100.0, 1.0, 512.0), 200.0 / 512.0, 1e-12);
        EXPECT_NEAR(texelWorldSize(10.0, 1.007874, 256.0), 2.0 * 10.0 * 1.007874 / 256.0, 1e-12);
        EXPECT_NEAR(texelWorldSize(0.0, 1.0, 512.0), 0.0, 1e-12);
        // Linear in distance, inverse in resolution.
        EXPECT_NEAR(texelWorldSize(30.0, 1.1, 512.0), 3.0 * texelWorldSize(10.0, 1.1, 512.0), 1e-12);
        EXPECT_NEAR(texelWorldSize(30.0, 1.1, 1024.0), 0.5 * texelWorldSize(30.0, 1.1, 512.0), 1e-12);
    }

    TEST(RenderPointShadowMath, theDefaultBiasIsOneAndOneTimesOnePointFive)
    {
        const Bias bias;
        EXPECT_EQ(bias.normalOffsetTexels, 1.0);
        EXPECT_EQ(bias.depthBiasTexels, 1.0);
        EXPECT_EQ(bias.scale, 1.5);
    }

    TEST(RenderPointShadowMath, offsetReceiverMovesAlongTheNormalByScaleTimesTheTexels)
    {
        const double tanHalf = 1.007874;
        const double resolution = 512.0;
        Bias bias;
        bias.normalOffsetTexels = 0.8;
        bias.depthBiasTexels = 3.0; // must not matter here
        bias.scale = 2.5;
        const V3 toReceiver = v3(30.0, 12.0, -7.0);
        const V3 normal = unit3(v3(1.0, -1.0, 0.5));
        const double distance = 30.0; // face 0: the x component
        const double amount = bias.scale * bias.normalOffsetTexels * refTexel(distance, tanHalf, resolution);
        const V3 expected = add3(toReceiver, mul3(normal, amount));
        expectNear3(offsetReceiver(toReceiver, normal, tanHalf, resolution, bias), expected, 1e-12);

        // The texel size is that of the receiver's own major-axis distance, whatever the face.
        const V3 other = v3(-3.0, -4.0, -50.0);
        const double amount2 = bias.scale * bias.normalOffsetTexels * refTexel(50.0, tanHalf, resolution);
        expectNear3(
            offsetReceiver(other, normal, tanHalf, resolution, bias), add3(other, mul3(normal, amount2)), 1e-12);
    }

    TEST(RenderPointShadowMath, offsetReceiverWithZeroScaleOrZeroTexelsChangesNothing)
    {
        const V3 toReceiver = v3(5.0, -9.0, 20.0);
        const V3 normal = v3(0.0, 0.0, -1.0);
        Bias bias;
        bias.scale = 0.0;
        expectNear3(offsetReceiver(toReceiver, normal, 1.0, 512.0, bias), toReceiver, 1e-15);
        bias.scale = 1.5;
        bias.normalOffsetTexels = 0.0;
        expectNear3(offsetReceiver(toReceiver, normal, 1.0, 512.0, bias), toReceiver, 1e-15);
    }

    TEST(RenderPointShadowMath, offsetReceiverGrowsWithDistanceAndShrinksWithResolution)
    {
        const Bias bias;
        const V3 normal = v3(0.0, 1.0, 0.0);
        const double nearShift = offsetReceiver(v3(10.0, 0.0, 0.0), normal, 1.0, 512.0, bias).y;
        const double farShift = offsetReceiver(v3(100.0, 0.0, 0.0), normal, 1.0, 512.0, bias).y;
        const double fineShift = offsetReceiver(v3(100.0, 0.0, 0.0), normal, 1.0, 1024.0, bias).y;
        EXPECT_NEAR(farShift, 10.0 * nearShift, 1e-12);
        EXPECT_NEAR(fineShift, 0.5 * farShift, 1e-12);
        EXPECT_GT(nearShift, 0.0);
    }

    TEST(RenderPointShadowMath, compareDepthIsTheDepthOfTheDistanceMinusTheBias)
    {
        const double tanHalf = 1.007874;
        const double resolution = 512.0;
        const double far = 200.0;
        Bias bias;
        bias.normalOffsetTexels = 9.0; // must not matter here
        bias.depthBiasTexels = 0.75;
        bias.scale = 2.0;
        for (const double distance : { 6.0, 20.0, 77.0, 190.0 })
        {
            const double shifted
                = distance - bias.scale * bias.depthBiasTexels * refTexel(distance, tanHalf, resolution);
            EXPECT_NEAR(compareDepth(far, distance, tanHalf, resolution, bias),
                Render::DepthMath::ndcDepth(nearPlane, far, shifted), 1e-12)
                << distance;
            // The bias moves the compared point towards the light, so the value is larger than the unbiased one.
            EXPECT_GT(compareDepth(far, distance, tanHalf, resolution, bias), depthValue(far, distance)) << distance;
        }
    }

    TEST(RenderPointShadowMath, compareDepthNeverGoesInsideTheNearPlane)
    {
        Bias bias;
        bias.depthBiasTexels = 6.0;
        bias.scale = 6.0;
        // At distance 4.5 the bias (6 * 6 * 2 * 4.5 * 1 / 128 = 2.53) would put the point at 1.97: clamped to the near
        // plane, whose depth is 1.
        EXPECT_NEAR(compareDepth(100.0, 4.5, 1.0, 128.0, bias), depthValue(100.0, nearPlane), 1e-12);
        EXPECT_NEAR(compareDepth(100.0, 4.5, 1.0, 128.0, bias), 1.0, 1e-12);
        // A distance already inside the near plane: the clamp still applies.
        EXPECT_NEAR(compareDepth(100.0, 2.0, 1.0, 128.0, bias), 1.0, 1e-12);
    }

    // --- The tangent basis and the Poisson disk ----------------------------------------------------------------------

    void expectBasisFor(const V3& normal, const char* label)
    {
        V3 t1 = v3(7, 7, 7); // garbage in: the outputs must be overwritten, not accumulated
        V3 t2 = v3(-3, 5, 11);
        tangentBasis(normal, t1, t2);
        EXPECT_NEAR(len3(t1), 1.0, 1e-12) << label;
        EXPECT_NEAR(len3(t2), 1.0, 1e-12) << label;
        EXPECT_NEAR(dot3(t1, t2), 0.0, 1e-12) << label;
        EXPECT_NEAR(dot3(t1, normal), 0.0, 1e-12) << label;
        EXPECT_NEAR(dot3(t2, normal), 0.0, 1e-12) << label;
        expectNear3(cross3(t1, t2), normal, 1e-12);
    }

    TEST(RenderPointShadowMath, tangentBasisIsOrthonormalAndRightHandedAtTheAxesAndPoles)
    {
        for (const double sign : { 1.0, -1.0 })
        {
            expectBasisFor(v3(sign, 0, 0), "x axis");
            expectBasisFor(v3(0, sign, 0), "y axis");
            expectBasisFor(v3(0, 0, sign), "z axis");
        }
        expectBasisFor(unit3(v3(1e-9, 1e-9, 1.0)), "near +z pole");
        expectBasisFor(unit3(v3(1e-9, -1e-9, -1.0)), "near -z pole");
        expectBasisFor(unit3(v3(1.0, 1.0, 1.0)), "diagonal");
        expectBasisFor(unit3(v3(-1.0, 2.0, -3.0)), "skew");
    }

    TEST(RenderPointShadowMath, tangentBasisIsOrthonormalAndRightHandedForRandomNormals)
    {
        Rng rng(0x5eed0008);
        for (int i = 0; i < 1000; ++i)
        {
            expectBasisFor(rng.direction(), "random");
        }
    }

    TEST(RenderPointShadowMath, tangentBasisIsDeterministic)
    {
        Rng rng(0x5eed0009);
        for (int i = 0; i < 200; ++i)
        {
            const V3 normal = rng.direction();
            V3 a1;
            V3 a2;
            V3 b1;
            V3 b2;
            tangentBasis(normal, a1, a2);
            tangentBasis(normal, b1, b2);
            expectNear3(a1, b1, 0.0);
            expectNear3(a2, b2, 0.0);
            EXPECT_NEAR(len3(a1), 1.0, 1e-12); // identical outputs are only worth something if they are a basis
            EXPECT_NEAR(len3(a2), 1.0, 1e-12);
        }
    }

    TEST(RenderPointShadowMath, tangentBasisIsContinuousAwayFromTheOneUsualSingularity)
    {
        // "Continuous (no branch at the poles other than the usual one)": a basis in the family of Frisvad / Duff has a
        // single discontinuity, at one pole. Away from the -z pole (n.z > -0.9) two normals 1e-6 apart must have
        // tangents that are close, and the +z pole and the equator must not jump.
        Rng rng(0x5eed000a);
        int checked = 0;
        for (int i = 0; i < 3000; ++i)
        {
            const V3 n1 = rng.direction();
            if (n1.z < -0.9)
            {
                continue;
            }
            const V3 n2 = unit3(add3(n1, mul3(rng.direction(), 1e-6)));
            V3 a1;
            V3 a2;
            V3 b1;
            V3 b2;
            tangentBasis(n1, a1, a2);
            tangentBasis(n2, b1, b2);
            EXPECT_LT(len3(sub3(a1, b1)), 1e-3) << i;
            EXPECT_LT(len3(sub3(a2, b2)), 1e-3) << i;
            EXPECT_NEAR(len3(a1), 1.0, 1e-12) << i; // a continuous field of zero vectors is not a basis
            EXPECT_NEAR(len3(b2), 1.0, 1e-12) << i;
            ++checked;
        }
        EXPECT_GT(checked, 2000);
        // Walking over the +z pole itself.
        V3 previous1;
        V3 previous2;
        tangentBasis(unit3(v3(-1e-3, 0.0, 1.0)), previous1, previous2);
        for (int step = -9; step <= 10; ++step)
        {
            V3 t1;
            V3 t2;
            tangentBasis(unit3(v3(step * 1e-4, 0.0, 1.0)), t1, t2);
            EXPECT_LT(len3(sub3(t1, previous1)), 0.01) << step;
            EXPECT_NEAR(len3(t1), 1.0, 1e-12) << step;
            previous1 = t1;
        }
    }

    TEST(RenderPointShadowMath, pcfTapIsTheSunsPoissonDisk)
    {
        // files/shaders/lib/view/shadow.slang `poissonDisk`.
        const double table[16][2] = { { -0.94201624, -0.39906216 }, { 0.94558609, -0.76890725 },
            { -0.094184101, -0.92938870 }, { 0.34495938, 0.29387760 }, { -0.91588581, 0.45771432 },
            { -0.81544232, -0.87912464 }, { -0.38277543, 0.27676845 }, { 0.97484398, 0.75648379 },
            { 0.44323325, -0.97511554 }, { 0.53742981, -0.47373420 }, { -0.26496911, -0.41893023 },
            { 0.79197514, 0.19090188 }, { -0.24188840, 0.99706507 }, { -0.81409955, 0.91437590 },
            { 0.19984126, 0.78641367 }, { 0.14383161, -0.14100790 } };
        for (int i = 0; i < 16; ++i)
        {
            const vsg::dvec2 tap = pcfTap(i);
            EXPECT_DOUBLE_EQ(tap.x, table[i][0]) << i;
            EXPECT_DOUBLE_EQ(tap.y, table[i][1]) << i;
        }
    }

    TEST(RenderPointShadowMath, pcfTapClampsTheIndex)
    {
        const vsg::dvec2 first = pcfTap(0);
        const vsg::dvec2 last = pcfTap(15);
        for (const int index : { -1, -100, std::numeric_limits<int>::min() })
        {
            EXPECT_DOUBLE_EQ(pcfTap(index).x, first.x) << index;
            EXPECT_DOUBLE_EQ(pcfTap(index).y, first.y) << index;
        }
        for (const int index : { 16, 17, 1000, std::numeric_limits<int>::max() })
        {
            EXPECT_DOUBLE_EQ(pcfTap(index).x, last.x) << index;
            EXPECT_DOUBLE_EQ(pcfTap(index).y, last.y) << index;
        }
        EXPECT_NE(first.x, last.x);
    }

    // --- visibility: the composition of the pieces
    // --------------------------------------------------------------------

    TEST(RenderPointShadowMath, aMapWithNothingInItLightsEverythingAndAMapFullOfNearGeometryLightsNothing)
    {
        const double resolution = 512.0;
        const double tanHalf = tanHalfFov(defaultGuardTexels, resolution);
        const Bias bias;
        const V3 normal = v3(-1.0, 0.0, 0.0);
        const auto empty = [](int, double, double) { return 0.0; };
        const auto full = [](int, double, double) { return 1.0; };
        for (const int taps : { 1, 4, 8, 16 })
        {
            EXPECT_NEAR(
                visibility(v3(30, 2, -3), normal, 110.0, tanHalf, resolution, bias, taps, 1.5, empty), 1.0, 1e-12);
            EXPECT_NEAR(
                visibility(v3(30, 2, -3), normal, 110.0, tanHalf, resolution, bias, taps, 1.5, full), 0.0, 1e-12);
        }
    }

    TEST(RenderPointShadowMath, visibilityIsTheFractionOfTapsThatAreLitAndCallsTheSamplerOncePerTap)
    {
        const double resolution = 512.0;
        const double tanHalf = tanHalfFov(defaultGuardTexels, resolution);
        const Bias bias;
        const V3 normal = unit3(v3(-1.0, 0.2, 0.1));
        for (const int taps : { 1, 3, 8, 16 })
        {
            for (int litTaps = 0; litTaps <= taps; ++litTaps)
            {
                int calls = 0;
                const auto sampler = [&](int, double, double) {
                    const bool lit = calls < litTaps;
                    ++calls;
                    return lit ? 0.0 : 1.0; // 0 = nothing stored (lit), 1 = occluder at the near plane (shadowed)
                };
                const double result
                    = visibility(v3(40, 3, 1), normal, 110.0, tanHalf, resolution, bias, taps, 1.5, sampler);
                EXPECT_EQ(calls, taps);
                EXPECT_NEAR(result, static_cast<double>(litTaps) / taps, 1e-12)
                    << taps << " taps, " << litTaps << " lit";
            }
        }
    }

    TEST(RenderPointShadowMath, visibilityTapsSitWhereTheDesignPutsThem)
    {
        // Step 2 and 3 of the receiver: the normal offset, then the Poisson offsets in the plane of the tangent basis,
        // scaled by pcfRadiusTexels * texel(d0), d0 = the major-axis distance of the receiver itself.
        const double resolution = 256.0;
        const double tanHalf = tanHalfFov(defaultGuardTexels, resolution);
        Bias bias;
        bias.normalOffsetTexels = 0.7;
        bias.depthBiasTexels = 1.3;
        bias.scale = 2.0;
        const double pcfRadius = 2.0;
        const V3 toReceiver = v3(30.0, 12.0, -7.0);
        const V3 normal = unit3(v3(-1.0, 0.4, 0.3));
        const int taps = 12;

        const double d0 = 30.0;
        const double texel = refTexel(d0, tanHalf, resolution);
        const V3 centre = add3(toReceiver, mul3(normal, bias.scale * bias.normalOffsetTexels * texel));
        V3 t1;
        V3 t2;
        tangentBasis(normal, t1, t2);
        std::vector<V3> expected;
        for (int i = 0; i < taps; ++i)
        {
            const vsg::dvec2 offset = pcfTap(i);
            expected.push_back(
                add3(centre, add3(mul3(t1, offset.x * pcfRadius * texel), mul3(t2, offset.y * pcfRadius * texel))));
        }

        std::vector<bool> matched(expected.size(), false);
        int calls = 0;
        const auto sampler = [&](int face, double u, double v) {
            ++calls;
            const V3 ray = unit3(refRay(face, u, v, tanHalf));
            bool found = false;
            for (std::size_t k = 0; k < expected.size(); ++k)
            {
                if (!matched[k] && refFaceOf(expected[k]) == face && len3(sub3(ray, unit3(expected[k]))) < 1e-9)
                {
                    matched[k] = true;
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found) << "a tap at face " << face << " (" << u << ", " << v
                               << ") is not one of the design's taps";
            return 0.0;
        };
        visibility(toReceiver, normal, 110.0, tanHalf, resolution, bias, taps, pcfRadius, sampler);
        EXPECT_EQ(calls, taps);
        EXPECT_EQ(std::count(matched.begin(), matched.end(), true), taps);
    }

    TEST(RenderPointShadowMath, visibilityComparesTheBiasedDepthWithGreaterAsTheCompareOp)
    {
        // With the radius 0 every tap is the offset receiver itself. The tap is lit when compareDepth > stored.
        const double resolution = 512.0;
        const double tanHalf = tanHalfFov(defaultGuardTexels, resolution);
        const double far = 110.0;
        const Bias bias;
        const V3 toReceiver = v3(-6.0, 45.0, 3.0);
        const V3 normal = unit3(v3(0.1, -1.0, 0.0));

        const double d0 = 45.0;
        const V3 shifted
            = add3(toReceiver, mul3(normal, bias.scale * bias.normalOffsetTexels * refTexel(d0, tanHalf, resolution)));
        const RefCoord coord = refProject(shifted, tanHalf);
        const double distance = std::max(
            coord.distance - bias.scale * bias.depthBiasTexels * refTexel(coord.distance, tanHalf, resolution),
            nearPlane);
        const double compare = Render::DepthMath::ndcDepth(nearPlane, far, distance);

        int faceSeen = -1;
        double uSeen = -1.0;
        double vSeen = -1.0;
        const auto justBehind = [&](int face, double u, double v) {
            faceSeen = face;
            uSeen = u;
            vSeen = v;
            return compare + 1e-9; // the stored occluder is a little nearer than the receiver: shadowed
        };
        const auto justInFront = [&](int, double, double) { return compare - 1e-9; };
        EXPECT_NEAR(visibility(toReceiver, normal, far, tanHalf, resolution, bias, 8, 0.0, justBehind), 0.0, 1e-12);
        EXPECT_EQ(faceSeen, coord.face);
        EXPECT_NEAR(uSeen, coord.u, 1e-9);
        EXPECT_NEAR(vSeen, coord.v, 1e-9);
        EXPECT_NEAR(visibility(toReceiver, normal, far, tanHalf, resolution, bias, 8, 0.0, justInFront), 1.0, 1e-12);
        // Equal depth is not lit (the compare is a strict GREATER).
        const auto equal = [&](int, double, double) { return compare; };
        EXPECT_NEAR(visibility(toReceiver, normal, far, tanHalf, resolution, bias, 8, 0.0, equal), 0.0, 1e-12);
    }

    // --- The geometric proof: a software shadow map of a sphere, a wall as the receiver
    // ---------------------------------

    constexpr double proofResolution = 512.0;
    constexpr double proofFar = 110.0; // 1.1 * a light radius of 100
    constexpr double proofSphereRadius = 2.0;
    constexpr double proofSphereDistance = 10.0;
    constexpr double proofWallDistance = 20.0;
    constexpr int proofTaps = 16;
    constexpr double proofPcfRadiusTexels = 1.5;
    // The spec's tolerance is "more than two texels"; the filter radius (1.5) and the texel quantisation of the
    // software map (0.5) already add up to two, and the normal offset moves the sample sideways a little on an oblique
    // wall, so the shadow and the light are asserted at three texels from the outline, and the outline itself within
    // one texel.
    constexpr double proofMarginTexels = 3.0;

    // The stored depth of the software map of a sphere centred at `centre` (light at the origin): the texel's centre
    // ray against the sphere, the depth of the major-axis component of the hit, 0 where the ray misses.
    double sphereMapDepth(const V3& centre, double radius, double tanHalf, int face, double u, double v)
    {
        const V3 ray = refRay(face, texelCentre(u, proofResolution), texelCentre(v, proofResolution), tanHalf);
        const double a = dot3(ray, ray);
        const double b = dot3(ray, centre);
        const double c = dot3(centre, centre) - radius * radius;
        const double discriminant = b * b - a * c;
        if (discriminant < 0.0)
        {
            return 0.0;
        }
        const double root = std::sqrt(discriminant);
        double s = (b - root) / a;
        if (s <= 0.0)
        {
            s = (b + root) / a;
        }
        if (s <= 0.0)
        {
            return 0.0;
        }
        // The hit is s * ray, and the ray has forward component 1, so s is the major-axis distance.
        return Render::DepthMath::ndcDepth(nearPlane, proofFar, s);
    }

    // texels per world unit when a point X on the wall moves along e, measured in the map of X's own face
    // (the derivative of u = 0.5 + 0.5 R / (d t) and of v).
    double texelsPerUnit(const V3& x, const V3& e, double tanHalf)
    {
        const RefFace& axes = refFace(refFaceOf(x));
        const double d = dot3(x, axes.forward);
        const double gu = dot3(e, axes.right) - dot3(x, axes.right) / d * dot3(e, axes.forward);
        const double gv = dot3(e, axes.up) - dot3(x, axes.up) / d * dot3(e, axes.forward);
        return std::sqrt(gu * gu + gv * gv) / refTexel(d, tanHalf, proofResolution);
    }

    // One layout: the tangent ray `o` of the shadow's outline and the axis `k` it is rotated about to get the sphere's
    // direction c. The wall is the plane c . X = D with its normal -c; the shadow on it is the disc |s| < D tan(alpha)
    // along e = k x c. The outline point on the `o` side is X(-D tan(alpha)).
    void checkSphereLayout(const V3& outline, const V3& axis, const char* label)
    {
        const double tanHalf = refTanHalf(defaultGuardTexels, proofResolution);
        const double alpha = std::asin(proofSphereRadius / proofSphereDistance);
        const V3 o = unit3(outline);
        const V3 k = unit3(axis);
        ASSERT_NEAR(dot3(o, k), 0.0, 1e-12) << label;
        const V3 c = add3(mul3(o, std::cos(alpha)), mul3(cross3(k, o), std::sin(alpha)));
        const V3 e = cross3(k, c);
        const V3 centre = mul3(c, proofSphereDistance);
        const V3 normal = mul3(c, -1.0);
        const double halfWidth = proofWallDistance * std::tan(alpha);
        const auto wallPoint = [&](double s) { return add3(mul3(c, proofWallDistance), mul3(e, s)); };

        // The outline point really is on the intended ray.
        const V3 outlinePoint = wallPoint(-halfWidth);
        ASSERT_NEAR(dot3(unit3(outlinePoint), o), 1.0, 1e-12) << label;

        const Bias bias;
        const auto stored = [&](int face, double u, double v) {
            return sphereMapDepth(centre, proofSphereRadius, tanHalf, face, u, v);
        };
        const auto lit = [&](double s) {
            return visibility(wallPoint(s), normal, proofFar, tanHalf, proofResolution, bias, proofTaps,
                proofPcfRadiusTexels, stored);
        };

        const double perUnit = texelsPerUnit(outlinePoint, e, tanHalf);
        ASSERT_GT(perUnit, 0.3) << label;
        const double margin = proofMarginTexels / perUnit;
        // Across a cube edge the far outline can lie in another face, where a texel covers more of the wall: its margin
        // is measured at that outline point, not at this one (a test fix after the first run, task 0077 stage a log).
        const double perUnitFar = texelsPerUnit(wallPoint(halfWidth), e, tanHalf);
        ASSERT_GT(perUnitFar, 0.3) << label;
        const double marginFar = proofMarginTexels / perUnitFar;

        // Well inside the shadow, and the centre of it, and the other side's inside.
        EXPECT_NEAR(lit(0.0), 0.0, 1e-12) << label << " centre";
        EXPECT_NEAR(lit(-halfWidth + margin), 0.0, 1e-12) << label << " inside, near the o outline";
        EXPECT_NEAR(lit(halfWidth - marginFar), 0.0, 1e-12) << label << " inside, near the far outline";
        // Well outside it, on both sides.
        EXPECT_NEAR(lit(-halfWidth - margin), 1.0, 1e-12) << label << " outside, near the o outline";
        EXPECT_NEAR(lit(halfWidth + marginFar), 1.0, 1e-12) << label << " outside, near the far outline";
        EXPECT_NEAR(lit(-3.0 * halfWidth), 1.0, 1e-12) << label << " far outside";
        // Perpendicular to the scan line the shadow is a disc: a point off the disc along k is lit, one inside is not.
        const V3 inDisc = add3(mul3(c, proofWallDistance), mul3(k, 0.5 * halfWidth));
        const V3 offDisc = add3(mul3(c, proofWallDistance), mul3(k, 1.0 * halfWidth + margin));
        EXPECT_NEAR(visibility(inDisc, normal, proofFar, tanHalf, proofResolution, bias, proofTaps,
                        proofPcfRadiusTexels, stored),
            0.0, 1e-12)
            << label;
        EXPECT_NEAR(visibility(offDisc, normal, proofFar, tanHalf, proofResolution, bias, proofTaps,
                        proofPcfRadiusTexels, stored),
            1.0, 1e-12)
            << label;

        // The edge: scan across the analytic outline in steps of 1/50 texel and find where the lit fraction crosses
        // 0.5.
        const double step = 0.02 / perUnit;
        bool found = false;
        double crossing = 0.0;
        double previousS = -halfWidth - 4.0 / perUnit;
        double previousLit = lit(previousS);
        EXPECT_GT(previousLit, 0.5) << label << " the scan starts in the light";
        for (int i = 1; i <= 400 && !found; ++i)
        {
            const double s = previousS + step;
            const double value = lit(s);
            if (previousLit >= 0.5 && value < 0.5)
            {
                // Linear interpolation between the two samples.
                crossing = previousS + step * (previousLit - 0.5) / (previousLit - value);
                found = true;
            }
            previousS = s;
            previousLit = value;
        }
        ASSERT_TRUE(found) << label << " no crossing of 0.5 within four texels of the outline";
        EXPECT_LE(std::fabs(crossing - (-halfWidth)) * perUnit, 1.0)
            << label << " the edge is " << std::fabs(crossing + halfWidth) * perUnit << " texels from the outline";
    }

    TEST(RenderPointShadowMath, theShadowOfASphereOnAWallHasItsEdgeWithinOneTexelAtTheCentreOfEveryFace)
    {
        for (int face = 0; face < 6; ++face)
        {
            const RefFace& axes = refFace(face);
            // The outline ray is the face's axis, the rotation is about the face's up axis (perpendicular to it).
            checkSphereLayout(axes.forward, axes.up, "face centre");
            // And about the right axis, for the other direction in the map.
            checkSphereLayout(axes.forward, axes.right, "face centre, vertical");
        }
    }

    TEST(RenderPointShadowMath, theShadowOfASphereOnAWallHasItsEdgeWithinOneTexelNearAFaceEdge)
    {
        const double toRadians = 3.14159265358979323846 / 180.0;
        // Five degrees from the 45 degree line between +X and +Y, inside the +X face.
        checkSphereLayout(
            v3(std::cos(40.0 * toRadians), std::sin(40.0 * toRadians), 0.0), v3(0, 0, 1), "near the x/y edge");
        // The same on the other side of the line, inside the +Y face.
        checkSphereLayout(
            v3(std::cos(50.0 * toRadians), std::sin(50.0 * toRadians), 0.0), v3(0, 0, 1), "near the y/x edge");
        // Near the +X / +Z edge, rotating in the vertical plane.
        checkSphereLayout(
            v3(std::cos(40.0 * toRadians), 0.0, std::sin(40.0 * toRadians)), v3(0, -1, 0), "near the x/z edge");
        // Within the guard band: 45.1 degrees, still the +Y face by the major axis rule.
        checkSphereLayout(
            v3(std::cos(44.9 * toRadians), std::sin(44.9 * toRadians), 0.0), v3(0, 0, 1), "at the guard band");
    }

    TEST(RenderPointShadowMath, theShadowOfASphereOnAWallHasItsEdgeWithinOneTexelAcrossACubeEdge)
    {
        // The outline ray is exactly on the 45 degree line: the lit and shadowed sides of the edge are in different
        // faces.
        checkSphereLayout(v3(1, 1, 0), v3(0, 0, 1), "across the x/y edge");
        checkSphereLayout(v3(1, 0, 1), v3(0, -1, 0), "across the x/z edge");
        checkSphereLayout(v3(0, 1, 1), v3(1, 0, 0), "across the y/z edge");
        checkSphereLayout(v3(-1, -1, 0), v3(0, 0, -1), "across the -x/-y edge");
        // Through a cube corner: all three major axes tie.
        checkSphereLayout(v3(1, 1, 1), v3(1, -1, 0), "through the corner");
        // A skew layout in no particular relation to the faces.
        const V3 skew = v3(0.3, -0.5, 0.81);
        checkSphereLayout(skew, cross3(skew, v3(0, 0, 1)), "skew");
    }

    // --- The thin wall: no leak
    // -----------------------------------------------------------------------------------------

    // A slab perpendicular to the unit axis a between the light (origin) and the receiver: front face at a . X = 20,
    // far side at 22. The software map stores the front face. The receiver plane is `behind` units past the far side.
    double slabVisibility(const V3& a, double behind, double scale, double resolution, double i, double j, int taps)
    {
        const double tanHalf = refTanHalf(defaultGuardTexels, resolution);
        const V3 helper = std::fabs(a.z) < 0.9 ? v3(0, 0, 1) : v3(1, 0, 0);
        const V3 e1 = unit3(cross3(a, helper));
        const V3 e2 = cross3(a, e1);
        const double frontFace = 20.0;
        const double farSide = 22.0;
        const V3 receiver = add3(mul3(a, farSide + behind), add3(mul3(e1, i), mul3(e2, j)));
        Bias bias;
        bias.scale = scale;
        const auto stored = [&](int face, double u, double v) {
            const V3 ray = refRay(face, texelCentre(u, resolution), texelCentre(v, resolution), tanHalf);
            const double along = dot3(ray, a);
            if (along <= 1e-9)
            {
                return 0.0;
            }
            return Render::DepthMath::ndcDepth(nearPlane, proofFar, frontFace / along);
        };
        return visibility(receiver, mul3(a, -1.0), proofFar, tanHalf, resolution, bias, taps, 1.5, stored);
    }

    TEST(RenderPointShadowMath, aTwoUnitSlabBetweenTheLightAndTheReceiverDoesNotLeakAtDefaultResolutionAndBias)
    {
        const std::array<V3, 6> axes = { v3(1, 0, 0), v3(0, -1, 0), v3(0, 0, 1), unit3(v3(1, 1, 0.5)),
            unit3(v3(-0.3, 0.5, -0.8)), unit3(v3(1, 1, 1)) };
        for (const V3& a : axes)
        {
            for (int i = -2; i <= 2; ++i)
            {
                for (int j = -2; j <= 2; ++j)
                {
                    // One unit behind the slab's far side, receiver points spread over the wall.
                    const double lit = slabVisibility(a, 1.0, 1.5, 512.0, 3.0 * i, 3.0 * j, 8);
                    EXPECT_NEAR(lit, 0.0, 1e-12)
                        << "axis " << a.x << " " << a.y << " " << a.z << " point " << i << "," << j;
                }
            }
        }
    }

    TEST(RenderPointShadowMath, aTwoUnitSlabDoesNotLeakAtTheLargestBiasAtDefaultResolution)
    {
        // Bias 6 texels at 512: the biased points move 6 * 2 * 0.09 = 1.1 units, less than the 3 units to the slab's
        // front.
        for (const V3& a : { v3(1, 0, 0), unit3(v3(1, 1, 0.5)) })
        {
            EXPECT_NEAR(slabVisibility(a, 1.0, 6.0, 512.0, 0.0, 0.0, 8), 0.0, 1e-12);
            EXPECT_NEAR(slabVisibility(a, 1.0, 6.0, 512.0, 4.0, -3.0, 16), 0.0, 1e-12);
        }
    }

    TEST(RenderPointShadowMath, theLeakDistanceBehindASlabFollowsTheBiasFormulaAtBiasSixAndLowResolution)
    {
        // On the axis the stored depth is the slab's front face at 20 and every tap has the receiver's distance. The
        // receiver at 22 + g is lit when its biased compare distance P - scale * texel(P) is below 20, with
        // P = (22 + g) - scale * texel(22 + g) (the normal offset moves it towards the light). That is the leak.
        for (const double resolution : { 128.0, 256.0 })
        {
            const double scale = 6.0;
            const double tanHalf = refTanHalf(defaultGuardTexels, resolution);
            const auto compareDistance = [&](double g) {
                const double distance = 22.0 + g;
                const double shifted = distance - scale * refTexel(distance, tanHalf, resolution);
                return shifted - scale * refTexel(shifted, tanHalf, resolution);
            };
            // Bisection for the gap where the compare distance reaches 20.
            double low = 0.0;
            double high = 20.0;
            ASSERT_LT(compareDistance(low), 20.0) << "the layout must leak at a gap of 0 for this resolution";
            ASSERT_GT(compareDistance(high), 20.0);
            for (int i = 0; i < 80; ++i)
            {
                const double mid = 0.5 * (low + high);
                if (compareDistance(mid) < 20.0)
                {
                    low = mid;
                }
                else
                {
                    high = mid;
                }
            }
            const double predicted = low;

            // Measure it with the visibility function: scan the gap and find where the receiver turns from lit to dark.
            double measured = -1.0;
            for (double g = 0.0; g <= 20.0; g += 0.0005)
            {
                if (slabVisibility(v3(1, 0, 0), g, scale, resolution, 0.0, 0.0, 8) < 0.5)
                {
                    measured = g;
                    break;
                }
            }
            ASSERT_GE(measured, 0.0);
            EXPECT_NEAR(measured, predicted, 0.002) << "resolution " << resolution;
            if (resolution == 128.0)
            {
                // At 1 unit behind it leaks at these settings; this is the leak distance the spec asks to be shown.
                EXPECT_GT(predicted, 1.0);
                EXPECT_NEAR(slabVisibility(v3(1, 0, 0), 1.0, scale, resolution, 0.0, 0.0, 8), 1.0, 1e-12);
            }
        }
    }

    TEST(RenderPointShadowMath, aLargerBiasNeverShrinksTheLeak)
    {
        // The receiver is shadowed from some gap onward; a larger bias moves that gap away from the slab.
        const double resolution = 128.0;
        double previous = -1.0;
        double first = -1.0;
        for (const double scale : { 0.25, 1.5, 3.0, 6.0 })
        {
            double gap = 40.0;
            for (double g = 0.0; g <= 40.0; g += 0.01)
            {
                if (slabVisibility(v3(1, 0, 0), g, scale, resolution, 0.0, 0.0, 4) < 0.5)
                {
                    gap = g;
                    break;
                }
            }
            EXPECT_GE(gap, previous) << "scale " << scale;
            previous = gap;
            if (first < 0.0)
            {
                first = gap;
            }
        }
        // At 128 texels the leak at bias 6 is about two units, and there is none at bias 0.25.
        EXPECT_GT(previous, first + 1.0);
    }
}
