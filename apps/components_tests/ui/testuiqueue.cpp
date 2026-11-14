// Task 0162: the element protocol's headless implementation and the command queue between the script thread and the UI
// engine thread. Written from the spec (docs/tasks/0162-ui-element-protocol-and-lua-library.md) and run red against
// stubs first.
#include <gtest/gtest.h>

#include <components/ui/commandqueue.hpp>
#include <components/ui/elementprotocol.hpp>
#include <components/ui/headless.hpp>

#include <thread>

namespace
{
    using namespace Ui;

    TEST(UiElementIds, SessionAndCounterShareOneNumber)
    {
        const ElementId id = makeElementId(7, 12345);
        EXPECT_EQ(sessionOf(id), 7u);
        EXPECT_EQ(id & 0xffffffu, 12345u);
        EXPECT_EQ(sessionOf(makeElementId(255, 1)), 255u);
    }

    TEST(UiEventKinds, NamesRoundTrip)
    {
        for (EventKind kind : { EventKind::Click, EventKind::Change, EventKind::Focus, EventKind::Blur, EventKind::Scroll })
        {
            const auto back = eventKindFromName(eventKindName(kind));
            ASSERT_TRUE(back.has_value());
            EXPECT_EQ(*back, kind);
        }
        EXPECT_FALSE(eventKindFromName("hover").has_value());
        EXPECT_FALSE(eventKindFromName("").has_value());
    }

    struct HeadlessTest : testing::Test
    {
        HeadlessBackend b;
    };

    TEST_F(HeadlessTest, AllocatedIdsAreDistinctNonZeroAndCarryTheSession)
    {
        HeadlessBackend other(9);
        const ElementId a = b.allocateId();
        const ElementId c = b.allocateId();
        EXPECT_NE(a, kNoElement);
        EXPECT_NE(a, c);
        EXPECT_EQ(sessionOf(a), 1u);
        EXPECT_EQ(sessionOf(other.allocateId()), 9u);
    }

    TEST_F(HeadlessTest, CreateAttachAndInspectATree)
    {
        const ElementId doc = b.allocateId(), a = b.allocateId(), c = b.allocateId();
        b.createDocument(doc, "main");
        b.create(a, "div");
        b.create(c, "span");
        b.insertBefore(doc, a, kNoElement);
        b.insertBefore(doc, c, kNoElement);
        ASSERT_NE(b.node(doc), nullptr);
        EXPECT_TRUE(b.node(doc)->mDocument);
        EXPECT_EQ(b.node(doc)->mName, "main");
        EXPECT_EQ(b.node(doc)->mChildren, (std::vector<ElementId>{ a, c }));
        EXPECT_EQ(b.node(a)->mParent, doc);
        EXPECT_EQ(b.node(c)->mTag, "span");
        EXPECT_EQ(b.liveElements(), 3u);
    }

    TEST_F(HeadlessTest, InsertBeforeOrdersAndMoves)
    {
        const ElementId p = b.allocateId(), x = b.allocateId(), y = b.allocateId(), z = b.allocateId();
        for (ElementId id : { p, x, y, z })
            b.create(id, "div");
        b.insertBefore(p, x, kNoElement);
        b.insertBefore(p, y, kNoElement);
        b.insertBefore(p, z, x); // z before x
        EXPECT_EQ(b.node(p)->mChildren, (std::vector<ElementId>{ z, x, y }));
        b.insertBefore(p, z, kNoElement); // move to the end
        EXPECT_EQ(b.node(p)->mChildren, (std::vector<ElementId>{ x, y, z }));
        b.detach(y);
        EXPECT_EQ(b.node(p)->mChildren, (std::vector<ElementId>{ x, z }));
        EXPECT_EQ(b.node(y)->mParent, kNoElement);
        ASSERT_NE(b.node(y), nullptr);
    }

    TEST_F(HeadlessTest, DestroyRemovesTheSubtreeAndDetachesFromTheParent)
    {
        const ElementId p = b.allocateId(), c = b.allocateId(), g = b.allocateId();
        for (ElementId id : { p, c, g })
            b.create(id, "div");
        b.insertBefore(p, c, kNoElement);
        b.insertBefore(c, g, kNoElement);
        b.destroy(c);
        EXPECT_EQ(b.node(c), nullptr);
        EXPECT_EQ(b.node(g), nullptr);
        EXPECT_TRUE(b.node(p)->mChildren.empty());
        EXPECT_EQ(b.liveElements(), 1u);
    }

    TEST_F(HeadlessTest, UnknownIdsAreIgnoredButStillCounted)
    {
        const ElementId ghost = makeElementId(1, 999);
        b.destroy(ghost);
        b.setText(ghost, "x");
        b.setClass(ghost, "x");
        b.insertBefore(ghost, ghost, kNoElement);
        EXPECT_EQ(b.liveElements(), 0u);
        EXPECT_EQ(b.callCount(), 4u);
    }

