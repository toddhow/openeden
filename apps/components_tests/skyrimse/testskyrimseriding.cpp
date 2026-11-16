// Task 0251 (horse riding, first slice): the rules of mounting, riding and dismounting, over a fake core. No plugin
// bytes, no game data. The numbers asserted are the ones in game_skyrimse/riding.hpp and the horse row of
// creatures.cpp (walk 137.624 units in 1.1 s, run 212.32 in 0.466667 s).
#include <game_skyrimse/riding.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr float kHorseWalk = 125.1127f;
    constexpr float kHorseRun = 454.9714f;
    constexpr WorldId kHorseRef = 77;
    constexpr WorldId kHorseBase = 0x1234;

    // A core that holds one horse and the player.
    class RidingFakeCore final : public ActorRequestWriter, public RidingWriter
    {
    public:
        bool setMovement(ActorHandle handle, float velX, float velY) override
        {
            ++mMovements;
            mHandle = handle;
            mVelX = velX;
            mVelY = velY;
            return true;
        }
        bool setHeading(ActorHandle handle, float heading) override
        {
            (void)handle;
            ++mHeadings;
            mHeadingSet = heading;
            return true;
        }
        bool actorPosition(ActorHandle handle, double& x, double& y, double& z) const override
        {
            if (!mHorseThere || handle.reference != kHorseRef)
                return false;
            x = mHorseX;
            y = mHorseY;
            z = mHorseZ;
            return true;
        }
        bool riderControls(RiderControls& out) const override
        {
            if (!mHaveControls)
                return false;
            out = mControls;
            return true;
        }
        bool carryPlayer(double x, double y, double z, float heading) override
        {
            ++mCarries;
            mCarryX = x;
            mCarryY = y;
            mCarryZ = z;
            mCarryHeading = heading;
            return true;
        }
        bool releasePlayer(double x, double y, double z) override
        {
            ++mReleases;
            mReleaseX = x;
            mReleaseY = y;
            mReleaseZ = z;
            return true;
        }

        RiderControls mControls;
        bool mHaveControls = true;
        bool mHorseThere = true;
        double mHorseX = 1000.0, mHorseY = 2000.0, mHorseZ = 300.0;
        ActorHandle mHandle;
        float mVelX = 0.f, mVelY = 0.f, mHeadingSet = 0.f;
        int mMovements = 0, mHeadings = 0, mCarries = 0, mReleases = 0;
        double mCarryX = 0, mCarryY = 0, mCarryZ = 0, mReleaseX = 0, mReleaseY = 0, mReleaseZ = 0;
        float mCarryHeading = 0.f;
    };

    ActorHandle horseHandle()
    {
        ActorHandle handle;
        handle.reference = kHorseRef;
        handle.session = 5;
        return handle;
    }

    ActorInfo horseInfo(std::string_view editorId)
    {
        ActorInfo info;
        info.handle = horseHandle();
        info.base = kHorseBase;
        info.editorId = editorId;
        return info;
    }

    struct RidingRig
    {
        Riding mRiding;
        RidingFakeCore mCore;
        int mLookups = 0;
        std::vector<std::string> mLog;
        std::vector<std::pair<WorldId, bool>> mBrain;

        explicit RidingRig(bool enabled = true, const char* owned = "LvlHorse")
        {
            mRiding.setEnabled(enabled);
            mRiding.setTestOwned(owned);
            mRiding.setHorseLookup([this](WorldId base, std::uint32_t variation, HorseInfo& out) {
                ++mLookups;
                (void)variation;
                if (base != kHorseBase)
                    return false;
                out.mWalkSpeed = kHorseWalk;
                out.mRunSpeed = kHorseRun;
                return true;
            });
            mRiding.setBrainControl([this](WorldId reference, bool stands) { mBrain.push_back({ reference, stands }); });
            mRiding.setLog([this](const std::string& line) { mLog.push_back(line); });
            mRiding.onActorAdded(horseInfo("LvlHorse"));
        }

        void mount()
        {
            mRiding.onMountRequested(horseHandle());
            mCore.mControls.valid = true;
            mCore.mControls.heading = 0.5f;
            mRiding.update(0.016f, mCore);
        }
    };

    // ---- the pure rules --------------------------------------------------------------------------------------

    TEST(SkyrimRidingRules, theGaitFollowsTheForwardControlAndTheRunControl)
    {
        RiderControls c;
        c.valid = true;
        EXPECT_EQ(gaitFor(c), Gait::Stand);
        c.forward = 1.f;
        EXPECT_EQ(gaitFor(c), Gait::Walk);
        c.run = true;
        EXPECT_EQ(gaitFor(c), Gait::Run);
        c.forward = -1.f;
        EXPECT_EQ(gaitFor(c), Gait::Stand);
        c.forward = kRidingDeadZone * 0.5f;
        EXPECT_EQ(gaitFor(c), Gait::Stand);
        c.forward = 1.f;
        c.valid = false;
        EXPECT_EQ(gaitFor(c), Gait::Stand);
        c.valid = true;
        c.forward = std::nanf("");
        EXPECT_EQ(gaitFor(c), Gait::Stand);
    }

    TEST(SkyrimRidingRules, theHeadingTurnsRightForAPositiveTurnAtTheTurnRate)
    {
        EXPECT_NEAR(nextRiderHeading(0.f, 1.f, 1.f), kRidingTurnRate, 1e-5f);
        EXPECT_NEAR(nextRiderHeading(0.f, -1.f, 0.5f), -0.5f * kRidingTurnRate, 1e-5f);
        EXPECT_NEAR(nextRiderHeading(1.f, 0.f, 1.f), 1.f, 1e-6f);
        // The turn is clamped to one.
        EXPECT_NEAR(nextRiderHeading(0.f, 7.f, 1.f), kRidingTurnRate, 1e-5f);
        // A heading wraps into (-pi, pi].
        EXPECT_LT(nextRiderHeading(3.f, 1.f, 1.f), 0.f);
        // Anything that is not finite changes nothing, or gives 0 for the heading itself.
        EXPECT_EQ(nextRiderHeading(1.f, std::nanf(""), 1.f), 1.f);
        EXPECT_EQ(nextRiderHeading(1.f, 1.f, -1.f), 1.f);
        EXPECT_EQ(nextRiderHeading(std::nanf(""), 1.f, 1.f), 0.f);
    }

    TEST(SkyrimRidingRules, theVelocityPointsAlongTheHeadingAtTheHorsesOwnSpeeds)
    {
        const Velocity north = velocityFor(Gait::Walk, 0.f, 100.f, 400.f);
        EXPECT_NEAR(north.mX, 0.f, 1e-4f);
        EXPECT_NEAR(north.mY, 100.f, 1e-4f);
        const Velocity east = velocityFor(Gait::Run, 1.5707963f, 100.f, 400.f);
        EXPECT_NEAR(east.mX, 400.f, 1e-3f);
        EXPECT_NEAR(east.mY, 0.f, 1e-3f);
        const Velocity still = velocityFor(Gait::Stand, 0.f, 100.f, 400.f);
        EXPECT_EQ(still.mX, 0.f);
        EXPECT_EQ(still.mY, 0.f);
        const Velocity noSpeed = velocityFor(Gait::Run, 0.f, 100.f, 0.f);
        EXPECT_EQ(noSpeed.mY, 0.f);
    }

    TEST(SkyrimRidingRules, theSaddleTurnsWithTheHorseAndTheDismountIsToItsRight)
    {
        const Point saddle = saddlePoint(10.0, 20.0, 30.0, 0.f);
        EXPECT_NEAR(saddle.mX, 10.0 + kSaddleX, 1e-4);
        EXPECT_NEAR(saddle.mY, 20.0 + kSaddleY, 1e-4);
        EXPECT_NEAR(saddle.mZ, 30.0 + kSaddleZ + kRiderLift, 1e-4);
        // A horse facing east (heading pi/2): its forward is +X, so a saddle offset forward by y lies to the east.
        const Point turned = saddlePoint(0.0, 0.0, 0.0, 1.5707963f);
        EXPECT_NEAR(turned.mX, kSaddleY, 1e-3);
        EXPECT_NEAR(turned.mY, -kSaddleX, 1e-3);

        const Point down = dismountPoint(0.0, 0.0, 5.0, 0.f);
        EXPECT_NEAR(down.mX, kDismountSide, 1e-3); // facing north, right is east
        EXPECT_NEAR(down.mY, 0.0, 1e-3);
        EXPECT_EQ(down.mZ, 5.0);
        const Point downEast = dismountPoint(0.0, 0.0, 5.0, 1.5707963f);
        EXPECT_NEAR(downEast.mX, 0.0, 1e-3);
        EXPECT_NEAR(downEast.mY, -kDismountSide, 1e-3); // facing east, right is south
    }

    TEST(SkyrimRidingRules, theRiderClipOfEachGaitIsInTheHorseRiderFolder)
    {
        EXPECT_EQ(riderClipFor(Gait::Stand), "horse_rider/idle");
        EXPECT_EQ(riderClipFor(Gait::Walk), "horse_rider/walkforward");
        EXPECT_EQ(riderClipFor(Gait::Run), "horse_rider/runforward");
    }

    // ---- mounting --------------------------------------------------------------------------------------------

    TEST(SkyrimRidingMount, aHorseTheTestSettingNamesCanBeMountedAndOthersCannot)
    {
        RidingRig rig;
        EXPECT_TRUE(rig.mRiding.canMount(horseHandle()));
        RidingRig other(true, "SomeOtherHorse");
        EXPECT_FALSE(other.mRiding.canMount(horseHandle()));
        RidingRig any(true, "*");
        EXPECT_TRUE(any.mRiding.canMount(horseHandle()));
        RidingRig none(true, "");
        EXPECT_FALSE(none.mRiding.canMount(horseHandle()));
    }

    TEST(SkyrimRidingMount, theTestNameIsCaseInsensitive)
    {
        RidingRig rig(true, "lvlhorse");
        EXPECT_TRUE(rig.mRiding.canMount(horseHandle()));
    }

    TEST(SkyrimRidingMount, offTracksNothingAndRefusesEverything)
    {
        RidingRig rig(false);
        EXPECT_FALSE(rig.mRiding.canMount(horseHandle()));
        rig.mount();
        EXPECT_FALSE(rig.mRiding.riding());
        EXPECT_EQ(rig.mCore.mCarries, 0);
        EXPECT_EQ(rig.mCore.mMovements, 0);
        EXPECT_EQ(rig.mLookups, 0);
    }

    TEST(SkyrimRidingMount, anActorThatIsNotAHorseCannotBeMounted)
    {
        RidingRig rig(true, "*");
        ActorInfo person = horseInfo("Hulda");
        person.handle.reference = 99;
        person.base = 0x777;
        rig.mRiding.onActorAdded(person);
        EXPECT_FALSE(rig.mRiding.canMount(person.handle));
    }

    TEST(SkyrimRidingMount, aStaleHandleCannotBeMounted)
    {
        RidingRig rig;
        ActorHandle stale = horseHandle();
        stale.session = 6;
        EXPECT_FALSE(rig.mRiding.canMount(stale));
        EXPECT_FALSE(rig.mRiding.canMount(ActorHandle{}));
    }

    TEST(SkyrimRidingMount, theLookupIsAskedOncePerHorse)
    {
        RidingRig rig;
        (void)rig.mRiding.canMount(horseHandle());
        (void)rig.mRiding.canMount(horseHandle());
        EXPECT_EQ(rig.mLookups, 1);
    }

    TEST(SkyrimRidingMount, aRequestMountsInTheNextUpdateAndTheBrainStandsAside)
    {
        RidingRig rig;
        rig.mRiding.onMountRequested(horseHandle());
        EXPECT_FALSE(rig.mRiding.riding());
        rig.mCore.mControls.valid = true;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_TRUE(rig.mRiding.riding());
        EXPECT_EQ(rig.mRiding.mounts(), 1u);
        ASSERT_EQ(rig.mBrain.size(), 1u);
        EXPECT_EQ(rig.mBrain[0].first, kHorseRef);
        EXPECT_TRUE(rig.mBrain[0].second);
        EXPECT_EQ(rig.mRiding.clip(), "horse_rider/idle");
        EXPECT_GE(rig.mCore.mCarries, 1);
    }

    TEST(SkyrimRidingMount, aRequestForAHorseTheCoreCannotLocateDoesNotMount)
    {
        RidingRig rig;
        rig.mCore.mHorseThere = false;
        rig.mount();
        EXPECT_FALSE(rig.mRiding.riding());
        EXPECT_EQ(rig.mCore.mCarries, 0);
    }

    TEST(SkyrimRidingMount, aWriterThatIsNotARidingWriterIsToldNothing)
    {
        // The plain writer of an older core: the request is dropped.
        class PlainWriter final : public ActorRequestWriter
        {
        public:
            bool setMovement(ActorHandle, float, float) override { return true; }
        };
        RidingRig rig;
        PlainWriter plain;
        rig.mRiding.onMountRequested(horseHandle());
        rig.mRiding.update(0.016f, plain);
        EXPECT_FALSE(rig.mRiding.riding());
    }

    // ---- riding ----------------------------------------------------------------------------------------------

    TEST(SkyrimRidingRide, forwardWalksTheHorseAlongTheViewAtItsWalkSpeedAndRunRunsIt)
    {
        RidingRig rig;
        rig.mount();
        rig.mCore.mControls.heading = 0.f;
        rig.mCore.mControls.forward = 1.f;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mRiding.gait(), Gait::Walk);
        EXPECT_NEAR(rig.mCore.mVelY, kHorseWalk, 1e-3f);
        EXPECT_NEAR(rig.mCore.mVelX, 0.f, 1e-3f);
        EXPECT_EQ(rig.mRiding.clip(), "horse_rider/walkforward");
        rig.mCore.mControls.run = true;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mRiding.gait(), Gait::Run);
        EXPECT_NEAR(rig.mCore.mVelY, kHorseRun, 1e-2f);
        EXPECT_EQ(rig.mRiding.clip(), "horse_rider/runforward");
    }

    TEST(SkyrimRidingRide, standingStopsTheHorseAndTurnsItToTheHeading)
    {
        RidingRig rig;
        rig.mount();
        rig.mCore.mControls.forward = 0.f;
        rig.mCore.mControls.heading = 1.f;
        const int headings = rig.mCore.mHeadings;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mVelX, 0.f);
        EXPECT_EQ(rig.mCore.mVelY, 0.f);
        EXPECT_EQ(rig.mCore.mHeadings, headings + 1);
        EXPECT_NEAR(rig.mCore.mHeadingSet, 1.f, 1e-5f);
    }

    TEST(SkyrimRidingRide, theTurnControlTurnsTheHorseWhileItGoes)
    {
        RidingRig rig;
        rig.mount();
        rig.mCore.mControls.heading = 0.f;
        rig.mCore.mControls.forward = 1.f;
        rig.mCore.mControls.turn = 1.f;
        rig.mRiding.update(0.5f, rig.mCore);
        const float expected = 0.5f * kRidingTurnRate;
        EXPECT_NEAR(rig.mCore.mCarryHeading, expected, 1e-5f);
        EXPECT_NEAR(rig.mCore.mVelX, std::sin(expected) * kHorseWalk, 1e-2f);
        EXPECT_GT(rig.mCore.mVelX, 0.f);
    }

    TEST(SkyrimRidingRide, thePlayerIsCarriedToTheSaddleEveryFrameFacingTheHeading)
    {
        RidingRig rig;
        rig.mount();
        rig.mCore.mControls.heading = 0.f;
        rig.mCore.mHorseX = 50.0;
        rig.mCore.mHorseY = 60.0;
        rig.mCore.mHorseZ = 70.0;
        const int carries = rig.mCore.mCarries;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mCarries, carries + 1);
        const Point expected = saddlePoint(50.0, 60.0, 70.0, 0.f);
        EXPECT_NEAR(rig.mCore.mCarryX, expected.mX, 1e-4);
        EXPECT_NEAR(rig.mCore.mCarryY, expected.mY, 1e-4);
        EXPECT_NEAR(rig.mCore.mCarryZ, expected.mZ, 1e-4);
        EXPECT_NEAR(rig.mCore.mCarryHeading, 0.f, 1e-5f);
    }

    TEST(SkyrimRidingRide, noControlsMeansTheHorseStandsStill)
    {
        RidingRig rig;
        rig.mount();
        rig.mCore.mControls.forward = 1.f;
        rig.mRiding.update(0.016f, rig.mCore);
        rig.mCore.mHaveControls = false;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mRiding.gait(), Gait::Stand);
        EXPECT_EQ(rig.mCore.mVelY, 0.f);
    }

    // ---- dismounting -----------------------------------------------------------------------------------------

    TEST(SkyrimRidingDismount, theJumpControlGetsOffAndPutsThePlayerDownBesideTheHorse)
    {
        RidingRig rig;
        rig.mount();
        rig.mCore.mControls.heading = 0.f;
        rig.mCore.mControls.jump = true;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_FALSE(rig.mRiding.riding());
        EXPECT_EQ(rig.mRiding.dismounts(), 1u);
        ASSERT_EQ(rig.mCore.mReleases, 1);
        // He is put down to the right of the horse as it faced when he got off (the heading of the last carry).
        const Point down = dismountPoint(1000.0, 2000.0, 300.0, 0.5f);
        EXPECT_NEAR(rig.mCore.mReleaseX, down.mX, 1e-3);
        EXPECT_NEAR(rig.mCore.mReleaseY, down.mY, 1e-3);
        EXPECT_EQ(rig.mCore.mVelX, 0.f);
        EXPECT_EQ(rig.mCore.mVelY, 0.f);
        EXPECT_EQ(rig.mBrain.back().second, false);
        EXPECT_EQ(rig.mRiding.clip(), "");
    }

    TEST(SkyrimRidingDismount, aJumpHeldDownFromTheMountDoesNotGetOffAtOnce)
    {
        RidingRig rig;
        rig.mRiding.onMountRequested(horseHandle());
        rig.mCore.mControls.valid = true;
        rig.mCore.mControls.jump = true;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_TRUE(rig.mRiding.riding());
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_TRUE(rig.mRiding.riding());
        rig.mCore.mControls.jump = false;
        rig.mRiding.update(0.016f, rig.mCore);
        rig.mCore.mControls.jump = true;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_FALSE(rig.mRiding.riding());
    }

    TEST(SkyrimRidingDismount, activatingTheHorseAgainGetsOff)
    {
        RidingRig rig;
        rig.mount();
        EXPECT_TRUE(rig.mRiding.canMount(horseHandle())); // the engine offers the action
        rig.mRiding.onMountRequested(horseHandle());
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_FALSE(rig.mRiding.riding());
        EXPECT_EQ(rig.mCore.mReleases, 1);
    }

    TEST(SkyrimRidingDismount, aHorseThatGoesAwayPutsThePlayerDownWhereItWasLastSeen)
    {
        RidingRig rig;
        rig.mount();
        rig.mRiding.onActorRemoved(horseHandle());
        rig.mCore.mHorseThere = false;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_FALSE(rig.mRiding.riding());
        ASSERT_EQ(rig.mCore.mReleases, 1);
        const Point down = dismountPoint(1000.0, 2000.0, 300.0, rig.mCore.mCarryHeading);
        EXPECT_NEAR(rig.mCore.mReleaseX, down.mX, 1e-3);
    }

    TEST(SkyrimRidingDismount, aHorseAddedAgainAfterADoorKeepsItsRider)
    {
        RidingRig rig;
        rig.mount();
        ActorHandle again = horseHandle();
        again.session = 9;
        rig.mRiding.onActorRemoved(horseHandle());
        ActorInfo info = horseInfo("LvlHorse");
        info.handle = again;
        rig.mRiding.onActorAdded(info);
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_TRUE(rig.mRiding.riding());
        EXPECT_EQ(rig.mRiding.mount(), again);
    }

    TEST(SkyrimRidingDismount, theUpdateOfAnIdlePackDoesNothing)
    {
        RidingRig rig;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mCarries, 0);
        EXPECT_EQ(rig.mCore.mMovements, 0);
        EXPECT_EQ(rig.mCore.mReleases, 0);
    }
}
