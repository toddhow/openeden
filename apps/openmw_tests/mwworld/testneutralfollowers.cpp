// Task 0196 (followers): the pure parts of the engine's side. Where carried followers stand, the player's position
// as the pack reads it through ActorRequestWriter::playerPosition, and the FollowerRules defaults.
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"
#include "apps/openmw/mwworld/neutralfollowers.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace openeden::sdk::game;

    ESM::Position at(float x, float y, float z, float yaw)
    {
        ESM::Position p;
        p.pos[0] = x;
        p.pos[1] = y;
        p.pos[2] = z;
        p.rot[0] = 0.f;
        p.rot[1] = 0.f;
        p.rot[2] = yaw;
        return p;
    }

    TEST(NeutralFollowerArrivals, nothingForNoFollowersOrABadPosition)
    {
        EXPECT_TRUE(MWWorld::planFollowerArrivals(at(0, 0, 0, 0), 0).empty());
        EXPECT_TRUE(MWWorld::planFollowerArrivals(at(std::numeric_limits<float>::quiet_NaN(), 0, 0, 0), 2).empty());
        EXPECT_TRUE(MWWorld::planFollowerArrivals(at(0, 0, 0, std::numeric_limits<float>::infinity()), 2).empty());
    }

    TEST(NeutralFollowerArrivals, oneFollowerStandsInFrontOfTheArrivalFacingTheSameWay)
    {
        // Yaw 0 faces +Y.
        const auto spots = MWWorld::planFollowerArrivals(at(100, 200, 50, 0.f), 1);
        ASSERT_EQ(spots.size(), 1u);
        EXPECT_NEAR(spots[0].pos[0], 100.f, 1e-3);
        EXPECT_NEAR(spots[0].pos[1], 200.f + MWWorld::kFollowerArrivalForward, 1e-3);
        EXPECT_NEAR(spots[0].pos[2], 50.f, 1e-3);
        EXPECT_NEAR(spots[0].rot[2], 0.f, 1e-6);
    }

    TEST(NeutralFollowerArrivals, theYawIsClockwiseFromPlusY)
    {
        // A quarter turn clockwise faces +X.
        const auto spots = MWWorld::planFollowerArrivals(at(0, 0, 0, 1.57079632679f), 1);
        ASSERT_EQ(spots.size(), 1u);
        EXPECT_NEAR(spots[0].pos[0], MWWorld::kFollowerArrivalForward, 1e-2);
        EXPECT_NEAR(spots[0].pos[1], 0.f, 1e-2);
    }

    TEST(NeutralFollowerArrivals, severalAreSpreadSidewaysAndCenteredOnTheArrival)
    {
        const auto spots = MWWorld::planFollowerArrivals(at(0, 0, 0, 0.f), 3);
        ASSERT_EQ(spots.size(), 3u);
        // Facing +Y, the player's right is +X.
        EXPECT_NEAR(spots[0].pos[0], -MWWorld::kFollowerArrivalSpread, 1e-3);
        EXPECT_NEAR(spots[1].pos[0], 0.f, 1e-3);
        EXPECT_NEAR(spots[2].pos[0], MWWorld::kFollowerArrivalSpread, 1e-3);
        for (const auto& s : spots)
            EXPECT_NEAR(s.pos[1], MWWorld::kFollowerArrivalForward, 1e-3);
    }

    TEST(NeutralFollowerArrivals, theSameInputGivesTheSameSpots)
    {
        const auto a = MWWorld::planFollowerArrivals(at(10, 20, 30, 1.f), 4);
        const auto b = MWWorld::planFollowerArrivals(at(10, 20, 30, 1.f), 4);
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
            for (int k = 0; k < 3; ++k)
                EXPECT_EQ(a[i].pos[k], b[i].pos[k]);
    }

    // ---- the player's position through the bridge ----------------------------------------------------------------

    class AskingPack final : public ActorRules
    {
    public:
        void onActorAdded(const ActorInfo&) override {}
        void onActorRemoved(ActorHandle) override {}
        void update(float, ActorRequestWriter& writer) override
        {
            mAsked = true;
            mValid = writer.playerPosition(mX, mY, mZ);
            mWriter = &writer;
        }
        bool mAsked = false;
        bool mValid = false;
        double mX = -1, mY = -1, mZ = -1;
        ActorRequestWriter* mWriter = nullptr;
    };

    class NullSink final : public MWPhysics::NeutralRequestSink
    {
    public:
        void setRequest(const void*, float, float) override {}
    };

    int sKey = 0;

    TEST(NeutralPlayerPosition, thePackReadsWhatTheCoreSetDuringUpdate)
    {
        MWPhysics::NeutralActorBridge bridge;
        AskingPack pack;
        NullSink sink;
        ASSERT_TRUE(bridge.add(&pack, &sKey, 1, 2, "Test", 0, 0, 0, 0.f).valid());
        bridge.setPlayerPosition(true, 12.5, -300.25, 66.0);
        bridge.update(&pack, 0.016f, sink);
        EXPECT_TRUE(pack.mAsked);
        EXPECT_TRUE(pack.mValid);
        EXPECT_EQ(pack.mX, 12.5);
        EXPECT_EQ(pack.mY, -300.25);
        EXPECT_EQ(pack.mZ, 66.0);
    }

    TEST(NeutralPlayerPosition, withoutAPositionOrWithANonFiniteOneTheWriterRefuses)
    {
        MWPhysics::NeutralActorBridge bridge;
        AskingPack pack;
        NullSink sink;
        ASSERT_TRUE(bridge.add(&pack, &sKey, 1, 2, "Test", 0, 0, 0, 0.f).valid());
        bridge.update(&pack, 0.016f, sink); // never set
        EXPECT_FALSE(pack.mValid);
        bridge.setPlayerPosition(true, std::numeric_limits<double>::quiet_NaN(), 0, 0);
        bridge.update(&pack, 0.016f, sink);
        EXPECT_FALSE(pack.mValid);
        bridge.setPlayerPosition(false, 1, 2, 3);
        bridge.update(&pack, 0.016f, sink);
        EXPECT_FALSE(pack.mValid);
    }

    TEST(NeutralPlayerPosition, aWriterKeptPastTheUpdateRefusesAgain)
    {
        MWPhysics::NeutralActorBridge bridge;
        AskingPack pack;
        NullSink sink;
        ASSERT_TRUE(bridge.add(&pack, &sKey, 1, 2, "Test", 0, 0, 0, 0.f).valid());
        bridge.setPlayerPosition(true, 1, 2, 3);
        bridge.update(&pack, 0.016f, sink);
        double x = 0, y = 0, z = 0;
        EXPECT_FALSE(pack.mWriter->playerPosition(x, y, z));
    }

    TEST(NeutralPlayerPosition, aCoreWithoutAnOverrideRefusesByDefault)
    {
        struct Bare final : ActorRequestWriter
        {
            bool setMovement(ActorHandle, float, float) override { return false; }
        } bare;
        double x = 0, y = 0, z = 0;
        EXPECT_FALSE(bare.playerPosition(x, y, z));
    }
}