    TEST_F(HeadlessTest, SettersStoreValues)
    {
        const ElementId a = b.allocateId();
        b.create(a, "div");
        b.setText(a, "hello");
        b.setClass(a, "one two");
        b.setProperty(a, "width", "10dp");
        b.setProperty(a, "width", "12dp");
        b.setAttribute(a, "src", "x.png");
        const auto* n = b.node(a);
        EXPECT_EQ(n->mText, "hello");
        EXPECT_EQ(n->mClass, "one two");
        EXPECT_EQ(n->mProperties.at("width"), "12dp");
        EXPECT_EQ(n->mAttributes.at("src"), "x.png");
    }

    TEST_F(HeadlessTest, CallLogHasOneLinePerProtocolCallWithIdCounters)
    {
        const ElementId d = b.allocateId(), a = b.allocateId();
        b.createDocument(d, "doc");
        b.create(a, "div");
        b.setText(a, "hi");
        b.insertBefore(d, a, kNoElement);
        b.listen(a, EventKind::Click, true);
        b.focus(a);
        b.destroy(d);
        EXPECT_EQ(b.calls(),
            (std::vector<std::string>{ "createDocument #1 doc", "create #2 div", "setText #2 hi",
                "insertBefore #1 #2 #0", "listen #2 click on", "focus #2", "destroy #1" }));
        EXPECT_EQ(b.callCount(), 7u);
        b.clearCalls();
        EXPECT_TRUE(b.calls().empty());
        EXPECT_EQ(b.callCount(), 0u);
    }

