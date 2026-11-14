// Task 0159: links through one-sided walls. A plane of triangles that faces one way stops what comes from its front and
// lets through what comes from its back (the mover ignores back faces); the navmesh sees a solid wall. The module finds
// the crossings the mover can make, so the game can give them to the navigator. Written before the code; the first run
// was against a stub that finds nothing.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "apps/openmw/mwphysics/neutralplanelinks.hpp"

namespace
{
    using namespace MWPhysics::NeutralPlaneLinks;
    using MWPhysics::NeutralMover::Sweep;

    // A world of one wall plane at x = 0 (normal +x is its front), a flat floor at z = floor between two limits.
    class PlaneWorld final : public World
    {
    public:
        bool mTwoSided = false;
        float mFloorZ = 100.f;
        float mFloorTopOnWest = 100.f; // the floor west of the wall can differ in height
        bool mFits = true;

        Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override
        {
            Sweep result;
            const float before = from.x;
            const float after = from.x + delta.x;
            const bool towardsFront = delta.x < 0.f; // moving against the normal
            const bool crosses = (before >= 0.f && after < 0.f) || (before <= 0.f && after > 0.f);
            if (crosses && (towardsFront || mTwoSided))
            {
                result.mHit = true;
                result.mFraction = std::clamp(-before / delta.x, 0.f, 1.f);
                result.mNormal = vsg::vec3(1.f, 0.f, 0.f);
            }
            return result;
        }

        Sweep probeDown(const vsg::vec3& from, float depth) const override
        {
            Sweep result;
            const float z = from.x < 0.f ? mFloorTopOnWest : mFloorZ;
            if (z > from.z || z < from.z - depth)
                return result;
            result.mHit = true;
            result.mFraction = (from.z - z) / depth;
            return result;
        }

        bool fits(const vsg::vec3&) const override { return mFits; }
    };

    // A wall at x = 0 from y0 to y1 and z0 to z1, as two triangles.
    std::vector<Triangle> wall(float y0, float y1, float z0, float z1)
    {
        return { Triangle{ { vsg::vec3(0, y0, z0), vsg::vec3(0, y1, z0), vsg::vec3(0, y1, z1) } },
            Triangle{ { vsg::vec3(0, y0, z0), vsg::vec3(0, y1, z1), vsg::vec3(0, y0, z1) } } };
    }

    TEST(NeutralPlaneLinks, aOneSidedWallGivesALinkFromItsBackToItsFront)
    {
        const PlaneWorld world;
        const auto links = find(wall(-100.f, 100.f, 100.f, 300.f), world);
        ASSERT_FALSE(links.empty());
        for (const Link& link : links)
        {
            EXPECT_LT(link.mFrom.x, 0.f); // from the back (moving along the normal passes)
            EXPECT_GT(link.mTo.x, 0.f);
            EXPECT_FLOAT_EQ(link.mTo.x - link.mFrom.x, 2.f * kStandOff);
            EXPECT_FLOAT_EQ(link.mFrom.z, 100.f);
            EXPECT_FLOAT_EQ(link.mTo.z, 100.f);
            EXPECT_GE(link.mFrom.y, -100.f);
            EXPECT_LE(link.mFrom.y, 100.f);
        }
    }

    TEST(NeutralPlaneLinks, aWallThatBlocksBothWaysGivesNoLink)
    {
        PlaneWorld world;
        world.mTwoSided = true;
        EXPECT_TRUE(find(wall(-100.f, 100.f, 100.f, 300.f), world).empty());
    }

    TEST(NeutralPlaneLinks, aFloorTriangleIsNotAWall)
    {
        const PlaneWorld world;
        const std::vector<Triangle> floor{ Triangle{ { vsg::vec3(-100, -100, 100), vsg::vec3(100, -100, 100),
            vsg::vec3(100, 100, 100) } } };
        EXPECT_TRUE(find(floor, world).empty());
    }

    TEST(NeutralPlaneLinks, floorsOfVeryDifferentHeightOnTheTwoSidesGiveNoLink)
    {
        PlaneWorld world;
        world.mFloorTopOnWest = 100.f - 2.f * kFloorReach / 3.f * 2.f; // lower than the step the mover climbs
        EXPECT_TRUE(find(wall(-100.f, 100.f, 100.f, 300.f), world).empty());
    }

    TEST(NeutralPlaneLinks, aWallFloatingFarAboveAnyFloorGivesNoLink)
    {
        const PlaneWorld world;
        EXPECT_TRUE(find(wall(-100.f, 100.f, 400.f, 600.f), world).empty());
    }

    TEST(NeutralPlaneLinks, anEndWhereTheCapsuleDoesNotFitGivesNoLink)
    {
        PlaneWorld world;
        world.mFits = false;
        EXPECT_TRUE(find(wall(-100.f, 100.f, 100.f, 300.f), world).empty());
    }

    TEST(NeutralPlaneLinks, aVeryLongWallGivesAtMostTheCapAndLinksAreNotDuplicates)
    {
        const PlaneWorld world;
        const auto links = find(wall(-20000.f, 20000.f, 100.f, 300.f), world);
        EXPECT_LE(links.size(), kMaxLinksPerObject);
        EXPECT_GT(links.size(), 1u);
        for (std::size_t i = 0; i < links.size(); ++i)
            for (std::size_t j = i + 1; j < links.size(); ++j)
            {
                const float dy = links[i].mFrom.y - links[j].mFrom.y;
                EXPECT_GE(std::fabs(dy), kMergeDistance);
            }
    }

    TEST(NeutralPlaneLinks, aNarrowSliverIsNotSampled)
    {
        const PlaneWorld world;
        EXPECT_TRUE(find(wall(0.f, kMinWallWidth * 0.5f, 100.f, 300.f), world).empty());
    }

    TEST(NeutralPlaneLinks, nonFiniteAndDegenerateTrianglesAreSkipped)
    {
        const PlaneWorld world;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const std::vector<Triangle> bad{ Triangle{ { vsg::vec3(0, nan, 0), vsg::vec3(0, 1, 0), vsg::vec3(0, 1, 1) } },
            Triangle{ { vsg::vec3(0, 0, 0), vsg::vec3(0, 0, 0), vsg::vec3(0, 0, 0) } } };
        EXPECT_TRUE(find(bad, world).empty());
        EXPECT_TRUE(find({}, world).empty());
    }

    TEST(NeutralPlaneLinks, theResultIsTheSameEveryTime)
    {
        const PlaneWorld world;
        const auto a = find(wall(-300.f, 300.f, 100.f, 300.f), world);
        const auto b = find(wall(-300.f, 300.f, 100.f, 300.f), world);
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            EXPECT_EQ(a[i].mFrom, b[i].mFrom);
            EXPECT_EQ(a[i].mTo, b[i].mTo);
        }
    }
}
