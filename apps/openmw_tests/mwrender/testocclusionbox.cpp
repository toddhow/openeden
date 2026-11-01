// Tests for MWRender::OcclusionBox, the box weather particle occlusion renders from above (task 0012, V4). Written from
// apps/openmw/mwrender/occlusionbox.hpp and OpenMW's precipitationocclusion.cpp at 442eec6e2f only.
//
// Conventions these tests rely on:
// - updateRange subtracts a margin of (-50, -50, 0) from the range, so XY size is range + 50 and Z has no margin.
// - PrecipitationOccluder::update reaches range.z + Constants::CellSizeInUnits (8192) above and below the camera. The
//   value is written out here rather than taken from the constant, so a change to it fails these tests.
// - occlusionCoord is linear and does not clamp, so positions outside the box give coordinates outside [0, 1].
// - Depth is reversed: larger is higher, so a particle whose depth is smaller than the map's is under that surface.

#include "apps/openmw/mwrender/occlusionbox.hpp"

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

#include <gtest/gtest.h>

namespace
{
    using MWRender::isOccluded;
    using MWRender::makeOcclusionBox;
    using MWRender::OcclusionBox;
    using MWRender::occlusionCoord;

    constexpr float sEpsilon = 1e-4f;
    constexpr float sCellSize = 8192.f;

    // Away from the origin, with every axis different, so swapped or dropped terms change the results.
    // Size (650, 850); zMax 8892; zMin -8292; zRange 17184.
    const vsg::vec3 sCamera(1000.f, -2000.f, 300.f);
    const vsg::vec3 sRange(600.f, 800.f, 400.f);
    constexpr float sLowX = 1000.f - 325.f;
    constexpr float sHighX = 1000.f + 325.f;
    constexpr float sLowY = -2000.f - 425.f;
    constexpr float sHighY = -2000.f + 425.f;
    constexpr float sZMax = 8892.f;
    constexpr float sZMin = -8292.f;

    void expectCoord(const vsg::vec3& actual, float x, float y, float z)
    {
        EXPECT_NEAR(actual.x, x, sEpsilon);
        EXPECT_NEAR(actual.y, y, sEpsilon);
        EXPECT_NEAR(actual.z, z, sEpsilon);
    }

    TEST(MWRenderOcclusionBoxTest, sizeIsRangePlusFiftyOnXAndY)
    {
        const OcclusionBox box = makeOcclusionBox(vsg::vec3(), vsg::vec3(600.f, 800.f, 400.f));
        EXPECT_FLOAT_EQ(box.size.x, 650.f);
        EXPECT_FLOAT_EQ(box.size.y, 850.f);
    }

    TEST(MWRenderOcclusionBoxTest, sizeMarginIsAddedNotScaled)
    {
        const OcclusionBox box = makeOcclusionBox(vsg::vec3(), vsg::vec3(100.5f, 5000.f, 1.f));
        EXPECT_FLOAT_EQ(box.size.x, 150.5f);
        EXPECT_FLOAT_EQ(box.size.y, 5050.f);
    }