    TEST_F(HeadlessTest, EventsAreDeliveredOnlyToListeningElements)
    {
        const ElementId a = b.allocateId();
        b.create(a, "div");
        EXPECT_FALSE(b.emit(a, EventKind::Click));
        b.listen(a, EventKind::Click, true);
        EXPECT_TRUE(b.emit(a, EventKind::Click));
        EXPECT_FALSE(b.emit(a, EventKind::Change, "x"));
        b.listen(a, EventKind::Click, false);
        EXPECT_FALSE(b.emit(a, EventKind::Click));
        std::vector<Event> events;
        b.takeEvents(events);
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].mTarget, a);
        EXPECT_EQ(events[0].mKind, EventKind::Click);
        events.clear();
        b.takeEvents(events);
        EXPECT_TRUE(events.empty());
    }

    TEST_F(HeadlessTest, CommitIsCountedButIsNotAProtocolCall)
    {
        b.commit();
        b.commit();
        EXPECT_EQ(b.commitCount(), 2u);
        EXPECT_EQ(b.callCount(), 0u);
    }

    TEST_F(HeadlessTest, FocusIsRemembered)
    {
        const ElementId a = b.allocateId();
        b.create(a, "input");
        b.focus(a);
        EXPECT_EQ(b.focused(), a);
        b.destroy(a);
        EXPECT_EQ(b.focused(), kNoElement);
    }

    // ---- the command queue ----

    struct QueueTest : testing::Test
    {
        std::shared_ptr<CommandQueue> queue = std::make_shared<CommandQueue>();
        HeadlessBackend consumer{ 0 };
    };

    TEST_F(QueueTest, NothingIsVisibleBeforeCommit)
    {
        auto s = queue->openSession();
        ASSERT_NE(s, nullptr);
        const ElementId a = s->allocateId();
        s->create(a, "div");
        EXPECT_EQ(queue->drain(consumer), 0u);
        EXPECT_EQ(consumer.liveElements(), 0u);
        s->commit();
        EXPECT_EQ(queue->pendingBatches(), 1u);
        EXPECT_EQ(queue->drain(consumer), 1u);
        EXPECT_EQ(consumer.liveElements(), 1u);
        EXPECT_NE(consumer.node(a), nullptr);
    }

    TEST_F(QueueTest, EveryCommandReachesTheConsumerWithItsArguments)
    {
        auto s = queue->openSession();
        const ElementId d = s->allocateId(), a = s->allocateId(), c = s->allocateId();
        s->createDocument(d, "main");
        s->create(a, "div");
        s->create(c, "input");
        s->insertBefore(d, a, kNoElement);
        s->insertBefore(d, c, a);
        s->setText(a, "some text, with \"quotes\" and \n lines");
        s->setClass(a, "x y");
        s->setProperty(a, "left", "5dp");
        s->setAttribute(c, "type", "text");
        s->listen(c, EventKind::Change, true);
        s->focus(c);
        s->detach(a);
        s->commit();
        queue->drain(consumer);
        EXPECT_EQ(consumer.node(d)->mChildren, (std::vector<ElementId>{ c }));
        EXPECT_EQ(consumer.node(a)->mText, "some text, with \"quotes\" and \n lines");
        EXPECT_EQ(consumer.node(a)->mClass, "x y");
        EXPECT_EQ(consumer.node(a)->mProperties.at("left"), "5dp");
        EXPECT_EQ(consumer.node(c)->mAttributes.at("type"), "text");
        EXPECT_TRUE(consumer.node(c)->mListens.count(EventKind::Change));
        EXPECT_EQ(consumer.focused(), c);
        EXPECT_EQ(consumer.node(a)->mParent, kNoElement);
    }

    TEST_F(QueueTest, BatchesApplyInCommitOrderAcrossSessions)
    {
        auto s1 = queue->openSession();
        auto s2 = queue->openSession();
        const ElementId a = s1->allocateId(), c = s2->allocateId();
        s1->create(a, "div");
        s1->setText(a, "one");
        s1->commit();
        s2->create(c, "div");
        s2->commit();
        s1->setText(a, "two");
        s1->commit();
        queue->drain(consumer);
        EXPECT_EQ(consumer.node(a)->mText, "two");
        EXPECT_EQ(consumer.calls(), (std::vector<std::string>{ "create #1 div", "setText #1 one", "create #1 div",
                                        "setText #1 two" }));
        EXPECT_NE(sessionOf(a), sessionOf(c));
    }

    TEST_F(QueueTest, EmptyCommitMakesNoBatch)
    {
        auto s = queue->openSession();
        s->commit();
        s->commit();
        EXPECT_EQ(queue->pendingBatches(), 0u);
        EXPECT_EQ(queue->drain(consumer), 0u);
    }

    TEST_F(QueueTest, EventsReturnToTheSessionThatOwnsTheTarget)
    {
        auto s1 = queue->openSession();
        auto s2 = queue->openSession();
        const ElementId a = s1->allocateId(), c = s2->allocateId();
        queue->pushEvent({ a, EventKind::Click, "" });
        queue->pushEvent({ c, EventKind::Change, "v" });
        queue->pushEvent({ a, EventKind::Scroll, "12" });
        std::vector<Event> e1, e2;
        s1->takeEvents(e1);
        s2->takeEvents(e2);
        ASSERT_EQ(e1.size(), 2u);
        EXPECT_EQ(e1[0].mKind, EventKind::Click);
        EXPECT_EQ(e1[1].mValue, "12");
        ASSERT_EQ(e2.size(), 1u);
        EXPECT_EQ(e2[0].mValue, "v");
        e1.clear();
        s1->takeEvents(e1);
        EXPECT_TRUE(e1.empty());
    }

    TEST_F(QueueTest, AnEventForAnUnknownSessionIsDropped)
    {
        auto s = queue->openSession();
        queue->pushEvent({ makeElementId(200, 1), EventKind::Click, "" });
        std::vector<Event> events;
        s->takeEvents(events);
        EXPECT_TRUE(events.empty());
    }

    TEST_F(QueueTest, TheInboxIsBounded)
    {
        auto s = queue->openSession();
        const ElementId a = s->allocateId();
        for (std::size_t i = 0; i < CommandQueue::kMaxInboxEvents + 100; ++i)
            queue->pushEvent({ a, EventKind::Scroll, "1" });
        std::vector<Event> events;
        s->takeEvents(events);
        EXPECT_EQ(events.size(), CommandQueue::kMaxInboxEvents);
    }

    TEST_F(QueueTest, ASessionThatEndsDestroysItsOpenDocuments)
    {
        auto keep = queue->openSession();
        const ElementId k = keep->allocateId();
        keep->createDocument(k, "keep");
        keep->commit();
        {
            auto s = queue->openSession();
            const ElementId d = s->allocateId(), a = s->allocateId();
            s->createDocument(d, "gone");
            s->create(a, "div");
            s->insertBefore(d, a, kNoElement);
            s->commit();
            queue->drain(consumer);
            EXPECT_EQ(consumer.liveElements(), 3u);
        }
        queue->drain(consumer);
        EXPECT_EQ(consumer.liveElements(), 1u);
        EXPECT_NE(consumer.node(k), nullptr);
    }

    TEST_F(QueueTest, SessionNumbersAreReusedAfterClose)
    {
        for (unsigned i = 0; i < CommandQueue::kMaxSessions + 5; ++i)
            EXPECT_NE(queue->openSession(), nullptr); // each is closed at the end of the statement
        std::vector<std::unique_ptr<Backend>> all;
        for (unsigned i = 0; i < CommandQueue::kMaxSessions; ++i)
            all.push_back(queue->openSession());
        EXPECT_THROW(queue->openSession(), std::runtime_error);
        all.pop_back();
        EXPECT_NO_THROW(queue->openSession());
    }

    TEST_F(QueueTest, ScriptThreadWritesWhileTheEngineThreadDrains)
    {
        auto s = queue->openSession();
        constexpr int kRounds = 400;
        std::thread writer([&] {
            for (int i = 0; i < kRounds; ++i)
            {
                const ElementId a = s->allocateId();
                s->create(a, "div");
                s->setText(a, std::to_string(i));
                s->commit();
            }
        });
        std::size_t applied = 0;
        HeadlessBackend sink{ 0 };
        while (applied < std::size_t(kRounds) * 2)
            applied += queue->drain(sink);
        writer.join();
        EXPECT_EQ(sink.liveElements(), std::size_t(kRounds));
        // Order inside a session is kept: the last element carries the last text.
        EXPECT_EQ(sink.calls().back(), "setText #" + std::to_string(kRounds) + " " + std::to_string(kRounds - 1));
    }
}
