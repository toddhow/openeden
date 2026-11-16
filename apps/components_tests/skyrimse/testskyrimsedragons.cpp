// Task 0253 (dragons, first slice): the pure half (game_skyrimse/dragons.hpp). No game data. Numbers by hand:
//  - the profile: walk 512 units in 1.33333 s (the motion block of mtforwardground), so 384 units a second; the capsule
//    is 414 units high (the skin's bounds) and 150 wide (ours);
//  - the flight: 4 s take-off, 3.33333 s approach and 1.33333 s touch-down (the clips' durations), the glide's 1536
//    units a second, the circle's radius 1500 round the player, the cruise height 900 above where it took off;
//  - the cone: 25 degrees half angle (0.4363 rad), the soul after 4 s within 2000 units.
#include <game_skyrimse/dragons.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::dragons;

    constexpr double kPi = 3.14159265358979323846;

    // ---- the profile ------------------------------------------------------------------------------------------------

    TEST(SkyrimDragonTable, theDragonRowHasTheMeasuredClipsAndNumbers)
    {
        const CreatureProfile& p = dragonProfile();
        EXPECT_EQ(p.mName, "dragon");
        EXPECT_EQ(p.mProject, "dragonproject");
        EXPECT_EQ(p.mIdle, "ground_combatidle");
        EXPECT_EQ(p.mWalk, "mtforwardground");
        EXPECT_EQ(p.mAttack, "ground_bite");
        EXPECT_EQ(p.mPower, "ground_tailwhip");
        EXPECT_EQ(p.mStagger, "mtland_staggerheavy");
        EXPECT_EQ(p.mDeath, "mtdeathintro");
        EXPECT_TRUE(p.mRecoil.empty()) << "a dragon does not recoil from every blow";
        EXPECT_NEAR(p.mWalkStride, 512.f, 1e-3f);
        EXPECT_NEAR(p.mWalkSpeed, 512.f / 1.33333f, 0.1f);
        EXPECT_NEAR(p.mCapsuleHeight, 414.f, 1.f);
        EXPECT_NEAR(p.mCapsuleRadius, 150.f, 1.f);
    }

    TEST(SkyrimDragonTable, theSkeletonPathFindsTheRowAndNothingElseDoes)
    {
        EXPECT_EQ(dragonProfileFor("Actors\\Dragon\\Character Assets\\Skeleton.nif"), &dragonProfile());
        EXPECT_EQ(dragonProfileFor("meshes/actors/dragon/character assets/skeleton.nif"), &dragonProfile());
        EXPECT_EQ(dragonProfileFor("MESHES\\ACTORS\\DRAGON\\CHARACTER ASSETS\\SKELETON.NIF"), &dragonProfile());
        EXPECT_EQ(dragonProfileFor("actors/dragonpriest/character assets/skeleton.nif"), nullptr);
        EXPECT_EQ(dragonProfileFor("actors/wolf/dragon/character assets/skeleton.nif"), nullptr);
        EXPECT_EQ(dragonProfileFor("actors/dragon/character assets/dragon.nif"), nullptr);
        EXPECT_EQ(dragonProfileFor("actors/canine/character assets wolf/skeleton.nif"), nullptr);
        EXPECT_EQ(dragonProfileFor(""), nullptr);
    }

    TEST(SkyrimDragonTable, onlyTheDragonRowIsADragon)
    {
        EXPECT_TRUE(isDragon(&dragonProfile()));
        EXPECT_FALSE(isDragon(nullptr));
        EXPECT_FALSE(isDragon(&wolfProfile()));
    }

    TEST(SkyrimDragonTable, theOlderCreatureLookupStillDoesNotKnowTheDragon)
    {
        EXPECT_EQ(creatureProfileFor("Actors\\Dragon\\Character Assets\\Skeleton.nif"), nullptr);
        EXPECT_EQ(creatureProfilesAll().size(), 17u);
    }

    // ---- the flight -------------------------------------------------------------------------------------------------

    FlightSettings quick()
    {
        FlightSettings s;
        s.mGroundSeconds = 2.f;
        s.mAirSeconds = 5.f;
        return s;
    }

    FlightInput input(double x, double y, double z, bool engaged = true)
    {
        FlightInput in;
        in.mDt = 0.1f;
        in.mX = x;
        in.mY = y;
        in.mZ = z;
        in.mEngaged = engaged;
        return in;
    }

    // Runs a flight from the ground for `seconds`, feeding back the position it places the dragon at.
    struct FlightSim
    {
        Flight mFlight;
        FlightInput mIn;
        FlightOutput mLast;
        std::vector<FlightOutput> mAll;
        explicit FlightSim(const FlightSettings& s = quick())
            : mFlight(s)
            , mIn(input(0.0, -1000.0, 10.0))
        {
        }
        void step(int n)
        {
            for (int i = 0; i < n; ++i)
            {
                mLast = mFlight.step(mIn);
                mAll.push_back(mLast);
                if (mLast.mPlace)
                {
                    mIn.mX = mLast.mX;
                    mIn.mY = mLast.mY;
                    mIn.mZ = mLast.mZ;
                    mIn.mHeading = mLast.mHeading;
                }
            }
        }
        void untilPhase(Phase phase, int limit = 2000)
        {
            for (int i = 0; i < limit && mFlight.phase() != phase; ++i)
                step(1);
        }
    };

    TEST(SkyrimDragonFlight, aDragonThatIsNotFightingStaysOnTheGroundForever)
    {
        FlightSim run;
        run.mIn.mEngaged = false;
        run.step(1000);
        EXPECT_EQ(run.mFlight.phase(), Phase::Ground);
        for (const FlightOutput& o : run.mAll)
        {
            EXPECT_FALSE(o.mPlace);
            EXPECT_FALSE(o.mLand);
        }
    }

    TEST(SkyrimDragonFlight, itTakesOffAfterItsGroundSecondsOfFighting)
    {
        FlightSim run;
        run.step(19);
        EXPECT_EQ(run.mFlight.phase(), Phase::Ground) << "1.9 s of 2.0";
        run.step(2);
        ASSERT_EQ(run.mFlight.phase(), Phase::Takeoff);
        // the step that began the take-off starts the pose
        bool started = false;
        for (const FlightOutput& o : run.mAll)
            if (o.mStartPose)
            {
                started = true;
                EXPECT_EQ(o.mClip, kClipTakeoff);
                EXPECT_TRUE(o.mPlace);
            }
        EXPECT_TRUE(started);
        EXPECT_EQ(run.mFlight.flights(), 1u);
    }

    TEST(SkyrimDragonFlight, theGroundTimeCountsOnlyWhileFightingAndNotBusy)
    {
        FlightSim run;
        run.mIn.mBusy = true;
        run.step(100);
        EXPECT_EQ(run.mFlight.phase(), Phase::Ground) << "swinging or breathing: no take-off, no counting";
        run.mIn.mBusy = false;
        run.step(25);
        EXPECT_EQ(run.mFlight.phase(), Phase::Takeoff);
    }

    TEST(SkyrimDragonFlight, withFlightOffItNeverTakesOff)
    {
        FlightSettings s = quick();
        s.mEnabled = false;
        FlightSim run(s);
        run.step(2000);
        EXPECT_EQ(run.mFlight.phase(), Phase::Ground);
        EXPECT_EQ(run.mFlight.flights(), 0u);
    }

    TEST(SkyrimDragonFlight, theTakeoffRisesToTheCruiseHeightInFourSecondsStandingOverItsPlace)
    {
        FlightSim run;
        run.untilPhase(Phase::Takeoff);
        const double z0 = run.mIn.mZ;
        const double x0 = run.mIn.mX, y0 = run.mIn.mY;
        double last = z0;
        for (int i = 0; i < 39; ++i)
        {
            run.step(1);
            ASSERT_EQ(run.mFlight.phase(), Phase::Takeoff) << i;
            EXPECT_GE(run.mLast.mZ, last - 1e-9) << "monotonic " << i;
            last = run.mLast.mZ;
            EXPECT_NEAR(run.mLast.mX, x0, 1e-6);
            EXPECT_NEAR(run.mLast.mY, y0, 1e-6);
            EXPECT_EQ(run.mLast.mClip, kClipTakeoff);
        }
        run.step(2);
        EXPECT_EQ(run.mFlight.phase(), Phase::Cruise);
        EXPECT_NEAR(run.mLast.mZ, 10.0 + 900.0, 40.0) << "the take-off ended at the cruise height (first cruise step)";
        EXPECT_EQ(run.mLast.mClip, kClipCruise);
        EXPECT_TRUE(run.mAll[run.mAll.size() - 2].mStartPose) << "the first cruise step starts the new loop";
    }

    TEST(SkyrimDragonFlight, theCruiseCirclesThePlayerAtTheGlideSpeedAndTheCruiseHeight)
    {
        FlightSim run;
        run.untilPhase(Phase::Cruise);
        run.step(30); // 3 s: the radius has settled
        std::vector<FlightOutput> samples;
        for (int i = 0; i < 10; ++i)
        {
            run.step(1);
            samples.push_back(run.mLast);
        }
        ASSERT_EQ(run.mFlight.phase(), Phase::Cruise);
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            const double dx = samples[i].mX - samples[i - 1].mX;
            const double dy = samples[i].mY - samples[i - 1].mY;
            EXPECT_NEAR(std::hypot(dx, dy) / 0.1, kGlideSpeed, 40.0) << i;
            EXPECT_NEAR(samples[i].mZ, 10.0 + 900.0, 1.0);
            EXPECT_FALSE(samples[i].mStartPose);
        }
        for (const FlightOutput& o : samples)
        {
            const double radius = std::hypot(o.mX, o.mY); // the player stands at the origin
            EXPECT_NEAR(radius, 1500.0, 40.0);
        }
    }

    TEST(SkyrimDragonFlight, theCruiseFacesAlongItsPath)
    {
        FlightSim run;
        run.untilPhase(Phase::Cruise);
        run.step(40);
        const FlightOutput a = run.mLast;
        run.step(1);
        const FlightOutput b = run.mLast;
        const double travel = std::atan2(b.mX - a.mX, b.mY - a.mY);
        double diff = std::fmod(static_cast<double>(b.mHeading) - travel + 3.0 * kPi, 2.0 * kPi) - kPi;
        EXPECT_LT(std::abs(diff), 0.25);
    }

    TEST(SkyrimDragonFlight, afterItsAirSecondsItApproachesLandsAndGoesBackToTheGround)
    {
        FlightSim run;
        run.untilPhase(Phase::Cruise);
        run.step(50);
        EXPECT_EQ(run.mFlight.phase(), Phase::Approach);
        EXPECT_EQ(run.mLast.mClip, kClipApproach);
        const double top = run.mLast.mZ;
        run.step(34); // 3.3 s
        EXPECT_EQ(run.mFlight.phase(), Phase::Touchdown) << "approach is 3.333 s";
        EXPECT_EQ(run.mLast.mClip, kClipTouchdown);
        EXPECT_LT(run.mLast.mZ, top - 600.0);
        // the landing point is 900 units from the player
        EXPECT_NEAR(std::hypot(run.mLast.mX, run.mLast.mY), 900.0, 60.0);
        bool landed = false;
        for (int i = 0; i < 20 && !landed; ++i)
        {
            run.step(1);
            landed = run.mLast.mLand;
        }
        ASSERT_TRUE(landed);
        EXPECT_EQ(run.mFlight.phase(), Phase::Ground);
        EXPECT_NEAR(run.mLast.mZ, 0.0, 30.0) << "the player's ground";
        EXPECT_FALSE(run.mFlight.airborne());
    }

    TEST(SkyrimDragonFlight, aSecondFlightNeedsAnotherGroundPeriod)
    {
        FlightSim run;
        run.untilPhase(Phase::Cruise);
        run.untilPhase(Phase::Ground);
        run.mIn.mX = 500.0;
        run.mIn.mY = 0.0;
        run.mIn.mZ = 0.0;
        run.step(15);
        EXPECT_EQ(run.mFlight.phase(), Phase::Ground);
        run.step(10);
        EXPECT_EQ(run.mFlight.phase(), Phase::Takeoff);
        EXPECT_EQ(run.mFlight.flights(), 2u);
    }

    TEST(SkyrimDragonFlight, aDragonThatDiesInTheAirIsTakenOffThePoseWhereItIs)
    {
        FlightSim run;
        run.untilPhase(Phase::Cruise);
        run.step(10);
        run.mIn.mDead = true;
        run.step(1);
        EXPECT_TRUE(run.mLast.mLand);
        EXPECT_EQ(run.mFlight.phase(), Phase::Ground);
        EXPECT_NEAR(run.mLast.mZ, 10.0 + 900.0, 40.0) << "where it was, not teleported";
        run.step(500);
        EXPECT_EQ(run.mFlight.phase(), Phase::Ground) << "a dead dragon never flies again";
        EXPECT_EQ(run.mFlight.flights(), 1u);
    }

    TEST(SkyrimDragonFlight, aDragonThatDiesOnTheGroundDoesNothing)
    {
        FlightSim run;
        run.mIn.mDead = true;
        run.step(500);
        for (const FlightOutput& o : run.mAll)
            EXPECT_FALSE(o.mPlace || o.mLand);
    }

    TEST(SkyrimDragonFlight, whenTheFightEndsInTheAirItLandsAtOnce)
    {
        FlightSim run;
        run.untilPhase(Phase::Cruise);
        run.step(5);
        run.mIn.mEngaged = false;
        run.step(1);
        EXPECT_EQ(run.mFlight.phase(), Phase::Approach);
    }

    TEST(SkyrimDragonFlight, aValueThatIsNotFiniteIsRefusedAndMovesNothing)
    {
        FlightSim run;
        run.untilPhase(Phase::Cruise);
        run.step(5);
        FlightInput bad = run.mIn;
        bad.mPlayerX = std::nan("");
        const FlightOutput out = run.mFlight.step(bad);
        EXPECT_FALSE(out.mPlace);
        EXPECT_EQ(run.mFlight.phase(), Phase::Cruise);
        bad = run.mIn;
        bad.mZ = INFINITY;
        EXPECT_FALSE(run.mFlight.step(bad).mPlace);
        bad = run.mIn;
        bad.mDt = std::nanf("");
        EXPECT_FALSE(run.mFlight.step(bad).mPlace);
    }

    TEST(SkyrimDragonFlight, twoFlightsGivenTheSameInputsGiveTheSameOutputs)
    {
        FlightSim a;
        FlightSim b;
        a.step(900);
        b.step(900);
        ASSERT_EQ(a.mAll.size(), b.mAll.size());
        for (std::size_t i = 0; i < a.mAll.size(); ++i)
        {
            EXPECT_EQ(a.mAll[i].mX, b.mAll[i].mX) << i;
            EXPECT_EQ(a.mAll[i].mY, b.mAll[i].mY) << i;
            EXPECT_EQ(a.mAll[i].mZ, b.mAll[i].mZ) << i;
            EXPECT_EQ(a.mAll[i].mHeading, b.mAll[i].mHeading) << i;
            EXPECT_EQ(a.mAll[i].mPhase, b.mAll[i].mPhase) << i;
        }
    }

    TEST(SkyrimDragonFlight, theHeadingTurnsNoFasterThanItsTurnRate)
    {
        FlightSim run;
        run.untilPhase(Phase::Cruise);
        run.mIn.mFaceTarget = true;
        float previous = run.mLast.mHeading;
        for (int i = 0; i < 60; ++i)
        {
            run.step(1);
            double d = std::fmod(static_cast<double>(run.mLast.mHeading) - previous + 3.0 * kPi, 2.0 * kPi) - kPi;
            EXPECT_LE(std::abs(d), 2.0 * 0.1 + 1e-4) << i;
            previous = run.mLast.mHeading;
        }
    }

    // ---- the breath cone ---------------------------------------------------------------------------------------------

    TEST(SkyrimDragonBreath, aPointOnTheAxisWithinRangeIsInsideAndOneBeyondIsNot)
    {
        const Cone c = breathCone(0, 0, 0, 0, 1000, 0, 1600.f, kBreathHalfAngle);
        EXPECT_NEAR(c.mAy, 1.0, 1e-9);
        EXPECT_TRUE(coneContains(c, 0, 1000, 0));
        EXPECT_TRUE(coneContains(c, 0, 1599, 0));
        EXPECT_FALSE(coneContains(c, 0, 1601, 0));
        EXPECT_TRUE(coneContains(c, 0, 0, 0)) << "the apex";
    }

    TEST(SkyrimDragonBreath, theHalfAngleIsTwentyFiveDegreesAndBehindIsOutside)
    {
        const Cone c = breathCone(0, 0, 0, 0, 1000, 0, 1600.f, kBreathHalfAngle);
        // 1000 ahead: the edge is 1000 x tan(25 deg) = 466.3 to the side
        EXPECT_TRUE(coneContains(c, 460, 1000, 0));
        EXPECT_FALSE(coneContains(c, 475, 1000, 0));
        EXPECT_FALSE(coneContains(c, 0, -100, 0));
        EXPECT_FALSE(coneContains(c, 1000, 0, 0)) << "90 degrees off";
    }

    TEST(SkyrimDragonBreath, theAxisMayPointDownAtAPlayerBelow)
    {
        const Cone c = breathCone(0, 0, 1400, 600, 0, 0, 2000.f, kBreathHalfAngle);
        EXPECT_LT(c.mAz, 0.0);
        EXPECT_NEAR(std::sqrt(c.mAx * c.mAx + c.mAy * c.mAy + c.mAz * c.mAz), 1.0, 1e-9);
        EXPECT_TRUE(coneContains(c, 600, 0, 0));
        EXPECT_FALSE(coneContains(c, 0, 0, 1500)) << "straight up";
    }

    TEST(SkyrimDragonBreath, anAimThatIsNotUsableGivesAConeThatContainsNothing)
    {
        const Cone zero = breathCone(5, 5, 5, 5, 5, 5, 1600.f, kBreathHalfAngle);
        EXPECT_FALSE(coneContains(zero, 5, 5, 5));
        EXPECT_FALSE(coneContains(zero, 5, 100, 5));
        const Cone nan = breathCone(std::nan(""), 0, 0, 1, 1, 1, 1600.f, kBreathHalfAngle);
        EXPECT_FALSE(coneContains(nan, 1, 1, 1));
        const Cone fine = breathCone(0, 0, 0, 0, 1, 0, 1600.f, kBreathHalfAngle);
        EXPECT_FALSE(coneContains(fine, std::nan(""), 100, 0));
        EXPECT_FALSE(coneContains(fine, 0, INFINITY, 0));
    }

    // ---- the soul ---------------------------------------------------------------------------------------------------

    TEST(SkyrimDragonSoul, itIsTakenAfterTheDelayWhenThePlayerIsNear)
    {
        EXPECT_FALSE(soulAbsorbs(3.9f, 500.0));
        EXPECT_TRUE(soulAbsorbs(4.0f, 500.0));
        EXPECT_TRUE(soulAbsorbs(30.f, 2000.0));
        EXPECT_FALSE(soulAbsorbs(30.f, 2000.5));
        EXPECT_FALSE(soulAbsorbs(-1.f, 10.0));
        EXPECT_FALSE(soulAbsorbs(std::nanf(""), 10.0));
        EXPECT_FALSE(soulAbsorbs(10.f, std::nan("")));
    }
}
