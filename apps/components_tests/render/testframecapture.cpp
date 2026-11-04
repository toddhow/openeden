#include <gtest/gtest.h>

#include <components/render/framecapture.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

// The pure parts of the game's screenshots (Render::FrameCapture): the cap on copies waiting to be written, whether a
// request is copied from the frame about to be recorded, and when a recorded copy is finished.
namespace
{
    using Render::ArmDecision;
    using Render::CopyState;
    using Render::ScreenshotCopyBudget;

    constexpr VkExtent2D sWindow{ 1280, 720 };
    constexpr int sRounds = 2000;

    TEST(RenderScreenshotCopyBudget, gives_tokens_up_to_the_cap_and_no_more)
    {
        ScreenshotCopyBudget budget(3);
        std::vector<ScreenshotCopyBudget::Token> held;
        for (int i = 0; i < 3; ++i)
        {
            auto token = budget.tryAcquire();
            ASSERT_TRUE(token.has_value()) << "token " << i;
            held.push_back(std::move(*token));
        }
        EXPECT_EQ(budget.inUse(), 3u);
        EXPECT_FALSE(budget.tryAcquire().has_value());
        EXPECT_EQ(budget.inUse(), 3u);
    }

    TEST(RenderScreenshotCopyBudget, a_dropped_token_makes_room_again)
    {
        ScreenshotCopyBudget budget(1);
        {
            auto token = budget.tryAcquire();
            ASSERT_TRUE(token.has_value());
            EXPECT_FALSE(budget.tryAcquire().has_value());
        }
        EXPECT_EQ(budget.inUse(), 0u);
        EXPECT_TRUE(budget.tryAcquire().has_value());
    }

    TEST(RenderScreenshotCopyBudget, a_moved_token_is_given_back_once)
    {
        ScreenshotCopyBudget budget(2);
        auto first = budget.tryAcquire();
        ASSERT_TRUE(first.has_value());
        std::optional<ScreenshotCopyBudget::Token> moved(std::move(*first));
        first.reset();
        EXPECT_EQ(budget.inUse(), 1u);
        auto second = budget.tryAcquire();
        ASSERT_TRUE(second.has_value());
        *moved = std::move(*second); // the token moved over is given back, the one moved in is kept
        EXPECT_EQ(budget.inUse(), 1u);
        second.reset();
        EXPECT_EQ(budget.inUse(), 1u);
        moved.reset();
        EXPECT_EQ(budget.inUse(), 0u);
    }

    TEST(RenderScreenshotCopyBudget, a_token_may_outlive_its_budget)
    {
        std::optional<ScreenshotCopyBudget::Token> token;
        {
            ScreenshotCopyBudget budget(1);
            token = budget.tryAcquire();
            ASSERT_TRUE(token.has_value());
        }
        token.reset(); // must not touch freed memory (the address sanitizer run would say so)
        SUCCEED();
    }

    TEST(RenderScreenshotCopyBudget, threads_never_hold_more_than_the_cap)
    {
        constexpr std::size_t cap = 4;
        constexpr int threadCount = 8;
        ScreenshotCopyBudget budget(cap);
        std::atomic<std::size_t> holding{ 0 };
        std::atomic<std::size_t> most{ 0 };
        std::atomic<bool> go{ false };
        std::vector<std::thread> threads;
        for (int t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&budget, &holding, &most, &go] {
                while (!go.load())
                    std::this_thread::yield();
                for (int i = 0; i < sRounds; ++i)
                {
                    auto token = budget.tryAcquire();
                    if (!token)
                        continue;
                    const std::size_t now = ++holding;
                    std::size_t seen = most.load();
                    while (now > seen && !most.compare_exchange_weak(seen, now))
                    {
                    }
                    --holding;
                }
            });
        }
        go = true;
        for (auto& thread : threads)
            thread.join();
        EXPECT_LE(most.load(), cap);
        EXPECT_GE(most.load(), 1u);
        EXPECT_EQ(budget.inUse(), 0u);
    }

    TEST(RenderFrameCaptureArm, a_visible_window_with_an_image_is_copied_at_once)
    {
        EXPECT_EQ(Render::decideArm(true, sWindow, 0, 60), ArmDecision::Arm);
        EXPECT_EQ(Render::decideArm(true, sWindow, 59, 60), ArmDecision::Arm);
    }

    TEST(RenderFrameCaptureArm, no_image_waits_for_the_next_frame)
    {
        EXPECT_EQ(Render::decideArm(false, sWindow, 0, 60), ArmDecision::Wait);
        EXPECT_EQ(Render::decideArm(true, VkExtent2D{ 0, 720 }, 0, 60), ArmDecision::Wait);
        EXPECT_EQ(Render::decideArm(true, VkExtent2D{ 1280, 0 }, 10, 60), ArmDecision::Wait);
    }

    TEST(RenderFrameCaptureArm, a_request_that_waited_too_long_is_dropped)
    {
        EXPECT_EQ(Render::decideArm(false, sWindow, 59, 60), ArmDecision::Wait);
        EXPECT_EQ(Render::decideArm(false, sWindow, 60, 60), ArmDecision::Drop);
        EXPECT_EQ(Render::decideArm(true, VkExtent2D{ 0, 0 }, 61, 60), ArmDecision::Drop);
    }

    TEST(RenderFrameCaptureArm, an_image_after_waiting_is_still_copied)
    {
        EXPECT_EQ(Render::decideArm(true, sWindow, 60, 60), ArmDecision::Arm);
    }

    TEST(RenderFrameCaptureCopy, a_frame_still_on_the_gpu_is_in_flight)
    {
        EXPECT_EQ(Render::copyState(true, true, VK_NOT_READY), CopyState::InFlight);
    }

    TEST(RenderFrameCaptureCopy, a_signalled_fence_means_done)
    {
        EXPECT_EQ(Render::copyState(true, true, VK_SUCCESS), CopyState::Done);
    }

    TEST(RenderFrameCaptureCopy, a_frame_the_fence_ring_has_moved_past_is_done)
    {
        // RecordAndSubmitTask::start waited for it before it reused the fence; the status is not read.
        EXPECT_EQ(Render::copyState(false, false, VK_NOT_READY), CopyState::Done);
        EXPECT_EQ(Render::copyState(false, true, VK_ERROR_DEVICE_LOST), CopyState::Done);
    }

    TEST(RenderFrameCaptureCopy, a_frame_never_submitted_is_lost)
    {
        EXPECT_EQ(Render::copyState(true, false, VK_NOT_READY), CopyState::Lost);
        EXPECT_EQ(Render::copyState(true, false, VK_SUCCESS), CopyState::Lost);
    }

    TEST(RenderFrameCaptureCopy, a_failing_fence_is_lost)
    {
        EXPECT_EQ(Render::copyState(true, true, VK_ERROR_DEVICE_LOST), CopyState::Lost);
        EXPECT_EQ(Render::copyState(true, true, VK_ERROR_OUT_OF_DEVICE_MEMORY), CopyState::Lost);
    }
}
