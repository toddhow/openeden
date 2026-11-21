// Task 0267 (paired animations, first slice): the horse's mount and dismount through the game's own paired clips, over
// a fake core. No plugin bytes, no game data. The clip numbers in the fake core are round ones; the real ones (measured
// on the installed clips) are in docs/tasks/0267-skyrim-paired-animations.md and the havokpack tests.
#include <game_skyrimse/riding.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr float kHorseWalk = 125.1127f;
    constexpr float kHorseRun = 454.9714f;
    constexpr WorldId kHorseRef = 77;
    constexpr WorldId kHorseBase = 0x1234;
    constexpr double kPi = std::numbers::pi;

    // A core that holds one horse and the player, can be asked about paired clips and plays them.
    class PairedFakeCore final : public ActorRequestWriter, public RidingWriter, public PairedWriter
    {
    public:
        bool setMovement(ActorHandle, float velX, float velY) override
        {
            ++mMovements;
            mVelX = velX;
            mVelY = velY;
            if (velX != 0.f || velY != 0.f)
                ++mMovingRequests;
            return true;
        }
        bool setHeading(ActorHandle, float heading) override
        {
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
        bool playerPosition(double& x, double& y, double& z) const override
        {
            x = mPlayerX;
            y = mPlayerY;
            z = mPlayerZ;
            return true;
        }
        bool riderControls(RiderControls& out) const override
        {
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
        PairedStatus pairedClipInfo(std::string_view clipName, PairedClipInfo& out) override
        {
            ++mInfoAsks;
            if (clipName == kPairedMountClip)
            {
                out = mMountInfo;
                return mMountStatus;
            }
            if (clipName == kPairedDismountClip)
            {
                out = mDismountInfo;
                return mDismountStatus;
            }
            return PairedStatus::Failed;
        }
        bool playPairedClip(ActorHandle actor, std::string_view clipName) override
        {
            mPlays.push_back({ actor.reference, std::string(clipName) });
            return true;
        }

        RiderControls mControls;
        bool mHorseThere = true;
        double mHorseX = 1000.0, mHorseY = 2000.0, mHorseZ = 300.0;
        double mPlayerX = 900.0, mPlayerY = 2000.0, mPlayerZ = 300.0; // 100 units west of the horse
        float mVelX = 0.f, mVelY = 0.f, mHeadingSet = 0.f;
        int mMovements = 0, mMovingRequests = 0, mHeadings = 0, mCarries = 0, mReleases = 0, mInfoAsks = 0;
        double mCarryX = 0, mCarryY = 0, mCarryZ = 0, mReleaseX = 0, mReleaseY = 0, mReleaseZ = 0;
        float mCarryHeading = 0.f;
        PairedStatus mMountStatus = PairedStatus::Ready;
        PairedStatus mDismountStatus = PairedStatus::Ready;
        PairedClipInfo mMountInfo{ 1.5f, { -63.5, 0.0, 0.0, -kPi / 2 }, { 1.7, 4.1, 130.4, 0.0 }, {}, {} };
        PairedClipInfo mDismountInfo{ 2.0f, { 1.7, 4.4, 130.4, 0.0 }, { -111.6, 10.1, 0.2, 0.0 }, {}, {} };
        std::vector<std::pair<WorldId, std::string>> mPlays;
    };

    // The same without the paired interface.
    class PlainFakeCore final : public ActorRequestWriter, public RidingWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool setHeading(ActorHandle, float) override { return true; }
        bool actorPosition(ActorHandle, double& x, double& y, double& z) const override
        {
            x = 1000.0;
            y = 2000.0;
            z = 300.0;
            return true;
        }
        bool riderControls(RiderControls& out) const override
        {
            out.valid = true;
            return true;
        }
        bool carryPlayer(double, double, double, float) override { return true; }
        bool releasePlayer(double, double, double) override
        {
            ++mReleases;
            return true;
        }
        int mReleases = 0;
    };

    ActorHandle pairedHorseHandle()
    {
        ActorHandle handle;
        handle.reference = kHorseRef;
        handle.session = 5;
        return handle;
    }

    ActorInfo pairedHorseInfo()
    {
        ActorInfo info;
        info.handle = pairedHorseHandle();
        info.base = kHorseBase;
        info.editorId = "LvlHorse";
        return info;
    }

    struct PairedRidingRig
    {
        Riding mRiding;
        PairedFakeCore mCore;
        std::vector<std::string> mLog;
        std::vector<std::pair<WorldId, bool>> mBrain;

        explicit PairedRidingRig(bool paired = true)
        {
            mRiding.setEnabled(true);
            mRiding.setPairedAnimations(paired);
            mRiding.setTestOwned("LvlHorse");
            mRiding.setHorseLookup([](WorldId base, std::uint32_t, HorseInfo& out) {
                if (base != kHorseBase)
                    return false;
                out.mWalkSpeed = kHorseWalk;
                out.mRunSpeed = kHorseRun;
                return true;
            });
            mRiding.setBrainControl([this](WorldId reference, bool stands) {
                mBrain.push_back({ reference, stands });
            });
            mRiding.setLog([this](const std::string& line) { mLog.push_back(line); });
            mRiding.onActorAdded(pairedHorseInfo());
            mCore.mControls.valid = true;
            mCore.mControls.heading = 0.5f;
        }

        void request() { mRiding.onMountRequested(pairedHorseHandle()); }
        void step(float dt = 0.5f) { mRiding.update(dt, mCore); }
        // Mounts and plays the whole mount clip (1.5 s in 0.5 s steps) and the first frame on the saddle.
        void mountAndSit()
        {
            request();
            for (int i = 0; i < 4; ++i)
                step();
            step();
        }
    };
}

