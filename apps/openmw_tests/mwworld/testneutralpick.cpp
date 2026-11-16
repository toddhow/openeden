#include <apps/openmw/mwworld/neutralpick.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

// Task 0233: a ray against a neutral furniture's bounding box, in the object's own frame. Every number is derived by hand:
// a box [-10,-10,0]..[10,10,50] at the origin with no rotation and scale 1 is crossed by the ray from (-100, 0, 25)
// along +X between x = -10 and x = 10, that is at 90 and 110 units from the start.
namespace
{
    using MWWorld::NeutralBoxHit;
    using vsg::dquat;
    using vsg::dvec3;

    const dvec3 kMin(-10.0, -10.0, 0.0);
    const dvec3 kMax(10.0, 10.0, 50.0);
    const dquat kNoTurn(0.0, 0.0, 0.0, 1.0);
    const dvec3 kOrigin(0.0, 0.0, 0.0);
    const double kNan = std::numeric_limits<double>::quiet_NaN();

    std::optional<NeutralBoxHit> cast(const dvec3& from, const dvec3& direction, double maxDistance = 500.0,
        const dvec3& boxMin = kMin, const dvec3& boxMax = kMax, const dvec3& position = kOrigin,
        const dquat& rotation = kNoTurn, double scale = 1.0)
    {
        return MWWorld::rayOrientedBox(from, direction, maxDistance, boxMin, boxMax, position, rotation, scale);
    }
}

TEST(NeutralPickBox, aRayThroughTheMiddleEntersAndLeavesAtTheFaces)
{
    const auto hit = cast(dvec3(-100.0, 0.0, 25.0), dvec3(1.0, 0.0, 0.0));
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->mEntry, 90.0, 1e-9);
    EXPECT_NEAR(hit->mExit, 110.0, 1e-9);
}

TEST(NeutralPickBox, aRayBesideTheBoxMisses)
{
    EXPECT_FALSE(cast(dvec3(-100.0, 30.0, 25.0), dvec3(1.0, 0.0, 0.0)).has_value());
    EXPECT_FALSE(cast(dvec3(-100.0, 0.0, 80.0), dvec3(1.0, 0.0, 0.0)).has_value()); // above it
}

TEST(NeutralPickBox, aRayThatStartsInsideHasAnEntryOfZero)
{
    const auto hit = cast(dvec3(0.0, 0.0, 25.0), dvec3(1.0, 0.0, 0.0));
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->mEntry, 0.0, 1e-9);
    EXPECT_NEAR(hit->mExit, 10.0, 1e-9);
}

TEST(NeutralPickBox, aBoxBeyondTheMaximumDistanceOrBehindTheRayIsNotHit)
{
    EXPECT_FALSE(cast(dvec3(-100.0, 0.0, 25.0), dvec3(1.0, 0.0, 0.0), 50.0).has_value()); // enters at 90
    EXPECT_TRUE(cast(dvec3(-100.0, 0.0, 25.0), dvec3(1.0, 0.0, 0.0), 95.0).has_value()); // enters at 90, within 95
    EXPECT_FALSE(cast(dvec3(100.0, 0.0, 25.0), dvec3(1.0, 0.0, 0.0)).has_value()); // the box is behind
}

TEST(NeutralPickBox, aRayAlongAFaceCountsAsAHit)
{
    const auto hit = cast(dvec3(-100.0, 10.0, 25.0), dvec3(1.0, 0.0, 0.0));
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->mEntry, 90.0, 1e-9);
}

TEST(NeutralPickBox, theDirectionIsNormalised)
{
    const auto hit = cast(dvec3(-100.0, 0.0, 25.0), dvec3(3.0, 0.0, 0.0));
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->mEntry, 90.0, 1e-9);
    EXPECT_FALSE(cast(dvec3(-100.0, 0.0, 25.0), dvec3(0.0, 0.0, 0.0)).has_value());
}

TEST(NeutralPickBox, theScaleStretchesTheBoxAboutTheOrigin)
{
    // scale 2: the box is [-20,-20,0]..[20,20,100]; the ray at z = 50 crosses x = -20 and x = 20
    const auto hit = cast(dvec3(-100.0, 0.0, 50.0), dvec3(1.0, 0.0, 0.0), 500.0, kMin, kMax, kOrigin, kNoTurn, 2.0);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->mEntry, 80.0, 1e-9);
    EXPECT_NEAR(hit->mExit, 120.0, 1e-9);
}

TEST(NeutralPickBox, thePositionMovesTheBox)
{
    // the frame stands at (100, 50, 0): the box is x 90..110, y 40..60
    const auto hit
        = cast(dvec3(0.0, 50.0, 25.0), dvec3(1.0, 0.0, 0.0), 500.0, kMin, kMax, dvec3(100.0, 50.0, 0.0));
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->mEntry, 90.0, 1e-9);
    EXPECT_NEAR(hit->mExit, 110.0, 1e-9);
}

