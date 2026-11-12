// Task 0135 stage S4: the go-to, cancel and heading requests and the goal results through the engine's bridge, with a
// fake pack and a fake sink (no Jolt, no Ptr).
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;

    struct Event
    {
        enum Kind
        {
            Goal,
            Cancel,
            Heading,
            Move
        } mKind;
        const void* mKey;
        double mX = 0, mY = 0, mZ = 0;
        ActorSpeed mSpeed = ActorSpeed::Walk;
        float mValue = 0.f;
    };

    class Sink final : public MWPhysics::NeutralRequestSink
    {
    public:
        Sink() { mEvents.reserve(64); }
        void setRequest(const void* key, float vx, float vy) override
        {
            Event e{ Event::Move, key };
            e.mX = vx;
            e.mY = vy;
            mEvents.push_back(e);
        }
        void setGoal(const void* key, const ActorGoal& goal) override
        {
            Event e{ Event::Goal, key, goal.x, goal.y, goal.z, goal.speed };
            mEvents.push_back(e);
        }
        void cancelGoal(const void* key) override { mEvents.push_back({ Event::Cancel, key }); }
        void setHeading(const void* key, float heading) override
        {
            Event e{ Event::Heading, key };
            e.mValue = heading;
            mEvents.push_back(e);
        }
        std::vector<Event> mEvents;
    };

    // The pack writes what the test queued and logs, in order, every call the core makes to it.
    class Pack final : public ActorRules
    {
    public:
        void onActorAdded(const ActorInfo& info) override { mLive.push_back(info.handle); }
        void onActorRemoved(ActorHandle handle) override { std::erase(mLive, handle); }
        void onGoalFinished(ActorHandle handle, GoalResult result) override
        {
            if (mThrowOnResult)
                throw std::runtime_error("pack failure");
            mCalls.push_back('R');
            mResults.push_back({ handle, result });
        }
        void update(float, ActorRequestWriter& writer) override
        {
            mCalls.push_back('U');
            if (mWriteGoalTo.valid())
                mWriteResults.push_back(writer.goTo(mWriteGoalTo, mGoal));
            if (mCancel.valid())
                mWriteResults.push_back(writer.cancelGoal(mCancel));
            if (mFace.valid())
                mWriteResults.push_back(writer.setHeading(mFace, mFaceValue));
            if (mMove.valid())
                mWriteResults.push_back(writer.setMovement(mMove, 1.f, 2.f));
            if (mWriteFromResult)
                mWriteResults.push_back(mKept->goTo(mWriteGoalTo, mGoal));
        }
        std::vector<ActorHandle> mLive;
        std::vector<std::pair<ActorHandle, GoalResult>> mResults;
        std::string mCalls;
        std::vector<bool> mWriteResults;
        ActorHandle mWriteGoalTo, mCancel, mFace, mMove;
        ActorGoal mGoal{ 10.0, 20.0, 30.0, ActorSpeed::Walk };
        float mFaceValue = 0.f;
        bool mThrowOnResult = false;
        bool mWriteFromResult = false;
        ActorRequestWriter* mKept = nullptr;
    };

    int sA = 0;
    int sB = 0;

    TEST(NeutralActorGoals, aGoToReachesTheSinkWithTheKeyAndTheSpeed)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        const ActorHandle h = bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        pack.mWriteGoalTo = h;
        pack.mGoal = { 1.5, -2.5, 3.5, ActorSpeed::Run };
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(sink.mEvents.size(), 1u);
        EXPECT_EQ(sink.mEvents[0].mKind, Event::Goal);
        EXPECT_EQ(sink.mEvents[0].mKey, &sA);
        EXPECT_EQ(sink.mEvents[0].mX, 1.5);
        EXPECT_EQ(sink.mEvents[0].mY, -2.5);
        EXPECT_EQ(sink.mEvents[0].mZ, 3.5);
        EXPECT_EQ(sink.mEvents[0].mSpeed, ActorSpeed::Run);
        EXPECT_EQ(bridge.stats().mGoals, 1u);
        ASSERT_EQ(pack.mWriteResults.size(), 1u);
        EXPECT_TRUE(pack.mWriteResults[0]);
    }

    TEST(NeutralActorGoals, aStaleOrUnknownHandleOrBadValueIsDroppedAndCounted)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        const ActorHandle h = bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        auto tryGoal = [&](ActorHandle handle, ActorGoal goal) {
            pack.mWriteGoalTo = handle;
            pack.mGoal = goal;
            pack.mWriteResults.clear();
            bridge.update(&pack, 0.016f, sink);
            return pack.mWriteResults.at(0);
        };
        EXPECT_FALSE(tryGoal({ 7, h.session + 5 }, { 1, 2, 3, ActorSpeed::Walk })); // unknown session
        EXPECT_FALSE(tryGoal({ 8, h.session }, { 1, 2, 3, ActorSpeed::Walk })); // wrong placement
        EXPECT_EQ(bridge.stats().mStale, 2u);
        EXPECT_FALSE(tryGoal(h, { nan, 2, 3, ActorSpeed::Walk }));
        EXPECT_FALSE(tryGoal(h, { 1, std::numeric_limits<double>::infinity(), 3, ActorSpeed::Walk }));
        EXPECT_FALSE(tryGoal(h, { 1, 2, 3, static_cast<ActorSpeed>(9) }));
        EXPECT_EQ(bridge.stats().mBadValues, 3u);
        EXPECT_TRUE(sink.mEvents.empty());
        EXPECT_EQ(bridge.stats().mGoals, 0u);
        EXPECT_TRUE(tryGoal(h, { 1, 2, 3, ActorSpeed::Walk }));
    }

    TEST(NeutralActorGoals, aRemovedActorsHandleIsStaleForGoalsToo)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        const ActorHandle h = bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        const ActorHandle other = bridge.add(&pack, &sB, 8, 1, "B", 0, 0, 0, 0.f);
        bridge.remove(&pack, &sA);
        pack.mWriteGoalTo = h;
        bridge.update(&pack, 0.016f, sink);
        EXPECT_FALSE(pack.mWriteResults.at(0));
        EXPECT_EQ(bridge.stats().mStale, 1u);
        pack.mWriteGoalTo = other;
        pack.mWriteResults.clear();
        bridge.update(&pack, 0.016f, sink);
        EXPECT_TRUE(pack.mWriteResults.at(0));
    }

    TEST(NeutralActorGoals, cancelAndHeadingReachTheSinkAndHeadingMustBeFinite)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        const ActorHandle h = bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        pack.mCancel = h;
        pack.mFace = h;
        pack.mFaceValue = 1.25f;
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(sink.mEvents.size(), 2u);
        EXPECT_EQ(sink.mEvents[0].mKind, Event::Cancel);
        EXPECT_EQ(sink.mEvents[1].mKind, Event::Heading);
        EXPECT_EQ(sink.mEvents[1].mValue, 1.25f);
        EXPECT_EQ(bridge.stats().mCancels, 1u);
        EXPECT_EQ(bridge.stats().mHeadings, 1u);
        pack.mCancel = {};
        pack.mFaceValue = std::numeric_limits<float>::quiet_NaN();
        pack.mWriteResults.clear();
        bridge.update(&pack, 0.016f, sink);
        EXPECT_FALSE(pack.mWriteResults.at(0));
        EXPECT_EQ(bridge.stats().mBadValues, 1u);
        EXPECT_EQ(sink.mEvents.size(), 2u);
    }

    TEST(NeutralActorGoals, aReportedResultIsToldBeforeTheNextUpdateOnceAndByHandle)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        const ActorHandle h = bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        const ActorHandle other = bridge.add(&pack, &sB, 8, 1, "B", 0, 0, 0, 0.f);
        bridge.reportGoal(&sA, GoalResult::Arrived);
        bridge.reportGoal(&sB, GoalResult::NoPath);
        EXPECT_TRUE(pack.mResults.empty()); // not told in the frame it ended
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(pack.mResults.size(), 2u);
        EXPECT_EQ(pack.mResults[0].first, h);
        EXPECT_EQ(pack.mResults[0].second, GoalResult::Arrived);
        EXPECT_EQ(pack.mResults[1].first, other);
        EXPECT_EQ(pack.mResults[1].second, GoalResult::NoPath);
        EXPECT_EQ(pack.mCalls, "RRU"); // results first, then the update
        bridge.update(&pack, 0.016f, sink);
        EXPECT_EQ(pack.mResults.size(), 2u); // never twice
        EXPECT_EQ(bridge.stats().mResultsDelivered, 2u);
    }

    TEST(NeutralActorGoals, aResultForAnActorThatWentIsDroppedNotDelivered)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        const ActorHandle other = bridge.add(&pack, &sB, 8, 1, "B", 0, 0, 0, 0.f);
        bridge.reportGoal(&sA, GoalResult::Arrived);
        bridge.remove(&pack, &sA); // the cell unloaded before the next update
        bridge.reportGoal(&sB, GoalResult::Blocked);
        bridge.reportGoal(&sA, GoalResult::Arrived); // no such actor any more
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(pack.mResults.size(), 1u);
        EXPECT_EQ(pack.mResults[0].first, other);
        EXPECT_EQ(pack.mResults[0].second, GoalResult::Blocked);
        EXPECT_GE(bridge.stats().mResultsDropped, 1u);
        // a re-added actor (new session) does not inherit the old result
        const ActorHandle again = bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        EXPECT_NE(again.session, 0u);
        pack.mResults.clear();
        bridge.update(&pack, 0.016f, sink);
        EXPECT_TRUE(pack.mResults.empty());
    }

    TEST(NeutralActorGoals, anActorRemovedAndAddedAgainBeforeTheUpdateInheritsNoResult)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        bridge.reportGoal(&sA, GoalResult::Arrived);
        bridge.remove(&pack, &sA);
        bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f); // the same body key, a new session
        bridge.update(&pack, 0.016f, sink);
        EXPECT_TRUE(pack.mResults.empty());
    }

    TEST(NeutralActorGoals, withNoRulesResultsAreDroppedAndAThrowingPackIsContained)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        bridge.reportGoal(&sA, GoalResult::Arrived);
        bridge.update(nullptr, 0.016f, sink);
        bridge.update(&pack, 0.016f, sink);
        EXPECT_TRUE(pack.mResults.empty());

        bridge.reportGoal(&sA, GoalResult::Arrived);
        pack.mThrowOnResult = true;
        bridge.update(&pack, 0.016f, sink);
        EXPECT_EQ(bridge.stats().mRulesFailures, 1u);
        EXPECT_EQ(pack.mCalls.back(), 'U'); // the update still ran
    }

    TEST(NeutralActorGoals, clearForgetsPendingResultsSilently)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        bridge.reportGoal(&sA, GoalResult::Arrived);
        bridge.clear();
        bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        bridge.update(&pack, 0.016f, sink);
        EXPECT_TRUE(pack.mResults.empty());
    }

    TEST(NeutralActorGoals, aGoalWrittenFromAResultCallbackIsRefused)
    {
        // The writer is open only inside update(); onGoalFinished runs before it.
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        const ActorHandle h = bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        pack.mWriteGoalTo = h;
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(sink.mEvents.size(), 1u);
        pack.mWriteGoalTo = {};
        // A writer kept from an update call is refused outside it.
        struct Keeper final : ActorRules
        {
            void onActorAdded(const ActorInfo&) override {}
            void onActorRemoved(ActorHandle) override {}
            void update(float, ActorRequestWriter& writer) override { mKept = &writer; }
            ActorRequestWriter* mKept = nullptr;
        } keeper;
        NeutralActorBridge other;
        const ActorHandle k = other.add(&keeper, &sB, 9, 1, "K", 0, 0, 0, 0.f);
        other.update(&keeper, 0.016f, sink);
        ASSERT_NE(keeper.mKept, nullptr);
        EXPECT_FALSE(keeper.mKept->goTo(k, ActorGoal{}));
        EXPECT_FALSE(keeper.mKept->cancelGoal(k));
        EXPECT_FALSE(keeper.mKept->setHeading(k, 0.f));
        EXPECT_EQ(other.stats().mOutsideUpdate, 3u);
    }

    TEST(NeutralActorGoals, reportingAndDeliveringResultsAllocatesNothing)
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        std::vector<int> keys(32);
        for (std::size_t i = 0; i < keys.size(); ++i)
            bridge.add(&pack, &keys[i], 100 + i, 1, "A", 0, 0, 0, 0.f);
        pack.mCalls.reserve(1u << 16);
        pack.mResults.reserve(1u << 12);
        bridge.update(&pack, 0.016f, sink);
        TestSupport::AllocationCounter counter;
        for (int frame = 0; frame < 50; ++frame)
        {
            for (std::size_t i = 0; i < keys.size(); ++i)
                bridge.reportGoal(&keys[i], GoalResult::Arrived);
            bridge.update(&pack, 0.016f, sink);
        }
        EXPECT_EQ(counter.count(), 0u);
        EXPECT_EQ(pack.mResults.size(), 50u * keys.size());
    }

    TEST(NeutralActorGoals, aSinkThatOnlyKnowsVelocitiesIgnoresTheNewRequests)
    {
        struct OldSink final : MWPhysics::NeutralRequestSink
        {
            void setRequest(const void*, float, float) override { ++mMoves; }
            int mMoves = 0;
        } oldSink;
        NeutralActorBridge bridge;
        Pack pack;
        const ActorHandle h = bridge.add(&pack, &sA, 7, 1, "A", 0, 0, 0, 0.f);
        pack.mWriteGoalTo = h;
        pack.mCancel = h;
        pack.mFace = h;
        pack.mMove = h;
        bridge.update(&pack, 0.016f, oldSink);
        EXPECT_EQ(oldSink.mMoves, 1);
    }
}
