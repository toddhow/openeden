// Task 0280 (dragons, second slice): the player's blows reach a dragon. Task 0253 reported that the player's swings
// "never reached the 150 radius capsule" and killed the dragon with a test setting. The runs of that task stood the
// player 900 units away; the swing's reach is measured from the capsule's SURFACE (centre distance minus the radius), so
// a bare-handed swing (reach 96) reaches a dragon whose centre is at most 150 + 96 = 246 units away and no more. These
// tests pin that geometry for the dragon's capsule (radius 150, height 414), the arc and the height band. They
// characterise the sweep as it is (no code changed for them; they passed the first time they were run). The attacker
// stands at the origin facing +Y.
#include "apps/openmw/mwphysics/neutralsweep.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralSweep::Candidate;
    using MWPhysics::NeutralSweep::Self;

    constexpr float kDegrees = 3.14159265358979f / 180.f;
    constexpr float kDragonRadius = 150.f;
    constexpr float kDragonHeight = 414.f;
    constexpr float kFistReach = 96.f; // the race's unarmed reach (the player's fists in the runs of task 0253)

    SweepRequest swing(float reach = kFistReach)
    {
        SweepRequest r;
        r.token = 1;
        r.heading = 0.f;
        r.reach = reach;
        r.halfAngle = 35.f * kDegrees;
        r.heightBelow = 16.f;
        r.heightAbove = 130.f;
        return r;
    }

    Candidate dragon(double x, double y, double z = 0.0)
    {
        Candidate c;
        c.mTarget = SweepTarget::Actor;
        c.mActor = { 7, 1 };
        c.mX = x;
        c.mY = y;
        c.mZ = z;
        c.mRadius = kDragonRadius;
        c.mHeight = kDragonHeight;
        return c;
    }

    std::vector<SweepHit> sweep(const SweepRequest& r, const std::vector<Candidate>& candidates)
    {
        std::array<SweepHit, kMaxSweepHits> out;
        const std::size_t count = MWPhysics::NeutralSweep::arcSweep(r, Self{ true, {} }, candidates, out);
        return std::vector<SweepHit>(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count));
    }

    TEST(NeutralSweepLarge, aSwingReachesTheDragonWhenTheGapToItsSurfaceIsWithinTheReach)
    {
        // centre 240 ahead: the surface is 90 away, inside 96
        const std::vector<SweepHit> hits = sweep(swing(), { dragon(0.0, 240.0) });
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].actor.reference, 7u);
        EXPECT_NEAR(hits[0].distance, 90.f, 1e-3f);
        EXPECT_NEAR(hits[0].y, 90.0, 1e-6) << "the touched point is on the surface facing the attacker";
    }

    TEST(NeutralSweepLarge, theGapIsMeasuredFromTheSurfaceNotTheCentre)
    {
        EXPECT_EQ(sweep(swing(), { dragon(0.0, 246.0) }).size(), 1u) << "exactly 150 + 96";
        EXPECT_TRUE(sweep(swing(), { dragon(0.0, 247.0) }).empty());
        EXPECT_TRUE(sweep(swing(), { dragon(0.0, 900.0) }).empty()) << "task 0253's runs stood 900 away";
    }

    TEST(NeutralSweepLarge, aLongerReachReachesFartherByTheSameAmount)
    {
        EXPECT_EQ(sweep(swing(141.f), { dragon(0.0, 150.0 + 141.0) }).size(), 1u);
        EXPECT_TRUE(sweep(swing(141.f), { dragon(0.0, 150.0 + 142.0) }).empty());
    }

    TEST(NeutralSweepLarge, theBodyItselfCoversTheAngleSoABlowBesideItsCentreLineStillLands)
    {
        // 80 degrees off the facing, centre 240 away: far outside the 35 degree half angle, but the dragon is 150 wide
        // (the body covers asin(150 / 240) = 38.7 degrees), so 80 - 38.7 = 41.3 degrees remain: a miss
        const double a = 80.0 * kDegrees;
        EXPECT_TRUE(sweep(swing(), { dragon(240.0 * std::sin(a), 240.0 * std::cos(a)) }).empty());
        // 70 degrees: 70 - 38.7 = 31.3 inside 35: a hit
        const double b = 70.0 * kDegrees;
        EXPECT_EQ(sweep(swing(), { dragon(240.0 * std::sin(b), 240.0 * std::cos(b)) }).size(), 1u);
    }

    TEST(NeutralSweepLarge, theBandMeetsTheTallBodyWhereverTheAttackerStandsOnIt)
    {
        // a dragon standing on a ledge 200 above the attacker's feet is still in the 130 band (it is 414 tall); one
        // 500 above is not, and neither is one whose top is 20 below the band's floor
        EXPECT_EQ(sweep(swing(), { dragon(0.0, 240.0, 100.0) }).size(), 1u);
        EXPECT_TRUE(sweep(swing(), { dragon(0.0, 240.0, 500.0) }).empty());
        EXPECT_TRUE(sweep(swing(), { dragon(0.0, 240.0, -450.0) }).empty());
    }
}
