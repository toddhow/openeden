// Task 0178 (combat stage S5-6): the flight of a projectile, tested with plain numbers (neutralprojectile.hpp). The
// expected values are worked out by hand in the comments.
#include "apps/openmw/mwphysics/neutralprojectile.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

namespace
{
    using namespace MWPhysics::NeutralProjectile;
    using MWPhysics::NeutralSweep::Candidate;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::SweepTarget;

    // An actor's capsule: radius 22, standing height 128, feet at z.
    Candidate capsuleAt(double x, double y, double feetZ, ActorHandle handle = { 5, 1 })
    {
        Candidate c;
        c.mTarget = SweepTarget::Actor;
        c.mActor = handle;
        c.mX = x;
        c.mY = y;
        c.mZ = feetZ;
        c.mRadius = 22.f;
        c.mHeight = 128.f;
        return c;
    }

    class WallAt final : public WorldRay
    {
    public:
        explicit WallAt(double x)
            : mX(x)
        {
        }
        bool cast(const Vec& p0, const Vec& p1, double& fraction) override
        {
            ++mCalls;
            if (p0.x < mX && p1.x >= mX)
            {
                fraction = (mX - p0.x) / (p1.x - p0.x);
                return true;
            }
            return false;
        }
        double mX;
        int mCalls = 0;
    };

    Shot shotAt(
        double x, double y, double z, double vx, double vy, double vz, float gravity = 0.f, float range = 5000.f)
    {
        Shot s;
        s.mId = 1;
        s.mOwner = CombatSubject::of({ 9, 1 });
        s.mToken = 4;
        s.mPosition = { x, y, z };
        s.mVelocity = { vx, vy, vz };
        s.mGravity = gravity;
        s.mRangeLeft = range;
        return s;
    }

    TEST(NeutralProjectileCapsule, aSideHitIsAtTheSurfaceOfTheCylinder)
    {
        // Along +X at the capsule's mid height: enters at x = -22, that is 78 of 200 units.
        EXPECT_NEAR(segmentCapsule({ -100, 0, 64 }, { 100, 0, 64 }, capsuleAt(0, 0, 0)), 0.39, 1e-9);
    }

    TEST(NeutralProjectileCapsule, aSegmentOverTheHeadOrBesideTheBodyMisses)
    {
        EXPECT_LT(segmentCapsule({ -100, 0, 200 }, { 100, 0, 200 }, capsuleAt(0, 0, 0)), 0.0);
        EXPECT_LT(segmentCapsule({ 50, 0, 200 }, { 50, 0, -50 }, capsuleAt(0, 0, 0)), 0.0);
        EXPECT_LT(segmentCapsule({ -100, 40, 64 }, { 100, 40, 64 }, capsuleAt(0, 0, 0)), 0.0);
    }

    TEST(NeutralProjectileCapsule, theTopAndBottomCapsAreHitFromAbove)
    {
        // Down the axis from z = 200 to 0: the top of the capsule is at 128 (a sphere of 22 round z = 106): 72 of 200.
        EXPECT_NEAR(segmentCapsule({ 0, 0, 200 }, { 0, 0, 0 }, capsuleAt(0, 0, 0)), 0.36, 1e-9);
        // Up the axis from z = -100 to 50: the bottom is at z = 0: 100 of 150.
        EXPECT_NEAR(segmentCapsule({ 0, 0, -100 }, { 0, 0, 50 }, capsuleAt(0, 0, 0)), 100.0 / 150.0, 1e-9);
    }

    TEST(NeutralProjectileCapsule, aSegmentThatStartsInsideTouchesAtZeroAndBadNumbersNeverTouch)
    {
        EXPECT_EQ(segmentCapsule({ 0, 0, 64 }, { 100, 0, 64 }, capsuleAt(0, 0, 0)), 0.0);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        EXPECT_LT(segmentCapsule({ nan, 0, 64 }, { 100, 0, 64 }, capsuleAt(0, 0, 0)), 0.0);
        EXPECT_LT(segmentCapsule({ -100, 0, 64 }, { 100, 0, nan }, capsuleAt(0, 0, 0)), 0.0);
        Candidate bad = capsuleAt(0, 0, 0);
        bad.mRadius = 0.f;
        EXPECT_LT(segmentCapsule({ -100, 0, 64 }, { 100, 0, 64 }, bad), 0.0);
    }

