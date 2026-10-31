#include <components/vsgutil/deletionqueue.hpp>

#include <vsg/core/Object.h>

#include <gtest/gtest.h>

namespace
{
    // A queue for two frames keeps an object through one advance and releases it on the second.
    struct DeletionQueueTest : ::testing::Test
    {
        vsgUtil::DeletionQueue mQueue{ 2 };
        vsg::ref_ptr<vsg::Object> mObject = vsg::Object::create();

        bool queueHoldsObject() const { return mObject->referenceCount() > 1; }
    };

    TEST_F(DeletionQueueTest, advanceFrameReleasesAfterQueuedFrames)
    {
        mQueue.add(mObject);
        EXPECT_TRUE(queueHoldsObject());
        mQueue.advanceFrame();
        EXPECT_TRUE(queueHoldsObject());
        mQueue.advanceFrame();
        EXPECT_FALSE(queueHoldsObject());
    }

    TEST_F(DeletionQueueTest, advanceToFrameAdvancesOncePerFrameNumber)
    {
        mQueue.add(mObject);
        mQueue.advanceToFrame(1);
        mQueue.advanceToFrame(1);
        mQueue.advanceToFrame(1);
        EXPECT_TRUE(queueHoldsObject());
        mQueue.advanceToFrame(2);
        EXPECT_FALSE(queueHoldsObject());
    }

    TEST_F(DeletionQueueTest, advanceToFrameCountsFrameZero)
    {
        mQueue.add(mObject);
        mQueue.advanceToFrame(0);
        mQueue.advanceToFrame(0);
        EXPECT_TRUE(queueHoldsObject());
        mQueue.advanceToFrame(1);
        EXPECT_FALSE(queueHoldsObject());
    }

    // Engine's hosted onDetached: frame 7 last used the object and the host's timer has not yet reported it. Reporting it
    // before add() means 8, 9 and 10 all count, and frame 10's submission has waited on frame 7's fence; the fence ring
    // is three long (vsg::Viewer::assignRecordAndSubmitTaskAndPresentation, numBuffers).
    TEST(DeletionQueue, advanceToFrameBeforeAddKeepsObjectUntilThirdNewFrame)
    {
        vsgUtil::DeletionQueue queue{ 3 };
        vsg::ref_ptr<vsg::Object> object = vsg::Object::create();
        queue.advanceToFrame(7);
        queue.add(object);
        queue.advanceToFrame(7);
        queue.advanceToFrame(8);
        queue.advanceToFrame(9);
        EXPECT_GT(object->referenceCount(), 1u);
        queue.advanceToFrame(10);
        EXPECT_EQ(object->referenceCount(), 1u);
    }

    // The defect that order prevents: without reporting frame 7 first, the object goes after frame 9.
    TEST(DeletionQueue, advanceToFrameAfterAddCountsTheFrameThatUsedTheObject)
    {
        vsgUtil::DeletionQueue queue{ 3 };
        vsg::ref_ptr<vsg::Object> object = vsg::Object::create();
        queue.advanceToFrame(6);
        queue.add(object);
        queue.advanceToFrame(7);
        queue.advanceToFrame(8);
        queue.advanceToFrame(9);
        EXPECT_EQ(object->referenceCount(), 1u);
    }

    TEST_F(DeletionQueueTest, advanceToFrameCountsSkippedFramesAsOne)
    {
        mQueue.add(mObject);
        mQueue.advanceToFrame(5);
        mQueue.advanceToFrame(50);
        EXPECT_FALSE(queueHoldsObject());
    }
}
