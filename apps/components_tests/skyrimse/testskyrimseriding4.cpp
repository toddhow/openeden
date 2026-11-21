// Task 0278 (buying a horse): the horse a purchase gave is the player's, and the 7 unit step at the hand-over of the
// paired mount and dismount clips is blended away. Over a fake core, no game data.
#include <game_skyrimse/follower_state.hpp>
#include <game_skyrimse/riding.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kRef = 77;
    constexpr WorldId kBase = 0x1234;
    constexpr double kPi = std::numbers::pi;

    class Core final : public ActorRequestWriter, public RidingWriter, public PairedWriter
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
        bool playerPosition(double& x, double& y, double& z) const override
        {
            x = 900.0;
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
        bool releasePlayer(double, double, double) override { return true; }
        PairedStatus pairedClipInfo(std::string_view clipName, PairedClipInfo& out) override
        {
            out = clipName == kPairedMountClip ? mMount : mDismount;
            return PairedStatus::Ready;
        }
        bool playPairedClip(ActorHandle, std::string_view) override { return true; }

        PairedClipInfo mMount{ 1.5f, { -63.5, 0.0, 0.0, -kPi / 2 }, { 1.7, 4.1, 130.4, 0.0 }, {}, {} };
        PairedClipInfo mDismount{ 2.0f, { 1.7, 4.4, 130.4, 0.0 }, { -111.6, 10.1, 0.2, 0.0 }, {}, {} };
    };

    ActorHandle horse()
    {
        ActorHandle h;
        h.reference = kRef;
        h.session = 5;
        return h;
    }

    struct Rig
    {
        Riding mRiding;
        Core mCore;
        FollowerState mState;

        Rig(bool testOwned, bool lookup)
        {
            mRiding.setEnabled(true);
            mRiding.setPairedAnimations(true);
            if (testOwned)
                mRiding.setTestOwned("LvlHorse");
            if (lookup)
                mRiding.setOwnedLookup([this](WorldId reference) {
                    const std::optional<float> rank = mState.factionRank(
                        static_cast<FormId>(reference), static_cast<FormId>(kPlayerHorseFactionId));
                    return rank.has_value() && *rank >= 1.f;
                });
            mRiding.setHorseLookup([](WorldId base, std::uint32_t, HorseInfo& out) {
                out.mWalkSpeed = 125.f;
                out.mRunSpeed = 455.f;
                return base == kBase;
            });
            ActorInfo info;
            info.handle = horse();
            info.base = kBase;
            info.editorId = "LvlHorse";
            mRiding.onActorAdded(info);
        }
    };
}

TEST(SkyrimHorsePurchase, a_horse_nobody_owns_cannot_be_mounted)
{
    Rig rig(false, true);
    EXPECT_FALSE(rig.mRiding.canMount(horse()));
}

TEST(SkyrimHorsePurchase, the_purchase_scripts_faction_rank_makes_it_the_players)
{
    Rig rig(false, true);
    ASSERT_TRUE(rig.mState.addToFaction(static_cast<FormId>(kRef), static_cast<FormId>(kPlayerHorseFactionId), 1));
    EXPECT_TRUE(rig.mRiding.canMount(horse()));
}

TEST(SkyrimHorsePurchase, rank_zero_or_another_faction_or_another_horse_is_not_a_purchase)
{
    Rig rig(false, true);
    ASSERT_TRUE(rig.mState.addToFaction(static_cast<FormId>(kRef), static_cast<FormId>(kPlayerHorseFactionId), 0));
    EXPECT_FALSE(rig.mRiding.canMount(horse()));
    ASSERT_TRUE(rig.mState.addToFaction(static_cast<FormId>(kRef), 0x1234u, 1));
    EXPECT_FALSE(rig.mRiding.canMount(horse()));
    ASSERT_TRUE(rig.mState.addToFaction(static_cast<FormId>(kRef + 1), static_cast<FormId>(kPlayerHorseFactionId), 1));
    EXPECT_FALSE(rig.mRiding.canMount(horse()));
}

TEST(SkyrimHorsePurchase, the_test_setting_stays_an_override_and_a_missing_lookup_changes_nothing)
{
    Rig test(true, false);
    EXPECT_TRUE(test.mRiding.canMount(horse()));
    Rig none(false, false);
    EXPECT_FALSE(none.mRiding.canMount(horse()));
}

