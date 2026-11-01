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

    // The order in a hosted Engine: a frame is stamped, the host's handleEvents and update run (detaches happen here or
    // between frames), then recordAndSubmit waits on the fence of three frames back, and only then is the frame
    // reported with advanceToFrame. Worst case: the object was last used by frame 7 and added before frame 7 was
    // reported. Its fence is waited on in frame 10's recordAndSubmit, so the report of frame 10, which follows it, is
    // the first safe release.
    TEST(DeletionQueue, postFrameAdvanceKeepsObjectUntilItsFenceHasBeenWaitedOn)
    {
        vsgUtil::DeletionQueue queue{ vsgUtil::DeletionQueue::sizeForPostFrameAdvance(3) };
        vsg::ref_ptr<vsg::Object> object = vsg::Object::create();
        queue.advanceToFrame(6);
        queue.add(object);
        queue.advanceToFrame(7);
        queue.advanceToFrame(7);
        queue.advanceToFrame(8);
        queue.advanceToFrame(9);
        EXPECT_GT(object->referenceCount(), 1u);
        queue.advanceToFrame(10);
        EXPECT_EQ(object->referenceCount(), 1u);
    }

    // The fence ring is three long whatever the swapchain, and a swapchain with more images keeps objects longer.
    TEST(DeletionQueue, sizeForPostFrameAdvanceCoversTheFenceRing)
    {
        EXPECT_EQ(vsgUtil::DeletionQueue::sizeForPostFrameAdvance(2), 4u);
        EXPECT_EQ(vsgUtil::DeletionQueue::sizeForPostFrameAdvance(3), 4u);
        EXPECT_EQ(vsgUtil::DeletionQueue::sizeForPostFrameAdvance(4), 5u);
    }

    TEST_F(DeletionQueueTest, advanceToFrameCountsSkippedFramesAsOne)
    {
        mQueue.add(mObject);
        mQueue.advanceToFrame(5);
        mQueue.advanceToFrame(50);
        EXPECT_FALSE(queueHoldsObject());
    }
}
