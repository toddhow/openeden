// Tests for the save thumbnail's state machine (round four of the screenshot work, findings 5 and 6): the state shared
// between RenderManager's request and retrieve and the callbacks of Render::FrameCapture. A request that is abandoned
// (the save never calls retrieve) must not let its late callback overwrite a newer request's state, and a second
// request made while the first still waits for its frame is the same picture. Written before the class.

#include "apps/openmw/mwrender/thumbnailstate.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{
    using State = MWRender::ThumbnailState<std::string>;

    TEST(ThumbnailState, a_delivered_copy_is_retrieved_once)
    {
        State state;
        const auto request = state.begin();
        EXPECT_TRUE(state.waiting());
        state.delivered(request, "frame");
        EXPECT_FALSE(state.waiting());
        const auto first = state.retrieve();
        ASSERT_TRUE(first.ready);
        ASSERT_TRUE(first.copy.has_value());
        EXPECT_EQ(*first.copy, "frame");
        // Nothing is requested now: ready with no picture, as the old code answered.
        const auto second = state.retrieve();
        EXPECT_TRUE(second.ready);
        EXPECT_FALSE(second.copy.has_value());
    }

    TEST(ThumbnailState, retrieving_while_the_frame_is_awaited_is_not_ready_and_keeps_waiting)
    {
        State state;
        const auto request = state.begin();
        EXPECT_FALSE(state.retrieve().ready);
        EXPECT_FALSE(state.retrieve().ready);
        EXPECT_TRUE(state.waiting());
        state.delivered(request, "x");
        EXPECT_TRUE(state.retrieve().ready);
    }

    TEST(ThumbnailState, a_failed_request_is_ready_without_a_picture)
    {
        State state;
        const auto request = state.begin();
        state.failed(request);
        const auto result = state.retrieve();
        EXPECT_TRUE(result.ready);
        EXPECT_FALSE(result.copy.has_value());
        EXPECT_FALSE(state.waiting());
    }

    TEST(ThumbnailState, the_callback_of_an_abandoned_request_does_not_touch_the_newer_one)
    {
        State state;
        const auto old = state.begin(); // the save is abandoned: retrieve is never called, the frame is in flight
        const auto request = state.begin(); // the next save, whose copy was accepted (the first was already armed)
        state.delivered(old, "old frame");
        EXPECT_TRUE(state.waiting()) << "the old frame answered the new request";
        EXPECT_FALSE(state.retrieve().ready);
        state.delivered(request, "new frame");
        const auto result = state.retrieve();
        ASSERT_TRUE(result.copy.has_value());
        EXPECT_EQ(*result.copy, "new frame");
    }

    TEST(ThumbnailState, a_late_failure_of_an_abandoned_request_does_not_fail_the_newer_one)
    {
        State state;
        const auto old = state.begin();
        const auto request = state.begin();
        state.failed(old);
        EXPECT_TRUE(state.waiting());
        state.delivered(request, "new frame");
        const auto result = state.retrieve();
        ASSERT_TRUE(result.copy.has_value());
        EXPECT_EQ(*result.copy, "new frame");
    }

    TEST(ThumbnailState, a_late_callback_after_the_result_was_taken_is_ignored)
    {
        State state;
        const auto old = state.begin();
        const auto request = state.begin();
        state.delivered(request, "new frame");
        EXPECT_TRUE(state.retrieve().copy.has_value());
        state.delivered(old, "old frame");
        const auto result = state.retrieve();
        EXPECT_TRUE(result.ready);
        EXPECT_FALSE(result.copy.has_value()) << "an old frame appeared with nothing requested";
    }

    TEST(ThumbnailState, a_request_merged_into_the_one_still_waiting_is_answered_by_it)
    {
        State state;
        const auto first = state.begin(); // abandoned, but its frame has not been recorded yet
        state.begin(); // FrameCapture::request says the waiting request is the same picture: its callbacks are dropped
        state.merged();
        state.delivered(first, "frame");
        const auto result = state.retrieve();
        ASSERT_TRUE(result.ready);
        ASSERT_TRUE(result.copy.has_value());
        EXPECT_EQ(*result.copy, "frame");
    }

    TEST(ThumbnailState, merging_twice_still_belongs_to_the_waiting_request)
    {
        State state;
        const auto first = state.begin();
        state.begin();
        state.merged();
        state.begin();
        state.merged();
        EXPECT_TRUE(state.waiting());
        state.delivered(first, "frame");
        EXPECT_TRUE(state.retrieve().copy.has_value());
    }

    TEST(ThumbnailState, a_request_that_cannot_be_made_fails_at_once)
    {
        State state;
        const auto request = state.begin();
        state.failed(request); // no capture, or the capture refused
        const auto result = state.retrieve();
        EXPECT_TRUE(result.ready);
        EXPECT_FALSE(result.copy.has_value());
    }

    TEST(ThumbnailState, generations_never_repeat)
    {
        State state;
        const auto a = state.begin();
        const auto b = state.begin();
        const auto c = state.begin();
        EXPECT_NE(a, b);
        EXPECT_NE(b, c);
        EXPECT_NE(a, c);
    }
}
