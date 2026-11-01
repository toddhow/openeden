// Tests for the renderer-free rules of distant objects (task 0012, V4n). Written from
// apps/openmw/mwrender/distantobjectcells.hpp and OpenMW's objectpaging.cpp and components/misc/convert.hpp at
// 442eec6e2f only, without seeing the implementation.
//
// Conventions these tests rely on:
// - ActiveCellGrid covers centre +/- halfGrid inclusive on both axes, so halfGrid 0 is the centre cell alone.
// - "Sorted" is std::pair's ordering: by x, then y.
// - Whether a cell exactly viewDistance away is included is not specified, so no test places a cell on that boundary.
//   This includes zero view distance, where the camera's own cell is at distance 0: those tests check only that no
//   other cell appears, or leave the camera's cell out through the active grid.
// - Object rotation is objectpaging.cpp's nodeAttitude (lines 853-855), the same as convert.hpp's makeOsgQuat:
//   Quat(z, -Z) * Quat(y, -Y) * Quat(x, -X). extern/osg/osg/Quat's operator* multiplies so that the left factor is
//   applied to a vector first, and Quat's angle/axis form is a right-handed rotation about that axis. So a vector gets
//   Z's rotation first, then Y's, then X's, each by the negative angle. preMultTranslate, preMultRotate and
//   preMultScale (lines 862-865) then put scale first and translation last.
// - A rotation by -90 degrees about each axis, written out by hand for the tests below:
//   X: (x, y, z) -> (x, z, -y); Y: (x, y, z) -> (-z, y, x); Z: (x, y, z) -> (y, -x, z).

#include "apps/openmw/mwrender/distantobjectcells.hpp"

#include <components/esm/defs.hpp>

