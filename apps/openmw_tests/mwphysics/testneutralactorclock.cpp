// Task 0135 stage S5: the game clock the pack reads through ActorRequestWriter::clock() (SDK actor_rules.hpp 0.3).
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;

    class NullSink final : public MWPhysics::NeutralRequestSink
    {
    public:
        void setRequest(const void*, float, float) override {}
    };

    class Reader final : public ActorRules
    {
    public:
        void onActorAdded(const ActorInfo&) override {}
        void onActorRemoved(ActorHandle) override {}
        void update(float, ActorRequestWriter& writer) override { mSeen.push_back(writer.clock()); }
        std::vector<ActorClock> mSeen;
    };

    int sKey = 0;

    TEST(NeutralActorClock, theDefaultClockIsInvalid)
    {
        NeutralActorBridge bridge;
        Reader pack;
        NullSink sink;
        bridge.add(&pack, &sKey, 7, 1, "A", 0, 0, 0, 0.f);
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(pack.mSeen.size(), 1u);
        EXPECT_FALSE(pack.mSeen[0].valid);
    }

    TEST(NeutralActorClock, theClockSetBeforeAnUpdateIsWhatThePackReads)
    {
        NeutralActorBridge bridge;
        Reader pack;
        NullSink sink;
        bridge.add(&pack, &sKey, 7, 1, "A", 0, 0, 0, 0.f);
        bridge.setClock({ 18.5f, 3, true });
        bridge.update(&pack, 0.016f, sink);
        bridge.setClock({ 2.25f, 4, true });
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(pack.mSeen.size(), 2u);
        EXPECT_TRUE(pack.mSeen[0].valid);
        EXPECT_EQ(pack.mSeen[0].hour, 18.5f);
        EXPECT_EQ(pack.mSeen[0].day, 3);
        EXPECT_EQ(pack.mSeen[1].hour, 2.25f);
        EXPECT_EQ(pack.mSeen[1].day, 4);
    }

    TEST(NeutralActorClock, aClockThatIsNotAHourOfTheDayIsKeptInvalid)
    {
        NeutralActorBridge bridge;
        Reader pack;
        NullSink sink;
        bridge.add(&pack, &sKey, 7, 1, "A", 0, 0, 0, 0.f);
        for (const float bad :
            { -1.f, 24.f, 100.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            bridge.setClock({ bad, 1, true });
            bridge.update(&pack, 0.016f, sink);
        }
        bridge.setClock({ 12.f, -5, true }); // a negative day is not a day
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(pack.mSeen.size(), 6u);
        for (const ActorClock& c : pack.mSeen)
            EXPECT_FALSE(c.valid);
    }
}
