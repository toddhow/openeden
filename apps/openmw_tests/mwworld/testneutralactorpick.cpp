#include <apps/openmw/mwworld/neutralpick.hpp>

#include <gtest/gtest.h>

#include <cmath>

// Task 0275: a living actor behind a low obstacle is picked by a box round its feet when its head is in clear view.
// Numbers by hand: the actor stands at the origin, the camera is at (0, -192, 160) (an eye 160 units up, 192 away), a
// counter 100 high lies between them. A ray from the eye aimed at the chest (0, 0, 100) goes down at atan(60 / 192).
namespace
{
    using vsg::dvec3;

    TEST(NeutralActorPick, theBoxIsTheActorsFootprintAndHeightAtItsScale)
    {
        const MWWorld::NeutralActorBox box = MWWorld::neutralActorBox(1.0);
        EXPECT_DOUBLE_EQ(box.mMin.x, -MWWorld::kActorPickRadius);
        EXPECT_DOUBLE_EQ(box.mMax.y, MWWorld::kActorPickRadius);
        EXPECT_DOUBLE_EQ(box.mMin.z, 0.0);
        // the box reaches 32 above the head: the stub player's eye is about 150 up, over a Skyrim actor's 128
        EXPECT_DOUBLE_EQ(box.mMax.z, 128.0 + 32.0);
        EXPECT_DOUBLE_EQ(box.mHead.z, 128.0 * 0.85);
        EXPECT_DOUBLE_EQ(box.mHead.x, 0.0);
        const MWWorld::NeutralActorBox big = MWWorld::neutralActorBox(1.5);
        EXPECT_DOUBLE_EQ(big.mMax.z, 192.0 + 32.0);
        EXPECT_NEAR(big.mHead.z, 192.0 * 0.85, 1e-9);
    }

    TEST(NeutralActorPick, aBadScaleFallsBackToTheStandardSize)
    {
        EXPECT_DOUBLE_EQ(MWWorld::neutralActorBox(std::nan("")).mMax.z, 160.0);
        EXPECT_DOUBLE_EQ(MWWorld::neutralActorBox(0.0).mMax.z, 160.0);
        EXPECT_DOUBLE_EQ(MWWorld::neutralActorBox(-2.0).mMax.z, 160.0);
    }

    TEST(NeutralActorPick, aLevelRayFromTheStubPlayersEyeCrossesTheBoxOverTheHead)
    {
        // eye 150 up, 130 away, looking level: the actor's own height (128) is below it, the headroom makes it cross
        const MWWorld::NeutralActorBox box = MWWorld::neutralActorBox(1.0);
        const auto hit = MWWorld::rayOrientedBox(dvec3(-130.0, 0.0, 150.0), dvec3(1.0, 0.0, 0.0), 400.0, box.mMin,
            box.mMax, dvec3(0.0, 0.0, 0.0), vsg::dquat(0.0, 0.0, 0.0, 1.0), 1.0);
        ASSERT_TRUE(hit.has_value());
        EXPECT_NEAR(hit->mEntry, 104.0, 1e-9);
    }

    TEST(NeutralActorPick, aRayAimedAtTheChestCrossesTheBox)
    {
        const MWWorld::NeutralActorBox box = MWWorld::neutralActorBox(1.0);
        const dvec3 eye(0.0, -192.0, 160.0);
        dvec3 d(0.0, 192.0, -60.0);
        const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        d = dvec3(d.x / len, d.y / len, d.z / len);
        const auto hit = MWWorld::rayOrientedBox(
            eye, d, 400.0, box.mMin, box.mMax, dvec3(0.0, 0.0, 0.0), vsg::dquat(0.0, 0.0, 0.0, 1.0), 1.0);
        ASSERT_TRUE(hit.has_value());
        EXPECT_GT(hit->mEntry, 150.0);
        EXPECT_LT(hit->mEntry, 210.0);
    }

    TEST(NeutralActorPick, anActivatableRenderHitIsNeverOverridden)
    {
        EXPECT_FALSE(MWWorld::actorBeatsRenderHit(true, true));
        EXPECT_FALSE(MWWorld::actorBeatsRenderHit(true, false));
    }

    TEST(NeutralActorPick, aStaticInTheWayIsPassedWhenTheHeadIsInClearView)
    {
        EXPECT_TRUE(MWWorld::actorBeatsRenderHit(false, true));
    }

    TEST(NeutralActorPick, aWallBetweenTheEyeAndTheHeadKeepsTheKeyFromWorking)
    {
        EXPECT_FALSE(MWWorld::actorBeatsRenderHit(false, false));
    }
}