#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace
{
    using MWRender::ActiveCellGrid;
    using MWRender::distantObjectCells;
    using MWRender::distantObjectMatrix;
    using MWRender::distantObjectMaxDistance;
    using MWRender::isDistantObjectType;

    using Cells = std::vector<std::pair<int, int>>;

    constexpr float sCellSize = 100.f;
    constexpr double sEpsilon = 1e-5;
    constexpr float sQuarterTurn = std::numbers::pi_v<float> / 2.f;

    // An active grid too far away to touch any cell a test expects.
    const ActiveCellGrid sFarActive{ { 100000, 100000 }, 0 };

    bool containsCell(const Cells& cells, const std::pair<int, int>& cell)
    {
        return std::find(cells.begin(), cells.end(), cell) != cells.end();
    }

    void expectTransforms(const vsg::dmat4& matrix, const vsg::dvec3& in, const vsg::dvec3& expected)
    {
        const vsg::dvec3 out = matrix * in;
        EXPECT_NEAR(out.x, expected.x, sEpsilon);
        EXPECT_NEAR(out.y, expected.y, sEpsilon);
        EXPECT_NEAR(out.z, expected.z, sEpsilon);
    }

    vsg::dvec3 rotateAboutX(const vsg::dvec3& v, double angle)
    {
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        return vsg::dvec3(v.x, v.y * c - v.z * s, v.y * s + v.z * c);
    }

    vsg::dvec3 rotateAboutY(const vsg::dvec3& v, double angle)
    {
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        return vsg::dvec3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
    }

    vsg::dvec3 rotateAboutZ(const vsg::dvec3& v, double angle)
    {
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        return vsg::dvec3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
    }

    TEST(MWRenderActiveCellGridTest, zeroHalfGridContainsOnlyTheCentre)
    {
        const ActiveCellGrid grid{ { 5, -3 }, 0 };
        EXPECT_TRUE(grid.contains({ 5, -3 }));
        EXPECT_FALSE(grid.contains({ 6, -3 }));
        EXPECT_FALSE(grid.contains({ 4, -3 }));
        EXPECT_FALSE(grid.contains({ 5, -2 }));
        EXPECT_FALSE(grid.contains({ 5, -4 }));
    }

    TEST(MWRenderActiveCellGridTest, edgesAndCornersAreInsideAwayFromOrigin)
    {
        const ActiveCellGrid grid{ { 5, -3 }, 1 };
        EXPECT_TRUE(grid.contains({ 5, -3 }));
        EXPECT_TRUE(grid.contains({ 4, -3 }));
        EXPECT_TRUE(grid.contains({ 6, -3 }));
        EXPECT_TRUE(grid.contains({ 5, -4 }));
        EXPECT_TRUE(grid.contains({ 5, -2 }));
        EXPECT_TRUE(grid.contains({ 4, -4 }));
        EXPECT_TRUE(grid.contains({ 4, -2 }));
        EXPECT_TRUE(grid.contains({ 6, -4 }));
        EXPECT_TRUE(grid.contains({ 6, -2 }));
    }

    TEST(MWRenderActiveCellGridTest, cellsJustOutsideEachEdgeAreOutside)
    {
        const ActiveCellGrid grid{ { 5, -3 }, 1 };
        EXPECT_FALSE(grid.contains({ 3, -3 }));
        EXPECT_FALSE(grid.contains({ 7, -3 }));
        EXPECT_FALSE(grid.contains({ 5, -5 }));
        EXPECT_FALSE(grid.contains({ 5, -1 }));
        EXPECT_FALSE(grid.contains({ 7, -1 }));
        // The origin is not special.
        EXPECT_FALSE(grid.contains({ 0, 0 }));
    }

    TEST(MWRenderDistantObjectCellsTest, distanceIsToTheNearestPointOfTheSquareNotItsCentre)
    {
        // The camera is at the middle of cell (1, 1). Edge neighbours are 50 away (their centres are 100 away); corner
        // neighbours are about 70.7 away.
        const Cells cells = distantObjectCells(vsg::vec3(150.f, 150.f, 0.f), sCellSize, 60.f, sFarActive);
        const Cells expected{ { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, 2 }, { 2, 1 } };
        EXPECT_EQ(cells, expected);
    }

    TEST(MWRenderDistantObjectCellsTest, cornerNeighboursJoinOnceInReach)
    {
        const Cells cells = distantObjectCells(vsg::vec3(150.f, 150.f, 0.f), sCellSize, 80.f, sFarActive);
        const Cells expected{ { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 0 }, { 1, 1 }, { 1, 2 }, { 2, 0 }, { 2, 1 },
            { 2, 2 } };
        EXPECT_EQ(cells, expected);
    }

    TEST(MWRenderDistantObjectCellsTest, cameraOffCentreInItsCellReachesOnlyTheNearSides)
    {
        // Distances from (110, 160): (0, 1) 10, (1, 2) 40, (0, 2) about 41.2, (1, 0) 60, (0, 0) about 60.8, (2, 1) 90.
        const Cells cells = distantObjectCells(vsg::vec3(110.f, 160.f, 0.f), sCellSize, 45.f, sFarActive);
        const Cells expected{ { 0, 1 }, { 0, 2 }, { 1, 1 }, { 1, 2 } };
        EXPECT_EQ(cells, expected);
    }

    TEST(MWRenderDistantObjectCellsTest, leavesOutTheActiveGridOnTheCamera)
    {
        const ActiveCellGrid active{ { 1, 1 }, 0 };
        const Cells cells = distantObjectCells(vsg::vec3(150.f, 150.f, 0.f), sCellSize, 80.f, active);
        const Cells expected{ { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 0 }, { 1, 2 }, { 2, 0 }, { 2, 1 }, { 2, 2 } };
        EXPECT_EQ(cells, expected);
    }

    TEST(MWRenderDistantObjectCellsTest, leavesOutAnActiveGridNotCentredOnTheCamera)
    {
        const ActiveCellGrid active{ { 0, 0 }, 1 };
        const Cells cells = distantObjectCells(vsg::vec3(150.f, 150.f, 0.f), sCellSize, 80.f, active);
        const Cells expected{ { 0, 2 }, { 1, 2 }, { 2, 0 }, { 2, 1 }, { 2, 2 } };
        EXPECT_EQ(cells, expected);
    }

    TEST(MWRenderDistantObjectCellsTest, emptyWhenTheActiveGridCoversEverythingInReach)
    {
        const ActiveCellGrid active{ { 1, 1 }, 1 };
        EXPECT_TRUE(distantObjectCells(vsg::vec3(150.f, 150.f, 0.f), sCellSize, 80.f, active).empty());
    }

    TEST(MWRenderDistantObjectCellsTest, negativeCoordinatesRoundDownToTheCell)
    {
        // (-50, -150) lies in cell (-1, -2), which spans [-100, 0) x [-200, -100); truncating would give (0, -1).
        const Cells alone = distantObjectCells(vsg::vec3(-50.f, -150.f, 0.f), sCellSize, 20.f, sFarActive);
        EXPECT_EQ(alone, (Cells{ { -1, -2 } }));

        const Cells cells = distantObjectCells(vsg::vec3(-50.f, -150.f, 0.f), sCellSize, 60.f, sFarActive);
        const Cells expected{ { -2, -2 }, { -1, -3 }, { -1, -2 }, { -1, -1 }, { 0, -2 } };
        EXPECT_EQ(cells, expected);
    }

    TEST(MWRenderDistantObjectCellsTest, cellsAroundTheOriginAreMeasuredOnBothSides)
    {
        // From (10, 10): (-1, 0) and (0, -1) are 10 away, (-1, -1) about 14.1.
        const Cells closeReach = distantObjectCells(vsg::vec3(10.f, 10.f, 0.f), sCellSize, 12.f, sFarActive);
        EXPECT_EQ(closeReach, (Cells{ { -1, 0 }, { 0, -1 }, { 0, 0 } }));

        const Cells widerReach = distantObjectCells(vsg::vec3(10.f, 10.f, 0.f), sCellSize, 20.f, sFarActive);
        EXPECT_EQ(widerReach, (Cells{ { -1, -1 }, { -1, 0 }, { 0, -1 }, { 0, 0 } }));
    }

    TEST(MWRenderDistantObjectCellsTest, cameraAwayFromTheOrigin)
    {
        // The middle of cell (10, -5) at Morrowind's cell size: edge neighbours 4096 away, corners about 5793.
        constexpr float cellSize = 8192.f;
        const vsg::vec3 camera(10.f * cellSize + 4096.f, -5.f * cellSize + 4096.f, 0.f);
        const Cells cells = distantObjectCells(camera, cellSize, 4200.f, sFarActive);
        const Cells expected{ { 9, -5 }, { 10, -6 }, { 10, -5 }, { 10, -4 }, { 11, -5 } };
        EXPECT_EQ(cells, expected);
    }

    TEST(MWRenderDistantObjectCellsTest, cameraHeightDoesNotMatter)
    {
        const Cells ground = distantObjectCells(vsg::vec3(150.f, 150.f, 0.f), sCellSize, 60.f, sFarActive);
        EXPECT_EQ(distantObjectCells(vsg::vec3(150.f, 150.f, 100000.f), sCellSize, 60.f, sFarActive), ground);
        EXPECT_EQ(distantObjectCells(vsg::vec3(150.f, 150.f, -100000.f), sCellSize, 60.f, sFarActive), ground);
    }

    TEST(MWRenderDistantObjectCellsTest, zeroViewDistanceReachesNoOtherCell)
    {
        // Whether the camera's own cell counts at distance 0 is the boundary case, so it is allowed either way.
        const Cells cells = distantObjectCells(vsg::vec3(150.f, 150.f, 0.f), sCellSize, 0.f, sFarActive);
        EXPECT_LE(cells.size(), 1u);
        if (!cells.empty())
        {
            EXPECT_EQ(cells.front(), (std::pair<int, int>(1, 1)));
        }
    }

    TEST(MWRenderDistantObjectCellsTest, zeroViewDistanceWithTheCameraCellActiveIsEmpty)
    {
        const ActiveCellGrid active{ { 1, 1 }, 0 };
        EXPECT_TRUE(distantObjectCells(vsg::vec3(150.f, 150.f, 0.f), sCellSize, 0.f, active).empty());
    }

    TEST(MWRenderDistantObjectCellsTest, sortedWithoutDuplicatesAndMatchingABruteForceSearch)
    {
        // These fractions keep every squared distance away from 350 * 350, so no cell is on the boundary.
        const vsg::vec3 camera(123.5f, -77.25f, 0.f);
        constexpr float viewDistance = 350.f;
        const ActiveCellGrid active{ { 1, -1 }, 1 };

        const Cells cells = distantObjectCells(camera, sCellSize, viewDistance, active);

        EXPECT_TRUE(std::is_sorted(cells.begin(), cells.end()));
        EXPECT_TRUE(std::adjacent_find(cells.begin(), cells.end()) == cells.end());

        Cells expected;
        for (int x = -10; x <= 10; ++x)
        {
            for (int y = -10; y <= 10; ++y)
            {
                const double minX = x * static_cast<double>(sCellSize);
                const double minY = y * static_cast<double>(sCellSize);
                const double dx = std::max({ minX - camera.x, 0.0, camera.x - (minX + sCellSize) });
                const double dy = std::max({ minY - camera.y, 0.0, camera.y - (minY + sCellSize) });
                if (dx * dx + dy * dy < viewDistance * viewDistance && !active.contains({ x, y }))
                    expected.emplace_back(x, y);
            }
        }
        EXPECT_EQ(cells, expected);
        EXPECT_FALSE(containsCell(cells, { 1, -1 }));
        EXPECT_TRUE(containsCell(cells, { 3, -1 }));
    }

    TEST(MWRenderIsDistantObjectTypeTest, staticsActivatorsAndDoorsAreDrawnFromAfar)
    {
        EXPECT_TRUE(isDistantObjectType(static_cast<int>(ESM::REC_STAT)));
        EXPECT_TRUE(isDistantObjectType(static_cast<int>(ESM::REC_ACTI)));
        EXPECT_TRUE(isDistantObjectType(static_cast<int>(ESM::REC_DOOR)));
    }

    TEST(MWRenderIsDistantObjectTypeTest, containersActorsAndLightsAreNot)
    {
        // OpenMW's typeFilter passes containers only when not far.
        EXPECT_FALSE(isDistantObjectType(static_cast<int>(ESM::REC_CONT)));
        EXPECT_FALSE(isDistantObjectType(static_cast<int>(ESM::REC_NPC_)));
        EXPECT_FALSE(isDistantObjectType(static_cast<int>(ESM::REC_LIGH)));
    }

    TEST(MWRenderDistantObjectMaxDistanceTest, isRadiusTimesScaleOverMinSize)
    {
        EXPECT_FLOAT_EQ(distantObjectMaxDistance(1000.f, 1.f, 0.5f), 2000.f);
        EXPECT_FLOAT_EQ(distantObjectMaxDistance(3.f, 1.5f, 0.25f), 18.f);
        EXPECT_FLOAT_EQ(distantObjectMaxDistance(100.f, 2.f, 0.01f), 100.f * 2.f / 0.01f);
    }

    TEST(MWRenderDistantObjectMaxDistanceTest, growsWithScale)
    {
        const float one = distantObjectMaxDistance(50.f, 1.f, 0.125f);
        EXPECT_FLOAT_EQ(distantObjectMaxDistance(50.f, 2.f, 0.125f), 2.f * one);
        EXPECT_FLOAT_EQ(distantObjectMaxDistance(50.f, 0.5f, 0.125f), 0.5f * one);
    }

    TEST(MWRenderDistantObjectMaxDistanceTest, nonPositiveMinSizeNeverCulls)
    {
        EXPECT_EQ(distantObjectMaxDistance(100.f, 2.f, 0.f), std::numeric_limits<float>::max());
        EXPECT_EQ(distantObjectMaxDistance(100.f, 2.f, -0.5f), std::numeric_limits<float>::max());
    }

    TEST(MWRenderDistantObjectMatrixTest, zeroRotationUnitScaleAtOriginIsIdentity)
    {
        const vsg::dmat4 matrix = distantObjectMatrix(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(0.f, 0.f, 0.f), 1.f);
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                EXPECT_NEAR(matrix[column][row], column == row ? 1.0 : 0.0, sEpsilon);
    }

    TEST(MWRenderDistantObjectMatrixTest, translatesByThePosition)
    {
        const vsg::dmat4 matrix = distantObjectMatrix(vsg::vec3(10.f, -20.f, 30.f), vsg::vec3(0.f, 0.f, 0.f), 1.f);
        expectTransforms(matrix, vsg::dvec3(0, 0, 0), vsg::dvec3(10, -20, 30));
        expectTransforms(matrix, vsg::dvec3(1, 2, 3), vsg::dvec3(11, -18, 33));
    }

    TEST(MWRenderDistantObjectMatrixTest, scalesBeforeTranslating)
    {
        const vsg::dmat4 matrix = distantObjectMatrix(vsg::vec3(10.f, 20.f, 30.f), vsg::vec3(0.f, 0.f, 0.f), 2.f);
        expectTransforms(matrix, vsg::dvec3(0, 0, 0), vsg::dvec3(10, 20, 30));
        expectTransforms(matrix, vsg::dvec3(1, 2, 3), vsg::dvec3(12, 24, 36));
    }

    TEST(MWRenderDistantObjectMatrixTest, positiveXRotationTurnsYTowardsNegativeZ)
    {
        const vsg::dmat4 matrix = distantObjectMatrix(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(sQuarterTurn, 0.f, 0.f), 1.f);
        expectTransforms(matrix, vsg::dvec3(1, 0, 0), vsg::dvec3(1, 0, 0));
        expectTransforms(matrix, vsg::dvec3(0, 1, 0), vsg::dvec3(0, 0, -1));
        expectTransforms(matrix, vsg::dvec3(0, 0, 1), vsg::dvec3(0, 1, 0));
    }

    TEST(MWRenderDistantObjectMatrixTest, positiveYRotationTurnsXTowardsPositiveZ)
    {
        const vsg::dmat4 matrix = distantObjectMatrix(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(0.f, sQuarterTurn, 0.f), 1.f);
        expectTransforms(matrix, vsg::dvec3(0, 1, 0), vsg::dvec3(0, 1, 0));
        expectTransforms(matrix, vsg::dvec3(1, 0, 0), vsg::dvec3(0, 0, 1));
        expectTransforms(matrix, vsg::dvec3(0, 0, 1), vsg::dvec3(-1, 0, 0));
    }

    TEST(MWRenderDistantObjectMatrixTest, positiveZRotationTurnsXTowardsNegativeY)
    {
        const vsg::dmat4 matrix = distantObjectMatrix(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(0.f, 0.f, sQuarterTurn), 1.f);
        expectTransforms(matrix, vsg::dvec3(0, 0, 1), vsg::dvec3(0, 0, 1));
        expectTransforms(matrix, vsg::dvec3(1, 0, 0), vsg::dvec3(0, -1, 0));
        expectTransforms(matrix, vsg::dvec3(0, 1, 0), vsg::dvec3(1, 0, 0));
    }

    TEST(MWRenderDistantObjectMatrixTest, zIsAppliedBeforeX)
    {
        // Z: (1, 0, 0) -> (0, -1, 0); X: -> (0, 0, 1). The other order would give (0, -1, 0).
        const vsg::dmat4 matrix
            = distantObjectMatrix(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(sQuarterTurn, 0.f, sQuarterTurn), 1.f);
        expectTransforms(matrix, vsg::dvec3(1, 0, 0), vsg::dvec3(0, 0, 1));
    }

    TEST(MWRenderDistantObjectMatrixTest, yIsAppliedBeforeX)
    {
        // Y: (1, 0, 0) -> (0, 0, 1); X: -> (0, 1, 0). The other order would give (0, 0, 1).
        const vsg::dmat4 matrix
            = distantObjectMatrix(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(sQuarterTurn, sQuarterTurn, 0.f), 1.f);
        expectTransforms(matrix, vsg::dvec3(1, 0, 0), vsg::dvec3(0, 1, 0));
    }

    TEST(MWRenderDistantObjectMatrixTest, zIsAppliedBeforeY)
    {
        // Z: (1, 0, 0) -> (0, -1, 0); Y leaves it. The other order would give (0, 0, 1).
        const vsg::dmat4 matrix
            = distantObjectMatrix(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(0.f, sQuarterTurn, sQuarterTurn), 1.f);
        expectTransforms(matrix, vsg::dvec3(1, 0, 0), vsg::dvec3(0, -1, 0));
    }

    TEST(MWRenderDistantObjectMatrixTest, allThreeAxesInOrder)
    {
        // Z: (1, 2, 3) -> (2, -1, 3); Y: -> (-3, -1, 2); X: -> (-3, 2, 1).
        const vsg::dmat4 matrix
            = distantObjectMatrix(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(sQuarterTurn, sQuarterTurn, sQuarterTurn), 1.f);
        expectTransforms(matrix, vsg::dvec3(1, 2, 3), vsg::dvec3(-3, 2, 1));
    }

    TEST(MWRenderDistantObjectMatrixTest, generalAnglesFollowTheSameOrder)
    {
        const vsg::vec3 rotation(0.3f, -0.7f, 1.1f);
        const vsg::dmat4 matrix = distantObjectMatrix(vsg::vec3(0.f, 0.f, 0.f), rotation, 1.f);

        vsg::dvec3 expected(1, 2, 3);
        expected = rotateAboutZ(expected, -static_cast<double>(rotation.z));
        expected = rotateAboutY(expected, -static_cast<double>(rotation.y));
        expected = rotateAboutX(expected, -static_cast<double>(rotation.x));
        expectTransforms(matrix, vsg::dvec3(1, 2, 3), expected);
    }

    TEST(MWRenderDistantObjectMatrixTest, scaleThenRotationThenTranslation)
    {
        // Scale: (1, 0, 0) -> (2, 0, 0); Z: -> (0, -2, 0); translation: -> (10, 18, 30).
        const vsg::dmat4 matrix
            = distantObjectMatrix(vsg::vec3(10.f, 20.f, 30.f), vsg::vec3(0.f, 0.f, sQuarterTurn), 2.f);
        expectTransforms(matrix, vsg::dvec3(1, 0, 0), vsg::dvec3(10, 18, 30));
        // The position is not rotated or scaled.
        expectTransforms(matrix, vsg::dvec3(0, 0, 0), vsg::dvec3(10, 20, 30));
    }
}