// ---- the pure rules -----------------------------------------------------------------------------------------------

TEST(SkyrimRidingPairedMaths, a_point_in_the_horse_frame_is_turned_by_its_heading)
{
    // heading 0 faces +Y: x is east, y is north
    Point p = horseFramePoint(1000.0, 2000.0, 300.0, 0.f, 10.0, 5.0, 130.0);
    EXPECT_NEAR(p.mX, 1010.0, 1e-9);
    EXPECT_NEAR(p.mY, 2005.0, 1e-9);
    EXPECT_NEAR(p.mZ, 430.0, 1e-9);
    // a quarter turn clockwise faces +X: its right is south, its forward east
    p = horseFramePoint(1000.0, 2000.0, 300.0, static_cast<float>(kPi / 2), 10.0, 5.0, 0.0);
    EXPECT_NEAR(p.mX, 1005.0, 1e-4);
    EXPECT_NEAR(p.mY, 1990.0, 1e-4);
    EXPECT_NEAR(p.mZ, 300.0, 1e-9);
}

TEST(SkyrimRidingPairedMaths, the_heading_that_puts_the_rider_on_the_left_flank)
{
    // west of the horse: heading 0 (its left is west)
    EXPECT_NEAR(headingForLeftFlank(1000.0, 2000.0, 900.0, 2000.0, 9.f), 0.f, 1e-5f);
    // south of it: heading -pi/2 (it faces west, its left is south)
    EXPECT_NEAR(headingForLeftFlank(1000.0, 2000.0, 1000.0, 1900.0, 9.f), -static_cast<float>(kPi / 2), 1e-5f);
    // east of it: it faces south
    EXPECT_NEAR(std::fabs(headingForLeftFlank(1000.0, 2000.0, 1100.0, 2000.0, 9.f)), static_cast<float>(kPi), 1e-5f);
    // north of it: heading pi/2 (it faces east, its left is north)
    EXPECT_NEAR(headingForLeftFlank(1000.0, 2000.0, 1000.0, 2100.0, 9.f), static_cast<float>(kPi / 2), 1e-5f);
}

TEST(SkyrimRidingPairedMaths, the_fallback_is_kept_when_the_two_stand_together_or_a_value_is_bad)
{
    EXPECT_EQ(headingForLeftFlank(1000.0, 2000.0, 1000.0, 2000.0, 9.f), 9.f);
    EXPECT_EQ(headingForLeftFlank(1000.0, 2000.0, 1000.0000001, 2000.0, 9.f), 9.f);
    EXPECT_EQ(headingForLeftFlank(std::nan(""), 2000.0, 900.0, 2000.0, 9.f), 9.f);
    EXPECT_EQ(headingForLeftFlank(1000.0, 2000.0, 900.0, std::nan(""), 9.f), 9.f);
}

// ---- off, and a core that cannot --------------------------------------------------------------------------------

TEST(SkyrimRidingPairedOff, the_setting_off_mounts_at_once_and_never_asks_about_a_clip)
{
    PairedRidingRig rig(false);
    rig.request();
    rig.step();
    EXPECT_TRUE(rig.mRiding.riding());
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    EXPECT_EQ(rig.mRiding.clip(), "horse_rider/idle");
    EXPECT_EQ(rig.mCore.mInfoAsks, 0);
    EXPECT_TRUE(rig.mCore.mPlays.empty());
}