TEST(NeutralPickBox, aQuarterTurnAboutZTurnsTheLongSideFromXToY)
{
    // local box [0,-2,0]..[100,2,50] is long along its +X; a quarter turn about Z (the active rotation that takes +X to
    // +Y) puts it at world x -2..2, y 0..100
    const double s = std::sqrt(0.5);
    const dquat quarter(0.0, 0.0, s, s);
    const dvec3 from(-50.0, 60.0, 25.0);
    const auto hit = cast(from, dvec3(1.0, 0.0, 0.0), 500.0, dvec3(0.0, -2.0, 0.0), dvec3(100.0, 2.0, 50.0), kOrigin, quarter, 1.0);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->mEntry, 48.0, 1e-9);
    EXPECT_NEAR(hit->mExit, 52.0, 1e-9);
    // the same ray at y = -60 passes the empty side
    EXPECT_FALSE(cast(dvec3(-50.0, -60.0, 25.0), dvec3(1.0, 0.0, 0.0), 500.0, dvec3(0.0, -2.0, 0.0),
        dvec3(100.0, 2.0, 50.0), kOrigin, quarter, 1.0)
            .has_value());
}

TEST(NeutralPickBox, numbersThatAreNotUsableMissInsteadOfThrowing)
{
    EXPECT_FALSE(cast(dvec3(kNan, 0.0, 25.0), dvec3(1.0, 0.0, 0.0)).has_value());
    EXPECT_FALSE(cast(dvec3(-100.0, 0.0, 25.0), dvec3(kNan, 0.0, 0.0)).has_value());
    EXPECT_FALSE(cast(dvec3(-100.0, 0.0, 25.0), dvec3(1.0, 0.0, 0.0), kNan).has_value());
    EXPECT_FALSE(cast(dvec3(-100.0, 0.0, 25.0), dvec3(1.0, 0.0, 0.0), 500.0, kMax, kMin).has_value()); // empty box
    EXPECT_FALSE(cast(dvec3(-100.0, 0.0, 25.0), dvec3(1.0, 0.0, 0.0), 500.0, kMin, kMax, kOrigin, kNoTurn, 0.0)
            .has_value());
    EXPECT_FALSE(cast(dvec3(-100.0, 0.0, 25.0), dvec3(1.0, 0.0, 0.0), 500.0, kMin, kMax, kOrigin, kNoTurn, kNan)
            .has_value());
}

TEST(NeutralPickRule, theBoxWinsWhenTheRenderedRayEndedBeyondItOrHitNothing)
{
    const NeutralBoxHit box{ 90.0, 110.0 };
    for (const bool activatable : { false, true })
    {
        EXPECT_TRUE(MWWorld::boxBeatsRenderHit(box, -1.0, activatable)); // nothing hit
        EXPECT_TRUE(MWWorld::boxBeatsRenderHit(box, 189.0, activatable)); // the floor behind
        EXPECT_TRUE(MWWorld::boxBeatsRenderHit(box, 110.5, activatable));
    }
}

TEST(NeutralPickRule, aHitThatCanBeActivatedBeforeOrInsideTheBoxStays)
{
    const NeutralBoxHit box{ 90.0, 110.0 };
    EXPECT_FALSE(MWWorld::boxBeatsRenderHit(box, 80.0, true)); // a thing in front of it
    EXPECT_FALSE(MWWorld::boxBeatsRenderHit(box, 90.0, true)); // its own front face
    EXPECT_FALSE(MWWorld::boxBeatsRenderHit(box, 100.0, true)); // an item on it, an actor beside it
    EXPECT_FALSE(MWWorld::boxBeatsRenderHit(box, 110.0, true)); // its back face
}

TEST(NeutralPickRule, aHitThatCannotBeActivatedInsideTheBoxLosesToIt)
{
    // the floor under a cooking pot lies inside the pot's bounds: the ray entered the box at 90 and met the floor at 100
    const NeutralBoxHit box{ 90.0, 110.0 };
    EXPECT_TRUE(MWWorld::boxBeatsRenderHit(box, 100.0, false));
    EXPECT_TRUE(MWWorld::boxBeatsRenderHit(box, 91.0, false));
}

TEST(NeutralPickRule, aHitThatCannotBeActivatedInFrontOfTheBoxStillWins)
{
    // a wall at 80 hides the furniture that starts at 90
    const NeutralBoxHit box{ 90.0, 110.0 };
    EXPECT_FALSE(MWWorld::boxBeatsRenderHit(box, 80.0, false));
    EXPECT_FALSE(MWWorld::boxBeatsRenderHit(box, 90.0, false)); // at the face itself: the surface in front stays
}

TEST(NeutralPickRule, aRenderDistanceThatIsNotANumberIsTreatedAsNoHit)
{
    const NeutralBoxHit box{ 90.0, 110.0 };
    EXPECT_TRUE(MWWorld::boxBeatsRenderHit(box, kNan, true));
    EXPECT_TRUE(MWWorld::boxBeatsRenderHit(box, kNan, false));
}
