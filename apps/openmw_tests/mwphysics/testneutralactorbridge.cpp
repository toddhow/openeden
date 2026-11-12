// Task 0135 stage S3: the engine's side of the actor hooks contract, tested with a fake pack ("test pack") and a fake
// sink, so no Skyrim data, no Jolt world and no Ptr is needed. Written against the header before the implementation
// (the first run was against a stub, see the task's stage log).
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;

    struct RecordedAdd
    {
        ActorInfo info;
        std::string editorId;
    };

    // A pack that records what it is told and writes the requests it is given each update.
    class TestPack final : public ActorRules
    {
    public:
        void onActorAdded(const ActorInfo& info) override
        {
            if (mThrow)
                throw std::runtime_error("pack failure");
            mAdds.push_back({ info, std::string(info.editorId) });
            mLive.push_back(info.handle);
        }
        void onActorRemoved(ActorHandle handle) override
        {
            if (mThrow)
                throw std::runtime_error("pack failure");
            mRemoved.push_back(handle);
            std::erase(mLive, handle);
        }
        void update(float seconds, ActorRequestWriter& writer) override
        {
            if (mThrow)
                throw std::runtime_error("pack failure");
            ++mUpdates;
            mSeconds = seconds;
            mLastResults.clear();
            for (const auto& [handle, vel] : mWrites)
                mLastResults.push_back(writer.setMovement(handle, vel.first, vel.second));
            mKeptWriter = &writer;
        }

        std::vector<RecordedAdd> mAdds;
        std::vector<ActorHandle> mRemoved;
        std::vector<ActorHandle> mLive;
        std::vector<std::pair<ActorHandle, std::pair<float, float>>> mWrites;
        std::vector<bool> mLastResults;
        ActorRequestWriter* mKeptWriter = nullptr;
        int mUpdates = 0;
        float mSeconds = -1.f;
        bool mThrow = false;
    };

    struct Request
    {
        const void* mKey;
        float mVelX;
        float mVelY;
    };

    class TestSink final : public MWPhysics::NeutralRequestSink
    {
    public:
        TestSink() { mRequests.reserve(64); }
        void setRequest(const void* key, float velX, float velY) override { mRequests.push_back({ key, velX, velY }); }
        std::vector<Request> mRequests;
    };

    int sKeyA = 0;
    int sKeyB = 0;
    int sKeyC = 0;

    TEST(NeutralActorBridge, addMakesAValidHandleAndTellsThePack)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        const ActorHandle h = bridge.add(&pack, &sKeyA, 77, 5, "Alpha", 1.5, -2.5, 3.5, 0.25f);
        EXPECT_TRUE(h.valid());
        EXPECT_EQ(h.reference, 77u);
        EXPECT_EQ(bridge.size(), 1u);
        ASSERT_EQ(pack.mAdds.size(), 1u);
        const ActorInfo& info = pack.mAdds[0].info;
        EXPECT_EQ(info.handle, h);
        EXPECT_EQ(info.base, 5u);
        EXPECT_EQ(pack.mAdds[0].editorId, "Alpha");
        EXPECT_EQ(info.x, 1.5);
        EXPECT_EQ(info.y, -2.5);
        EXPECT_EQ(info.z, 3.5);
        EXPECT_EQ(info.heading, 0.25f);
        EXPECT_EQ(bridge.handleOf(&sKeyA), h);
        EXPECT_EQ(bridge.stats().mAdded, 1u);
    }

    TEST(NeutralActorBridge, sessionsAreAscendingAndNeverReused)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        const ActorHandle a = bridge.add(&pack, &sKeyA, 1, 1, "a", 0, 0, 0, 0);
        const ActorHandle b = bridge.add(&pack, &sKeyB, 2, 1, "b", 0, 0, 0, 0);
        EXPECT_LT(a.session, b.session);
        bridge.remove(&pack, &sKeyA);
        const ActorHandle c = bridge.add(&pack, &sKeyC, 1, 1, "a again", 0, 0, 0, 0);
        EXPECT_GT(c.session, b.session);
        EXPECT_NE(c, a);
        EXPECT_EQ(c.reference, a.reference); // the same placement comes back with a new session
    }

    TEST(NeutralActorBridge, addIsRefusedForBadInput)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        EXPECT_FALSE(bridge.add(&pack, nullptr, 1, 1, "x", 0, 0, 0, 0).valid());
        EXPECT_FALSE(bridge.add(&pack, &sKeyA, kNoWorldId, 1, "x", 0, 0, 0, 0).valid());
        const ActorHandle first = bridge.add(&pack, &sKeyA, 1, 1, "x", 0, 0, 0, 0);
        EXPECT_TRUE(first.valid());
        EXPECT_FALSE(bridge.add(&pack, &sKeyA, 2, 1, "dup", 0, 0, 0, 0).valid()); // the key is registered
        EXPECT_EQ(bridge.size(), 1u);
        EXPECT_EQ(pack.mAdds.size(), 1u); // refused adds are never told to the pack
    }

    TEST(NeutralActorBridge, addWithNoRulesStillRegisters)
    {
        NeutralActorBridge bridge;
        const ActorHandle h = bridge.add(nullptr, &sKeyA, 3, 1, "x", 0, 0, 0, 0);
        EXPECT_TRUE(h.valid());
        EXPECT_EQ(bridge.size(), 1u);
        bridge.remove(nullptr, &sKeyA);
        EXPECT_EQ(bridge.size(), 0u);
    }

    TEST(NeutralActorBridge, removeTellsThePackWithTheHandleAndForgetsIt)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        const ActorHandle a = bridge.add(&pack, &sKeyA, 1, 1, "a", 0, 0, 0, 0);
        const ActorHandle b = bridge.add(&pack, &sKeyB, 2, 1, "b", 0, 0, 0, 0);
        bridge.remove(&pack, &sKeyA);
        ASSERT_EQ(pack.mRemoved.size(), 1u);
        EXPECT_EQ(pack.mRemoved[0], a);
        EXPECT_EQ(bridge.size(), 1u);
        EXPECT_FALSE(bridge.handleOf(&sKeyA).valid());
        EXPECT_EQ(bridge.handleOf(&sKeyB), b);
        bridge.remove(&pack, &sKeyA); // unknown now: nothing happens
        EXPECT_EQ(pack.mRemoved.size(), 1u);
        EXPECT_EQ(bridge.stats().mRemoved, 1u);
    }

    TEST(NeutralActorBridge, addAndRemoveOrderIsWhatThePackSees)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        const ActorHandle a = bridge.add(&pack, &sKeyA, 1, 1, "a", 0, 0, 0, 0);
        const ActorHandle b = bridge.add(&pack, &sKeyB, 2, 1, "b", 0, 0, 0, 0);
        const ActorHandle c = bridge.add(&pack, &sKeyC, 3, 1, "c", 0, 0, 0, 0);
        bridge.remove(&pack, &sKeyB);
        bridge.remove(&pack, &sKeyA);
        EXPECT_EQ(pack.mLive, std::vector<ActorHandle>({ c }));
        EXPECT_EQ(pack.mRemoved, (std::vector<ActorHandle>{ b, a }));
        EXPECT_EQ(pack.mAdds[0].info.handle, a);
        EXPECT_EQ(pack.mAdds[2].info.handle, c);
    }

    TEST(NeutralActorBridge, updatePassesTheTimeAndLivesOnlyForValidRequests)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        TestSink sink;
        const ActorHandle a = bridge.add(&pack, &sKeyA, 10, 1, "a", 0, 0, 0, 0);
        const ActorHandle b = bridge.add(&pack, &sKeyB, 11, 1, "b", 0, 0, 0, 0);
        pack.mWrites = { { b, { 3.f, -4.f } }, { a, { 1.f, 2.f } } };
        bridge.update(&pack, 0.5f, sink);
        EXPECT_EQ(pack.mUpdates, 1);
        EXPECT_EQ(pack.mSeconds, 0.5f);
        ASSERT_EQ(sink.mRequests.size(), 2u);
        EXPECT_EQ(sink.mRequests[0].mKey, &sKeyB);
        EXPECT_EQ(sink.mRequests[0].mVelX, 3.f);
        EXPECT_EQ(sink.mRequests[0].mVelY, -4.f);
        EXPECT_EQ(sink.mRequests[1].mKey, &sKeyA);
        EXPECT_EQ(pack.mLastResults, (std::vector<bool>{ true, true }));
        EXPECT_EQ(bridge.stats().mRequests, 2u);
    }

    TEST(NeutralActorBridge, aStaleHandleIsDroppedAndCounted)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        TestSink sink;
        const ActorHandle a = bridge.add(&pack, &sKeyA, 10, 1, "a", 0, 0, 0, 0);
        bridge.remove(&pack, &sKeyA);
        const ActorHandle again = bridge.add(&pack, &sKeyB, 10, 1, "a2", 0, 0, 0, 0); // same placement, new session
        ActorHandle wrongSession = again;
        wrongSession.session += 100;
        ActorHandle wrongReference = again;
        wrongReference.reference = 99;
        pack.mWrites = { { a, { 1.f, 0.f } }, { wrongSession, { 1.f, 0.f } }, { wrongReference, { 1.f, 0.f } },
            { ActorHandle{}, { 1.f, 0.f } }, { again, { 7.f, 8.f } } };
        bridge.update(&pack, 0.016f, sink);
        EXPECT_EQ(pack.mLastResults, (std::vector<bool>{ false, false, false, false, true }));
        ASSERT_EQ(sink.mRequests.size(), 1u);
        EXPECT_EQ(sink.mRequests[0].mKey, &sKeyB);
        EXPECT_EQ(bridge.stats().mStale, 4u);
    }

    TEST(NeutralActorBridge, valuesThatAreNotFiniteAreDroppedAndCounted)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        TestSink sink;
        const ActorHandle a = bridge.add(&pack, &sKeyA, 10, 1, "a", 0, 0, 0, 0);
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        pack.mWrites = { { a, { nan, 0.f } }, { a, { 0.f, inf } }, { a, { -inf, 1.f } } };
        bridge.update(&pack, 0.016f, sink);
        EXPECT_TRUE(sink.mRequests.empty());
        EXPECT_EQ(bridge.stats().mBadValues, 3u);
    }

    TEST(NeutralActorBridge, aRequestOutsideUpdateIsDropped)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        TestSink sink;
        const ActorHandle a = bridge.add(&pack, &sKeyA, 10, 1, "a", 0, 0, 0, 0);
        pack.mWrites = { { a, { 1.f, 1.f } } };
        bridge.update(&pack, 0.016f, sink);
        ASSERT_NE(pack.mKeptWriter, nullptr);
        sink.mRequests.clear();
        EXPECT_FALSE(pack.mKeptWriter->setMovement(a, 5.f, 5.f)); // the pack kept the writer past update()
        EXPECT_TRUE(sink.mRequests.empty());
        EXPECT_EQ(bridge.stats().mOutsideUpdate, 1u);
    }

    TEST(NeutralActorBridge, aRequestFromOnAddedIsDropped)
    {
        // The pack is told about an actor before any update: it cannot write yet.
        class Eager final : public ActorRules
        {
        public:
            void onActorAdded(const ActorInfo&) override {}
            void onActorRemoved(ActorHandle) override {}
            void update(float, ActorRequestWriter& w) override { mWriter = &w; }
            ActorRequestWriter* mWriter = nullptr;
        } pack;
        NeutralActorBridge bridge;
        TestSink sink;
        const ActorHandle a = bridge.add(&pack, &sKeyA, 1, 1, "a", 0, 0, 0, 0);
        bridge.update(&pack, 0.1f, sink);
        ASSERT_NE(pack.mWriter, nullptr);
        EXPECT_FALSE(pack.mWriter->setMovement(a, 1.f, 1.f));
        EXPECT_TRUE(sink.mRequests.empty());
    }

    TEST(NeutralActorBridge, updateWithNoRulesOrNoActorsDoesNothing)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        TestSink sink;
        bridge.update(&pack, 0.1f, sink); // no actors: the pack is not even asked
        EXPECT_EQ(pack.mUpdates, 0);
        bridge.add(&pack, &sKeyA, 1, 1, "a", 0, 0, 0, 0);
        bridge.update(nullptr, 0.1f, sink);
        EXPECT_EQ(pack.mUpdates, 0);
    }

    TEST(NeutralActorBridge, badFrameTimesArePassedAsZero)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        TestSink sink;
        bridge.add(&pack, &sKeyA, 1, 1, "a", 0, 0, 0, 0);
        bridge.update(&pack, -1.f, sink);
        EXPECT_EQ(pack.mSeconds, 0.f);
        bridge.update(&pack, std::numeric_limits<float>::quiet_NaN(), sink);
        EXPECT_EQ(pack.mSeconds, 0.f);
        bridge.update(&pack, std::numeric_limits<float>::infinity(), sink);
        EXPECT_EQ(pack.mSeconds, 0.f);
        bridge.update(&pack, 0.02f, sink);
        EXPECT_EQ(pack.mSeconds, 0.02f);
    }

    TEST(NeutralActorBridge, aThrowingPackIsContainedAndCounted)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        TestSink sink;
        pack.mThrow = true;
        EXPECT_TRUE(bridge.add(&pack, &sKeyA, 1, 1, "a", 0, 0, 0, 0).valid()); // the actor exists in the engine anyway
        EXPECT_NO_THROW(bridge.update(&pack, 0.1f, sink));
        EXPECT_NO_THROW(bridge.remove(&pack, &sKeyA));
        EXPECT_EQ(bridge.stats().mRulesFailures, 3u);
        EXPECT_EQ(bridge.size(), 0u);
    }

    TEST(NeutralActorBridge, theTableIsCapped)
    {
        NeutralActorBridge bridge;
        std::vector<int> keys(NeutralActorBridge::kMaxActors + 1);
        std::size_t added = 0;
        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            if (bridge.add(nullptr, &keys[i], static_cast<WorldId>(i + 1), 1, "x", 0, 0, 0, 0).valid())
                ++added;
        }
        EXPECT_EQ(added, NeutralActorBridge::kMaxActors);
    }

    TEST(NeutralActorBridge, aFrameAllocatesNothing)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        TestSink sink;
        std::vector<int> keys(32);
        std::vector<ActorHandle> handles;
        for (std::size_t i = 0; i < keys.size(); ++i)
            handles.push_back(bridge.add(&pack, &keys[i], static_cast<WorldId>(i + 1), 1, "x", 0, 0, 0, 0));
        for (const ActorHandle& h : handles)
            pack.mWrites.push_back({ h, { 30.f, 0.f } });
        pack.mLastResults.reserve(handles.size());
        bridge.update(&pack, 0.016f, sink); // warm up
        sink.mRequests.clear();
        TestSupport::AllocationCounter counter;
        for (int frame = 0; frame < 200; ++frame)
        {
            sink.mRequests.clear();
            bridge.update(&pack, 0.016f, sink);
        }
        EXPECT_EQ(counter.count(), 0u);
        EXPECT_EQ(sink.mRequests.size(), handles.size());
    }

    TEST(NeutralActorBridge, clearForgetsEverythingSilentlyAndSessionsKeepCounting)
    {
        NeutralActorBridge bridge;
        TestPack pack;
        const ActorHandle a = bridge.add(&pack, &sKeyA, 1, 1, "a", 0, 0, 0, 0);
        bridge.add(&pack, &sKeyB, 2, 1, "b", 0, 0, 0, 0);
        bridge.clear();
        EXPECT_EQ(bridge.size(), 0u);
        EXPECT_TRUE(pack.mRemoved.empty());
        EXPECT_FALSE(bridge.handleOf(&sKeyA).valid());
        const ActorHandle again = bridge.add(&pack, &sKeyA, 1, 1, "a", 0, 0, 0, 0);
        EXPECT_GT(again.session, a.session);
    }

    TEST(NeutralActorBridge, handleOfNeedsAKey)
    {
        NeutralActorBridge bridge;
        EXPECT_FALSE(bridge.handleOf(nullptr).valid());
        EXPECT_FALSE(bridge.handleOf(&sKeyA).valid());
    }
}