TEST(SkyrimRidingPairedOff, a_core_without_the_paired_interface_mounts_at_once)
{
    Riding riding;
    riding.setEnabled(true);
    riding.setPairedAnimations(true);
    riding.setTestOwned("LvlHorse");
    riding.setHorseLookup([](WorldId base, std::uint32_t, HorseInfo& out) {
        out.mWalkSpeed = kHorseWalk;
        out.mRunSpeed = kHorseRun;
        return base == kHorseBase;
    });
    riding.onActorAdded(pairedHorseInfo());
    PlainFakeCore core;
    riding.onMountRequested(pairedHorseHandle());
    riding.update(0.5f, core);
    EXPECT_TRUE(riding.riding());
    EXPECT_EQ(riding.phase(), RidePhase::Ride);
    EXPECT_EQ(riding.clip(), "horse_rider/idle");
}

// ---- mounting ---------------------------------------------------------------------------------------------------

TEST(SkyrimRidingPairedMount, the_mount_clip_plays_on_both_actors_with_the_rider_at_the_horses_origin)
{
    PairedRidingRig rig;
    rig.request();
    rig.step();
    EXPECT_TRUE(rig.mRiding.riding());
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Mounting);
    EXPECT_EQ(rig.mRiding.clip(), kPairedMountClip);
    EXPECT_EQ(rig.mRiding.bodyMountReference(), 0u);
    ASSERT_EQ(rig.mCore.mPlays.size(), 1u);
    EXPECT_EQ(rig.mCore.mPlays[0].first, kHorseRef);
    EXPECT_EQ(rig.mCore.mPlays[0].second, std::string(kPairedMountHorseClip));
    ASSERT_FALSE(rig.mBrain.empty());
    EXPECT_EQ(rig.mBrain.back(), std::make_pair(kHorseRef, true));
    // the rider stood west of the horse, so the horse is turned to have him on its left (heading 0). The player stays
    // on the saddle (his camera must not be inside the horse) and his body is drawn at the horse's origin: the offset
    // is the difference
    EXPECT_GE(rig.mCore.mCarries, 1);
    EXPECT_NEAR(rig.mCore.mCarryX, 1000.0 + static_cast<double>(kSaddleX), 1e-4);
    EXPECT_NEAR(rig.mCore.mCarryY, 2000.0 + static_cast<double>(kSaddleY), 1e-4);
    EXPECT_NEAR(rig.mCore.mCarryZ, 300.0 + static_cast<double>(kSaddleZ), 1e-4);
    EXPECT_NEAR(rig.mCore.mCarryHeading, 0.f, 1e-5f);
    const Point offset = rig.mRiding.bodyOffset();
    EXPECT_NEAR(offset.mX, -static_cast<double>(kSaddleX), 1e-4);
    EXPECT_NEAR(offset.mY, -static_cast<double>(kSaddleY), 1e-4);
    EXPECT_NEAR(offset.mZ, -static_cast<double>(kSaddleZ), 1e-4);
    EXPECT_NEAR(rig.mCore.mHeadingSet, 0.f, 1e-5f);
    EXPECT_EQ(rig.mCore.mMovingRequests, 0);
}

TEST(SkyrimRidingPairedMount, the_player_is_on_the_saddle_when_the_clip_is_over)
{
    PairedRidingRig rig;
    rig.request();
    rig.step(); // starts: 0 s
    rig.step(); // 0.5
    rig.step(); // 1.0
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Mounting);
    rig.step(); // 1.5: the clip's length
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    EXPECT_EQ(rig.mRiding.clip(), "horse_rider/idle");
    EXPECT_EQ(rig.mRiding.bodyMountReference(), static_cast<std::uint32_t>(kHorseRef));
    EXPECT_EQ(rig.mRiding.bodyOffset().mZ, 0.0); // the body follows the saddle again
    ASSERT_EQ(rig.mCore.mPlays.size(), 2u);
    EXPECT_EQ(rig.mCore.mPlays[1].first, kHorseRef);
    EXPECT_EQ(rig.mCore.mPlays[1].second, ""); // the horse goes back to its own animation
    rig.step();
    EXPECT_NEAR(rig.mCore.mCarryZ, 300.0 + static_cast<double>(kSaddleZ), 1e-4);
    EXPECT_EQ(rig.mRiding.mounts(), 1u);
}

