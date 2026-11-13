// Task 0135 stage S6b: furniture-use requests through the engine's bridge, with a fake pack and a fake sink (no Jolt,
// no Ptr): validation, the clip is copied by the sink and not kept by the bridge, stale handles, outside update().
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;

    struct Use
    {
        const void* mKey;
        FurnitureUse mUse;
        std::string mClip; // copied: the view in mUse is only valid during the call
    };

    class Sink final : public MWPhysics::NeutralRequestSink
    {
    public:
        void setRequest(const void*, float, float) override {}
        void useFurniture(const void* key, const FurnitureUse& use) override
        {
            mUses.push_back({ key, use, std::string(use.clip) });
        }
        void leaveFurniture(const void* key, double x, double y, double z) override
        {
            mLeaves.push_back({ key, x, y, z });
        }
        struct Leave
        {
            const void* mKey;
            double mX, mY, mZ;
        };
        std::vector<Use> mUses;
        std::vector<Leave> mLeaves;
    };

    class Pack final : public ActorRules
    {
    public:
        void onActorAdded(const ActorInfo& info) override { mHandle = info.handle; }
        void onActorRemoved(ActorHandle) override {}
        void update(float, ActorRequestWriter& writer) override
        {
            mUseResults.clear();
            for (const FurnitureUse& use : mUsesToWrite)
                mUseResults.push_back(writer.useFurniture(mTarget.valid() ? mTarget : mHandle, use));
            if (mLeave)
                mLeaveResult = writer.leaveFurniture(mTarget.valid() ? mTarget : mHandle, mLx, mLy, mLz);
            mKept = &writer;
        }
        ActorHandle mHandle;
        ActorHandle mTarget;
        std::vector<FurnitureUse> mUsesToWrite;
        std::vector<bool> mUseResults;
        bool mLeave = false;
        double mLx = 0, mLy = 0, mLz = 0;
        bool mLeaveResult = false;
        ActorRequestWriter* mKept = nullptr;
    };

    FurnitureUse goodUse()
    {
        FurnitureUse u;
        u.x = 10;
        u.y = 20;
        u.z = 30;
        u.heading = 1.5f;
        u.pose = FurniturePose::Sleep;
        u.clip = "bed_leftsleep";
        u.clipPerSex = false;
        return u;
    }

    struct Fixture
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        int mKeyA = 0;
        Fixture() { bridge.add(&pack, &mKeyA, 77, 5, "Mikael", 0, 0, 0, 0.f); }
    };

    TEST(NeutralActorFurniture, aValidUseReachesTheSinkWithItsValues)
    {
        Fixture f;
        f.pack.mUsesToWrite = { goodUse() };
        f.bridge.update(&f.pack, 0.016f, f.sink);
        ASSERT_EQ(f.sink.mUses.size(), 1u);
        EXPECT_EQ(f.sink.mUses[0].mKey, &f.mKeyA);
        EXPECT_EQ(f.sink.mUses[0].mUse.x, 10.0);
        EXPECT_EQ(f.sink.mUses[0].mUse.heading, 1.5f);
        EXPECT_EQ(f.sink.mUses[0].mUse.pose, FurniturePose::Sleep);
        EXPECT_EQ(f.sink.mUses[0].mClip, "bed_leftsleep");
        EXPECT_TRUE(f.pack.mUseResults[0]);
        EXPECT_EQ(f.bridge.stats().mFurnitureUses, 1u);
    }

    TEST(NeutralActorFurniture, badValuesAreDroppedAndCounted)
    {
        Fixture f;
        FurnitureUse nanX = goodUse();
        nanX.x = std::numeric_limits<double>::quiet_NaN();
        FurnitureUse infHeading = goodUse();
        infHeading.heading = std::numeric_limits<float>::infinity();
        FurnitureUse badPose = goodUse();
        badPose.pose = static_cast<FurniturePose>(9);
        FurnitureUse emptyClip = goodUse();
        emptyClip.clip = "";
        FurnitureUse pathClip = goodUse();
        pathClip.clip = "../x";
        FurnitureUse dotClip = goodUse();
        dotClip.clip = "a.b";
        const std::string longName(kMaxFurnitureClipName + 1, 'a');
        FurnitureUse tooLong = goodUse();
        tooLong.clip = longName;
        f.pack.mUsesToWrite = { nanX, infHeading, badPose, emptyClip, pathClip, dotClip, tooLong };
        f.bridge.update(&f.pack, 0.016f, f.sink);
        EXPECT_TRUE(f.sink.mUses.empty());
        for (bool accepted : f.pack.mUseResults)
            EXPECT_FALSE(accepted);
        EXPECT_EQ(f.bridge.stats().mBadValues, 7u);
        EXPECT_EQ(f.bridge.stats().mFurnitureUses, 0u);
    }

    TEST(NeutralActorFurniture, aClipNameOfExactlyTheLimitIsAccepted)
    {
        Fixture f;
        const std::string name(kMaxFurnitureClipName, 'Z');
        FurnitureUse u = goodUse();
        u.clip = name;
        f.pack.mUsesToWrite = { u };
        f.bridge.update(&f.pack, 0.016f, f.sink);
        ASSERT_EQ(f.sink.mUses.size(), 1u);
        EXPECT_EQ(f.sink.mUses[0].mClip, name);
    }

    TEST(NeutralActorFurniture, aStaleHandleIsRefusedAndCounted)
    {
        Fixture f;
        f.pack.mTarget = { 77, 999 }; // right placement, wrong session
        f.pack.mUsesToWrite = { goodUse() };
        f.pack.mLeave = true;
        f.bridge.update(&f.pack, 0.016f, f.sink);
        EXPECT_TRUE(f.sink.mUses.empty());
        EXPECT_TRUE(f.sink.mLeaves.empty());
        EXPECT_FALSE(f.pack.mUseResults[0]);
        EXPECT_FALSE(f.pack.mLeaveResult);
        EXPECT_EQ(f.bridge.stats().mStale, 2u);
    }

    TEST(NeutralActorFurniture, leavingPassesThePositionAndRefusesNonFiniteOnes)
    {
        Fixture f;
        f.pack.mLeave = true;
        f.pack.mLx = 1;
        f.pack.mLy = 2;
        f.pack.mLz = 3;
        f.bridge.update(&f.pack, 0.016f, f.sink);
        ASSERT_EQ(f.sink.mLeaves.size(), 1u);
        EXPECT_EQ(f.sink.mLeaves[0].mKey, &f.mKeyA);
        EXPECT_EQ(f.sink.mLeaves[0].mY, 2.0);
        EXPECT_TRUE(f.pack.mLeaveResult);
        f.pack.mLz = std::numeric_limits<double>::infinity();
        f.bridge.update(&f.pack, 0.016f, f.sink);
        EXPECT_EQ(f.sink.mLeaves.size(), 1u);
        EXPECT_FALSE(f.pack.mLeaveResult);
        EXPECT_EQ(f.bridge.stats().mFurnitureLeaves, 1u);
        EXPECT_EQ(f.bridge.stats().mBadValues, 1u);
    }

    TEST(NeutralActorFurniture, aWriterKeptPastUpdateIsRefused)
    {
        Fixture f;
        f.bridge.update(&f.pack, 0.016f, f.sink);
        ASSERT_NE(f.pack.mKept, nullptr);
        EXPECT_FALSE(f.pack.mKept->useFurniture(f.pack.mHandle, goodUse()));
        EXPECT_FALSE(f.pack.mKept->leaveFurniture(f.pack.mHandle, 1, 2, 3));
        EXPECT_TRUE(f.sink.mUses.empty());
        EXPECT_EQ(f.bridge.stats().mOutsideUpdate, 2u);
    }

    TEST(NeutralActorFurniture, aSinkThatKnowsNothingOfFurnitureIgnoresTheRequestsButTheCallSucceeds)
    {
        // The default sink functions do nothing (an old sink keeps working); the bridge still counts the request.
        struct OldSink final : MWPhysics::NeutralRequestSink
        {
            void setRequest(const void*, float, float) override {}
        } oldSink;
        Fixture f;
        f.pack.mUsesToWrite = { goodUse() };
        f.bridge.update(&f.pack, 0.016f, oldSink);
        EXPECT_TRUE(f.pack.mUseResults[0]);
        EXPECT_EQ(f.bridge.stats().mFurnitureUses, 1u);
    }

    TEST(NeutralActorFurniture, theWriterBaseClassRefusesByDefault)
    {
        struct Bare final : ActorRequestWriter
        {
            bool setMovement(ActorHandle, float, float) override { return true; }
        } bare;
        EXPECT_FALSE(bare.useFurniture({ 1, 1 }, goodUse()));
        EXPECT_FALSE(bare.leaveFurniture({ 1, 1 }, 0, 0, 0));
    }
}
