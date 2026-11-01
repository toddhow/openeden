#include <gtest/gtest.h>

#include <components/render/gputiming.hpp>

#include <vsg/vk/Device.h>

#include <vector>

namespace
{
    using namespace Render;

    TEST(RenderGpuTimingTest, timestampDelta_is_a_plain_difference_without_a_wrap)
    {
        EXPECT_EQ(timestampDelta(100, 350, 64), 250u);
        EXPECT_EQ(timestampDelta(100, 350, 36), 250u);
    }

    TEST(RenderGpuTimingTest, timestampDelta_survives_the_counter_wrapping_between_the_readings)
    {
        // A 36 bit counter: 0xFFFFFFFF0 + 0x20 wraps to 0x10.
        const uint64_t begin = 0xFFFFFFFF0ull;
        const uint64_t end = 0x10ull;
        EXPECT_EQ(timestampDelta(begin, end, 36), 0x20u);
    }

    TEST(RenderGpuTimingTest, timestampDelta_ignores_bits_above_the_valid_bits)
    {
        EXPECT_EQ(timestampDelta(0xAB00000005ull, 0xCD0000000Full, 32), 10u);
    }

    TEST(RenderGpuTimingTest, ticksToMilliseconds_uses_the_period_in_nanoseconds)
    {
        // 1,000,000 ticks of 1 ns are 1 ms; a 52 ns period makes 1,000 ticks 0.052 ms.
        EXPECT_DOUBLE_EQ(ticksToMilliseconds(1000000, 1.0f), 1.0);
        EXPECT_NEAR(ticksToMilliseconds(1000, 52.0f), 0.052, 1e-9);
        EXPECT_DOUBLE_EQ(ticksToMilliseconds(0, 52.0f), 0.0);
    }

    TEST(RenderGpuTimingTest, SampleWindow_median_and_p95_of_one_to_a_hundred)
    {
        SampleWindow window(200);
        for (int i = 1; i <= 100; ++i)
            window.add(static_cast<float>(i));
        std::vector<float> scratch;
        const SampleSummary s = window.summarize(scratch);
        EXPECT_EQ(s.count, 100u);
        EXPECT_FLOAT_EQ(s.median, 50.5f);
        EXPECT_FLOAT_EQ(s.p95, 95.f);
    }

    TEST(RenderGpuTimingTest, SampleWindow_median_of_an_odd_count_is_the_middle_sample)
    {
        SampleWindow window(10);
        for (float v : { 5.f, 1.f, 3.f })
            window.add(v);
        std::vector<float> scratch;
        EXPECT_FLOAT_EQ(window.summarize(scratch).median, 3.f);
    }

    TEST(RenderGpuTimingTest, SampleWindow_forgets_the_oldest_samples_when_full)
    {
        SampleWindow window(4);
        for (float v : { 100.f, 100.f, 100.f, 100.f, 1.f, 2.f, 3.f, 4.f })
            window.add(v);
        std::vector<float> scratch;
        const SampleSummary s = window.summarize(scratch);
        EXPECT_EQ(s.count, 4u);
        EXPECT_FLOAT_EQ(s.median, 2.5f);
        EXPECT_FLOAT_EQ(s.p95, 4.f);
    }

    TEST(RenderGpuTimingTest, SampleWindow_empty_summary_is_zero)
    {
        SampleWindow window(4);
        std::vector<float> scratch;
        const SampleSummary s = window.summarize(scratch);
        EXPECT_EQ(s.count, 0u);
        EXPECT_FLOAT_EQ(s.median, 0.f);
        EXPECT_FLOAT_EQ(s.p95, 0.f);
    }

    TEST(RenderGpuTimingTest, SampleWindow_keeps_at_most_its_capacity)
    {
        SampleWindow window(8);
        for (int i = 0; i < 1000; ++i)
            window.add(static_cast<float>(i));
        EXPECT_EQ(window.size(), 8u);
    }

    TEST(RenderGpuTimingTest, the_frame_that_is_read_never_shares_a_slot_with_a_frame_in_flight)
    {
        for (uint64_t frame = gpuTimingFrameDelay; frame < 64; ++frame)
        {
            const uint64_t read = frame - gpuTimingFrameDelay;
            // The frames being recorded or still on the GPU are the current one and the two before it.
            for (uint64_t offset = 0; offset < 3; ++offset)
                EXPECT_NE(gpuTimingSlot(read), gpuTimingSlot(frame - offset)) << frame;
        }
    }

    TEST(RenderGpuTimingTest, query_indices_of_different_slots_and_passes_do_not_overlap)
    {
        std::vector<bool> used(gpuTimingFrameSlots * gpuTimingMaxPasses * 2, false);
        for (std::size_t slot = 0; slot < gpuTimingFrameSlots; ++slot)
            for (std::size_t pass = 0; pass < gpuTimingMaxPasses; ++pass)
            {
                const uint32_t first = gpuTimingFirstQuery(slot, pass);
                ASSERT_LT(first + 1, used.size());
                EXPECT_FALSE(used[first]);
                EXPECT_FALSE(used[first + 1]);
                used[first] = used[first + 1] = true;
            }
    }

    TEST(RenderGpuTimingTest, a_missing_device_gives_no_timer_and_says_why)
    {
        std::string why;
        EXPECT_EQ(GpuTimer::create(nullptr, 64, 1.f, why), nullptr);
        EXPECT_FALSE(why.empty());
    }
}
