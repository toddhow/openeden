// Tests of what ExtensionWorld does with physics contacts (task 0017). A real game cannot be made to produce them on
// demand: actors are kinematic, and Jolt reports no contact between a kinematic body and a static one. Written by the
// implementer, not the test-writing subagent, for that reason; see task 0017's notes.

#include "apps/openmw/extensionworld.hpp"

#include <components/extension/host_impl.hpp>

#include <openeden/game_events.hpp>
#include <openeden/physics_events.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace
{
    using namespace openeden::sdk;

    constexpr float kUnitsPerMeter = 69.99125109f; // Constants::UnitsPerMeter

    struct ExtensionWorldContactTest : ::testing::Test
    {
        openeden::extension::HostImpl mHost;
        OMW::ExtensionWorld mWorld{ mHost, nullptr };
        std::vector<PhysicsContactBeginEvent> mBegins;
        std::vector<PhysicsContactEndEvent> mEnds;
        std::vector<PhysicsHitEvent> mHits;
        std::thread::id mDeliveredOn;

        const JPH::BodyID mBody1{ 1, 1 };
        const JPH::BodyID mBody2{ 2, 1 };
        const JPH::Vec3 mNormal{ 0, 0, 1 };

        ExtensionWorldContactTest()
        {
            auto& bus = mHost.eventBus();
            bus.subscribe<PhysicsContactBeginEvent>("test", [this](const PhysicsContactBeginEvent& event) {
                mBegins.push_back(event);
                mDeliveredOn = std::this_thread::get_id();
            });
            bus.subscribe<PhysicsContactEndEvent>(
                "test", [this](const PhysicsContactEndEvent& event) { mEnds.push_back(event); });
            bus.subscribe<PhysicsHitEvent>("test", [this](const PhysicsHitEvent& event) { mHits.push_back(event); });
        }
    };

    TEST_F(ExtensionWorldContactTest, nothingArrivesBeforeDeliver)
    {
        mWorld.contactAdded(mBody1, mBody2, JPH::RVec3::sZero(), mNormal, false);
        EXPECT_TRUE(mBegins.empty());
        mWorld.deliver();
        EXPECT_EQ(mBegins.size(), 1u);
    }

    TEST_F(ExtensionWorldContactTest, severalSubShapeContactsAreOneContactOfTheBodies)
    {
        mWorld.contactAdded(mBody1, mBody2, JPH::RVec3::sZero(), mNormal, false);
        mWorld.contactAdded(mBody2, mBody1, JPH::RVec3::sZero(), mNormal, false);
        mWorld.contactRemoved(mBody1, mBody2);
        mWorld.deliver();
        EXPECT_EQ(mBegins.size(), 1u);
        EXPECT_TRUE(mEnds.empty());

        mWorld.contactRemoved(mBody2, mBody1);
        mWorld.deliver();
        EXPECT_EQ(mEnds.size(), 1u);
    }

    TEST_F(ExtensionWorldContactTest, theEndOfAContactNobodyWasToldOfIsIgnored)
    {
        mWorld.contactRemoved(mBody1, mBody2);
        mWorld.deliver();
        EXPECT_TRUE(mEnds.empty());
    }

    TEST_F(ExtensionWorldContactTest, contactPointIsInMeters)
    {
        mWorld.contactAdded(mBody1, mBody2, JPH::RVec3(kUnitsPerMeter * 2, 0, kUnitsPerMeter * -3), mNormal, false);
        mWorld.deliver();
        ASSERT_EQ(mBegins.size(), 1u);
        EXPECT_NEAR(mBegins[0].pointM.x, 2.0f, 1e-4f);
        EXPECT_NEAR(mBegins[0].pointM.z, -3.0f, 1e-4f);
        EXPECT_EQ(mBegins[0].normal, (PhysicsVec3{ 0.0f, 0.0f, 1.0f }));
    }

    TEST_F(ExtensionWorldContactTest, aProjectileHitComesWithItsContact)
    {
        mWorld.contactAdded(mBody1, mBody2, JPH::RVec3::sZero(), mNormal, true);
        mWorld.deliver();
        EXPECT_EQ(mHits.size(), 1u);
        EXPECT_EQ(mBegins.size(), 1u);
    }

    TEST_F(ExtensionWorldContactTest, contactsFromAPhysicsThreadAreDeliveredOnTheCallingThread)
    {
        std::thread physics([this] {
            for (int i = 0; i < 100; ++i)
            {
                mWorld.contactAdded(mBody1, mBody2, JPH::RVec3::sZero(), mNormal, false);
                mWorld.contactRemoved(mBody1, mBody2);
            }
        });
        physics.join();
        mWorld.deliver();
        EXPECT_EQ(mBegins.size(), 100u);
        EXPECT_EQ(mEnds.size(), 100u);
        EXPECT_EQ(mDeliveredOn, std::this_thread::get_id());
    }

    TEST_F(ExtensionWorldContactTest, gameEventsArriveInTheOrderTheyHappened)
    {
        std::vector<int> order;
        auto& bus = mHost.eventBus();
        bus.subscribe<GameSavedEvent>("test", [&](const GameSavedEvent&) { order.push_back(1); });
        bus.subscribe<CellLoadedEvent>("test", [&](const CellLoadedEvent&) { order.push_back(2); });
        mWorld.gameSaved();
        mWorld.cellLoaded("A cell");
        mWorld.gameSaved();
        mWorld.deliver();
        EXPECT_EQ(order, (std::vector<int>{ 1, 2, 1 }));
    }

    TEST_F(ExtensionWorldContactTest, aLongCellNameIsCutAndEndsInNul)
    {
        CellChangedEvent received;
        mHost.eventBus().subscribe<CellChangedEvent>("test", [&](const CellChangedEvent& event) { received = event; });
        mWorld.cellChanged(std::string(kEventNameCapacity * 2, 'x'));
        mWorld.deliver();
        EXPECT_EQ(std::string(received.cellName), std::string(kEventNameCapacity - 1, 'x'));
    }
}