TEST(SkyrimRidingPairedMount, the_horse_stands_still_and_the_jump_control_does_nothing_while_the_clip_plays)
{
    PairedRidingRig rig;
    rig.request();
    rig.step();
    rig.mCore.mControls.jump = true;
    rig.mCore.mControls.forward = 1.f;
    rig.step();
    rig.step();
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Mounting);
    EXPECT_EQ(rig.mRiding.dismounts(), 0u);
    EXPECT_EQ(rig.mCore.mMovingRequests, 0);
    EXPECT_EQ(rig.mCore.mReleases, 0);
}

TEST(SkyrimRidingPairedMount, a_clip_still_being_read_is_waited_for_and_then_given_up)
{
    PairedRidingRig rig;
    rig.mCore.mMountStatus = PairedStatus::NotReady;
    rig.request();
    rig.step(1.f);
    rig.step(1.f);
    EXPECT_FALSE(rig.mRiding.riding()); // still waiting: the request is kept
    EXPECT_TRUE(rig.mCore.mPlays.empty());
    rig.step(1.f); // three seconds: it mounts without the animation
    EXPECT_TRUE(rig.mRiding.riding());
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    EXPECT_EQ(rig.mRiding.clip(), "horse_rider/idle");
    EXPECT_TRUE(rig.mCore.mPlays.empty());
}

TEST(SkyrimRidingPairedMount, a_clip_that_becomes_ready_while_waiting_starts_the_paired_mount)
{
    PairedRidingRig rig;
    rig.mCore.mMountStatus = PairedStatus::NotReady;
    rig.request();
    rig.step(0.5f);
    EXPECT_FALSE(rig.mRiding.riding());
    rig.mCore.mMountStatus = PairedStatus::Ready;
    rig.step(0.5f);
    EXPECT_TRUE(rig.mRiding.riding());
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Mounting);
}

TEST(SkyrimRidingPairedMount, a_clip_that_cannot_be_read_mounts_at_once_without_it)
{
    PairedRidingRig rig;
    rig.mCore.mMountStatus = PairedStatus::Failed;
    rig.request();
    rig.step();
    EXPECT_TRUE(rig.mRiding.riding());
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    EXPECT_EQ(rig.mRiding.clip(), "horse_rider/idle");
    EXPECT_TRUE(rig.mCore.mPlays.empty());
}

// ---- dismounting ------------------------------------------------------------------------------------------------

TEST(SkyrimRidingPairedDismount, the_jump_control_plays_the_dismount_clip_and_puts_the_player_where_it_ends)
{
    PairedRidingRig rig;
    rig.mountAndSit();
    ASSERT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    const std::size_t played = rig.mCore.mPlays.size();
    rig.mCore.mControls.jump = true;
    rig.step();
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Dismounting);
    EXPECT_EQ(rig.mRiding.clip(), kPairedDismountClip);
    EXPECT_EQ(rig.mRiding.bodyMountReference(), 0u);
    ASSERT_EQ(rig.mCore.mPlays.size(), played + 1);
    EXPECT_EQ(rig.mCore.mPlays.back().second, std::string(kPairedDismountHorseClip));
    EXPECT_EQ(rig.mCore.mReleases, 0);
    EXPECT_NEAR(rig.mCore.mCarryZ, 300.0 + static_cast<double>(kSaddleZ), 1e-4); // on the saddle while the clip plays
    EXPECT_NEAR(rig.mRiding.bodyOffset().mZ, -static_cast<double>(kSaddleZ), 1e-4);
    rig.step(); // 0.5
    rig.step(); // 1.0
    rig.step(); // 1.5
    EXPECT_EQ(rig.mCore.mReleases, 0);
    rig.step(); // 2.0: the clip's length
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    EXPECT_FALSE(rig.mRiding.riding());
    EXPECT_EQ(rig.mCore.mReleases, 1);
    EXPECT_EQ(rig.mRiding.bodyOffset().mZ, 0.0);
    // where the clip left the rider, in the horse's frame, turned by the horse's heading (the view's: 0.5)
    const Point end = horseFramePoint(1000.0, 2000.0, 300.0, 0.5f, -111.6, 10.1, 0.2);
    EXPECT_NEAR(rig.mCore.mReleaseX, end.mX, 1e-3);
    EXPECT_NEAR(rig.mCore.mReleaseY, end.mY, 1e-3);
    EXPECT_NEAR(rig.mCore.mReleaseZ, end.mZ, 1e-3);
    EXPECT_EQ(rig.mCore.mPlays.back().second, ""); // the horse goes back to its own animation
    EXPECT_EQ(rig.mBrain.back(), std::make_pair(kHorseRef, false));
    EXPECT_EQ(rig.mRiding.dismounts(), 1u);
}