TEST(SkyrimHorsePurchase, the_horse_faction_is_the_one_the_scripts_property_names)
{
    // TIF__00068CF1's PlayerHorseFaction property: form 0x00068D78 (read from the data's VMAD).
    EXPECT_EQ(kPlayerHorseFactionId, 0x00068D78u);
}

TEST(SkyrimHandoverBlend, the_weight_falls_linearly_from_one_to_zero)
{
    EXPECT_FLOAT_EQ(handoverWeight(0.f), 1.f);
    EXPECT_FLOAT_EQ(handoverWeight(-1.f), 1.f);
    EXPECT_NEAR(handoverWeight(kHandoverBlendSeconds / 2), 0.5f, 1e-6f);
    EXPECT_FLOAT_EQ(handoverWeight(kHandoverBlendSeconds), 0.f);
    EXPECT_FLOAT_EQ(handoverWeight(99.f), 0.f);
}

TEST(SkyrimHandoverBlend, after_a_mount_the_body_starts_at_the_clips_last_pose_and_comes_to_the_saddle)
{
    Rig rig(true, false);
    rig.mRiding.onMountRequested(horse());
    for (int i = 0; i < 4; ++i)
        rig.mRiding.update(0.5f, rig.mCore); // 0, 0.5, 1.0, 1.5: the clip's length
    ASSERT_EQ(rig.mRiding.phase(), RidePhase::Ride);
    const Point saddle = saddlePoint(1000.0, 2000.0, 300.0, 0.f);
    const Point endPose = horseFramePoint(1000.0, 2000.0, 300.0, 0.f, 1.7, 4.1, 130.4);
    Point blend = rig.mRiding.blendOffset();
    EXPECT_NEAR(blend.mX, endPose.mX - saddle.mX, 1e-4);
    EXPECT_NEAR(blend.mY, endPose.mY - saddle.mY, 1e-4);
    EXPECT_NEAR(blend.mZ, endPose.mZ - saddle.mZ, 1e-4);
    EXPECT_NE(blend.mZ, 0.0);
    // the rule the earlier slice pinned stays: bodyOffset itself is zero on the saddle
    EXPECT_EQ(rig.mRiding.bodyOffset().mZ, 0.0);
    rig.mRiding.update(kHandoverBlendSeconds / 2, rig.mCore);
    const Point half = rig.mRiding.blendOffset();
    EXPECT_NEAR(half.mZ, blend.mZ / 2, 1e-3);
    rig.mRiding.update(kHandoverBlendSeconds, rig.mCore);
    blend = rig.mRiding.blendOffset();
    EXPECT_EQ(blend.mX, 0.0);
    EXPECT_EQ(blend.mY, 0.0);
    EXPECT_EQ(blend.mZ, 0.0);
}

TEST(SkyrimHandoverBlend, a_plain_mount_has_no_blend)
{
    Rig rig(true, false);
    rig.mRiding.setPairedAnimations(false);
    rig.mRiding.onMountRequested(horse());
    rig.mRiding.update(0.5f, rig.mCore);
    ASSERT_TRUE(rig.mRiding.riding());
    const Point blend = rig.mRiding.blendOffset();
    EXPECT_EQ(blend.mX, 0.0);
    EXPECT_EQ(blend.mZ, 0.0);
}

TEST(SkyrimHandoverBlend, a_dismount_starts_from_the_saddle_and_blends_to_the_clips_first_pose)
{
    Rig rig(true, false);
    rig.mRiding.setPairedAnimations(false);
    rig.mRiding.onMountRequested(horse());
    rig.mRiding.update(0.5f, rig.mCore);
    ASSERT_TRUE(rig.mRiding.riding());
    rig.mRiding.setPairedAnimations(true);
    rig.mRiding.onMountRequested(horse()); // activating the horse again gets off
    rig.mRiding.update(0.0f, rig.mCore);
    ASSERT_EQ(rig.mRiding.phase(), RidePhase::Dismounting);
    const Point saddle = saddlePoint(1000.0, 2000.0, 300.0, 0.f);
    const Point start = horseFramePoint(1000.0, 2000.0, 300.0, 0.f, 1.7, 4.4, 130.4);
    const Point blend = rig.mRiding.blendOffset();
    EXPECT_NEAR(blend.mZ, saddle.mZ - start.mZ, 1e-3);
}