    TEST(NeutralProjectileStep, aShotWithNoGravityFliesStraight)
    {
        Shot s = shotAt(0, 0, 0, 1000, 0, 0);
        const StepResult r = step(s, 0.1f, {}, nullptr);
        EXPECT_EQ(r.mOutcome, Outcome::Flying);
        EXPECT_NEAR(s.mPosition.x, 100.0, 1e-4); // dt is the float 0.1f
        EXPECT_NEAR(s.mPosition.z, 0.0, 1e-12);
        EXPECT_NEAR(s.mTravelled, 100.f, 1e-4f);
        EXPECT_NEAR(s.mAge, 0.1f, 1e-6f);
        EXPECT_NEAR(s.mRangeLeft, 4900.f, 1e-3f);
        EXPECT_NEAR(r.mDirection.x, 1.0, 1e-12);
    }

    TEST(NeutralProjectileStep, gravityBendsTheArcAsSemiImplicitEulerDoes)
    {
        // g = 100, dt = 0.5: vz = 0 - 100 * 0.5 = -50, then z = 0 + (-50) * 0.5 = -25; x = 1000 * 0.5 = 500.
        Shot s = shotAt(0, 0, 0, 1000, 0, 0, 100.f);
        step(s, 0.5f, {}, nullptr);
        EXPECT_NEAR(s.mVelocity.z, -50.0, 1e-9);
        EXPECT_NEAR(s.mPosition.z, -25.0, 1e-9);
        EXPECT_NEAR(s.mPosition.x, 500.0, 1e-9);
        step(s, 0.5f, {}, nullptr); // vz = -100, z = -25 - 50 = -75
        EXPECT_NEAR(s.mPosition.z, -75.0, 1e-9);
        EXPECT_NEAR(speedOf(s), std::sqrt(1000.0 * 1000.0 + 100.0 * 100.0), 1e-3);
    }

    TEST(NeutralProjectileStep, theRangeEndsTheFlightWhereItRunsOut)
    {
        Shot s = shotAt(0, 0, 0, 1000, 0, 0, 0.f, 150.f);
        const StepResult r = step(s, 0.2f, {}, nullptr); // 200 units asked, 150 allowed
        EXPECT_EQ(r.mOutcome, Outcome::Expired);
        EXPECT_NEAR(r.mAt.x, 150.0, 1e-9);
        EXPECT_NEAR(s.mTravelled, 150.f, 1e-3f);
        EXPECT_NEAR(s.mAge, 0.15f, 1e-5f);
    }

    TEST(NeutralProjectileStep, anActorInTheWayIsHitAtItsSurfaceWithTheTimeSpent)
    {
        // The segment is 0 -> 100; the capsule centre is at x = 80, so the surface is at 58: 0.58 of the step.
        Shot s = shotAt(0, 0, 64, 1000, 0, 0);
        const std::vector<Candidate> candidates = { capsuleAt(80, 0, 0, { 5, 1 }) };
        const StepResult r = step(s, 0.1f, candidates, nullptr);
        EXPECT_EQ(r.mOutcome, Outcome::HitActor);
        EXPECT_EQ(r.mActor, (ActorHandle{ 5, 1 }));
        EXPECT_NEAR(r.mAt.x, 58.0, 1e-9);
        EXPECT_NEAR(s.mPosition.x, 58.0, 1e-9);
        EXPECT_NEAR(s.mTravelled, 58.f, 1e-4f);
        EXPECT_NEAR(s.mAge, 0.058f, 1e-6f);
    }

    TEST(NeutralProjectileStep, theNearestOfSeveralActorsWinsAndTheOwnerIsNeverHit)
    {
        Shot s = shotAt(0, 0, 64, 1000, 0, 0);
        const std::vector<Candidate> candidates
            = { capsuleAt(90, 0, 0, { 7, 1 }), capsuleAt(70, 0, 0, { 6, 1 }), capsuleAt(10, 0, 0, { 9, 1 }) };
        // The third is the owner (handle 9/1) and stands on the path: it is skipped.
        const StepResult r = step(s, 0.1f, candidates, nullptr);
        EXPECT_EQ(r.mOutcome, Outcome::HitActor);
        EXPECT_EQ(r.mActor, (ActorHandle{ 6, 1 }));
        EXPECT_NEAR(r.mAt.x, 48.0, 1e-9);
    }