    TEST(MWRenderOcclusionBoxTest, sizeDoesNotDependOnTheCamera)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        EXPECT_FLOAT_EQ(box.size.x, 650.f);
        EXPECT_FLOAT_EQ(box.size.y, 850.f);
    }

    TEST(MWRenderOcclusionBoxTest, centreIsTheCameraXY)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        EXPECT_FLOAT_EQ(box.centre.x, 1000.f);
        EXPECT_FLOAT_EQ(box.centre.y, -2000.f);
    }

    TEST(MWRenderOcclusionBoxTest, zMaxIsCameraPlusRangePlusOneCell)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        EXPECT_FLOAT_EQ(box.zMax, 300.f + 400.f + sCellSize);
    }

    TEST(MWRenderOcclusionBoxTest, zMaxFollowsACameraBelowZero)
    {
        const OcclusionBox box = makeOcclusionBox(vsg::vec3(0.f, 0.f, -500.f), sRange);
        EXPECT_FLOAT_EQ(box.zMax, -500.f + 400.f + sCellSize);
    }

    TEST(MWRenderOcclusionBoxTest, zRangeSpansTheRangeAndOneCellEachWayWithNoMargin)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        EXPECT_FLOAT_EQ(box.zRange, 2.f * (400.f + sCellSize));
    }

    TEST(MWRenderOcclusionBoxTest, zRangeDoesNotDependOnTheCamera)
    {
        const OcclusionBox atOrigin = makeOcclusionBox(vsg::vec3(), sRange);
        const OcclusionBox away = makeOcclusionBox(sCamera, sRange);
        EXPECT_FLOAT_EQ(atOrigin.zRange, away.zRange);
    }

    TEST(MWRenderOcclusionBoxTest, cameraPositionMapsToTheMiddleOfTheBox)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        expectCoord(occlusionCoord(box, sCamera), 0.5f, 0.5f, 0.5f);
    }

    TEST(MWRenderOcclusionBoxTest, cameraAtTheOriginMapsToTheMiddleOfTheBox)
    {
        const OcclusionBox box = makeOcclusionBox(vsg::vec3(), sRange);
        expectCoord(occlusionCoord(box, vsg::vec3()), 0.5f, 0.5f, 0.5f);
    }

    TEST(MWRenderOcclusionBoxTest, coordUsesTheBoxFieldsAsGiven)
    {
        // Set by hand, so this does not depend on makeOcclusionBox. zMin is 50 - 100 = -50.
        OcclusionBox box;
        box.centre = vsg::vec2(10.f, 20.f);
        box.size = vsg::vec2(100.f, 200.f);
        box.zMax = 50.f;
        box.zRange = 100.f;

        expectCoord(occlusionCoord(box, vsg::vec3(35.f, 70.f, 25.f)), 0.75f, 0.75f, 0.75f);
        expectCoord(occlusionCoord(box, vsg::vec3(-40.f, -80.f, -50.f)), 0.f, 0.f, 0.f);
    }

    TEST(MWRenderOcclusionBoxTest, lowCornerMapsToZero)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        expectCoord(occlusionCoord(box, vsg::vec3(sLowX, sLowY, sZMin)), 0.f, 0.f, 0.f);
    }

    TEST(MWRenderOcclusionBoxTest, highCornerMapsToOne)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        expectCoord(occlusionCoord(box, vsg::vec3(sHighX, sHighY, sZMax)), 1.f, 1.f, 1.f);
    }

    TEST(MWRenderOcclusionBoxTest, everyCornerMapsToItsOwnZerosAndOnes)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        expectCoord(occlusionCoord(box, vsg::vec3(sHighX, sLowY, sZMin)), 1.f, 0.f, 0.f);
        expectCoord(occlusionCoord(box, vsg::vec3(sLowX, sHighY, sZMin)), 0.f, 1.f, 0.f);
        expectCoord(occlusionCoord(box, vsg::vec3(sHighX, sHighY, sZMin)), 1.f, 1.f, 0.f);
        expectCoord(occlusionCoord(box, vsg::vec3(sLowX, sLowY, sZMax)), 0.f, 0.f, 1.f);
        expectCoord(occlusionCoord(box, vsg::vec3(sHighX, sLowY, sZMax)), 1.f, 0.f, 1.f);
        expectCoord(occlusionCoord(box, vsg::vec3(sLowX, sHighY, sZMax)), 0.f, 1.f, 1.f);
    }

    TEST(MWRenderOcclusionBoxTest, coordIsLinearBetweenTheEdges)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        // A quarter of the size or range from the low side on each axis.
        const vsg::vec3 world(sLowX + 650.f / 4.f, sLowY + 850.f / 4.f, sZMin + 17184.f / 4.f);
        expectCoord(occlusionCoord(box, world), 0.25f, 0.25f, 0.25f);
    }

    TEST(MWRenderOcclusionBoxTest, eachCoordDependsOnlyOnItsOwnAxis)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        expectCoord(occlusionCoord(box, vsg::vec3(sHighX, -2000.f, 300.f)), 1.f, 0.5f, 0.5f);
        expectCoord(occlusionCoord(box, vsg::vec3(1000.f, sHighY, 300.f)), 0.5f, 1.f, 0.5f);
        expectCoord(occlusionCoord(box, vsg::vec3(1000.f, -2000.f, sZMax)), 0.5f, 0.5f, 1.f);
    }

    TEST(MWRenderOcclusionBoxTest, sameOffsetFromTheCameraGivesTheSameCoord)
    {
        const vsg::vec3 offset(100.f, -200.f, 3000.f);
        const vsg::vec3 atOrigin = occlusionCoord(makeOcclusionBox(vsg::vec3(), sRange), offset);
        const vsg::vec3 away = occlusionCoord(makeOcclusionBox(sCamera, sRange), sCamera + offset);
        expectCoord(away, atOrigin.x, atOrigin.y, atOrigin.z);
    }

    TEST(MWRenderOcclusionBoxTest, positionsPastTheLowEdgesGoBelowZero)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        // One full size or range past each low edge.
        expectCoord(occlusionCoord(box, vsg::vec3(sLowX - 650.f, sLowY - 850.f, sZMin - 17184.f)), -1.f, -1.f, -1.f);
    }

    TEST(MWRenderOcclusionBoxTest, positionsPastTheHighEdgesGoAboveOne)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        // Half a size or range past each high edge.
        expectCoord(occlusionCoord(box, vsg::vec3(sHighX + 325.f, sHighY + 425.f, sZMax + 8592.f)), 1.5f, 1.5f, 1.5f);
    }

    TEST(MWRenderOcclusionBoxTest, particleBelowTheSurfaceIsOccluded)
    {
        EXPECT_TRUE(isOccluded(0.25f, 0.75f));
    }

    TEST(MWRenderOcclusionBoxTest, particleAboveTheSurfaceIsNotOccluded)
    {
        EXPECT_FALSE(isOccluded(0.75f, 0.25f));
    }

    TEST(MWRenderOcclusionBoxTest, equalDepthsAreNotOccluded)
    {
        EXPECT_FALSE(isOccluded(0.5f, 0.5f));
        EXPECT_FALSE(isOccluded(0.f, 0.f));
        EXPECT_FALSE(isOccluded(1.f, 1.f));
    }

    TEST(MWRenderOcclusionBoxTest, particleUnderARoofIsOccluded)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        const float roofDepth = occlusionCoord(box, vsg::vec3(1000.f, -2000.f, 800.f)).z;
        const float particleDepth = occlusionCoord(box, vsg::vec3(1000.f, -2000.f, 500.f)).z;
        EXPECT_TRUE(isOccluded(particleDepth, roofDepth));
    }

    TEST(MWRenderOcclusionBoxTest, particleAboveARoofIsNotOccluded)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        const float roofDepth = occlusionCoord(box, vsg::vec3(1000.f, -2000.f, 800.f)).z;
        const float particleDepth = occlusionCoord(box, vsg::vec3(1000.f, -2000.f, 1100.f)).z;
        EXPECT_FALSE(isOccluded(particleDepth, roofDepth));
    }

    TEST(MWRenderOcclusionBoxTest, particleAtTheHeightOfARoofIsNotOccluded)
    {
        const OcclusionBox box = makeOcclusionBox(sCamera, sRange);
        const vsg::vec3 world(1000.f, -2000.f, 800.f);
        EXPECT_FALSE(isOccluded(occlusionCoord(box, world).z, occlusionCoord(box, world).z));
    }
}
