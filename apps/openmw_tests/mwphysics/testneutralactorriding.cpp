// Task 0251 (riding): the writer the bridge hands the pack also answers the RidingWriter interface
// (sdk/include/openeden/game/riding_rules.hpp) through the hooks the world gives it.
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralRidingHooks;

    class RidingNullSink final : public MWPhysics::NeutralRequestSink
    {
    public:
        void setRequest(const void*, float, float) override {}
    };

    // A pack that asks the core for the riding calls from inside update().
    class RidingProbe final : public ActorRules
    {
    public:
        void onActorAdded(const ActorInfo&) override {}
        void onActorRemoved(ActorHandle) override {}
        void update(float, ActorRequestWriter& writer) override
        {
            ++mUpdates;
            mHasRiding = dynamic_cast<RidingWriter*>(&writer) != nullptr;
            if (auto* riding = dynamic_cast<RidingWriter*>(&writer))
            {
                mControls = riding->riderControls(mSeenControls);
                mCarried = riding->carryPlayer(mCarryValues[0], mCarryValues[1], mCarryValues[2], mCarryHeading);
                mReleased = riding->releasePlayer(mCarryValues[0], mCarryValues[1], mCarryValues[2]);
                mWriter = &writer;
            }
        }
        int mUpdates = 0;
        bool mHasRiding = false;
        bool mControls = false, mCarried = false, mReleased = false;
        RiderControls mSeenControls;
        double mCarryValues[3] = { 1.0, 2.0, 3.0 };
        float mCarryHeading = 0.5f;
        ActorRequestWriter* mWriter = nullptr;
    };

    int sRidingKey = 0;

    struct RidingBridgeRig
    {
        NeutralActorBridge mBridge;
        RidingProbe mPack;
        RidingNullSink mSink;
        NeutralRidingHooks mHooks;
        int mCarries = 0;
        int mReleases = 0;
        double mLastCarry[3] = { 0, 0, 0 };
        float mLastHeading = 0.f;

        RidingBridgeRig()
        {
            mHooks.mControls = [](RiderControls& out) {
                out.valid = true;
                out.forward = 1.f;
                out.run = true;
                out.heading = 2.f;
                return true;
            };
            mHooks.mCarry = [this](double x, double y, double z, float heading) {
                ++mCarries;
                mLastCarry[0] = x;
                mLastCarry[1] = y;
                mLastCarry[2] = z;
                mLastHeading = heading;
                return true;
            };
            mHooks.mRelease = [this](double, double, double) {
                ++mReleases;
                return true;
            };
            mBridge.add(&mPack, &sRidingKey, 7, 1, "Horse", 0, 0, 0, 0.f);
        }
    };

    TEST(NeutralActorRiding, theWriterIsARidingWriterAndAnswersThroughTheHooks)
    {
        RidingBridgeRig rig;
        rig.mBridge.setRidingHooks(&rig.mHooks);
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        EXPECT_TRUE(rig.mPack.mHasRiding);
        EXPECT_TRUE(rig.mPack.mControls);
        EXPECT_TRUE(rig.mPack.mSeenControls.valid);
        EXPECT_EQ(rig.mPack.mSeenControls.forward, 1.f);
        EXPECT_TRUE(rig.mPack.mSeenControls.run);
        EXPECT_EQ(rig.mPack.mSeenControls.heading, 2.f);
        EXPECT_TRUE(rig.mPack.mCarried);
        EXPECT_EQ(rig.mCarries, 1);
        EXPECT_EQ(rig.mLastCarry[0], 1.0);
        EXPECT_EQ(rig.mLastCarry[2], 3.0);
        EXPECT_EQ(rig.mLastHeading, 0.5f);
        EXPECT_TRUE(rig.mPack.mReleased);
        EXPECT_EQ(rig.mReleases, 1);
    }

    TEST(NeutralActorRiding, withoutHooksEveryRidingCallIsRefused)
    {
        RidingBridgeRig rig;
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        EXPECT_TRUE(rig.mPack.mHasRiding);
        EXPECT_FALSE(rig.mPack.mControls);
        EXPECT_FALSE(rig.mPack.mCarried);
        EXPECT_FALSE(rig.mPack.mReleased);
        EXPECT_EQ(rig.mCarries, 0);
    }

    TEST(NeutralActorRiding, aHookThatIsEmptyIsRefusedAndNeverCalled)
    {
        RidingBridgeRig rig;
        NeutralRidingHooks none;
        rig.mBridge.setRidingHooks(&none);
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        EXPECT_FALSE(rig.mPack.mControls);
        EXPECT_FALSE(rig.mPack.mCarried);
        EXPECT_FALSE(rig.mPack.mReleased);
    }

    TEST(NeutralActorRiding, valuesThatAreNotFiniteAreRefusedAndCounted)
    {
        RidingBridgeRig rig;
        rig.mBridge.setRidingHooks(&rig.mHooks);
        rig.mPack.mCarryValues[1] = std::numeric_limits<double>::quiet_NaN();
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        EXPECT_FALSE(rig.mPack.mCarried);
        EXPECT_FALSE(rig.mPack.mReleased);
        EXPECT_EQ(rig.mCarries, 0);
        EXPECT_EQ(rig.mReleases, 0);
        EXPECT_EQ(rig.mBridge.stats().mBadValues, 2u);
        rig.mPack.mCarryValues[1] = 2.0;
        rig.mPack.mCarryHeading = std::numeric_limits<float>::infinity();
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        EXPECT_FALSE(rig.mPack.mCarried);
        EXPECT_EQ(rig.mCarries, 0);
    }

    TEST(NeutralActorRiding, theRidingCallsAreOnlyOpenInsideUpdate)
    {
        RidingBridgeRig rig;
        rig.mBridge.setRidingHooks(&rig.mHooks);
        rig.mBridge.update(&rig.mPack, 0.016f, rig.mSink);
        ASSERT_NE(rig.mPack.mWriter, nullptr);
        auto* riding = dynamic_cast<RidingWriter*>(rig.mPack.mWriter);
        ASSERT_NE(riding, nullptr);
        const int carries = rig.mCarries;
        RiderControls out;
        EXPECT_FALSE(riding->riderControls(out));
        EXPECT_FALSE(riding->carryPlayer(1.0, 2.0, 3.0, 0.f));
        EXPECT_FALSE(riding->releasePlayer(1.0, 2.0, 3.0));
        EXPECT_EQ(rig.mCarries, carries);
    }
}