    TEST(NeutralProjectileStep, thePlayerIsHitUnlessItOwnsTheShot)
    {
        Candidate player = capsuleAt(60, 0, 0);
        player.mTarget = SweepTarget::Player;
        player.mActor = {};
        {
            Shot s = shotAt(0, 0, 64, 1000, 0, 0);
            const StepResult r = step(s, 0.1f, std::vector<Candidate>{ player }, nullptr);
            EXPECT_EQ(r.mOutcome, Outcome::HitPlayer);
            EXPECT_NEAR(r.mAt.x, 38.0, 1e-9);
        }
        {
            Shot s = shotAt(0, 0, 64, 1000, 0, 0);
            s.mOwner = CombatSubject::ofPlayer();
            const StepResult r = step(s, 0.1f, std::vector<Candidate>{ player }, nullptr);
            EXPECT_EQ(r.mOutcome, Outcome::Flying);
        }
    }

    TEST(NeutralProjectileStep, aNearerWallBeatsAnActorAndAFartherOneDoesNot)
    {
        const std::vector<Candidate> candidates = { capsuleAt(80, 0, 0) }; // surface at 58 of the 0 -> 100 step
        {
            Shot s = shotAt(0, 0, 64, 1000, 0, 0);
            WallAt wall(30.0);
            const StepResult r = step(s, 0.1f, candidates, &wall);
            EXPECT_EQ(r.mOutcome, Outcome::HitWorld);
            EXPECT_NEAR(r.mAt.x, 30.0, 1e-9);
            EXPECT_NEAR(s.mAge, 0.03f, 1e-6f);
        }
        {
            Shot s = shotAt(0, 0, 64, 1000, 0, 0);
            WallAt wall(59.0); // behind the actor's surface at 58
            const StepResult r = step(s, 0.1f, candidates, &wall);
            EXPECT_EQ(r.mOutcome, Outcome::HitActor);
        }
        {
            Shot s = shotAt(0, 0, 64, 1000, 0, 0);
            WallAt wall(500.0); // beyond the step: nothing
            EXPECT_EQ(step(s, 0.1f, candidates, &wall).mOutcome, Outcome::HitActor);
        }
    }

    TEST(NeutralProjectileStep, aStepThatIsNotAStepChangesNothing)
    {
        Shot s = shotAt(0, 0, 0, 1000, 0, 0);
        const Shot before = s;
        for (const float dt :
            { 0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            const StepResult r = step(s, dt, {}, nullptr);
            EXPECT_EQ(r.mOutcome, Outcome::Flying);
            EXPECT_EQ(s.mPosition.x, before.mPosition.x);
            EXPECT_EQ(s.mAge, before.mAge);
        }
    }

    TEST(NeutralProjectileStep, theSameFramesGiveTheSameArcBitForBit)
    {
        Shot a = shotAt(0, 0, 100, 3000, 1500, 400, 686.4f);
        Shot b = a;
        for (int i = 0; i < 60; ++i)
        {
            step(a, 1.f / 60.f, {}, nullptr);
            step(b, 1.f / 60.f, {}, nullptr);
        }
        EXPECT_EQ(a.mPosition.x, b.mPosition.x);
        EXPECT_EQ(a.mPosition.y, b.mPosition.y);
        EXPECT_EQ(a.mPosition.z, b.mPosition.z);
        EXPECT_EQ(a.mVelocity.z, b.mVelocity.z);
        EXPECT_LT(a.mPosition.z, 100.0 + 400.0); // it rose and fell: the arc is bent
    }

    TEST(NeutralProjectileStep, aFrameOfFlightDoesNotAllocate)
    {
        Shot s = shotAt(0, 0, 64, 1000, 0, 0, 686.4f, 1.0e6f);
        const std::vector<Candidate> candidates = { capsuleAt(5000, 0, 0), capsuleAt(0, 5000, 0) };
        WallAt wall(1.0e9);
        TestSupport::AllocationCounter counter;
        for (int i = 0; i < 1000; ++i)
            step(s, 1.f / 60.f, candidates, &wall);
        EXPECT_EQ(counter.count(), 0u);
    }
}
