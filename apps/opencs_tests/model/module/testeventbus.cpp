// Task 0091, stage b: Studio's editor event bus and the selection value it publishes. Written from the spec alone,
// against a stub that did nothing.
//
// Rules pinned: handlers run in registration order across owners; the event arrives as published; a handler that
// throws is removed and reported once (owner and text) and the others still run; an event published from a handler
// is queued behind the running dispatch (first in, first out); a handler removed (or its owner removed) during a
// dispatch is not called afterwards; a handler may unsubscribe itself; the queue is capped and counts drops; the
// selection publishes only when it changes.
#include "apps/opencs/model/module/eventbus.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace CSMModule;
    using namespace openeden::editor;

    EditorEvent event(EditorEventType type, std::string subject = {}, std::string detail = {}, std::uint64_t step = 0)
    {
        EditorEvent e;
        e.type = type;
        e.subject = std::move(subject);
        e.detail = std::move(detail);
        e.step = step;
        return e;
    }

    TEST(CSMEventBus, handlersRunInRegistrationOrderAcrossOwners)
    {
        EventBus bus;
        std::vector<std::string> log;
        bus.subscribe("a", [&](const EditorEvent&) { log.push_back("a1"); });
        bus.subscribe("b", [&](const EditorEvent&) { log.push_back("b1"); });
        bus.subscribe("a", [&](const EditorEvent&) { log.push_back("a2"); });
        bus.publish(event(EditorEventType::AssetOpened, "x"));
        EXPECT_EQ(log, (std::vector<std::string>{ "a1", "b1", "a2" }));
        EXPECT_EQ(bus.subscriberCount(), 3u);
    }

    TEST(CSMEventBus, theEventArrivesAsPublished)
    {
        EventBus bus;
        EditorEvent seen;
        bus.subscribe("a", [&](const EditorEvent& e) { seen = e; });
        bus.publish(event(EditorEventType::SimulationStepped, "s", "d", 42));
        EXPECT_EQ(seen.type, EditorEventType::SimulationStepped);
        EXPECT_EQ(seen.subject, "s");
        EXPECT_EQ(seen.detail, "d");
        EXPECT_EQ(seen.step, 42u);
    }

    TEST(CSMEventBus, subscriptionIdsAreNonZeroAndDistinctAndUnsubscribeStopsDelivery)
    {
        EventBus bus;
        int calls = 0;
        const std::uint64_t first = bus.subscribe("a", [&](const EditorEvent&) { ++calls; });
        const std::uint64_t second = bus.subscribe("a", [&](const EditorEvent&) { ++calls; });
        EXPECT_NE(first, 0u);
        EXPECT_NE(second, 0u);
        EXPECT_NE(first, second);
        bus.unsubscribe(first);
        bus.unsubscribe(9999); // unknown: nothing happens
        bus.publish(event(EditorEventType::AssetOpened));
        EXPECT_EQ(calls, 1);
    }

    TEST(CSMEventBus, removeOwnerRemovesOnlyThatOwnersHandlers)
    {
        EventBus bus;
        std::vector<std::string> log;
        bus.subscribe("a", [&](const EditorEvent&) { log.push_back("a"); });
        bus.subscribe("b", [&](const EditorEvent&) { log.push_back("b"); });
        bus.removeOwner("a");
        bus.publish(event(EditorEventType::AssetOpened));
        EXPECT_EQ(log, (std::vector<std::string>{ "b" }));
    }

    TEST(CSMEventBus, aThrowingHandlerIsRemovedReportedOnceAndTheOthersStillRun)
    {
        EventBus bus;
        std::vector<std::string> reports;
        bus.setThrowReport(
            [&](const std::string& owner, const std::string& what) { reports.push_back(owner + ":" + what); });
        std::vector<std::string> log;
        bus.subscribe("bad", [&](const EditorEvent&) {
            log.push_back("bad");
            throw std::runtime_error("boom");
        });
        bus.subscribe("good", [&](const EditorEvent&) { log.push_back("good"); });

        bus.publish(event(EditorEventType::AssetOpened));
        bus.publish(event(EditorEventType::AssetOpened));

        EXPECT_EQ(log, (std::vector<std::string>{ "bad", "good", "good" }));
        ASSERT_EQ(reports.size(), 1u);
        EXPECT_EQ(reports[0], "bad:boom");
        EXPECT_EQ(bus.subscriberCount(), 1u);
    }

    TEST(CSMEventBus, aHandlerThatThrowsSomethingElseIsContainedToo)
    {
        EventBus bus;
        int reports = 0;
        bus.setThrowReport([&](const std::string&, const std::string&) { ++reports; });
        bus.subscribe("bad", [](const EditorEvent&) { throw 7; });
        EXPECT_NO_THROW(bus.publish(event(EditorEventType::AssetOpened)));
        EXPECT_EQ(reports, 1);
    }

    TEST(CSMEventBus, anEventPublishedFromAHandlerWaitsForTheRunningDispatchInOrder)
    {
        EventBus bus;
        std::vector<std::string> log;
        bus.subscribe("a", [&](const EditorEvent& e) {
            log.push_back("a:" + e.subject);
            if (e.subject == "first")
            {
                bus.publish(event(EditorEventType::AssetOpened, "second"));
                bus.publish(event(EditorEventType::AssetOpened, "third"));
                log.push_back("a:after-publish");
            }
        });
        bus.subscribe("b", [&](const EditorEvent& e) { log.push_back("b:" + e.subject); });
        bus.publish(event(EditorEventType::AssetOpened, "first"));
        EXPECT_EQ(log,
            (std::vector<std::string>{
                "a:first", "a:after-publish", "b:first", "a:second", "b:second", "a:third", "b:third" }));
    }

    TEST(CSMEventBus, theQueueIsCappedAndCountsWhatItDrops)
    {
        EventBus bus;
        int delivered = 0;
        bus.subscribe("a", [&](const EditorEvent& e) {
            ++delivered;
            if (e.subject == "go")
                for (std::size_t i = 0; i < kMaxQueuedEvents + 10; ++i)
                    bus.publish(event(EditorEventType::AssetOpened, "again"));
        });
        bus.publish(event(EditorEventType::AssetOpened, "go"));
        EXPECT_EQ(bus.droppedCount(), 10u);
        EXPECT_EQ(delivered, static_cast<int>(1 + kMaxQueuedEvents));
    }

    TEST(CSMEventBus, aHandlerRemovedDuringADispatchIsNotCalledAfterwards)
    {
        EventBus bus;
        std::vector<std::string> log;
        std::uint64_t second = 0;
        bus.subscribe("a", [&](const EditorEvent&) {
            log.push_back("a");
            bus.unsubscribe(second);
        });
        second = bus.subscribe("b", [&](const EditorEvent&) { log.push_back("b"); });
        bus.publish(event(EditorEventType::AssetOpened));
        bus.publish(event(EditorEventType::AssetOpened));
        EXPECT_EQ(log, (std::vector<std::string>{ "a", "a" }));
    }

    TEST(CSMEventBus, anOwnerRemovedDuringADispatchIsNotCalledAfterwardsAndTheDispatchSurvives)
    {
        EventBus bus;
        std::vector<std::string> log;
        bus.subscribe("a", [&](const EditorEvent&) {
            log.push_back("a");
            bus.removeOwner("b");
            bus.removeOwner("a");
        });
        bus.subscribe("b", [&](const EditorEvent&) { log.push_back("b"); });
        bus.subscribe("c", [&](const EditorEvent&) { log.push_back("c"); });
        bus.publish(event(EditorEventType::AssetOpened));
        EXPECT_EQ(log, (std::vector<std::string>{ "a", "c" }));
        EXPECT_EQ(bus.subscriberCount(), 1u);
    }

    TEST(CSMEventBus, theThrowReportMayRemoveTheOwnerDuringTheDispatch)
    {
        EventBus bus;
        bus.setThrowReport([&](const std::string& owner, const std::string&) { bus.removeOwner(owner); });
        std::vector<std::string> log;
        bus.subscribe("bad", [](const EditorEvent&) { throw std::runtime_error("x"); });
        bus.subscribe("bad", [&](const EditorEvent&) { log.push_back("bad2"); });
        bus.subscribe("good", [&](const EditorEvent&) { log.push_back("good"); });
        bus.publish(event(EditorEventType::AssetOpened));
        EXPECT_EQ(log, (std::vector<std::string>{ "good" }));
    }

    TEST(CSMEventBus, aHandlerMaySubscribeAndTheNewOneSeesLaterEventsOnly)
    {
        EventBus bus;
        std::vector<std::string> log;
        bool added = false;
        bus.subscribe("a", [&](const EditorEvent&) {
            if (!added)
            {
                added = true;
                bus.subscribe("a", [&](const EditorEvent&) { log.push_back("new"); });
            }
        });
        bus.publish(event(EditorEventType::AssetOpened));
        EXPECT_TRUE(log.empty());
        bus.publish(event(EditorEventType::AssetOpened));
        EXPECT_EQ(log, (std::vector<std::string>{ "new" }));
    }

    // ---- the selection ----------------------------------------------------------------------------------

    TEST(CSMSelectionModel, startsEmptyAndPublishesOnlyWhenTheValueChanges)
    {
        EventBus bus;
        std::vector<EditorEvent> seen;
        bus.subscribe("a", [&](const EditorEvent& e) { seen.push_back(e); });
        SelectionModel model(bus);
        EXPECT_TRUE(model.current().empty());

        model.select({ "joint", "spine" });
        model.select({ "joint", "spine" }); // same value: nothing
        ASSERT_EQ(seen.size(), 1u);
        EXPECT_EQ(seen[0].type, EditorEventType::SelectionChanged);
        EXPECT_EQ(seen[0].subject, "joint");
        EXPECT_EQ(seen[0].detail, "spine");
        EXPECT_EQ(model.current(), (Selection{ "joint", "spine" }));

        model.select({ "joint", "head" });
        ASSERT_EQ(seen.size(), 2u);
        EXPECT_EQ(seen[1].detail, "head");
    }

    TEST(CSMSelectionModel, clearingPublishesAnEmptyEventOnceAndAnEmptyKindDropsTheId)
    {
        EventBus bus;
        std::vector<EditorEvent> seen;
        bus.subscribe("a", [&](const EditorEvent& e) { seen.push_back(e); });
        SelectionModel model(bus);
        model.clear(); // already empty: nothing
        EXPECT_TRUE(seen.empty());

        model.select({ "joint", "spine" });
        model.select({ "", "left over id" }); // an empty kind is "nothing"
        ASSERT_EQ(seen.size(), 2u);
        EXPECT_TRUE(seen[1].subject.empty());
        EXPECT_TRUE(seen[1].detail.empty());
        EXPECT_TRUE(model.current().empty());
        EXPECT_TRUE(model.current().id.empty());
        model.clear();
        EXPECT_EQ(seen.size(), 2u);
    }
}
