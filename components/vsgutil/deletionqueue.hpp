#ifndef VSGOPENMW_VSGUTIL_DELETIONQUEUE_H
#define VSGOPENMW_VSGUTIL_DELETIONQUEUE_H

#include <cstdint>
#include <optional>
#include <vector>

#include <vsg/core/Object.h>

namespace vsgUtil
{
    /*
     * Prevents Vulkan objects from being deleted while still in use by a queued frame.
     */
    class DeletionQueue
    {
        using Queue = std::vector<vsg::ref_ptr<vsg::Object>>;
        std::vector<Queue> _queues;
        std::optional<std::uint64_t> _lastFrame;
    public:
        DeletionQueue(size_t numFrames)
        {
            _queues.resize(numFrames);
        }
        void resize(size_t numFrames)
        {
            _queues.resize(numFrames);
        }
        void add(vsg::ref_ptr<vsg::Object> object)
        {
            _queues[0].push_back(object);
        }
        void advanceFrame()
        {
            _queues.back().clear();
            for (size_t i = _queues.size()-1; i > 0; --i)
                _queues[i].swap(_queues[i-1]);
        }
        /*
         * Advances once for each new frame number, however often it is called. For a host that does not draw every time
         * it is called (an on-demand viewer), or several callers on one viewer: an extra advance would free an object a
         * queued frame still uses. Frames missed between calls count as one, which only keeps objects a little longer.
         */
        void advanceToFrame(std::uint64_t frameNumber)
        {
            if (_lastFrame == frameNumber)
                return;
            _lastFrame = frameNumber;
            advanceFrame();
        }
    };
}

#endif
