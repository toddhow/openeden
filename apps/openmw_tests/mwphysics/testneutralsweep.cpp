// Task 0171 (combat stage S5-3): the shape test of a melee swing (apps/openmw/mwphysics/neutralsweep.hpp), written from
// the header and the contract's SweepRequest. Every expectation is worked out by hand in its comment: the attacker
// stands at the origin facing +Y (heading 0), reach 100, half angle 35 degrees, a band from 16 below to 130 above its
// feet, and every body is a capsule of radius 22 and height 128 (the neutral actor's).
#include "apps/openmw/mwphysics/neutralsweep.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralSweep::Candidate;
    using MWPhysics::NeutralSweep::Self;

    constexpr float kDegrees = 3.14159265358979f / 180.f;

    SweepRequest request()
    {
        SweepRequest r;
        r.token = 1;
        r.heading = 0.f;
        r.reach = 100.f;
        r.halfAngle = 35.f * kDegrees;
        r.heightBelow = 16.f;
        r.heightAbove = 130.f;
        return r;
    }

    Candidate actor(std::uint32_t reference, double x, double y, double z = 0.0)
    {
        Candidate c;
        c.mTarget = SweepTarget::Actor;
        c.mActor = { reference, 1 };
        c.mX = x;
        c.mY = y;
        c.mZ = z;
        c.mRadius = 22.f;
        c.mHeight = 128.f;
        return c;
    }

    Candidate player(double x, double y)
    {
        Candidate c = actor(0, x, y);
        c.mTarget = SweepTarget::Player;
        c.mActor = {};
        return c;
    }

    std::vector<SweepHit> sweep(const SweepRequest& r, const Self& self, const std::vector<Candidate>& candidates,
        std::size_t* dropped = nullptr)
    {
        std::array<SweepHit, kMaxSweepHits> out;
        const std::size_t count = MWPhysics::NeutralSweep::arcSweep(r, self, candidates, out, dropped);
        return std::vector<SweepHit>(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count));
    }

    TEST(NeutralSweep, aBodyStraightAheadInsideTheReachIsHitAtItsSurface)
    {
        // Centre 100 ahead, radius 22: the surface is 78 away (<= 100).
        const auto hits = sweep(request(), {}, { actor(7, 0, 100) });
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].target, SweepTarget::Actor);
        EXPECT_EQ(hits[0].actor.reference, 7u);
        EXPECT_NEAR(hits[0].distance, 78.f, 1e-3f);
        // The touched point is on the side facing the attacker: 22 short of the centre.
        EXPECT_NEAR(hits[0].x, 0.0, 1e-9);
        EXPECT_NEAR(hits[0].y, 78.0, 1e-9);
    }

    TEST(NeutralSweep, theReachEdgeIsInclusiveAndAHairBeyondIsOut)
    {
        // Surface exactly 100: centre 122. Surface 100.01: centre 122.01.
        EXPECT_EQ(sweep(request(), {}, { actor(1, 0, 122) }).size(), 1u);
        EXPECT_TRUE(sweep(request(), {}, { actor(1, 0, 122.01) }).empty());
    }

    TEST(NeutralSweep, theArcIsMeasuredToTheEdgeOfTheBodyNotItsCentre)
    {
        // Centre 100 away: the body covers asin(22 / 100) = 12.71 degrees, so a centre at 47 degrees (34.29 to the near
        // edge) is in and one at 48 (35.29) is out; a centre at 60 degrees is far out.
        const auto at = [](double degrees) {
            return actor(1, 100.0 * std::sin(degrees * kDegrees), 100.0 * std::cos(degrees * kDegrees));
        };
        EXPECT_EQ(sweep(request(), {}, { at(47) }).size(), 1u);
        EXPECT_TRUE(sweep(request(), {}, { at(48) }).empty());
        EXPECT_TRUE(sweep(request(), {}, { at(60) }).empty());
        EXPECT_EQ(sweep(request(), {}, { at(-47) }).size(), 1u); // the arc is symmetric
        EXPECT_TRUE(sweep(request(), {}, { at(-48) }).empty());
    }

    TEST(NeutralSweep, aBodyBehindTheAttackerIsNeverHit)
    {
        EXPECT_TRUE(sweep(request(), {}, { actor(1, 0, -50), actor(2, 40, -40) }).empty());
    }

    TEST(NeutralSweep, aBodyTheAttackerStandsInsideIsHitWhateverTheAngle)
    {
        // Centre 5 behind: closer than the radius, the arc test does not apply, the distance is 0.
        const auto hits = sweep(request(), {}, { actor(1, 0, -5) });
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_FLOAT_EQ(hits[0].distance, 0.f);
    }

    TEST(NeutralSweep, theHeadingTurnsTheArc)
    {
        SweepRequest r = request();
        r.heading = 90.f * kDegrees; // faces +X (forward = (sin h, cos h))
        EXPECT_EQ(sweep(r, {}, { actor(1, 100, 0) }).size(), 1u);
        EXPECT_TRUE(sweep(r, {}, { actor(1, 0, 100) }).empty());
        r.x = 500.0;
        r.y = -200.0;
        EXPECT_EQ(sweep(r, {}, { actor(1, 600, -200) }).size(), 1u); // the origin moves the whole arc
    }

    TEST(NeutralSweep, theHeightBandSelectsByTheBodysVerticalExtent)
    {
        // Band is z in [-16, 130]. A body standing at z 200 (feet above the band) and one whose top, -200 + 128 = -72,
        // is below it are out; one standing at -100 reaches up to 28 and is in; one standing at 130 touches the top
        // edge.
        EXPECT_TRUE(sweep(request(), {}, { actor(1, 0, 60, 200) }).empty());
        EXPECT_TRUE(sweep(request(), {}, { actor(1, 0, 60, -200) }).empty());
        EXPECT_EQ(sweep(request(), {}, { actor(1, 0, 60, -100) }).size(), 1u);
        EXPECT_EQ(sweep(request(), {}, { actor(1, 0, 60, 130) }).size(), 1u);
    }

    TEST(NeutralSweep, theAttackerItselfIsLeftOut)
    {
        // The attacker's own body is always inside its arc (closer than the radius).
        const std::vector<Candidate> bodies = { actor(5, 0, 0), actor(6, 0, 80) };
        auto hits = sweep(request(), { false, { 5, 1 } }, bodies);
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].actor.reference, 6u);
        // The same reference with another session is another actor.
        Candidate other = actor(5, 0, 0);
        other.mActor = { 5, 2 };
        hits = sweep(request(), { false, { 5, 1 } }, { other });
        EXPECT_EQ(hits.size(), 1u);
        // The player as attacker leaves the player body out and keeps actors.
        hits = sweep(request(), { true, {} }, { player(0, 0), actor(9, 0, 80) });
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].target, SweepTarget::Actor);
        // An actor as attacker hits the player.
        hits = sweep(request(), { false, { 5, 1 } }, { actor(5, 0, 0), player(0, 80) });
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].target, SweepTarget::Player);
        EXPECT_NEAR(hits[0].distance, 58.f, 1e-3f);
    }

    TEST(NeutralSweep, hitsComeNearestFirstAndTiesBreakByKindThenReferenceNotByInputOrder)
    {
        // Mirror positions (+-30, 80) are the same distance away; the player stands on the same circle.
        const double s = std::sqrt(30.0 * 30.0 + 80.0 * 80.0); // 85.44
        std::vector<Candidate> bodies = { actor(9, 30, 80), actor(3, -30, 80), player(0.0, s), actor(4, 0, 50) };
        const auto hits = sweep(request(), {}, bodies);
        ASSERT_EQ(hits.size(), 4u);
        EXPECT_EQ(hits[0].actor.reference, 4u); // 50 - 22 = 28
        EXPECT_NEAR(hits[0].distance, 28.f, 1e-3f);
        // Then the three at distance (85.44 - 22): actors by reference (3 before 9), the player last.
        EXPECT_EQ(hits[1].actor.reference, 3u);
        EXPECT_EQ(hits[2].actor.reference, 9u);
        EXPECT_EQ(hits[3].target, SweepTarget::Player);
        // The result does not depend on the order the bodies are listed in.
        std::vector<Candidate> reversed(bodies.rbegin(), bodies.rend());
        const auto again = sweep(request(), {}, reversed);
        ASSERT_EQ(again.size(), hits.size());
        for (std::size_t i = 0; i < hits.size(); ++i)
        {
            EXPECT_EQ(again[i].target, hits[i].target);
            EXPECT_EQ(again[i].actor.reference, hits[i].actor.reference);
            EXPECT_FLOAT_EQ(again[i].distance, hits[i].distance);
        }
    }

    TEST(NeutralSweep, moreThanTheCapKeepsTheNearestAndCountsTheRest)
    {
        // 20 bodies in a row ahead, 1 unit apart centre to centre of the line is too tight for a clean count, so step 1
        // along Y from 30: all 20 are inside reach 100 + 22 (centres 30..49, surfaces 8..27).
        std::vector<Candidate> bodies;
        for (std::uint32_t i = 0; i < 20; ++i)
            bodies.push_back(actor(100 + i, 0, 30 + static_cast<double>(i)));
        std::reverse(bodies.begin(), bodies.end());
        std::size_t dropped = 0;
        const auto hits = sweep(request(), {}, bodies, &dropped);
        ASSERT_EQ(hits.size(), kMaxSweepHits);
        EXPECT_EQ(dropped, 4u);
        for (std::size_t i = 0; i < hits.size(); ++i)
            EXPECT_EQ(hits[i].actor.reference, 100u + i); // the 16 nearest, nearest first
    }

    TEST(NeutralSweep, aBodyWithNumbersThatAreNotFiniteIsSkippedNotCounted)
    {
        Candidate bad = actor(1, std::numeric_limits<double>::quiet_NaN(), 60);
        Candidate zero = actor(2, 0, 60);
        zero.mRadius = 0.f;
        std::size_t dropped = 99;
        const auto hits = sweep(request(), {}, { bad, zero, actor(3, 0, 60) }, &dropped);
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].actor.reference, 3u);
        EXPECT_EQ(dropped, 0u);
    }

    TEST(NeutralSweep, aWideArcHitsWhatANarrowOneMisses)
    {
        SweepRequest r = request();
        r.halfAngle = 3.14159265f; // a full circle
        EXPECT_EQ(sweep(r, {}, { actor(1, 0, -80) }).size(), 1u);
    }
}
