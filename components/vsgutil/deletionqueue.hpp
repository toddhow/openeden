#ifndef VSGOPENMW_VSGUTIL_DELETIONQUEUE_H
#define VSGOPENMW_VSGUTIL_DELETIONQUEUE_H

#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <vsg/core/Object.h>

namespace vsgUtil
{
    /*
     * Prevents Vulkan objects from being deleted while still in use by a queued frame.
     *
     * Thread safety: add, advanceFrame, advanceToFrame and resize may be called from any thread. The game adds from the
     * main thread and from the terrain preload thread (Terrain::Preload::setPositions, through Paging::updateView,
     * detaches the nodes it replaces) while the main thread advances once a frame; without the mutex those two raced on
     * the same vector, an object was released twice or never, and a later advance crashed freeing the node tree.
     * Objects leaving the queue are released after the mutex is let go, so that a destructor may add to the queue
     * again.
     */
    class DeletionQueue
    {
        using Queue = std::vector<vsg::ref_ptr<vsg::Object>>;
        std::mutex _mutex; // guards _queues and _lastFrame
        std::vector<Queue> _queues;
        std::optional<std::uint64_t> _lastFrame;

    public:
        /*
         * The size for a queue advanced only after each frame's submission. vsg::Viewer gives each task a ring of three
         * fences (Viewer::assignRecordAndSubmitTaskAndPresentation, numBuffers), however few swapchain images there
         * are, so frame N's fence is waited on during frame N + 3's submission. An object can be added after frame N's
         * submission and before that frame is reported, which counts as one advance, so it needs N, N + 1, N + 2 and N
         * + 3 to pass.
         */
        static constexpr size_t sizeForPostFrameAdvance(size_t numFrames)
        {
            constexpr size_t fenceRing = 3;
            return (numFrames > fenceRing ? numFrames : fenceRing) + 1;
        }

        DeletionQueue(size_t numFrames) { _queues.resize(numFrames); }
        void resize(size_t numFrames)
        {
            std::vector<Queue> dropped; // released after the lock
            {
                const std::lock_guard lock(_mutex);
                for (size_t i = numFrames; i < _queues.size(); ++i)
                    dropped.push_back(std::move(_queues[i]));
                _queues.resize(numFrames);
            }
        }
        void add(vsg::ref_ptr<vsg::Object> object)
        {
            const std::lock_guard lock(_mutex);
            _queues[0].push_back(std::move(object));
        }
        void advanceFrame()
        {
            Queue expired; // released after the lock
            {
                const std::lock_guard lock(_mutex);
                expired.swap(_queues.back());
                for (size_t i = _queues.size() - 1; i > 0; --i)
                    _queues[i].swap(_queues[i - 1]);
            }
        }
        /*
         * Advances once for each new frame number, however often it is called. For a host that does not draw every time
         * it is called (an on-demand viewer), or several callers on one viewer: an extra advance would free an object a
         * queued frame still uses. Frames missed between calls count as one, which only keeps objects a little longer.
         */
        void advanceToFrame(std::uint64_t frameNumber)
        {
            {
                const std::lock_guard lock(_mutex);
                if (_lastFrame == frameNumber)
                    return;
                _lastFrame = frameNumber;
            }
            advanceFrame();
        }
    };
}

#endif
