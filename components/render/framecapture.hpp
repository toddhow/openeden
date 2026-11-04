#ifndef OPENEDEN_RENDER_FRAMECAPTURE_H
#define OPENEDEN_RENDER_FRAMECAPTURE_H

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

#include <vsg/core/ref_ptr.h>
#include <vsg/vk/vulkan.h>

namespace vsg
{
    class CommandGraph;
    class Commands;
    class Switch;
    class Viewer;
    class Window;
}

namespace Render
{
    struct Screenshot;

    /*
     * Screenshots of the game's window, copied inside the frame they show (OpenEden, 2026-11-04, second round).
     *
     * The copy of the swapchain image is recorded at the end of the last command graph that writes the image (the
     * game's GUI graph, Engine::setScreenshotGraph), so it runs in the frame's own submission, before
     * vkQueuePresentKHR hands the image to the presentation engine. The pixels are read once that frame's fence has
     * signalled, and the file is written on a worker. Nothing here waits for the GPU.
     *
     * The pure parts below are tested in apps/components_tests/render/testframecapture.cpp.
     */

    // At most cap() screenshot copies (a host-visible image of the window's size each, held until the file is
    // written) exist at once. Thread-safe: tokens are taken on the main thread and given back by whoever drops them
    // (the writer thread), and a token may outlive its budget.
    class ScreenshotCopyBudget
    {
    public:
        class Token
        {
        public:
            Token(Token&& other) noexcept;
            Token& operator=(Token&& other) noexcept;
            Token(const Token&) = delete;
            Token& operator=(const Token&) = delete;
            ~Token();

        private:
            friend class ScreenshotCopyBudget;
            explicit Token(std::shared_ptr<std::atomic<std::size_t>> inUse);
            void release();

            std::shared_ptr<std::atomic<std::size_t>> mInUse;
        };

        explicit ScreenshotCopyBudget(std::size_t cap);

        // A token while fewer than cap() are held, else nothing.
        std::optional<Token> tryAcquire();
        std::size_t inUse() const;
        std::size_t cap() const { return mCap; }

    private:
        std::shared_ptr<std::atomic<std::size_t>> mInUse;
        std::size_t mCap;
    };

    enum class ArmDecision
    {
        Arm, // copy the frame about to be recorded
        Wait, // no image to copy in this frame: try the next one
        Drop, // waited too long
    };

    // Whether a waiting screenshot request is copied from the frame about to be recorded. Only a visible window with
    // an acquired image of non-zero size can be copied; otherwise the request waits, at most maxWaitFrames frames.
    ArmDecision decideArm(bool imageAcquired, VkExtent2D extent, unsigned framesWaited, unsigned maxWaitFrames);

    enum class CopyState
    {
        InFlight,
        Done,
        Lost,
    };

    // The state of a copy recorded age frames ago, from VSG's ring of frame fences (vsg::RecordAndSubmitTask::fence):
    // fenceInRing is whether fence(age) still names that frame's fence, which it does while age is below the ring's
    // length. Once the ring has moved past the frame, RecordAndSubmitTask::start has waited for its fence, so the copy
    // is done. fenceSubmitted is Fence::hasDependencies (false: the frame was never submitted) and fenceStatus
    // vkGetFenceStatus.
    CopyState copyState(bool fenceInRing, bool fenceSubmitted, VkResult fenceStatus);

    /*
     * The game's screenshots, owned by Render::Engine. All calls on the main thread, from Engine::loop.
     */
    class FrameCapture
    {
    public:
        using Done = std::function<void(vsg::ref_ptr<Screenshot>)>;
        // A request with no image to copy waits this many recorded frames, then is dropped.
        static constexpr unsigned sMaxWaitFrames = 60;

        explicit FrameCapture(ScreenshotCopyBudget& budget);
        ~FrameCapture();

        // Appends the switch that holds the copy to graph's children: graph must be the last to write the window's
        // image in a frame (the highest submit order of the window's graphs, and alone at it).
        void attach(vsg::ref_ptr<vsg::CommandGraph> graph);
        bool attached() const { return mGraph != nullptr; }

        // A request for the next frame that is recorded; done is called with the copied screenshot once that frame's
        // copy is finished, and never for a request that is dropped or lost. A second request before the first was
        // copied is the same picture and is merged with it.
        void request(Done done);

        // Before recordAndSubmit: puts the copy of the window's acquired image into the switch.
        void beforeRecord(vsg::Window& window, VkImageLayout presentLayout);
        // After recordAndSubmit: empties the switch, and hands every finished copy to its done.
        void afterRecord(vsg::Viewer& viewer);
        // After the loop's last deviceWaitIdle: every copy in flight is finished.
        void finish();

    private:
        struct Copy
        {
            vsg::ref_ptr<Screenshot> screenshot;
            vsg::ref_ptr<vsg::Commands> commands;
            Done done;
            unsigned age = 0;
        };
        static void deliver(Copy& copy);

        ScreenshotCopyBudget& mBudget;
        vsg::ref_ptr<vsg::CommandGraph> mGraph;
        vsg::ref_ptr<vsg::Switch> mSwitch;
        std::optional<Done> mWaiting;
        unsigned mFramesWaited = 0;
        std::optional<Copy> mArmed;
        std::deque<Copy> mInFlight;
    };
}

#endif
