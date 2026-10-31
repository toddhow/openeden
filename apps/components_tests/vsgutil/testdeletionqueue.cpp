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

    TEST_F(DeletionQueueTest, advanceToFrameCountsSkippedFramesAsOne)
    {
        mQueue.add(mObject);
        mQueue.advanceToFrame(5);
        mQueue.advanceToFrame(50);
        EXPECT_FALSE(queueHoldsObject());
    }
}
