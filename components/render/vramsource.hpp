#ifndef OPENEDEN_RENDER_VRAMSOURCE_H
#define OPENEDEN_RENDER_VRAMSOURCE_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Render::VideoMemory
{
    /*
     * Task 0080 (docs/tasks/0080-vram-scale-back-and-mip-streaming.md): where the video memory scale-back reads the
     * driver's numbers. Pure: no Vulkan include, so the controller is tested against FakeSource.
     */

    /// What the driver says, in bytes, summed over the device-local heaps; -1 means unknown.
    struct Sample
    {
        std::int64_t budgetBytes = -1;
        std::int64_t usageBytes = -1;
    };

    class Source
    {
    public:
        virtual ~Source() = default;
        virtual Sample sample() = 0;
    };

    /// A scripted list of samples for tests and for the in-game stress mode: each call returns the next, the last one
    /// repeats. An empty list gives unknown samples.
    class FakeSource final : public Source
    {
    public:
        explicit FakeSource(std::vector<Sample> samples)
            : mSamples(std::move(samples))
        {
        }

        Sample sample() override
        {
            if (mSamples.empty())
                return Sample{};
            const Sample result = mSamples[mNext];
            if (mNext + 1 < mSamples.size())
                ++mNext;
            return result;
        }

    private:
        std::vector<Sample> mSamples;
        std::size_t mNext = 0;
    };
}

#endif
