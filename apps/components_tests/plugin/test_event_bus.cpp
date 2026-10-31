#include <components/plugin/event_bus.hpp>
#include <openeden/event_bus.hpp>
#include <openeden/game_events.hpp>
#include <openeden/phases.hpp>
#include <openeden/physics_events.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
    using openeden::plugin::EventBusImpl;
    using openeden::plugin::ForwardedFieldRecord;
    using openeden::sdk::BodyId;
    using openeden::sdk::EventBus;
    using openeden::sdk::EventId;
    using openeden::sdk::ForwardedEventDesc;
    using openeden::sdk::ForwardedField;
    using openeden::sdk::forwardedField;
    using openeden::sdk::ForwardedFieldType;
    using openeden::sdk::Phase;
    using openeden::sdk::PhysicsContactBeginEvent;
    using openeden::sdk::PhysicsContactEndEvent;
    using openeden::sdk::PhysicsHitEvent;

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
            bus.publishDeferred(ScoreEvent{ cycle * 10 + 1 }, Phase::Animation);
            bus.publishDeferred(ScoreEvent{ cycle * 10 + 2 }, Phase::Animation);
            bus.publishDeferred(ScoreEvent{ cycle * 10 + 3 }, Phase::Animation);

            EXPECT_EQ(bus.droppedCount(), 0u);

            bus.processDeferredEvents(Phase::Animation);
        }

        EXPECT_EQ(receivedScores.size(), 15u);
        EXPECT_EQ(receivedScores[0], 1);
        EXPECT_EQ(receivedScores[1], 2);
        EXPECT_EQ(receivedScores[2], 3);
        EXPECT_EQ(receivedScores[12], 41);
        EXPECT_EQ(receivedScores[13], 42);
        EXPECT_EQ(receivedScores[14], 43);
    }

    TEST(EventBusTest, EveryPhaseDeliversItsOwnDeferredEvents)
    {
        constexpr std::array<Phase, 6> phases{ Phase::Input, Phase::PreSimulation, Phase::PostSimulation,
            Phase::Animation, Phase::PreRender, Phase::Ui };

        EventBusImpl bus;
        std::vector<int> received;
        bus.subscribe<ScoreEvent>("collector", [&received](const ScoreEvent& e) { received.push_back(e.points); });

        for (std::size_t i = 0; i < phases.size(); ++i)
            bus.publishDeferred(ScoreEvent{ static_cast<int>(i) }, phases[i]);

        for (std::size_t i = phases.size(); i-- > 0;)
        {
            bus.processDeferredEvents(phases[i]);
            ASSERT_EQ(received.size(), phases.size() - i);
            EXPECT_EQ(received.back(), static_cast<int>(i));
        }
        EXPECT_EQ(bus.droppedCount(), 0u);
    }

    // ---- Events forwarded to Lua (decision 0003) -------------------------------------------------------------------

    struct AllTypesEvent
    {
        static constexpr EventId kEventId = 2001;

        float mFloat = 0.0f;
        double mDouble = 0.0;
        std::int32_t mInt = 0;
        std::uint32_t mUInt = 0;
        bool mBool = false;
        char mText[16] = {};
    };

    // Payload for the raw descriptors below: 16 bytes.
    constexpr std::size_t kRawPayloadSize = 16;
    constexpr EventId kRawEventId = 2002;

    ForwardedEventDesc rawDesc(std::span<const ForwardedField> fields, std::string_view name = "raw_event")
    {
        return ForwardedEventDesc{ kRawEventId, "plugin_raw", name, kRawPayloadSize, fields };
    }

    std::array<ForwardedField, 6> allTypesFields()
    {
        return { forwardedField("float_field", &AllTypesEvent::mFloat, offsetof(AllTypesEvent, mFloat)),
            forwardedField("double_field", &AllTypesEvent::mDouble, offsetof(AllTypesEvent, mDouble)),
            forwardedField("int_field", &AllTypesEvent::mInt, offsetof(AllTypesEvent, mInt)),
            forwardedField("uint_field", &AllTypesEvent::mUInt, offsetof(AllTypesEvent, mUInt)),
            forwardedField("bool_field", &AllTypesEvent::mBool, offsetof(AllTypesEvent, mBool)),
            forwardedField("text_field", &AllTypesEvent::mText, offsetof(AllTypesEvent, mText)) };
    }

    void expectField(const ForwardedFieldRecord& field, std::string_view name, ForwardedFieldType type,
        std::size_t offset, std::size_t size)
    {
        EXPECT_EQ(field.name, name);
        EXPECT_EQ(field.type, type);
        EXPECT_EQ(field.offset, offset);
        EXPECT_EQ(field.size, size);
    }

    void expectNothingMarked(const EventBusImpl& bus)
    {
        EXPECT_FALSE(bus.isForwardedToLua(kRawEventId));
        EXPECT_TRUE(bus.forwardedEvents().empty());
    }

    // Added by the implementer after review: the core reads a field by its type in task 0016.
    TEST(EventBusForwardingTest, FieldSizeWrongForItsTypeIsRejected)
    {
        using openeden::sdk::ForwardedEventDesc;
        using openeden::sdk::ForwardedField;
        using openeden::sdk::ForwardedFieldType;
        EventBusImpl bus;
        const ForwardedFieldType types[] = { ForwardedFieldType::Float32, ForwardedFieldType::Float64,
            ForwardedFieldType::Int32, ForwardedFieldType::UInt32, ForwardedFieldType::Boolean,
            ForwardedFieldType::String };
        for (const ForwardedFieldType type : types)
        {
            // Size 0 is wrong for every type, and 3 is wrong for every type but String.
            const ForwardedField empty[] = { { "f", type, 0, 0 } };
            EXPECT_FALSE(bus.markForwardedToLua(ForwardedEventDesc{ 900, "plugin", "event", 64, empty }));
            const ForwardedField three[] = { { "f", type, 0, 3 } };
            EXPECT_EQ(bus.markForwardedToLua(ForwardedEventDesc{ 901, "plugin", "event", 64, three }),
                type == ForwardedFieldType::String);
            if (type == ForwardedFieldType::String)
                EXPECT_TRUE(bus.isForwardedToLua(901));
        }
    }

    TEST(EventBusForwardingTest, EmptyPluginIdAndRepeatedPluginEventNameAreRejected)
    {
        using openeden::sdk::ForwardedEventDesc;
        EventBusImpl bus;
        EXPECT_FALSE(bus.markForwardedToLua(ForwardedEventDesc{ 910, "", "event", 8, {} }));
        EXPECT_TRUE(bus.markForwardedToLua(ForwardedEventDesc{ 911, "plugin", "event", 8, {} }));
        // Another event ID under the same Lua name would mix two payload layouts
        EXPECT_FALSE(bus.markForwardedToLua(ForwardedEventDesc{ 912, "plugin", "event", 8, {} }));
        EXPECT_TRUE(bus.markForwardedToLua(ForwardedEventDesc{ 913, "other", "event", 8, {} }));
    }

    TEST(EventBusForwardingTest, UnmarkedEventsAreNotForwarded)
    {
        EventBusImpl bus;
        EXPECT_FALSE(bus.isForwardedToLua(AllTypesEvent::kEventId));
        EXPECT_TRUE(bus.forwardedEvents().empty());

        bus.subscribe<AllTypesEvent>("plugin", [](const AllTypesEvent&) {});
        bus.publishImmediate(AllTypesEvent{});
        EXPECT_FALSE(bus.isForwardedToLua(AllTypesEvent::kEventId));
    }

    TEST(EventBusForwardingTest, TypedMarkRecordsEveryFieldType)
    {
        EventBusImpl bus;
        EventBus& pluginView = bus;
        const auto fields = allTypesFields();

        EXPECT_TRUE(pluginView.markForwardedToLua<AllTypesEvent>("plugin_score", "score_changed", fields));

        EXPECT_TRUE(bus.isForwardedToLua(AllTypesEvent::kEventId));
        ASSERT_EQ(bus.forwardedEvents().size(), 1u);
        ASSERT_EQ(bus.forwardedEvents().count(AllTypesEvent::kEventId), 1u);

        const auto& record = bus.forwardedEvents().at(AllTypesEvent::kEventId);
        EXPECT_EQ(record.pluginId, "plugin_score");
        EXPECT_EQ(record.name, "score_changed");
        EXPECT_EQ(record.payloadSize, sizeof(AllTypesEvent));

        ASSERT_EQ(record.fields.size(), 6u);
        expectField(record.fields[0], "float_field", ForwardedFieldType::Float32, offsetof(AllTypesEvent, mFloat),
            sizeof(float));
        expectField(record.fields[1], "double_field", ForwardedFieldType::Float64, offsetof(AllTypesEvent, mDouble),
            sizeof(double));
        expectField(record.fields[2], "int_field", ForwardedFieldType::Int32, offsetof(AllTypesEvent, mInt),
            sizeof(std::int32_t));
        expectField(record.fields[3], "uint_field", ForwardedFieldType::UInt32, offsetof(AllTypesEvent, mUInt),
            sizeof(std::uint32_t));
        expectField(
            record.fields[4], "bool_field", ForwardedFieldType::Boolean, offsetof(AllTypesEvent, mBool), sizeof(bool));
        expectField(record.fields[5], "text_field", ForwardedFieldType::String, offsetof(AllTypesEvent, mText),
            sizeof(AllTypesEvent::mText));
    }

    TEST(EventBusForwardingTest, RecordSurvivesTheCallersStringsAndFieldArray)
    {
        EventBusImpl bus;
        {
            std::string pluginId = "plugin_with_a_long_enough_id_to_be_on_the_heap";
            std::string name = "an_event_name_long_enough_to_be_on_the_heap_too";
            std::string fieldName = "a_field_name_long_enough_to_be_on_the_heap_too";
            std::vector<ForwardedField> fields{
                ForwardedField{ fieldName, ForwardedFieldType::Int32, 4, 4 },
            };

            EXPECT_TRUE(bus.markForwardedToLua(
                ForwardedEventDesc{ kRawEventId, pluginId, name, kRawPayloadSize, fields }));

            // Overwrite everything before it goes away, so a kept view would read the wrong text even if freed
            // memory still held the old bytes.
            pluginId.assign(pluginId.size(), 'x');
            name.assign(name.size(), 'x');
            fieldName.assign(fieldName.size(), 'x');
            fields[0] = ForwardedField{};
        }

        ASSERT_TRUE(bus.isForwardedToLua(kRawEventId));
        const auto& record = bus.forwardedEvents().at(kRawEventId);
        EXPECT_EQ(record.pluginId, "plugin_with_a_long_enough_id_to_be_on_the_heap");
        EXPECT_EQ(record.name, "an_event_name_long_enough_to_be_on_the_heap_too");
        EXPECT_EQ(record.payloadSize, kRawPayloadSize);
        ASSERT_EQ(record.fields.size(), 1u);
        expectField(record.fields[0], "a_field_name_long_enough_to_be_on_the_heap_too", ForwardedFieldType::Int32, 4, 4);
    }

    TEST(EventBusForwardingTest, EventWithNoFieldsIsAllowed)
    {
        EventBusImpl bus;
        EXPECT_TRUE(bus.markForwardedToLua(rawDesc({})));
        EXPECT_TRUE(bus.isForwardedToLua(kRawEventId));
        EXPECT_TRUE(bus.forwardedEvents().at(kRawEventId).fields.empty());
    }

    TEST(EventBusForwardingTest, FieldEndingExactlyAtThePayloadEndIsAllowed)
    {
        EventBusImpl bus;
        const std::array<ForwardedField, 1> fields{ ForwardedField{ "last", ForwardedFieldType::Float64, 8, 8 } };
        EXPECT_TRUE(bus.markForwardedToLua(rawDesc(fields)));
    }

    TEST(EventBusForwardingTest, EmptyEventNameIsRejected)
    {
        EventBusImpl bus;
        EXPECT_FALSE(bus.markForwardedToLua(rawDesc({}, "")));
        expectNothingMarked(bus);
    }

    TEST(EventBusForwardingTest, EmptyFieldNameIsRejected)
    {
        EventBusImpl bus;
        const std::array<ForwardedField, 2> fields{ ForwardedField{ "good", ForwardedFieldType::Int32, 0, 4 },
            ForwardedField{ "", ForwardedFieldType::Int32, 4, 4 } };
        EXPECT_FALSE(bus.markForwardedToLua(rawDesc(fields)));
        expectNothingMarked(bus);
    }

    TEST(EventBusForwardingTest, DuplicateFieldNamesAreRejected)
    {
        EventBusImpl bus;
        const std::array<ForwardedField, 3> fields{ ForwardedField{ "same", ForwardedFieldType::Int32, 0, 4 },
            ForwardedField{ "other", ForwardedFieldType::Int32, 4, 4 },
            ForwardedField{ "same", ForwardedFieldType::Int32, 8, 4 } };
        EXPECT_FALSE(bus.markForwardedToLua(rawDesc(fields)));
        expectNothingMarked(bus);
    }

    TEST(EventBusForwardingTest, FieldReachingPastThePayloadIsRejected)
    {
        EventBusImpl bus;

        const std::array<ForwardedField, 1> pastEnd{ ForwardedField{ "field", ForwardedFieldType::Float64, 12, 8 } };
        EXPECT_FALSE(bus.markForwardedToLua(rawDesc(pastEnd)));
        expectNothingMarked(bus);

        const std::array<ForwardedField, 1> startsPastEnd{ ForwardedField{
            "field", ForwardedFieldType::Int32, kRawPayloadSize + 4, 4 } };
        EXPECT_FALSE(bus.markForwardedToLua(rawDesc(startsPastEnd)));
        expectNothingMarked(bus);
    }

    TEST(EventBusForwardingTest, FieldWhoseOffsetPlusSizeOverflowsIsRejected)
    {
        // offset + size wraps around to a small number here, so a bounds check written as a sum would pass.
        constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
        EventBusImpl bus;

        const std::array<ForwardedField, 1> hugeOffset{ ForwardedField{ "field", ForwardedFieldType::Int32, kMax, 4 } };
        EXPECT_FALSE(bus.markForwardedToLua(rawDesc(hugeOffset)));
        expectNothingMarked(bus);

        const std::array<ForwardedField, 1> hugeSize{ ForwardedField{ "field", ForwardedFieldType::String, 4, kMax } };
        EXPECT_FALSE(bus.markForwardedToLua(rawDesc(hugeSize)));
        expectNothingMarked(bus);
    }

    TEST(EventBusForwardingTest, AlreadyMarkedEventKeepsTheFirstRecord)
    {
        EventBusImpl bus;
        const std::array<ForwardedField, 1> first{ ForwardedField{ "first", ForwardedFieldType::Int32, 0, 4 } };
        const std::array<ForwardedField, 1> second{ ForwardedField{ "second", ForwardedFieldType::Float32, 4, 4 } };

        EXPECT_TRUE(bus.markForwardedToLua(
            ForwardedEventDesc{ kRawEventId, "plugin_first", "first_name", kRawPayloadSize, first }));
        EXPECT_FALSE(bus.markForwardedToLua(
            ForwardedEventDesc{ kRawEventId, "plugin_second", "second_name", kRawPayloadSize, second }));

        EXPECT_TRUE(bus.isForwardedToLua(kRawEventId));
        ASSERT_EQ(bus.forwardedEvents().size(), 1u);
        const auto& record = bus.forwardedEvents().at(kRawEventId);
        EXPECT_EQ(record.pluginId, "plugin_first");
        EXPECT_EQ(record.name, "first_name");
        ASSERT_EQ(record.fields.size(), 1u);
        expectField(record.fields[0], "first", ForwardedFieldType::Int32, 0, 4);
    }

    TEST(EventBusForwardingTest, RecordsAreSortedByEventId)
    {
        EventBusImpl bus;
        for (const EventId id : { EventId{ 30 }, EventId{ 10 }, EventId{ 20 } })
            EXPECT_TRUE(bus.markForwardedToLua(ForwardedEventDesc{ id, "plugin", std::to_string(id), 0, {} }));

        std::vector<EventId> order;
        for (const auto& [id, record] : bus.forwardedEvents())
            order.push_back(id);
        EXPECT_EQ(order, (std::vector<EventId>{ 10, 20, 30 }));
    }

    TEST(EventBusForwardingTest, MarkingDoesNotChangeDeliveryToSubscribers)
    {
        EventBusImpl bus;
        EventBus& pluginView = bus;
        bus.setPluginLoadOrder({ "plugin_a", "plugin_b" });

        std::vector<std::string> log;
        // One subscriber from before the mark, one from after
        bus.subscribe<AllTypesEvent>("plugin_b", [&log](const AllTypesEvent& e) {
            log.push_back("b" + std::to_string(e.mInt));
        });
        const auto fields = allTypesFields();
        ASSERT_TRUE(pluginView.markForwardedToLua<AllTypesEvent>("plugin_a", "all_types", fields));
        bus.subscribe<AllTypesEvent>("plugin_a", [&log](const AllTypesEvent& e) {
            log.push_back("a" + std::to_string(e.mInt));
        });

        AllTypesEvent event;
        event.mInt = 1;
        bus.publishImmediate(event);
        EXPECT_EQ(log, (std::vector<std::string>{ "a1", "b1" }));

        log.clear();
        event.mInt = 2;
        bus.publishDeferred(event, Phase::PreRender);
        EXPECT_TRUE(log.empty());
        bus.processDeferredEvents(Phase::PreRender);
        EXPECT_EQ(log, (std::vector<std::string>{ "a2", "b2" }));
        EXPECT_EQ(bus.droppedCount(), 0u);
    }

    // ---- Physics event declarations (decision 0003; the engine publishes them in task 0017) ------------------------

    static_assert(std::is_trivially_copyable_v<PhysicsHitEvent>);
    static_assert(std::is_trivially_copyable_v<PhysicsContactBeginEvent>);
    static_assert(std::is_trivially_copyable_v<PhysicsContactEndEvent>);
    static_assert(PhysicsHitEvent::kEventId != PhysicsContactBeginEvent::kEventId);
    static_assert(PhysicsHitEvent::kEventId != PhysicsContactEndEvent::kEventId);
    static_assert(PhysicsContactBeginEvent::kEventId != PhysicsContactEndEvent::kEventId);

    TEST(EventBusPhysicsEventsTest, HitEventTravelsThroughTheBus)
    {
        EventBusImpl bus;
        std::vector<PhysicsHitEvent> received;
        bus.subscribe<PhysicsHitEvent>("plugin", [&received](const PhysicsHitEvent& e) { received.push_back(e); });

        PhysicsHitEvent event;
        event.body = BodyId{ 1, 2 };
        event.other = BodyId{ 3, 4 };
        event.pointM = { 1.0f, 2.0f, 3.0f };
        event.normal = { 0.0f, 0.0f, 1.0f };

        bus.publishImmediate(event);
        ASSERT_EQ(received.size(), 1u);

        // The header promises deferred delivery in a frame phase
        bus.publishDeferred(event, Phase::PostSimulation);
        EXPECT_EQ(received.size(), 1u);
        bus.processDeferredEvents(Phase::PostSimulation);
        ASSERT_EQ(received.size(), 2u);

        for (const PhysicsHitEvent& e : received)
        {
            EXPECT_EQ(e.body, event.body);
            EXPECT_EQ(e.other, event.other);
            EXPECT_EQ(e.pointM, event.pointM);
            EXPECT_EQ(e.normal, event.normal);
        }
    }

    TEST(EventBusPhysicsEventsTest, ContactEventsTravelThroughTheBus)
    {
        EventBusImpl bus;
        std::vector<PhysicsContactBeginEvent> begins;
        std::vector<PhysicsContactEndEvent> ends;
        bus.subscribe<PhysicsContactBeginEvent>(
            "plugin", [&begins](const PhysicsContactBeginEvent& e) { begins.push_back(e); });
        bus.subscribe<PhysicsContactEndEvent>(
            "plugin", [&ends](const PhysicsContactEndEvent& e) { ends.push_back(e); });

        PhysicsContactBeginEvent begin;
        begin.bodyA = BodyId{ 5, 6 };
        begin.bodyB = BodyId{ 7, 8 };
        begin.pointM = { 4.0f, 5.0f, 6.0f };
        begin.normal = { 1.0f, 0.0f, 0.0f };
        bus.publishDeferred(begin, Phase::PostSimulation);

        PhysicsContactEndEvent end;
        end.bodyA = BodyId{ 5, 6 };
        end.bodyB = BodyId{ 7, 8 };
        bus.publishDeferred(end, Phase::PostSimulation);

        bus.processDeferredEvents(Phase::PostSimulation);

        ASSERT_EQ(begins.size(), 1u);
        EXPECT_EQ(begins[0].bodyA, begin.bodyA);
        EXPECT_EQ(begins[0].bodyB, begin.bodyB);
        EXPECT_EQ(begins[0].pointM, begin.pointM);
        EXPECT_EQ(begins[0].normal, begin.normal);

        ASSERT_EQ(ends.size(), 1u);
        EXPECT_EQ(ends[0].bodyA, end.bodyA);
        EXPECT_EQ(ends[0].bodyB, end.bodyB);
    }

    // ---- decodeForwardedEvent (task 0016) ---------------------------------------------------------------------------

    using openeden::plugin::decodeForwardedEvent;
    using openeden::plugin::DecodedField;
    using openeden::plugin::ForwardedEventRecord;

    ForwardedEventRecord allTypesRecord()
    {
        EventBusImpl bus;
        EventBus& pluginView = bus;
        const auto fields = allTypesFields();
        EXPECT_TRUE(pluginView.markForwardedToLua<AllTypesEvent>("plugin", "all_types", fields));
        return bus.forwardedEvents().at(AllTypesEvent::kEventId);
    }

    ForwardedEventRecord singleFieldRecord(ForwardedFieldType type, std::size_t offset, std::size_t size)
    {
        ForwardedEventRecord record;
        record.pluginId = "plugin";
        record.name = "single";
        record.payloadSize = offset + size;
        record.fields.push_back(ForwardedFieldRecord{ "field", type, offset, size });
        return record;
    }

    // The field by name, so these tests do not depend on the order of out.
    const DecodedField* findDecoded(const std::vector<DecodedField>& fields, std::string_view name)
    {
        for (const DecodedField& field : fields)
            if (field.name == name)
                return &field;
        return nullptr;
    }

    template <typename T>
    void expectDecoded(const std::vector<DecodedField>& fields, std::string_view name, const T& expected)
    {
        const DecodedField* const field = findDecoded(fields, name);
        ASSERT_NE(field, nullptr) << name;
        ASSERT_TRUE(std::holds_alternative<T>(field->value)) << name;
        EXPECT_EQ(std::get<T>(field->value), expected) << name;
    }

    TEST(DecodeForwardedEventTest, EveryFieldTypeIsDecoded)
    {
        AllTypesEvent event;
        event.mFloat = 1.5f;
        event.mDouble = 0.1;
        event.mInt = 42;
        event.mUInt = 7;
        event.mBool = true;
        std::memcpy(event.mText, "hello", 6);

        std::vector<DecodedField> out;
        std::string error;
        ASSERT_TRUE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error)) << error;

        EXPECT_EQ(out.size(), 6u);
        expectDecoded<double>(out, "float_field", 1.5);
        expectDecoded<double>(out, "double_field", 0.1);
        expectDecoded<double>(out, "int_field", 42.0);
        expectDecoded<double>(out, "uint_field", 7.0);
        expectDecoded<bool>(out, "bool_field", true);
        expectDecoded<std::string>(out, "text_field", "hello");
    }

    TEST(DecodeForwardedEventTest, FloatBecomesTheExactSameValueAsADouble)
    {
        AllTypesEvent event;
        event.mFloat = 0.1f;

        std::vector<DecodedField> out;
        std::string error;
        ASSERT_TRUE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error)) << error;

        expectDecoded<double>(out, "float_field", static_cast<double>(0.1f));
    }

    TEST(DecodeForwardedEventTest, FalseBooleanIsABooleanNotANumber)
    {
        AllTypesEvent event;
        event.mBool = false;

        std::vector<DecodedField> out;
        std::string error;
        ASSERT_TRUE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error)) << error;

        expectDecoded<bool>(out, "bool_field", false);
    }

    TEST(DecodeForwardedEventTest, NegativeIntsKeepTheirSign)
    {
        for (const std::int32_t value : { -1, -7, std::numeric_limits<std::int32_t>::min() })
        {
            AllTypesEvent event;
            event.mInt = value;

            std::vector<DecodedField> out;
            std::string error;
            ASSERT_TRUE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error)) << error;

            expectDecoded<double>(out, "int_field", static_cast<double>(value));
        }
    }

    TEST(DecodeForwardedEventTest, UInt32AboveIntMaxIsAPositiveDouble)
    {
        for (const std::uint32_t value : { 2147483648u, 3000000000u, std::numeric_limits<std::uint32_t>::max() })
        {
            AllTypesEvent event;
            event.mUInt = value;

            std::vector<DecodedField> out;
            std::string error;
            ASSERT_TRUE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error)) << error;

            expectDecoded<double>(out, "uint_field", static_cast<double>(value));
        }
    }

    TEST(DecodeForwardedEventTest, StringAtFullSizeWithNoNulEndsAtItsSize)
    {
        AllTypesEvent event;
        std::memcpy(event.mText, "abcdefghijklmnop", 16); // Exactly sizeof(mText), so no terminator

        std::vector<DecodedField> out;
        std::string error;
        ASSERT_TRUE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error)) << error;

        expectDecoded<std::string>(out, "text_field", "abcdefghijklmnop");
    }

    TEST(DecodeForwardedEventTest, StringEndsAtItsFirstNul)
    {
        AllTypesEvent event;
        std::memcpy(event.mText, "hi\0garbage", 10);

        std::vector<DecodedField> out;
        std::string error;
        ASSERT_TRUE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error)) << error;

        expectDecoded<std::string>(out, "text_field", "hi");
    }

    TEST(DecodeForwardedEventTest, AllNulStringIsEmpty)
    {
        const AllTypesEvent event;

        std::vector<DecodedField> out;
        std::string error;
        ASSERT_TRUE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error)) << error;

        expectDecoded<std::string>(out, "text_field", "");
    }

    TEST(DecodeForwardedEventTest, ValidMultiByteUtf8IsAccepted)
    {
        AllTypesEvent event;
        std::memcpy(event.mText, "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80", 14); // e-acute, euro sign, an emoji

        std::vector<DecodedField> out;
        std::string error;
        ASSERT_TRUE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error)) << error;

        expectDecoded<std::string>(out, "text_field", "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80");
    }

    TEST(DecodeForwardedEventTest, FieldsAtUnalignedOffsetsAreRead)
    {
        // The record gives any offset, so a read must not assume alignment. Run under the UBSan gate too.
        std::array<std::uint8_t, 1 + sizeof(std::int32_t)> intPayload{};
        const std::int32_t intValue = -5;
        std::memcpy(intPayload.data() + 1, &intValue, sizeof(intValue));
        std::vector<DecodedField> out;
        std::string error;
        ASSERT_TRUE(decodeForwardedEvent(singleFieldRecord(ForwardedFieldType::Int32, 1, sizeof(std::int32_t)),
            intPayload.data(), intPayload.size(), out, error))
            << error;
        expectDecoded<double>(out, "field", -5.0);

        std::array<std::uint8_t, 3 + sizeof(double)> doublePayload{};
        const double doubleValue = 2.5;
        std::memcpy(doublePayload.data() + 3, &doubleValue, sizeof(doubleValue));
        std::vector<DecodedField> doubleOut;
        ASSERT_TRUE(decodeForwardedEvent(singleFieldRecord(ForwardedFieldType::Float64, 3, sizeof(double)),
            doublePayload.data(), doublePayload.size(), doubleOut, error))
            << error;
        expectDecoded<double>(doubleOut, "field", 2.5);
    }

    TEST(DecodeForwardedEventTest, RecordWithNoFieldsDecodesToNothing)
    {
        ForwardedEventRecord record;
        record.pluginId = "plugin";
        record.name = "empty";

        std::vector<DecodedField> out;
        std::string error;
        EXPECT_TRUE(decodeForwardedEvent(record, nullptr, 0, out, error)) << error;
        EXPECT_TRUE(out.empty());
    }

    TEST(DecodeForwardedEventTest, ShortPayloadFailsWithAReason)
    {
        const AllTypesEvent event;
        const ForwardedEventRecord record = allTypesRecord();

        for (const std::size_t size : { sizeof(AllTypesEvent) - 1, sizeof(std::int32_t), std::size_t{ 0 } })
        {
            std::vector<DecodedField> out;
            std::string error;
            EXPECT_FALSE(decodeForwardedEvent(record, &event, size, out, error)) << "size " << size;
            EXPECT_FALSE(error.empty()) << "size " << size;
        }
    }

    TEST(DecodeForwardedEventTest, InvalidUtf8FailsWithAReason)
    {
        const std::vector<std::string> invalid{
            "\xFF\xFE", // Bytes that never appear in UTF-8
            "a\x80", // Continuation byte with no lead
            "\xC3", // Lead byte cut off by the end of the string
            "\xE2\x82", // Three-byte form cut off
            "\xC0\xAF", // Overlong encoding of '/'
            "\xED\xA0\x80", // UTF-16 surrogate
            "\xF4\x90\x80\x80", // Above U+10FFFF
        };
        const ForwardedEventRecord record = allTypesRecord();

        for (const std::string& text : invalid)
        {
            AllTypesEvent event;
            std::memcpy(event.mText, text.data(), text.size());

            std::vector<DecodedField> out;
            std::string error;
            EXPECT_FALSE(decodeForwardedEvent(record, &event, sizeof(event), out, error));
            EXPECT_FALSE(error.empty());
        }
    }

    TEST(DecodeForwardedEventTest, MultiByteCharacterCutByTheFieldEndIsInvalid)
    {
        // The field's last byte starts a two-byte character whose second byte is the next field's
        AllTypesEvent event;
        std::memset(event.mText, 'a', sizeof(event.mText));
        event.mText[sizeof(event.mText) - 1] = '\xC3';

        std::vector<DecodedField> out;
        std::string error;
        EXPECT_FALSE(decodeForwardedEvent(allTypesRecord(), &event, sizeof(event), out, error));
        EXPECT_FALSE(error.empty());
    }

    // ---- setForwardSink (task 0016) ---------------------------------------------------------------------------------

    struct SinkEntry
    {
        std::string pluginId;
        std::string name;
        std::vector<std::uint8_t> bytes;
    };

    openeden::plugin::ForwardSink loggingSink(std::vector<SinkEntry>& log)
    {
        return [&log](const ForwardedEventRecord& record, const void* data, std::size_t size) {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            log.push_back(SinkEntry{ record.pluginId, record.name, std::vector<std::uint8_t>(bytes, bytes + size) });
        };
    }

    int pointsIn(const SinkEntry& entry)
    {
        ScoreEvent event;
        EXPECT_EQ(entry.bytes.size(), sizeof(event));
        if (entry.bytes.size() == sizeof(event))
            std::memcpy(&event, entry.bytes.data(), sizeof(event));
        return event.points;
    }

    std::vector<int> pointsIn(const std::vector<SinkEntry>& log)
    {
        std::vector<int> result;
        for (const SinkEntry& entry : log)
            result.push_back(pointsIn(entry));
        return result;
    }

    void markScoreEvent(EventBusImpl& bus)
    {
        EventBus& pluginView = bus;
        const std::array<ForwardedField, 1> fields{ forwardedField(
            "points", &ScoreEvent::points, offsetof(ScoreEvent, points)) };
        ASSERT_TRUE(pluginView.markForwardedToLua<ScoreEvent>("plugin_score", "score_changed", fields));
    }

    TEST(EventBusForwardSinkTest, MarkedImmediateEventReachesTheSinkWithNoSubscribers)
    {
        EventBusImpl bus;
        markScoreEvent(bus);
        std::vector<SinkEntry> log;
        bus.setForwardSink(loggingSink(log));

        bus.publishImmediate(ScoreEvent{ 7 });

        ASSERT_EQ(log.size(), 1u);
        EXPECT_EQ(log[0].pluginId, "plugin_score");
        EXPECT_EQ(log[0].name, "score_changed");
        EXPECT_EQ(pointsIn(log[0]), 7);
    }

    TEST(EventBusForwardSinkTest, DeferredEventReachesTheSinkWhenItsPhaseIsProcessed)
    {
        EventBusImpl bus;
        markScoreEvent(bus);
        std::vector<SinkEntry> log;
        bus.setForwardSink(loggingSink(log));

        bus.publishDeferred(ScoreEvent{ 7 }, Phase::PreRender);
        EXPECT_TRUE(log.empty());

        bus.processDeferredEvents(Phase::Input);
        EXPECT_TRUE(log.empty());

        bus.processDeferredEvents(Phase::PreRender);
        ASSERT_EQ(log.size(), 1u);
        EXPECT_EQ(pointsIn(log[0]), 7);

        // Delivered once
        bus.processDeferredEvents(Phase::PreRender);
        EXPECT_EQ(log.size(), 1u);
    }

    TEST(EventBusForwardSinkTest, EventsReachTheSinkInPublishOrder)
    {
        EventBusImpl bus;
        markScoreEvent(bus);
        std::vector<SinkEntry> log;
        bus.setForwardSink(loggingSink(log));

        bus.publishImmediate(ScoreEvent{ 1 });
        bus.publishImmediate(ScoreEvent{ 2 });
        bus.publishDeferred(ScoreEvent{ 3 }, Phase::Animation);
        bus.publishDeferred(ScoreEvent{ 4 }, Phase::Animation);
        bus.publishDeferred(ScoreEvent{ 5 }, Phase::Animation);
        bus.processDeferredEvents(Phase::Animation);

        EXPECT_EQ(pointsIn(log), (std::vector<int>{ 1, 2, 3, 4, 5 }));
    }

    TEST(EventBusForwardSinkTest, UnmarkedEventsNeverReachTheSink)
    {
        EventBusImpl bus;
        markScoreEvent(bus);
        std::vector<SinkEntry> log;
        bus.setForwardSink(loggingSink(log));

        int subscriberCalls = 0;
        bus.subscribe<DamageEvent>("plugin", [&](const DamageEvent&) { ++subscriberCalls; });

        bus.publishImmediate(DamageEvent{ 1, 2.0f });
        bus.publishDeferred(DamageEvent{ 3, 4.0f }, Phase::Ui);
        bus.processDeferredEvents(Phase::Ui);

        EXPECT_EQ(subscriberCalls, 2);
        EXPECT_TRUE(log.empty());
    }

    TEST(EventBusForwardSinkTest, NullSinkStopsForwardingAndPublishingStillWorks)
    {
        EventBusImpl bus;
        markScoreEvent(bus);
        std::vector<SinkEntry> log;
        bus.setForwardSink(loggingSink(log));
        bus.publishImmediate(ScoreEvent{ 1 });
        bus.publishDeferred(ScoreEvent{ 2 }, Phase::Input);
        ASSERT_EQ(log.size(), 1u);

        int subscriberCalls = 0;
        bus.subscribe<ScoreEvent>("plugin", [&](const ScoreEvent&) { ++subscriberCalls; });
        bus.setForwardSink(nullptr);

        EXPECT_NO_THROW(bus.publishImmediate(ScoreEvent{ 3 }));
        // Queued while the sink was set, delivered after it was removed: nobody to give it to
        EXPECT_NO_THROW(bus.processDeferredEvents(Phase::Input));

        EXPECT_EQ(log.size(), 1u);
        EXPECT_EQ(subscriberCalls, 2);
    }

    TEST(EventBusForwardSinkTest, NoSinkEverSetIsFineForMarkedEvents)
    {
        EventBusImpl bus;
        markScoreEvent(bus);
        int subscriberCalls = 0;
        bus.subscribe<ScoreEvent>("plugin", [&](const ScoreEvent&) { ++subscriberCalls; });

        EXPECT_NO_THROW(bus.publishImmediate(ScoreEvent{ 1 }));
        EXPECT_EQ(subscriberCalls, 1);
    }

    TEST(EventBusForwardSinkTest, SubscribersStillGetMarkedEvents)
    {
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "plugin_a", "plugin_b" });
        markScoreEvent(bus);
        std::vector<SinkEntry> log;
        bus.setForwardSink(loggingSink(log));

        std::vector<std::string> calls;
        bus.subscribe<ScoreEvent>(
            "plugin_b", [&](const ScoreEvent& e) { calls.push_back("b" + std::to_string(e.points)); });
        bus.subscribe<ScoreEvent>(
            "plugin_a", [&](const ScoreEvent& e) { calls.push_back("a" + std::to_string(e.points)); });

        bus.publishImmediate(ScoreEvent{ 1 });
        bus.publishDeferred(ScoreEvent{ 2 }, Phase::Ui);
        bus.processDeferredEvents(Phase::Ui);

        EXPECT_EQ(calls, (std::vector<std::string>{ "a1", "b1", "a2", "b2" }));
        EXPECT_EQ(pointsIn(log), (std::vector<int>{ 1, 2 }));
    }

    TEST(EventBusForwardSinkTest, DeferredPayloadLargerThanTheInlineBufferArrivesWhole)
    {
        // Larger than EventBusImpl::kInlinePayloadCapacity (256), so the queue keeps it on the heap
        constexpr EventId kLargeEventId = 2003;
        std::vector<std::uint8_t> payload(300);
        for (std::size_t i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<std::uint8_t>(i);

        EventBusImpl bus;
        ASSERT_TRUE(bus.markForwardedToLua(ForwardedEventDesc{ kLargeEventId, "plugin", "large", payload.size(), {} }));
        std::vector<SinkEntry> log;
        bus.setForwardSink(loggingSink(log));

        bus.publishDeferred(kLargeEventId, payload.data(), payload.size(), Phase::Ui);
        bus.processDeferredEvents(Phase::Ui);

        ASSERT_EQ(log.size(), 1u);
        EXPECT_EQ(log[0].bytes, payload);
    }

    TEST(EventBusForwardSinkTest, ThrowingSinkDoesNotReachThePublisherAndSubscribersStillRun)
    {
        // Root CLAUDE.md: no exception crosses the core/plugin boundary in either direction. The publisher is a plugin.
        EventBusImpl bus;
        bus.setPluginLoadOrder({ "plugin_a", "plugin_b" });
        markScoreEvent(bus);
        int sinkCalls = 0;
        bus.setForwardSink([&](const ForwardedEventRecord&, const void*, std::size_t) {
            ++sinkCalls;
            throw std::runtime_error("sink failed");
        });

        std::vector<std::string> calls;
        bus.subscribe<ScoreEvent>("plugin_a", [&](const ScoreEvent&) { calls.push_back("a"); });
        bus.subscribe<ScoreEvent>("plugin_b", [&](const ScoreEvent&) { calls.push_back("b"); });

        EXPECT_NO_THROW(bus.publishImmediate(ScoreEvent{ 1 }));

        EXPECT_EQ(sinkCalls, 1);
        EXPECT_EQ(calls, (std::vector<std::string>{ "a", "b" }));
    }

    TEST(EventBusForwardSinkTest, ThrowingSinkDoesNotLoseDeferredEvents)
    {
        EventBusImpl bus;
        markScoreEvent(bus);
        int sinkCalls = 0;
        bus.setForwardSink([&](const ForwardedEventRecord&, const void*, std::size_t) {
            ++sinkCalls;
            throw std::runtime_error("sink failed");
        });

        std::vector<int> received;
        bus.subscribe<ScoreEvent>("plugin", [&](const ScoreEvent& e) { received.push_back(e.points); });

        bus.publishDeferred(ScoreEvent{ 1 }, Phase::Ui);
        bus.publishDeferred(ScoreEvent{ 2 }, Phase::Ui);
        bus.publishDeferred(ScoreEvent{ 3 }, Phase::Ui);
        EXPECT_NO_THROW(bus.processDeferredEvents(Phase::Ui));

        EXPECT_EQ(sinkCalls, 3);
        EXPECT_EQ(received, (std::vector<int>{ 1, 2, 3 }));

        // The queue is drained: nothing is left to deliver again
        EXPECT_NO_THROW(bus.processDeferredEvents(Phase::Ui));
        EXPECT_EQ(received.size(), 3u);
    }

    TEST(EventBusForwardSinkTest, ThrowingSubscriberDoesNotStopTheSink)
    {
        EventBusImpl bus;
        markScoreEvent(bus);
        std::vector<SinkEntry> log;
        bus.setForwardSink(loggingSink(log));
        bus.subscribe<ScoreEvent>("plugin", [](const ScoreEvent&) { throw std::runtime_error("subscriber failed"); });

        EXPECT_NO_THROW(bus.publishImmediate(ScoreEvent{ 1 }));

        EXPECT_EQ(pointsIn(log), (std::vector<int>{ 1 }));
    }

    // Task 0017: the game events.
    using openeden::sdk::ActorHitEvent;
    using openeden::sdk::CellChangedEvent;
    using openeden::sdk::CellLoadedEvent;
    using openeden::sdk::CellUnloadedEvent;
    using openeden::sdk::EntityId;
    using openeden::sdk::EntityLifecycleEvent;
    using openeden::sdk::EntityLifecycleNotice;
    using openeden::sdk::GameLoadedEvent;
    using openeden::sdk::GameSavedEvent;
    using openeden::sdk::kEventNameCapacity;

    static_assert(std::is_trivially_copyable_v<GameLoadedEvent>);
    static_assert(std::is_trivially_copyable_v<GameSavedEvent>);
    static_assert(std::is_trivially_copyable_v<CellChangedEvent>);
    static_assert(std::is_trivially_copyable_v<CellLoadedEvent>);
    static_assert(std::is_trivially_copyable_v<CellUnloadedEvent>);
    static_assert(std::is_trivially_copyable_v<ActorHitEvent>);
    static_assert(std::is_trivially_copyable_v<EntityLifecycleNotice>);

    constexpr std::array kWorldEventIds{ GameLoadedEvent::kEventId, GameSavedEvent::kEventId,
        CellChangedEvent::kEventId, CellLoadedEvent::kEventId, CellUnloadedEvent::kEventId, ActorHitEvent::kEventId,
        EntityLifecycleNotice::kEventId, PhysicsHitEvent::kEventId, PhysicsContactBeginEvent::kEventId,
        PhysicsContactEndEvent::kEventId };

    constexpr bool worldEventIdsAreDistinct()
    {
        for (std::size_t i = 0; i < kWorldEventIds.size(); ++i)
            for (std::size_t j = i + 1; j < kWorldEventIds.size(); ++j)
                if (kWorldEventIds[i] == kWorldEventIds[j])
                    return false;
        return true;
    }

    static_assert(worldEventIdsAreDistinct());

    /// Publishes the event in both ways and returns the copies a subscriber received.
    template <typename T>
    std::vector<T> receivedBothWays(const T& event)
    {
        EventBusImpl bus;
        std::vector<T> received;
        bus.subscribe<T>("plugin", [&](const T& got) { received.push_back(got); });

        bus.publishImmediate(event);
        EXPECT_EQ(received.size(), 1u);

        // game_events.hpp: delivered at the start of Phase::Input.
        bus.publishDeferred(event, Phase::Input);
        EXPECT_EQ(received.size(), 1u);
        bus.processDeferredEvents(Phase::Input);
        EXPECT_EQ(received.size(), 2u);
        return received;
    }

    template <typename T>
    void expectCellNameArrives()
    {
        constexpr std::string_view name = "Museum of Wonders";
        T event;
        std::memcpy(event.cellName, name.data(), name.size());
        for (const T& got : receivedBothWays(event))
            EXPECT_EQ(std::string_view(got.cellName), name);
    }

    TEST(GameEventsTest, GameLoadedArrives)
    {
        EXPECT_EQ(receivedBothWays(GameLoadedEvent{}).size(), 2u);
    }

    TEST(GameEventsTest, GameSavedArrives)
    {
        EXPECT_EQ(receivedBothWays(GameSavedEvent{}).size(), 2u);
    }

    TEST(GameEventsTest, CellChangedArrivesWithItsName)
    {
        expectCellNameArrives<CellChangedEvent>();
    }

    TEST(GameEventsTest, CellLoadedArrivesWithItsName)
    {
        expectCellNameArrives<CellLoadedEvent>();
    }

    TEST(GameEventsTest, CellUnloadedArrivesWithItsName)
    {
        expectCellNameArrives<CellUnloadedEvent>();
    }

    TEST(GameEventsTest, ActorHitArrivesWithItsPayload)
    {
        ActorHitEvent event;
        event.attacker = EntityId{ 1, 2 };
        event.target = EntityId{ 3, 4 };
        event.damage = 1.5f;
        event.successful = true;
        for (const ActorHitEvent& got : receivedBothWays(event))
        {
            EXPECT_EQ(got.attacker, event.attacker);
            EXPECT_EQ(got.target, event.target);
            EXPECT_EQ(got.damage, event.damage);
            EXPECT_TRUE(got.successful);
        }
    }

    TEST(GameEventsTest, LifecycleNoticeArrivesWithItsPayload)
    {
        EntityLifecycleNotice event;
        event.kind = EntityLifecycleEvent::Revived;
        event.entity = EntityId{ 5, 6 };
        for (const EntityLifecycleNotice& got : receivedBothWays(event))
        {
            EXPECT_EQ(got.kind, EntityLifecycleEvent::Revived);
            EXPECT_EQ(got.entity, event.entity);
        }
    }

    TEST(GameEventsTest, NameCapacityLeavesRoomForLongCellNames)
    {
        // Text is cut to fit, never overflowing: the array has the published capacity.
        EXPECT_EQ(sizeof(CellChangedEvent::cellName), kEventNameCapacity);
        EXPECT_EQ(sizeof(CellLoadedEvent::cellName), kEventNameCapacity);
        EXPECT_EQ(sizeof(CellUnloadedEvent::cellName), kEventNameCapacity);
    }
} // namespace
