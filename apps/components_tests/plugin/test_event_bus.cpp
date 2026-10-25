#include <components/plugin/event_bus.hpp>
#include <openeden/event_bus.hpp>
#include <openeden/phases.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::plugin::EventBusImpl;
    using openeden::sdk::EventBus;
    using openeden::sdk::EventId;
    using openeden::sdk::Phase;

    // Test payload structures
    struct DamageEvent
    {
        static constexpr EventId kEventId = 1001;

        std::uint32_t targetEntityId = 0;
        float amount = 0.0f;
    };

    struct ScoreEvent
    {
        static constexpr EventId kEventId = 1002;

        int points = 0;
    };

    TEST(EventBusTest, ImmediatePublishDeliversSynchronously)
    {
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "plugin_combat" });

        bool received = false;
        std::uint32_t receivedTarget = 0;
        float receivedAmount = 0.0f;

        bus.subscribe<DamageEvent>("plugin_combat", [&](const DamageEvent& event) {
            received = true;
            receivedTarget = event.targetEntityId;
            receivedAmount = event.amount;
        });

        const DamageEvent dmg{ 42, 18.5f };
        bus.publishImmediate(dmg);

        EXPECT_TRUE(received);
        EXPECT_EQ(receivedTarget, 42u);
        EXPECT_FLOAT_EQ(receivedAmount, 18.5f);
    }

    TEST(EventBusTest, DeferredPublishDeliveredAtTargetPhaseBoundary)
    {
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "plugin_subscriber" });

        int eventCount = 0;
        bus.subscribe<ScoreEvent>("plugin_subscriber", [&](const ScoreEvent& /*event*/) { ++eventCount; });

        // Publish targeting PreSimulation
        const ScoreEvent score{ 100 };
        bus.publishDeferred(score, Phase::PreSimulation);

        // Before any phase processing: event has not been delivered
        EXPECT_EQ(eventCount, 0);

        // Processing a different phase (e.g. Input) does not deliver PreSimulation events
        bus.processDeferredEvents(Phase::Input);
        EXPECT_EQ(eventCount, 0);

        // Processing PreSimulation boundary delivers the deferred event
        bus.processDeferredEvents(Phase::PreSimulation);
        EXPECT_EQ(eventCount, 1);

        // Subsequent call to PreSimulation without new publications delivers nothing
        bus.processDeferredEvents(Phase::PreSimulation);
        EXPECT_EQ(eventCount, 1);
    }

    TEST(EventBusTest, DeterministicPublishOrderFifo)
    {
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "plugin_tracker" });

        std::vector<int> deliveredPoints;
        bus.subscribe<ScoreEvent>(
            "plugin_tracker", [&](const ScoreEvent& event) { deliveredPoints.push_back(event.points); });

        // Publish multiple events in sequence
        bus.publishDeferred(ScoreEvent{ 10 }, Phase::PostSimulation);
        bus.publishDeferred(ScoreEvent{ 20 }, Phase::PostSimulation);
        bus.publishDeferred(ScoreEvent{ 30 }, Phase::PostSimulation);
        bus.publishDeferred(ScoreEvent{ 40 }, Phase::PostSimulation);

        EXPECT_TRUE(deliveredPoints.empty());

        bus.processDeferredEvents(Phase::PostSimulation);

        // Verified FIFO publish order
        const std::vector<int> expected{ 10, 20, 30, 40 };
        EXPECT_EQ(deliveredPoints, expected);
    }

    TEST(EventBusTest, SubscribersCalledInPluginLoadOrder)
    {
        EventBusImpl bus;
        // Load order: alpha -> beta -> gamma
        bus.setPluginLoadOrder({ "plugin_alpha", "plugin_beta", "plugin_gamma" });

        std::vector<std::string> callSequence;

        // Subscribe in reverse/arbitrary order: gamma first, alpha second, beta third
        bus.subscribe<DamageEvent>(
            "plugin_gamma", [&](const DamageEvent& /*event*/) { callSequence.push_back("plugin_gamma"); });
        bus.subscribe<DamageEvent>(
            "plugin_alpha", [&](const DamageEvent& /*event*/) { callSequence.push_back("plugin_alpha"); });
        bus.subscribe<DamageEvent>(
            "plugin_beta", [&](const DamageEvent& /*event*/) { callSequence.push_back("plugin_beta"); });

        const DamageEvent dmg{ 1, 5.0f };
        bus.publishImmediate(dmg);

        // Invariant: delivery must strictly follow plugin load order
        const std::vector<std::string> expectedOrder{ "plugin_alpha", "plugin_beta", "plugin_gamma" };
        EXPECT_EQ(callSequence, expectedOrder);
    }

    TEST(EventBusTest, DeferredSubscribersCalledInPluginLoadOrder)
    {
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "first_plugin", "second_plugin" });

        std::vector<std::string> callSequence;

        bus.subscribe<ScoreEvent>(
            "second_plugin", [&](const ScoreEvent& /*event*/) { callSequence.push_back("second_plugin"); });
        bus.subscribe<ScoreEvent>(
            "first_plugin", [&](const ScoreEvent& /*event*/) { callSequence.push_back("first_plugin"); });

        bus.publishDeferred(ScoreEvent{ 50 }, Phase::PreSimulation);
        EXPECT_TRUE(callSequence.empty());

        bus.processDeferredEvents(Phase::PreSimulation);

        const std::vector<std::string> expectedOrder{ "first_plugin", "second_plugin" };
        EXPECT_EQ(callSequence, expectedOrder);
    }

    TEST(EventBusTest, QueueCapacitySaturationDropsEventsAndIncrementsDroppedCount)
    {
        constexpr std::size_t kCapacity = 4;
        EventBusImpl bus(kCapacity);
        bus.setPluginLoadOrder({ "plugin_consumer" });

        std::vector<int> receivedPoints;
        bus.subscribe<ScoreEvent>(
            "plugin_consumer", [&](const ScoreEvent& event) { receivedPoints.push_back(event.points); });

        EXPECT_EQ(bus.droppedCount(), 0u);

        // Push 6 events into a queue of capacity 4
        for (int i = 1; i <= 6; ++i)
        {
            EXPECT_NO_THROW(bus.publishDeferred(ScoreEvent{ i * 10 }, Phase::PreSimulation));
        }

        // Invariant: events beyond capacity are dropped and droppedCount() incremented without throwing or crashing
        EXPECT_EQ(bus.droppedCount(), 2u);

        bus.processDeferredEvents(Phase::PreSimulation);

        // First 4 events were delivered
        const std::vector<int> expected{ 10, 20, 30, 40 };
        EXPECT_EQ(receivedPoints, expected);

        // After processing, queue has room again
        EXPECT_NO_THROW(bus.publishDeferred(ScoreEvent{ 70 }, Phase::PreSimulation));
        EXPECT_EQ(bus.droppedCount(), 2u); // Dropped count remains unchanged

        bus.processDeferredEvents(Phase::PreSimulation);
        EXPECT_EQ(receivedPoints.size(), 5u);
        EXPECT_EQ(receivedPoints.back(), 70);
    }

    TEST(EventBusTest, MultiplePhasesMaintainIndependentDeferredQueues)
    {
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "plugin_all" });

        std::vector<std::string> phaseLog;

        bus.subscribe<ScoreEvent>("plugin_all",
            [&](const ScoreEvent& event) { phaseLog.push_back("score_" + std::to_string(event.points)); });
        bus.subscribe<DamageEvent>("plugin_all",
            [&](const DamageEvent& event) { phaseLog.push_back("damage_" + std::to_string(event.targetEntityId)); });

        bus.publishDeferred(ScoreEvent{ 1 }, Phase::PreSimulation);
        bus.publishDeferred(DamageEvent{ 2, 1.0f }, Phase::PostSimulation);

        // Draining PreSimulation delivers only its own event
        bus.processDeferredEvents(Phase::PreSimulation);
        ASSERT_EQ(phaseLog.size(), 1u);
        EXPECT_EQ(phaseLog[0], "score_1");

        // PostSimulation event remains queued until its boundary
        bus.processDeferredEvents(Phase::PostSimulation);
        ASSERT_EQ(phaseLog.size(), 2u);
        EXPECT_EQ(phaseLog[1], "damage_2");
    }

    TEST(EventBusTest, UnsubscribeStopsDelivery)
    {
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "plugin_temp" });

        int callCount = 0;
        bus.subscribe<ScoreEvent>("plugin_temp", [&](const ScoreEvent& /*event*/) { ++callCount; });

        bus.publishImmediate(ScoreEvent{ 10 });
        EXPECT_EQ(callCount, 1);

        bus.unsubscribe(ScoreEvent::kEventId, "plugin_temp");

        bus.publishImmediate(ScoreEvent{ 20 });
        EXPECT_EQ(callCount, 1);
    }

    TEST(EventBusTest, RawPayloadPublishAndSubscribe)
    {
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "plugin_raw" });

        constexpr EventId kRawEventId = 9999;
        std::vector<std::uint8_t> receivedBytes;

        bus.subscribe(kRawEventId, "plugin_raw", [&](const void* data, std::size_t size) {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            receivedBytes.assign(bytes, bytes + size);
        });

        const std::vector<std::uint8_t> payload = { 0xAA, 0xBB, 0xCC, 0xDD };
        bus.publishImmediate(kRawEventId, payload.data(), payload.size());

        EXPECT_EQ(receivedBytes, payload);
    }

    TEST(EventBusTest, SubscriberExceptionContained)
    {
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "bad_plugin", "good_plugin" });

        bool goodCalled = false;

        bus.subscribe<ScoreEvent>(
            "bad_plugin", [](const ScoreEvent&) { throw std::runtime_error("Bad subscriber exploded"); });

        bus.subscribe<ScoreEvent>("good_plugin", [&goodCalled](const ScoreEvent& e) {
            if (e.points == 55)
            {
                goodCalled = true;
            }
        });

        // publishImmediate must contain the exception from bad_plugin and still deliver to good_plugin
        EXPECT_NO_THROW(bus.publishImmediate(ScoreEvent{ 55 }));
        EXPECT_TRUE(goodCalled);
    }

    TEST(EventBusTest, CircularRingBufferWrapsSeamlessly)
    {
        // Small capacity 4
        EventBusImpl bus(4);

        std::vector<int> receivedScores;
        bus.subscribe<ScoreEvent>(
            "collector", [&receivedScores](const ScoreEvent& e) { receivedScores.push_back(e.points); });

        // Run multiple cycles to wrap around head and tail pointers in ring buffer
        for (int cycle = 0; cycle < 5; ++cycle)
        {
            bus.publishDeferred(ScoreEvent{ cycle * 10 + 1 }, Phase::FixedStepPrePhysics);
            bus.publishDeferred(ScoreEvent{ cycle * 10 + 2 }, Phase::FixedStepPrePhysics);
            bus.publishDeferred(ScoreEvent{ cycle * 10 + 3 }, Phase::FixedStepPrePhysics);

            EXPECT_EQ(bus.droppedCount(), 0u);

            bus.processDeferredEvents(Phase::FixedStepPrePhysics);
        }

        EXPECT_EQ(receivedScores.size(), 15u);
        EXPECT_EQ(receivedScores[0], 1);
        EXPECT_EQ(receivedScores[1], 2);
        EXPECT_EQ(receivedScores[2], 3);
        EXPECT_EQ(receivedScores[12], 41);
        EXPECT_EQ(receivedScores[13], 42);
        EXPECT_EQ(receivedScores[14], 43);
    }
} // namespace
