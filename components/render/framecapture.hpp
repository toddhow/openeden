#ifndef OPENEDEN_RENDER_FRAMECAPTURE_H
#define OPENEDEN_RENDER_FRAMECAPTURE_H

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

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
     * Copies of a window's image, made inside the frame they show (OpenEden, 2026-11-04, rounds two and three).
     *
     * The copy of the swapchain image is recorded at the end of a command graph of the frame (for screenshots the
     * last graph that writes the image: the game's GUI graph, an editor view's only graph; for the save thumbnail the
     * game's main graph, before the GUI), so it runs in the frame's own submission, before vkQueuePresentKHR hands the
     * image to the presentation engine. The pixels are read once that frame's fence has signalled. Nothing here waits
     * for the GPU.
     *
     * The pure parts below are tested in apps/components_tests/render/testframecapture.cpp.
     */

    // At most cap() copies (a host-visible image of the picture's size each) exist at once: from the frame that
    // records one until its pixels have been read (Screenshot::operate frees it before it writes the file).
    // Thread-safe: tokens are taken on the main thread and given back by whoever drops them (the writer thread), and
    // a token may outlive its budget.
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

    // What VSG's fence ring says about the frame recorded age frames ago: RecordAndSubmitTask::fence(age) and that
    // fence's state (FrameCapture::afterRecord fills it; the tests fill it from a fake ring).
    struct FrameFence
    {
        bool inRing = false; // fence(age) is not null: age is below the ring's length
        bool submitted = false; // Fence::hasDependencies
        VkResult status = VK_NOT_READY; // vkGetFenceStatus, read only when submitted
    };

    /*
     * The bookkeeping of one capture, without Vulkan (OpenEden, 2026-11-04, third round; tested in testframecapture.cpp
     * against a fake fence ring): the request waiting for a frame, the item armed for the frame being recorded, and
     * the items whose frames are in flight, oldest first. Item is whatever the caller keeps with a request
     * (FrameCapture: the callbacks, then the screenshot and its copy commands). Main thread only.
     *
     * Per recorded frame the caller does: decide(), and on Arm takeWaiting() then armed() (or drops the item when the
     * copy cannot be made); recordAndSubmit; afterRecord(), once, whatever happened before.
     */
    template <class Item>
    class CaptureQueue
    {
    public:
        struct Collected
        {
            std::vector<Item> done; // finished copies, oldest first
            std::vector<Item> lost; // frames that never finished
            bool armedThisFrame = false; // the caller empties what it recorded the copy with
        };

        explicit CaptureQueue(unsigned maxWaitFrames)
            : mMaxWaitFrames(maxWaitFrames)
        {
        }

        // A request for the next recorded frame. One already waiting is the same picture: the new one is merged with
        // it (false, and item is not kept).
        bool request(Item item)
        {
            if (mWaiting)
                return false;
            mWaiting.emplace(std::move(item));
            mFramesWaited = 0;
            return true;
        }
        bool waiting() const { return mWaiting.has_value(); }
        unsigned framesWaited() const { return mFramesWaited; }

        // Before recording a frame, for the waiting request (None when there is none): Arm (take it and arm it), Wait
        // (counted) or Drop (take it and report it).
        std::optional<ArmDecision> decide(bool imageAcquired, VkExtent2D extent)
        {
            if (!mWaiting)
                return std::nullopt;
            const ArmDecision decision = decideArm(imageAcquired, extent, mFramesWaited, mMaxWaitFrames);
            if (decision == ArmDecision::Wait)
                ++mFramesWaited;
            return decision;
        }
        Item takeWaiting()
        {
            Item item = std::move(*mWaiting);
            mWaiting.reset();
            return item;
        }
        // The copy of item is recorded into the frame about to be submitted.
        void armed(Item item) { mArmed.emplace(std::move(item)); }

        // After recordAndSubmit, once per recorded frame: every item in flight is a frame older, the armed one joins
        // them at age 0, and the items whose frames have finished (fenceOf(age), a FrameFence) or never will are
        // handed back, oldest first; the first still in flight stops the search, since later ones are in later frames.
        template <class FenceOf>
        Collected afterRecord(FenceOf&& fenceOf)
        {
            Collected collected;
            for (Entry& entry : mInFlight)
                ++entry.age;
            if (mArmed)
            {
                mInFlight.push_back(Entry{ std::move(*mArmed), 0 });
                mArmed.reset();
                collected.armedThisFrame = true;
            }
            while (!mInFlight.empty())
            {
                const FrameFence fence = fenceOf(mInFlight.front().age);
                const CopyState state = copyState(fence.inRing, fence.submitted, fence.status);
                if (state == CopyState::InFlight)
                    break;
                (state == CopyState::Done ? collected.done : collected.lost)
                    .push_back(std::move(mInFlight.front().item));
                mInFlight.pop_front();
            }
            return collected;
        }

        // After the device is idle (the end of the frame loop): every item in flight is done, oldest first; the one
        // armed but never recorded and the one still waiting are given back as dropped.
        std::pair<std::vector<Item>, std::vector<Item>> finish()
        {
            std::vector<Item> done;
            std::vector<Item> dropped;
            for (Entry& entry : mInFlight)
                done.push_back(std::move(entry.item));
            mInFlight.clear();
            if (mArmed)
                dropped.push_back(std::move(*mArmed));
            mArmed.reset();
            if (mWaiting)
                dropped.push_back(std::move(*mWaiting));
            mWaiting.reset();
            return { std::move(done), std::move(dropped) };
        }

        std::size_t inFlight() const { return mInFlight.size(); }

    private:
        struct Entry
        {
            Item item;
            unsigned age = 0;
        };

        unsigned mMaxWaitFrames;
        std::optional<Item> mWaiting;
        unsigned mFramesWaited = 0;
        std::optional<Item> mArmed;
        std::deque<Entry> mInFlight;
    };

    /*
     * A value that a callback hands back to the code that asked for it, where the callback can outlive that code
     * (OpenEden, 2026-11-04, round four, finding 1). The callback holds a share of the value, so it stays valid after
     * the asker has returned: Engine::drawFrameAndCapture used to capture a local by reference, and if the frame threw
     * after the copy was armed, the armed item stayed in the CaptureQueue with a callback that wrote to a dead stack
     * slot when a later frame finished it. Main thread only.
     */
    template <class T>
    class ResultSlot
    {
    public:
        ResultSlot()
            : mValue(std::make_shared<T>())
        {
        }

        // The callback to give to the queue: stores its argument. Valid for as long as it exists, whatever happens to
        // this slot.
        std::function<void(T)> sink() const
        {
            return [value = mValue](T result) { *value = std::move(result); };
        }

        // What the sink received (a default T when it was never called), and empties the slot.
        T take()
        {
            T result = std::move(*mValue);
            *mValue = T{};
            return result;
        }

    private:
        std::shared_ptr<T> mValue;
    };

    enum class DownloadSync;

    /*
     * One kind of copy of a window's image, owned by Render::Engine (Engine::addFrameCapture). The Vulkan side of
     * CaptureQueue: all calls on the main thread, beforeRecord and afterRecord once per recorded frame (Engine::loop,
     * or Engine::drawFrameAndCapture for an editor view).
     */
    class FrameCapture
    {
    public:
        // The copy, finished: its pixels are read by Screenshot::operate (or readDownloadedImage on the copy).
        using Done = std::function<void(vsg::ref_ptr<Screenshot>)>;
        // No copy: the request was dropped (no image for sMaxWaitFrames frames, the budget's cap reached, a copy that
        // could not be made, the frame loop ended) or its frame never finished. Each logs its reason first.
        using Failed = std::function<void()>;
        // A request with no image to copy waits this many recorded frames, then is dropped.
        static constexpr unsigned sMaxWaitFrames = 60;

        FrameCapture(ScreenshotCopyBudget& budget, DownloadSync sync);
        ~FrameCapture();

        // Appends the switch that holds the copy to graph's children; everything graph records before it, and every
        // graph of a lower submit order, is in the picture.
        void attach(vsg::ref_ptr<vsg::CommandGraph> graph);
        bool attached() const { return mGraph != nullptr; }

        // A request for the next frame that is recorded. done is called with the copied screenshot once that frame
        // has finished, failed otherwise (either may be empty). size is the picture's size (the swapchain's when
        // empty: a blit scales). A second request before the first was copied is the same picture and is merged with
        // it: its callbacks are dropped (false).
        bool request(Done done, Failed failed = {}, std::optional<VkExtent2D> size = {});

        // Before recordAndSubmit: puts the copy of the window's acquired image into the switch.
        void beforeRecord(vsg::Window& window, VkImageLayout presentLayout);
        // After recordAndSubmit: empties the switch, and hands every finished copy to its done.
        void afterRecord(vsg::Viewer& viewer);
        // After the loop's last deviceWaitIdle: every copy in flight is finished; a request still waiting, or armed but
        // never recorded, fails, and why (logged with it) says what ended the capture.
        void finish(std::string_view why = "the frame loop ended first");

    private:
        // The queue and its items, in framecapture.cpp: an item holds a Screenshot, which screenshot.hpp defines
        // after including this header.
        struct State;

        ScreenshotCopyBudget& mBudget;
        DownloadSync mSync;
        vsg::ref_ptr<vsg::CommandGraph> mGraph;
        vsg::ref_ptr<vsg::Switch> mSwitch;
        std::unique_ptr<State> mState;
    };
}

#endif
