#ifndef OPENEDEN_RENDER_GPUTIMING_H
#define OPENEDEN_RENDER_GPUTIMING_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>
#include <vsg/vk/vulkan.h>

namespace vsg
{
    class Device;
    class QueryPool;
}

namespace Render
{
    /*
     * GPU timing (task 0023, F0a). The GPU writes a timestamp when it reaches a point in a command buffer; two of them
     * around a pass give the time the pass took on the GPU. This file has two layers:
     *  - plain functions and SampleWindow, which need no GPU and are unit tested;
     *  - GpuTimer, which owns the Vulkan query pool and exists only when [Rendering] gpu timing is on.
     */

    // The GPU's tick counter has only validBits bits (VkQueueFamilyProperties::timestampValidBits) and wraps around.
    // The difference of two readings is taken modulo 2^validBits so a wrap between them still gives the right answer.
    uint64_t timestampDelta(uint64_t begin, uint64_t end, uint32_t validBits);

    // One tick lasts periodNs nanoseconds (VkPhysicalDeviceLimits::timestampPeriod).
    double ticksToMilliseconds(uint64_t ticks, float periodNs);

    struct SampleSummary
    {
        float median = 0.f;
        float p95 = 0.f;
        std::size_t count = 0;
    };

    /*
     * The last `capacity` samples of one pass, in a ring that is allocated once. add() never allocates. summarize()
     * sorts a copy in a scratch vector the caller supplies (it is called rarely, not per frame).
     */
    class SampleWindow
    {
    public:
        explicit SampleWindow(std::size_t capacity);
        void add(float value);
        std::size_t size() const { return mCount; }
        SampleSummary summarize(std::vector<float>& scratch) const;

    private:
        std::vector<float> mRing;
        std::size_t mNext = 0;
        std::size_t mCount = 0;
    };

    // Which of the ring's frame slots a frame's queries live in. A frame is read gpuTimingFrameDelay frames after it
    // was recorded, by when the GPU has finished it (the frame fence ring is three long); the ring has one more slot
    // than that, so the slot being read is never the one being recorded.
    constexpr std::size_t gpuTimingFrameSlots = 4;
    constexpr uint64_t gpuTimingFrameDelay = 3;
    constexpr std::size_t gpuTimingMaxPasses = 16;
    constexpr std::size_t gpuTimingWindow = 240;

    constexpr std::size_t gpuTimingSlot(uint64_t frame)
    {
        return static_cast<std::size_t>(frame % gpuTimingFrameSlots);
    }
    constexpr uint32_t gpuTimingFirstQuery(std::size_t slot, std::size_t pass)
    {
        return static_cast<uint32_t>((slot * gpuTimingMaxPasses + pass) * 2);
    }

    class GpuTimer;

    /*
     * Placed in a command graph. Records either the start of a pass (which first resets that pass's two queries, so it
     * must be outside any render pass instance) or its end. Owned by the scene graph; refers to its GpuTimer, which
     * must outlive the command buffers that use it: Engine waits for the device to be idle before it destroys the timer.
     */
    class GpuTimestampNode : public vsg::Inherit<vsg::Node, GpuTimestampNode>
    {
    public:
        GpuTimestampNode(GpuTimer& timer, std::size_t pass, bool isBegin)
            : mTimer(&timer)
            , mPass(pass)
            , mIsBegin(isBegin)
        {
        }
        void accept(vsg::RecordTraversal& recordTraversal) const override;

    private:
        GpuTimer* mTimer;
        std::size_t mPass;
        bool mIsBegin;
    };

    class GpuTimer
    {
    public:
        // Null, with the reason in whyNot, when the device cannot do it (no valid timestamp bits on the queue family,
        // or a zero timestampPeriod). The caller reports a missing feature once.
        static std::unique_ptr<GpuTimer> create(vsg::ref_ptr<vsg::Device> device, uint32_t timestampValidBits,
            float timestampPeriodNs, std::string& whyNot);

        ~GpuTimer();
        GpuTimer(const GpuTimer&) = delete;
        GpuTimer& operator=(const GpuTimer&) = delete;

        // Registers a pass and returns its index; only before the first frame is recorded. Returns passCapacity()
        // when the table is full (and the nodes made for that index do nothing).
        std::size_t addPass(std::string name);
        std::size_t passCapacity() const { return gpuTimingMaxPasses; }
        vsg::ref_ptr<vsg::Node> beginNode(std::size_t pass) { return GpuTimestampNode::create(*this, pass, true); }
        vsg::ref_ptr<vsg::Node> endNode(std::size_t pass) { return GpuTimestampNode::create(*this, pass, false); }

        // Main thread, after frame `currentFrame` has been recorded and submitted. Reads the results of frame
        // currentFrame - gpuTimingFrameDelay without waiting; a pass whose results are not ready is skipped.
        void collect(uint64_t currentFrame);

        const std::vector<std::string>& passNames() const { return mNames; }
        // The frame the last collect() read, and each pass's time in ms for it, or a negative number if the pass was
        // not recorded or not ready.
        uint64_t lastFrame() const { return mLastFrame; }
        bool hasLast() const { return mHasLast; }
        float lastMilliseconds(std::size_t pass) const { return mLast[pass]; }
        // A table of median and p95 per pass over the window, one pass per line.
        std::string summaryText() const;

        // Called by GpuTimestampNode on the recording thread.
        void record(VkCommandBuffer commandBuffer, uint64_t frame, std::size_t pass, bool isBegin);

    private:
        GpuTimer(vsg::ref_ptr<vsg::Device> device, uint32_t validBits, float periodNs);

        vsg::ref_ptr<vsg::Device> mDevice;
        vsg::ref_ptr<vsg::QueryPool> mPool;
        uint32_t mValidBits;
        float mPeriodNs;
        std::vector<std::string> mNames;
        // frame + 1 of the last frame whose end timestamp was recorded for each (slot, pass); 0 = never.
        std::array<std::atomic<uint64_t>, gpuTimingFrameSlots * gpuTimingMaxPasses> mRecorded;
        std::vector<SampleWindow> mWindows;
        std::array<float, gpuTimingMaxPasses> mLast;
        uint64_t mLastFrame = 0;
        bool mHasLast = false;
        uint64_t mNotReady = 0; // passes recorded but not yet finished by the GPU when read, so their sample is lost
    };
}

#endif
