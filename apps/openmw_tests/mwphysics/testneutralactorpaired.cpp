// Task 0267 (paired animations): the writer the bridge hands the pack also answers the PairedWriter interface
// (sdk/include/openeden/game/paired_animation.hpp) through the hooks the world gives it.
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralRidingHooks;

    class PairedNullSink final : public MWPhysics::NeutralRequestSink
    {
    public:
        void setRequest(const void*, float, float) override {}
    };

    int sPairedKey = 0;

    // A pack that asks the core for the paired calls from inside update(), and (as the riding rules do) from
    // dismountNow().
    class PairedProbe final : public ActorRules, public RidingRules
    {
    public:
        void onActorAdded(const ActorInfo& info) override { mHandle = info.handle; }
        void onActorRemoved(ActorHandle) override {}
        void update(float, ActorRequestWriter& writer) override
        {
            ++mUpdates;
            auto* paired = dynamic_cast<PairedWriter*>(&writer);
            mHasPaired = paired != nullptr;
            if (paired == nullptr)
                return;
            mStatus = paired->pairedClipInfo(mClipName, mInfo);
            mPlayed = paired->playPairedClip(mHandle, mClipName);
            mPlayedStale = paired->playPairedClip(ActorHandle{}, mClipName);
        }
        bool canMount(ActorHandle) const override { return false; }
        void onMountRequested(ActorHandle) override {}
        void dismountNow(RidingWriter& player) override
        {
            ++mDismounts;
            mWriter = &player;
            if (auto* paired = dynamic_cast<PairedWriter*>(&player))
                mClearedByDismount = paired->playPairedClip(mHandle, std::string_view());
        }

        ActorHandle mHandle;
        std::string mClipName = "~2/actors/x/y";
        int mUpdates = 0;
        int mDismounts = 0;
        bool mHasPaired = false;
        bool mPlayed = false;
        bool mPlayedStale = true;
        bool mClearedByDismount = false;
        PairedStatus mStatus = PairedStatus::Ready;
        PairedClipInfo mInfo;
        RidingWriter* mWriter = nullptr;
    };

    struct PairedBridgeRig
    {
        NeutralActorBridge mBridge;
        PairedProbe mPack;
        PairedNullSink mSink;
        NeutralRidingHooks mHooks;
        std::vector<std::pair<const void*, std::string>> mPlays;
        std::string mInfoAsked;

        PairedBridgeRig()
        {
            mHooks.mPairedInfo = [this](std::string_view clip, PairedClipInfo& out) {
                mInfoAsked = std::string(clip);
                out.duration = 1.5f;
                out.end = { 1.0, 2.0, 3.0, 0.25 };
                return PairedStatus::Ready;
            };
            mHooks.mPairedPlay = [this](const void* key, std::string_view clip) {
                mPlays.push_back({ key, std::string(clip) });
                return true;
            };
            mBridge.add(&mPack, &sPairedKey, 7, 1, "Horse", 0, 0, 0, 0.f);
        }
    };

    TEST(NeutralActorPairedWriter, theWriterIsAPairedWriterAndAnswersThroughTheHooks)
    {
        PairedBridgeRig rig;
        rig.mBridge.setRidingHooks(&rig.mHooks);
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        EXPECT_TRUE(rig.mPack.mHasPaired);
        EXPECT_EQ(rig.mPack.mStatus, PairedStatus::Ready);
        EXPECT_EQ(rig.mInfoAsked, "~2/actors/x/y");
        EXPECT_EQ(rig.mPack.mInfo.duration, 1.5f);
        EXPECT_EQ(rig.mPack.mInfo.end.y, 2.0);
        EXPECT_EQ(rig.mPack.mInfo.end.yaw, 0.25);
        EXPECT_TRUE(rig.mPack.mPlayed);
        ASSERT_EQ(rig.mPlays.size(), 1u);
        EXPECT_EQ(rig.mPlays[0].first, &sPairedKey); // the key behind the handle
        EXPECT_EQ(rig.mPlays[0].second, "~2/actors/x/y");
    }

    TEST(NeutralActorPairedWriter, aHandleThatIsNotRegisteredIsRefusedAndNeverReachesTheHook)
    {
        PairedBridgeRig rig;
        rig.mBridge.setRidingHooks(&rig.mHooks);
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        EXPECT_FALSE(rig.mPack.mPlayedStale);
        EXPECT_EQ(rig.mPlays.size(), 1u); // only the registered actor's call got through
        EXPECT_GE(rig.mBridge.stats().mStale, 1u);
    }

    TEST(NeutralActorPairedWriter, withoutHooksEveryPairedCallIsRefused)
    {
        PairedBridgeRig rig;
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        EXPECT_TRUE(rig.mPack.mHasPaired);
        EXPECT_EQ(rig.mPack.mStatus, PairedStatus::Failed);
        EXPECT_FALSE(rig.mPack.mPlayed);
        EXPECT_TRUE(rig.mPlays.empty());
    }

    TEST(NeutralActorPairedWriter, aHookThatIsEmptyIsRefused)
    {
        PairedBridgeRig rig;
        NeutralRidingHooks none;
        rig.mBridge.setRidingHooks(&none);
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        EXPECT_EQ(rig.mPack.mStatus, PairedStatus::Failed);
        EXPECT_FALSE(rig.mPack.mPlayed);
    }

    TEST(NeutralActorPairedWriter, aLoadDoorsDismountMayClearTheHorsesClipAndTheWriterClosesAfterwards)
    {
        PairedBridgeRig rig;
        rig.mBridge.setRidingHooks(&rig.mHooks);
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink); // registers the handle in the probe
        rig.mPlays.clear();
        rig.mBridge.dismountRider(rig.mPack);
        EXPECT_EQ(rig.mPack.mDismounts, 1);
        EXPECT_TRUE(rig.mPack.mClearedByDismount);
        ASSERT_EQ(rig.mPlays.size(), 1u);
        EXPECT_EQ(rig.mPlays[0].second, "");
        ASSERT_NE(rig.mPack.mWriter, nullptr);
        auto* paired = dynamic_cast<PairedWriter*>(rig.mPack.mWriter);
        ASSERT_NE(paired, nullptr);
        EXPECT_FALSE(paired->playPairedClip(rig.mPack.mHandle, "~2/a/b"));
        PairedClipInfo info;
        EXPECT_EQ(paired->pairedClipInfo("~1/a/b", info), PairedStatus::Failed);
        EXPECT_EQ(rig.mPlays.size(), 1u);
    }

    TEST(NeutralActorPairedWriter, theDefaultsOfTheInterfaceRefuse)
    {
        struct Nothing final : PairedWriter
        {
        } nothing;
        PairedClipInfo info{ 9.f, {}, {} };
        EXPECT_EQ(nothing.pairedClipInfo("~1/a", info), PairedStatus::Failed);
        EXPECT_EQ(info.duration, 0.f);
        EXPECT_FALSE(nothing.playPairedClip(ActorHandle{}, "~1/a"));
    }
}
