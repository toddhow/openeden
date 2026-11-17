// Task 0277: ActorRequestWriter::placeActor through the engine's bridge, with a fake pack and a fake sink (no Jolt, no
// Ptr): the values reach the sink, bad values and stale handles are refused, nothing is written outside update().
#include "apps/openmw/mwphysics/neutralactorbridge.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;

    class Sink final : public MWPhysics::NeutralRequestSink
    {
    public:
        void setRequest(const void*, float, float) override {}
        bool placeLiving(const void* key, double x, double y, double z, float heading) override
        {
            mPlaced.push_back({ key, x, y, z, heading });
            return true;
        }
        struct Place
        {
            const void* mKey;
            double mX, mY, mZ;
            float mHeading;
        };
        std::vector<Place> mPlaced;
    };

    class Pack final : public ActorRules
    {
    public:
        void onActorAdded(const ActorInfo& info) override { mHandle = info.handle; }
        void onActorRemoved(ActorHandle) override {}
        void update(float, ActorRequestWriter& writer) override
        {
            mResults.clear();
            for (const double x : mXs)
                mResults.push_back(writer.placeActor(mTarget.valid() ? mTarget : mHandle, x, 2.0, 3.0, 0.5f));
            mKept = &writer;
        }
        ActorHandle mHandle;
        ActorHandle mTarget;
        std::vector<double> mXs;
        std::vector<bool> mResults;
        ActorRequestWriter* mKept = nullptr;
    };

    struct Fixture
    {
        NeutralActorBridge bridge;
        Pack pack;
        Sink sink;
        int mKeyA = 0;
        Fixture() { bridge.add(&pack, &mKeyA, 77, 5, "Vilkas", 0, 0, 0, 0.f); }
    };

    TEST(NeutralActorPlace, aValidPlacementReachesTheSinkWithItsValues)
    {
        Fixture f;
        f.pack.mXs = { 10.0 };
        f.bridge.update(&f.pack, 0.016f, f.sink);
        ASSERT_EQ(f.sink.mPlaced.size(), 1u);
        EXPECT_EQ(f.sink.mPlaced[0].mKey, &f.mKeyA);
        EXPECT_EQ(f.sink.mPlaced[0].mX, 10.0);
        EXPECT_EQ(f.sink.mPlaced[0].mY, 2.0);
        EXPECT_EQ(f.sink.mPlaced[0].mZ, 3.0);
        EXPECT_EQ(f.sink.mPlaced[0].mHeading, 0.5f);
        EXPECT_TRUE(f.pack.mResults[0]);
    }

    TEST(NeutralActorPlace, aValueThatIsNotFiniteIsDroppedAndCounted)
    {
        Fixture f;
        f.pack.mXs = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() };
        f.bridge.update(&f.pack, 0.016f, f.sink);
        EXPECT_TRUE(f.sink.mPlaced.empty());
        EXPECT_FALSE(f.pack.mResults[0]);
        EXPECT_FALSE(f.pack.mResults[1]);
        EXPECT_EQ(f.bridge.stats().mBadValues, 2u);
    }

    TEST(NeutralActorPlace, aStaleHandleIsRefused)
    {
        Fixture f;
        f.pack.mTarget = { 77, 999 }; // right placement, wrong session
        f.pack.mXs = { 10.0 };
        f.bridge.update(&f.pack, 0.016f, f.sink);
        EXPECT_TRUE(f.sink.mPlaced.empty());
        EXPECT_FALSE(f.pack.mResults[0]);
    }

    TEST(NeutralActorPlace, nothingIsWrittenOutsideUpdate)
    {
        Fixture f;
        f.pack.mXs = {};
        f.bridge.update(&f.pack, 0.016f, f.sink);
        ASSERT_NE(f.pack.mKept, nullptr);
        EXPECT_FALSE(f.pack.mKept->placeActor(f.pack.mHandle, 1.0, 2.0, 3.0, 0.f));
        EXPECT_TRUE(f.sink.mPlaced.empty());
    }
}
