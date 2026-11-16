// Task 0255: the core lets the pack get the rider off outside update() (a load door is about to take the player).
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralRidingHooks;

    // Rules that get off when told, through the writer they are handed, and keep that writer to try it again later.
    class DismountProbe final : public RidingRules
    {
    public:
        bool canMount(ActorHandle) const override { return false; }
        void onMountRequested(ActorHandle) override {}
        void dismountNow(RidingWriter& player) override
        {
            ++mCalls;
            mWriter = &player;
            mReleased = player.releasePlayer(10.0, 20.0, 30.0);
            mCarried = player.carryPlayer(1.0, 2.0, 3.0, 0.f);
            RiderControls controls;
            mControls = player.riderControls(controls);
        }
        int mCalls = 0;
        bool mReleased = false, mCarried = false, mControls = false;
        RidingWriter* mWriter = nullptr;
    };

    struct DismountRig
    {
        NeutralActorBridge mBridge;
        DismountProbe mRules;
        NeutralRidingHooks mHooks;
        int mReleases = 0;
        double mReleasedAt[3] = { 0, 0, 0 };

        DismountRig()
        {
            mHooks.mControls = [](RiderControls& out) {
                out.valid = true;
                return true;
            };
            mHooks.mCarry = [](double, double, double, float) { return true; };
            mHooks.mRelease = [this](double x, double y, double z) {
                ++mReleases;
                mReleasedAt[0] = x;
                mReleasedAt[1] = y;
                mReleasedAt[2] = z;
                return true;
            };
        }
    };

    TEST(NeutralActorRidingDismount, theRulesAreToldOnceAndTheirReleaseReachesTheHook)
    {
        DismountRig rig;
        rig.mBridge.setRidingHooks(&rig.mHooks);
        rig.mBridge.dismountRider(rig.mRules);
        EXPECT_EQ(rig.mRules.mCalls, 1);
        EXPECT_TRUE(rig.mRules.mReleased);
        EXPECT_EQ(rig.mReleases, 1);
        EXPECT_EQ(rig.mReleasedAt[0], 10.0);
        EXPECT_EQ(rig.mReleasedAt[2], 30.0);
        EXPECT_TRUE(rig.mRules.mControls);
    }

    TEST(NeutralActorRidingDismount, afterTheCallTheWriterIsClosedAgain)
    {
        DismountRig rig;
        rig.mBridge.setRidingHooks(&rig.mHooks);
        rig.mBridge.dismountRider(rig.mRules);
        ASSERT_NE(rig.mRules.mWriter, nullptr);
        const int releases = rig.mReleases;
        EXPECT_FALSE(rig.mRules.mWriter->releasePlayer(1.0, 2.0, 3.0));
        EXPECT_FALSE(rig.mRules.mWriter->carryPlayer(1.0, 2.0, 3.0, 0.f));
        EXPECT_EQ(rig.mReleases, releases);
    }

    TEST(NeutralActorRidingDismount, withoutHooksTheRulesAreStillToldAndEveryCallIsRefused)
    {
        DismountRig rig;
        rig.mBridge.dismountRider(rig.mRules);
        EXPECT_EQ(rig.mRules.mCalls, 1);
        EXPECT_FALSE(rig.mRules.mReleased);
        EXPECT_FALSE(rig.mRules.mCarried);
        EXPECT_FALSE(rig.mRules.mControls);
        EXPECT_EQ(rig.mReleases, 0);
    }

    TEST(NeutralActorRidingDismount, aNonFiniteReleaseIsRefusedAsInsideUpdate)
    {
        struct Bad final : RidingRules
        {
            bool canMount(ActorHandle) const override { return false; }
            void onMountRequested(ActorHandle) override {}
            void dismountNow(RidingWriter& player) override
            {
                mResult = player.releasePlayer(1.0, 2.0, std::numeric_limits<double>::infinity());
            }
            bool mResult = true;
        } bad;
        DismountRig rig;
        rig.mBridge.setRidingHooks(&rig.mHooks);
        rig.mBridge.dismountRider(bad);
        EXPECT_FALSE(bad.mResult);
        EXPECT_EQ(rig.mReleases, 0);
    }
}
