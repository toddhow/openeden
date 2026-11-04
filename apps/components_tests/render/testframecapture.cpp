#include <gtest/gtest.h>

#include <components/render/download.hpp>
#include <components/render/framecapture.hpp>

#include <vsg/core/Version.h>

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

    // Render::CaptureQueue (third review round) against a fake of VSG 1.1.16's fence ring.

    // vsg::RecordAndSubmitTask's ring as the queue sees it: one fence per frame slot, sRingLength slots
    // (Viewer::assignRecordAndSubmitTaskAndPresentation, numBuffers = 3); advance() shifts the slots once a frame, so
    // fence(age) is the fence of the frame recorded age frames ago while age < sRingLength, and null after; start()
    // waits, without a limit, for the fence of a slot before it reuses it, so a frame that has left the ring has
    // finished. The fake GPU finishes a frame only when the test says so, or when the ring reuses its slot.
    constexpr std::size_t sRingLength = 3;

    class FakeRing
    {
    public:
        // One frame recorded (and submitted, unless said otherwise).
        int record(bool submit = true)
        {
            const int frame = mNext++;
            mSlots.insert(mSlots.begin(), frame);
            if (mSlots.size() > sRingLength)
            {
                // start() of the reused slot waits for its frame; the GPU finishes in order.
                for (int f = 0; f <= mSlots.back(); ++f)
                    mFinished.push_back(f);
                mSlots.pop_back();
            }
            if (submit)
                mSubmitted.push_back(frame);
            return frame;
        }
        void finish(int frame) { mFinished.push_back(frame); }

        Render::FrameFence fence(unsigned age) const
        {
            Render::FrameFence result;
            if (age >= mSlots.size())
                return result; // fence(age) is null
            const int frame = mSlots[age];
            result.inRing = true;
            result.submitted = contains(mSubmitted, frame);
            result.status = contains(mFinished, frame) ? VK_SUCCESS : VK_NOT_READY;
            return result;
        }
        auto fenceOf() const
        {
            return [this](unsigned age) { return fence(age); };
        }

    private:
        static bool contains(const std::vector<int>& list, int frame)
        {
            return std::find(list.begin(), list.end(), frame) != list.end();
        }

        int mNext = 0;
        std::vector<int> mSlots; // mSlots[age] is the frame recorded age frames ago
        std::vector<int> mSubmitted;
        std::vector<int> mFinished;
    };

    using Queue = Render::CaptureQueue<int>;
    constexpr unsigned sMaxWait = 60;

    // A request copied from the next recorded frame: what the shell does in FrameCapture::beforeRecord.
    void armIfWaiting(Queue& queue)
    {
        const auto decision = queue.decide(true, sWindow);
        if (decision && *decision == ArmDecision::Arm)
            queue.armed(queue.takeWaiting());
    }

    TEST(RenderCaptureQueue, a_copy_is_handed_back_only_once_its_frame_has_finished)
    {
        Queue queue(sMaxWait);
        FakeRing ring;
        queue.request(7);
        armIfWaiting(queue);
        const int frame = ring.record();
        auto collected = queue.afterRecord(ring.fenceOf());
        EXPECT_TRUE(collected.done.empty());
        EXPECT_TRUE(collected.armedThisFrame);
        ring.record();
        collected = queue.afterRecord(ring.fenceOf());
        EXPECT_TRUE(collected.done.empty()) << "the GPU has not finished the frame yet";
        ring.finish(frame);
        ring.record();
        collected = queue.afterRecord(ring.fenceOf());
        ASSERT_EQ(collected.done.size(), 1u);
        EXPECT_EQ(collected.done[0], 7);
        EXPECT_EQ(queue.inFlight(), 0u);
    }

    TEST(RenderCaptureQueue, a_frame_the_ring_has_left_is_handed_back_even_if_never_seen_finished)
    {
        Queue queue(sMaxWait);
        FakeRing ring;
        queue.request(1);
        armIfWaiting(queue);
        ring.record();
        EXPECT_TRUE(queue.afterRecord(ring.fenceOf()).done.empty());
        for (std::size_t i = 1; i < sRingLength; ++i)
        {
            ring.record();
            EXPECT_TRUE(queue.afterRecord(ring.fenceOf()).done.empty()) << "frame " << i;
        }
        ring.record(); // the slot of the copy's frame is reused: start() waited for it
        const auto collected = queue.afterRecord(ring.fenceOf());
        ASSERT_EQ(collected.done.size(), 1u);
        EXPECT_EQ(collected.done[0], 1);
    }

    TEST(RenderCaptureQueue, ages_move_once_per_recorded_frame)
    {
        // The copy's frame is still on the GPU while the next one has finished (not how a GPU works, but the only
        // way to tell fence(age) of the right frame from a neighbour's).
        Queue queue(sMaxWait);
        FakeRing ring;
        queue.request(1);
        armIfWaiting(queue);
        ring.record();
        queue.afterRecord(ring.fenceOf());
        const int next = ring.record();
        ring.finish(next);
        EXPECT_TRUE(queue.afterRecord(ring.fenceOf()).done.empty()) << "read the fence of the wrong frame";
    }

    TEST(RenderCaptureQueue, the_first_copy_still_in_flight_stops_the_search)
    {
        Queue queue(sMaxWait);
        FakeRing ring;
        queue.request(1);
        armIfWaiting(queue);
        const int first = ring.record();
        queue.afterRecord(ring.fenceOf());
        queue.request(2);
        armIfWaiting(queue);
        const int second = ring.record();
        ring.finish(second);
        auto collected = queue.afterRecord(ring.fenceOf());
        EXPECT_TRUE(collected.done.empty());
        EXPECT_EQ(queue.inFlight(), 2u);
        ring.finish(first);
        ring.record();
        collected = queue.afterRecord(ring.fenceOf());
        ASSERT_EQ(collected.done.size(), 2u);
        EXPECT_EQ(collected.done[0], 1);
        EXPECT_EQ(collected.done[1], 2);
    }

    TEST(RenderCaptureQueue, a_frame_never_submitted_is_lost)
    {
        Queue queue(sMaxWait);
        FakeRing ring;
        queue.request(3);
        armIfWaiting(queue);
        ring.record(false);
        const auto collected = queue.afterRecord(ring.fenceOf());
        EXPECT_TRUE(collected.done.empty());
        ASSERT_EQ(collected.lost.size(), 1u);
        EXPECT_EQ(collected.lost[0], 3);
        EXPECT_EQ(queue.inFlight(), 0u);
    }

    TEST(RenderCaptureQueue, two_requests_before_a_frame_is_recorded_are_one_copy)
    {
        Queue queue(sMaxWait);
        FakeRing ring;
        EXPECT_TRUE(queue.request(1));
        EXPECT_FALSE(queue.request(2));
        armIfWaiting(queue);
        EXPECT_FALSE(queue.waiting());
        ring.record();
        ring.finish(0);
        const auto collected = queue.afterRecord(ring.fenceOf());
        ASSERT_EQ(collected.done.size(), 1u);
        EXPECT_EQ(collected.done[0], 1);
    }

    TEST(RenderCaptureQueue, only_the_frame_that_recorded_a_copy_says_so)
    {
        Queue queue(sMaxWait);
        FakeRing ring;
        queue.request(1);
        armIfWaiting(queue);
        ring.record();
        EXPECT_TRUE(queue.afterRecord(ring.fenceOf()).armedThisFrame);
        ring.record();
        EXPECT_FALSE(queue.afterRecord(ring.fenceOf()).armedThisFrame);
    }

    TEST(RenderCaptureQueue, a_request_with_no_image_waits_then_is_dropped)
    {
        Queue queue(3);
        queue.request(5);
        for (int i = 0; i < 3; ++i)
        {
            const auto decision = queue.decide(false, sWindow);
            ASSERT_TRUE(decision.has_value());
            EXPECT_EQ(*decision, ArmDecision::Wait) << "frame " << i;
        }
        const auto decision = queue.decide(false, sWindow);
        ASSERT_TRUE(decision.has_value());
        EXPECT_EQ(*decision, ArmDecision::Drop);
        EXPECT_EQ(queue.takeWaiting(), 5);
        EXPECT_FALSE(queue.waiting());
        EXPECT_FALSE(queue.decide(true, sWindow).has_value());
    }

    TEST(RenderCaptureQueue, finishing_hands_back_everything_in_flight_and_drops_the_rest)
    {
        Queue queue(sMaxWait);
        FakeRing ring;
        queue.request(1);
        armIfWaiting(queue);
        ring.record();
        queue.afterRecord(ring.fenceOf());
        queue.request(2);
        armIfWaiting(queue);
        ring.record();
        queue.afterRecord(ring.fenceOf());
        queue.request(3); // waiting when the loop ends
        auto [done, dropped] = queue.finish();
        ASSERT_EQ(done.size(), 2u);
        EXPECT_EQ(done[0], 1);
        EXPECT_EQ(done[1], 2);
        ASSERT_EQ(dropped.size(), 1u);
        EXPECT_EQ(dropped[0], 3);
        EXPECT_EQ(queue.inFlight(), 0u);
        EXPECT_FALSE(queue.waiting());
    }

    // The barrier masks of the sync modes (downloadSyncMasks; third review round). The reason is in download.cpp: a
    // barrier's first scope holds the earlier commands, in submission order, at the stages it names, so the stages and
    // accesses named here are what the copy waits for.

    TEST(RenderDownloadSync, the_screenshot_copy_waits_for_every_stage_and_the_attachment_and_transfer_writes)
    {
        const Render::DownloadSyncMasks masks = Render::downloadSyncMasks(Render::DownloadSync::BeforePresent);
        EXPECT_EQ(masks.firstSrcStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
        EXPECT_NE(masks.srcImageWrites & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0u);
        EXPECT_NE(masks.srcImageWrites & VK_ACCESS_TRANSFER_WRITE_BIT, 0u);
        EXPECT_NE(masks.lastDstStage & VK_PIPELINE_STAGE_HOST_BIT, 0u);
        EXPECT_EQ(masks.copyReader, static_cast<VkAccessFlags>(VK_ACCESS_HOST_READ_BIT));
        EXPECT_EQ(masks.srcImageAfter, 0u) << "nothing follows in the frame but the present";
    }

    TEST(RenderDownloadSync, the_thumbnail_copy_names_the_colour_attachment_output_stage_and_its_writes)
    {
        const Render::DownloadSyncMasks masks = Render::downloadSyncMasks(Render::DownloadSync::InFrame);
        EXPECT_NE(masks.firstSrcStage & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u);
        EXPECT_NE(masks.firstSrcStage & VK_PIPELINE_STAGE_TRANSFER_BIT, 0u);
        EXPECT_NE(masks.srcImageWrites & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0u);
        EXPECT_NE(masks.srcImageWrites & VK_ACCESS_TRANSFER_WRITE_BIT, 0u);
        EXPECT_NE(masks.lastDstStage & VK_PIPELINE_STAGE_HOST_BIT, 0u);
        EXPECT_EQ(masks.copyReader, static_cast<VkAccessFlags>(VK_ACCESS_HOST_READ_BIT));
        // The GUI pass that follows loads the image at the colour attachment stage: the transition back is ordered
        // before those reads and writes.
        EXPECT_NE(masks.lastDstStage & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u);
        EXPECT_NE(masks.srcImageAfter & VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, 0u);
        EXPECT_NE(masks.srcImageAfter & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0u);
    }

    TEST(RenderDownloadSync, the_transfer_mode_keeps_the_inherited_masks)
    {
        const Render::DownloadSyncMasks masks = Render::downloadSyncMasks(Render::DownloadSync::Transfer);
        EXPECT_EQ(masks.firstSrcStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_TRANSFER_BIT));
        EXPECT_EQ(masks.srcImageWrites, static_cast<VkAccessFlags>(VK_ACCESS_MEMORY_READ_BIT));
        EXPECT_EQ(masks.lastDstStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_TRANSFER_BIT));
        EXPECT_EQ(masks.copyReader, static_cast<VkAccessFlags>(VK_ACCESS_MEMORY_READ_BIT));
        EXPECT_EQ(masks.srcImageAfter, static_cast<VkAccessFlags>(VK_ACCESS_MEMORY_READ_BIT));
    }

    // The fake above, and CaptureQueue's reading of the ring, rest on two places of VSG 1.1.16 that no test can reach
    // without a device: Viewer::assignRecordAndSubmitTaskAndPresentation makes every RecordAndSubmitTask with
    // numBuffers = 3 (Viewer.cpp, about line 501), and RecordAndSubmitTask::start waits for a slot's fence with no
    // limit before reusing it (RecordAndSubmitTask.cpp, start). Another VSG fails here: read those two places again,
    // then move the pin.
    TEST(RenderCaptureQueue, vsg_is_the_version_whose_fence_ring_the_queue_was_checked_against)
    {
        EXPECT_EQ(VSG_VERSION_MAJOR, 1);
        EXPECT_EQ(VSG_VERSION_MINOR, 1);
        EXPECT_EQ(VSG_VERSION_PATCH, 16);
    }
}
