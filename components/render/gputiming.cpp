#include "gputiming.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <vsg/app/RecordTraversal.h>
#include <vsg/state/QueryPool.h>
#include <vsg/vk/CommandBuffer.h>
#include <vsg/vk/Device.h>

namespace Render
{
    uint64_t timestampDelta(uint64_t begin, uint64_t end, uint32_t validBits)
    {
        const uint64_t mask = validBits >= 64 ? ~uint64_t{ 0 } : ((uint64_t{ 1 } << validBits) - 1);
        return (end - begin) & mask;
    }

    double ticksToMilliseconds(uint64_t ticks, float periodNs)
    {
        return static_cast<double>(ticks) * static_cast<double>(periodNs) / 1.0e6;
    }

    SampleWindow::SampleWindow(std::size_t capacity)
        : mRing(std::max<std::size_t>(capacity, 1), 0.f)
    {
    }

    void SampleWindow::add(float value)
    {
        mRing[mNext] = value;
        mNext = (mNext + 1) % mRing.size();
        mCount = std::min(mCount + 1, mRing.size());
    }

    SampleSummary SampleWindow::summarize(std::vector<float>& scratch) const
    {
        SampleSummary result;
        result.count = mCount;
        if (mCount == 0)
            return result;
        // The ring holds mCount samples in its first mCount slots until it is full, and all of its slots after that.
        scratch.assign(mRing.begin(), mRing.begin() + static_cast<std::ptrdiff_t>(mCount));
        std::sort(scratch.begin(), scratch.end());
        // Median: the middle sample, or the mean of the two middle ones.
        const std::size_t n = scratch.size();
        result.median = n % 2 == 1 ? scratch[n / 2] : 0.5f * (scratch[n / 2 - 1] + scratch[n / 2]);
        // p95: the nearest-rank sample, number ceil(0.95 n) counting from one.
        const std::size_t rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n)));
        result.p95 = scratch[std::min(std::max<std::size_t>(rank, 1), n) - 1];
        return result;
    }

    void GpuTimestampNode::accept(vsg::RecordTraversal& recordTraversal) const
    {
        const vsg::FrameStamp* stamp = recordTraversal.getFrameStamp();
        if (!stamp || mPass >= gpuTimingMaxPasses)
            return;
        mTimer->record(*recordTraversal.getCommandBuffer(), stamp->frameCount, mPass, mIsBegin);
    }

    std::unique_ptr<GpuTimer> GpuTimer::create(vsg::ref_ptr<vsg::Device> device, uint32_t timestampValidBits,
        float timestampPeriodNs, std::string& whyNot)
    {
        if (!device)
        {
            whyNot = "there is no device";
            return nullptr;
        }
        if (timestampValidBits == 0)
        {
            whyNot = "the graphics queue family reports no valid timestamp bits";
            return nullptr;
        }
        if (!(timestampPeriodNs > 0.f))
        {
            whyNot = "the device reports a timestamp period of zero";
            return nullptr;
        }
        return std::unique_ptr<GpuTimer>(new GpuTimer(device, timestampValidBits, timestampPeriodNs));
    }

    GpuTimer::GpuTimer(vsg::ref_ptr<vsg::Device> device, uint32_t validBits, float periodNs)
        : mDevice(device)
        , mValidBits(validBits)
        , mPeriodNs(periodNs)
    {
        const uint32_t queryCount = static_cast<uint32_t>(gpuTimingFrameSlots * gpuTimingMaxPasses * 2);
        mPool = vsg::QueryPool::create(device, 0, VK_QUERY_TYPE_TIMESTAMP, queryCount, 0);
        for (auto& recorded : mRecorded)
            recorded.store(0);
        mLast.fill(-1.f);
        mNames.reserve(gpuTimingMaxPasses);
        mWindows.reserve(gpuTimingMaxPasses);
    }

    GpuTimer::~GpuTimer() = default;

    std::size_t GpuTimer::addPass(std::string name)
    {
        if (mNames.size() >= gpuTimingMaxPasses)
            return gpuTimingMaxPasses;
        mNames.push_back(std::move(name));
        mWindows.emplace_back(gpuTimingWindow);
        return mNames.size() - 1;
    }

    void GpuTimer::record(VkCommandBuffer commandBuffer, uint64_t frame, std::size_t pass, bool isBegin)
    {
        const std::size_t slot = gpuTimingSlot(frame);
        const uint32_t first = gpuTimingFirstQuery(slot, pass);
        if (isBegin)
        {
            // A query must be reset before it is written again. A reset is not allowed inside a render pass instance;
            // the nodes are placed outside them.
            vkCmdResetQueryPool(commandBuffer, mPool->vk(), first, 2);
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mPool->vk(), first);
        }
        else
        {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mPool->vk(), first + 1);
            mRecorded[slot * gpuTimingMaxPasses + pass].store(frame + 1, std::memory_order_release);
        }
    }

    void GpuTimer::collect(uint64_t currentFrame)
    {
        if (currentFrame < gpuTimingFrameDelay)
            return;
        const uint64_t frame = currentFrame - gpuTimingFrameDelay;
        const std::size_t slot = gpuTimingSlot(frame);
        mLast.fill(-1.f);
        bool any = false;
        for (std::size_t pass = 0; pass < mNames.size(); ++pass)
        {
            if (mRecorded[slot * gpuTimingMaxPasses + pass].load(std::memory_order_acquire) != frame + 1)
                continue;
            // Two queries, each a value and an availability word, read without VK_QUERY_RESULT_WAIT_BIT: a result the
            // GPU has not produced yet is reported as unavailable, and nothing stalls.
            std::array<uint64_t, 4> data{};
            const VkResult result = vkGetQueryPoolResults(*mDevice, mPool->vk(), gpuTimingFirstQuery(slot, pass), 2,
                sizeof(data), data.data(), 2 * sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if ((result != VK_SUCCESS && result != VK_NOT_READY) || data[1] == 0 || data[3] == 0)
            {
                ++mNotReady;
                continue;
            }
            const float ms
                = static_cast<float>(ticksToMilliseconds(timestampDelta(data[0], data[2], mValidBits), mPeriodNs));
            mLast[pass] = ms;
            mWindows[pass].add(ms);
            any = true;
        }
        mHasLast = any;
        if (any)
            mLastFrame = frame;
    }

    std::string GpuTimer::summaryText() const
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(3);
        std::vector<float> scratch;
        for (std::size_t pass = 0; pass < mNames.size(); ++pass)
        {
            const SampleSummary s = mWindows[pass].summarize(scratch);
            out << "  " << mNames[pass] << ": median " << s.median << " ms, p95 " << s.p95 << " ms, samples "
                << s.count << '\n';
        }
        if (mNotReady)
            out << "  (" << mNotReady << " samples were not ready when read and are left out)\n";
        return out.str();
    }
}
