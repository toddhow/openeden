#include <components/vsgutil/deferredbin.hpp>

#include <vector>

#include <vsg/app/RecordTraversal.h>
#include <vsg/vk/State.h>

#include <gtest/gtest.h>

namespace
{
    // Records when it is drawn (visited by a record traversal).
    class Marker : public vsg::Inherit<vsg::Node, Marker>
    {
    public:
        Marker(std::vector<int>& log, int id)
            : mLog(log)
            , mId(id)
        {
        }
        void traverse(vsg::RecordTraversal&) const override { mLog.push_back(mId); }

    private:
        std::vector<int>& mLog;
        int mId;
    };

    struct DeferredBinTest : ::testing::Test
    {
        std::vector<int> mLog;
        vsg::ref_ptr<vsg::RecordTraversal> mTraversalPtr = vsg::RecordTraversal::create();
        vsg::RecordTraversal& mTraversal = *mTraversalPtr;
        vsg::ref_ptr<vsgUtil::DeferredBin> mBin = vsgUtil::DeferredBin::create(0, vsg::Bin::DESCENDING);
        vsg::ref_ptr<Marker> mNear = Marker::create(mLog, 1);
        vsg::ref_ptr<Marker> mFar = Marker::create(mLog, 2);

        void SetUp() override
        {
            // As the view's traversal does when it meets a DepthSorted node: the far one is drawn first.
            mBin->add(mTraversal.getState(), 5.0, mNear.get());
            mBin->add(mTraversal.getState(), 50.0, mFar.get());
        }
    };

    TEST_F(DeferredBinTest, recording_the_view_draws_nothing)
    {
        mBin->accept(mTraversal);
        EXPECT_TRUE(mLog.empty());
    }

    TEST_F(DeferredBinTest, replay_draws_the_collected_elements_far_to_near)
    {
        mBin->accept(mTraversal);
        mBin->replay(mTraversal);
        EXPECT_EQ(mLog, (std::vector<int>{ 2, 1 }));
    }

    TEST_F(DeferredBinTest, the_replay_node_replays_the_bin)
    {
        auto replay = vsgUtil::BinReplay::create(mBin);
        replay->accept(mTraversal);
        EXPECT_EQ(mLog, (std::vector<int>{ 2, 1 }));
    }

    TEST_F(DeferredBinTest, a_cleared_bin_replays_nothing)
    {
        mBin->clear();
        mBin->replay(mTraversal);
        EXPECT_TRUE(mLog.empty());
    }

    TEST_F(DeferredBinTest, a_second_replay_draws_again)
    {
        mBin->replay(mTraversal);
        mBin->replay(mTraversal);
        EXPECT_EQ(mLog.size(), 4u);
    }
}
