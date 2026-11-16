// Task 0254: the engine's side of ActorRequestWriter::placeCorpse (a loaded save puts a dead actor back where it lay),
// with a fake pack and a fake sink: no Skyrim data, no Jolt world and no Ptr.
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;

    struct Corpse
    {
        const void* mKey;
        double mX, mY, mZ;
        float mHeading;
    };

    class CorpseSink final : public MWPhysics::NeutralRequestSink
    {
    public:
        void setRequest(const void*, float, float) override {}
        bool placeCorpse(const void* key, double x, double y, double z, float heading) override
        {
            if (mRefuse)
                return false;
            mCorpses.push_back({ key, x, y, z, heading });
            return true;
        }
        std::vector<Corpse> mCorpses;
        bool mRefuse = false;
    };

    class CorpsePack final : public ActorRules
    {
    public:
        void onActorAdded(const ActorInfo&) override {}
        void onActorRemoved(ActorHandle) override {}
        void update(float, ActorRequestWriter& writer) override
        {
            mResults.clear();
            for (const ActorHandle& handle : mHandles)
                mResults.push_back(writer.placeCorpse(handle, mX, mY, mZ, mHeading));
        }
        std::vector<ActorHandle> mHandles;
        std::vector<bool> mResults;
        double mX = 1.0, mY = 2.0, mZ = 3.0;
        float mHeading = 0.5f;
    };

    int sKey = 0;

    TEST(NeutralPlaceCorpse, theSinkIsToldWhereTheCorpseLies)
    {
        NeutralActorBridge bridge;
        CorpsePack pack;
        CorpseSink sink;
        pack.mHandles.push_back(bridge.add(&pack, &sKey, 7, 1, "a", 0, 0, 0, 0));
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(pack.mResults.size(), 1u);
        EXPECT_TRUE(pack.mResults[0]);
        ASSERT_EQ(sink.mCorpses.size(), 1u);
        EXPECT_EQ(sink.mCorpses[0].mKey, &sKey);
        EXPECT_EQ(sink.mCorpses[0].mX, 1.0);
        EXPECT_EQ(sink.mCorpses[0].mY, 2.0);
        EXPECT_EQ(sink.mCorpses[0].mZ, 3.0);
        EXPECT_EQ(sink.mCorpses[0].mHeading, 0.5f);
    }

    TEST(NeutralPlaceCorpse, aStaleHandleIsRefusedAndNothingReachesTheSink)
    {
        NeutralActorBridge bridge;
        CorpsePack pack;
        CorpseSink sink;
        pack.mHandles.push_back(bridge.add(&pack, &sKey, 7, 1, "a", 0, 0, 0, 0)); // so that the update runs at all
        pack.mHandles.push_back(ActorHandle{ 99, 42 });
        bridge.update(&pack, 0.016f, sink);
        ASSERT_EQ(pack.mResults.size(), 2u);
        EXPECT_TRUE(pack.mResults[0]);
        EXPECT_FALSE(pack.mResults[1]);
        EXPECT_EQ(sink.mCorpses.size(), 1u); // only the live one
    }

    TEST(NeutralPlaceCorpse, aValueThatIsNotFiniteIsRefusedAndCounted)
    {
        NeutralActorBridge bridge;
        CorpsePack pack;
        CorpseSink sink;
        pack.mHandles.push_back(bridge.add(&pack, &sKey, 7, 1, "a", 0, 0, 0, 0));
        pack.mZ = std::numeric_limits<double>::quiet_NaN();
        bridge.update(&pack, 0.016f, sink);
        EXPECT_FALSE(pack.mResults[0]);
        EXPECT_TRUE(sink.mCorpses.empty());
        EXPECT_EQ(bridge.stats().mBadValues, 1u);
    }

    TEST(NeutralPlaceCorpse, aSinkThatRefusesMakesTheRequestFail)
    {
        NeutralActorBridge bridge;
        CorpsePack pack;
        CorpseSink sink;
        sink.mRefuse = true;
        pack.mHandles.push_back(bridge.add(&pack, &sKey, 7, 1, "a", 0, 0, 0, 0));
        bridge.update(&pack, 0.016f, sink);
        EXPECT_FALSE(pack.mResults[0]);
    }

    TEST(NeutralPlaceCorpse, theDefaultSinkRefuses)
    {
        class PlainSink final : public MWPhysics::NeutralRequestSink
        {
        public:
            void setRequest(const void*, float, float) override {}
        };
        NeutralActorBridge bridge;
        CorpsePack pack;
        PlainSink sink;
        pack.mHandles.push_back(bridge.add(&pack, &sKey, 7, 1, "a", 0, 0, 0, 0));
        bridge.update(&pack, 0.016f, sink);
        EXPECT_FALSE(pack.mResults[0]);
    }
}