TEST(SkyrimRidingPairedDismount, activating_the_horse_again_dismounts_the_same_way)
{
    PairedRidingRig rig;
    rig.mountAndSit();
    rig.mRiding.onMountRequested(pairedHorseHandle());
    rig.step();
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Dismounting);
}

TEST(SkyrimRidingPairedDismount, the_horse_does_not_walk_while_the_clip_plays)
{
    PairedRidingRig rig;
    rig.mountAndSit();
    rig.mCore.mControls.forward = 1.f; // he was riding at a walk when he jumped
    rig.mCore.mControls.jump = true;
    const int before = rig.mCore.mMovingRequests;
    rig.step();
    rig.step();
    EXPECT_EQ(rig.mCore.mMovingRequests, before);
    EXPECT_EQ(rig.mCore.mVelX, 0.f);
    EXPECT_EQ(rig.mCore.mVelY, 0.f);
}

TEST(SkyrimRidingPairedDismount, a_dismount_clip_that_cannot_be_read_gets_off_at_once)
{
    PairedRidingRig rig;
    rig.mCore.mDismountStatus = PairedStatus::Failed;
    rig.mountAndSit();
    rig.mCore.mControls.jump = true;
    rig.step();
    EXPECT_FALSE(rig.mRiding.riding());
    EXPECT_EQ(rig.mCore.mReleases, 1);
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Ride);
}

TEST(SkyrimRidingPairedDismount, a_dismount_clip_that_is_not_ready_gets_off_at_once_too)
{
    PairedRidingRig rig;
    rig.mCore.mDismountStatus = PairedStatus::NotReady;
    rig.mountAndSit();
    rig.mCore.mControls.jump = true;
    rig.step();
    EXPECT_FALSE(rig.mRiding.riding());
    EXPECT_EQ(rig.mCore.mReleases, 1);
}

// ---- interruptions ----------------------------------------------------------------------------------------------

TEST(SkyrimRidingPairedInterrupt, a_load_door_during_the_mount_clip_puts_him_down_and_clears_the_horses_clip)
{
    PairedRidingRig rig;
    rig.request();
    rig.step();
    rig.step();
    ASSERT_EQ(rig.mRiding.phase(), RidePhase::Mounting);
    rig.mRiding.dismountNow(rig.mCore);
    EXPECT_FALSE(rig.mRiding.riding());
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    EXPECT_EQ(rig.mCore.mReleases, 1);
    ASSERT_FALSE(rig.mCore.mPlays.empty());
    EXPECT_EQ(rig.mCore.mPlays.back().first, kHorseRef);
    EXPECT_EQ(rig.mCore.mPlays.back().second, "");
    EXPECT_EQ(rig.mBrain.back(), std::make_pair(kHorseRef, false));
}

TEST(SkyrimRidingPairedInterrupt, losing_the_horse_during_the_clip_ends_it_on_the_ground)
{
    PairedRidingRig rig;
    rig.request();
    rig.step();
    rig.mCore.mHorseThere = false;
    rig.mRiding.onActorRemoved(pairedHorseHandle());
    rig.step();
    EXPECT_FALSE(rig.mRiding.riding());
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    EXPECT_EQ(rig.mCore.mReleases, 1);
}

TEST(SkyrimRidingPairedInterrupt, a_load_during_the_dismount_clip_ends_the_ride_and_clears_the_horses_clip_next_frame)
{
    PairedRidingRig rig;
    rig.mountAndSit();
    rig.mCore.mControls.jump = true;
    rig.step();
    ASSERT_EQ(rig.mRiding.phase(), RidePhase::Dismounting);
    rig.mRiding.restoreState(RidingState{});
    EXPECT_FALSE(rig.mRiding.riding());
    EXPECT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    rig.mCore.mControls.jump = false;
    rig.step();
    ASSERT_FALSE(rig.mCore.mPlays.empty());
    EXPECT_EQ(rig.mCore.mPlays.back().first, kHorseRef);
    EXPECT_EQ(rig.mCore.mPlays.back().second, "");
}
